#include "Parameters.h"

#include "RomPrograms.h"

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
using juce::AudioParameterIntAttributes;
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

    layout.add(std::make_unique<AudioParameterBool>(ParameterID { ids::swayOn, kVersion }, "Sway On", true));
    layout.add(std::make_unique<AudioParameterFloat>(ParameterID { ids::swayDepth, kVersion }, "Sway Depth",
                                                     NormalisableRange<float>(0.0f, 100.0f, 0.1f), 25.0f,
                                                     withText([](float v) { return juce::String(v, 1) + " ct"; }, "ct")));
    layout.add(std::make_unique<AudioParameterFloat>(ParameterID { ids::swayRate, kVersion }, "Sway Rate",
                                                     steppedRange(0.3f, 8.0f, 0.01f, 2.5f), 3.0f,
                                                     withText([](float v) { return juce::String(v, 2) + " Hz"; }, "Hz")));
    layout.add(std::make_unique<AudioParameterFloat>(ParameterID { ids::swayShape, kVersion }, "Sway Shape",
                                                     NormalisableRange<float>(0.0f, 1.0f, 0.01f), 0.4f, percent()));
    layout.add(std::make_unique<AudioParameterFloat>(ParameterID { ids::swayStrum, kVersion }, "Sway Strum Sync",
                                                     NormalisableRange<float>(0.0f, 1.0f, 0.01f), 0.3f, percent()));
    layout.add(std::make_unique<AudioParameterFloat>(ParameterID { ids::swaySensitivity, kVersion }, "Sway Sensitivity",
                                                     NormalisableRange<float>(0.0f, 1.0f, 0.01f), 0.5f, percent()));
    layout.add(std::make_unique<AudioParameterFloat>(ParameterID { ids::swayAsymmetry, kVersion }, "Sway Asymmetry",
                                                     NormalisableRange<float>(0.0f, 1.0f, 0.01f), 0.0f, percent()));
    layout.add(std::make_unique<AudioParameterFloat>(ParameterID { ids::swayBar, kVersion }, "Sway Bar",
                                                     NormalisableRange<float>(-200.0f, 0.0f, 0.1f), 0.0f,
                                                     withText([](float v) { return juce::String(v, 1) + " ct"; }, "ct")));
    layout.add(std::make_unique<AudioParameterChoice>(ParameterID { ids::swayBarSource, kVersion }, "Sway Bar Source",
                                                      juce::StringArray { "Knob", "Pitch bend", "Mod wheel" }, 0));
    layout.add(std::make_unique<AudioParameterFloat>(ParameterID { ids::swayTwinDetune, kVersion }, "Sway Twin Detune",
                                                     NormalisableRange<float>(0.0f, 30.0f, 0.1f), 8.0f,
                                                     withText([](float v) { return juce::String(v, 1) + " ct"; }, "ct")));
    layout.add(std::make_unique<AudioParameterFloat>(ParameterID { ids::swayTwinLevel, kVersion }, "Sway Twin Level",
                                                     NormalisableRange<float>(0.0f, 1.0f, 0.01f), 0.0f, percent()));
    layout.add(std::make_unique<AudioParameterChoice>(ParameterID { ids::swayPosition, kVersion }, "Sway Position",
                                                      juce::StringArray { "Pre", "Post" }, 0));
    layout.add(std::make_unique<AudioParameterFloat>(ParameterID { ids::swayMix, kVersion }, "Sway Mix",
                                                     NormalisableRange<float>(0.0f, 1.0f, 0.01f), 1.0f, percent()));
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
