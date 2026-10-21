#pragma once

#include "BarGenerator.h"

#include <array>
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

// Bends one path by the glide bar: the pitch shifter plus the optional twin string, mixed with the
// path itself by MIX. The path comes out delayed by latencySamples() whether it is bent or not, so
// bent and straight paths stay in time with each other.
class Bender
{
public:
    static constexpr int kMaxChannels = DualHeadShifter::kMaxChannels;

    void prepare(double sampleRate);
    void reset();

    // `cents`: the bar, one value per sample. With `bend` false the path fades (20 ms) to itself,
    // delayed; the shifters then only keep their delay lines filled.
    void process(float* const* channels, int numChannels, int numSamples, const SwaySettings& s, const float* cents,
                 bool bend);

    int latencySamples() const { return latency; }
    // How far the bend is in now (0 - MIX): 0 while straight.
    float mix() const { return enabledMix; }
    const DualHeadShifter& mainShifter() const { return main; }

private:
    DualHeadShifter main, twin;
    DelayLine dryDelay[kMaxChannels];
    int latency = 0;
    float enabledMix = 0.0f, twinMix = 0.0f, mixStep = 0.0f;
};

// The whole Sway block: one bar position drives the pitch shifter (the same bar moves both sides
// when Sway sits after the stereo SPX90), plus the optional twin string.
class SwayProcessor
{
public:
    static constexpr int kMaxChannels = Bender::kMaxChannels;

    void prepare(double sampleRate);
    void reset();

    void process(float* io, int numSamples, const SwaySettings& s) { process(&io, 1, numSamples, s); }
    // `detector`, when given, is what the pick-attack detector listens to (the dry guitar when Sway
    // sits after the reverb); otherwise it listens to the channels themselves.
    void process(float* const* channels, int numChannels, int numSamples, const SwaySettings& s,
                 const float* detector = nullptr);

    // The same in two steps, for when something else follows the bar too (HOLD, other paths): first
    // where the bar is, one value per sample in cents, from what the pick detector listens to; then
    // the shifting, from those positions. Call computeBar() once per block, or for pieces of one with
    // `continuing` set after the first.
    void computeBar(const float* detector, int numSamples, const SwaySettings& s, float* centsOut,
                    bool continuing = false);
    void processWithBar(float* const* channels, int numChannels, int numSamples, const SwaySettings& s,
                        const float* cents);

    // Constant delay of the dry path; the wet path is centred on it.
    int latencySamples() const { return bender.latencySamples(); }

    // Bar position (cents) and position in the shape's cycle (0 - 1) at the end of the last block,
    // for display.
    float barCents() const { return lastCents; }
    float phase() const { return bar.phase(); }
    int pickAttacks() const { return bar.pickAttacks(); }
    // For tests.
    const DualHeadShifter& mainShifter() const { return bender.mainShifter(); }

private:
    BarGenerator bar;
    Bender bender;
    float lastCents = 0.0f;
    // process(): the detector's input and the bar, a piece of the block at a time.
    static constexpr int kPiece = 256;
    std::array<float, kPiece> pieceDetector {}, pieceBar {};
};

} // namespace sway
