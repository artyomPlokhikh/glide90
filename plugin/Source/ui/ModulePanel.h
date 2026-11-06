#pragma once

#include "Controls.h"
#include "PanelLookAndFeel.h"

namespace glide90::ui
{

// A module (GLIDE, REVERSE GATE): a card with its name in a title bar, on the grid both share: the title
// bar, a screen, two rows of controls. The title bar is a handle: dragging it to the other place turns
// their order round (the owner moves the panel and decides where it lands).
class ModulePanel : public juce::Component, public juce::TooltipClient
{
public:
    explicit ModulePanel(const juce::String& title);

    static constexpr int kTitleHeight = 40, kScreenTop = 42, kScreenHeight = 132;
    static constexpr int kRow1 = 184, kRuleY = 298, kRow2 = 312, kHeight = 420;
    // Left edge of grid column `column`.
    static constexpr int columnX(int column) { return PanelLookAndFeel::kMargin + column * Knob::kColumn; }
    static constexpr int widthFor(int columns) { return 2 * PanelLookAndFeel::kMargin + columns * Knob::kColumn; }

    const juce::String& getTitle() const { return title; }

    // The title bar as a handle (a grip before the title). The drag, in the parent's coordinates:
    // started, moved by `dx` from where it started, ended (cancelled: Escape).
    void setDraggable(bool);
    bool isDraggable() const { return draggable; }
    std::function<void()> onDragStart;
    std::function<void(float dx)> onDrag;
    std::function<void(bool cancelled)> onDragEnd;
    // What dragging the title bar does, shown over the handle only.
    void setDragTooltip(const juce::String& text) { dragTooltip = text; }
    juce::String getTooltip() override;
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

    juce::String title, dragTooltip;
    bool draggable = false, hover = false, pressed = false, dragging = false, lifted = false;
    juce::Point<float> start; // in the parent
};

} // namespace glide90::ui
