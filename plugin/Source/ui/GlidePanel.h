#pragma once

#include "Controls.h"
#include "GatePanel.h"
#include "ModulePanel.h"
#include "ShapeDisplay.h"

namespace glide90
{
class Glide90Processor;
}

namespace glide90::ui
{

// The Glide module: the shape screen, then what the bar does (mode, rate, depth, direction). RATE is
// milliseconds, or a note value with its SYNC key on, and in MANUAL the slot holds the BAR. Under them
// PICK (PICK SENS, DYNAMICS: what TRIGGER listens to, dimmed in the other modes) and HAND (random, smooth).
class GlidePanel : public ModulePanel, private juce::Timer
{
public:
    explicit GlidePanel(Glide90Processor& p);
    ~GlidePanel() override;

    static constexpr int kColumns = 4;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void updateForMode();
    int choice(const char* id) const;

    Glide90Processor& proc;
    ToggleKey on, sync;
    ChoiceList mode, direction;
    ShapeDisplay display;
    Knob period, division, bar, depth, random, smooth, dynamics;
    PickSense pickSense;
    int shownMode = -1, shownSync = -1;
    bool pickGroupActive = false;
};

} // namespace glide90::ui
