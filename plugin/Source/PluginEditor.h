#pragma once

#include "PluginProcessor.h"
#include "ui/GlidePanel.h"
#include "ui/HeaderBar.h"
#include "ui/PanelLookAndFeel.h"
#include "ui/SpxControls.h"
#include "ui/SpxPanel.h"

namespace glide90
{

class Glide90Editor : public juce::AudioProcessorEditor
{
public:
    static constexpr int kWidth = 1000;
    static constexpr int kHeaderHeight = 44, kStripHeight = 110, kSpxHeight = 150, kGlideHeight = 320;

    explicit Glide90Editor(Glide90Processor&);
    ~Glide90Editor() override;

    void resized() override;

private:
    ui::PanelLookAndFeel lookAndFeel;
    ui::HeaderBar header;
    ui::SpxPanel strip;
    ui::SpxControls spx;
    ui::GlidePanel glide;
    juce::TooltipWindow tooltips { this, 600 };
};

} // namespace glide90
