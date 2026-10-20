#include "HoldEngine.h"

#include "Splice.h"

#include <algorithm>
#include <cmath>

namespace hold
{
namespace
{

constexpr double kLn2Over1200 = 0.000577622650466621;
// The fastest a grain may read (the bar up 300 cents at FOLLOW 200 %, plus SPREAD): held material
// is laid out so that a grain at this speed still has enough of it.
constexpr double kMaxRatio = 1.6;
constexpr double kPi = 3.14159265358979323846;

float coeffForSeconds(double seconds, double rate)
{
    return (float) (1.0 - std::exp(-1.0 / (std::max(seconds, 1e-5) * rate)));
}

// 4-point Hermite, as the glide's delay lines read.
template <typename At>
float hermite(const At& at, double pos)
{
    const double floorPos = std::floor(pos);
    const auto i = (int64_t) floorPos;
    const float t = (float) (pos - floorPos);
    const float y0 = at(i - 1), y1 = at(i), y2 = at(i + 1), y3 = at(i + 2);
    const float c1 = 0.5f * (y2 - y0);
    const float c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
    const float c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
    return ((c3 * t + c2) * t + c1) * t + y1;
}

template <typename At>
float linear(const At& at, double pos)
{
    const double floorPos = std::floor(pos);
    const auto i = (int64_t) floorPos;
    const float t = (float) (pos - floorPos);
    const float a = at(i);
    return a + t * (at(i + 1) - a);
}

} // namespace

void HoldEngine::prepare(double sampleRate)
{
    rate = sampleRate;
    detector.prepare(rate);
    int size = 1;
    while (size < (int) (kRingSeconds * rate))
        size <<= 1;
    ring.assign((size_t) size, 0.0f);
    mask = size - 1;
    for (auto& slot : slots)
        slot.data.assign((size_t) std::ceil(kSlotSeconds * rate) + 4, 0.0f);
    window.resize((size_t) kWindow + 1);
    for (int i = 0; i <= kWindow; ++i)
        window[(size_t) i] = (float) (0.5 - 0.5 * std::cos(2.0 * kPi * i / kWindow));

    rampStep = (float) (1.0 / (0.02 * rate));
    releaseStep = (float) (1.0 / (0.005 * rate));
    envAttack = coeffForSeconds(0.005, rate);
    envRelease = coeffForSeconds(0.3, rate);
    loudestFall = (float) std::exp(-1.0 / (3.0 * rate)); // the loudest lately: falls 8.7 dB in 3 s
    duckCoeff = coeffForSeconds(0.01, rate);
    gateAttack = (float) (1.0 / (0.01 * rate));
    gateRelease = (float) (1.0 / (0.2 * rate));
    levelCoeff = coeffForSeconds(0.02, rate);
    lowpassCoeff = (float) std::min(1.0, 1.0 - std::exp(-2.0 * kPi * 12000.0 / rate));
    liveColumnLength = std::max(1, (int) (HoldDisplay::kLiveSeconds * rate / HoldDisplay::kColumns));
    regionSamples = (int) (0.3 * rate);
    reset();
}

void HoldEngine::reset()
{
    std::fill(ring.begin(), ring.end(), 0.0f);
    now = 0;
    for (auto& slot : slots)
    {
        slot.length = slot.target = 0;
        slot.decay = 1.0f;
    }
    current = recording = -1;
    startIn = -1;
    copyFrom = -1;
    wasLatched = false;
    for (auto& g : grains)
        g.active = false;
    spawnIn = 0;
    lastGrain = -1;
    enabledMix = followMix = levelGain = gate = 0.0f;
    snapFeed = true;
    duckGain = 1.0f;
    env = 0.0f;
    loudest = 1e-3f;
    lowpass = 0.0f;
    numNotes = 0;
    noteCents = 0.0f;
    liveColumn = liveCount = 0;
    liveMin = liveMax = 0.0f;
    peak = 0.0f;
    seed = 0x9e3779b9u;
    detector.reset();

    for (auto& v : shown.live)
        v.store(0.0f);
    shown.liveHead.store(0);
    for (int i = 0; i < kVoices; ++i)
    {
        shown.grainPos[(size_t) i].store(-1.0f);
        shown.grainAmp[(size_t) i].store(0.0f);
    }
    const int index = 1 - shown.heldIndex.load();
    shown.held[(size_t) index].fill(0.0f);
    shown.heldIndex.store(index);
    shown.heldVersion.fetch_add(1);
    shown.heldSeconds.store(0.0f);
    shown.holding.store(false);
}

int HoldEngine::activeGrains() const
{
    return (int) std::count_if(grains.begin(), grains.end(), [](const Grain& g) { return g.active; });
}

float HoldEngine::uniform()
{
    seed ^= seed << 13;
    seed ^= seed >> 17;
    seed ^= seed << 5;
    return (float) (seed >> 8) / (float) (1u << 24); // 0 - 1
}

float HoldEngine::capture(float x, Character c)
{
    if (c == Character::Clean)
    {
        lowpass = x; // ready for a switch to 12 bits without a jump
        return x;
    }
    // 12 bits with triangular dither, behind a dull one-pole low-pass.
    lowpass += lowpassCoeff * (x - lowpass);
    const float dither = uniform() - uniform();
    const float q = std::round(lowpass * 2048.0f + dither) / 2048.0f;
    return std::clamp(q, -1.0f, 1.0f - 1.0f / 2048.0f);
}

void HoldEngine::startRecording(int k)
{
    auto& slot = slots[(size_t) k];
    // A grain still reading this slot (a fast run of picks) fades out quickly.
    for (auto& g : grains)
        if (g.active && g.source == k)
            g.releasing = true;
    slot.length = 0;
    slot.target = std::min(regionSamples, (int) slot.data.size() - 4);
    slot.decay = 1.0f;
    recording = k;
}

void HoldEngine::finishRecording()
{
    current = recording;
    recording = -1;
    copyFrom = -1;
    lastGrain = -1; // the next grain lines up with nothing in the old chord
    spawnIn = 0;    // and comes at once
    publishHeld(slots[(size_t) current]);
}

int HoldEngine::freeSlot() const
{
    // Not the one that plays; of the others, the one fewest grains still read.
    int best = -1, fewest = kVoices + 1;
    for (int k = 0; k < (int) slots.size(); ++k)
    {
        if (k == current)
            continue;
        const int reading = (int) std::count_if(grains.begin(), grains.end(),
                                                [k](const Grain& g) { return g.active && g.source == k; });
        if (reading < fewest)
        {
            fewest = reading;
            best = k;
        }
    }
    return best;
}

void HoldEngine::publishHeld(const Slot& slot)
{
    const int index = 1 - shown.heldIndex.load();
    auto& pairs = shown.held[(size_t) index];
    const int n = std::max(1, slot.length);
    for (int col = 0; col < HoldDisplay::kColumns; ++col)
    {
        const int a = col * n / HoldDisplay::kColumns, b = std::max(a + 1, (col + 1) * n / HoldDisplay::kColumns);
        float lo = 0.0f, hi = 0.0f;
        for (int i = a; i < b && i < slot.length; ++i)
        {
            lo = std::min(lo, slot.data[(size_t) i]);
            hi = std::max(hi, slot.data[(size_t) i]);
        }
        pairs[(size_t) (2 * col)] = lo;
        pairs[(size_t) (2 * col + 1)] = hi;
    }
    shown.heldIndex.store(index);
    shown.heldVersion.fetch_add(1);
    shown.heldSeconds.store((float) (slot.length / rate));
}

void HoldEngine::pushLiveColumn(float x)
{
    liveMin = std::min(liveMin, x);
    liveMax = std::max(liveMax, x);
    if (++liveCount < liveColumnLength)
        return;
    shown.live[(size_t) (2 * liveColumn)].store(liveMin);
    shown.live[(size_t) (2 * liveColumn + 1)].store(liveMax);
    liveColumn = (liveColumn + 1) % HoldDisplay::kColumns;
    shown.liveHead.store(liveColumn);
    liveCount = 0;
    liveMin = liveMax = 0.0f;
}

float HoldEngine::readSlot(const Slot& slot, double pos, bool clean) const
{
    const int last = std::max(0, slot.length - 1);
    const auto at = [&slot, last](int64_t i) { return slot.data[(size_t) std::clamp<int64_t>(i, 0, last)]; };
    return clean ? hermite(at, pos) : linear(at, pos);
}

float HoldEngine::readRing(double pos, bool clean) const
{
    const auto at = [this](int64_t i) { return ring[(size_t) (i & mask)]; };
    return clean ? hermite(at, pos) : linear(at, pos);
}

void HoldEngine::spawn(const HoldSettings& s, int length, int smear)
{
    int index = -1;
    for (int i = 0; i < kVoices; ++i)
        if (!grains[(size_t) i].active)
        {
            index = i;
            break;
        }
    if (index < 0)
        return;
    auto& g = grains[(size_t) index];
    const float spread = std::clamp(s.spreadCents, 0.0f, 50.0f);
    g.length = length;
    g.age = 0;
    g.releasing = false;
    g.release = 1.0f;
    g.detuneRatio = (float) std::exp((2.0f * uniform() - 1.0f) * spread * kLn2Over1200);
    const float pan = (2.0f * uniform() - 1.0f) * std::min(1.0f, spread / 25.0f);
    g.panL = std::min(1.0f, 1.0f - pan);
    g.panR = std::min(1.0f, 1.0f + pan);
    g.reversed = uniform() < std::clamp(s.reverse, 0.0f, 1.0f);
    const float jitter = uniform();

    if (s.mode == Mode::Live)
    {
        // Behind the newest sample by enough that a grain reading fast never catches up with it.
        g.source = -1;
        const int behind = (int) (length * (kMaxRatio - 1.0)) + 64;
        g.pos = g.reversed ? (double) (now - 64) - jitter * smear : (double) (now - behind) - jitter * smear;
    }
    else
    {
        const auto& slot = slots[(size_t) current];
        const int span = (int) std::ceil(length * kMaxRatio) + 4;
        // Grains start from the held chord's first moments (or SMEAR into it), a sample or two in so
        // that interpolation has a sample before them.
        const int usable = std::max(3, slot.length - span);
        int anchor = 2 + (int) (jitter * (float) std::min(smear, usable - 3));
        if (g.reversed)
            g.pos = std::min((double) slot.length - 3.0, (double) (anchor + span - 4));
        else
        {
            g.pos = anchor;
            // Held without smear, the grains are a loop of one stretch: start each where it lines up
            // with the one playing, or the overlap combs.
            const auto& prev = grains[(size_t) std::max(0, lastGrain)];
            if (smear < (int) (0.02 * rate) && lastGrain >= 0 && prev.active && prev.source == current && !prev.reversed)
            {
                const int last = slot.length - 1;
                const auto at = [&slot, last](int i) { return slot.data[(size_t) std::clamp(i, 0, last)]; };
                const int reference = (int) std::lround(prev.pos);
                const auto found = sway::findSplice(at, reference, anchor, 2, usable - 1, (int) (0.0125 * rate),
                                                    (int) (0.01 * rate), std::max(1, (int) std::lround(rate / 12000.0)),
                                                    false);
                g.pos = found.start + (prev.pos - reference);
            }
        }
        g.source = current;
    }
    g.active = true;
    lastGrain = index;
}

void HoldEngine::process(const float* dry, const float* barCents, int numSamples, const HoldSettings& s,
                         float* toGate, float* outL, float* outR)
{
    const int length = std::clamp((int) std::lround(std::clamp(s.sizeMs, 20.0f, 500.0f) * 0.001 * rate), 16,
                                  (int) (0.5 * rate) + 1);
    const int density = std::clamp(s.density, 1, 8);
    const int smear = (int) std::lround(std::clamp(s.smearMs, 0.0f, 1000.0f) * 0.001 * rate);
    const int capacity = (int) slots[0].data.size() - 4;
    regionSamples = std::clamp((int) (length * kMaxRatio) + smear + (int) (0.05 * rate), (int) (0.3 * rate), capacity);
    const bool clean = s.character == Character::Clean;
    detector.setSensitivity(s.sensitivity);
    const float decayMul = std::isfinite(s.decaySeconds) && s.decaySeconds > 0.0f
                               ? (float) std::exp(-1.0 / (s.decaySeconds * rate))
                               : 1.0f;
    const float norm = 1.0f / std::sqrt(0.5f * (float) density);
    const float enabledTarget = s.enabled ? 1.0f : 0.0f;
    const float followTarget = s.followBar ? 1.0f : 0.0f;
    const float feedTarget = s.feed == Feed::Out ? 1.0f : 0.0f;
    if (snapFeed)
    {
        feedMix = feedTarget;
        snapFeed = false;
    }
    const float duck = std::clamp(s.duck, 0.0f, 1.0f);
    const float follow = std::clamp(s.follow, 0.0f, 2.0f);
    const float level = std::clamp(s.level, 0.0f, 1.0f);
    const bool captures = s.mode == Mode::Strike || s.mode == Mode::Midi;
    const bool latchedNow = s.mode == Mode::Latch && s.latched;
    const int interval = std::max(1, length / density);
    int midiIndex = 0;

    auto takeMidi = [&](int n) {
        while (midiIndex < s.numMidi && s.midi != nullptr && s.midi[midiIndex].sample <= n)
        {
            const auto& e = s.midi[midiIndex++];
            // Take the note out of the stack wherever it is; a note-on puts it back on top.
            for (int i = 0; i < numNotes; ++i)
                if (notes[(size_t) i] == e.note)
                {
                    std::copy(notes.begin() + i + 1, notes.begin() + numNotes, notes.begin() + i);
                    --numNotes;
                    break;
                }
            if (e.on)
            {
                if (numNotes == (int) notes.size())
                {
                    std::copy(notes.begin() + 1, notes.end(), notes.begin());
                    --numNotes;
                }
                notes[(size_t) numNotes++] = e.note;
            }
            if (numNotes > 0)
                noteCents = 100.0f * (float) (notes[(size_t) numNotes - 1] - s.rootNote);
        }
    };

    if (!s.enabled && enabledMix <= 0.0f)
    {
        // Off: keep listening (the buffer, the detector, the loudness) so that turning it on finds
        // everything current, but play nothing.
        for (int n = 0; n < numSamples; ++n)
        {
            takeMidi(n);
            const float x = dry[n];
            const float c = capture(x, s.character);
            ring[(size_t) (now & mask)] = c;
            pushLiveColumn(c);
            detector.detect(x);
            const float ax = std::abs(x);
            env += (ax > env ? envAttack : envRelease) * (ax - env);
            loudest = std::max({ loudest * loudestFall, env, 1e-3f });
            ++now;
            toGate[n] = outL[n] = outR[n] = 0.0f;
        }
        takeMidi(numSamples);
        for (auto& g : grains)
            g.active = false;
        for (int i = 0; i < kVoices; ++i)
            shown.grainPos[(size_t) i].store(-1.0f);
        shown.holding.store(false);
        gate = 0.0f;
        wasLatched = latchedNow;
        return;
    }

    for (int n = 0; n < numSamples; ++n)
    {
        takeMidi(n);
        const float x = dry[n];
        const float c = capture(x, s.character);
        ring[(size_t) (now & mask)] = c;
        pushLiveColumn(c);

        // Capturing: a pick starts a new chord a moment later, past its transient; FREEZE takes the
        // last stretch out of the buffer.
        if (detector.detect(x) && captures)
        {
            if (recording < 0 || copyFrom >= 0)
                startRecording(freeSlot());
            else
                startRecording(recording); // a newer pick before the last chord was in: take this one
            copyFrom = -1;
            startIn = (int) (0.03 * rate);
        }
        if (latchedNow && !wasLatched)
        {
            startRecording(freeSlot());
            auto& slot = slots[(size_t) recording];
            slot.target = (int) std::min<int64_t>(slot.target, now + 1);
            copyFrom = now + 1 - slot.target;
        }
        wasLatched = latchedNow;
        if (recording >= 0)
        {
            auto& slot = slots[(size_t) recording];
            if (copyFrom >= 0)
            {
                // A few samples per sample: done well before the buffer comes round to them.
                for (int j = 0; j < 16 && slot.length < slot.target; ++j, ++slot.length)
                    slot.data[(size_t) slot.length] = ring[(size_t) ((copyFrom + slot.length) & mask)];
            }
            else if (startIn > 0)
                --startIn;
            else
                slot.data[(size_t) slot.length++] = c;
            if (slot.length >= slot.target)
                finishRecording();
        }

        float gateTarget = 0.0f;
        switch (s.mode)
        {
            case Mode::Strike: gateTarget = current >= 0 ? 1.0f : 0.0f; break;
            case Mode::Latch: gateTarget = latchedNow && current >= 0 && copyFrom < 0 ? 1.0f : 0.0f; break;
            case Mode::Live: gateTarget = 1.0f; break;
            case Mode::Midi: gateTarget = numNotes > 0 && current >= 0 ? 1.0f : 0.0f; break;
        }
        gate += std::clamp(gateTarget - gate, -gateRelease, gateAttack); // in quickly, out over 200 ms

        // DUCK: down while the guitar is as loud as it has been lately, back up as it dies away.
        const float ax = std::abs(x);
        env += (ax > env ? envAttack : envRelease) * (ax - env);
        loudest = std::max({ loudest * loudestFall, env, 1e-3f });
        duckGain += duckCoeff * (1.0f - duck * std::min(1.0f, env / loudest) - duckGain);

        enabledMix += std::clamp(enabledTarget - enabledMix, -rampStep, rampStep);
        followMix += std::clamp(followTarget - followMix, -rampStep, rampStep);
        feedMix += std::clamp(feedTarget - feedMix, -rampStep, rampStep);
        levelGain += levelCoeff * (level - levelGain);

        const double cents = (double) barCents[n] * follow * followMix + (s.mode == Mode::Midi ? noteCents : 0.0f);
        const double ratio = std::exp(cents * kLn2Over1200);

        const bool hasSource = s.mode == Mode::Live || current >= 0;
        if (hasSource && (gateTarget > 0.0f || gate > 1e-4f) && --spawnIn <= 0)
        {
            spawn(s, length, smear);
            // Held without smear the grains are a loop: evenly spaced, so their windows add up to a
            // steady level. With smear they read different places: a little irregular, so they do not
            // beat at their own rate.
            const float jitter = smear >= (int) (0.02 * rate) ? 0.2f : 0.0f;
            spawnIn = std::max(1, (int) std::lround(interval * (1.0f + jitter * (2.0f * uniform() - 1.0f))));
        }

        float sumL = 0.0f, sumR = 0.0f;
        for (auto& g : grains)
        {
            if (!g.active)
                continue;
            const float phase = (float) g.age / (float) g.length * (float) kWindow;
            const int wi = std::min(kWindow - 1, (int) phase);
            const float w = window[(size_t) wi] + (phase - (float) wi) * (window[(size_t) wi + 1] - window[(size_t) wi]);
            float v;
            if (g.source >= 0)
            {
                const auto& slot = slots[(size_t) g.source];
                v = readSlot(slot, g.pos, clean) * slot.decay;
                if (g.pos < 1.0 || g.pos > (double) slot.length - 3.0)
                    g.releasing = true; // out of material
            }
            else
            {
                v = readRing(g.pos, clean);
                if (g.pos > (double) (now - 2) || g.pos < (double) (now - mask + 4))
                    g.releasing = true;
            }
            v *= w * g.release;
            sumL += v * g.panL;
            sumR += v * g.panR;

            g.pos += (g.reversed ? -1.0 : 1.0) * ratio * g.detuneRatio;
            if (g.releasing && (g.release -= releaseStep) <= 0.0f)
                g.active = false;
            if (++g.age >= g.length)
                g.active = false;
        }
        for (auto& slot : slots)
            slot.decay *= decayMul;

        const float gain = norm * levelGain * duckGain * gate * enabledMix;
        const float l = sumL * gain, r = sumR * gain;
        toGate[n] = 0.5f * (l + r) * (1.0f - feedMix);
        outL[n] = l * feedMix;
        outR[n] = r * feedMix;
        peak = std::max({ peak, std::abs(l), std::abs(r) });
        ++now;
    }
    takeMidi(numSamples);

    if (enabledMix <= 0.0f)
        for (auto& g : grains)
            g.active = false;

    // For the screen.
    const double liveWindow = HoldDisplay::kLiveSeconds * rate;
    for (int i = 0; i < kVoices; ++i)
    {
        const auto& g = grains[(size_t) i];
        float pos = -1.0f, amp = 0.0f;
        if (g.active)
        {
            if (g.source >= 0)
                pos = (float) (g.pos / std::max(1, slots[(size_t) g.source].length));
            else
                pos = (float) (1.0 - (double) (now - g.pos) / liveWindow);
            amp = window[(size_t) std::min(kWindow, g.age * kWindow / std::max(1, g.length))] * g.release;
        }
        shown.grainPos[(size_t) i].store(pos);
        shown.grainAmp[(size_t) i].store(amp);
    }
    shown.holding.store(s.mode == Mode::Live || current >= 0);
    if (peak > shown.level.load())
        shown.level.store(peak);
    peak = 0.0f;
}

} // namespace hold
