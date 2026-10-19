#pragma once

#include "Converter.h"
#include "ErEngine.h"
#include "SincResampler.h"

#include <vector>

namespace spx
{

struct SpxSettings
{
    int program = 17;
    ErSettings er = {};
    float balance = 1.0f;     // 0 = direct only, 1 = effect only
    float outLevel = 1.0f;    // 0 - 1
    float inputKnob = 8.0f;   // front-panel INPUT, 0 - 10, unity at 8
    bool inputMinus20 = false;  // rear-panel level switches: -20 dBm instead of +4 dBm
    bool outputMinus20 = false;
    bool bypass = false;      // analog bypass: full bandwidth, INPUT knob still applies
};

// The whole SPX90: mono in, stereo out, at the host rate. Internally the signal is resampled to
// the DSP rate, filtered by the measured converter response, quantised to 16 bits, processed by
// the selected program and mixed with the (equally band-limited) direct signal, as in the unit.
class SpxEngine
{
public:
    void prepare(double hostRate, int maxBlockSize);
    void reset();

    void process(const float* monoIn, float* outL, float* outR, int numSamples, const SpxSettings& s);

    // Constant delay from input to output in host samples.
    int latencySamples() const { return latency; }

    static float inputGain(float knob, bool minus20);

private:
    void processDsp(int numDsp, const SpxSettings& s);
    int measureLatency();

    double hostRate = 48000.0;
    int latency = 0;

    Downsampler down;
    Upsampler upL, upR;
    ConverterFilter<tables::converterIn> adcFilter;
    ConverterFilter<tables::converterOut> dacFilterL, dacFilterR;
    ErEngine er;

    std::vector<float> dspIn, dspWetL, dspWetR, dspOutL, dspOutR;

    // Smoothed at the DSP rate.
    float gainIn = 1.0f, balance = 1.0f, outGain = 1.0f;
    float smoothCoeff = 0.0f;
    bool snapParameters = true; // jump straight to the settings after a reset

    // Bypass: the analog path, delayed to stay aligned with the processed one.
    std::vector<float> bypassDelay;
    int bypassPos = 0;
    float bypassMix = 0.0f;
    float bypassStep = 0.0f;
    std::vector<float> scratchL, scratchR;
};

} // namespace spx
