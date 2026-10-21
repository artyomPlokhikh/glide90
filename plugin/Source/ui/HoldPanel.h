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

// Where a held sound dragged out of HOLD's screen is written: a folder that stays (a host may refer to
// the file rather than copy it).
juce::File holdCaptureFolder();
// Writes a held sound to a new WAV file in `folder` (24 bits at `rate`) and returns it, or an invalid
// File. `raw`: the capture itself, mono. Otherwise HOLD playing it frozen (no DUCK, no bar), stereo,
// kCaptureSeconds long, faded at both ends and peaking at -1 dBFS.
inline constexpr double kCaptureSeconds = 8.0;
juce::File writeHoldCapture(const std::vector<float>& held, double rate, float readFrom, const hold::HoldSettings& s,
                            bool raw, const juce::File& folder);

// HOLD's screen. Open: MODE (arrows, or a click for the list) and DENSITY on its top line, the held
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
    // Dragging the picture out: the held sound as a file (Alt: the raw capture).
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;

private:
    // What a click can change, and where.
    enum class Spot
    {
        Nowhere,
        ModeDown,
        Mode,
        ModeUp,
        DensityDown,
        Density,
        DensityUp,
        Character,
        VoicesDown,
        Voices,
        VoicesUp,
        RootDown,
        Root,
        RootUp
    };
    Spot spotAt(juce::Point<float> p) const;
    juce::Rectangle<float> spotArea(Spot s) const;
    void step(const char* id, int delta, bool wrap);
    void setTo(const char* id, int value);
    juce::String status() const;
    juce::Rectangle<float> plotArea() const;
    bool hasSound() const;
    void paintWave(juce::Graphics&, juce::Rectangle<float> plot, bool grains);
    void frame(double seconds);
    int choice(const char* id) const;

    Glide90Processor& proc;
    hold::HoldDisplay& source;
    std::array<float, 2 * hold::HoldDisplay::kColumns> wave {};
    std::array<float, hold::HoldDisplay::kGrains> grainPos {}, grainAmp {};
    int shownVersion = -1, mode = 0, feed = 0, density = 2, character = 0, root = 60, noteOns = 0;
    int autoRoot = -1; // the note AUTO found, or -1
    int learnFrom = -1; // ROOT from a key: the note-on count when asked, until the next one
    int voices = 0;
    bool midiIn = false, dragging = false;
    bool on = false, holding = false, frozen = false, gateBypassed = false, compact = false;
    std::array<uint64_t, 2> notes {}; // MIDI keys sounding
    float heldSeconds = 0.0f, levelDb = -100.0f;
    Spot hover = Spot::Nowhere;
    double lastFrame = 0.0;
    juce::VBlankAttachment vblank { this, [this](double seconds) { frame(seconds); } };
};

// FREEZE, played like a footswitch: a click latches it (and a click lets it go), a press held longer
// than kHoldMs freezes only while held. Never dimmed: it works in every mode.
class FreezeKey : public juce::TextButton
{
public:
    FreezeKey(juce::AudioProcessorValueTreeState& state, const char* paramId);

    static constexpr juce::uint32 kHoldMs = 400;

    void mouseDown(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
    // What a press and its release do, at these times (ms): for the mouse, and for tests.
    void press(juce::uint32 nowMs);
    void release(juce::uint32 nowMs);

private:
    juce::RangedAudioParameter& param;
    juce::ParameterAttachment attachment;
    juce::uint32 downAt = 0;
    bool wasOn = false, pressed = false;
};

// The Hold strip, under GLIDE and GATE between IN and OUT: one row, so the window stays close to its
// size without HOLD. Its title bar has FREEZE and ON; a click on the title folds the strip down to the
// bar (a setting of the user, not of the sound), the screen then shrinking into it. Open: the screen,
// then GRAINS (size, smear, spread, reverse; density is on the screen) and PLAY (swell, tail, decay,
// duck, level). Where HOLD goes and whether the bar bends it is the ROUTING's; PICK SENS is IN's: the
// same picking feeds the glide and HOLD.
class HoldPanel : public ModulePanel
{
public:
    explicit HoldPanel(Glide90Processor& p);
    ~HoldPanel() override;

    static constexpr int kFoldedHeight = kTitleHeight, kOpenHeight = 164;
    static constexpr int kRuleY = 46, kRowTop = 56, kScreenWidth = 212, kKnobColumn = 76, kGrainKnobs = 4;

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
    std::vector<Knob*> knobs(); // in their order on the strip: GRAINS, then PLAY
    int knobX(int index);

    Glide90Processor& proc;
    ToggleKey on;
    FreezeKey freeze;
    HoldDisplay display;
    Knob size, smear, spread, reverse, swell, tail, decay, duck, level;
    bool folded = false;
};

} // namespace glide90::ui
