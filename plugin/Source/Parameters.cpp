#include "Parameters.h"

#include "RomPrograms.h"
#include "Shape.h"

namespace glide90
{
namespace
{

using Layout = juce::AudioProcessorValueTreeState::ParameterLayout;
using juce::AudioParameterBool;
using juce::AudioParameterChoice;
using juce::AudioParameterFloat;
using juce::AudioParameterFloatAttributes;
using juce::AudioParameterInt;
using juce::NormalisableRange;
using juce::ParameterID;

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

AudioParameterFloatAttributes withText(std::function<juce::String(float)> text, const juce::String& unit = {})
{
    return AudioParameterFloatAttributes().withStringFromValueFunction([text](float v, int) { return text(v); }).withLabel(unit);
}

AudioParameterFloatAttributes percent()
{
    return withText([](float v) { return juce::String(juce::roundToInt(v * 100.0f)) + "%"; });
}

// Text shown on the LCD in front of "=", per parameter.
const std::map<juce::String, juce::String>& lcdLabels()
{
    static const std::map<juce::String, juce::String> labels {
        { ids::erType, "TYPE" },       { ids::erRoomSize, "ROOM SIZE" }, { ids::erLiveness, "LIVENESS" },
        { ids::erDelay, "DELAY" },     { ids::erLpf, "LPF" },            { ids::balance, "BALANCE" },
        { ids::outLevel, "OUT LVL" },
    };
    return labels;
}

} // namespace

Layout createParameterLayout()
{
    Layout layout;
    const auto& rom17 = spx::romProgram(17).er;

    // Edit buffer of the SPX90: the parameters of the recalled memory.
    layout.add(std::make_unique<AudioParameterInt>(ParameterID { ids::memory, kVersion }, "Memory", 1, 90, 17));
    layout.add(std::make_unique<AudioParameterChoice>(ParameterID { ids::erType, kVersion }, "ER Type",
                                                      juce::StringArray { "HALL", "RANDOM", "REVERSE", "PLATE" },
                                                      (int) rom17.type));
    layout.add(std::make_unique<AudioParameterFloat>(ParameterID { ids::erRoomSize, kVersion }, "ER Room Size",
                                                     steppedRange(0.1f, 20.0f, 0.1f, 4.0f), rom17.roomSize,
                                                     withText([](float v) { return juce::String(v, 1); })));
    layout.add(std::make_unique<AudioParameterInt>(ParameterID { ids::erLiveness, kVersion }, "ER Liveness", 0, 10,
                                                   rom17.liveness));
    layout.add(std::make_unique<AudioParameterFloat>(ParameterID { ids::erDelay, kVersion }, "ER Delay",
                                                     steppedRange(0.1f, 400.0f, 0.1f, 40.0f), rom17.delayMs,
                                                     withText([](float v) { return juce::String(v, 1) + "ms"; }, "ms")));
    juce::StringArray lpfNames;
    for (float hz : spx::kLpfValuesHz)
        lpfNames.add(lpfLabel(hz));
    layout.add(std::make_unique<AudioParameterChoice>(ParameterID { ids::erLpf, kVersion }, "ER LPF", lpfNames,
                                                      lpfNames.size() - 1));
    layout.add(std::make_unique<AudioParameterFloat>(ParameterID { ids::balance, kVersion }, "Balance",
                                                     NormalisableRange<float>(0.0f, 1.0f, 0.01f), 1.0f, percent()));
    layout.add(std::make_unique<AudioParameterFloat>(ParameterID { ids::outLevel, kVersion }, "Out Level",
                                                     NormalisableRange<float>(0.0f, 1.0f, 0.01f), 1.0f, percent()));

    layout.add(std::make_unique<AudioParameterFloat>(ParameterID { ids::input, kVersion }, "Input",
                                                     NormalisableRange<float>(0.0f, 10.0f, 0.01f), 8.0f,
                                                     withText([](float v) { return juce::String(v, 1); })));
    layout.add(std::make_unique<AudioParameterChoice>(ParameterID { ids::inputSwitch, kVersion }, "Input Level Switch",
                                                      juce::StringArray { "+4 dBm", "-20 dBm" }, 0));
    layout.add(std::make_unique<AudioParameterChoice>(ParameterID { ids::outputSwitch, kVersion }, "Output Level Switch",
                                                      juce::StringArray { "+4 dBm", "-20 dBm" }, 0));
    layout.add(std::make_unique<AudioParameterBool>(ParameterID { ids::bypass, kVersion }, "SPX Bypass", false));
    layout.add(std::make_unique<AudioParameterChoice>(ParameterID { ids::outputMode, kVersion }, "Output Mode",
                                                      juce::StringArray { "STEREO", "L ONLY", "MONO" }, 0));

    layout.add(std::make_unique<AudioParameterBool>(ParameterID { ids::glideOn, kVersion }, "Glide On", true));
    layout.add(std::make_unique<AudioParameterChoice>(ParameterID { ids::glideMode, kVersion }, "Glide Mode",
                                                      juce::StringArray { "FREE", "SYNC", "TRIGGER", "MANUAL" }, 0));
    layout.add(std::make_unique<AudioParameterFloat>(ParameterID { ids::glidePeriod, kVersion }, "Glide Rate",
                                                     steppedRange(30.0f, 4000.0f, 1.0f, 400.0f), 333.0f,
                                                     withText([](float v) { return juce::String(juce::roundToInt(v)) + " ms"; }, "ms")));
    juce::StringArray divisions;
    for (const auto& d : kDivisions)
        divisions.add(d.label);
    layout.add(std::make_unique<AudioParameterChoice>(ParameterID { ids::glideDivision, kVersion }, "Glide Sync Rate",
                                                      divisions, kDefaultDivision));
    layout.add(std::make_unique<AudioParameterFloat>(ParameterID { ids::glideDepth, kVersion }, "Glide Depth",
                                                     steppedRange(0.0f, 300.0f, 0.1f, 60.0f), 30.0f,
                                                     withText([](float v) { return juce::String(v, v < 10.0f ? 1 : 0) + " ct"; }, "ct")));
    layout.add(std::make_unique<AudioParameterChoice>(ParameterID { ids::glideDirection, kVersion }, "Glide Direction",
                                                      juce::StringArray { "DOWN", "BOTH", "UP" }, 0));
    layout.add(std::make_unique<AudioParameterFloat>(ParameterID { ids::glideRandom, kVersion }, "Glide Random",
                                                     NormalisableRange<float>(0.0f, 1.0f, 0.01f), 0.15f, percent()));
    layout.add(std::make_unique<AudioParameterFloat>(ParameterID { ids::glideSmooth, kVersion }, "Glide Smooth",
                                                     NormalisableRange<float>(0.0f, 1.0f, 0.01f), 0.2f, percent()));
    juce::StringArray shapes;
    for (auto name : sway::kShapePresetNames)
        shapes.add(juce::String(name.data(), name.size()));
    shapes.add(kCustomShapeName);
    layout.add(std::make_unique<AudioParameterChoice>(ParameterID { ids::glideShape, kVersion }, "Glide Shape", shapes, 0));
    layout.add(std::make_unique<AudioParameterChoice>(ParameterID { ids::glideTriggerSource, kVersion },
                                                      "Glide Trigger", juce::StringArray { "PICK ATTACK", "MIDI NOTE" }, 0));
    layout.add(std::make_unique<AudioParameterFloat>(ParameterID { ids::glideSensitivity, kVersion }, "Glide Sensitivity",
                                                     NormalisableRange<float>(0.0f, 1.0f, 0.01f), 0.5f, percent()));
    layout.add(std::make_unique<AudioParameterFloat>(ParameterID { ids::glideDynamics, kVersion }, "Glide Dynamics",
                                                     NormalisableRange<float>(0.0f, 1.0f, 0.01f), 0.0f, percent()));
    layout.add(std::make_unique<AudioParameterChoice>(ParameterID { ids::glideManualSource, kVersion }, "Glide Manual Source",
                                                      juce::StringArray { "BAR KNOB", "PITCH BEND", "MOD WHEEL", "EXPRESSION (CC11)" }, 0));
    layout.add(std::make_unique<AudioParameterFloat>(ParameterID { ids::glideBar, kVersion }, "Glide Bar",
                                                     NormalisableRange<float>(0.0f, 1.0f, 0.001f), 0.0f, percent()));
    layout.add(std::make_unique<AudioParameterFloat>(ParameterID { ids::glideTwinDetune, kVersion }, "Glide Twin Detune",
                                                     NormalisableRange<float>(0.0f, 30.0f, 0.1f), 8.0f,
                                                     withText([](float v) { return juce::String(v, 1) + " ct"; }, "ct")));
    layout.add(std::make_unique<AudioParameterFloat>(ParameterID { ids::glideTwinLevel, kVersion }, "Glide Twin Level",
                                                     NormalisableRange<float>(0.0f, 1.0f, 0.01f), 0.0f, percent()));
    layout.add(std::make_unique<AudioParameterFloat>(ParameterID { ids::glideMix, kVersion }, "Glide Mix",
                                                     NormalisableRange<float>(0.0f, 1.0f, 0.01f), 1.0f, percent()));
    layout.add(std::make_unique<AudioParameterChoice>(ParameterID { ids::glidePosition, kVersion }, "Glide Position",
                                                      juce::StringArray { "PRE", "POST" }, 0));
    return layout;
}

std::vector<juce::String> parameterPages(int algorithm)
{
    const auto& prog = spx::romProgram(juce::jlimit(1, 30, algorithm));
    if (prog.implemented && prog.algorithm == spx::Algorithm::EarlyReflection)
        return { ids::erType, ids::erRoomSize, ids::erLiveness, ids::erDelay, ids::erLpf };
    return {};
}

std::vector<juce::String> balancePages()
{
    return { ids::balance, ids::outLevel };
}

juce::String lcdText(const juce::RangedAudioParameter& p)
{
    const auto& labels = lcdLabels();
    const auto it = labels.find(p.getParameterID());
    const juce::String label = it != labels.end() ? it->second : p.getName(16).toUpperCase();
    return label + "=" + p.getCurrentValueAsText();
}

} // namespace glide90
