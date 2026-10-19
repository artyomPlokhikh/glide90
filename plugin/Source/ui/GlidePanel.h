#pragma once

#include "Controls.h"
#include "ShapeDisplay.h"

namespace glide90
{
class Glide90Processor;
}

namespace glide90::ui
{

// The Glide unit in three groups: DRIVE (on, mode and rate; then what the mode listens to: source,
// pick sensitivity, dynamics), the shape screen, and MOTION over SOUND (depth, direction, random,
// smooth; twin, mix, position). Every control keeps its place; RATE becomes a note value in SYNC and
// the BAR in MANUAL.
class GlidePanel : public juce::Component, private juce::Timer
{
public:
    explicit GlidePanel(Glide90Processor& p);
    ~GlidePanel() override;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void updateForMode();
    int choice(const char* id) const;

    static constexpr int kRow1 = 24, kRow2 = 140;

    Glide90Processor& proc;
    ToggleKey on;
    SelectorSwitch mode;
    GlassSelector source { false };
    ShapeDisplay display;
    Knob period, division, bar, depth, random, smooth, twinDetune, twinLevel, mix, sensitivity, dynamics;
    ToggleSwitch direction, position;
    int shownMode = -1, shownTrigger = -1, shownManual = -1;
};

} // namespace glide90::ui
