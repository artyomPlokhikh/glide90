#include "PickDetector.h"
#include "SwayProcessor.h"
#include "TestUtil.h"

#include <doctest/doctest.h>

#include <cmath>
#include <ostream> // doctest prints string_view through it (MSVC does not pull it in)
#include <string>

using sway::BendDirection;
using sway::GlideMode;
using sway::SwayProcessor;
using sway::SwaySettings;
using test::db;

namespace
{

std::vector<float> run(std::vector<float> x, double rate, const SwaySettings& s)
{
    SwayProcessor p;
    p.prepare(rate);
    for (size_t i = 0; i < x.size(); i += 256)
        p.process(x.data() + i, (int) std::min<size_t>(256, x.size() - i), s);
    return x;
}

// Lowest and highest pitch (cents re 440 Hz) over 50 ms windows from `fromSeconds` on.
std::pair<double, double> pitchRange(const std::vector<float>& out, double rate, double fromSeconds)
{
    double lo = 1e9, hi = -1e9;
    const size_t win = (size_t) (0.05 * rate);
    for (size_t i = (size_t) (fromSeconds * rate); i + win < out.size(); i += win / 2)
    {
        const double c = 1200.0 * std::log2(test::measureFrequency(out, i, i + win, rate) / 440.0);
        lo = std::min(lo, c);
        hi = std::max(hi, c);
    }
    return { lo, hi };
}

SwaySettings steady()
{
    SwaySettings s;
    s.random = 0.0f;
    s.smooth = 0.0f;
    return s;
}

// A hard pick on a silent string: loud enough to be heard at any sensitivity above 0.
std::vector<float> hardPick(double rate, double seconds)
{
    std::vector<float> pick((size_t) (seconds * rate), 0.0f);
    uint32_t seed = 3;
    for (size_t i = 0; i < pick.size(); ++i)
    {
        const double t = (double) i / rate;
        double v = 0.0;
        for (int h = 1; h <= 16; ++h)
            v += std::sin(2.0 * test::kPi * 196.0 * h * t) / h * std::exp(-t * (0.8 + 0.6 * h));
        if (i < (size_t) (0.002 * rate))
        {
            seed = seed * 1664525u + 1013904223u;
            v += (double) (seed >> 8) / 8388608.0 - 1.0;
        }
        pick[i] = (float) (0.3 * v);
    }
    return pick;
}

} // namespace

TEST_CASE("shapes are smooth, periodic and pass through their points")
{
    for (size_t p = 0; p < sway::kShapePresetNames.size(); ++p)
    {
        const std::string name(sway::kShapePresetNames[p]);
        CAPTURE(name);
        const auto shape = sway::makeShape((sway::ShapePreset) p);
        for (int i = 0; i < shape.size(); ++i)
            CHECK(shape.at(shape.point(i).x) == doctest::Approx(shape.point(i).y).epsilon(1e-4));
        float prev = shape.at(0.0f), maxStep = 0.0f;
        for (int i = 1; i <= 4000; ++i)
        {
            const float v = shape.at((float) i / 4000.0f);
            REQUIRE(v >= 0.0f);
            REQUIRE(v <= 1.0f);
            maxStep = std::max(maxStep, std::abs(v - prev));
            prev = v;
        }
        CHECK(maxStep < 0.02f); // no jumps, including across the wrap
    }
}

TEST_CASE("sway off or at zero depth passes the signal through, delayed by the reported latency")
{
    const double rate = 48000.0;
    SwayProcessor p;
    p.prepare(rate);
    const int lat = p.latencySamples();

    for (bool enabled : { false, true })
    {
        CAPTURE(enabled);
        SwaySettings s = steady();
        s.enabled = enabled;
        s.depthCents = 0.0f;
        const auto in = test::sine(440.0, rate, 48000);
        const auto out = run(in, rate, s);
        double err = 0.0;
        for (size_t i = 24000; i < 48000; ++i)
            err = std::max(err, (double) std::abs(out[i] - in[i - (size_t) lat]));
        CHECK(err < 1e-3);
    }
}

