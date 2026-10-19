#pragma once

#include "Controls.h"
#include "ModulePanel.h"
#include "ShapeDisplay.h"

namespace glide90
{
class Glide90Processor;
}

namespace glide90::ui
{

// The Glide module: the shape screen, then MOTION (mode, rate, depth, direction, random, smooth) and
// below it SOUND (twin, twin level, mix) beside PICK · CONTROLLER (source, pick sensitivity,
// dynamics: what TRIGGER and MANUAL listen to, dimmed in the other modes). RATE becomes a note value
// in SYNC and the BAR in MANUAL.
class GlidePanel : public ModulePanel, private juce::Timer
{
public:
    explicit GlidePanel(Glide90Processor& p);
    ~GlidePanel() override;

    static constexpr int kColumns = 6;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void updateForMode();
    int choice(const char* id) const;

    Glide90Processor& proc;
    ToggleKey on;
    ChoiceList mode, direction;
    GlassSelector source { false };
    ShapeDisplay display;
    Knob period, division, bar, depth, random, smooth, twinDetune, twinLevel, mix, sensitivity, dynamics;
    int shownMode = -1, shownTrigger = -1, shownManual = -1;
    bool pickGroupActive = false;
};

} // namespace glide90::ui
