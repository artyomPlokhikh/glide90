#include "BarGenerator.h"

#include <algorithm>
#include <cmath>

namespace sway
{
namespace
{

constexpr double kTwoPi = 6.283185307179586;

float coeffForSeconds(double seconds, double rate)
{
    return (float) (1.0 - std::exp(-1.0 / (std::max(seconds, 1e-4) * rate)));
}

} // namespace

void BarGenerator::prepare(double sampleRate)
{
    rate = sampleRate;
    reset();
}

void BarGenerator::reset()
{
    phase = 0.0;
    jitter = jitterTarget = 0.0f;
    jitterCountdown = 0;
    randTarget = randA = randB = 0.0f;
    randCountdown = 0;
    envFast = envSlow = 0.0f;
    refractory = 0;
    push = 0.0f;
    pushRising = false;
    smoothed = 0.0f;
}

float BarGenerator::uniform()
{
    seed = seed * 1664525u + 1013904223u;
    return (float) (seed >> 8) / (float) (1u << 24) * 2.0f - 1.0f;
}

float BarGenerator::next(float input, const SwaySettings& s)
{
    const double hz = std::clamp((double) s.rateHz, 0.05, 20.0);

    // A hand never keeps a steady tempo: the LFO rate wanders by up to +-20 %.
    if (--jitterCountdown <= 0)
    {
        jitterTarget = 0.2f * uniform() * s.shape;
        jitterCountdown = (int) (rate * 0.7 / hz);
    }
    jitter += coeffForSeconds(0.5 / hz, rate) * (jitterTarget - jitter);
    phase += hz * (1.0 + jitter) / rate;
    phase -= std::floor(phase);
    const float lfo = (float) std::sin(kTwoPi * phase);

    // Irregular movement: random targets at roughly the wobble rate, smoothed twice.
    if (--randCountdown <= 0)
    {
        randTarget = uniform();
        randCountdown = (int) (rate / hz * (0.6 + 0.4 * (uniform() + 1.0f)));
    }
    const float rc = coeffForSeconds(0.25 / hz, rate);
    randA += rc * (randTarget - randA);
    randB += rc * (randA - randB);
    const float random = std::clamp(randB * 1.6f, -1.0f, 1.0f);

    // Picking attacks: a fast envelope jumping well above a slow one. Each attack pushes the bar.
    const float level = std::abs(input);
    envFast += (level > envFast ? coeffForSeconds(0.001, rate) : coeffForSeconds(0.03, rate)) * (level - envFast);
    envSlow += (level > envSlow ? coeffForSeconds(0.05, rate) : coeffForSeconds(0.3, rate)) * (level - envSlow);
    const float ratio = 1.3f + 3.0f * (1.0f - s.sensitivity);
    if (refractory > 0)
        --refractory;
    else if (envFast > envSlow * ratio && envFast > 0.003f)
    {
        pushRising = true;
        refractory = (int) (rate * 0.08);
    }
    if (pushRising)
    {
        push += coeffForSeconds(0.04, rate) * (1.05f - push);
        if (push >= 1.0f)
        {
            push = 1.0f;
            pushRising = false;
        }
    }
    else
        push += coeffForSeconds(0.6 / hz, rate) * (0.0f - push);

    // Mix the movement sources into one bar excursion x in [-1, 1], +1 = pushed fully.
    const float osc = (1.0f - s.shape) * lfo + s.shape * random;
    const float x = (1.0f - s.strumSync) * osc + s.strumSync * (2.0f * push - 1.0f);

    // A Jazzmaster bar mostly lowers the pitch: map [-1, 1] to [0, -1] ("down only") or keep it
    // symmetric, and blend between the two.
    const float down = -(x + 1.0f) * 0.5f;
    const float y = down + s.asymmetry * (-x - down);

    const float target = s.depthCents * y + s.barCents;
    smoothed += coeffForSeconds(0.004, rate) * (target - smoothed);
    return smoothed;
}

} // namespace sway
