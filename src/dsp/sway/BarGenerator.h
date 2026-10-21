#pragma once

#include "PickDetector.h"
#include "Shape.h"

#include <cstdint>
#include <vector>

namespace sway
{

enum class GlideMode
{
    Free,    // the shape loops every cycleSeconds
    Sync,    // the shape loops every syncBeats, locked to the host's beat position
    Trigger, // each picking attack (or MIDI note) plays the shape once, over cycleSeconds
    Manual,  // no movement of its own: the bar follows `manual` (knob, pitch bend, CC)
};

enum class BendDirection
{
    Down, // a Jazzmaster bar: pushing only lowers the pitch
    Both, // around the open pitch
    Up,
};

struct SwaySettings
{
    bool enabled = true;
    GlideMode mode = GlideMode::Free;
    const Shape* shape = nullptr; // nullptr: sine

    double cycleSeconds = 0.33;   // Free, Trigger
    double syncBeats = 1.0;       // Sync: one cycle in quarter notes
    double bpm = 120.0;           // Sync
    double hostBeats = -1.0;      // Sync: host position in quarter notes at the block start; < 0 when stopped

    float depthCents = 30.0f;     // 0 - 300
    BendDirection direction = BendDirection::Down;
    float random = 0.15f;         // 0 - 1: cycle-to-cycle variation of depth and timing, plus drift
    float smooth = 0.2f;          // 0 - 1: how lazily the hand follows (2 - 150 ms)

    bool triggerFromPick = true;  // Trigger: picking attacks; otherwise MIDI notes
    float sensitivity = 0.5f;     // Trigger: attack detector sensitivity
    int midiTriggerAt = -1;       // Trigger: sample index of a MIDI note-on in this block, or -1
    float midiTriggerVelocity = 1.0f;
    float dynamics = 0.0f;        // Trigger: 0 = every strike or note at full depth, 1 = depth follows its strength

    float manual = 0.0f;          // Manual: -1 - 1, positive pushes the bar (towards `direction`)
};

// Position of a vibrato bar in cents (negative = pitch down), one value per sample.
class BarGenerator
{
public:
    void prepare(double sampleRate);
    void reset();

    // Call at the start of each block (Sync locks its phase to the host here).
    void beginBlock(const SwaySettings& s);
    // `input` is the dry signal, used to detect picking attacks; `index` the sample within the block.
    float next(float input, int index, const SwaySettings& s);

    // Position in the cycle (0 - 1) of the last sample, for display.
    float phase() const { return (float) cyclePhase; }
    // Picking attacks detected so far (TRIGGER from PICK ATTACK), for display and tests.
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
