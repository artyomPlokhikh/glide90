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
inline constexpr const char* outputMode = "output_mode";

// Glide: the vibrato bar.
inline constexpr const char* glideOn = "glide_on";
inline constexpr const char* glideMode = "glide_mode";
inline constexpr const char* glidePeriod = "glide_period";
inline constexpr const char* glideDivision = "glide_division";
inline constexpr const char* glideDepth = "glide_depth";
inline constexpr const char* glideDirection = "glide_direction";
inline constexpr const char* glideRandom = "glide_random";
inline constexpr const char* glideSmooth = "glide_smooth";
inline constexpr const char* glideShape = "glide_shape";
inline constexpr const char* glideTriggerSource = "glide_trigger_source";
inline constexpr const char* glideSensitivity = "glide_sensitivity";
inline constexpr const char* glideDynamics = "glide_dynamics";
inline constexpr const char* glideManualSource = "glide_manual_source";
inline constexpr const char* glideBar = "glide_bar";
inline constexpr const char* glideTwinDetune = "glide_twin_detune";
inline constexpr const char* glideTwinLevel = "glide_twin_level";
inline constexpr const char* glideMix = "glide_mix";
inline constexpr const char* glidePosition = "glide_position";
} // namespace ids

enum class TriggerSource
{
    Pick,
    MidiNote
};

enum class ManualSource
{
    BarKnob,
    PitchBend,
    ModWheel,
    Expression // CC 11
};

// What leaves the plugin: both SPX outputs, the left one on both channels (one output of the unit
// into one amp), or their sum.
enum class OutputMode
{
    Stereo,
    LeftOnly,
    MonoSum
};

enum class GlidePosition
{
    Pre,
    Post
};

// Note values for SYNC: label and length in quarter notes.
struct Division
{
    const char* label;
    double beats;
};
inline constexpr Division kDivisions[] {
    { "4 BARS", 16.0 }, { "2 BARS", 8.0 },  { "1 BAR", 4.0 },       { "1/2", 2.0 },    { "1/2 D", 3.0 },
    { "1/2 T", 4.0 / 3.0 }, { "1/4", 1.0 }, { "1/4 D", 1.5 },       { "1/4 T", 2.0 / 3.0 }, { "1/8", 0.5 },
    { "1/8 D", 0.75 },  { "1/8 T", 1.0 / 3.0 }, { "1/16", 0.25 }, { "1/16 D", 0.375 }, { "1/16 T", 1.0 / 6.0 },
    { "1/32", 0.125 },
};
inline constexpr int kDefaultDivision = 6; // 1/4
// The glide_shape choice after the preset shapes.
inline constexpr const char* kCustomShapeName = "Custom";

juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

// The parameters the PARAMETER key steps through for a program's algorithm (empty for the
// programs that are not emulated yet), and the two the BALANCE key steps through.
std::vector<juce::String> parameterPages(int algorithm);
std::vector<juce::String> balancePages();

// One LCD line for a parameter, e.g. "ROOM SIZE=3.3" or "LPF=THRU".
juce::String lcdText(const juce::RangedAudioParameter& p);

} // namespace glide90