TEST_CASE("manual mode holds the bar where it is put")
{
    const double rate = 48000.0;
    SwaySettings s = steady();
    s.mode = GlideMode::Manual;
    s.depthCents = 100.0f;
    s.manual = 1.0f;
    const auto out = run(test::sine(440.0, rate, (size_t) (3 * rate)), rate, s);
    const double f = test::measureFrequency(out, (size_t) (1.5 * rate), out.size(), rate);
    CHECK(1200.0 * std::log2(f / 440.0) == doctest::Approx(-100.0).epsilon(0.01));
}

TEST_CASE("the free-running shape moves the pitch by the depth, in the chosen direction")
{
    const double rate = 48000.0;
    const auto shape = sway::makeShape(sway::ShapePreset::Sine);
    SwaySettings s = steady();
    s.shape = &shape;
    s.depthCents = 50.0f;
    s.cycleSeconds = 0.5;
    struct Case
    {
        BendDirection direction;
        double lo, hi;
    };
    for (const auto c : { Case { BendDirection::Down, -50.0, 0.0 }, Case { BendDirection::Up, 0.0, 50.0 },
                          Case { BendDirection::Both, -50.0, 50.0 } })
    {
        CAPTURE((int) c.direction);
        s.direction = c.direction;
        const auto [lo, hi] = pitchRange(run(test::sine(440.0, rate, (size_t) (4 * rate)), rate, s), rate, 1.0);
        MESSAGE("pitch range " << lo << " .. " << hi << " cents");
        CHECK(lo == doctest::Approx(c.lo).epsilon(0.1 * 50.0 / std::max(1.0, std::abs(c.lo))));
        CHECK(hi == doctest::Approx(c.hi).epsilon(0.1 * 50.0 / std::max(1.0, std::abs(c.hi))));
    }
}

TEST_CASE("sync mode follows the host's beat position")
{
    const double rate = 48000.0, bpm = 120.0;
    SwaySettings s = steady();
    s.mode = GlideMode::Loop;
    s.sync = true;
    s.syncBeats = 1.0;
    s.bpm = bpm;
    SwayProcessor p;
    p.prepare(rate);
    std::vector<float> block(256, 0.0f);
    const double beatsPerSample = bpm / 60.0 / rate;
    double beats = 7.3; // start mid-bar
    for (int b = 0; b < 200; ++b)
    {
        s.hostBeats = beats;
        p.process(block.data(), 256, s);
        beats += 256 * beatsPerSample;
        const double expected = beats - std::floor(beats);
        const double diff = std::abs(p.phase() - expected);
        REQUIRE(std::min(diff, 1.0 - diff) < 1e-3);
    }
}

TEST_CASE("trigger mode plays the shape once per pick, then rests")
{
    const double rate = 48000.0;
    const auto shape = sway::makeShape(sway::ShapePreset::Dive);
    SwaySettings s = steady();
    s.mode = GlideMode::Trigger;
    s.shape = &shape;
    s.depthCents = 50.0f;
    s.cycleSeconds = 0.25;
    SwayProcessor p;
    p.prepare(rate);
    std::vector<float> block(256, 0.0f);
    auto runBlocks = [&](int n) {
        for (int b = 0; b < n; ++b)
            p.process(block.data(), 256, s);
    };

    runBlocks(40); // no pick yet: the bar rests
    CHECK(p.phase() == 0.0f);
    CHECK(std::abs(p.barCents()) < 0.01f);

    auto pick = hardPick(rate, 256.0 * 6.0 / rate);
    for (size_t i = 0; i + 256 <= pick.size(); i += 256)
        p.process(pick.data() + i, 256, s);
    REQUIRE(p.pickAttacks() == 1);
    runBlocks(1); // ~32 ms into the dive
    CHECK(p.phase() > 0.05f);
    CHECK(p.barCents() < -10.0f);

    runBlocks(60); // well past one cycle: back at rest and staying there
    CHECK(p.phase() == 0.0f);
    CHECK(std::abs(p.barCents()) < 1.0f);
}

