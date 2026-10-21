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

// The Glide module: the shape screen, then MOTION (mode, rate, depth, direction, random, smooth) and
// below MODE the CONTROL group (source, dynamics: what TRIGGER and MANUAL listen to, dimmed in the
// other modes). RATE becomes a note value in SYNC and the
// BAR in MANUAL. In GLIDE90 PICK SENS is IN's (HOLD listens to the picking too); in LITE it is
// GLIDE's own, third in CONTROL.
class GlidePanel : public ModulePanel, private juce::Timer
{
public:
    explicit GlidePanel(Glide90Processor& p);
    ~GlidePanel() override;

    // LITE: five, MOTION's knobs in the first row, DIRECTION under MODE and CONTROL beside it.
    static constexpr int kColumns = kLite ? 5 : 6;

    void paint(juce::Graphics&) override;
    void resized() override;
    bool groupNamesQuiet() const override { return !controlGroupActive; }

private:
    void timerCallback() override;
    void updateForMode();
    int choice(const char* id) const;

    Glide90Processor& proc;
    ToggleKey on;
    ChoiceList mode, direction;
    GlassSelector source { false };
    ShapeDisplay display;
    Knob period, division, bar, depth, random, smooth, dynamics;
    std::optional<PickSense> pickSense; // LITE
    int shownMode = -1, shownTrigger = -1, shownManual = -1;
    bool controlGroupActive = false;
};

} // namespace glide90::ui
