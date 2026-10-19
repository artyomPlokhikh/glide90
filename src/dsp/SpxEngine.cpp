#include "SpxEngine.h"

#include "RomPrograms.h"

#include <algorithm>
#include <cmath>

namespace spx
{
namespace
{

constexpr float kLevelSwitchDb = 24.0f; // -20 dBm vs +4 dBm
constexpr double kSmoothingMs = 20.0;
constexpr double kBypassFadeMs = 10.0;

float dbToGain(float db)
{
    return std::pow(10.0f, db / 20.0f);
}

} // namespace

float SpxEngine::inputGain(float knob, bool minus20)
{
    // Unity at 8, +12 dB at 10 (spec: max gain +12 dB); audio taper below unity.
    const float k = std::clamp(knob, 0.0f, 10.0f);
    const float g = k >= 8.0f ? dbToGain(6.0f * (k - 8.0f)) : std::pow(k / 8.0f, 3.0f);
    return minus20 ? g * dbToGain(kLevelSwitchDb) : g;
}

void SpxEngine::prepare(double rate, int maxBlockSize)
{
    hostRate = rate;
    down.prepare(hostRate, kDspRate);
    const int lag = 8 + (int) std::ceil(kDspRate / hostRate); // block-to-block jitter margin
    upL.prepare(kDspRate, hostRate, lag);
    upR.prepare(kDspRate, hostRate, lag);
    er.prepare();

    const size_t dspMax = (size_t) std::ceil(maxBlockSize * kDspRate / hostRate) + 4;
    for (auto* v : { &dspIn, &dspWetL, &dspWetR, &dspOutL, &dspOutR })
        v->reserve(dspMax);
    scratchL.resize((size_t) maxBlockSize);
    scratchR.resize((size_t) maxBlockSize);

    smoothCoeff = (float) (1.0 - std::exp(-1.0 / (kSmoothingMs * 0.001 * kDspRate)));
    bypassStep = (float) (1.0 / (kBypassFadeMs * 0.001 * hostRate));

    bypassDelay.assign(1, 0.0f); // the measurement below runs process()
    latency = measureLatency();
    bypassDelay.assign((size_t) latency + 1, 0.0f);
    reset();
}

void SpxEngine::reset()
{
    down.reset();
    upL.reset();
    upR.reset();
    er.reset();
    adcFilter.reset();
    dacFilterL.reset();
    dacFilterR.reset();
    std::fill(bypassDelay.begin(), bypassDelay.end(), 0.0f);
    bypassPos = 0;
    snapParameters = true;
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

    const float targetIn = inputGain(s.inputKnob, s.inputMinus20);
    const float targetBal = std::clamp(s.balance, 0.0f, 1.0f);
    const float targetOut = std::clamp(s.outLevel, 0.0f, 1.0f) * (s.outputMinus20 ? dbToGain(-kLevelSwitchDb) : 1.0f);
    if (snapParameters)
    {
        gainIn = targetIn;
        balance = targetBal;
        outGain = targetOut;
        bypassMix = s.bypass ? 1.0f : 0.0f;
        snapParameters = false;
    }

    // Input stage: analog gain and anti-alias filter, then the 16-bit A/D.
    for (int n = 0; n < numDsp; ++n)
    {
        gainIn += smoothCoeff * (targetIn - gainIn);
        dspIn[(size_t) n] = quantise16(adcFilter.process(dspIn[(size_t) n] * gainIn));
    }

    dspWetL.resize((size_t) numDsp);
    dspWetR.resize((size_t) numDsp);
    if (useEr)
    {
        er.setSettings(s.er);
        er.process(dspIn.data(), dspWetL.data(), dspWetR.data(), numDsp);
    }
    else
    {
        std::copy(dspIn.begin(), dspIn.begin() + numDsp, dspWetL.begin());
        std::copy(dspIn.begin(), dspIn.begin() + numDsp, dspWetR.begin());
    }

    // BALANCE and OUT LVL are digital; the direct signal also comes out of the D/A.
    dspOutL.resize((size_t) numDsp);
    dspOutR.resize((size_t) numDsp);
    for (int n = 0; n < numDsp; ++n)
    {
        balance += smoothCoeff * (targetBal - balance);
        outGain += smoothCoeff * (targetOut - outGain);
        const float dry = dspIn[(size_t) n] * (1.0f - balance);
        const float l = (dry + dspWetL[(size_t) n] * balance) * outGain;
        const float r = (dry + dspWetR[(size_t) n] * balance) * outGain;
        dspOutL[(size_t) n] = dacFilterL.process(quantise16(l));
        dspOutR[(size_t) n] = dacFilterR.process(quantise16(r));
    }
}

void SpxEngine::process(const float* monoIn, float* outL, float* outR, int numSamples, const SpxSettings& s)
{
    dspIn.clear();
    down.process(monoIn, numSamples, dspIn);
    const int numDsp = (int) dspIn.size();
    processDsp(numDsp, s);

    upL.push(dspOutL.data(), numDsp);
    upR.push(dspOutR.data(), numDsp);
    upL.pull(outL, numSamples);
    upR.pull(outR, numSamples);

    // Analog bypass, kept time-aligned with the processed path and crossfaded on toggling.
    const float bypassGain = inputGain(s.inputKnob, s.inputMinus20) * (s.outputMinus20 ? dbToGain(-kLevelSwitchDb) : 1.0f);
    const float target = s.bypass ? 1.0f : 0.0f;
    const int size = (int) bypassDelay.size();
    for (int n = 0; n < numSamples; ++n)
    {
        bypassDelay[(size_t) bypassPos] = monoIn[n] * bypassGain;
        bypassPos = (bypassPos + 1) % size;
        const float delayed = bypassDelay[(size_t) bypassPos];
        bypassMix = target > bypassMix ? std::min(target, bypassMix + bypassStep) : std::max(target, bypassMix - bypassStep);
        if (bypassMix > 0.0f)
        {
            outL[n] = outL[n] * (1.0f - bypassMix) + delayed * bypassMix;
            outR[n] = outR[n] * (1.0f - bypassMix) + delayed * bypassMix;
        }
    }
}

} // namespace spx
