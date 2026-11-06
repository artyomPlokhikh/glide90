#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <iterator>
#include <optional>

namespace glide90
{

namespace ids
{
// The plugin's first and last stage: every sound passes through both. INPUT and both modes belong
// to the rig (presets and A/B leave them alone); OUTPUT is part of the sound.
inline constexpr const char* input = "input";
inline constexpr const char* inputMode = "input_mode";
inline constexpr const char* output = "output";
inline constexpr const char* outputMode = "output_mode";
// How easily the pick detector, which TRIGGER listens to, hears a pick.
inline constexpr const char* pickSensitivity = "pick_sensitivity";

// The reverse gate: the SPX90's program 17, the only one kept.
inline constexpr const char* gateOn = "gate_on";
inline constexpr const char* gateRoomSize = "gate_room_size";
inline constexpr const char* gateLiveness = "gate_liveness";
inline constexpr const char* gatePreDelay = "gate_pre_delay";
inline constexpr const char* gateLpf = "gate_lpf";
// The unit's BALANCE between the direct guitar and the reverse gate.
inline constexpr const char* gateMix = "gate_mix";

// Glide: the vibrato bar.
inline constexpr const char* glideOn = "glide_on";
inline constexpr const char* glideMode = "glide_mode";
inline constexpr const char* glideSync = "glide_sync"; // RATE as a note value (glide_division) or as time (glide_period)
inline constexpr const char* glidePeriod = "glide_period";
inline constexpr const char* glideDivision = "glide_division";
inline constexpr const char* glideDepth = "glide_depth";
inline constexpr const char* glideDirection = "glide_direction";
inline constexpr const char* glideRandom = "glide_random";
inline constexpr const char* glideSmooth = "glide_smooth";
inline constexpr const char* glideShape = "glide_shape";
inline constexpr const char* glideDynamics = "glide_dynamics";
inline constexpr const char* glideBar = "glide_bar";
// GLIDE before or after the reverse gate (the modules' order, dragged by their title bars).
inline constexpr const char* glidePosition = "glide_position";
} // namespace ids

// Levels in dB. A range that goes down to kOffDb is silent at its bottom ("-inf dB").
inline constexpr float kOffDb = -60.0f;
inline constexpr float kInputRangeDb = 24.0f;        // INPUT: -24 ... +24 dB
inline constexpr float kOutputMaxDb = 12.0f;         // OUTPUT: off ... +12 dB
// The gain of a level in dB; silence at kOffDb and below.
inline float gainOf(float db)
{
    return juce::Decibels::decibelsToGain(db, kOffDb);
}

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

// The bar before the reverse gate (it bends what goes in) or after it (it bends what comes out).
enum class GlidePosition
{
    Pre,
    Post
};

// The modules' names on their panels.
namespace names
{
inline constexpr const char* in = "IN";
inline constexpr const char* out = "OUT";
inline constexpr const char* glide = "GLIDE";
inline constexpr const char* reverseGate = "REVERSE GATE";
} // namespace names

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
