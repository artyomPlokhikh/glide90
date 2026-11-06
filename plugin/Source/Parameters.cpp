#include "Parameters.h"

#include "RomPrograms.h"
#include "Shape.h"

#include <cmath>

namespace glide90
{
namespace
{

using Layout = juce::AudioProcessorValueTreeState::ParameterLayout;
using juce::AudioParameterBool;
using juce::AudioParameterChoice;
using juce::AudioParameterChoiceAttributes;
using juce::AudioParameterFloat;
using juce::AudioParameterFloatAttributes;
using juce::AudioParameterInt;
using juce::AudioParameterIntAttributes;
using juce::NormalisableRange;
using juce::ParameterID;

// Every parameter came in with the first published version.
constexpr int kVersion = 1;

juce::String lpfLabel(float hz)
{
    if (hz <= 0.0f)
        return "THRU";
    return hz < 10000.0f ? juce::String(hz / 1000.0f, 1) + "kHz" : juce::String(juce::roundToInt(hz / 1000.0f)) + "kHz";
}

NormalisableRange<float> steppedRange(float lo, float hi, float step, float centre)
{
    NormalisableRange<float> r(lo, hi, step);
    r.setSkewForCentre(centre);
    return r;
}

// --- Reading typed values ---

bool isOneOf(const juce::String& id, std::initializer_list<const char*> list)
{
    for (const auto* candidate : list)
        if (id == candidate)
            return true;
    return false;
}

// The first number in the text and what follows it, lower case ("25 ms" -> 25, "ms").
struct Number
{
    double value;
    juce::String unit;
};

std::optional<Number> readNumber(const juce::String& typed)
{
    const auto text = typed.trim().replaceCharacter(',', '.');
    int start = 0;
    while (start < text.length() && !juce::CharacterFunctions::isDigit(text[start]) && text[start] != '-'
           && text[start] != '+' && text[start] != '.')
        ++start;
    const auto rest = text.substring(start);
    auto end = rest.getCharPointer();
    const auto begin = end;
    const double value = juce::CharacterFunctions::readDoubleValue(end);
    const juce::String digits(begin, end);
    if (!digits.containsAnyOf("0123456789") || !std::isfinite(value))
        return {};
    return Number { value, juce::String(end).trim().toLowerCase() };
}

// Lower case without spaces, so "1/4 D", "1/4d" and "1/4 dotted" read alike.
juce::String normalised(const juce::String& text)
{
    return text.toLowerCase()
        .removeCharacters(" \t-_")
        .replace("dotted", "d")
        .replace("triplet", "t")
        .replace("bars", "bar");
}

std::optional<float> matchChoice(const juce::String& text, const juce::StringArray& choices)
{
    const auto wanted = normalised(text);
    if (wanted.isEmpty())
        return {};
    for (int i = 0; i < choices.size(); ++i)
        if (normalised(choices[i]) == wanted)
            return (float) i;
    return {};
}

std::optional<float> readLpf(const juce::String& text)
{
    const auto word = normalised(text);
    if (word == "thru" || word == "off" || word == "flat" || word == "through")
        return (float) (spx::kLpfValuesHz.size() - 1);
    const auto n = readNumber(text);
    if (!n)
        return {};
    double hz = n->value;
    if (n->unit.startsWithChar('k') || (n->unit.isEmpty() && hz < 100.0))
        hz *= 1000.0; // "2.5", "2.5k", "2.5 kHz"
    return (float) lpfIndex((float) hz);
}

juce::String secondsWord()
{
    return juce::String::fromUTF8("\xd1\x81"); // Cyrillic "s", as in "сек"
}

std::optional<float> parseText(const juce::String& id, const juce::String& text, const juce::StringArray& choices)
{
    if (id == ids::gateLpf)
        return readLpf(text);
    if (!choices.isEmpty())
        return matchChoice(text, choices);
    if (id == ids::pickSensitivity && normalised(text) == "off")
        return 0.0f;
    // Levels in dB: "-6", "-6 dB", "+3"; "-inf", "off" or the infinity sign for silence (on INPUT,
    // which goes no lower than -24 dB, its bottom).
    if (isOneOf(id, { ids::input, ids::output }))
    {
        const auto word = normalised(text).removeCharacters("+");
        if (word == "inf" || word == "infdb" || word == "off" || word == "mute"
            || text.contains(juce::String::fromUTF8("\xe2\x88\x9e")))
            return kOffDb;
        const auto n = readNumber(text);
        return n ? std::optional<float>((float) n->value) : std::nullopt;
    }

    const auto n = readNumber(text);
    if (!n)
        return {};
    const auto& unit = n->unit;
    double v = n->value;

    // Shown as percentages, typed as percentages: "50" and "50 %" are both one half.
    if (isOneOf(id, { ids::gateMix, ids::glideRandom, ids::glideSmooth, ids::pickSensitivity, ids::glideDynamics,
                      ids::glideBar }))
        return (float) (v / 100.0);
    // Milliseconds, or seconds when said so ("1.2 s").
    if (isOneOf(id, { ids::gatePreDelay, ids::glidePeriod }))
    {
        const bool seconds = (unit.startsWithChar('s') || unit.startsWith(secondsWord()));
        return (float) (seconds ? v * 1000.0 : v);
    }
    // Cents, or semitones when said so ("2 st").
    if (id == ids::glideDepth && (unit.startsWith("st") || unit.startsWith("semi")))
        return (float) (v * 100.0);
    return (float) v;
}

// Every parameter reads typed text through parseText; text that names no value gives the default.
std::unique_ptr<AudioParameterFloat> floatParam(const char* id, const juce::String& name, NormalisableRange<float> range,
                                                float defaultValue, std::function<juce::String(float)> text)
{
    auto attributes = AudioParameterFloatAttributes().withValueFromStringFunction(
        [key = juce::String(id), defaultValue](const juce::String& t) {
            return parseText(key, t, {}).value_or(defaultValue);
        });
    if (text)
        attributes = attributes.withStringFromValueFunction([text](float v, int) { return text(v); });
    return std::make_unique<AudioParameterFloat>(ParameterID { id, kVersion }, name, range, defaultValue, attributes);
}

std::unique_ptr<AudioParameterFloat> percentParam(const char* id, const juce::String& name, float defaultValue,
                                                  float step = 0.01f)
{
    return floatParam(id, name, NormalisableRange<float>(0.0f, 1.0f, step), defaultValue,
                      [](float v) { return juce::String(juce::roundToInt(v * 100.0f)) + "%"; });
}

// A level in dB, shown as "+3.0 dB", "0.0 dB", or "-inf dB" at kOffDb.
std::unique_ptr<AudioParameterFloat> dbParam(const char* id, const juce::String& name, float lo, float hi, float defaultValue)
{
    return floatParam(id, name, NormalisableRange<float>(lo, hi, 0.1f), defaultValue, [](float v) {
        if (v <= kOffDb + 0.05f)
            return juce::String("-inf dB");
        const float shown = std::abs(v) < 0.05f ? 0.0f : v;
        return (shown > 0.0f ? "+" : "") + juce::String(shown, 1) + " dB";
    });
}

std::unique_ptr<AudioParameterInt> intParam(const char* id, const juce::String& name, int lo, int hi, int defaultValue,
                                            std::function<juce::String(int)> text = {})
{
    auto attributes = AudioParameterIntAttributes().withValueFromStringFunction(
        [key = juce::String(id), defaultValue](const juce::String& t) {
            const auto v = parseText(key, t, {});
            return v ? juce::roundToInt(*v) : defaultValue;
        });
    if (text)
        attributes = attributes.withStringFromValueFunction([text](int v, int) { return text(v); });
    return std::make_unique<AudioParameterInt>(ParameterID { id, kVersion }, name, lo, hi, defaultValue, attributes);
}

std::unique_ptr<AudioParameterChoice> choiceParam(const char* id, const juce::String& name, const juce::StringArray& choices,
                                                  int defaultIndex)
{
    return std::make_unique<AudioParameterChoice>(
        ParameterID { id, kVersion }, name, choices, defaultIndex,
        AudioParameterChoiceAttributes().withValueFromStringFunction(
            [key = juce::String(id), choices, defaultIndex](const juce::String& t) {
                const auto v = parseText(key, t, choices);
                return v ? juce::roundToInt(*v) : defaultIndex;
            }));
}

std::unique_ptr<AudioParameterBool> boolParam(const char* id, const juce::String& name, bool defaultValue)
{
    return std::make_unique<AudioParameterBool>(ParameterID { id, kVersion }, name, defaultValue);
}

} // namespace

int lpfIndex(float hz)
{
    int best = (int) spx::kLpfValuesHz.size() - 1; // THRU
    if (hz <= 0.0f)
        return best;
    float bestErr = 1e9f;
    for (int i = 0; i + 1 < (int) spx::kLpfValuesHz.size(); ++i)
    {
        const float err = std::abs(std::log(spx::kLpfValuesHz[(size_t) i] / hz));
        if (err < bestErr)
        {
            bestErr = err;
            best = i;
        }
    }
    return best;
}

std::optional<float> parseParameterText(const juce::RangedAudioParameter& p, const juce::String& text)
{
    juce::StringArray choices;
    if (const auto* c = dynamic_cast<const juce::AudioParameterChoice*>(&p))
        choices = c->choices;
    else if (dynamic_cast<const juce::AudioParameterBool*>(&p) != nullptr)
    {
        const auto word = normalised(text);
        if (word == "on" || word == "yes" || word == "true" || word == "1")
            return 1.0f;
        if (word == "off" || word == "no" || word == "false" || word == "0")
            return 0.0f;
        return {};
    }
    return parseText(p.getParameterID(), text, choices);
}

juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout()
{
    Layout layout;
    const auto& rom17 = spx::romProgram(17).er;
    const auto oneDecimal = [](float v) { return juce::String(v, 1); };

    // The defaults are the first factory preset, Glide Guitar.
    // The plugin's first and last stage, and the pick detector.
    layout.add(dbParam(ids::input, "Input", -kInputRangeDb, kInputRangeDb, 0.0f));
    layout.add(choiceParam(ids::inputMode, "Input Mode", { "MONO", "STEREO" }, (int) InputMode::Stereo));
    layout.add(dbParam(ids::output, "Output", kOffDb, kOutputMaxDb, 0.0f));
    layout.add(choiceParam(ids::outputMode, "Output Mode", { "STEREO", "L ONLY", "MONO" }, 0));
    // At 0 no pick is heard at all.
    layout.add(floatParam(ids::pickSensitivity, "Pick Sensitivity", NormalisableRange<float>(0.0f, 1.0f, 0.01f), 0.55f,
                          [](float v) { return v <= 0.0f ? juce::String("OFF") : juce::String(juce::roundToInt(v * 100.0f)) + "%"; }));

    // The reverse gate: the SPX90's program 17, here at its longest swell.
    layout.add(boolParam(ids::gateOn, "Gate On", true));
    layout.add(floatParam(ids::gateRoomSize, "Gate Room Size", steppedRange(0.1f, 20.0f, 0.1f, 4.0f), 20.0f, oneDecimal));
    layout.add(intParam(ids::gateLiveness, "Gate Liveness", 0, 10, 4));
    layout.add(floatParam(ids::gatePreDelay, "Gate Pre-Delay", steppedRange(0.1f, 400.0f, 0.1f, 40.0f), rom17.delayMs,
                          [](float v) { return juce::String(v, 1) + "ms"; }));
    juce::StringArray lpfNames;
    for (float hz : spx::kLpfValuesHz)
        lpfNames.add(lpfLabel(hz));
    layout.add(choiceParam(ids::gateLpf, "Gate LPF", lpfNames, lpfNames.size() - 1));
    layout.add(percentParam(ids::gateMix, "Gate Mix", 1.0f));

    layout.add(boolParam(ids::glideOn, "Glide On", true));
    layout.add(choiceParam(ids::glideMode, "Glide Mode", { "LOOP", "TRIGGER", "MANUAL" }, 1));
    layout.add(boolParam(ids::glideSync, "Glide Sync", false));
    layout.add(floatParam(ids::glidePeriod, "Glide Rate", steppedRange(30.0f, 4000.0f, 1.0f, 400.0f), 600.0f,
                          [](float v) { return juce::String(juce::roundToInt(v)) + " ms"; }));
    juce::StringArray divisions;
    for (const auto& d : kDivisions)
        divisions.add(d.label);
    layout.add(choiceParam(ids::glideDivision, "Glide Sync Rate", divisions, kDefaultDivision));
    layout.add(floatParam(ids::glideDepth, "Glide Depth", steppedRange(0.0f, 300.0f, 0.1f, 60.0f), 40.0f,
                          [](float v) { return juce::String(v, v < 10.0f ? 1 : 0) + " ct"; }));
    layout.add(choiceParam(ids::glideDirection, "Glide Direction", { "DOWN", "BOTH", "UP" }, 0));
    layout.add(percentParam(ids::glideRandom, "Glide Random", 0.25f));
    layout.add(percentParam(ids::glideSmooth, "Glide Smooth", 0.25f));
    juce::StringArray shapes;
    for (auto name : sway::kShapePresetNames)
        shapes.add(juce::String(name.data(), name.size()));
    shapes.add(kCustomShapeName);
    layout.add(choiceParam(ids::glideShape, "Glide Shape", shapes, (int) sway::ShapePreset::Dive));
    layout.add(percentParam(ids::glideDynamics, "Glide Dynamics", 0.6f));
    layout.add(percentParam(ids::glideBar, "Glide Bar", 0.0f, 0.001f));
    layout.add(choiceParam(ids::glidePosition, "Glide Position", { "PRE", "POST" }, (int) GlidePosition::Pre));
    return layout;
}

} // namespace glide90
