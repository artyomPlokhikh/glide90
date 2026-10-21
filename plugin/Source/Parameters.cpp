#include "Parameters.h"

#include "RomPrograms.h"
#include "Shape.h"

#include <cmath>
#include <map>

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

// "C3", "c#2", "Eb4", "-1" style octaves; middle C (60) is C3. Or the note number itself.
std::optional<float> readNote(const juce::String& typed)
{
    const auto text = typed.trim().toLowerCase().removeCharacters(" ");
    static const int semitones[] { 9, 11, 0, 2, 4, 5, 7 }; // a b c d e f g
    if (text.isNotEmpty() && text[0] >= 'a' && text[0] <= 'g')
    {
        int note = semitones[text[0] - 'a'];
        int i = 1;
        if (text[i] == '#')
            ++note, ++i;
        else if (text[i] == 'b' && text.length() > 2)
            --note, ++i;
        const auto octave = text.substring(i);
        if (!octave.containsAnyOf("0123456789"))
            return {};
        return (float) (note + 12 * (octave.getIntValue() + 2));
    }
    const auto n = readNumber(typed);
    return n ? std::optional<float>((float) n->value) : std::nullopt;
}

std::optional<float> parseText(const juce::String& id, const juce::String& text, const juce::StringArray& choices)
{
    if (id == ids::gateLpf)
        return readLpf(text);
    if (id == ids::holdRoot)
        return normalised(text) == "auto" ? std::optional<float>((float) kHoldRootAuto) : readNote(text);
    if (id == ids::holdDecay)
    {
        const auto word = normalised(text);
        if (word == "inf" || word == "infinite" || word == "forever" || word == "hold" || word == "max"
            || text.trim() == juce::String::fromUTF8("\xe2\x88\x9e"))
            return 31.0f;
        const auto n = readNumber(text);
        if (!n)
            return {};
        return (float) (n->unit.startsWith("ms") ? n->value / 1000.0 : n->value); // seconds unless said
    }
    if (!choices.isEmpty())
        return matchChoice(text, choices);
    if (id == ids::pickSensitivity && normalised(text) == "off")
        return 0.0f;
    // Levels in dB: "-6", "-6 dB", "+3"; "-inf", "off" or the infinity sign for silence (on INPUT,
    // which goes no lower than -24 dB, its bottom).
    if (isOneOf(id, { ids::input, ids::output, ids::holdLevel, ids::gateLevel, ids::dryLevel }))
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
                      ids::glideBar, ids::holdReverse, ids::holdDuck }))
        return (float) (v / 100.0);
    // Milliseconds, or seconds when said so ("1.2 s").
    if (isOneOf(id, { ids::gatePreDelay, ids::glidePeriod, ids::holdSize, ids::holdSmear, ids::holdSwell, ids::holdTail }))
    {
        const bool seconds = (unit.startsWithChar('s') || unit.startsWith(secondsWord()));
        return (float) (seconds ? v * 1000.0 : v);
    }
    // Cents, or semitones when said so ("2 st").
    if (isOneOf(id, { ids::glideDepth, ids::holdSpread }) && (unit.startsWith("st") || unit.startsWith("semi")))
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

juce::String noteName(int note)
{
    return juce::MidiMessage::getMidiNoteName(note, true, true, 3);
}

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

