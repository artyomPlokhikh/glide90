#pragma once

#include "Controls.h"
#include "PanelLookAndFeel.h"

namespace glide90::ui
{

// A module (Glide, Reverse Gate): a card with its name in a title bar, on the grid every module
// shares: the title bar, a screen, two rows of controls. The title bar is the handle to drag the
// module to the other place; the owner moves it and decides where it lands.
class ModulePanel : public juce::Component
{
public:
    explicit ModulePanel(const juce::String& title);

    static constexpr int kTitleHeight = 40, kScreenTop = 42, kScreenHeight = 132;
    static constexpr int kRow1 = 184, kRuleY = 298, kRow2 = 312, kHeight = 420;
    // Left edge of grid column `column`.
    static constexpr int columnX(int column) { return PanelLookAndFeel::kMargin + column * Knob::kColumn; }
    static constexpr int widthFor(int columns) { return 2 * PanelLookAndFeel::kMargin + columns * Knob::kColumn; }

    // The drag, in the parent's coordinates: started, moved by `dx` from where it started, ended
    // (cancelled: Escape, or the mouse taken away).
    std::function<void()> onDragStart;
    std::function<void(float dx)> onDrag;
    std::function<void(bool cancelled)> onDragEnd;
    // Drawn raised while it is dragged.
    void setLifted(bool);

    void paint(juce::Graphics&) override;
    void mouseMove(const juce::MouseEvent&) override;
    void mouseExit(const juce::MouseEvent&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
    bool keyPressed(const juce::KeyPress&) override;

protected:
    // Where the ON key sits, at the right end of the title bar.
    juce::Rectangle<int> onKeyBounds() const;

private:
    juce::Rectangle<int> handleArea() const;
    void endDrag(bool cancelled);

    juce::String title;
    bool hover = false, pressed = false, dragging = false, lifted = false;
    juce::Point<float> start; // in the parent
};

} // namespace glide90::ui
