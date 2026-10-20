#pragma once

#include "Converter.h"
#include "ErEngine.h"
#include "SincResampler.h"

#include <utility>
#include <vector>

namespace spx
{

struct SpxSettings
{
    int program = 17;
    ErSettings er = {};
    float balance = 1.0f; // 0 = direct only, 1 = effect only
    bool bypass = false;  // analog bypass: full bandwidth
};

// The whole SPX90 at unity gain (the plugin's INPUT and OUTPUT do the levels): mono or stereo in,
// stereo out, at the host rate. The unit has one input: with two,
// the effect hears their sum and the direct signal keeps its sides. Internally the signal is resampled to
// the DSP rate, filtered by the measured converter response, quantised to 16 bits, processed by
// the selected program and mixed with the (equally band-limited) direct signal, as in the unit.
class SpxEngine
{
public:
    void prepare(double hostRate, int maxBlockSize);
    void reset();

    // `send`, when given, is heard by the effect only (added to what it hears, after the A/D), never
    // by the direct signal: a return from HOLD into the reverse gate.
    void process(const float* inL, const float* inR, float* outL, float* outR, int numSamples, const SpxSettings& s,
                 const float* send = nullptr);
    void process(const float* monoIn, float* outL, float* outR, int numSamples, const SpxSettings& s)
    {
        process(monoIn, monoIn, outL, outR, numSamples, s);
    }

    // Peak of the effect alone (before BALANCE) since the last call: how loud the reverse swell is.
    float takeWetPeak() { return std::exchange(wetPeak, 0.0f); }

    // Constant delay from input to output in host samples.
    int latencySamples() const { return latency; }

private:
    void processDsp(int numDsp, const SpxSettings& s);
    int measureLatency();

    double hostRate = 48000.0;
    int latency = 0;

    Downsampler downL, downR, downSend;
    Upsampler upL, upR;
    ConverterFilter<tables::converterIn> adcFilterL, adcFilterR, adcFilterSend;
    ConverterFilter<tables::converterOut> dacFilterL, dacFilterR;
    ErEngine er;

    std::vector<float> dspInL, dspInR, dspSum, dspWetL, dspWetR, dspOutL, dspOutR, dspSend;
    bool sendActive = false; // the send's chain is running, in step with the inputs

    // Smoothed at the DSP rate.
    float balance = 1.0f;
    float wetPeak = 0.0f;
    float smoothCoeff = 0.0f;
    bool snapParameters = true; // jump straight to the settings after a reset

    // Bypass: the analog path, delayed to stay aligned with the processed one.
    std::vector<float> bypassDelayL, bypassDelayR;
    int bypassPos = 0;
    float bypassMix = 0.0f;
    float bypassStep = 0.0f;
    std::vector<float> scratchL, scratchR;
};

} // namespace spx
