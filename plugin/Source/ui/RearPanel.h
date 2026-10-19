#pragma once

#include "Controls.h"

namespace glide90::ui
{

// The unit's rear panel as a thin strip: the level switches and the output mode. These set up the
// rig (what feeds the plugin and what it feeds), not the sound.
class RearPanel : public juce::Component
{
public:
    explicit RearPanel(juce::AudioProcessorValueTreeState& state);

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    SlideSwitch inLevel, outLevel, outputMode;
};

} // namespace glide90::ui
