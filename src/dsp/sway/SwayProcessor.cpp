#include "SwayProcessor.h"

#include <algorithm>
#include <cmath>

namespace sway
{
namespace
{

constexpr double kLn2Over1200 = 0.000577622650466621;

double ratioFromCents(double cents)
{
    return std::exp(cents * kLn2Over1200);
}

} // namespace

void DelayLine::prepare(int maxDelaySamples)
{
    int size = 4;
    while (size < maxDelaySamples + 4)
        size <<= 1;
    buffer.assign((size_t) size, 0.0f);
    mask = size - 1;
    writePos = 0;
}

void DelayLine::reset()
{
    std::fill(buffer.begin(), buffer.end(), 0.0f);
    writePos = 0;
}

void DelayLine::push(float x)
{
    writePos = (writePos + 1) & mask;
    buffer[(size_t) writePos] = x;
}

float DelayLine::read(double delaySamples) const
{
    const double pos = writePos - delaySamples;
    const int i = (int) std::floor(pos);
    const float t = (float) (pos - i);
    const float y0 = buffer[(size_t) ((i - 1) & mask)];
    const float y1 = buffer[(size_t) (i & mask)];
    const float y2 = buffer[(size_t) ((i + 1) & mask)];
    const float y3 = buffer[(size_t) ((i + 2) & mask)];
    const float c1 = 0.5f * (y2 - y0);
    const float c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
    const float c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
    return ((c3 * t + c2) * t + c1) * t + y1;
}

void DualHeadShifter::prepare(double sampleRate)
{
    minDelay = 0.001 * sampleRate;
    maxDelay = 0.051 * sampleRate;
    rest = 0.5 * (minDelay + maxDelay);
    fadeLength = (int) (0.015 * sampleRate);
    searchRadius = (int) (0.0125 * sampleRate); // one period of the low E string
    corrLength = (int) (0.010 * sampleRate);
    // Speed (delay change per sample) at which an idle head creeps back to rest: half a cent.
    drift = 1.0 - ratioFromCents(-0.5);
    line.prepare((int) maxDelay + corrLength + 8);
    reset();
}

void DualHeadShifter::reset()
{
    line.reset();
    active = incoming = rest;
    fadeLeft = 0;
}

double DualHeadShifter::correlationAt(double delayA, double delayB, int stride) const
{
    double ab = 0.0, bb = 1e-12;
    for (int k = 0; k < corrLength; k += stride)
    {
        const float a = line.read(delayA + k);
        const float b = line.read(delayB + k);
        ab += (double) a * b;
        bb += (double) b * b;
    }
    return ab / std::sqrt(bb);
}

void DualHeadShifter::startSplice(double direction)
{
    // Jump about half the range back towards the other end, then search for the start whose recent
    // past matches the active head's: coarse on every other sample, then refined.
    const double nominal = active + direction * 0.5 * (maxDelay - minDelay);
    double best = nominal, bestScore = -1e30;
    for (int off = -searchRadius; off <= searchRadius; off += 2)
    {
        const double d = nominal + off;
        const double score = correlationAt(active, d, 2);
        if (score > bestScore)
        {
            bestScore = score;
            best = d;
        }
    }
    const double coarse = best;
    for (int off = -1; off <= 1; ++off)
    {
        const double score = correlationAt(active, coarse + off, 1);
        if (score > bestScore || off == -1)
        {
            bestScore = score;
            best = coarse + off;
        }
    }
    incoming = std::clamp(best, minDelay, maxDelay);

    double aa = 1e-12, ab = 0.0, bb = 1e-12;
    for (int k = 0; k < corrLength; ++k)
    {
        const float a = line.read(active + k), b = line.read(incoming + k);
        aa += (double) a * a;
        ab += (double) a * b;
        bb += (double) b * b;
    }
    splicedCorrelation = (float) std::clamp(ab / std::sqrt(aa * bb), 0.0, 1.0);
    fadeLeft = fadeLength;
}

float DualHeadShifter::process(float x, float cents)
{
    line.push(x);
    // Reading at speed r: the delay grows by (1 - r) per sample.
    const double speed = 1.0 - ratioFromCents(cents);
    double step = speed;
    if (std::abs(speed) < drift * 4.0 && fadeLeft == 0)
        step += std::clamp(rest - active, -drift, drift); // (almost) no shift: creep back to rest
    active = std::clamp(active + step, minDelay, maxDelay);

    if (fadeLeft == 0)
    {
        // Leave enough room for a whole crossfade at the current speed.
        const double margin = std::abs(speed) * fadeLength + 2.0;
        if (speed > 0.0 && active > maxDelay - margin)
            startSplice(-1.0);
        else if (speed < 0.0 && active < minDelay + margin)
            startSplice(1.0);
    }

    if (fadeLeft == 0)
        return line.read(active);

    incoming = std::clamp(incoming + step, minDelay, maxDelay);
    // Raised-cosine fade, normalised for constant power given how alike the two heads are: plain
    // amplitude crossfade for identical signals, equal power for unrelated ones.
    const float t = 1.0f - (float) fadeLeft / (float) fadeLength;
    const float gIn = 0.5f - 0.5f * std::cos(3.14159265f * t);
    const float gOut = 1.0f - gIn;
    const float norm = 1.0f / std::sqrt(gIn * gIn + gOut * gOut + 2.0f * splicedCorrelation * gIn * gOut);
    const float y = (gOut * line.read(active) + gIn * line.read(incoming)) * norm;
    if (--fadeLeft == 0)
        active = incoming;
    return y;
}

void SwayProcessor::prepare(double sampleRate)
{
    bar.prepare(sampleRate);
    for (int ch = 0; ch < kMaxChannels; ++ch)
    {
        main[ch].prepare(sampleRate);
        twin[ch].prepare(sampleRate);
    }
    latency = (int) std::lround(main[0].restingDelay());
    for (auto& d : dryDelay)
        d.prepare(latency + 8);
    mixStep = (float) (1.0 / (0.02 * sampleRate));
    reset();
}

void SwayProcessor::reset()
{
    bar.reset();
    for (int ch = 0; ch < kMaxChannels; ++ch)
    {
        main[ch].reset();
        twin[ch].reset();
        dryDelay[ch].reset();
    }
}

void SwayProcessor::process(float* const* channels, int numChannels, int numSamples, const SwaySettings& s)
{
    numChannels = std::clamp(numChannels, 1, kMaxChannels);
    const float targetMix = s.enabled ? std::clamp(s.mix, 0.0f, 1.0f) : 0.0f;
    const float twinGain = std::clamp(s.twinLevel, 0.0f, 1.0f);
    const float twinNorm = 1.0f / (1.0f + 0.5f * twinGain);
    // The twin string responds a little differently to the same bar movement.
    constexpr float kTwinBarScale = 0.85f;

    bar.beginBlock(s);
    for (int n = 0; n < numSamples; ++n)
    {
        float sum = 0.0f;
        for (int ch = 0; ch < numChannels; ++ch)
            sum += channels[ch][n];

        enabledMix += std::clamp(targetMix - enabledMix, -mixStep, mixStep);
        const float cents = bar.next(sum / (float) numChannels, n, s);
        lastCents = cents * enabledMix; // what is actually applied: 0 while off
        const bool off = enabledMix <= 0.0f && targetMix <= 0.0f;

        for (int ch = 0; ch < numChannels; ++ch)
        {
            const float x = channels[ch][n];
            dryDelay[ch].push(x);
            const float dry = dryDelay[ch].read((double) latency);
            if (off)
            {
                // Keep the delay line filled so switching on is seamless.
                main[ch].process(x, 0.0f);
                channels[ch][n] = dry;
                continue;
            }

            float wet = main[ch].process(x, cents);
            if (twinGain > 0.0f)
            {
                const float t = twin[ch].process(x, cents * kTwinBarScale + s.twinDetuneCents);
                wet = (wet + twinGain * t) * twinNorm;
            }
            channels[ch][n] = dry + enabledMix * (wet - dry);
        }
    }
}

} // namespace sway
