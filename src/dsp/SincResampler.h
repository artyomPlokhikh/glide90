#pragma once

#include <memory>
#include <vector>

namespace spx
{

// Windowed-sinc kernel shared by the two resamplers, tabulated at a fine phase resolution.
// The cutoff only has to keep the 0-12.5 kHz band flat and reject anything that would alias or
// image into it: the measured converter response, applied at the DSP rate, does the steep part.
class SincTable
{
public:
    SincTable(double cutoffHz, double rateHz, int halfTaps, int phases);

    // Kernel value at a distance of `x` input samples from the centre, |x| < halfTaps.
    float at(double x) const;
    int halfTaps() const { return half; }

private:
    int half;
    int phases;
    std::vector<float> table;
};

// Host rate -> DSP rate. Push host samples, receive however many DSP samples fall due.
class Downsampler
{
public:
    void prepare(double hostRate, double dspRate);
    void reset();

    // Appends produced DSP samples to `out`.
    void process(const float* in, int numIn, std::vector<float>& out);

    // Group delay in host samples.
    double latencyHost() const { return kernelHalf; }

private:
    double step = 1.0;      // host samples per DSP sample
    double nextTime = 0.0;  // position of the next output in host-sample time, relative to history start
    int kernelHalf = 0;
    float gain = 1.0f;
    std::vector<float> history;
    std::unique_ptr<SincTable> kernel;
};

// DSP rate -> host rate. Push DSP samples as they are produced, pull exactly as many host samples
// as the block needs. Reads lag the writes by a fixed number of DSP samples so the pull never
// runs dry even though the number of DSP samples per host block varies by one.
class Upsampler
{
public:
    void prepare(double dspRate, double hostRate, int lagDsp);
    void reset();

    void push(const float* in, int numIn);
    void pull(float* out, int numOut);

    // Delay in host samples from a DSP sample being pushed to it appearing at the output.
    double latencyHost() const;

private:
    double step = 1.0;     // DSP samples per host sample
    double readTime = 0.0; // in DSP samples, relative to buffer start
    int lag = 0;
    double hostPerDsp = 1.0;
    std::vector<float> buffer;
    std::unique_ptr<SincTable> kernel;
};

} // namespace spx
