#pragma once

#include <vector>

namespace sway
{

// Finds picking attacks in a guitar signal: a sudden burst of top end. Also rates how hard a strike
// was against the player's own recent strikes. The glide's TRIGGER owns one.
class PickDetector
{
public:
    // A strike at the player's usual level.
    static constexpr float kTypicalStrike = 0.85f;

    void prepare(double sampleRate);
    void reset();

    // 0 - 1: how small a jump in top end counts as an attack. 0: none does, the detector is off.
    void setSensitivity(float sensitivity);

    // One sample in; true on the sample an attack is found.
    bool detect(float input);

    // The top-end level the detector follows (the pick's burst, not the ringing chord under it).
    float envelope() const { return envFast; }
    // How hard a strike with this top-end peak was, 0.15 - 1 (kTypicalStrike at the usual level).
    float strikeStrength(float peak);
    // Attacks found since the plugin started (not cleared by reset), for display and tests.
    int attacks() const { return triggers; }

private:
    double rate = 48000.0;
    float highPassCoeff = 0.3f;
    float highPassLow = 0.0f, highPassLow2 = 0.0f;
    float attackCoeff = 0.0f, releaseCoeff = 0.0f;
    float envFast = 0.0f;
    std::vector<float> levelHistory; // the top-end level over the last 10 ms
    int historyPos = 0;
    float jumpRatio = 1.88f; // level rise that counts as an attack
    bool enabled = true;
    int refractory = 0;
    int triggers = 0;
    float averageStrikeDb = 0.0f;
    bool haveAverageStrike = false;
};

} // namespace sway
