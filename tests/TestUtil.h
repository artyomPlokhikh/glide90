#pragma once

#include "SpxEngine.h"

#include <cmath>
#include <vector>

namespace test
{

constexpr double kPi = 3.14159265358979323846;

// Runs `in` through a freshly prepared engine in blocks and returns both channels.
inline std::vector<std::vector<float>> render(const std::vector<float>& in, double rate, const spx::SpxSettings& s,
                                              int block = 512)
{
    spx::SpxEngine engine;
    engine.prepare(rate, block);
    std::vector<std::vector<float>> out(2, std::vector<float>(in.size()));
    for (size_t start = 0; start < in.size(); start += (size_t) block)
    {
        const int n = (int) std::min<size_t>((size_t) block, in.size() - start);
        engine.process(in.data() + start, out[0].data() + start, out[1].data() + start, n, s);
    }
    return out;
}

inline std::vector<float> sine(double freq, double rate, size_t n, float amp = 0.25f)
{
    std::vector<float> v(n);
    for (size_t i = 0; i < n; ++i)
        v[i] = amp * (float) std::sin(2.0 * kPi * freq * (double) i / rate);
    return v;
}

inline double rms(const std::vector<float>& v, size_t from, size_t to)
{
    double acc = 0.0;
    for (size_t i = from; i < to; ++i)
        acc += (double) v[i] * v[i];
    return std::sqrt(acc / (double) (to - from));
}

inline double db(double x)
{
    return 20.0 * std::log10(std::max(x, 1e-12));
}

// Magnitude at one frequency (Goertzel), normalised so a unit sine reads 1.
inline double toneLevel(const std::vector<float>& v, size_t from, size_t to, double freq, double rate)
{
    double re = 0.0, im = 0.0;
    for (size_t i = from; i < to; ++i)
    {
        const double ph = 2.0 * kPi * freq * (double) i / rate;
        re += v[i] * std::cos(ph);
        im += v[i] * std::sin(ph);
    }
    return 2.0 * std::sqrt(re * re + im * im) / (double) (to - from);
}

// Frequency from upward zero crossings with linear interpolation.
inline double measureFrequency(const std::vector<float>& v, size_t from, size_t to, double rate)
{
    double first = -1.0, last = -1.0;
    int crossings = 0;
    for (size_t i = from + 1; i < to; ++i)
        if (v[i - 1] < 0.0f && v[i] >= 0.0f)
        {
            const double t = (double) (i - 1) + v[i - 1] / (double) (v[i - 1] - v[i]);
            if (first < 0.0)
                first = t;
            else
                ++crossings;
            last = t;
        }
    return crossings > 0 ? crossings * rate / (last - first) : 0.0;
}

} // namespace test
