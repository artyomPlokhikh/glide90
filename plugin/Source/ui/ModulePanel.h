#pragma once

#include "Controls.h"
#include "PanelLookAndFeel.h"

namespace glide90::ui
{

// A module (Glide, Reverse Gate, Hold): a card with its name in a title bar, on the grid every module
// shares: the title bar, a screen, two rows of controls. The modules stay in their places: the route
// strip above them says which way the signal goes.
class ModulePanel : public juce::Component
{
public:
    explicit ModulePanel(const juce::String& title);

    static constexpr int kTitleHeight = 40, kScreenTop = 42, kScreenHeight = 132;
    static constexpr int kRow1 = 184, kRuleY = 298, kRow2 = 312, kHeight = 420;
    // Left edge of grid column `column`.
    static constexpr int columnX(int column) { return PanelLookAndFeel::kMargin + column * Knob::kColumn; }
    static constexpr int widthFor(int columns) { return 2 * PanelLookAndFeel::kMargin + columns * Knob::kColumn; }

    const juce::String& getTitle() const { return title; }
    // Whether the names over the second row are all drawn quiet (the controls under them not listened to).
    virtual bool groupNamesQuiet() const { return false; }

    void paint(juce::Graphics&) override;

protected:
    // Where the ON key sits, at the right end of the title bar.
    juce::Rectangle<int> onKeyBounds() const;

private:
    juce::String title;
};

} // namespace glide90::ui
