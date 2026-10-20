#include "BarGenerator.h"

#include <algorithm>
#include <cmath>

namespace sway
{
namespace
{

float coeffForSeconds(double seconds, double rate)
{
    return (float) (1.0 - std::exp(-1.0 / (std::max(seconds, 1e-4) * rate)));
}

const Shape& defaultShape()
{
    static const Shape sine;
    return sine;
}

} // namespace

void BarGenerator::prepare(double sampleRate)
{
    rate = sampleRate;
    detector.prepare(sampleRate);
    velocityCoeff = coeffForSeconds(0.01, rate);
    driftCoeff = coeffForSeconds(0.5 * blockCycles, rate);
    smoothCoeff = coeffForSeconds(0.002, rate);
    reset();
}

void BarGenerator::reset()
{
    cyclePhase = 0.0;
    resting = true;
    depthScale = rateScale = velocityScale = velocityTarget = 1.0f;
    drift = driftTarget = 0.0f;
    driftCountdown = 0;
    detector.reset();
    strikeSamplesLeft = 0;
    strikePeak = 0.0f;
    smoothed = 0.0f;
}

float BarGenerator::uniform()
{
    seed = seed * 1664525u + 1013904223u;
    return (float) (seed >> 8) / (float) (1u << 24); // 0 - 1
}

void BarGenerator::startCycle(bool freeTiming, float random)
{
    // A hand never repeats itself exactly: each cycle goes a little less deep and a little faster or
    // slower. Synced cycles keep their timing so they stay on the beat.
    depthScale = 1.0f - 0.6f * random * uniform();
    rateScale = freeTiming ? 1.0f + 0.35f * random * (2.0f * uniform() - 1.0f) : 1.0f;
}

void BarGenerator::beginBlock(const SwaySettings& s)
{
    // Everything that only changes with the settings, once per block instead of per sample.
    blockCycles = s.mode == GlideMode::Sync ? std::max(s.syncBeats, 1e-3) * 60.0 / std::max(s.bpm, 1.0)
                                            : std::max(s.cycleSeconds, 1e-3);
    driftCoeff = coeffForSeconds(0.5 * blockCycles, rate);
    smoothCoeff = coeffForSeconds(0.002 + 0.15 * std::clamp(s.smooth, 0.0f, 1.0f), rate);
    detector.setSensitivity(s.sensitivity);

    if (s.mode == GlideMode::Sync && s.hostBeats >= 0.0)
    {
        const double beats = std::max(s.syncBeats, 1e-3);
        const double locked = s.hostBeats / beats - std::floor(s.hostBeats / beats);
        if (locked < cyclePhase - 0.5)
            startCycle(false, s.random); // the host wrapped into a new cycle
        cyclePhase = locked;
    }
}

float BarGenerator::next(float input, int index, const SwaySettings& s)
{
    const float random = std::clamp(s.random, 0.0f, 1.0f);
    const float depth = std::clamp(s.depthCents, 0.0f, 300.0f);
    float y; // bar push, 0 - 1 (or -1 - 1 for Manual)

    if (s.mode == GlideMode::Manual)
    {
        y = std::clamp(s.manual, -1.0f, 1.0f);
    }
    else
    {
        bool restart = false;
        const float dynamics = std::clamp(s.dynamics, 0.0f, 1.0f);
        if (s.mode == GlideMode::Trigger)
        {
            const bool attack = detector.detect(input);
            if (s.triggerFromPick ? attack : index == s.midiTriggerAt)
            {
                restart = true;
                if (s.triggerFromPick)
                {
                    // How hard the strike was is known a few ms later; start at a typical strike.
                    strikeSamplesLeft = (int) (rate * 0.008);
                    strikePeak = 0.0f;
                    velocityScale = velocityTarget = 1.0f - dynamics * (1.0f - kTypicalStrike);
                }
                else
                    velocityScale = velocityTarget = 1.0f - dynamics * (1.0f - std::clamp(s.midiTriggerVelocity, 0.0f, 1.0f));
            }
            if (strikeSamplesLeft > 0)
            {
                // The pick's own burst of top end (what the detector follows), not the ringing chord
                // underneath it: a soft strike over a loud chord is still a soft strike.
                strikePeak = std::max(strikePeak, detector.envelope());
                if (--strikeSamplesLeft == 0)
                    velocityTarget = 1.0f - dynamics * (1.0f - detector.strikeStrength(strikePeak));
            }
            // The measured strength arrives 8 ms into the shape: glide to it rather than step.
            velocityScale += velocityCoeff * (velocityTarget - velocityScale);
        }
        else
            velocityScale = velocityTarget = 1.0f;

        const double cycles = blockCycles;
        const bool oneShot = s.mode == GlideMode::Trigger;
        if (restart)
        {
            cyclePhase = 0.0;
            resting = false;
            startCycle(true, random);
        }
        else if (!(oneShot && resting))
        {
            cyclePhase += rateScale / (cycles * rate);
            if (cyclePhase >= 1.0)
            {
                cyclePhase -= std::floor(cyclePhase);
                if (oneShot)
                {
                    // TRIGGER plays the shape once, then the bar rests until the next attack or note.
                    cyclePhase = 0.0;
                    resting = true;
                }
                else
                    startCycle(s.mode != GlideMode::Sync, random);
            }
        }
        if (!oneShot)
            resting = false;

        // Slow random drift of the hand on top of the shape (a resting hand is still).
        if (--driftCountdown <= 0)
        {
            driftTarget = 2.0f * uniform() - 1.0f;
            driftCountdown = (int) (rate * cycles * (0.5 + uniform()));
        }
        drift += driftCoeff * (driftTarget - drift);

        const Shape& shape = s.shape != nullptr ? *s.shape : defaultShape();
        y = shape.at((float) cyclePhase) * depthScale * velocityScale + (resting ? 0.0f : 0.2f * random * drift);
        y = std::clamp(y, 0.0f, 1.0f);
        if (s.direction == BendDirection::Both)
            y = 2.0f * y - 1.0f; // centred on the open pitch
    }

    float cents;
    switch (s.direction)
    {
        case BendDirection::Down: cents = -depth * std::max(y, 0.0f); break;
        case BendDirection::Up: cents = depth * std::max(y, 0.0f); break;
        case BendDirection::Both: default: cents = -depth * y; break;
    }
    // TRIGGER between strikes: the hand is off the bar, so the string is at its open pitch whatever
    // the shape's first point or the direction (BOTH would otherwise rest a whole DEPTH sharp).
    if (s.mode == GlideMode::Trigger && resting)
        cents = 0.0f;

    smoothed += smoothCoeff * (cents - smoothed);
    return smoothed;
}

} // namespace sway
