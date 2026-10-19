#include "PanelLookAndFeel.h"

namespace glide90::ui
{

juce::Font panelFont(float height, bool bold)
{
    return juce::Font(juce::FontOptions(height, bold ? juce::Font::bold : juce::Font::plain));
}

PanelLookAndFeel::PanelLookAndFeel()
{
    setColour(juce::ComboBox::backgroundColourId, juce::Colour(0xff17181a));
    setColour(juce::ComboBox::outlineColourId, juce::Colour(0xff505257));
    setColour(juce::ComboBox::textColourId, colours::legend);
    setColour(juce::ComboBox::arrowColourId, colours::legendDim);
    setColour(juce::PopupMenu::backgroundColourId, juce::Colour(0xff1d1e21));
    setColour(juce::PopupMenu::textColourId, colours::legend);
    setColour(juce::PopupMenu::highlightedBackgroundColourId, colours::accent.withAlpha(0.35f));
    setColour(juce::Label::textColourId, colours::legend);
    setColour(juce::Slider::textBoxTextColourId, colours::legend);
    setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour(juce::TooltipWindow::backgroundColourId, juce::Colour(0xff111214));
    setColour(juce::TooltipWindow::textColourId, colours::legend);
}

void drawLed(juce::Graphics& g, juce::Rectangle<float> r, juce::Colour colour, bool on)
{
    if (on)
    {
        g.setColour(colour.withAlpha(0.25f));
        g.fillEllipse(r.expanded(r.getWidth() * 0.45f));
    }
    g.setGradientFill(juce::ColourGradient(on ? colour.brighter(0.6f) : colours::ledOff.brighter(0.25f), r.getCentreX(),
                                           r.getY() + r.getHeight() * 0.3f, on ? colour : colours::ledOff,
                                           r.getCentreX(), r.getBottom(), true));
    g.fillEllipse(r);
    g.setColour(juce::Colours::black.withAlpha(0.6f));
    g.drawEllipse(r, 1.0f);
}

void PanelLookAndFeel::drawRotarySlider(juce::Graphics& g, int x, int y, int w, int h, float pos, float start,
                                        float end, juce::Slider& slider)
{
    const auto bounds = juce::Rectangle<int>(x, y, w, h).toFloat();
    const float radius = juce::jmin(bounds.getWidth(), bounds.getHeight()) * 0.5f;
    const auto c = bounds.getCentre();
    const float knobR = radius * 0.72f;
    const float angle = start + pos * (end - start);

    // Scale ticks.
    g.setColour(colours::legendDim);
    for (int i = 0; i <= 10; ++i)
    {
        const float a = start + (float) i / 10.0f * (end - start);
        const auto dir = juce::Point<float>(std::sin(a), -std::cos(a));
        g.drawLine(juce::Line<float>(c + dir * (radius * 0.84f), c + dir * (radius * (i % 5 == 0 ? 1.0f : 0.94f))),
                   i % 5 == 0 ? 1.4f : 1.0f);
    }

    if (slider.getProperties()[kArc])
    {
        juce::Path arc;
        arc.addCentredArc(c.x, c.y, radius * 0.84f, radius * 0.84f, 0.0f, start, angle, true);
        g.setColour(colours::accent.withAlpha(slider.isEnabled() ? 0.9f : 0.3f));
        g.strokePath(arc, juce::PathStrokeType(2.5f));
    }

    // Shadow, skirt and cap.
    g.setColour(juce::Colours::black.withAlpha(0.45f));
    g.fillEllipse(juce::Rectangle<float>(knobR * 2.0f, knobR * 2.0f).withCentre(c.translated(0.0f, 2.5f)));
    g.setGradientFill(juce::ColourGradient(juce::Colour(0xff3d3f43), c.x, c.y - knobR, juce::Colour(0xff0f1011), c.x,
                                           c.y + knobR, false));
    g.fillEllipse(juce::Rectangle<float>(knobR * 2.0f, knobR * 2.0f).withCentre(c));
    const float capR = knobR * 0.78f;
    g.setGradientFill(juce::ColourGradient(juce::Colour(0xff2b2d30), c.x - capR * 0.4f, c.y - capR * 0.6f,
                                           juce::Colour(0xff121314), c.x + capR * 0.5f, c.y + capR, true));
    g.fillEllipse(juce::Rectangle<float>(capR * 2.0f, capR * 2.0f).withCentre(c));
    g.setColour(juce::Colours::white.withAlpha(0.08f));
    g.drawEllipse(juce::Rectangle<float>(capR * 2.0f, capR * 2.0f).withCentre(c), 1.0f);

    const auto dir = juce::Point<float>(std::sin(angle), -std::cos(angle));
    g.setColour(slider.isEnabled() ? juce::Colours::white : colours::legendDim);
    g.drawLine(juce::Line<float>(c + dir * (knobR * 0.25f), c + dir * (knobR * 0.95f)), 2.2f);
}

void PanelLookAndFeel::drawButtonBackground(juce::Graphics& g, juce::Button& b, const juce::Colour&, bool highlighted,
                                            bool down)
{
    auto r = b.getLocalBounds().toFloat().reduced(1.0f);
    g.setColour(juce::Colours::black.withAlpha(0.55f));
    g.fillRoundedRectangle(r.translated(0.0f, 1.5f), 4.0f);
    if (down)
        r = r.translated(0.0f, 1.0f);
    const auto top = colours::keyTop.brighter(highlighted ? 0.12f : 0.0f).darker(down ? 0.35f : 0.0f);
    const auto bottom = colours::keyBottom.darker(down ? 0.35f : 0.0f);
    g.setGradientFill(juce::ColourGradient(top, r.getX(), r.getY(), bottom, r.getX(), r.getBottom(), false));
    g.fillRoundedRectangle(r, 4.0f);
    g.setColour(juce::Colours::white.withAlpha(down ? 0.03f : 0.12f));
    g.drawHorizontalLine((int) r.getY() + 1, r.getX() + 3.0f, r.getRight() - 3.0f);
    g.setColour(juce::Colours::black.withAlpha(0.8f));
    g.drawRoundedRectangle(r, 4.0f, 1.0f);

    // A key with a toggle state carries its own LED (BYPASS).
    if (b.getClickingTogglesState() || b.getProperties()["led"])
        drawLed(g, juce::Rectangle<float>(7.0f, 7.0f).withCentre({ r.getRight() - 9.0f, r.getY() + 8.0f }),
                colours::ledRed, b.getToggleState());
}

void PanelLookAndFeel::drawButtonText(juce::Graphics& g, juce::TextButton& b, bool, bool down)
{
    g.setFont(panelFont(juce::jmin(13.0f, b.getHeight() * 0.42f), true));
    g.setColour(b.isEnabled() ? colours::legend : colours::legendDim);
    g.drawFittedText(b.getButtonText(), b.getLocalBounds().translated(0, down ? 1 : 0).reduced(3, 0),
                     juce::Justification::centred, 1);
}

void PanelLookAndFeel::drawToggleButton(juce::Graphics& g, juce::ToggleButton& b, bool highlighted, bool)
{
    const auto r = b.getLocalBounds().toFloat();
    const float d = juce::jmin(11.0f, r.getHeight() * 0.6f);
    drawLed(g, juce::Rectangle<float>(d, d).withCentre({ r.getX() + d * 0.5f + 2.0f, r.getCentreY() }),
            colours::accent, b.getToggleState());
    g.setColour(highlighted ? colours::legend.brighter(0.2f) : colours::legend);
    g.setFont(panelFont(12.0f, true));
    g.drawFittedText(b.getButtonText(), b.getLocalBounds().withTrimmedLeft((int) d + 8), juce::Justification::centredLeft,
                     1);
}

} // namespace glide90::ui