TEST_CASE("trigger with sync: one pass as long as the note at the host's tempo, not on its beat, RANDOM leaves its length")
{
    const double rate = 48000.0;
    for (float random : { 0.0f, 1.0f })
    {
        CAPTURE(random);
        SwaySettings s = steady();
        s.mode = GlideMode::Trigger;
        s.sync = true;
        s.random = random;
        s.syncBeats = 0.5; // an eighth at 120 BPM: 0.25 s
        s.bpm = 120.0;
        s.cycleSeconds = 4.0; // not this
        s.hostBeats = 7.3;
        SwayProcessor p;
        p.prepare(rate);
        std::vector<float> block(256, 0.0f);
        auto silentBlocks = [&](int n) {
            for (int b = 0; b < n; ++b)
            {
                std::fill(block.begin(), block.end(), 0.0f);
                p.process(block.data(), 256, s);
                s.hostBeats += 256.0 * s.bpm / 60.0 / rate;
            }
        };

        silentBlocks(40); // no pick: the host's beat alone does not start it
        CHECK(p.phase() == 0.0f);

        auto pick = hardPick(rate, 0.1);
        int played = 0;
        for (size_t i = 0; i + 256 <= pick.size() && p.pickAttacks() == 0; i += 256)
            p.process(pick.data() + i, 256, s);
        REQUIRE(p.pickAttacks() == 1);
        while (p.phase() > 0.0f && played < 500)
        {
            silentBlocks(1);
            ++played;
        }
        CHECK(played * 256.0 / rate == doctest::Approx(0.25).epsilon(0.03));
    }
}

TEST_CASE("pick attacks are caught in fast strumming over ringing chords, once each")
{
    // Down and up strums of a six-string chord every 1/8 at 120 BPM: each string a bright plucked
    // tone that rings for seconds, so the chords pile up; the strings are hit 4 ms apart, up strums
    // 6 dB softer and in reverse order, with a short pick click on each string.
    const double rate = 48000.0;
    const double strings[] { 82.4, 110.0, 146.8, 196.0, 246.9, 329.6 };
    const double spacing = 0.25, seconds = 6.0;
    std::vector<float> in((size_t) (seconds * rate), 0.0f);
    uint32_t seed = 9;
    int strums = 0;
    for (double t0 = 0.5; t0 < seconds - 0.5; t0 += spacing, ++strums)
    {
        const bool up = strums % 2 == 1;
        const float level = up ? 0.5f : 1.0f;
        for (int s = 0; s < 6; ++s)
        {
            const size_t start = (size_t) ((t0 + 0.004 * (up ? 5 - s : s)) * rate);
            for (size_t i = start; i < in.size() && i < start + (size_t) (3.0 * rate); ++i)
            {
                const double t = (double) (i - start) / rate;
                double v = 0.0;
                for (int h = 1; h <= 16; ++h)
                    v += std::sin(2.0 * test::kPi * strings[s] * h * t) / h * std::exp(-t * (0.8 + 0.6 * h));
                if (i - start < (size_t) (0.002 * rate))
                {
                    seed = seed * 1664525u + 1013904223u;
                    v += 0.5 * ((double) (seed >> 8) / 8388608.0 - 1.0); // the pick
                }
                in[i] += (float) (0.05 * level * v);
            }
        }
    }

    for (float sensitivity : { 0.1f, 0.5f, 0.8f, 1.0f })
    {
        CAPTURE(sensitivity);
        SwaySettings s = steady();
        s.mode = GlideMode::Trigger;
        s.sensitivity = sensitivity;
        SwayProcessor p;
        p.prepare(rate);
        auto x = in;
        for (size_t i = 0; i < x.size(); i += 256)
            p.process(x.data() + i, (int) std::min<size_t>(256, x.size() - i), s);
        MESSAGE("strums " << strums << ", attacks detected " << p.pickAttacks());
        CHECK(p.pickAttacks() <= strums); // never twice for one strum
        if (sensitivity >= 0.5f)
            CHECK(p.pickAttacks() >= strums - 1); // the soft up strums too
        else
            CHECK(p.pickAttacks() >= strums / 2); // at least the down strums
    }
}

TEST_CASE("at sensitivity 0 the pick detector is off; turned back on, it hears the next pick only")
{
    const double rate = 48000.0;
    const auto pick = hardPick(rate, 0.5);
    sway::PickDetector d;
    d.prepare(rate);
    auto play = [&](float sensitivity) {
        d.setSensitivity(sensitivity);
        int heard = 0;
        for (float x : pick)
            heard += d.detect(x) ? 1 : 0;
        return heard;
    };
    CHECK(play(0.0f) == 0);
    CHECK(play(0.0f) == 0);
    CHECK(play(0.01f) == 1);
}

