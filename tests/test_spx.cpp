#include "RomPrograms.h"
#include "TestUtil.h"
#include "Wav.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <numeric>
#include <string>

using test::db;

namespace
{

spx::SpxSettings programSettings(int program, float balance = 1.0f)
{
    spx::SpxSettings s;
    s.program = program;
    s.er = spx::romProgram(program).er;
    s.balance = balance;
    return s;
}

struct Comparison
{
    double correlation = 0.0;
    double worstEnvelopeDb = 0.0;
    double worstBandDb = 0.0;
};

// The capture's sub-sample phase relative to the DSP clock is arbitrary, so the model is aligned
// to it with a fractional delay as well as an integer one.
std::vector<float> fractionalDelay(const std::vector<float>& x, size_t start, size_t n, double frac)
{
    constexpr int half = 24;
    std::vector<float> out(n, 0.0f);
    for (size_t i = 0; i < n; ++i)
    {
        double acc = 0.0;
        for (int k = -half + 1; k <= half; ++k)
        {
            const long j = (long) (start + i) + k;
            if (j < 0 || j >= (long) x.size())
                continue;
            const double t = k - frac;
            const double w = 0.5 + 0.5 * std::cos(test::kPi * t / half);
            acc += x[(size_t) j] * (std::abs(t) < 1e-9 ? 1.0 : std::sin(test::kPi * t) / (test::kPi * t)) * w;
        }
        out[i] = (float) acc;
    }
    return out;
}

double normalisedCorrelation(const std::vector<float>& a, const std::vector<float>& b)
{
    const double ab = std::inner_product(a.begin(), a.end(), b.begin(), 0.0);
    const double aa = std::inner_product(a.begin(), a.end(), a.begin(), 0.0);
    const double bb = std::inner_product(b.begin(), b.end(), b.begin(), 0.0);
    return ab / std::sqrt(aa * bb + 1e-30);
}

// Aligns `model` to `measured` (both impulse responses at the same rate), normalises them and
// compares waveform, 10 ms envelope (where the capture is within 30 dB of its loudest window,
// i.e. well above its noise) and 1/3-octave spectrum.
Comparison compare(const std::vector<float>& model, const std::vector<float>& measured, double rate)
{
    const size_t n = measured.size();
    int bestLag = 0;
    double best = -1.0;
    const double em = std::inner_product(measured.begin(), measured.end(), measured.begin(), 0.0);
    for (int lag = 0; lag + n <= model.size() && lag < 6000; ++lag)
    {
        double c = 0.0, e = 0.0;
        for (size_t i = 0; i < n; ++i)
        {
            c += (double) model[(size_t) lag + i] * measured[i];
            e += (double) model[(size_t) lag + i] * model[(size_t) lag + i];
        }
        const double r = c / std::sqrt(e * em + 1e-30);
        if (r > best)
        {
            best = r;
            bestLag = lag;
        }
    }

    std::vector<float> a;
    best = -1.0;
    for (double frac = -0.5; frac <= 0.5; frac += 1.0 / 32.0)
    {
        auto shifted = fractionalDelay(model, (size_t) bestLag, n, frac);
        const double r = normalisedCorrelation(shifted, measured);
        if (r > best)
        {
            best = r;
            a = std::move(shifted);
        }
    }

    double sa = 0.0, sm = 0.0;
    for (size_t i = 0; i < n; ++i)
    {
        sa += (double) a[i] * a[i];
        sm += (double) measured[i] * measured[i];
    }
    const double g = std::sqrt(sm / sa);
    for (auto& v : a)
        v = (float) (v * g);

    Comparison out;
    out.correlation = best;

    const size_t w = (size_t) (0.01 * rate);
    double peak = 0.0;
    for (size_t s = 0; s + w <= n; s += w)
        peak = std::max(peak, test::rms(measured, s, s + w));
    for (size_t s = 0; s + w <= n; s += w)
    {
        const double m = test::rms(measured, s, s + w);
        if (db(m / peak) < -30.0)
            continue;
        out.worstEnvelopeDb = std::max(out.worstEnvelopeDb, std::abs(db(test::rms(a, s, s + w) / m)));
    }

    for (double fc = 250.0; fc <= 10000.0; fc *= std::pow(2.0, 1.0 / 3.0))
    {
        double ea = 0.0, emb = 0.0;
        for (double f = fc / 1.12; f < fc * 1.12; f += fc / 40.0)
        {
            ea += std::pow(test::toneLevel(a, 0, n, f, rate), 2.0);
            emb += std::pow(test::toneLevel(measured, 0, n, f, rate), 2.0);
        }
        out.worstBandDb = std::max(out.worstBandDb, std::abs(10.0 * std::log10(ea / emb)));
    }
    return out;
}

} // namespace

