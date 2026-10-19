#pragma once

#include "Widgets.h"

namespace glide90
{
class Glide90Processor;
}

namespace glide90::ui
{

// Every SPX setting on a knob: program, early-reflection parameters, mix and levels.
class SpxControls : public juce::Component, private juce::Timer
{
public:
    explicit SpxControls(Glide90Processor& p);
    ~SpxControls() override;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void rebuildProgramList();

    Glide90Processor& proc;
    juce::ComboBox program;
    ChoiceButtons type;
    Knob roomSize, liveness, delay, lpf, mix, output, input;
    ToggleKey inPad, outPad;
    int listedRevision = -1;
    int shownAlgorithm = -1;
};

} // namespace glide90::ui
