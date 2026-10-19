#pragma once

#include "ShapeDisplay.h"
#include "Widgets.h"

namespace glide90
{
class Glide90Processor;
}

namespace glide90::ui
{

// The Glide block: the shape screen on the left, the controls on the right. Controls that do not
// apply to the current mode swap out (RATE in ms or as a note value, the trigger or manual source).
class GlidePanel : public juce::Component, private juce::Timer
{
public:
    explicit GlidePanel(Glide90Processor& p);
    ~GlidePanel() override;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void updateForMode();

    Glide90Processor& proc;
    ToggleKey on;
    ChoiceButtons mode, direction, position;
    ShapeDisplay display;
    Knob period, division, depth, random, smooth, sensitivity, bar, twinDetune, twinLevel, mix;
    juce::ComboBox triggerSource, manualSource;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> triggerAttachment, manualAttachment;
    int shownMode = -1, shownTrigger = -1, shownManual = -1;
};

} // namespace glide90::ui
