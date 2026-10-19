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
    reset();
}

void BarGenerator::reset()
{
    cyclePhase = 0.0;
    depthScale = rateScale = velocityScale = 1.0f;
    drift = driftTarget = 0.0f;
    driftCountdown = 0;
    envFast = envSlow = 0.0f;
    refractory = 0;
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

bool BarGenerator::detectAttack(float input, float sensitivity)
{
    // A fast envelope jumping well above a slow one.
    const float level = std::abs(input);
    envFast += (level > envFast ? coeffForSeconds(0.001, rate) : coeffForSeconds(0.03, rate)) * (level - envFast);
    envSlow += (level > envSlow ? coeffForSeconds(0.05, rate) : coeffForSeconds(0.3, rate)) * (level - envSlow);
    if (refractory > 0)
    {
        --refractory;
        return false;
    }
    const float ratio = 1.3f + 3.0f * (1.0f - std::clamp(sensitivity, 0.0f, 1.0f));
    if (envFast > envSlow * ratio && envFast > 0.003f)
    {
        refractory = (int) (rate * 0.08);
        return true;
    }
    return false;
}

void BarGenerator::beginBlock(const SwaySettings& s)
{
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
        if (s.mode == GlideMode::Trigger)
        {
            const bool attack = detectAttack(input, s.sensitivity);
            if (s.triggerFromPick ? attack : index == s.midiTriggerAt)
            {
                restart = true;
                velocityScale = s.triggerFromPick ? 1.0f : std::clamp(s.midiTriggerVelocity, 0.0f, 1.0f);
            }
        }
        else
            velocityScale = 1.0f;

        const double cycles = s.mode == GlideMode::Sync ? std::max(s.syncBeats, 1e-3) * 60.0 / std::max(s.bpm, 1.0)
                                                        : std::max(s.cycleSeconds, 1e-3);
        if (restart)
        {
            cyclePhase = 0.0;
            startCycle(true, random);
        }
        else
        {
            cyclePhase += rateScale / (cycles * rate);
            if (cyclePhase >= 1.0)
            {
                cyclePhase -= std::floor(cyclePhase);
                startCycle(s.mode != GlideMode::Sync, random);
            }
        }

        // Slow random drift of the hand on top of the shape.
        if (--driftCountdown <= 0)
        {
            driftTarget = 2.0f * uniform() - 1.0f;
            driftCountdown = (int) (rate * cycles * (0.5 + uniform()));
        }
        drift += coeffForSeconds(0.5 * cycles, rate) * (driftTarget - drift);

        const Shape& shape = s.shape != nullptr ? *s.shape : defaultShape();
        y = shape.at((float) cyclePhase) * depthScale * velocityScale + 0.2f * random * drift;
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

    smoothed += coeffForSeconds(0.002 + 0.15 * std::clamp(s.smooth, 0.0f, 1.0f), rate) * (cents - smoothed);
    return smoothed;
}

} // namespace sway
