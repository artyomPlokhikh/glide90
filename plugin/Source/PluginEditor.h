#pragma once

#include "PluginProcessor.h"
#include "ui/GatePanel.h"
#include "ui/GlidePanel.h"
#include "ui/HeaderBar.h"
#include "ui/HoldPanel.h"
#include "ui/PanelLookAndFeel.h"
#include "ui/RoutingPanel.h"

namespace glide90
{

// Under the header, PICK SENS above IN and the ROUTING strip beside it: where HOLD goes and which paths
// GLIDE bends. Under them, left to right: IN, GLIDE, the reverse gate, OUT; under the modules, between
// IN and OUT, the HOLD strip, as it plays beside them (folded to its title bar, the window is shorter).
class Glide90Editor : public juce::AudioProcessorEditor
{
public:
    static constexpr int kGap = 12;
    static constexpr int kModulesWidth = ui::GlidePanel::widthFor(ui::GlidePanel::kColumns) + kGap
                                         + ui::GatePanel::widthFor(ui::GatePanel::kColumns);
    static constexpr int kWidth = 2 * ui::PanelLookAndFeel::kMargin + 2 * ui::IoColumn::kWidth + 2 * kGap + kModulesWidth;
    static constexpr int kHeaderHeight = 56, kRouteTop = 64;
    static constexpr int kBodyTop = kRouteTop + ui::RoutingPanel::kHeight + kGap;
    static constexpr int kHoldTop = kBodyTop + ui::ModulePanel::kHeight + kGap;
    static constexpr int heightFor(bool holdFolded)
    {
        return kHoldTop + (holdFolded ? ui::HoldPanel::kFoldedHeight : ui::HoldPanel::kOpenHeight)
               + ui::PanelLookAndFeel::kMargin;
    }
    // The height at 100 %, with HOLD open or folded.
    int contentHeight() const { return heightFor(holdPanel.isFolded()); }

    explicit Glide90Editor(Glide90Processor&);
    ~Glide90Editor() override;

    void resized() override;
    void parentHierarchyChanged() override;

    // Shows the looks' motion for `m` (a still of a look in motion, for snapshots); the next display
    // frame goes back to what the sound does.
    void showMotion(const ui::Motion& m);

private:
    void setZoom(float scale);
    // `wanted`, or a smaller zoom if the window would not fit the screen at it.
    float fittingZoom(float wanted) const;
    void setLook(int index);
    int modulesLeft() const { return ui::PanelLookAndFeel::kMargin + ui::IoColumn::kWidth + kGap; }
    void frame(double seconds);
    // Reads what the sound does into `motion`, for the background and the screens.
    void updateMotion(double seconds);

    Glide90Processor& proc;
    ui::PanelLookAndFeel lookAndFeel;
    // Everything is laid out at 100 % inside `content`, which is scaled to the window.
    ui::Backdrop content;
    ui::HeaderBar header;
    ui::PickSense pickSense;
    ui::RoutingPanel routing;
    ui::IoColumn input, output;
    ui::GlidePanel glide;
    ui::GatePanel gate;
    ui::HoldPanel holdPanel;

    ui::Motion motion;
    double lastFrame = 0.0, riseStart = 0.0, cutAt = -1.0e6;
    float wetDb = -100.0f, loudDb = -40.0f;
    bool rising = false;
    juce::VBlankAttachment vblank { this, [this](double seconds) { frame(seconds); } };
    juce::TooltipWindow tooltips { this, 700 };
};

} // namespace glide90
