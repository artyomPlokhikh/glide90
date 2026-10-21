#include "ModulePanel.h"

#include "PanelLookAndFeel.h"

namespace glide90::ui
{

ModulePanel::ModulePanel(const juce::String& name) : title(name)
{
    setWantsKeyboardFocus(false);
}

juce::Rectangle<int> ModulePanel::onKeyBounds() const
{
    return { getWidth() - PanelLookAndFeel::kMargin - 60, 8, 60, 28 };
}

void ModulePanel::paint(juce::Graphics& g)
{
    drawCard(g, getLocalBounds().toFloat());
    // Every title starts where HOLD's starts, after the chevron that folds it.
    drawCardTitle(g, title, { 28.0f, 8.0f, 260.0f, 26.0f });
}

} // namespace glide90::ui
