#pragma once

#include "Controls.h"
#include "ModulePanel.h"

#include "ErEngine.h"

namespace glide90
{
class Glide90Processor;
}

namespace glide90::ui
{

// The reverse gate's screen: its reflections as they come, swelling up to the cut, with the
// pre-delay before them and the total length.
class GateDisplay : public juce::Component, public juce::SettableTooltipClient
{
public:
    explicit GateDisplay(Glide90Processor& p);

    // What the sound does (set by the editor): a light runs through the reflections with the swell.
    const Motion* motion = nullptr;
    ~GateDisplay() override;

    void paint(juce::Graphics&) override;

private:
    // Rebuilds the taps when a setting changed; true if it did.
    bool update();
    void frame(double seconds);

    Glide90Processor& proc;
    spx::ErSettings settings;
    bool on = true;
    std::vector<spx::ErEngine::Tap> left, right;
    float loudest = 1.0f, lengthMs = 0.0f, shownScaleMs = 1000.0f;
    double lastFrame = 0.0;
    float shownProgress = -1.0f;
    juce::VBlankAttachment vblank { this, [this](double seconds) { frame(seconds); } };
};

// The Reverse Gate module: the screen, ROOM SIZE (what the reverse gate is about) two rows tall,
// liveness and pre-delay, and below them the MIX group: low-pass and mix.
class GatePanel : public ModulePanel
{
public:
    explicit GatePanel(Glide90Processor& p);
    // What the sound does, for the screen's motion (the editor's, read every frame).
    void setMotion(const Motion* m) { display.motion = m; }

    static constexpr int kColumns = 3;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    ToggleKey on;
    GateDisplay display;
    Knob roomSize, liveness, delay, lpf, mix;
};

// The input or the output: its meter, the full height of the screens and the first row, with a dB
// scale, and its level knob on the second row.
class IoColumn : public juce::Component
{
public:
    IoColumn(juce::AudioProcessorValueTreeState& state, const char* paramId, const juce::String& title,
             const juce::String& knobLabel, std::function<float()> takePeak);

    static constexpr int kWidth = 78;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    juce::String title;
    LevelMeter meter;
    Knob knob;
};

} // namespace glide90::ui
