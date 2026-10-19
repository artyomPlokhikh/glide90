#pragma once

#include <cstdint>

namespace sway
{

struct SwaySettings
{
    bool enabled = true;
    float depthCents = 25.0f;   // 0 - 100: how far the hand moves the bar
    float rateHz = 3.0f;        // 0.3 - 8: speed of the wobble
    float shape = 0.4f;         // 0 = steady LFO, 1 = irregular hand movement
    float strumSync = 0.3f;     // 0 - 1: share of the movement driven by picking attacks
    float sensitivity = 0.5f;   // 0 - 1: attack detector sensitivity
    float asymmetry = 0.0f;     // 0 = bar only goes down (Jazzmaster), 1 = symmetric around pitch
    float barCents = 0.0f;      // manual bar position, 0 to -200 cents (automation, pitch bend, CC)
    float twinDetuneCents = 0.0f; // 0 - 30: second string tuned slightly off
    float twinLevel = 0.0f;     // 0 - 1
    float mix = 1.0f;           // 0 - 1
};

// Position of a vibrato bar in cents (negative = pitch down), one value per sample.
class BarGenerator
{
public:
    void prepare(double sampleRate);
    void reset();

    // `input` is the dry signal, used to detect picking attacks.
    float next(float input, const SwaySettings& s);

private:
    float uniform();

    double rate = 48000.0;
    double phase = 0.0;
    float jitter = 0.0f, jitterTarget = 0.0f;
    int jitterCountdown = 0;

    float randTarget = 0.0f, randA = 0.0f, randB = 0.0f;
    int randCountdown = 0;

    float envFast = 0.0f, envSlow = 0.0f;
    int refractory = 0;
    float push = 0.0f;
    bool pushRising = false;

    float smoothed = 0.0f;
    uint32_t seed = 0x1234567u;
};

} // namespace sway