TEST_CASE("with DYNAMICS, a harder pick strike bends further")
{
    // Single plucks with a pick click, alternating hard and 12 dB softer, half a second apart.
    const double rate = 48000.0;
    std::vector<float> in((size_t) (8 * rate), 0.0f);
    uint32_t seed = 5;
    std::vector<size_t> starts;
    for (int k = 0; k < 14; ++k)
    {
        const size_t start = (size_t) ((0.5 + 0.5 * k) * rate);
        starts.push_back(start);
        const float level = k % 2 == 0 ? 1.0f : 0.25f;
        for (size_t i = start; i < start + (size_t) (0.4 * rate); ++i)
        {
            const double t = (double) (i - start) / rate;
            double v = std::sin(2.0 * test::kPi * 196.0 * t) * std::exp(-t * 6.0)
                       * std::min(1.0, (0.4 - t) / 0.02); // faded out, not cut (a cut would click)
            if (i - start < (size_t) (0.002 * rate))
            {
                seed = seed * 1664525u + 1013904223u;
                v += 0.6 * ((double) (seed >> 8) / 8388608.0 - 1.0);
            }
            in[i] = (float) (0.3 * level * v);
        }
    }
    const auto shape = sway::makeShape(sway::ShapePreset::Dive);
    auto deepest = [&](float dynamics) {
        SwaySettings s = steady();
        s.mode = GlideMode::Trigger;
        s.shape = &shape;
        s.depthCents = 60.0f;
        s.cycleSeconds = 0.3;
        s.dynamics = dynamics;
        SwayProcessor p;
        p.prepare(rate);
        std::vector<float> block(256);
        std::vector<float> lowest(starts.size(), 0.0f);
        for (size_t i = 0; i < in.size(); i += 256)
        {
            std::copy(in.begin() + (long) i, in.begin() + (long) i + 256, block.begin());
            p.process(block.data(), 256, s);
            for (size_t k = 0; k < starts.size(); ++k)
                if (i >= starts[k] && i < starts[k] + (size_t) (0.25 * rate))
                    lowest[k] = std::min(lowest[k], p.barCents());
        }
        return lowest;
    };

    const auto full = deepest(0.0f);
    const auto dyn = deepest(1.0f);
    for (size_t k = 4; k + 1 < full.size(); k += 2) // once the average has settled
    {
        CAPTURE(k);
        CHECK(full[k] == doctest::Approx(full[k + 1]).epsilon(0.1)); // DYNAMICS 0: every strike alike
        CHECK(dyn[k] < -50.0f);                                       // a hard strike: about full depth
        CHECK(dyn[k + 1] > 0.7f * dyn[k]);                            // the soft one bends clearly less
    }
}

TEST_CASE("random never pushes the bar past the depth")
{
    const double rate = 48000.0;
    const auto shape = sway::makeShape(sway::ShapePreset::Square);
    SwaySettings s;
    s.shape = &shape;
    s.random = 1.0f;
    s.depthCents = 60.0f;
    s.cycleSeconds = 0.2;
    const auto [lo, hi] = pitchRange(run(test::sine(440.0, rate, (size_t) (6 * rate)), rate, s), rate, 1.0);
    CHECK(lo > -63.0);
    CHECK(hi < 3.0);
}

TEST_CASE("the pitch shifter keeps a steady level through its head handovers, up and down")
{
    // A handover that lands out of phase would cancel the signal for a moment.
    const double rate = 48000.0;
    for (float cents : { -100.0f, -25.0f, 30.0f, 200.0f })
    {
        CAPTURE(cents);
        sway::DualHeadShifter shifter;
        shifter.prepare(rate);
        auto x = test::sine(440.0, rate, (size_t) (6 * rate));
        for (auto& v : x)
            v = shifter.process(v, cents);
        const size_t win = (size_t) (0.02 * rate); // long enough to average out the sine's own ripple
        double lo = 1e9, hi = 0.0;
        for (size_t i = (size_t) rate; i + win < x.size(); i += win / 4)
        {
            const double r = test::rms(x, i, i + win);
            lo = std::min(lo, r);
            hi = std::max(hi, r);
        }
        CHECK(db(hi / lo) < 0.5);
        const double f = test::measureFrequency(x, (size_t) rate, x.size(), rate);
        CHECK(1200.0 * std::log2(f / 440.0) == doctest::Approx(cents).epsilon(0.01));
    }
}