namespace
{
// Hands each parameter, made afresh, to `take`.
struct Collect
{
    std::function<void(std::unique_ptr<juce::RangedAudioParameter>)> take;
    template <typename P>
    void add(std::unique_ptr<P> p)
    {
        take(std::move(p));
    }
};

// Every parameter of both products (GLIDE90's and LITE's own two).
void buildParameters(Collect& layout)
{
    const auto& rom17 = spx::romProgram(17).er;
    const auto oneDecimal = [](float v) { return juce::String(v, 1); };

    // The defaults are the first factory preset, Glide Guitar.
    // The plugin's first and last stage, and the pick detector GLIDE and HOLD share.
    layout.add(dbParam(ids::input, "Input", -kInputRangeDb, kInputRangeDb, 0.0f));
    layout.add(choiceParam(ids::inputMode, "Input Mode", { "MONO", "STEREO" }, (int) InputMode::Stereo));
    layout.add(dbParam(ids::output, "Output", kOffDb, kOutputMaxDb, 0.0f));
    layout.add(choiceParam(ids::outputMode, "Output Mode", { "STEREO", "L ONLY", "MONO" }, 0));
    // At 0 no pick is heard at all.
    layout.add(floatParam(ids::pickSensitivity, "Pick Sensitivity", NormalisableRange<float>(0.0f, 1.0f, 0.01f), 0.55f,
                          [](float v) { return v <= 0.0f ? juce::String("OFF") : juce::String(juce::roundToInt(v * 100.0f)) + "%"; }));
    // DRY: off, the reverse gate's swell alone.
    layout.add(dbParam(ids::dryLevel, "Dry Level", kOffDb, kDryLevelMaxDb, kOffDb));

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
    layout.add(dbParam(ids::gateLevel, "Gate Level", kOffDb, kGateLevelMaxDb, 0.0f));
    layout.add(percentParam(ids::gateMix, "Gate Mix", 1.0f));

    layout.add(boolParam(ids::glideOn, "Glide On", true));
    layout.add(choiceParam(ids::glideMode, "Glide Mode", { "FREE", "SYNC", "TRIGGER", "MANUAL" }, 2));
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
    layout.add(choiceParam(ids::glideTriggerSource, "Glide Trigger", { "PICK ATTACK", "MIDI NOTE" }, 0));
    layout.add(percentParam(ids::glideDynamics, "Glide Dynamics", 0.6f));
    layout.add(choiceParam(ids::glideManualSource, "Glide Manual Source",
                           { "BAR KNOB", "PITCH BEND", "MOD WHEEL", "EXPRESSION (CC11)" }, 0));
    layout.add(percentParam(ids::glideBar, "Glide Bar", 0.0f, 0.001f));
    layout.add(choiceParam(ids::glidePosition, "Glide Position", { "PRE", "POST" }, (int) GlidePosition::Pre));
    juce::StringArray routings;
    for (const auto& r : kRoutings)
        routings.add(r.name);
    layout.add(choiceParam(ids::routing, "Routing", routings, 0));

    // HOLD: off by default.
    layout.add(boolParam(ids::holdOn, "Hold On", false));
    layout.add(choiceParam(ids::holdMode, "Hold Mode", { "STRIKE", "MANUAL", "LIVE", "MIDI" }, 0));
    layout.add(boolParam(ids::holdFreeze, "Hold Freeze", false));
    const auto ms = [](float v) { return juce::String(juce::roundToInt(v)) + " ms"; };
    layout.add(floatParam(ids::holdSize, "Hold Size", steppedRange(20.0f, 500.0f, 1.0f, 150.0f), 180.0f, ms));
    layout.add(intParam(ids::holdDensity, "Hold Density", 1, 8, 2));
    layout.add(floatParam(ids::holdSmear, "Hold Smear", steppedRange(0.0f, 1000.0f, 1.0f, 150.0f), 60.0f, ms));
    layout.add(floatParam(ids::holdSpread, "Hold Spread", NormalisableRange<float>(0.0f, 50.0f, 0.1f), 6.0f,
                          [](float v) { return juce::String(v, 1) + " ct"; }));
    layout.add(percentParam(ids::holdReverse, "Hold Reverse", 0.0f));
    layout.add(percentParam(ids::holdDuck, "Hold Duck", 0.7f));
    layout.add(floatParam(ids::holdDecay, "Hold Decay", steppedRange(0.5f, 31.0f, 0.1f, 5.0f), 31.0f, [](float v) {
        if (v >= kHoldDecayForever)
            return juce::String("INF");
        return v < 10.0f ? juce::String(v, 1) + " s" : juce::String(juce::roundToInt(v)) + " s";
    }));
    layout.add(dbParam(ids::holdLevel, "Hold Level", kOffDb, kHoldLevelMaxDb, -3.0f));
    layout.add(choiceParam(ids::holdCharacter, "Hold Character", { "CLEAN", "12-BIT" }, 0));
    layout.add(intParam(ids::holdRoot, "Hold Root", kHoldRootAuto, 96, kHoldRootAuto,
                        [](int v) { return v == kHoldRootAuto ? juce::String("AUTO") : noteName(v); }));
    layout.add(floatParam(ids::holdSwell, "Hold Swell", steppedRange(10.0f, 4000.0f, 1.0f, 300.0f), 10.0f, ms));
    layout.add(floatParam(ids::holdTail, "Hold Tail", steppedRange(20.0f, 8000.0f, 1.0f, 600.0f), 200.0f, ms));
    juce::StringArray voices { "POLY", "MONO" };
    for (float glide : kHoldGlideMs)
        voices.add(glide >= 1000.0f ? "GLIDE " + juce::String(glide / 1000.0f, 0) + " S"
                                    : "GLIDE " + juce::String(juce::roundToInt(glide)) + " MS");
    layout.add(choiceParam(ids::holdVoices, "Hold Voices", voices, 0));
}
} // namespace

bool inProduct(const juce::String& id)
{
    const bool glideOnly = id == ids::gateMix || id == ids::glidePosition;
    if constexpr (kLite)
        return !id.startsWith("hold_") && !isOneOf(id, { ids::routing, ids::dryLevel, ids::gateLevel });
    return !glideOnly;
}

Layout createParameterLayout()
{
    Layout layout;
    Collect collect { [&layout](std::unique_ptr<juce::RangedAudioParameter> p) {
        if (inProduct(p->getParameterID()))
            layout.add(std::move(p));
    } };
    buildParameters(collect);
    return layout;
}

float fixedValue(const juce::String& id)
{
    static const std::map<juce::String, float> fixed = [] {
        std::map<juce::String, float> m;
        Collect collect { [&m](std::unique_ptr<juce::RangedAudioParameter> p) {
            m[p->getParameterID()] = p->convertFrom0to1(p->getDefaultValue());
        } };
        buildParameters(collect);
        m[ids::holdOn] = 0.0f; // LITE has no HOLD
        return m;
    }();
    const auto it = fixed.find(id);
    return it != fixed.end() ? it->second : 0.0f;
}

} // namespace glide90
