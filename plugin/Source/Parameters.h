#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <optional>

namespace glide90
{

namespace ids
{
// The reverse gate (the SPX90's program 17, the only one kept) and its mix and output level.
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
inline constexpr const char* inputMode = "input_mode";

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

// Hold: captures the chord and holds it, bent by the glide bar.
inline constexpr const char* holdOn = "hold_on";
inline constexpr const char* holdMode = "hold_mode";
inline constexpr const char* holdLatch = "hold_latch";
inline constexpr const char* holdSensitivity = "hold_sensitivity";
inline constexpr const char* holdFollow = "hold_follow";
inline constexpr const char* holdSize = "hold_size";
inline constexpr const char* holdDensity = "hold_density";
inline constexpr const char* holdSmear = "hold_smear";
inline constexpr const char* holdSpread = "hold_spread";
inline constexpr const char* holdReverse = "hold_reverse";
inline constexpr const char* holdDuck = "hold_duck";
inline constexpr const char* holdDecay = "hold_decay";
inline constexpr const char* holdLevel = "hold_level";
inline constexpr const char* holdFeed = "hold_feed";
inline constexpr const char* holdCharacter = "hold_character";
inline constexpr const char* holdRoot = "hold_root";
} // namespace ids

// hold_decay at its top (and above this) holds forever.
inline constexpr float kHoldDecayForever = 30.5f;
// MIDI note names as in most hosts: middle C (60) is C3.
juce::String noteName(int note);

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

// What comes in: the sum of both inputs, as into the unit's one input, or both, the direct signal
// keeping its sides (the effect hears their sum either way).
enum class InputMode
{
    Mono,
    Stereo
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

// A value typed by the user, as a plain value of `p`: "50" on a percentage is 50 %, "1.2 s" is 1200 ms,
// "2.5" on the LPF is 2.5 kHz, "thru", "1/4d". Nothing when the text names no value.
std::optional<float> parseParameterText(const juce::RangedAudioParameter& p, const juce::String& text);

// The LPF choice nearest to `hz`; THRU for 0 or less.
int lpfIndex(float hz);

} // namespace glide90
