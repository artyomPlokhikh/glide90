#include "SwayProcessor.h"
#include "TestUtil.h"

#include <doctest/doctest.h>

#include <cmath>

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

} // namespace

TEST_CASE("sway off or at zero depth passes the signal through, delayed by the reported latency")
{
    const double rate = 48000.0;
    SwayProcessor p;
    p.prepare(rate);
    const int lat = p.latencySamples();

    for (bool enabled : { false, true })
    {
        CAPTURE(enabled);
        SwaySettings s;
        s.enabled = enabled;
        s.depthCents = 0.0f;
        s.strumSync = 0.0f;
        const auto in = test::sine(440.0, rate, 48000);
        const auto out = run(in, rate, s);
        double err = 0.0;
        for (size_t i = 24000; i < 48000; ++i)
            err = std::max(err, (double) std::abs(out[i] - in[i - (size_t) lat]));
        CHECK(err < 1e-3);
    }
}

TEST_CASE("a held bar lowers the pitch and keeps it there")
{
    const double rate = 48000.0;
    SwaySettings s;
    s.depthCents = 0.0f;
    s.strumSync = 0.0f;
    s.barCents = -100.0f;
    const auto out = run(test::sine(440.0, rate, (size_t) (6 * rate)), rate, s);
    const double expected = 440.0 * std::pow(2.0, -100.0 / 1200.0);
    // Several seconds in, after the slow path has taken over the whole offset.
    for (double t : { 3.0, 5.0 })
    {
        CAPTURE(t);
        const double f = test::measureFrequency(out, (size_t) (t * rate), (size_t) ((t + 0.5) * rate), rate);
        CHECK(f == doctest::Approx(expected).epsilon(0.003));
    }
}

TEST_CASE("the wobble moves the pitch by about the set depth, mostly downwards")
{
    const double rate = 48000.0;
    SwaySettings s;
    s.depthCents = 50.0f;
    s.rateHz = 2.0f;
    s.shape = 0.0f;
    s.strumSync = 0.0f;
    s.asymmetry = 0.0f;
    const auto out = run(test::sine(440.0, rate, (size_t) (8 * rate)), rate, s);

    double lo = 1e9, hi = 0.0;
    const size_t win = (size_t) (0.05 * rate);
    for (size_t i = (size_t) (3 * rate); i + win < out.size(); i += win / 2)
    {
        const double f = test::measureFrequency(out, i, i + win, rate);
        lo = std::min(lo, f);
        hi = std::max(hi, f);
    }
    const double centsLo = 1200.0 * std::log2(lo / 440.0), centsHi = 1200.0 * std::log2(hi / 440.0);
    MESSAGE("pitch range " << centsLo << " .. " << centsHi << " cents");
    CHECK(centsLo < -35.0);
    CHECK(centsLo > -65.0);
    CHECK(centsHi < 12.0);
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
    SwaySettings s;
    s.depthCents = 100.0f;
    s.rateHz = 8.0f;
    s.shape = 1.0f;
    s.strumSync = 1.0f;
    s.sensitivity = 1.0f;
    s.asymmetry = 1.0f;
    s.barCents = -200.0f;
    s.twinDetuneCents = 30.0f;
    s.twinLevel = 1.0f;
    std::vector<float> in((size_t) (10 * rate));
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
