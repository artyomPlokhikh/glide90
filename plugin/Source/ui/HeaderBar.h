#pragma once

#include "Controls.h"

namespace glide90
{
class Glide90Processor;
}

namespace glide90::ui
{

// Preset bar: browse and step through factory and user presets (an asterisk marks unsaved changes),
// save, compare A/B, undo/redo and zoom the window.
class HeaderBar : public juce::Component, private juce::Timer
{
public:
    explicit HeaderBar(Glide90Processor& p);
    ~HeaderBar() override;

    // Called with the new zoom factor.
    std::function<void(float)> onZoom;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void refreshUserFiles();
    void stepPreset(int delta);
    void askToSave();

    Glide90Processor& proc;
    GlassSelector presets { true, true }, zoom { false };
    juce::TextButton save { "SAVE" }, a { "A" }, b { "B" }, undoKey { "UNDO" }, redoKey { "REDO" };
    juce::Array<juce::File> userFiles;
    std::unique_ptr<juce::AlertWindow> saveDialog;
};

} // namespace glide90::ui
