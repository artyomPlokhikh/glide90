#include "Constants.h"
#include "SincResampler.h"
#include "TestUtil.h"

#include <doctest/doctest.h>

TEST_CASE("down/up resampling through the DSP rate is transparent below 12 kHz")
{
    for (double rate : { 44100.0, 48000.0, 96000.0 })
    {
        CAPTURE(rate);
        spx::Downsampler down;
        spx::Upsampler up;
        down.prepare(rate, spx::kDspRate);
        up.prepare(spx::kDspRate, rate, 12);

        for (double f : { 100.0, 1000.0, 5000.0, 10000.0, 12000.0 })
        {
            CAPTURE(f);
            down.reset();
            up.reset();
            const auto in = test::sine(f, rate, (size_t) rate / 2);
            std::vector<float> out(in.size()), dsp;
            for (size_t s = 0; s < in.size(); s += 256)
            {
                const int n = (int) std::min<size_t>(256, in.size() - s);
                dsp.clear();
                down.process(in.data() + s, n, dsp);
                up.push(dsp.data(), (int) dsp.size());
                up.pull(out.data() + s, n);
            }
            const double level = test::toneLevel(out, in.size() / 2, in.size(), f, rate);
            CHECK(std::abs(test::db(level / 0.25)) < 0.1);
        }
    }
}

TEST_CASE("resampling rejects what would alias into the passband")
{
    const double rate = 48000.0;
    spx::Downsampler down;
    spx::Upsampler up;
    down.prepare(rate, spx::kDspRate);
    up.prepare(spx::kDspRate, rate, 12);
    // 20 kHz would fold to 11.37 kHz at the DSP rate.
    const auto in = test::sine(20000.0, rate, (size_t) rate / 2);
    std::vector<float> out(in.size()), dsp;
    for (size_t s = 0; s < in.size(); s += 256)
    {
        const int n = (int) std::min<size_t>(256, in.size() - s);
        dsp.clear();
        down.process(in.data() + s, n, dsp);
        up.push(dsp.data(), (int) dsp.size());
        up.pull(out.data() + s, n);
    }
    const double alias = test::toneLevel(out, in.size() / 2, in.size(), spx::kDspRate - 20000.0, rate);
    CHECK(test::db(alias / 0.25) < -85.0);
}