TEST_CASE("direct path: unity gain, measured converter roll-off, reported latency")
{
    const double rate = 48000.0;
    const auto s = programSettings(17, 0.0f);

    for (double f : { 1000.0, 10000.0, 12500.0, 14000.0 })
    {
        CAPTURE(f);
        const auto in = test::sine(f, rate, 24000);
        const auto out = test::render(in, rate, s);
        const double level = db(test::toneLevel(out[0], 12000, 24000, f, rate) / 0.25);
        if (f == 1000.0)
            CHECK(std::abs(level) < 0.2);
        if (f == 10000.0)
            CHECK(std::abs(level) < 0.6);
        if (f == 12500.0)
            CHECK(level == doctest::Approx(-6.2).epsilon(0.25));
        if (f == 14000.0)
            CHECK(level < -40.0);
    }

    spx::SpxEngine engine;
    engine.prepare(rate, 512);
    std::vector<float> in(4096, 0.0f), l(4096), r(4096);
    in[0] = 0.5f;
    engine.process(in.data(), l.data(), r.data(), 4096, s);
    const auto peakAt = std::max_element(l.begin(), l.end(), [](float a, float b) { return std::abs(a) < std::abs(b); }) - l.begin();
    CHECK(peakAt == engine.latencySamples());
}

TEST_CASE("the response does not depend on when the input arrives")
{
    // An impulse in the first block and one much later must come out identically, latency apart.
    for (double rate : { 44100.0, 48000.0, 96000.0 })
    {
        CAPTURE(rate);
        const size_t later = 7777;
        std::vector<float> in(later + 8192, 0.0f);
        in[0] = 0.5f;
        const auto first = test::render(in, rate, programSettings(5));
        std::fill(in.begin(), in.end(), 0.0f);
        in[later] = 0.5f;
        const auto second = test::render(in, rate, programSettings(5));
        double diff = 0.0, energy = 0.0;
        for (size_t i = 0; i < 8192; ++i)
        {
            diff += std::pow((double) first[0][i] - second[0][later + i], 2.0);
            energy += std::pow((double) first[0][i], 2.0);
        }
        // What remains is the 16-bit converters rounding the two impulses differently; a time slip
        // would leave the difference near 0 dB.
        CHECK(db(std::sqrt(diff / energy)) < -40.0);
    }
}

TEST_CASE("irregular host block sizes give the same output as fixed ones")
{
    for (double rate : { 44100.0, 48000.0, 96000.0 })
    {
        CAPTURE(rate);
        const int maxBlock = 512;
        auto in = test::sine(1000.0, rate, 30000);
        in[100] = 0.9f;
        const auto ref = test::render(in, rate, programSettings(17), maxBlock);

        spx::SpxEngine engine;
        engine.prepare(rate, maxBlock);
        std::vector<float> l(in.size()), r(in.size());
        uint32_t seed = 7;
        for (size_t start = 0; start < in.size();)
        {
            seed = seed * 1664525u + 1013904223u;
            const int n = (int) std::min<size_t>(1 + (seed >> 8) % maxBlock, in.size() - start);
            engine.process(in.data() + start, l.data() + start, r.data() + start, n, programSettings(17));
            start += (size_t) n;
        }
        double worst = 0.0;
        for (size_t i = 0; i < in.size(); ++i)
            worst = std::max(worst, (double) std::abs(l[i] - ref[0][i]));
        // Block splits round the resampler clock slightly differently, which can flip a 16-bit step
        // (1 LSB = 3e-5) here and there; a time slip would be orders of magnitude larger.
        CHECK(worst < 1e-4);
    }
}