TEST_CASE("sway stays finite and bounded with every control at its extreme")
{
    const double rate = 44100.0;
    for (auto mode : { GlideMode::Loop, GlideMode::Trigger, GlideMode::Manual })
    {
        CAPTURE((int) mode);
        SwaySettings s;
        s.mode = mode;
        s.depthCents = 300.0f;
        s.cycleSeconds = 0.03;
        s.random = 1.0f;
        s.smooth = 0.0f;
        s.sensitivity = 1.0f;
        s.direction = BendDirection::Both;
        s.manual = -1.0f;
        std::vector<float> in((size_t) (6 * rate));
        uint32_t seed = 1;
        for (size_t i = 0; i < in.size(); ++i)
        {
            seed = seed * 1664525u + 1013904223u;
            const float burst = (i % 11025) < 400 ? 1.0f : 0.05f; // picking attacks
            in[i] = burst * ((float) (seed >> 8) / 8388608.0f - 1.0f);
        }
        const auto out = run(in, rate, s);
        float peak = 0.0f;
        for (float v : out)
        {
            REQUIRE(std::isfinite(v));
            peak = std::max(peak, std::abs(v));
        }
        CHECK(peak < 2.5f);
    }
}

namespace
{

// Low-passed noise: no period, so a handover search has nothing but the signal itself to line up on.
std::vector<float> rumble(double rate, size_t n, uint32_t seed)
{
    std::vector<float> v(n);
    float lp = 0.0f;
    const float c = (float) (1.0 - std::exp(-2.0 * test::kPi * 2000.0 / rate));
    for (auto& x : v)
    {
        seed = seed * 1664525u + 1013904223u;
        lp += c * ((float) (seed >> 8) / 8388608.0f - 1.0f - lp);
        x = 0.5f * lp;
    }
    return v;
}

} // namespace

TEST_CASE("stereo stays aligned through the handovers")
{
    // Two channels that differ a little: each choosing its own handover would leave them up to a
    // period of the search apart.
    const double rate = 48000.0;
    const size_t n = (size_t) (4 * rate);
    auto left = rumble(rate, n, 1);
    auto right = left;
    const auto noise = rumble(rate, n, 99);
    for (size_t i = 0; i < n; ++i)
        right[i] += 0.3f * noise[i];

    SwaySettings s = steady();
    s.depthCents = 200.0f;
    s.cycleSeconds = 0.5;
    SwayProcessor p;
    p.prepare(rate);
    for (size_t i = 0; i < n; i += 256)
    {
        float* channels[] { left.data() + i, right.data() + i };
        p.process(channels, 2, 256, s);
    }
    // In every 20 ms window the two outputs are as alike as the inputs were (the noise is ~-10 dB).
    const size_t win = (size_t) (0.02 * rate);
    double worst = 1.0;
    for (size_t i = (size_t) rate; i + win < n; i += win)
    {
        double lr = 0.0, ll = 1e-12, rr = 1e-12;
        for (size_t k = i; k < i + win; ++k)
        {
            lr += (double) left[k] * right[k];
            ll += (double) left[k] * left[k];
            rr += (double) right[k] * right[k];
        }
        worst = std::min(worst, lr / std::sqrt(ll * rr));
    }
    CHECK(worst > 0.9);
}

TEST_CASE("a handover search costs the same few tens of thousands of steps at any sample rate")
{
    // It runs inside one sample: at hundreds of thousands of steps it would overrun a small audio buffer.
    int cost[2] {};
    int i = 0;
    for (double rate : { 48000.0, 96000.0 })
    {
        sway::DualHeadShifter shifter;
        shifter.prepare(rate);
        const auto x = rumble(rate, (size_t) rate, 7);
        for (float v : x)
        {
            shifter.process(v, -200.0f);
            cost[i] = std::max(cost[i], shifter.lastSearchCost());
        }
        ++i;
    }
    MESSAGE("search cost at 48 kHz " << cost[0] << ", at 96 kHz " << cost[1]);
    CHECK(cost[0] > 0);
    CHECK(cost[0] < 60000);
    CHECK(cost[1] < 60000);
}

