#pragma once

#include "PluginProcessor.h"
#include "ui/GlidePanel.h"
#include "ui/HeaderBar.h"
#include "ui/PanelLookAndFeel.h"
#include "ui/Rack.h"
#include "ui/RearPanel.h"
#include "ui/SpxControls.h"
#include "ui/SpxPanel.h"

namespace glide90
{

class Glide90Editor : public juce::AudioProcessorEditor
{
public:
    // The units' width; the rack's ears add to it.
    static constexpr int kWidth = 1000;
    static constexpr int kWindowWidth = kWidth + 2 * ui::Rack::kEarWidth;
    static constexpr int kHeaderHeight = 48, kStripHeight = 112, kSpxHeight = 138, kGlideHeight = 256, kRearHeight = 40;
    static constexpr int kHeight = kHeaderHeight + kStripHeight + kSpxHeight + kGlideHeight + kRearHeight;

    explicit Glide90Editor(Glide90Processor&);
    ~Glide90Editor() override;

    void resized() override;

private:
    void setZoom(float scale);

    Glide90Processor& proc;
    ui::PanelLookAndFeel lookAndFeel;
    // Everything is laid out at 100 % inside `content`, which is scaled to the window.
    ui::Rack content;
    ui::HeaderBar header;
    ui::SpxPanel strip;
    ui::SpxControls spx;
    ui::GlidePanel glide;
    ui::RearPanel rear;
    juce::TooltipWindow tooltips { this, 700 };
};

} // namespace glide90
