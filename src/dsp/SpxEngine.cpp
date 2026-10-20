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
    downL.prepare(hostRate, kDspRate);
    downR.prepare(hostRate, kDspRate);
    downSend.prepare(hostRate, kDspRate);
    const int lag = 8 + (int) std::ceil(kDspRate / hostRate); // block-to-block jitter margin
    upL.prepare(kDspRate, hostRate, lag);
    upR.prepare(kDspRate, hostRate, lag);
    er.prepare();

    const size_t dspMax = (size_t) std::ceil(maxBlockSize * kDspRate / hostRate) + 4;
    for (auto* v : { &dspInL, &dspInR, &dspSum, &dspWetL, &dspWetR, &dspOutL, &dspOutR, &dspSend })
        v->reserve(dspMax);
    {
        // Grow the send's history once here rather than on the audio thread.
        const std::vector<float> silence((size_t) maxBlockSize, 0.0f);
        downSend.process(silence.data(), maxBlockSize, dspSend);
        dspSend.clear();
    }
    scratchL.resize((size_t) maxBlockSize);
    scratchR.resize((size_t) maxBlockSize);

    smoothCoeff = (float) (1.0 - std::exp(-1.0 / (kSmoothingMs * 0.001 * kDspRate)));
    bypassStep = (float) (1.0 / (kBypassFadeMs * 0.001 * hostRate));
    analogCoeff = (float) (1.0 - std::exp(-1.0 / (kSmoothingMs * 0.001 * hostRate)));

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
    downSend.reset();
    adcFilterSend.reset();
    sendActive = false;
    upL.reset();
    upR.reset();
    er.reset();
    adcFilterL.reset();
    adcFilterR.reset();
    dacFilterL.reset();
    dacFilterR.reset();
    std::fill(bypassDelayL.begin(), bypassDelayL.end(), 0.0f);
    std::fill(bypassDelayR.begin(), bypassDelayR.end(), 0.0f);
    bypassPos = 0;
    snapParameters = true;
    analogOut = -1.0f;
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
    const float targetOut = std::clamp(s.outLevel, 0.0f, 1.0f); // OUT LVL is digital, before the D/A
    if (snapParameters)
    {
        gainIn = targetIn;
        balance = targetBal;
        outGain = targetOut;
        bypassMix = s.bypass ? 1.0f : 0.0f;
        snapParameters = false;
    }

    // Input stage: analog gain and anti-alias filter, then the 16-bit A/D. The effect hears the sum
    // of the two inputs (the unit has one); with the same signal on both, that is the signal itself.
    dspSum.resize((size_t) numDsp);
    for (int n = 0; n < numDsp; ++n)
    {
        gainIn += smoothCoeff * (targetIn - gainIn);
        const auto i = (size_t) n;
        dspInL[i] = quantise16(adcFilterL.process(dspInL[i] * gainIn));
        dspInR[i] = quantise16(adcFilterR.process(dspInR[i] * gainIn));
        dspSum[i] = 0.5f * (dspInL[i] + dspInR[i]);
    }
    if (sendActive)
        for (int n = 0; n < numDsp; ++n)
            dspSum[(size_t) n] += quantise16(adcFilterSend.process(dspSend[(size_t) n] * gainIn));

    dspWetL.resize((size_t) numDsp);
    dspWetR.resize((size_t) numDsp);
    if (useEr)
    {
        er.setSettings(s.er);
        er.process(dspSum.data(), dspWetL.data(), dspWetR.data(), numDsp);
        for (int n = 0; n < numDsp; ++n)
            wetPeak = std::max({ wetPeak, std::abs(dspWetL[(size_t) n]), std::abs(dspWetR[(size_t) n]) });
    }
    else
    {
        std::copy(dspInL.begin(), dspInL.begin() + numDsp, dspWetL.begin());
        std::copy(dspInR.begin(), dspInR.begin() + numDsp, dspWetR.begin());
    }

    // BALANCE and OUT LVL are digital; the direct signal also comes out of the D/A.
    dspOutL.resize((size_t) numDsp);
    dspOutR.resize((size_t) numDsp);
    for (int n = 0; n < numDsp; ++n)
    {
        balance += smoothCoeff * (targetBal - balance);
        outGain += smoothCoeff * (targetOut - outGain);
        const float l = (dspInL[(size_t) n] * (1.0f - balance) + dspWetL[(size_t) n] * balance) * outGain;
        const float r = (dspInR[(size_t) n] * (1.0f - balance) + dspWetR[(size_t) n] * balance) * outGain;
        dspOutL[(size_t) n] = dacFilterL.process(quantise16(l));
        dspOutR[(size_t) n] = dacFilterR.process(quantise16(r));
    }
}

void SpxEngine::process(const float* inL, const float* inR, float* outL, float* outR, int numSamples,
                        const SpxSettings& s, const float* send)
{
    if (send != nullptr && !sendActive)
    {
        downSend.restartAlongside(downL);
        adcFilterSend.reset();
        sendActive = true;
    }
    else if (send == nullptr)
        sendActive = false;

    dspInL.clear();
    dspInR.clear();
    dspSend.clear();
    downL.process(inL, numSamples, dspInL);
    downR.process(inR, numSamples, dspInR);
    if (sendActive)
        downSend.process(send, numSamples, dspSend);
    const int numDsp = (int) dspInL.size();
    processDsp(numDsp, s);

    upL.push(dspOutL.data(), numDsp);
    upR.push(dspOutR.data(), numDsp);
    upL.pull(outL, numSamples);
    upR.pull(outR, numSamples);

    // Analog bypass, kept time-aligned with the processed path and crossfaded on toggling.
    const float bypassGain = inputGain(s.inputKnob, s.inputMinus20);
    // The rear-panel output switch is analog, after the D/A: it scales the converted output (and the
    // bypass) without costing resolution.
    const float analogTarget = s.outputMinus20 ? dbToGain(-kLevelSwitchDb) : 1.0f;
    const float target = s.bypass ? 1.0f : 0.0f;
    const int size = (int) bypassDelayL.size();
    if (analogOut < 0.0f)
        analogOut = analogTarget; // first block after a reset
    for (int n = 0; n < numSamples; ++n)
    {
        bypassDelayL[(size_t) bypassPos] = inL[n] * bypassGain;
        bypassDelayR[(size_t) bypassPos] = inR[n] * bypassGain;
        bypassPos = (bypassPos + 1) % size;
        const float delayedL = bypassDelayL[(size_t) bypassPos], delayedR = bypassDelayR[(size_t) bypassPos];
        bypassMix = target > bypassMix ? std::min(target, bypassMix + bypassStep) : std::max(target, bypassMix - bypassStep);
        if (bypassMix > 0.0f)
        {
            outL[n] = outL[n] * (1.0f - bypassMix) + delayedL * bypassMix;
            outR[n] = outR[n] * (1.0f - bypassMix) + delayedR * bypassMix;
        }
        analogOut += analogCoeff * (analogTarget - analogOut);
        outL[n] *= analogOut;
        outR[n] *= analogOut;
    }
}

} // namespace spx
