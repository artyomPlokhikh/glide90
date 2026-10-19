#pragma once

#include "PluginProcessor.h"
#include "ui/PanelLookAndFeel.h"
#include "ui/SpxPanel.h"
#include "ui/SwayPanel.h"

namespace glide90
{

class Glide90Editor : public juce::AudioProcessorEditor
{
public:
    static constexpr int kWidth = 1000, kSpxHeight = 200, kSwayHeight = 190;

    explicit Glide90Editor(Glide90Processor&);
    ~Glide90Editor() override;

    void resized() override;

private:
    ui::PanelLookAndFeel lookAndFeel;
    ui::SpxPanel spx;
    ui::SwayPanel sway;
    juce::TooltipWindow tooltips { this, 600 };
};

} // namespace glide90
