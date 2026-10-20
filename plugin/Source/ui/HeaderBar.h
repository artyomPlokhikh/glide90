#pragma once

#include "Controls.h"
#include "Dialog.h"

namespace glide90
{
class Glide90Processor;
}

namespace glide90::ui
{

// The top bar: the name; browse and step through factory and user presets (an asterisk marks
// unsaved changes), save or delete, compare A/B, undo/redo; SETUP for the window size, the look and
// its motion.
class HeaderBar : public juce::Component, private juce::Timer
{
public:
    explicit HeaderBar(Glide90Processor& p);
    ~HeaderBar() override;

    // Called with the new zoom factor.
    std::function<void(float)> onZoom;
    // Called with the index of the look chosen.
    std::function<void(int)> onTheme;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void refreshUserFiles();
    void stepPreset(int delta);
    void askToSave();
    void savePreset(const juce::String& name);
    // The file of the current preset, if it is a user preset that still exists.
    juce::File currentUserFile() const;
    void askToDelete(const juce::File& file);
    // Shows a dialog inside the plugin window (it gets the panel's look and closes with the window).
    void showDialog(std::unique_ptr<Dialog> window, std::function<void(int)> onResult);

    Glide90Processor& proc;
    void showSetup();

    GlassSelector presets { true, true };
    juce::TextButton save { "SAVE" }, a { "A" }, b { "B" }, undoKey { "UNDO" }, redoKey { "REDO" }, setup { "SETUP" };
    juce::Array<juce::File> userFiles;
    std::unique_ptr<Dialog> dialog;
};

} // namespace glide90::ui
