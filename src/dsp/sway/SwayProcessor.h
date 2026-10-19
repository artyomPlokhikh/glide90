#pragma once

#include "BarGenerator.h"

#include <vector>

namespace sway
{

// Fractional-delay line with 4-point Hermite interpolation.
class DelayLine
{
public:
    void prepare(int maxDelaySamples);
    void reset();
    void push(float x);
    float read(double delaySamples) const; // 1 <= delay < size - 2

private:
    std::vector<float> buffer;
    int mask = 0;
    int writePos = 0;
};

// Holds any pitch offset for as long as needed: one read head moves through a delay at the
// shifted speed, and before it runs out of range a second head takes over further along. The
// second head's start is chosen where the signal lines up with the first one (best correlation
// over the last few milliseconds), so the crossfade neither cancels nor jumps in phase. Used for
// both the wobble (which mostly stays within the head's range) and held bar positions.
class DualHeadShifter
{
public:
    void prepare(double sampleRate);
    void reset();
    float process(float x, float cents);
    double restingDelay() const { return rest; }

private:
    void startSplice(double direction);
    double correlationAt(double delayA, double delayB, int stride) const;

    DelayLine line;
    double minDelay = 0.0, maxDelay = 0.0, rest = 0.0, drift = 0.0;
    int fadeLength = 0, searchRadius = 0, corrLength = 0;

    double active = 0.0, incoming = 0.0;
    int fadeLeft = 0;
    float splicedCorrelation = 0.0f;
};

// The whole Sway block: one bar position drives a pitch shifter per channel (the same bar moves
// both sides when Sway sits after the stereo SPX90), plus the optional twin string.
class SwayProcessor
{
public:
    static constexpr int kMaxChannels = 2;

    void prepare(double sampleRate);
    void reset();

    void process(float* io, int numSamples, const SwaySettings& s) { process(&io, 1, numSamples, s); }
    void process(float* const* channels, int numChannels, int numSamples, const SwaySettings& s);

    // Constant delay of the dry path; the wet path is centred on it.
    int latencySamples() const { return latency; }

    // Bar position (cents) and position in the shape's cycle (0 - 1) at the end of the last block,
    // for display.
    float barCents() const { return lastCents; }
    float phase() const { return bar.phase(); }
    int pickAttacks() const { return bar.pickAttacks(); }

private:
    BarGenerator bar;
    DualHeadShifter main[kMaxChannels], twin[kMaxChannels];
    DelayLine dryDelay[kMaxChannels];
    int latency = 0;
    float enabledMix = 0.0f, mixStep = 0.0f;
    float lastCents = 0.0f;
};

} // namespace sway
