#pragma once

#include <vector>

namespace spx
{

enum class ErType
{
    Hall,
    Random,
    Reverse,
    Plate
};

// Parameters of the SPX90 early-reflection algorithm, shared by programs 5 (ER1), 6 (ER2),
// 16 (Gate Reverb) and 17 (Reverse Gate). Values are already on the hardware's grid.
struct ErSettings
{
    ErType type = ErType::Reverse;
    int density = 2;         // 1 = ER1 (low density), 2 = ER2 (high density)
    float roomSize = 3.3f;   // 0.1 - 20.0
    int liveness = 5;        // 0 - 10
    float delayMs = 25.0f;   // 0.1 - 400.0
    float lpfHz = 0.0f;      // 1000 - 11000, 0 = THRU

    bool operator==(const ErSettings&) const = default;
};

// A sparse multi-tap delay reading the measured reflection patterns. Runs at the DSP rate.
class ErEngine
{
public:
    void prepare();
    void reset();

    // Takes effect with a short crossfade from the previous tap set.
    void setSettings(const ErSettings& s);

    void process(const float* in, float* outL, float* outR, int numSamples);

    struct Tap
    {
        int delay;
        float gain;
    };
    // The taps currently in use, for tests.
    const std::vector<Tap>& tapsL() const { return current[0]; }
    const std::vector<Tap>& tapsR() const { return current[1]; }

    static std::vector<Tap> buildTaps(const ErSettings& s, int channel);
    // Same, into `out` without allocating once `out` has room for the longest table.
    static void buildTaps(const ErSettings& s, int channel, std::vector<Tap>& out);

private:
    std::vector<float> memory;
    int writePos = 0;

    ErSettings settings;
    bool hasSettings = false;
    std::vector<Tap> current[2], previous[2];
    int fadeLeft = 0;

    float lpfCoeff = 1.0f;
    float lpfState[2] {};
};

} // namespace spx
