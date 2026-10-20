#include "PickDetector.h"

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

} // namespace

void PickDetector::prepare(double sampleRate)
{
    rate = sampleRate;
    highPassCoeff = (float) (1.0 - std::exp(-2.0 * 3.141592653589793 * 3000.0 / sampleRate));
    levelHistory.assign((size_t) std::max(1.0, 0.01 * sampleRate), 1e-6f);
    attackCoeff = coeffForSeconds(0.0005, rate);
    releaseCoeff = coeffForSeconds(0.02, rate);
    reset();
}

void PickDetector::reset()
{
    envFast = highPassLow = highPassLow2 = 0.0f;
    std::fill(levelHistory.begin(), levelHistory.end(), 1e-6f);
    historyPos = 0;
    refractory = 0;
    haveAverageStrike = false;
}

void PickDetector::setSensitivity(float sensitivity)
{
    const float jumpDb = 9.0f - 7.0f * std::clamp(sensitivity, 0.0f, 1.0f); // 5.5 dB at 0.5
    jumpRatio = std::pow(10.0f, jumpDb / 20.0f);
}

bool PickDetector::detect(float input)
{
    // A pick attack is a sudden burst of top end. Follow only the highs (two high-pass stages at
    // 3 kHz), where a chord that is still ringing has little left but the pick click is strong,
    // and trigger when that level jumps by several dB within 10 ms.
    highPassLow += highPassCoeff * (input - highPassLow);
    const float stage1 = input - highPassLow;
    highPassLow2 += highPassCoeff * (stage1 - highPassLow2);
    const float level = std::abs(stage1 - highPassLow2);
    envFast += (level > envFast ? attackCoeff : releaseCoeff) * (level - envFast);
    const float now = envFast + 1e-6f;
    const float past = levelHistory[(size_t) historyPos];
    levelHistory[(size_t) historyPos] = now;
    historyPos = (historyPos + 1) % (int) levelHistory.size();

    if (refractory > 0)
    {
        --refractory;
        return false;
    }
    if (now > past * jumpRatio && envFast > 0.0005f)
    {
        refractory = (int) (rate * 0.05); // sixteenths up to ~280 BPM
        ++triggers;
        return true;
    }
    return false;
}

float PickDetector::strikeStrength(float peak)
{
    // Relative to the player's own recent strikes, so the input gain does not matter: a strike at
    // the usual level is kTypicalStrike, 6 dB harder is full depth, 12 dB softer about a third.
    const float db = 20.0f * std::log10(std::max(peak, 1e-6f));
    if (!haveAverageStrike)
    {
        averageStrikeDb = db;
        haveAverageStrike = true;
    }
    const float relative = db - averageStrikeDb;
    // The usual level follows real strikes: it rises quickly, sinks slowly, and ignores anything far
    // below it (a squeak or a muted scratch that the detector also caught).
    if (relative > -20.0f)
        averageStrikeDb += (relative > 0.0f ? 0.3f : 0.1f) * relative;
    const float slope = relative >= 0.0f ? (1.0f - kTypicalStrike) / 6.0f : (kTypicalStrike - 0.33f) / 12.0f;
    return std::clamp(kTypicalStrike + slope * relative, 0.15f, 1.0f);
}

} // namespace sway
