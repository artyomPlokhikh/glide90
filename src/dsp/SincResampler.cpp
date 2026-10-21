#include "SincResampler.h"

#include <algorithm>
#include <cmath>

namespace spx
{
namespace
{

constexpr double kPi = 3.14159265358979323846;

// Passband 0-12.5 kHz stays flat; the band edge is placed so that nothing above ~18 kHz can alias
// or image below 12.5 kHz. Content that lands in 12.5-15.7 kHz is removed by the converter model.
constexpr double kCutoffHz = 15300.0;
constexpr double kTransitionHz = 5600.0;
constexpr double kStopbandDb = 90.0;
constexpr int kPhases = 512;

double besselI0(double x)
{
    double sum = 1.0, term = 1.0;
    for (int k = 1; k < 40; ++k)
    {
        term *= (x / (2.0 * k)) * (x / (2.0 * k));
        sum += term;
        if (term < 1e-12 * sum)
            break;
    }
    return sum;
}

int halfTapsFor(double rateHz)
{
    // Kaiser estimate of the filter length for the required stopband and transition width.
    const double dw = 2.0 * kPi * kTransitionHz / rateHz;
    const double n = (kStopbandDb - 8.0) / (2.285 * dw);
    return std::max(4, (int) std::ceil(n * 0.5));
}

} // namespace

SincTable::SincTable(double cutoffHz, double rateHz, int halfTaps, int numPhases)
    : half(halfTaps), phases(numPhases)
{
    const double wc = cutoffHz / rateHz; // cycles per input sample
    const double beta = 0.1102 * (kStopbandDb - 8.7);
    const double norm = besselI0(beta);
    const int n = 2 * half * phases + 2;
    table.resize((size_t) n);
    for (int i = 0; i < n; ++i)
    {
        const double x = (double) i / phases - half;
        const double r = x / half;
        const double w = std::abs(r) < 1.0 ? besselI0(beta * std::sqrt(1.0 - r * r)) / norm : 0.0;
        const double s = std::abs(x) < 1e-12 ? 1.0 : std::sin(2.0 * kPi * wc * x) / (2.0 * kPi * wc * x);
        table[(size_t) i] = (float) (2.0 * wc * s * w);
    }
}

float SincTable::at(double x) const
{
    const double p = (x + half) * phases;
    const int i = (int) p;
    if (i < 0 || i + 1 >= (int) table.size())
        return 0.0f;
    const float f = (float) (p - i);
    return table[(size_t) i] + f * (table[(size_t) i + 1] - table[(size_t) i]);
}

void Downsampler::prepare(double hostRate, double dspRate)
{
    step = hostRate / dspRate;
    kernelHalf = halfTapsFor(hostRate);
    // When the host runs slower than the DSP the kernel must band-limit to the host's Nyquist.
    const double cutoff = std::min(kCutoffHz, 0.45 * hostRate);
    kernel = std::make_unique<SincTable>(cutoff, hostRate, kernelHalf, kPhases);
    reset();
}

void Downsampler::reset()
{
    // Primed with a full kernel of silence, so the output lags the input by kernelHalf from the
    // very first block; otherwise the first block comes out short and the upsampler starves once.
    history.assign((size_t) (2 * kernelHalf), 0.0f);
    nextTime = (double) kernelHalf;
}

void Downsampler::process(const float* in, int numIn, std::vector<float>& out)
{
    history.insert(history.end(), in, in + numIn);
    const int size = (int) history.size();

    while (nextTime + kernelHalf < size)
    {
        const int centre = (int) std::floor(nextTime);
        const double frac = nextTime - centre;
        float acc = 0.0f;
        for (int k = -kernelHalf + 1; k <= kernelHalf; ++k)
            acc += history[(size_t) (centre + k)] * kernel->at(k - frac);
        out.push_back(acc);
        nextTime += step;
    }

    const int drop = std::max(0, (int) std::floor(nextTime) - kernelHalf);
    history.erase(history.begin(), history.begin() + drop);
    nextTime -= drop;
}

void Upsampler::prepare(double dspRate, double hostRate, int lagDsp)
{
    step = dspRate / hostRate;
    hostPerDsp = hostRate / dspRate;
    const int half = halfTapsFor(dspRate);
    const double cutoff = std::min(kCutoffHz, 0.45 * hostRate);
    kernel = std::make_unique<SincTable>(cutoff, dspRate, half, kPhases);
    lag = std::max(lagDsp, half + 4);
    reset();
}

void Upsampler::reset()
{
    const int half = kernel->halfTaps();
    buffer.assign((size_t) (lag + half), 0.0f);
    readTime = (double) half;
}

void Upsampler::push(const float* in, int numIn)
{
    buffer.insert(buffer.end(), in, in + numIn);
}

void Upsampler::pull(float* out, int numOut)
{
    const int half = kernel->halfTaps();
    for (int n = 0; n < numOut; ++n)
    {
        const int centre = (int) std::floor(readTime);
        if (centre + half >= (int) buffer.size())
        {
            out[n] = 0.0f; // starved; cannot happen with a sufficient lag
            continue;
        }
        const double frac = readTime - centre;
        float acc = 0.0f;
        for (int k = -half + 1; k <= half; ++k)
            acc += buffer[(size_t) (centre + k)] * kernel->at(k - frac);
        out[n] = acc;
        readTime += step;
    }

    const int drop = std::max(0, (int) std::floor(readTime) - half);
    buffer.erase(buffer.begin(), buffer.begin() + drop);
    readTime -= drop;
}

double Upsampler::latencyHost() const
{
    return lag * hostPerDsp;
}

} // namespace spx
