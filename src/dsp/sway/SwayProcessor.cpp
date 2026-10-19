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
    // The search looks at the signal on a ~12 kHz grid whatever the sample rate, so a handover
    // costs the same few tens of thousands of multiply-adds at 44.1 or 192 kHz.
    searchStep = std::max(1, (int) std::lround(sampleRate / 12000.0));
    // Speed (delay change per sample) at which an idle head creeps back to rest: half a cent.
    drift = 1.0 - ratioFromCents(-0.5);
    // A bar left at rest this long, with the head this far from rest, is brought back in one handover.
    recentreAfter = (int) (0.1 * sampleRate);
    recentreThreshold = (int) (0.001 * sampleRate);
    const int length = (int) maxDelay + corrLength + 8;
    for (auto& l : line)
        l.prepare(length);
    sum.prepare(length);
    reset();
}

void DualHeadShifter::reset()
{
    for (auto& l : line)
        l.reset();
    sum.reset();
    active = incoming = rest;
    fadeLeft = 0;
    restingFor = 0;
    recentred = false;
}

void DualHeadShifter::push(const float* in, int numChannels)
{
    channels = std::clamp(numChannels, 1, kMaxChannels);
    float total = 0.0f;
    for (int ch = 0; ch < channels; ++ch)
    {
        line[ch].push(in[ch]);
        total += in[ch];
    }
    if (channels > 1)
        sum.push(total);
}

double DualHeadShifter::correlation(int delayA, int delayB, int stride) const
{
    // The recent past of a head at delayB against the active head's, normalised by delayB's energy
    // (delayA's is the same for every candidate).
    const auto& l = searchLine();
    double ab = 0.0, bb = 1e-12;
    for (int k = 0; k < corrLength; k += stride)
    {
        const float a = l.at(delayA + k), b = l.at(delayB + k);
        ab += (double) a * b;
        bb += (double) b * b;
    }
    return ab / std::sqrt(bb);
}

void DualHeadShifter::startSplice(double nominal, bool preferNominal)
{
    // Search around `nominal` for the start whose recent past matches the active head's: coarse on
    // the search grid, then refined. Delays are whole samples here; the new head keeps the active
    // one's fraction, so the alignment found is the alignment heard.
    const int a = (int) std::lround(active);
    const int centre = (int) std::lround(nominal);
    const int lo = (int) std::ceil(minDelay), hi = (int) std::floor(maxDelay);
    searchCost = 0;
    int best = std::clamp(centre, lo, hi);
    double bestScore = -1e30;
    auto consider = [&](int d, int stride) {
        if (d < lo || d > hi)
            return;
        double score = correlation(a, d, stride);
        searchCost += (corrLength + stride - 1) / stride;
        // Bringing the head back to rest: of two good matches, take the one nearer to rest.
        if (preferNominal)
            score -= 0.5 * std::abs(score) * std::abs(d - centre) / searchRadius;
        if (score > bestScore)
        {
            bestScore = score;
            best = d;
        }
    };
    for (int off = -searchRadius; off <= searchRadius; off += searchStep)
        consider(centre + off, searchStep);
    const int coarse = best;
    const int refineStride = std::max(1, searchStep / 4);
    for (int d = coarse - searchStep + 1; d <= coarse + searchStep - 1; ++d)
        if (d != coarse)
            consider(d, refineStride);
    incoming = std::clamp(active + (best - a), minDelay, maxDelay);

    const auto& l = searchLine();
    const int b = (int) std::lround(incoming);
    double aa = 1e-12, ab = 0.0, bb = 1e-12;
    for (int k = 0; k < corrLength; k += refineStride)
    {
        const float x = l.at(a + k), y = l.at(b + k);
        aa += (double) x * x;
        ab += (double) x * y;
        bb += (double) y * y;
    }
    splicedCorrelation = (float) std::clamp(ab / std::sqrt(aa * bb), 0.0, 1.0);
    fadeLeft = fadeLength;
}

void DualHeadShifter::idle(const float* in, int numChannels)
{
    push(in, numChannels);
    active = incoming = rest;
    fadeLeft = 0;
    restingFor = 0;
    recentred = false;
}

