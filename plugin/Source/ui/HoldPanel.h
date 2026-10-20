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

// HOLD's screen. Open: MODE (arrows, or a click for the list) and FEED on its top line, the held
// chord's waveform (in LIVE the last moments going by) with the grains playing it, the layer's level,
// and SAMPLER and (in MIDI) ROOT on its bottom line. Folded into the title bar: what HOLD does in one
// line, the waveform beside it.
class HoldDisplay : public juce::Component, public juce::SettableTooltipClient
{
public:
    explicit HoldDisplay(Glide90Processor& p);
    ~HoldDisplay() override;

    void setCompact(bool c);
    // A click on the folded bar: the owner unfolds the strip.
    std::function<void()> onUnfold;

    void paint(juce::Graphics&) override;
    void mouseMove(const juce::MouseEvent&) override;
    void mouseExit(const juce::MouseEvent&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

private:
    // What a click can change, and where.
    enum class Spot
    {
        Nowhere,
        ModeDown,
        Mode,
        ModeUp,
        Feed,
        Character,
        RootDown,
        Root,
        RootUp
    };
    Spot spotAt(juce::Point<float> p) const;
    juce::Rectangle<float> spotArea(Spot s) const;
    void step(const char* id, int delta, bool wrap);
    void setTo(const char* id, int value);
    juce::String status() const;
    void paintWave(juce::Graphics&, juce::Rectangle<float> plot, bool grains);
    void frame(double seconds);
    int choice(const char* id) const;

    Glide90Processor& proc;
    hold::HoldDisplay& source;
    std::array<float, 2 * hold::HoldDisplay::kColumns> wave {};
    std::array<float, hold::HoldDisplay::kGrains> grainPos {}, grainAmp {};
    int shownVersion = -1, mode = 0, feed = 0, character = 0, root = 60;
    bool on = false, holding = false, latched = false, gateBypassed = false, compact = false;
    float heldSeconds = 0.0f, levelDb = -100.0f;
    Spot hover = Spot::Nowhere;
    double lastFrame = 0.0;
    juce::VBlankAttachment vblank { this, [this](double seconds) { frame(seconds); } };
};

// The Hold strip, across the window under GLIDE and GATE: one row, so the window stays close to its
// size without HOLD. Its title bar has FREEZE and ON; a click on the title folds the strip down to
// the bar (a setting of the user, not of the sound), the screen then shrinking into it. Open: the
// screen, then FOLLOW, the grains (size, density, smear, spread, reverse) and the mix (duck, decay,
// level). PICK SENS is the glide's: the same picking feeds both.
class HoldPanel : public ModulePanel
{
public:
    explicit HoldPanel(Glide90Processor& p);
    ~HoldPanel() override;

    static constexpr int kFoldedHeight = kTitleHeight, kOpenHeight = 156;
    static constexpr int kRowTop = 46, kScreenWidth = 212;

    bool isFolded() const { return folded; }
    void setFolded(bool);
    // The user folded or unfolded it (the window changes height).
    std::function<void(bool folded)> onFold;

    void paint(juce::Graphics&) override;
    void resized() override;
    void mouseMove(const juce::MouseEvent&) override;
    void mouseDown(const juce::MouseEvent&) override;

private:
    juce::Rectangle<int> titleArea() const;
    juce::Rectangle<int> freezeBounds() const;

    Glide90Processor& proc;
    ToggleKey on, freeze;
    HoldDisplay display;
    Knob follow, size, density, smear, spread, reverse, duck, decay, level;
    bool folded = false;
    juce::ParameterAttachment modeWatch; // FREEZE is lit only in LATCH
};

} // namespace glide90::ui
