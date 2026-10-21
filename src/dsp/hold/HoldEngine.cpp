#include "HoldEngine.h"

#include "Splice.h"

#include <algorithm>
#include <cmath>
#include <memory>

namespace hold
{
namespace
{

constexpr double kLn2Over1200 = 0.000577622650466621;
// The fastest a grain may read at the key's own pitch (the bar up 300 cents, plus SPREAD, with room
// to spare): held material is laid out so that a grain at this speed still has enough of it.
constexpr double kMaxRatio = 1.6;
constexpr double kPi = 3.14159265358979323846;
constexpr double kPickDelaySeconds = 0.03;  // a capture starts past the pick's transient
constexpr double kPickRecentSeconds = 0.15; // FREEZE this soon after a pick takes that pick's chord
constexpr int kMidiRange = 24;              // MIDI: keys up to two octaves above ROOT

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

// YIN's cumulative mean normalised difference over `window` samples of x for lags up to `maxLag`;
// the period (in samples, with parabolic interpolation) of the first dip below `threshold`, or 0.
double yinPeriod(const float* x, int window, int minLag, int maxLag, float threshold)
{
    std::array<float, 512> d {};
    maxLag = std::min(maxLag, (int) d.size() - 2);
    double running = 0.0;
    d[0] = 1.0f;
    for (int tau = 1; tau <= maxLag + 1; ++tau)
    {
        double sum = 0.0;
        for (int j = 0; j < window; ++j)
        {
            const double diff = (double) x[j] - x[j + tau];
            sum += diff * diff;
        }
        running += sum;
        d[(size_t) tau] = running > 0.0 ? (float) (sum * tau / running) : 1.0f;
    }
    for (int tau = std::max(2, minLag); tau <= maxLag; ++tau)
        if (d[(size_t) tau] < threshold)
        {
            while (tau + 1 <= maxLag && d[(size_t) tau + 1] < d[(size_t) tau])
                ++tau;
            const double a = d[(size_t) tau - 1], b = d[(size_t) tau], c = d[(size_t) tau + 1];
            const double denom = a - 2.0 * b + c;
            return tau + (std::abs(denom) > 1e-12 ? 0.5 * (a - c) / denom : 0.0);
        }
    return 0.0;
}

} // namespace

int detectNote(const float* samples, int count, double rate)
{
    // Down to about 12 kHz (a guitar's fundamental is below 1.4 kHz): cheap enough for the audio thread.
    const int factor = std::max(1, (int) std::lround(rate / 12000.0));
    const double r = rate / factor;
    std::array<float, 1600> x {};
    const int n = std::min((int) x.size(), count / factor);
    double energy = 0.0;
    for (int i = 0; i < n; ++i)
    {
        float sum = 0.0f;
        for (int k = 0; k < factor; ++k)
            sum += samples[i * factor + k];
        x[(size_t) i] = sum / (float) factor;
        energy += (double) x[(size_t) i] * x[(size_t) i];
    }
    const int minLag = (int) (r / 1400.0), maxLag = (int) std::ceil(r / 70.0); // 70 Hz - 1.4 kHz
    const int window = std::min(400, (n - maxLag - 2) / 2);
    if (window < 2 * maxLag || energy / std::max(1, n) < 1e-7) // too short, or silence (below -70 dB)
        return -1;
    // One clear note: two stretches, one after the other, both clearly periodic and at one pitch. A
    // chord beats and drifts, a double stop has no single period.
    const double first = yinPeriod(x.data(), window, minLag, maxLag, 0.1f);
    const double second = yinPeriod(x.data() + window, window, minLag, maxLag, 0.1f);
    if (first <= 0.0 || second <= 0.0 || std::abs(1200.0 * std::log2(first / second)) > 30.0)
        return -1;
    const double freq = r / (0.5 * (first + second));
    const int note = (int) std::lround(69.0 + 12.0 * std::log2(freq / 440.0));
    return note >= 24 && note <= 96 ? note : -1;
}

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
    levelCoeff = coeffForSeconds(0.02, rate);
    lowpassCoeff = (float) std::min(1.0, 1.0 - std::exp(-2.0 * kPi * 12000.0 / rate));
    liveColumnLength = std::max(1, (int) (HoldDisplay::kLiveSeconds * rate / HoldDisplay::kColumns));
    regionSamples = (int) (0.3 * rate);
    grainLength = (int) (0.18 * rate);
    smearSamples = 0;
    reset();
}

