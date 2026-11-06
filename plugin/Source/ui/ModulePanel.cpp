#include "ModulePanel.h"

#include "PanelLookAndFeel.h"

namespace glide90::ui
{
namespace
{
constexpr float kDragThreshold = 4.0f;
} // namespace

ModulePanel::ModulePanel(const juce::String& name) : title(name)
{
    setWantsKeyboardFocus(false);
}

juce::Rectangle<int> ModulePanel::onKeyBounds() const
{
    return { getWidth() - PanelLookAndFeel::kMargin - 60, 8, 60, 28 };
}

juce::Rectangle<int> ModulePanel::handleArea() const
{
    // The title bar but its ON key.
    return draggable ? juce::Rectangle<int>(0, 0, onKeyBounds().getX() - 8, kTitleHeight) : juce::Rectangle<int>();
}

juce::String ModulePanel::getTooltip()
{
    return handleArea().contains(getMouseXYRelative()) ? dragTooltip : juce::String();
}

void ModulePanel::setDraggable(bool d)
{
    draggable = d;
    repaint();
}

void ModulePanel::setLifted(bool l)
{
    if (l != lifted)
    {
        lifted = l;
        repaint();
    }
}

void ModulePanel::paint(juce::Graphics& g)
{
    drawCard(g, getLocalBounds().toFloat(), lifted);
    if (draggable)
    {
        // The grip: two columns of dots, brighter under the mouse.
        const float gx = 13.0f, gy = (float) kTitleHeight * 0.5f - 6.0f;
        g.setColour(theme().textDim.withMultipliedAlpha(hover || lifted ? 1.0f : 0.6f));
        for (int row = 0; row < 3; ++row)
            for (int col = 0; col < 2; ++col)
                g.fillEllipse(gx + (float) col * 5.0f, gy + (float) row * 5.0f, 2.2f, 2.2f);
    }
    // Every title starts after the grip.
    drawCardTitle(g, title, { 28.0f, 8.0f, 260.0f, 26.0f });
}

void ModulePanel::mouseMove(const juce::MouseEvent& e)
{
    if (!draggable)
        return;
    const bool over = handleArea().contains(e.getPosition());
    setMouseCursor(over ? juce::MouseCursor::DraggingHandCursor : juce::MouseCursor::NormalCursor);
    if (over != hover)
    {
        hover = over;
        repaint(handleArea());
    }
}

void ModulePanel::mouseExit(const juce::MouseEvent&)
{
    if (hover && !dragging)
    {
        hover = false;
        repaint(handleArea());
    }
}

void ModulePanel::mouseDown(const juce::MouseEvent& e)
{
    pressed = draggable && e.mods.isLeftButtonDown() && handleArea().contains(e.getPosition());
    if (pressed && getParentComponent() != nullptr)
        start = e.getEventRelativeTo(getParentComponent()).position;
}

void ModulePanel::mouseDrag(const juce::MouseEvent& e)
{
    if (!pressed || getParentComponent() == nullptr)
        return;
    // In the parent: the panel moves under the mouse.
    const float dx = e.getEventRelativeTo(getParentComponent()).position.x - start.x;
    if (!dragging)
    {
        if (std::abs(dx) < kDragThreshold)
            return;
        dragging = true;
        setMouseCursor(juce::MouseCursor::DraggingHandCursor);
        // For Escape, where the host lets the plugin have the keys.
        setWantsKeyboardFocus(true);
        grabKeyboardFocus();
        if (onDragStart)
            onDragStart();
    }
    if (onDrag)
        onDrag(dx);
}

void ModulePanel::mouseUp(const juce::MouseEvent&)
{
    pressed = false;
    if (dragging)
        endDrag(false);
}

bool ModulePanel::keyPressed(const juce::KeyPress& key)
{
    if (dragging && key == juce::KeyPress::escapeKey)
    {
        pressed = false;
        endDrag(true);
        return true;
    }
    return false;
}

void ModulePanel::endDrag(bool cancelled)
{
    dragging = false;
    setWantsKeyboardFocus(false);
    if (onDragEnd)
        onDragEnd(cancelled);
    repaint();
}

} // namespace glide90::ui
