#pragma once

#include "PickDetector.h"
#include "Shape.h"

#include <cstdint>
#include <vector>

namespace sway
{

enum class GlideMode
{
    Loop,    // the shape repeats, once per cycle
    Trigger, // each picking attack plays the shape once, over one cycle
    Manual,  // no movement of its own: the bar follows `manual`
};

enum class BendDirection
{
    Down, // pushing only lowers the pitch
    Both, // around the open pitch
    Up,
};

struct SwaySettings
{
    bool enabled = true;
    GlideMode mode = GlideMode::Loop;
    const Shape* shape = nullptr; // nullptr: sine

    // One cycle: cycleSeconds, or with `sync` syncBeats at the host's tempo. LOOP then also keeps its
    // phase on the host's beat position.
    bool sync = false;
    double cycleSeconds = 0.33;
    double syncBeats = 1.0;       // one cycle in quarter notes
    double bpm = 120.0;
    double hostBeats = -1.0;      // host position in quarter notes at the block start; < 0 when stopped

    float depthCents = 30.0f;     // 0 - 300
    BendDirection direction = BendDirection::Down;
    float random = 0.15f;         // 0 - 1: cycle-to-cycle variation of depth and timing, plus drift
    float smooth = 0.2f;          // 0 - 1: how lazily the hand follows (2 - 150 ms)

    float sensitivity = 0.5f;     // Trigger: attack detector sensitivity
    float dynamics = 0.0f;        // Trigger: 0 = every strike at full depth, 1 = depth follows its strength

    float manual = 0.0f;          // Manual: -1 - 1, positive pushes the bar (towards `direction`)
};

// Position of a vibrato bar in cents (negative = pitch down), one value per sample.
class BarGenerator
{
public:
    void prepare(double sampleRate);
    void reset();

    // Call at the start of each block (a synced LOOP locks its phase to the host here).
    void beginBlock(const SwaySettings& s);
    // `input` is the dry signal, used to detect picking attacks.
    float next(float input, const SwaySettings& s);

    // Position in the cycle (0 - 1) of the last sample, for display.
    float phase() const { return (float) cyclePhase; }
    // Picking attacks detected so far (TRIGGER), for display and tests.
    int pickAttacks() const { return detector.attacks(); }

private:
    float uniform();
    void startCycle(bool freeTiming, float random);

    static constexpr float kTypicalStrike = PickDetector::kTypicalStrike; // depth of a strike at the player's usual level

    double rate = 48000.0;
    double cyclePhase = 0.0;
    bool resting = true; // TRIGGER: the shape has played out and waits for the next attack
    float depthScale = 1.0f, rateScale = 1.0f;
    float velocityScale = 1.0f, velocityTarget = 1.0f; // the scale glides to its target once a strike is measured
    float drift = 0.0f, driftTarget = 0.0f;
    int driftCountdown = 0;
    PickDetector detector;

    // Coefficients: fixed ones from prepare(), the rest once per block from the settings.
    float velocityCoeff = 0.0f;
    double blockCycles = 0.33; // seconds per cycle
    float driftCoeff = 0.0f, smoothCoeff = 1.0f;
    int strikeSamplesLeft = 0; // measuring the strike just detected
    float strikePeak = 0.0f;
    float smoothed = 0.0f;
    uint32_t seed = 0x1234567u;
};

} // namespace sway
