#pragma once

#include "Controls.h"
#include "ModulePanel.h"

#include "HoldEngine.h"

namespace glide90
{
class Glide90Processor;
}

namespace glide90::ui
{

// HOLD's screen: the held chord's waveform (or, in LIVE, the last moments going by) with the grains
// playing it drawn over it, and the layer's level along the bottom.
class HoldDisplay : public juce::Component, public juce::SettableTooltipClient
{
public:
    explicit HoldDisplay(Glide90Processor& p);
    ~HoldDisplay() override;

    void paint(juce::Graphics&) override;

private:
    void frame(double seconds);
    int choice(const char* id) const;

    Glide90Processor& proc;
    hold::HoldDisplay& source;
    std::array<float, 2 * hold::HoldDisplay::kColumns> wave {};
    std::array<float, hold::HoldDisplay::kGrains> grainPos {}, grainAmp {};
    int shownVersion = -1, mode = 0;
    bool on = false, holding = false, latched = false, toGate = true;
    float heldSeconds = 0.0f, levelDb = -100.0f;
    double lastFrame = 0.0;
    juce::VBlankAttachment vblank { this, [this](double seconds) { frame(seconds); } };
};

// The Hold module, under GLIDE and GATE across their width. It stays in its place: HOLD plays beside
// the chain, whatever its order. The screen; then CAPTURE (mode, pick sensitivity, the FREEZE key),
// GRAINS (size, density, smear, spread) and FOLLOW, two rows tall; below them COLOUR (reverse,
// 12-bit, MIDI root) and MIX (duck, decay, level, feed).
class HoldPanel : public ModulePanel, private juce::Timer
{
public:
    explicit HoldPanel(Glide90Processor& p);
    ~HoldPanel() override;

    static constexpr int kColumns = 8;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void updateForMode();
    // Left edge of grid column `column`: the grid is centred in the module, which is wider than it.
    int colX(int column) const;
    int choice(const char* id) const;

    Glide90Processor& proc;
    ToggleKey on, freeze;
    ChoiceList mode, feed, character;
    HoldDisplay display;
    Knob sensitivity, size, density, smear, spread, follow, reverse, root, duck, decay, level;
    int shownMode = -1;
};

} // namespace glide90::ui
