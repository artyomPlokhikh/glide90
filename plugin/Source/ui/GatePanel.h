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

// The Reverse Gate module: the screen across four columns, and under it ROOM SIZE (what the reverse
// gate is about) two rows tall, liveness and pre-delay, and the MIX group: low-pass and mix.
class GatePanel : public ModulePanel
{
public:
    explicit GatePanel(Glide90Processor& p);
    // What the sound does, for the screen's motion (the editor's, read every frame).
    void setMotion(const Motion* m) { display.motion = m; }

    static constexpr int kColumns = 4;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    // The knobs take three columns, centred under the screen.
    static constexpr int knobX(int column) { return columnX(column) + Knob::kColumn / 2; }

    ToggleKey on;
    GateDisplay display;
    Knob roomSize, liveness, delay, lpf, mix;
};

// A light that flashes on each pick attack heard; `picksHeard` counts them.
class PickLight : public juce::Component, public juce::SettableTooltipClient
{
public:
    explicit PickLight(std::function<int()> picksHeard);

    void paint(juce::Graphics&) override;

private:
    void frame(double seconds);

    std::function<int()> picksHeard;
    int seen = -1;
    double litUntil = 0.0;
    bool lit = false;
    juce::VBlankAttachment vblank { this, [this](double seconds) { frame(seconds); } };
};

// The plugin's input or output, down the window's side: a key for its mode (MONO / STEREO ...) under
// the title, its meter with a dB scale, and its level knob at the bottom, level with the lowest row of
// knobs beside it. The input also has PICK SENS and the pick light above its level knob: the pick
// detector listens to the input, for GLIDE and HOLD alike.
class IoColumn : public juce::Component
{
public:
    IoColumn(juce::AudioProcessorValueTreeState& state, const char* levelParamId, const juce::String& title,
             const juce::String& levelLabel, std::function<float()> takePeak, const char* modeParamId,
             int stereoChoice);

    static constexpr int kWidth = 78;

    // Adds PICK SENS for `paramId`, and the light, above the level knob. The meter's top mark then
    // reads CLIP: what the input meter shows is the reverse gate's A/D.
    void addPickSense(juce::AudioProcessorValueTreeState& state, const char* paramId, std::function<int()> picksHeard);
    // The tops of the knobs, in this column: PICK SENS's (if any) and the level knob's.
    void setKnobRows(int pickTop, int levelTop);

    void paint(juce::Graphics&) override;
    void resized() override;

    // Where the meter's dB marks are drawn.
    juce::Rectangle<float> scaleArea() const;

    CycleKey& modeKey() { return mode; }
    Knob& levelKnob() { return knob; }
    Knob* pickKnob() { return pick.get(); }
    PickLight* pickLight() { return light.get(); }

private:
    juce::String title;
    LevelMeter meter;
    Knob knob;
    CycleKey mode;
    std::unique_ptr<Knob> pick;
    std::unique_ptr<PickLight> light;
    int pickRow = 0, levelRow = 0;
};

} // namespace glide90::ui
