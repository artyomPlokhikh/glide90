#include "Rack.h"

#include "PanelLookAndFeel.h"

namespace glide90::ui
{

void Rack::paint(juce::Graphics& g)
{
    for (auto* unit : getChildren())
    {
        if (unit->getX() != kEarWidth || unit->getRight() != getWidth() - kEarWidth)
            continue; // a dialog, not a unit
        const auto row = juce::Rectangle<int>(0, unit->getY(), getWidth(), unit->getHeight());
        for (auto ear : { row.withRight(unit->getX()), row.withLeft(unit->getRight()) })
        {
            // The ears are the unit's face, folded at the rack.
            drawRackUnit(g, ear);
            const auto r = ear.toFloat();
            const bool left = ear.getX() == 0;

            // One slot on a short unit, two on a tall one, near its top and bottom.
            const auto slot = juce::Rectangle<float>(20.0f, 11.0f);
            const float x = r.getCentreX() + (left ? -2.0f : 2.0f);
            if (r.getHeight() < 80.0f)
                drawRackSlot(g, slot.withCentre({ x, r.getCentreY() }));
            else
                for (float y : { r.getY() + 22.0f, r.getBottom() - 22.0f })
                    drawRackSlot(g, slot.withCentre({ x, y }));
        }
    }
}

} // namespace glide90::ui