TEST_CASE("the effect is about as loud as the direct signal and does not clip at a sane level")
{
    // Pink-ish noise at -12 dBFS peak, roughly a hot guitar DI.
    const double rate = 48000.0;
    std::vector<float> in((size_t) (4 * rate));
    uint32_t seed = 3;
    float lp = 0.0f;
    for (auto& v : in)
    {
        seed = seed * 1664525u + 1013904223u;
        lp += 0.2f * (((float) (seed >> 8) / 8388608.0f - 1.0f) - lp);
        v = 0.6f * lp;
    }
    for (int program : { 5, 6, 16, 17 })
        for (float rs : { 2.0f, 20.0f })
            for (int liveness : { 0, 5, 10 })
            {
                CAPTURE(program);
                CAPTURE(rs);
                CAPTURE(liveness);
                auto s = programSettings(program);
                s.er.roomSize = rs;
                s.er.liveness = liveness;
                const auto out = test::render(in, rate, s);
                const size_t from = (size_t) (1.5 * rate);
                const double gain = db(test::rms(out[0], from, in.size()) / test::rms(in, from, in.size()));
                CHECK(std::abs(gain) < 6.0);
                float peak = 0.0f;
                for (size_t i = from; i < in.size(); ++i)
                    peak = std::max(peak, std::abs(out[0][i]));
                CHECK(peak < 0.9f);
            }
}

TEST_CASE("the output level switch is analog: same signal 24 dB down, not 4 bits less")
{
    const double rate = 48000.0;
    auto in = test::sine(440.0, rate, 24000, 0.01f); // quiet: the 16-bit steps matter
    auto s = programSettings(17);
    const auto normal = test::render(in, rate, s);
    s.outputMinus20 = true;
    const auto lowered = test::render(in, rate, s);
    double err = 0.0, ref = 0.0;
    const double g = std::pow(10.0, -24.0 / 20.0);
    for (size_t i = 12000; i < 24000; ++i)
    {
        err += std::pow(lowered[0][i] - g * normal[0][i], 2.0);
        ref += std::pow(g * normal[0][i], 2.0);
    }
    CHECK(db(std::sqrt(err / ref)) < -80.0);
}

TEST_CASE("bypass is the analog path: full bandwidth, same latency")
{
    const double rate = 48000.0;
    auto s = programSettings(17);
    s.bypass = true;
    const auto in = test::sine(18000.0, rate, 24000);
    const auto out = test::render(in, rate, s);
    CHECK(std::abs(db(test::toneLevel(out[0], 12000, 24000, 18000.0, rate) / 0.25)) < 0.1);
}

TEST_CASE("A/D clips at full scale")
{
    auto s = programSettings(17, 0.0f);
    s.inputKnob = 10.0f; // +12 dB
    const auto out = test::render(test::sine(1000.0, 48000.0, 24000, 0.9f), 48000.0, s);
    float peak = 0.0f;
    for (size_t i = 12000; i < 24000; ++i)
        peak = std::max(peak, std::abs(out[0][i]));
    CHECK(peak < 1.15f); // clipped (plus filter overshoot) instead of 0.9 * 4
}

TEST_CASE("programs 5, 6, 16 and 17 match impulse responses of the real SPX90")
{
    const std::string dir = std::string(GLIDE90_IR_DIR) + "/Millertone SPX90 (GrungeVerb)/";
    if (!std::filesystem::exists(dir))
    {
        MESSAGE("reference IRs not found in ir/, skipping");
        return;
    }

    struct Case
    {
        int program;
        const char* file;
    };
    for (const Case c : { Case { 5, "GrungeVerb (Early Ref 01).wav" }, Case { 6, "GrungeVerb (Early Ref 02).wav" },
                          Case { 16, "GrungeVerb (Gated Reverb).wav" }, Case { 17, "GrungeVerb (Rev Gate).wav" } })
    {
        CAPTURE(c.program);
        Wav ir;
        REQUIRE(readWav(dir + c.file, ir));
        const double rate = ir.sampleRate;
        const size_t len = ir.data[0].size() + (size_t) (0.5 * rate);
        std::vector<float> in(len, 0.0f);
        in[0] = 0.5f;
        const auto out = test::render(in, rate, programSettings(c.program));
        const auto cmp = compare(out[0], ir.data[0], rate);
        MESSAGE("program " << c.program << ": correlation " << cmp.correlation << ", envelope within "
                           << cmp.worstEnvelopeDb << " dB, 1/3-octave bands within " << cmp.worstBandDb << " dB");
        CHECK(cmp.correlation > 0.998);
        CHECK(cmp.worstEnvelopeDb < 1.0);
        CHECK(cmp.worstBandDb < 0.5);
    }
}
