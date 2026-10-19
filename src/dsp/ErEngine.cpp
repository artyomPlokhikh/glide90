#include "ErEngine.h"

#include "Constants.h"
#include "ErTables.h"

#include <algorithm>
#include <cmath>
#include <span>

namespace spx
{
namespace
{

constexpr int kFadeSamples = 627; // 20 ms at the DSP rate

using Span = std::span<const tables::Tap>;

Span tableFor(ErType type, int density, int channel)
{
    using namespace tables;
    const bool left = channel == 0;
    if (density == 1)
    {
        switch (type)
        {
            case ErType::Hall: return left ? Span(er1_hall_l) : Span(er1_hall_r);
            case ErType::Random: return left ? Span(er1_random_l) : Span(er1_random_r);
            case ErType::Reverse: return left ? Span(er1_reverse_l) : Span(er1_reverse_r);
            case ErType::Plate: return left ? Span(er1_plate_l) : Span(er1_plate_r);
        }
    }
    switch (type)
    {
        case ErType::Hall: return left ? Span(er2_hall_l) : Span(er2_hall_r);
        case ErType::Random: return left ? Span(er2_random_l) : Span(er2_random_r);
        case ErType::Reverse: return left ? Span(er2_reverse_l) : Span(er2_reverse_r);
        case ErType::Plate: return left ? Span(er2_plate_l) : Span(er2_plate_r);
    }
    return {};
}

// LIVENESS scales the reflection levels in dB around the factory setting of 5: lower values make
// the pattern fade (or, for REVERSE, swell) more steeply, higher values flatten it. The captures
// are all at LIVENESS 5, so the slope of this mapping is an estimate.
float livenessExponent(int liveness)
{
    return 1.6f - 0.12f * (float) liveness;
}

} // namespace

std::vector<ErEngine::Tap> ErEngine::buildTaps(const ErSettings& s, int channel)
{
    std::vector<Tap> taps;
    buildTaps(s, channel, taps);
    return taps;
}

void ErEngine::buildTaps(const ErSettings& s, int channel, std::vector<Tap>& taps)
{
    const int pre = (int) std::lround(s.delayMs * kDspRate / 1000.0);
    const float exponent = livenessExponent(s.liveness);
    taps.clear();
    float energy = 0.0f;
    for (const auto& t : tableFor(s.type, s.density, channel))
    {
        const float mag = std::pow(std::abs(t.gain), exponent);
        energy += mag * mag;
        const int d = pre + (int) std::lround(t.pos * s.roomSize);
        if (d < 1 || d >= kDelayMemory)
            continue;
        taps.push_back({ d, std::copysign(mag, t.gain) });
    }

    // The captures only give levels relative to the loudest reflection. Scale the pattern to unit
    // energy so the effect is about as loud as the direct signal for any TYPE, density and LIVENESS;
    // left at "loudest tap = 1", hundreds of taps put the effect 9-17 dB over the D/A's full scale.
    const float norm = energy > 0.0f ? 1.0f / std::sqrt(energy) : 1.0f;
    for (auto& t : taps)
        t.gain *= norm;

    // Taps that land on the same sample (small rooms) add up.
    std::sort(taps.begin(), taps.end(), [](const Tap& a, const Tap& b) { return a.delay < b.delay; });
    size_t merged = 0;
    for (const auto& t : taps)
    {
        if (merged > 0 && taps[merged - 1].delay == t.delay)
            taps[merged - 1].gain += t.gain;
        else
            taps[merged++] = t;
    }
    taps.resize(merged);
}

void ErEngine::prepare()
{
    memory.assign((size_t) kDelayMemory, 0.0f);
    size_t longest = 0;
    for (auto type : { ErType::Hall, ErType::Random, ErType::Reverse, ErType::Plate })
        for (int density : { 1, 2 })
            for (int ch : { 0, 1 })
                longest = std::max(longest, tableFor(type, density, ch).size());
    for (int ch = 0; ch < 2; ++ch)
    {
        current[ch].reserve(longest);
        previous[ch].reserve(longest);
    }
    reset();
}

void ErEngine::reset()
{
    std::fill(memory.begin(), memory.end(), 0.0f);
    writePos = 0;
    fadeLeft = 0;
    hasSettings = false; // nothing is playing yet, so the next settings apply without a crossfade
    lpfState[0] = lpfState[1] = 0.0f;
}

void ErEngine::setSettings(const ErSettings& s)
{
    if (hasSettings && s == settings)
        return;

    const bool first = !hasSettings;
    settings = s;
    hasSettings = true;
    lpfCoeff = s.lpfHz > 0.0f ? 1.0f - std::exp(-2.0f * 3.14159265f * s.lpfHz / (float) kDspRate) : 1.0f;

    for (int ch = 0; ch < 2; ++ch)
    {
        std::swap(previous[ch], current[ch]);
        buildTaps(s, ch, current[ch]);
    }
    fadeLeft = first ? 0 : kFadeSamples;
}

void ErEngine::process(const float* in, float* outL, float* outR, int numSamples)
{
    constexpr int mask = kDelayMemory - 1;
    float* out[2] = { outL, outR };

    for (int n = 0; n < numSamples; ++n)
    {
        memory[(size_t) writePos] = in[n];

        for (int ch = 0; ch < 2; ++ch)
        {
            float acc = 0.0f;
            for (const auto& t : current[ch])
                acc += t.gain * memory[(size_t) ((writePos - t.delay) & mask)];

            if (fadeLeft > 0)
            {
                float old = 0.0f;
                for (const auto& t : previous[ch])
                    old += t.gain * memory[(size_t) ((writePos - t.delay) & mask)];
                const float w = (float) fadeLeft / kFadeSamples;
                acc = acc * (1.0f - w) + old * w;
            }

            lpfState[ch] += lpfCoeff * (acc - lpfState[ch]);
            out[ch][n] = lpfState[ch];
        }

        if (fadeLeft > 0)
            --fadeLeft;
        writePos = (writePos + 1) & mask;
    }
}

} // namespace spx
