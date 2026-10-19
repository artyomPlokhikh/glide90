#pragma once

#include "Constants.h"
#include "ErTables.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <iterator>

namespace spx
{

// One analog/converter stage of the SPX90 signal path, run at the DSP rate. The input stage
// (ConverterFilter<tables::converterIn>) followed by the output stage
// (ConverterFilter<tables::converterOut>) is the response measured through the real unit: flat to
// 11.5 kHz, -6 dB at 12.5 kHz, -49 dB at 14 kHz, and not minimum phase.
template <const auto& kernel>
class ConverterFilter
{
public:
    static constexpr int kTaps = (int) std::size(kernel);

    void reset()
    {
        history.fill(0.0f);
        pos = 0;
    }

    float process(float x)
    {
        pos = (pos == 0 ? kTaps : pos) - 1;
        history[(size_t) pos] = x;
        history[(size_t) (pos + kTaps)] = x;
        float acc = 0.0f;
        const float* h = history.data() + pos;
        for (int k = 0; k < kTaps; ++k)
            acc += kernel[k] * h[k];
        return acc;
    }

private:
    std::array<float, 2 * (size_t) kTaps> history {};
    int pos = 0;
};

// 16-bit linear A/D or D/A: clips at full scale and truncates to the 16-bit grid.
inline float quantise16(float x)
{
    const float clipped = std::clamp(x, -kAdcFullScale, kAdcFullScale - kQuantStep);
    return std::floor(clipped * 32768.0f) * kQuantStep;
}

} // namespace spx