void HoldEngine::reset()
{
    std::fill(ring.begin(), ring.end(), 0.0f);
    now = 0;
    lastPickAt = -1;
    autoNote = -1;
    shown.autoRoot.store(-1);
    for (auto& slot : slots)
    {
        slot.length = slot.target = 0;
        slot.from = 0;
        slot.pickAt = -1;
        slot.readFrom = 2;
        slot.readFromEnd = slot.frozen = slot.freezeWhenDone = false;
        slot.decay = 1.0;
        slot.gain = 0.0f;
    }
    current = previous = recording = -1;
    wasFrozen = sustainDown = false;
    lastMode = Mode::Strike;
    finished = 0;
    for (auto& v : voices)
        v = Voice {};
    voiceOrder = 0;
    numNotes = 0;
    for (auto& g : grains)
        g.active = false;
    enabledMix = bendMix = levelGain = 0.0f;
    snapFeed = true;
    duckGain = 1.0f;
    env = 0.0f;
    loudest = 1e-3f;
    lowpass = 0.0f;
    liveColumn = liveCount = 0;
    liveMin = liveMax = 0.0f;
    peak = 0.0f;
    seed = 0x9e3779b9u;
    detector.reset();

    for (auto& v : shown.live)
        v.store(0.0f);
    shown.liveHead.store(0);
    for (int i = 0; i < HoldDisplay::kGrains; ++i)
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
    shown.frozen.store(false);
    for (auto& word : shown.notes)
        word.store(0);
}

int HoldEngine::activeGrains() const
{
    return (int) std::count_if(grains.begin(), grains.end(), [](const Grain& g) { return g.active; });
}

