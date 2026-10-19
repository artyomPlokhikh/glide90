#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace glide90::ui
{

// A parameter knob with its name above and its value below. Double-click resets to the default.
class Knob : public juce::Component
{
public:
    Knob(juce::AudioProcessorValueTreeState& state, const char* paramId, const juce::String& name,
         juce::Colour accent, bool bipolar = false);

    void paint(juce::Graphics&) override;
    void resized() override;
    void setTooltip(const juce::String& text) { slider.setTooltip(text); }

    juce::Slider slider { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow };

private:
    juce::String name;
    juce::AudioProcessorValueTreeState::SliderAttachment attachment;
};

// A row (or column) of buttons choosing one value of a choice parameter.
class ChoiceButtons : public juce::Component, public juce::SettableTooltipClient
{
public:
    ChoiceButtons(juce::AudioProcessorValueTreeState& state, const char* paramId, juce::Colour accent,
                  bool vertical = false);

    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;

private:
    juce::Rectangle<float> segment(int index) const;

    juce::StringArray labels;
    juce::Colour accent;
    bool vertical;
    int selected = 0;
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