TEST_CASE("between strikes the bar rests at the open pitch, also when bending both ways")
{
    const double rate = 48000.0;
    for (auto direction : { BendDirection::Down, BendDirection::Both, BendDirection::Up })
    {
        CAPTURE((int) direction);
        SwaySettings s = steady();
        s.mode = GlideMode::Trigger;
        s.direction = direction;
        s.depthCents = 100.0f;
        const auto shape = sway::makeShape(sway::ShapePreset::RampUp); // starts pushed all the way
        s.shape = &shape;
        SwayProcessor p;
        p.prepare(rate);
        std::vector<float> block(256, 0.0f);
        for (int b = 0; b < 40; ++b)
            p.process(block.data(), 256, s);
        CHECK(std::abs(p.barCents()) < 0.5f);
    }
}

TEST_CASE("after a held bend the wet signal comes back in time with the dry one")
{
    // Held down, the head runs far from rest; once the bar is back, it must not stay 10-25 ms early
    // or late (a comb filter against the dry signal below 100 % MIX) for the minute the creep takes.
    const double rate = 48000.0;
    SwaySettings s = steady();
    s.mode = GlideMode::Manual;
    s.depthCents = 100.0f;
    SwayProcessor p;
    p.prepare(rate);
    auto x = test::sine(196.0, rate, (size_t) (2 * rate));
    double held = 0.0;
    for (size_t i = 0; i < x.size(); i += 256)
    {
        s.manual = i < (size_t) (0.7 * rate) ? 1.0f : 0.0f;
        p.process(x.data() + i, 256, s);
        if (i < (size_t) (0.7 * rate))
            held = std::max(held, std::abs(p.mainShifter().offsetFromRest()));
    }
    MESSAGE("furthest from rest while held " << held / rate * 1000.0 << " ms, after release "
                                             << p.mainShifter().offsetFromRest() / rate * 1000.0 << " ms");
    CHECK(held > 0.01 * rate);
    CHECK(std::abs(p.mainShifter().offsetFromRest()) < 0.0052 * rate); // within a period of the note
}

TEST_CASE("a path that is not bent comes through delayed, and bending it in and out is smooth")
{
    const double rate = 48000.0;
    const auto x = test::sine(440.0, rate, (size_t) (3.0 * rate), 0.3f);
    const std::vector<float> bar(x.size(), -150.0f);

    // Straight: the path itself, one latency late, to the sample.
    {
        sway::Bender b;
        b.prepare(rate);
        auto y = x;
        for (size_t i = 0; i < y.size(); i += 256)
        {
            float* ch[] = { y.data() + i };
            b.process(ch, 1, (int) std::min<size_t>(256, y.size() - i), bar.data() + i, false);
        }
        const auto d = (size_t) b.latencySamples();
        float err = 0.0f;
        for (size_t i = d; i < y.size(); ++i)
            err = std::max(err, std::abs(y[i] - x[i - d]));
        CHECK(err == 0.0f);
    }

    // Bent from 1 s to 2 s: the pitch goes there and back, and no step is louder than the tone's own.
    sway::Bender b;
    b.prepare(rate);
    auto y = x;
    for (size_t i = 0; i < y.size(); i += 256)
    {
        float* ch[] = { y.data() + i };
        const bool bend = i >= (size_t) rate && i < (size_t) (2.0 * rate);
        b.process(ch, 1, (int) std::min<size_t>(256, y.size() - i), bar.data() + i, bend);
    }
    const auto at = [&](double seconds) { return (size_t) (seconds * rate); };
    CHECK(std::abs(1200.0 * std::log2(test::measureFrequency(y, at(1.3), at(1.9), rate) / 440.0) + 150.0) < 3.0);
    CHECK(std::abs(1200.0 * std::log2(test::measureFrequency(y, at(2.3), at(2.9), rate) / 440.0)) < 1.0);
    float worst = 0.0f;
    for (size_t i = at(0.5); i < y.size(); ++i)
        worst = std::max(worst, std::abs(y[i] - y[i - 1]));
    CHECK(worst < 1.3f * 0.3f * (float) (2.0 * test::kPi * 440.0 / rate));
}
