#pragma once

#include "LcdDisplay.h"

#include <juce_audio_processors/juce_audio_processors.h>

namespace glide90
{
class Glide90Processor;
}

namespace glide90::ui
{

// The SPX90-style front panel. Works like the unit: MEMORY up/down picks a memory number (it blinks
// until RECALL loads it), PARAMETER steps through the program's parameters, BALANCE through
// BALANCE / OUT LVL, and the -1 / +1 keys (or the mouse wheel over the LCD) change the value shown.
class SpxPanel : public juce::Component, private juce::Timer
{
public:
    explicit SpxPanel(Glide90Processor& p);
    ~SpxPanel() override;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void refresh();
    juce::String currentPage() const;
    void step(int direction);
    void showMessage(const juce::String& text);
    juce::String titleOf(int memory) const;

    Glide90Processor& proc;

    LcdDisplay lcd;
    SevenSegment memoryLed;
    LevelMeter meter;
    juce::Slider inputKnob { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::NoTextBox };
    juce::AudioProcessorValueTreeState::SliderAttachment inputAttachment;

    juce::TextButton parameterKey { "PARAMETER" }, balanceKey { "BALANCE" }, decKey { "-1" }, incKey { "+1" };
    juce::TextButton memDownKey { juce::String::fromUTF8("\xe2\x96\xbc") }, memUpKey { juce::String::fromUTF8("\xe2\x96\xb2") };
    juce::TextButton storeKey { "STORE" }, recallKey { "RECALL" }, bypassKey { "BYPASS" };
    juce::TextButton inSwitch { "-20 dBm" }, outSwitch { "-20 dBm" };
    std::unique_ptr<juce::ParameterAttachment> bypassAttachment, inSwitchAttachment, outSwitchAttachment;

    bool balanceMode = false;
    int pageIndex = 0;
    int pendingMemory = 17;
    int lastLoaded = -1;
    juce::String message;
    juce::uint32 messageUntil = 0;
    juce::uint32 keyDownSince = 0;
    float meterDb = -100.0f;
};

} // namespace glide90::ui
