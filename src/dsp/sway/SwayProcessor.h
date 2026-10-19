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
    float at(int delaySamples) const { return buffer[(size_t) ((writePos - delaySamples) & mask)]; }

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
//
// All channels share the heads, so a stereo signal stays aligned through every handover; the
// handover is searched once, on the channels' sum.
class DualHeadShifter
{
public:
    static constexpr int kMaxChannels = 2;

    void prepare(double sampleRate);
    void reset();

    // One sample per channel in, the shifted samples out.
    void process(const float* in, float* out, int numChannels, float cents);
    float process(float x, float cents)
    {
        float y = 0.0f;
        process(&x, &y, 1, cents);
        return y;
    }
    // Keeps the delay lines current for a voice that is not heard right now, so that it comes in
    // with the present signal; its heads go back to rest.
    void idle(const float* in, int numChannels);

    double restingDelay() const { return rest; }
    // For tests: how far the active head is from rest (samples), and the multiply-adds the last
    // handover search took.
    double offsetFromRest() const { return active - rest; }
    int lastSearchCost() const { return searchCost; }

private:
    void push(const float* in, int numChannels);
    const DelayLine& searchLine() const { return channels == 1 ? line[0] : sum; }
    double correlation(int delayA, int delayB, int stride) const;
    void startSplice(double nominal, bool preferNominal);

    DelayLine line[kMaxChannels];
    DelayLine sum; // the channels added up, for the handover search
    int channels = 1;
    double minDelay = 0.0, maxDelay = 0.0, rest = 0.0, drift = 0.0;
    int fadeLength = 0, searchRadius = 0, corrLength = 0, searchStep = 1;
    int recentreAfter = 0, recentreThreshold = 0;

    double active = 0.0, incoming = 0.0;
    int fadeLeft = 0;
    float splicedCorrelation = 0.0f;
    int restingFor = 0;     // samples the bar has been at rest
    bool recentred = false; // the head was already brought back during this rest
    int searchCost = 0;
};

// The whole Sway block: one bar position drives the pitch shifter (the same bar moves both sides
// when Sway sits after the stereo SPX90), plus the optional twin string.
class SwayProcessor
{
public:
    static constexpr int kMaxChannels = DualHeadShifter::kMaxChannels;

    void prepare(double sampleRate);
    void reset();

    void process(float* io, int numSamples, const SwaySettings& s) { process(&io, 1, numSamples, s); }
    // `detector`, when given, is what the pick-attack detector listens to (the dry guitar when Sway
    // sits after the reverb); otherwise it listens to the channels themselves.
    void process(float* const* channels, int numChannels, int numSamples, const SwaySettings& s,
                 const float* detector = nullptr);

    // Constant delay of the dry path; the wet path is centred on it.
    int latencySamples() const { return latency; }

    // Bar position (cents) and position in the shape's cycle (0 - 1) at the end of the last block,
    // for display.
    float barCents() const { return lastCents; }
    float phase() const { return bar.phase(); }
    int pickAttacks() const { return bar.pickAttacks(); }
    // For tests.
    const DualHeadShifter& mainShifter() const { return main; }

private:
    BarGenerator bar;
    DualHeadShifter main, twin;
    DelayLine dryDelay[kMaxChannels];
    int latency = 0;
    float enabledMix = 0.0f, twinMix = 0.0f, mixStep = 0.0f;
    float lastCents = 0.0f;
};

} // namespace sway
