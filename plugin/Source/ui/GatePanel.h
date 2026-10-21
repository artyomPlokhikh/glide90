#pragma once

#include "Controls.h"
#include "ModulePanel.h"
#include "Product.h"

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
// gate is about) two rows tall, liveness and pre-delay, and the TONE · LEVEL group: low-pass and level.
class GatePanel : public ModulePanel
{
public:
    explicit GatePanel(Glide90Processor& p);
    // What the sound does, for the screen's motion (the editor's, read every frame).
    void setMotion(const Motion* m) { display.motion = m; }

    // LITE: three, the screen as wide as the knobs.
    static constexpr int kColumns = kLite ? 3 : 4;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    // The knobs take three columns, centred under the screen.
    static constexpr int knobX(int column) { return columnX(column) + (kColumns - 3) * Knob::kColumn / 2; }

    ToggleKey on;
    GateDisplay display;
    Knob roomSize, liveness, delay, lpf, level;
};

// PICK SENS on its own, above IN: how easily the pick detector GLIDE and HOLD both listen to hears a
// pick attack. No card: a bare knob. Its pointer lights up on each pick heard (`picksHeard` counts them).
class PickSense : public juce::Component
{
public:
    PickSense(juce::AudioProcessorValueTreeState& state, const char* paramId, std::function<int()> picksHeard);

    Knob& knob() { return dial; }
    // How lit the pointer is now, 0-1 (for tests).
    float flashLevel() const { return flash; }

    void resized() override;

private:
    void frame(double seconds);

    Knob dial;
    std::function<int()> picksHeard;
    int seen = -1;
    double litUntil = 0.0, lastFrame = 0.0;
    float flash = 0.0f;
    juce::VBlankAttachment vblank { this, [this](double seconds) { frame(seconds); } };
};

// The plugin's input or output, down the window's side: a key for its mode (MONO / STEREO ...) under
// the title, its meter with a dB scale, and its level knob at the bottom, level with the lowest row of
// knobs beside it.
class IoColumn : public juce::Component
{
public:
    IoColumn(juce::AudioProcessorValueTreeState& state, const char* levelParamId, const juce::String& title,
             const juce::String& levelLabel, std::function<float()> takePeak, const char* modeParamId,
             int stereoChoice);

    static constexpr int kWidth = 78;

    // The meter's top mark reads CLIP: what the input meter shows is the reverse gate's A/D.
    void markClip();
    // The top of the level knob, in this column.
    void setKnobRow(int levelTop);

    void paint(juce::Graphics&) override;
    void resized() override;

    // Where the meter's dB marks are drawn.
    juce::Rectangle<float> scaleArea() const;

    const juce::String& getTitle() const { return title; }
    CycleKey& modeKey() { return mode; }
    Knob& levelKnob() { return knob; }

private:
    juce::String title;
    LevelMeter meter;
    Knob knob;
    CycleKey mode;
    bool clip = false;
    int levelRow = 0;
};

} // namespace glide90::ui
