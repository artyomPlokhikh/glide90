#pragma once

#include "ErEngine.h"

#include <array>
#include <string_view>

namespace spx
{

enum class Algorithm
{
    Reverb,
    EarlyReflection,
    Delay,
    Echo,
    Modulation,
    Gate,
    Pitch,
    Freeze,
    Pan,
    Vibrato,
    Eq,
    ReverbGate,
};

struct RomProgram
{
    int number;
    std::string_view name; // as on the 16-character LCD
    Algorithm algorithm;
    bool implemented;      // stage 1 covers the early-reflection family only
    ErSettings er {};      // factory values for the early-reflection programs
    float balance = 1.0f;  // factory BALANCE (OUT LVL is 100 % in every ROM program)
};

// Factory programs, SPX90 Operating Manual p.24.
inline constexpr std::array<RomProgram, 30> kRomPrograms { {
    { 1, "REV 1 HALL", Algorithm::Reverb, false },
    { 2, "REV 2 ROOM", Algorithm::Reverb, false },
    { 3, "REV 3 VOCAL", Algorithm::Reverb, false },
    { 4, "REV 4 PLATE", Algorithm::Reverb, false },
    { 5, "EARLY REF 1", Algorithm::EarlyReflection, true, { ErType::Hall, 1, 2.0f, 5, 10.0f, 0.0f } },
    { 6, "EARLY REF 2", Algorithm::EarlyReflection, true, { ErType::Hall, 2, 2.0f, 5, 10.0f, 0.0f } },
    { 7, "DELAY L R", Algorithm::Delay, false },
    { 8, "STEREO ECHO", Algorithm::Echo, false },
    { 9, "STEREO FLANGE A", Algorithm::Modulation, false, {}, 0.5f },
    { 10, "STEREO FLANGE B", Algorithm::Modulation, false, {}, 0.75f },
    { 11, "CHORUS A", Algorithm::Modulation, false },
    { 12, "CHORUS B", Algorithm::Modulation, false },
    { 13, "STEREO PHASING", Algorithm::Modulation, false },
    { 14, "TREMOLO", Algorithm::Modulation, false },
    { 15, "SYMPHONIC", Algorithm::Modulation, false },
    { 16, "GATE REVERB", Algorithm::EarlyReflection, true, { ErType::Random, 2, 2.0f, 5, 20.0f, 6300.0f } },
    { 17, "REVERSE GATE", Algorithm::EarlyReflection, true, { ErType::Reverse, 2, 3.3f, 5, 25.0f, 0.0f } },
    { 18, "ADR NOISE GATE", Algorithm::Gate, false },
    { 19, "COMPRESSOR", Algorithm::Gate, false },
    { 20, "REVERB & GATE", Algorithm::ReverbGate, false },
    { 21, "PITCH CHANGE A", Algorithm::Pitch, false },
    { 22, "PITCH CHANGE B", Algorithm::Pitch, false },
    { 23, "PITCH CHANGE C", Algorithm::Pitch, false },
    { 24, "PITCH CHANGE D", Algorithm::Pitch, false },
    { 25, "FREEZE A", Algorithm::Freeze, false },
    { 26, "FREEZE B", Algorithm::Freeze, false },
    { 27, "AUTO PAN", Algorithm::Pan, false },
    { 28, "TRIGGERED PAN", Algorithm::Pan, false },
    { 29, "DELAY VIBRATO", Algorithm::Vibrato, false },
    { 30, "PARAMETRIC EQ", Algorithm::Eq, false },
} };

inline const RomProgram& romProgram(int number)
{
    return kRomPrograms[(size_t) (number - 1)];
}

// LPF settings, 1.0 - 11 kHz, then THRU encoded as 0. The manual gives only the range; the steps
// follow the 1/6-octave series its PEQ uses and include every value printed in the ROM table.
inline constexpr std::array<float, 23> kLpfValuesHz { 1000, 1100, 1250, 1400, 1600, 1800, 2000, 2200,
                                                     2500, 2800, 3200, 3600, 4000, 4500, 5000, 5600,
                                                     6300, 7000, 8000, 9000, 10000, 11000, 0 };

} // namespace spx
