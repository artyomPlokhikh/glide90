#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <vector>

namespace glide90
{

namespace ids
{
// SPX90 memory and edit buffer. Everything from erType to outLevel is stored with a memory,
// like on the unit ("all parameters except Input Level can be memorized").
inline constexpr const char* memory = "memory";
inline constexpr const char* erType = "er_type";
inline constexpr const char* erRoomSize = "er_room_size";
inline constexpr const char* erLiveness = "er_liveness";
inline constexpr const char* erDelay = "er_delay";
inline constexpr const char* erLpf = "er_lpf";
inline constexpr const char* balance = "balance";
inline constexpr const char* outLevel = "out_level";

// Front-panel knob, rear-panel level switches and the BYPASS key.
inline constexpr const char* input = "input";
inline constexpr const char* inputSwitch = "input_switch";
inline constexpr const char* outputSwitch = "output_switch";
inline constexpr const char* bypass = "spx_bypass";

// Sway.
inline constexpr const char* swayOn = "sway_on";
inline constexpr const char* swayDepth = "sway_depth";
inline constexpr const char* swayRate = "sway_rate";
inline constexpr const char* swayShape = "sway_shape";
inline constexpr const char* swayStrum = "sway_strum";
inline constexpr const char* swaySensitivity = "sway_sensitivity";
inline constexpr const char* swayAsymmetry = "sway_asymmetry";
inline constexpr const char* swayBar = "sway_bar";
inline constexpr const char* swayBarSource = "sway_bar_source";
inline constexpr const char* swayTwinDetune = "sway_twin_detune";
inline constexpr const char* swayTwinLevel = "sway_twin_level";
inline constexpr const char* swayPosition = "sway_position";
inline constexpr const char* swayMix = "sway_mix";
} // namespace ids

enum class BarSource
{
    Knob,
    PitchBend,
    ModWheel
};

enum class SwayPosition
{
    Pre,
    Post
};

juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

// The parameters the PARAMETER key steps through for a program's algorithm (empty for the
// programs that are not emulated yet), and the two the BALANCE key steps through.
std::vector<juce::String> parameterPages(int algorithm);
std::vector<juce::String> balancePages();

// One LCD line for a parameter, e.g. "ROOM SIZE=3.3" or "LPF=THRU".
juce::String lcdText(const juce::RangedAudioParameter& p);

} // namespace glide90