int HoldEngine::activeVoices() const
{
    return (int) std::count_if(voices.begin(), voices.end(), [](const Voice& v) { return v.active && v.env > 0.0f; });
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

// --- Captures ---

void HoldEngine::releaseGrains(int slot, int voice)
{
    for (auto& g : grains)
        if (g.active && (slot < 0 || g.source == slot) && (voice < 0 || g.voice == voice))
            g.releasing = true;
}

void HoldEngine::startRecording(int k, int64_t from, int64_t pickAt, bool readFromEnd, bool freezeWhenDone)
{
    auto& slot = slots[(size_t) k];
    // A grain still reading this slot (a fast run of picks) fades out quickly.
    releaseGrains(k, -1);
    slot.length = 0;
    slot.target = std::min(regionSamples, (int) slot.data.size() - 4);
    slot.from = from;
    slot.pickAt = pickAt;
    slot.readFrom = 2;
    slot.readFromEnd = readFromEnd;
    slot.decay = 1.0;
    slot.gain = 0.0f;
    slot.frozen = false;
    slot.freezeWhenDone = freezeWhenDone;
    recording = k;
}

void HoldEngine::finishRecording()
{
    auto& slot = slots[(size_t) recording];
    // A stretch up to now: grains read its newest part, with room before it for lining them up.
    if (slot.readFromEnd)
        slot.readFrom = std::max(2, slot.length - (int) std::ceil(grainLength * kMaxRatio) - smearSamples
                                        - (int) (0.03 * rate) - 4);
    slot.frozen = slot.freezeWhenDone;
    slot.freezeWhenDone = false;
    slot.gain = 0.0f; // comes in over SWELL
    findNote(slot);
    if (current >= 0 && current != recording)
    {
        // The one playing goes out over TAIL; one still going out from before goes at once.
        if (previous >= 0 && previous != current)
        {
            releaseGrains(previous, -1);
            slots[(size_t) previous].gain = 0.0f;
        }
        previous = current;
    }
    current = recording;
    recording = -1;
    ++finished;
    for (auto& v : voices)
    {
        v.lastGrain = -1; // the next grain lines up with nothing in the old sound
        v.spawnIn = 0;    // and comes at once
    }
    publishHeld(slot);
}

void HoldEngine::freezeNow(Mode mode)
{
    // STRIKE, MIDI: hold what plays (or the chord just picked, still being captured).
    if (mode == Mode::Strike || mode == Mode::Midi)
    {
        if (recording >= 0 && slots[(size_t) recording].pickAt >= 0)
        {
            slots[(size_t) recording].freezeWhenDone = true;
            return;
        }
        if (current >= 0)
        {
            slots[(size_t) current].frozen = true;
            return;
        }
    }
    // LIVE was not playing what was held; MANUAL let go and silent: a new hold, not the last one
    // coming back.
    if (current >= 0 && (mode == Mode::Live || (mode == Mode::Manual && voices[0].env < 0.01f)))
    {
        releaseGrains(current, -1);
        current = -1;
    }
    // Take the sound now: the chord of a pick just before (past its transient), else the stretch up to
    // this moment. The old sound plays on until it is in.
    const int k = recording >= 0 ? recording : freeSlot();
    const auto recent = (int64_t) (kPickRecentSeconds * rate);
    if (mode != Mode::Live && lastPickAt >= 0 && now - lastPickAt < recent)
        startRecording(k, lastPickAt + (int64_t) (kPickDelaySeconds * rate), lastPickAt, false, true);
    else
    {
        const int target = std::min(regionSamples, (int) slots[(size_t) k].data.size() - 4);
        startRecording(k, now + 1 - target, -1, true, true);
    }
}

int HoldEngine::freeSlot() const
{
    // Not one in use; of the others, the one fewest grains still read.
    int best = -1, fewest = kGrainPool + 1;
    for (int k = 0; k < (int) slots.size(); ++k)
    {
        if (k == current || k == previous || k == recording)
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

void HoldEngine::findNote(Slot& slot)
{
    // What the grains read, from where they start.
    const int from = std::clamp(slot.readFrom, 0, std::max(0, slot.length - 1));
    slot.note = detectNote(slot.data.data() + from, slot.length - from, rate);
    if (slot.note >= 0)
    {
        autoNote = slot.note;
        shown.autoRoot.store(autoNote);
    }
    slot.autoRoot = autoNote; // a chord keeps the last clear note
}

double HoldEngine::rootCents(const HoldSettings& s, int source) const
{
    if (s.mode != Mode::Midi || source < 0)
        return 0.0;
    const auto& slot = slots[(size_t) source];
    return 100.0 * (s.autoRoot && slot.autoRoot >= 0 ? slot.autoRoot : s.rootNote);
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

// --- The held sound, in and out ---

void HoldEngine::copyHeld(std::vector<float>& samples, float& readFrom) const
{
    if (current < 0)
    {
        samples.clear();
        readFrom = 0.0f;
        return;
    }
    const auto& slot = slots[(size_t) current];
    samples.resize((size_t) slot.length);
    std::copy_n(slot.data.begin(), slot.length, samples.begin());
    readFrom = slot.length > 0 ? (float) slot.readFrom / (float) slot.length : 0.0f;
}

void HoldEngine::copyRecent(std::vector<float>& samples, int count) const
{
    count = (int) std::clamp<int64_t>(count, 0, std::min<int64_t>(mask, now));
    samples.resize((size_t) count);
    for (int i = 0; i < count; ++i)
        samples[(size_t) i] = ring[(size_t) ((now - count + i) & mask)];
}

void HoldEngine::restoreHeld(const float* samples, int count, double sourceRate, float readFrom)
{
    // The sound comes from a project file: anything but a plausible rate holds nothing.
    if (!std::isfinite(sourceRate) || sourceRate < 1000.0 || sourceRate > 1.0e6)
        return;
    if (!std::isfinite(readFrom))
        readFrom = 0.0f;
    const int capacity = (int) slots[0].data.size() - 4;
    const int n = (int) std::clamp<double>(std::round(count * rate / std::max(1.0, sourceRate)), 0.0, capacity);
    if (samples == nullptr || count < 4 || n < 4)
        return;
    const int k = freeSlot();
    auto& slot = slots[(size_t) k];
    releaseGrains(-1, -1);
    const double step = sourceRate / rate;
    for (int i = 0; i < n; ++i)
    {
        const double pos = i * step;
        const int i0 = std::clamp((int) pos, 0, count - 1);
        const int i1 = std::min(count - 1, i0 + 1);
        const float t = (float) (pos - i0);
        slot.data[(size_t) i] = samples[i0] + t * (samples[i1] - samples[i0]);
    }
    slot.length = slot.target = n;
    slot.from = 0;
    slot.pickAt = -1;
    slot.readFromEnd = slot.freezeWhenDone = false;
    slot.readFrom = std::clamp((int) std::lround(readFrom * (float) n), 2, std::max(2, n - 3));
    slot.decay = 1.0;
    slot.gain = 1.0f;
    slot.frozen = true;
    findNote(slot);
    previous = recording = -1;
    current = k;
    wasFrozen = true;
    for (auto& v : voices)
    {
        v.lastGrain = -1;
        v.spawnIn = 0;
    }
    publishHeld(slot);
}

// --- MIDI ---

void HoldEngine::takeMidi(const HoldSettings& s, int upTo, int& index)
{
    while (index < s.numMidi && s.midi != nullptr && s.midi[index].sample <= upTo)
    {
        const auto& e = s.midi[(size_t) index++];
        if (e.kind == MidiEvent::Kind::Sustain)
        {
            sustainDown = e.on;
            if (!e.on)
                for (auto& v : voices)
                    v.sustained = false;
            continue;
        }
        if (e.on)
        {
            shown.lastNoteOn.store(e.note);
            shown.noteOns.fetch_add(1);
        }
        if (s.mode != Mode::Midi)
            continue;
        if (e.on)
            noteOn(s, e.note, e.velocity);
        else
            noteOff(s, e.note);
    }
}

void HoldEngine::startVoice(Voice& v, int note, double cents, float velocity)
{
    const int index = (int) (&v - voices.data());
    if (v.active)
    {
        releaseGrains(-1, index); // a voice taken over: its grains fade out quickly
        v.env = 0.0f;
    }
    v.active = true;
    v.note = note;
    v.cents = v.target = cents;
    v.glideLeft = 0;
    v.velocity = velocity;
    v.held = true;
    v.sustained = false;
    v.spawnIn = 0;
    v.lastGrain = -1;
    v.order = ++voiceOrder;
}

void HoldEngine::noteOn(const HoldSettings& s, int note, float velocity)
{
    if (note > rootFor(s) + kMidiRange)
        return;
    const double cents = 100.0 * note; // the key; each held sound is played against its own ROOT
    const float gain = 0.4f + 0.6f * std::clamp(velocity, 0.0f, 1.0f); // soft keys quieter, never silent
    if (s.voicing == Voicing::Poly)
    {
        // The same key again takes its voice; else a free one; else the oldest let go; else the oldest.
        Voice* pick = nullptr;
        for (auto& v : voices)
            if (pick == nullptr && v.active && v.note == note)
                pick = &v;
        for (auto& v : voices)
            if (pick == nullptr && !v.active)
                pick = &v;
        if (pick == nullptr)
            for (auto& v : voices)
                if (!v.held && !v.sustained && (pick == nullptr || v.order < pick->order))
                    pick = &v;
        if (pick == nullptr)
            for (auto& v : voices)
                if (pick == nullptr || v.order < pick->order)
                    pick = &v;
        startVoice(*pick, note, cents, gain);
        return;
    }

    // MONO, GLIDE: one voice, playing the newest key held.
    for (int i = 0; i < numNotes; ++i)
        if (notes[(size_t) i] == note)
        {
            std::copy(notes.begin() + i + 1, notes.begin() + numNotes, notes.begin() + i);
            --numNotes;
            break;
        }
    if (numNotes == (int) notes.size())
    {
        std::copy(notes.begin() + 1, notes.end(), notes.begin());
        --numNotes;
    }
    notes[(size_t) numNotes++] = note;

    auto& v = voices[0];
    if (!(v.active && (v.held || v.sustained) && v.env > 0.0f))
    {
        startVoice(v, note, cents, gain);
        return;
    }
    // Legato: the sound goes on at the new pitch.
    v.note = note;
    v.target = cents;
    v.held = true;
    if (s.voicing == Voicing::Glide)
    {
        v.glideLeft = std::max(1, (int) (s.glideMs * 0.001 * rate));
        v.glideStep = (v.target - v.cents) / v.glideLeft;
    }
    else
    {
        v.cents = v.target;
        v.glideLeft = 0;
    }
}

void HoldEngine::noteOff(const HoldSettings& s, int note)
{
    if (s.voicing == Voicing::Poly)
    {
        for (auto& v : voices)
            if (v.active && v.note == note && v.held)
            {
                v.held = false;
                v.sustained = sustainDown;
            }
        return;
    }
    for (int i = 0; i < numNotes; ++i)
        if (notes[(size_t) i] == note)
        {
            std::copy(notes.begin() + i + 1, notes.begin() + numNotes, notes.begin() + i);
            --numNotes;
            break;
        }
    auto& v = voices[0];
    if (numNotes > 0)
    {
        // Back to the key still held, legato.
        const int top = notes[(size_t) numNotes - 1];
        if (top != v.note && v.active)
        {
            v.note = top;
            v.target = 100.0 * top;
            if (s.voicing == Voicing::Glide)
            {
                v.glideLeft = std::max(1, (int) (s.glideMs * 0.001 * rate));
                v.glideStep = (v.target - v.cents) / v.glideLeft;
            }
            else
                v.cents = v.target;
        }
        return;
    }
    if (v.note == note)
    {
        v.held = false;
        v.sustained = sustainDown;
    }
}

void HoldEngine::publishNotes()
{
    std::array<uint64_t, 2> bits {};
    for (const auto& v : voices)
        if (v.active && (v.held || v.sustained) && v.note >= 0 && v.note < 128)
            bits[(size_t) (v.note >> 6)] |= uint64_t { 1 } << (v.note & 63);
    shown.notes[0].store(bits[0]);
    shown.notes[1].store(bits[1]);
}

// --- Grains ---

void HoldEngine::spawn(const HoldSettings& s, int vi, int source, int length, int smear)
{
    int index = -1;
    for (int i = 0; i < kGrainPool; ++i)
        if (!grains[(size_t) i].active)
        {
            index = i;
            break;
        }
    if (index < 0)
        return;
    auto& v = voices[(size_t) vi];
    auto& g = grains[(size_t) index];
    const float spread = std::clamp(s.spreadCents, 0.0f, 50.0f);
    g.length = length;
    g.age = 0;
    g.voice = vi;
    g.releasing = false;
    g.release = 1.0f;
    g.detuneRatio = (float) std::exp((2.0f * uniform() - 1.0f) * spread * kLn2Over1200);
    const float pan = (2.0f * uniform() - 1.0f) * std::min(1.0f, spread / 25.0f);
    g.panL = std::min(1.0f, 1.0f - pan);
    g.panR = std::min(1.0f, 1.0f + pan);
    g.reversed = uniform() < std::clamp(s.reverse, 0.0f, 1.0f);
    const float jitter = uniform();
    // A grain of the sound going out lines up with nothing; one of the sound that plays, with the last.
    const bool tracked = source < 0 || source == current;

    if (source < 0)
    {
        // Behind the newest sample by enough that a grain reading fast never catches up with it.
        const int behind = (int) (length * (kMaxRatio - 1.0)) + 64;
        g.pos = g.reversed ? (double) (now - 64) - jitter * smear : (double) (now - behind) - jitter * smear;
    }
    else
    {
        const auto& slot = slots[(size_t) source];
        // Enough material for the key's pitch too (in MIDI a key up an octave reads twice as fast).
        const double keyRatio = std::exp((std::max(v.cents, v.target) - rootCents(s, source)) * kLn2Over1200);
        const int span = (int) std::ceil(length * kMaxRatio * keyRatio) + 4;
        // Grains start from where the sound is read (SMEAR wanders on from there), a sample or two in
        // so that interpolation has a sample before them.
        const int usable = std::max(3, slot.length - span);
        const int base = std::clamp(slot.readFrom, 2, std::max(2, usable - 1));
        const int room = std::max(0, usable - 1 - base);
        const int anchor = base + (int) (jitter * (float) std::min(smear, room));
        if (g.reversed)
            g.pos = std::min((double) slot.length - 3.0, (double) (anchor + span - 4));
        else
        {
            g.pos = anchor;
            // Held without smear, the grains are a loop of one stretch: start each where it lines up
            // with the one playing, or the overlap combs.
            const auto& prev = grains[(size_t) std::max(0, v.lastGrain)];
            if (tracked && smear < (int) (0.02 * rate) && v.lastGrain >= 0 && prev.active && prev.source == source
                && !prev.reversed)
            {
                const int last = slot.length - 1;
                const auto at = [&slot, last](int i) { return slot.data[(size_t) std::clamp(i, 0, last)]; };
                const int reference = (int) std::lround(prev.pos);
                const auto found = sway::findSplice(at, reference, anchor, 2, usable - 1,
                                                    (int) (0.0125 * rate), (int) (0.01 * rate),
                                                    std::max(1, (int) std::lround(rate / 12000.0)), false);
                g.pos = found.start + (prev.pos - reference);
            }
        }
    }
    g.source = source;
    g.active = true;
    if (tracked)
        v.lastGrain = index;
}

// --- Processing ---

void HoldEngine::process(const float* dry, const float* barCents, int numSamples, const HoldSettings& s,
                         float* toGate, float* outL, float* outR)
{
    const int length = std::clamp((int) std::lround(std::clamp(s.sizeMs, 20.0f, 500.0f) * 0.001 * rate), 16,
                                  (int) (0.5 * rate) + 1);
    const int density = std::clamp(s.density, 1, 8);
    const int smear = (int) std::lround(std::clamp(s.smearMs, 0.0f, 1000.0f) * 0.001 * rate);
    const int capacity = (int) slots[0].data.size() - 4;
    const bool midiMode = s.mode == Mode::Midi;
    // In MIDI a capture holds enough for a key two octaves up.
    const double maxRatio = kMaxRatio * (midiMode ? std::exp2(kMidiRange / 12.0) : 1.0);
    regionSamples = std::clamp((int) (length * maxRatio) + smear + (int) (0.05 * rate), (int) (0.3 * rate), capacity);
    grainLength = length;
    smearSamples = smear;
    const bool clean = s.character == Character::Clean;
    detector.setSensitivity(s.sensitivity);
    // DECAY: the time a held sound takes to fall 60 dB. In double: so close to 1, a float multiplier
    // would drift from the time asked for.
    const double decayMul = std::isfinite(s.decaySeconds) && s.decaySeconds > 0.0f
                                ? std::exp(-std::log(1000.0) / ((double) s.decaySeconds * rate))
                                : 1.0;
    const float norm = 1.0f / std::sqrt(0.5f * (float) density);
    const float enabledTarget = s.enabled ? 1.0f : 0.0f;
    const float bendTarget = s.bend ? 1.0f : 0.0f;
    const float feedTarget = s.feed == Feed::Out ? 1.0f : 0.0f;
    if (snapFeed)
    {
        feedMix = feedTarget;
        snapFeed = false;
    }
    const float duck = std::clamp(s.duck, 0.0f, 1.0f);
    const float level = std::clamp(s.level, 0.0f, 1.0f);
    // SWELL in, TAIL out: the voices, and a new capture over the old one.
    const float attack = (float) (1.0 / (std::max(0.001, s.swellMs * 0.001) * rate));
    const float release = (float) (1.0 / (std::max(0.001, s.tailMs * 0.001) * rate));
    const int interval = std::max(1, length / density);
    int midiIndex = 0;

    // A new mode starts with no keys down; out of MIDI, voice 0 plays whatever the mode holds.
    if (s.mode != lastMode)
    {
        for (auto& v : voices)
            v.held = v.sustained = false;
        numNotes = 0;
        lastMode = s.mode;
    }
    if (!midiMode)
    {
        auto& v = voices[0];
        v.note = -1;
        v.cents = v.target = 0.0;
        v.glideLeft = 0;
        v.velocity = 1.0f;
    }

    if (!s.enabled && enabledMix <= 0.0f)
    {
        // Off: keep listening (the buffer, the detector, the loudness) so that turning it on finds
        // everything current, but play nothing.
        for (int n = 0; n < numSamples; ++n)
        {
            takeMidi(s, n, midiIndex);
            const float x = dry[n];
            const float c = capture(x, s.character);
            ring[(size_t) (now & mask)] = c;
            pushLiveColumn(c);
            if (detector.detect(x))
                lastPickAt = now;
            const float ax = std::abs(x);
            env += (ax > env ? envAttack : envRelease) * (ax - env);
            loudest = std::max({ loudest * loudestFall, env, 1e-3f });
            ++now;
            toGate[n] = outL[n] = outR[n] = 0.0f;
        }
        takeMidi(s, numSamples, midiIndex);
        for (auto& g : grains)
            g.active = false;
        for (auto& v : voices)
        {
            v.env = 0.0f;
            v.active = v.held || v.sustained;
        }
        for (int i = 0; i < HoldDisplay::kGrains; ++i)
            shown.grainPos[(size_t) i].store(-1.0f);
        shown.holding.store(false);
        wasFrozen = s.frozen || (sustainDown && !midiMode);
        shown.frozen.store(false);
        publishNotes();
        return;
    }

    int mainSource = current;
    for (int n = 0; n < numSamples; ++n)
    {
        takeMidi(s, n, midiIndex);
        const bool frozenNow = s.frozen || (sustainDown && !midiMode);
        const float x = dry[n];
        const float c = capture(x, s.character);
        ring[(size_t) (now & mask)] = c;
        pushLiveColumn(c);
        const bool picked = detector.detect(x);
        if (picked)
            lastPickAt = now;

        // FREEZE: hold the sound now and stop listening; let go, and listen again.
        if (frozenNow != wasFrozen)
        {
            if (frozenNow)
                freezeNow(s.mode);
            else
            {
                if (current >= 0)
                    slots[(size_t) current].frozen = false;
                if (recording >= 0)
                    slots[(size_t) recording].freezeWhenDone = false;
            }
            wasFrozen = frozenNow;
        }

        // A pick starts a new chord a moment later, past its transient. One coming while the last chord
        // is still going in: if that one has enough for a grain, it is kept, else this one replaces it.
        if (picked && (s.mode == Mode::Strike || midiMode) && !frozenNow)
        {
            if (recording >= 0 && slots[(size_t) recording].pickAt >= 0
                && slots[(size_t) recording].length >= grainLength)
                finishRecording();
            startRecording(recording >= 0 ? recording : freeSlot(), now + (int64_t) (kPickDelaySeconds * rate), now,
                           false, false);
        }

        // Recording: what the ring holds already is copied a few samples at a time (done well before
        // the ring comes round to it), what is still to come one sample per sample.
        if (recording >= 0)
        {
            auto& slot = slots[(size_t) recording];
            for (int j = 0; j < 16 && slot.length < slot.target && slot.from + slot.length <= now; ++j, ++slot.length)
                slot.data[(size_t) slot.length] = ring[(size_t) ((slot.from + slot.length) & mask)];
            if (slot.length >= slot.target)
                finishRecording();
        }

        // The sound that plays comes in over SWELL, the one it replaced goes out over TAIL.
        if (current >= 0)
            slots[(size_t) current].gain = std::min(1.0f, slots[(size_t) current].gain + attack);
        if (previous >= 0)
        {
            auto& gone = slots[(size_t) previous];
            gone.gain -= release;
            if (gone.gain <= 0.0f)
            {
                gone.gain = 0.0f;
                releaseGrains(previous, -1);
                previous = -1;
            }
        }

        // What the grains read: LIVE the passing sound (or what FREEZE took of it), the others the held one.
        const bool liveHeld = s.mode == Mode::Live && frozenNow && current >= 0 && slots[(size_t) current].frozen;
        mainSource = s.mode == Mode::Live && !liveHeld ? -1 : current;
        const bool hasSource = s.mode == Mode::Live || current >= 0;
        if (!midiMode)
        {
            bool open = false;
            switch (s.mode)
            {
                case Mode::Strike: open = current >= 0; break;
                case Mode::Manual: open = frozenNow && current >= 0; break;
                case Mode::Live: open = true; break;
                case Mode::Midi: break;
            }
            voices[0].held = open;
        }

        // DUCK: down while the guitar is as loud as it has been lately, back up as it dies away.
        const float ax = std::abs(x);
        env += (ax > env ? envAttack : envRelease) * (ax - env);
        loudest = std::max({ loudest * loudestFall, env, 1e-3f });
        duckGain += duckCoeff * (1.0f - duck * std::min(1.0f, env / loudest) - duckGain);

        enabledMix += std::clamp(enabledTarget - enabledMix, -rampStep, rampStep);
        bendMix += std::clamp(bendTarget - bendMix, -rampStep, rampStep);
        feedMix += std::clamp(feedTarget - feedMix, -rampStep, rampStep);
        levelGain += levelCoeff * (level - levelGain);
        const double bar = (double) barCents[n] * bendMix;

        for (int vi = 0; vi < kMaxVoices; ++vi)
        {
            auto& v = voices[(size_t) vi];
            const bool open = (v.held || v.sustained) && hasSource;
            v.env += std::clamp((open ? 1.0f : 0.0f) - v.env, -release, attack);
            if (v.glideLeft > 0)
            {
                --v.glideLeft;
                v.cents = v.glideLeft == 0 ? v.target : v.cents + v.glideStep;
            }
            v.active = v.held || v.sustained || v.env > 0.0f;
            if (!v.active)
                continue;
            v.ratio = std::exp((bar + v.cents - rootCents(s, mainSource)) * kLn2Over1200);
            if (previous >= 0)
                v.ratioPrevious = std::exp((bar + v.cents - rootCents(s, previous)) * kLn2Over1200);
            if (hasSource && (open || v.env > 1e-4f) && --v.spawnIn <= 0)
            {
                spawn(s, vi, mainSource, length, smear);
                if (previous >= 0 && mainSource >= 0)
                    spawn(s, vi, previous, length, smear);
                // Held without smear the grains are a loop: evenly spaced, so their windows add up to a
                // steady level. With smear they read different places: a little irregular, so they do
                // not beat at their own rate.
                const float jitter = smear >= (int) (0.02 * rate) ? 0.2f : 0.0f;
                v.spawnIn = std::max(1, (int) std::lround(interval * (1.0f + jitter * (2.0f * uniform() - 1.0f))));
            }
        }

        float sumL = 0.0f, sumR = 0.0f;
        for (auto& g : grains)
        {
            if (!g.active)
                continue;
            const auto& v = voices[(size_t) g.voice];
            const float phase = (float) g.age / (float) g.length * (float) kWindow;
            const int wi = std::min(kWindow - 1, (int) phase);
            const float w = window[(size_t) wi] + (phase - (float) wi) * (window[(size_t) wi + 1] - window[(size_t) wi]);
            float val;
            if (g.source >= 0)
            {
                const auto& slot = slots[(size_t) g.source];
                val = readSlot(slot, g.pos, clean) * (float) slot.decay * slot.gain;
                if (g.pos < 1.0 || g.pos > (double) slot.length - 3.0)
                    g.releasing = true; // out of material
            }
            else
            {
                val = readRing(g.pos, clean);
                if (g.pos > (double) (now - 2) || g.pos < (double) (now - mask + 4))
                    g.releasing = true;
            }
            val *= w * g.release * v.env * v.velocity;
            sumL += val * g.panL;
            sumR += val * g.panR;

            g.pos += (g.reversed ? -1.0 : 1.0) * (g.source >= 0 && g.source == previous ? v.ratioPrevious : v.ratio)
                     * g.detuneRatio;
            if (g.releasing && (g.release -= releaseStep) <= 0.0f)
                g.active = false;
            if (++g.age >= g.length)
                g.active = false;
        }
        // What is held dies away over DECAY; what is frozen stays.
        for (auto& slot : slots)
            if (!slot.frozen)
                slot.decay *= decayMul;

        const float gain = norm * levelGain * duckGain * enabledMix;
        const float l = sumL * gain, r = sumR * gain;
        toGate[n] = 0.5f * (l + r) * (1.0f - feedMix);
        outL[n] = l * feedMix;
        outR[n] = r * feedMix;
        peak = std::max({ peak, std::abs(l), std::abs(r) });
        ++now;
    }
    takeMidi(s, numSamples, midiIndex);

    if (enabledMix <= 0.0f)
        for (auto& g : grains)
            g.active = false;

    // For the screen: the grains of what plays now.
    const double liveWindow = HoldDisplay::kLiveSeconds * rate;
    int shownGrains = 0;
    for (const auto& g : grains)
    {
        if (shownGrains == HoldDisplay::kGrains)
            break;
        if (!g.active || g.source != mainSource)
            continue;
        float pos;
        if (g.source >= 0)
            pos = (float) (g.pos / std::max(1, slots[(size_t) g.source].length));
        else
            pos = (float) (1.0 - (double) (now - g.pos) / liveWindow);
        const float amp = window[(size_t) std::min(kWindow, g.age * kWindow / std::max(1, g.length))] * g.release
                          * voices[(size_t) g.voice].env;
        shown.grainPos[(size_t) shownGrains].store(pos);
        shown.grainAmp[(size_t) shownGrains].store(amp);
        ++shownGrains;
    }
    for (int i = shownGrains; i < HoldDisplay::kGrains; ++i)
    {
        shown.grainPos[(size_t) i].store(-1.0f);
        shown.grainAmp[(size_t) i].store(0.0f);
    }
    shown.holding.store(s.mode == Mode::Live || current >= 0);
    shown.frozen.store(wasFrozen);
    publishNotes();
    if (peak > shown.level.load())
        shown.level.store(peak);
    peak = 0.0f;
}

void renderHeld(const float* samples, int count, double rate, float readFrom, HoldSettings s, float* outL,
                float* outR, int numSamples)
{
    auto engine = std::make_unique<HoldEngine>();
    engine->prepare(rate);
    engine->restoreHeld(samples, count, rate, readFrom);
    s.enabled = true;
    s.mode = Mode::Manual;
    s.frozen = true;
    s.duck = 0.0f;
    s.bend = false;
    s.level = 1.0f;
    s.feed = Feed::Out;
    s.midi = nullptr;
    s.numMidi = 0;
    constexpr int kBlock = 512;
    std::vector<float> silence((size_t) kBlock, 0.0f), gate((size_t) kBlock);
    for (int start = 0; start < numSamples; start += kBlock)
    {
        const int n = std::min(kBlock, numSamples - start);
        engine->process(silence.data(), silence.data(), n, s, gate.data(), outL + start, outR + start);
    }
}

} // namespace hold
