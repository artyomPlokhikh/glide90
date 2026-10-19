#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace glide90
{
class Glide90Processor;
}

namespace glide90::ui
{

// Preset bar: browse factory and user presets, step through them, save your own.
class HeaderBar : public juce::Component, private juce::Timer
{
public:
    explicit HeaderBar(Glide90Processor& p);
    ~HeaderBar() override;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    static constexpr int kUserIdBase = 1000;

    void timerCallback() override;
    void rebuildList();
    void load(int itemId);
    void stepPreset(int delta);
    void askToSave();

    Glide90Processor& proc;
    juce::ComboBox presets;
    juce::TextButton prev { juce::String::fromUTF8("\xe2\x97\x80") }, next { juce::String::fromUTF8("\xe2\x96\xb6") };
    juce::TextButton save { "SAVE" };
    juce::Array<juce::File> userFiles;
    juce::String shownName;
    std::unique_ptr<juce::AlertWindow> saveDialog;
};

} // namespace glide90::ui
