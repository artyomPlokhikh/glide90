#pragma once

#include "Controls.h"

namespace glide90
{
class Glide90Processor;
}

namespace glide90::ui
{

// Every reverb setting on a control: program, type, the early-reflection parameters, mix and output
// level. (The rear-panel switches are on the RearPanel strip.)
class SpxControls : public juce::Component, private juce::Timer
{
public:
    explicit SpxControls(Glide90Processor& p);
    ~SpxControls() override;

    // Called when a control is touched, with its parameter (the LCD shows it).
    std::function<void(const juce::RangedAudioParameter&)> onTouch;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void stepProgram(int delta);

    static constexpr int kRowTop = 24;

    Glide90Processor& proc;
    GlassSelector program { true };
    SelectorSwitch type;
    Knob roomSize, liveness, delay, lpf, mix, output;

    int shownAlgorithm = -1;
};

} // namespace glide90::ui
