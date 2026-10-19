#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace glide90::ui
{

// A parameter knob with its name above and its value in a small window below. Double-click resets
// it to the default.
class Knob : public juce::Component
{
public:
    Knob(juce::AudioProcessorValueTreeState& state, const char* paramId, const juce::String& name);

    void paint(juce::Graphics&) override;
    void resized() override;
    void setTooltip(const juce::String& text) { slider.setTooltip(text); }

    juce::Slider slider { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow };

private:
    juce::String name;
    juce::AudioProcessorValueTreeState::SliderAttachment attachment;
};

// A row of keys choosing one value of a choice parameter; the chosen key's LED is lit.
class ChoiceButtons : public juce::Component, public juce::SettableTooltipClient
{
public:
    ChoiceButtons(juce::AudioProcessorValueTreeState& state, const char* paramId);

    void resized() override;

private:
    juce::OwnedArray<juce::TextButton> keys;
    juce::ParameterAttachment attachment;
};

// A key with an LED that toggles a two-state parameter.
class ToggleKey : public juce::TextButton
{
public:
    ToggleKey(juce::AudioProcessorValueTreeState& state, const char* paramId, const juce::String& label);

private:
    juce::ParameterAttachment attachment;
};

} // namespace glide90::ui