void DualHeadShifter::process(const float* in, float* out, int numChannels, float cents)
{
    push(in, numChannels);
    // Reading at speed r: the delay grows by (1 - r) per sample.
    const double speed = 1.0 - ratioFromCents(cents);
    const bool atRest = std::abs(speed) < drift * 4.0;
    double step = speed;
    if (atRest && fadeLeft == 0)
        step += std::clamp(rest - active, -drift, drift); // (almost) no shift: creep back to rest
    active = std::clamp(active + step, minDelay, maxDelay);

    restingFor = atRest ? restingFor + 1 : 0;
    if (!atRest)
        recentred = false;

    if (fadeLeft == 0)
    {
        // Leave enough room for a whole crossfade at the current speed.
        const double margin = std::abs(speed) * fadeLength + 2.0;
        const double half = 0.5 * (maxDelay - minDelay);
        if (speed > 0.0 && active > maxDelay - margin)
            startSplice(active - half, false);
        else if (speed < 0.0 && active < minDelay + margin)
            startSplice(active + half, false);
        else if (restingFor >= recentreAfter && !recentred && std::abs(active - rest) > recentreThreshold)
        {
            // The bar is back at rest but the head is not: the wet signal would stay early or late
            // against the dry one (a comb filter below 100 % MIX) for the minute the creep takes.
            recentred = true;
            startSplice(rest, true);
        }
    }

    if (fadeLeft == 0)
    {
        for (int ch = 0; ch < channels; ++ch)
            out[ch] = line[ch].read(active);
        return;
    }

    incoming = std::clamp(incoming + step, minDelay, maxDelay);
    // Raised-cosine fade, normalised for constant power given how alike the two heads are: plain
    // amplitude crossfade for identical signals, equal power for unrelated ones.
    const float t = 1.0f - (float) fadeLeft / (float) fadeLength;
    const float gIn = 0.5f - 0.5f * std::cos(3.14159265f * t);
    const float gOut = 1.0f - gIn;
    const float norm = 1.0f / std::sqrt(gIn * gIn + gOut * gOut + 2.0f * splicedCorrelation * gIn * gOut);
    for (int ch = 0; ch < channels; ++ch)
        out[ch] = (gOut * line[ch].read(active) + gIn * line[ch].read(incoming)) * norm;
    if (--fadeLeft == 0)
        active = incoming;
}

void SwayProcessor::prepare(double sampleRate)
{
    bar.prepare(sampleRate);
    main.prepare(sampleRate);
    twin.prepare(sampleRate);
    latency = (int) std::lround(main.restingDelay());
    for (auto& d : dryDelay)
        d.prepare(latency + 8);
    mixStep = (float) (1.0 / (0.02 * sampleRate));
    reset();
}

void SwayProcessor::reset()
{
    bar.reset();
    main.reset();
    twin.reset();
    for (auto& d : dryDelay)
        d.reset();
    twinMix = 0.0f;
}

void SwayProcessor::process(float* const* channels, int numChannels, int numSamples, const SwaySettings& s,
                            const float* detector)
{
    numChannels = std::clamp(numChannels, 1, kMaxChannels);
    const float targetMix = s.enabled ? std::clamp(s.mix, 0.0f, 1.0f) : 0.0f;
    const float targetTwin = std::clamp(s.twinLevel, 0.0f, 1.0f);
    // The twin string responds a little differently to the same bar movement.
    constexpr float kTwinBarScale = 0.85f;

    bar.beginBlock(s);
    for (int n = 0; n < numSamples; ++n)
    {
        float x[kMaxChannels] {}, dry[kMaxChannels] {}, wet[kMaxChannels] {}, second[kMaxChannels] {};
        float total = 0.0f;
        for (int ch = 0; ch < numChannels; ++ch)
        {
            x[ch] = channels[ch][n];
            total += x[ch];
            dryDelay[ch].push(x[ch]);
            dry[ch] = dryDelay[ch].read((double) latency);
        }

        enabledMix += std::clamp(targetMix - enabledMix, -mixStep, mixStep);
        twinMix += std::clamp(targetTwin - twinMix, -mixStep, mixStep);
        const float cents = bar.next(detector != nullptr ? detector[n] : total / (float) numChannels, n, s);
        lastCents = cents * enabledMix; // what is actually applied: 0 while off

        // Voices that are not heard keep their delay lines filled, so they come in seamlessly.
        if (enabledMix <= 0.0f && targetMix <= 0.0f)
        {
            main.idle(x, numChannels);
            twin.idle(x, numChannels);
            for (int ch = 0; ch < numChannels; ++ch)
                channels[ch][n] = dry[ch];
            continue;
        }

        main.process(x, wet, numChannels, cents);
        if (twinMix > 0.0f || targetTwin > 0.0f)
        {
            twin.process(x, second, numChannels, cents * kTwinBarScale + s.twinDetuneCents);
            const float norm = 1.0f / (1.0f + 0.5f * twinMix);
            for (int ch = 0; ch < numChannels; ++ch)
                wet[ch] = (wet[ch] + twinMix * second[ch]) * norm;
        }
        else
            twin.idle(x, numChannels);

        for (int ch = 0; ch < numChannels; ++ch)
            channels[ch][n] = dry[ch] + enabledMix * (wet[ch] - dry[ch]);
    }
}

} // namespace sway
