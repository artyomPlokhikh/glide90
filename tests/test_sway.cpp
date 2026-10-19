#include "SwayProcessor.h"
#include "TestUtil.h"

#include <doctest/doctest.h>

#include <cmath>

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

} // namespace

TEST_CASE("shapes are smooth, periodic and pass through their points")
{
    for (size_t p = 0; p < sway::kShapePresetNames.size(); ++p)
    {
        CAPTURE(sway::kShapePresetNames[p]);
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
    s.mode = GlideMode::Sync;
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

TEST_CASE("trigger mode restarts the shape on a MIDI note")
{
    const double rate = 48000.0;
    SwaySettings s = steady();
    s.mode = GlideMode::Trigger;
    s.triggerFromPick = false;
    s.cycleSeconds = 1.0;
    SwayProcessor p;
    p.prepare(rate);
    std::vector<float> block(256, 0.0f);
    for (int b = 0; b < 40; ++b)
        p.process(block.data(), 256, s);
    CHECK(p.phase() > 0.1f);
    s.midiTriggerAt = 200;
    p.process(block.data(), 256, s);
    CHECK(p.phase() == doctest::Approx(55.0 / rate).epsilon(0.05));
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
    for (auto mode : { GlideMode::Free, GlideMode::Trigger, GlideMode::Manual })
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
        s.twinDetuneCents = 30.0f;
        s.twinLevel = 1.0f;
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
