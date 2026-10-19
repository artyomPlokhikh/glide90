#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace glide90
{
class Glide90Processor;
}

namespace glide90::ui
{

// Live position of the bar: 0 at the top, pitch lowered downwards.
class BarIndicator : public juce::Component
{
public:
    void setCents(float c);
    void paint(juce::Graphics&) override;

private:
    float cents = 0.0f;
};

// The Sway block: a vibrato bar that moves by itself (or under MIDI control) before the SPX90.
class SwayPanel : public juce::Component, private juce::Timer
{
public:
    explicit SwayPanel(Glide90Processor& p);
    ~SwayPanel() override;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    struct Knob
    {
        juce::Slider slider { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow };
        juce::String legend;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    };

    void timerCallback() override;

    Glide90Processor& proc;
    std::vector<std::unique_ptr<Knob>> knobs;
    juce::ToggleButton onButton { "SWAY ON" };
    juce::ComboBox position, barSource;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> onAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> positionAttachment, barSourceAttachment;
    BarIndicator bar;
};

} // namespace glide90::ui
