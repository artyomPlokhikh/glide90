#include "SpxEngine.h"

#include "RomPrograms.h"

#include <algorithm>
#include <cmath>

namespace spx
{
namespace
{

constexpr double kSmoothingMs = 20.0;
constexpr double kBypassFadeMs = 10.0;

} // namespace

void SpxEngine::prepare(double rate, int maxBlockSize)
{
    hostRate = rate;
    downL.prepare(hostRate, kDspRate);
    downR.prepare(hostRate, kDspRate);
    downEffect.prepare(hostRate, kDspRate);
    const int lag = 8 + (int) std::ceil(kDspRate / hostRate); // block-to-block jitter margin
    for (auto* up : { &upL, &upR, &upWetL, &upWetR })
        up->prepare(kDspRate, hostRate, lag);
    er.prepare();

    const size_t dspMax = (size_t) std::ceil(maxBlockSize * kDspRate / hostRate) + 4;
    for (auto* v : { &dspInL, &dspInR, &dspEffect, &dspWetL, &dspWetR, &dspOutL, &dspOutR, &dspOutWetL, &dspOutWetR })
        v->reserve(dspMax);
    for (auto* v : { &scratchL, &scratchR, &scratchEffect })
        v->resize((size_t) maxBlockSize);

    smoothCoeff = (float) (1.0 - std::exp(-1.0 / (kSmoothingMs * 0.001 * kDspRate)));
    bypassStep = (float) (1.0 / (kBypassFadeMs * 0.001 * hostRate));

    for (auto* line : { &bypassDelayL, &bypassDelayR })
        line->assign(1, 0.0f); // the measurement below runs process()
    latency = measureLatency();
    for (auto* line : { &bypassDelayL, &bypassDelayR })
        line->assign((size_t) latency + 1, 0.0f);
    reset();
}

void SpxEngine::reset()
{
    downL.reset();
    downR.reset();
    downEffect.reset();
    for (auto* up : { &upL, &upR, &upWetL, &upWetR })
        up->reset();
    er.reset();
    adcFilterL.reset();
    adcFilterR.reset();
    adcFilterEffect.reset();
    dacFilterL.reset();
    dacFilterR.reset();
    dacFilterWetL.reset();
    dacFilterWetR.reset();
    std::fill(bypassDelayL.begin(), bypassDelayL.end(), 0.0f);
    std::fill(bypassDelayR.begin(), bypassDelayR.end(), 0.0f);
    bypassPos = 0;
    snapParameters = true;
}

void SpxEngine::resetEffect()
{
    // The tail is in the reflections' delay lines; the converters around them hold a millisecond at
    // most, gone long before anything is heard again.
    er.reset();
}

int SpxEngine::measureLatency()
{
    // Run an impulse through the direct path and find where it comes out.
    reset();
    SpxSettings dry;
    dry.balance = 0.0f;
    const int block = (int) scratchL.size();
    const int total = (int) (0.05 * hostRate) + block;
    std::vector<float> in((size_t) block, 0.0f), outL((size_t) block), outR((size_t) block);
    int peakAt = 0;
    float peak = 0.0f;
    for (int start = 0; start < total; start += block)
    {
        std::fill(in.begin(), in.end(), 0.0f);
        if (start == 0)
            in[0] = 0.5f;
        process(in.data(), outL.data(), outR.data(), block, dry);
        for (int i = 0; i < block; ++i)
            if (std::abs(outL[(size_t) i]) > peak)
            {
                peak = std::abs(outL[(size_t) i]);
                peakAt = start + i;
            }
    }
    return peakAt;
}

void SpxEngine::processDsp(int numDsp, const SpxSettings& s)
{
    const RomProgram& prog = romProgram(std::clamp(s.program, 1, 30));
    const bool useEr = prog.implemented && prog.algorithm == Algorithm::EarlyReflection;

    const float targetBal = std::clamp(s.balance, 0.0f, 1.0f);
    if (snapParameters)
    {
        balance = targetBal;
        bypassMix = s.bypass ? 1.0f : 0.0f;
        snapParameters = false;
    }

    // Input stage: anti-alias filter, then the 16-bit A/D, for the direct signal and for what the
    // effect hears.
    for (int n = 0; n < numDsp; ++n)
    {
        const auto i = (size_t) n;
        dspInL[i] = quantise16(adcFilterL.process(dspInL[i]));
        dspInR[i] = quantise16(adcFilterR.process(dspInR[i]));
        dspEffect[i] = quantise16(adcFilterEffect.process(dspEffect[i]));
    }

    dspWetL.resize((size_t) numDsp);
    dspWetR.resize((size_t) numDsp);
    if (useEr)
    {
        er.setSettings(s.er);
        er.process(dspEffect.data(), dspWetL.data(), dspWetR.data(), numDsp);
        for (int n = 0; n < numDsp; ++n)
            wetPeak = std::max({ wetPeak, std::abs(dspWetL[(size_t) n]), std::abs(dspWetR[(size_t) n]) });
    }
    else
    {
        std::copy(dspInL.begin(), dspInL.begin() + numDsp, dspWetL.begin());
        std::copy(dspInR.begin(), dspInR.begin() + numDsp, dspWetR.begin());
    }

    // BALANCE is digital; the direct signal also comes out of the D/A.
    for (auto* v : { &dspOutL, &dspOutR, &dspOutWetL, &dspOutWetR })
        v->resize((size_t) numDsp);
    for (int n = 0; n < numDsp; ++n)
    {
        const auto i = (size_t) n;
        balance += smoothCoeff * (targetBal - balance);
        dspOutL[i] = dacFilterL.process(quantise16(dspInL[i] * (1.0f - balance)));
        dspOutR[i] = dacFilterR.process(quantise16(dspInR[i] * (1.0f - balance)));
        dspOutWetL[i] = dacFilterWetL.process(quantise16(dspWetL[i] * balance));
        dspOutWetR[i] = dacFilterWetR.process(quantise16(dspWetR[i] * balance));
    }
}

void SpxEngine::process(const float* directL, const float* directR, const float* effectIn, float* directOutL,
                        float* directOutR, float* wetOutL, float* wetOutR, int numSamples, const SpxSettings& s)
{
    dspInL.clear();
    dspInR.clear();
    dspEffect.clear();
    downL.process(directL, numSamples, dspInL);
    downR.process(directR, numSamples, dspInR);
    downEffect.process(effectIn, numSamples, dspEffect);
    const int numDsp = (int) dspInL.size();
    processDsp(numDsp, s);

    upL.push(dspOutL.data(), numDsp);
    upR.push(dspOutR.data(), numDsp);
    upWetL.push(dspOutWetL.data(), numDsp);
    upWetR.push(dspOutWetR.data(), numDsp);
    upL.pull(directOutL, numSamples);
    upR.pull(directOutR, numSamples);
    upWetL.pull(wetOutL, numSamples);
    upWetR.pull(wetOutR, numSamples);

    // Analog bypass, kept time-aligned with the processed path and crossfaded on toggling: the direct
    // signal takes the analog path, the effect goes quiet.
    const float target = s.bypass ? 1.0f : 0.0f;
    const int size = (int) bypassDelayL.size();
    for (int n = 0; n < numSamples; ++n)
    {
        bypassDelayL[(size_t) bypassPos] = directL[n];
        bypassDelayR[(size_t) bypassPos] = directR[n];
        bypassPos = (bypassPos + 1) % size;
        const float delayedL = bypassDelayL[(size_t) bypassPos], delayedR = bypassDelayR[(size_t) bypassPos];
        bypassMix = target > bypassMix ? std::min(target, bypassMix + bypassStep) : std::max(target, bypassMix - bypassStep);
        if (bypassMix > 0.0f)
        {
            const float processed = 1.0f - bypassMix;
            directOutL[n] = directOutL[n] * processed + delayedL * bypassMix;
            directOutR[n] = directOutR[n] * processed + delayedR * bypassMix;
            wetOutL[n] *= processed;
            wetOutR[n] *= processed;
        }
    }
}

void SpxEngine::process(const float* inL, const float* inR, float* outL, float* outR, int numSamples,
                        const SpxSettings& s)
{
    const int most = (int) scratchEffect.size();
    for (int start = 0; start < numSamples; start += most)
    {
        const int num = std::min(most, numSamples - start);
        for (int n = 0; n < num; ++n)
            scratchEffect[(size_t) n] = 0.5f * (inL[start + n] + inR[start + n]);
        process(inL + start, inR + start, scratchEffect.data(), outL + start, outR + start, scratchL.data(),
                scratchR.data(), num, s);
        for (int n = 0; n < num; ++n)
        {
            outL[start + n] += scratchL[(size_t) n];
            outR[start + n] += scratchR[(size_t) n];
        }
    }
}

} // namespace spx
