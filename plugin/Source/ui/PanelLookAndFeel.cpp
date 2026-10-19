#include "PanelLookAndFeel.h"

namespace glide90::ui
{

juce::Font panelFont(float height, bool bold)
{
    return juce::Font(juce::FontOptions(height, bold ? juce::Font::bold : juce::Font::plain));
}

void drawRackUnit(juce::Graphics& g, juce::Rectangle<int> bounds)
{
    const auto r = bounds.toFloat();
    g.setGradientFill(juce::ColourGradient(colours::panelTop, 0.0f, r.getY(), colours::panelBottom, 0.0f, r.getBottom(), false));
    g.fillRect(r);
    // Brushed finish.
    for (int y = bounds.getY() + 1; y < bounds.getBottom(); y += 2)
    {
        g.setColour(juce::Colours::white.withAlpha((y / 2) % 3 == 0 ? 0.018f : 0.008f));
        g.drawHorizontalLine(y, r.getX(), r.getRight());
    }
    // Edges of the unit.
    g.setColour(juce::Colours::white.withAlpha(0.07f));
    g.drawHorizontalLine(bounds.getY(), r.getX(), r.getRight());
    g.setColour(juce::Colours::black);
    g.drawHorizontalLine(bounds.getBottom() - 1, r.getX(), r.getRight());

    // Rack ears with oval slots.
    for (float x : { r.getX(), r.getRight() - 24.0f })
    {
        g.setColour(colours::ear);
        g.fillRect(x, r.getY(), 24.0f, r.getHeight());
        g.setColour(juce::Colours::black.withAlpha(0.6f));
        g.drawVerticalLine((int) (x < r.getCentreX() ? x + 24.0f : x), r.getY(), r.getBottom());
        const float slotH = juce::jmin(22.0f, r.getHeight() * 0.18f);
        for (float y : { r.getY() + 12.0f, r.getBottom() - 12.0f - slotH })
        {
            g.setColour(juce::Colour(0xfff2f2f2));
            g.fillRoundedRectangle(x + 5.0f, y, 14.0f, slotH, 7.0f);
            g.setColour(juce::Colours::black.withAlpha(0.35f));
            g.drawRoundedRectangle(x + 5.0f, y, 14.0f, slotH, 7.0f, 1.0f);
        }
    }
}

void drawGlass(juce::Graphics& g, juce::Rectangle<float> r, float corner)
{
    g.setColour(colours::glassEdge);
    g.fillRoundedRectangle(r, corner + 1.0f);
    const auto inner = r.reduced(1.5f);
    g.setGradientFill(juce::ColourGradient(juce::Colour(0xff121715), inner.getX(), inner.getY(), colours::glass,
                                           inner.getX(), inner.getBottom(), false));
    g.fillRoundedRectangle(inner, corner);
    g.setColour(juce::Colours::white.withAlpha(0.025f));
    g.fillRoundedRectangle(inner.withHeight(inner.getHeight() * 0.45f), corner);
}

void drawKeyLed(juce::Graphics& g, juce::Point<float> c, bool on)
{
    const auto r = juce::Rectangle<float>(12.0f, 3.5f).withCentre(c);
    if (on)
    {
        g.setColour(colours::ledRed.withAlpha(0.3f));
        g.fillRoundedRectangle(r.expanded(3.0f, 2.5f), 2.0f);
    }
    g.setColour(on ? colours::ledRed : colours::ledOff);
    g.fillRoundedRectangle(r, 1.0f);
}

void drawLed(juce::Graphics& g, juce::Rectangle<float> r, juce::Colour colour, bool on)
{
    if (on)
    {
        g.setColour(colour.withAlpha(0.25f));
        g.fillRoundedRectangle(r.expanded(2.0f), 2.0f);
    }
    g.setColour(on ? colour : colour.darker(2.6f).withSaturation(0.4f));
    g.fillRoundedRectangle(r, 1.0f);
}

PanelLookAndFeel::PanelLookAndFeel()
{
    setColour(juce::ComboBox::backgroundColourId, colours::glass);
    setColour(juce::ComboBox::outlineColourId, colours::glassEdge);
    setColour(juce::ComboBox::textColourId, colours::vfd);
    setColour(juce::ComboBox::arrowColourId, colours::vfd);
    setColour(juce::PopupMenu::backgroundColourId, juce::Colour(0xff0d100f));
    setColour(juce::PopupMenu::textColourId, colours::vfd);
    setColour(juce::PopupMenu::headerTextColourId, colours::legend);
    setColour(juce::PopupMenu::highlightedBackgroundColourId, colours::vfd.withAlpha(0.2f));
    setColour(juce::PopupMenu::highlightedTextColourId, juce::Colours::white);
    setColour(juce::Label::textColourId, colours::legend);
    setColour(juce::Slider::textBoxTextColourId, colours::vfd);
    setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour(juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    setColour(juce::TextEditor::backgroundColourId, colours::glass);
    setColour(juce::TextEditor::textColourId, colours::vfd);
    setColour(juce::TextEditor::outlineColourId, colours::glassEdge);
    setColour(juce::TextEditor::highlightColourId, colours::vfd.withAlpha(0.3f));
    setColour(juce::CaretComponent::caretColourId, colours::vfd);
    setColour(juce::AlertWindow::backgroundColourId, juce::Colour(0xff161617));
    setColour(juce::AlertWindow::textColourId, colours::legend);
    setColour(juce::AlertWindow::outlineColourId, colours::glassEdge);
    setColour(juce::TooltipWindow::backgroundColourId, juce::Colour(0xff0d100f));
    setColour(juce::TooltipWindow::textColourId, colours::vfd);
    setColour(juce::TooltipWindow::outlineColourId, colours::glassEdge);
}

juce::Label* PanelLookAndFeel::createSliderTextBox(juce::Slider& slider)
{
    // The value in green behind a small smoked window.
    auto* l = LookAndFeel_V4::createSliderTextBox(slider);
    l->setFont(panelFont(13.0f));
    l->setColour(juce::Label::outlineColourId, colours::glassEdge);
    l->setColour(juce::Label::backgroundColourId, colours::glass);
    l->setColour(juce::Label::textColourId, colours::vfd);
    l->setColour(juce::Label::outlineWhenEditingColourId, colours::vfd);
    l->setColour(juce::TextEditor::textColourId, colours::vfd);
    l->setColour(juce::TextEditor::backgroundColourId, colours::glass);
    return l;
}

void PanelLookAndFeel::drawRotarySlider(juce::Graphics& g, int x, int y, int w, int h, float pos, float start,
                                        float end, juce::Slider& slider)
{
    // A small black knurled knob with a white line, and a ring of tick marks, like INPUT on the unit.
    const auto bounds = juce::Rectangle<int>(x, y, w, h).toFloat();
    const float radius = juce::jmin(bounds.getWidth(), bounds.getHeight()) * 0.5f - 1.0f;
    const auto c = bounds.getCentre();
    const float angle = start + pos * (end - start);
    const float alpha = slider.isEnabled() ? 1.0f : 0.35f;

    g.setColour(colours::legend.withAlpha(0.8f * alpha));
    for (int i = 0; i <= 10; ++i)
    {
        const float a = start + (float) i / 10.0f * (end - start);
        const auto dir = juce::Point<float>(std::sin(a), -std::cos(a));
        const bool major = i == 0 || i == 5 || i == 10;
        g.drawLine(juce::Line<float>(c + dir * (radius * (major ? 0.8f : 0.86f)), c + dir * radius), major ? 1.6f : 1.1f);
    }

    const float knobR = radius * 0.68f;
    // Knurled skirt.
    juce::Path skirt;
    constexpr int teeth = 28;
    for (int i = 0; i < teeth * 2; ++i)
    {
        const float a = juce::MathConstants<float>::twoPi * (float) i / (teeth * 2);
        const float rr = i % 2 == 0 ? knobR : knobR * 0.94f;
        const auto pt = c + juce::Point<float>(std::sin(a), -std::cos(a)) * rr;
        if (i == 0)
            skirt.startNewSubPath(pt);
        else
            skirt.lineTo(pt);
    }
    skirt.closeSubPath();
    g.setColour(juce::Colours::black.withAlpha(0.6f * alpha));
    g.fillEllipse(juce::Rectangle<float>(knobR * 2.0f, knobR * 2.0f).withCentre(c.translated(0.0f, 2.5f)));
    g.setGradientFill(juce::ColourGradient(juce::Colour(0xff3a3a3b).withAlpha(alpha), c.x, c.y - knobR,
                                           juce::Colour(0xff0b0b0b).withAlpha(alpha), c.x, c.y + knobR, false));
    g.fillPath(skirt);

    // Cap.
    const float capR = knobR * 0.72f;
    const auto cap = juce::Rectangle<float>(capR * 2.0f, capR * 2.0f).withCentre(c);
    g.setGradientFill(juce::ColourGradient(juce::Colour(0xff2c2c2d).withAlpha(alpha), c.x - capR * 0.5f, c.y - capR,
                                           juce::Colour(0xff111112).withAlpha(alpha), c.x + capR * 0.5f, c.y + capR, false));
    g.fillEllipse(cap);
    g.setColour(juce::Colours::white.withAlpha(0.12f * alpha));
    g.drawEllipse(cap, 1.0f);

    const auto dir = juce::Point<float>(std::sin(angle), -std::cos(angle));
    g.setColour(juce::Colours::white.withAlpha(alpha));
    g.drawLine(juce::Line<float>(c + dir * (knobR * 0.2f), c + dir * (knobR * 0.98f)), 2.4f);
}

void PanelLookAndFeel::drawButtonBackground(juce::Graphics& g, juce::Button& b, const juce::Colour&, bool highlighted,
                                            bool down)
{
    // A square black key standing out of the panel.
    auto r = b.getLocalBounds().toFloat().reduced(1.0f);
    g.setColour(juce::Colours::black);
    g.fillRoundedRectangle(r.translated(0.0f, 1.5f), 2.5f);
    if (down)
        r = r.translated(0.0f, 1.0f);
    g.setGradientFill(juce::ColourGradient(juce::Colour(down ? 0xff141415 : 0xff232324).brighter(highlighted ? 0.12f : 0.0f),
                                           r.getX(), r.getY(), juce::Colour(0xff0c0c0d), r.getX(), r.getBottom(), false));
    g.fillRoundedRectangle(r, 2.5f);
    g.setColour(juce::Colours::white.withAlpha(down ? 0.05f : 0.14f));
    g.drawLine(r.getX() + 2.0f, r.getY() + 0.5f, r.getRight() - 2.0f, r.getY() + 0.5f, 1.0f);
    g.setColour(juce::Colours::white.withAlpha(0.05f));
    g.drawRoundedRectangle(r.reduced(3.0f), 2.0f, 1.0f);
    g.setColour(juce::Colours::black);
    g.drawRoundedRectangle(r, 2.5f, 1.0f);

    if (b.getProperties()[kLed])
        drawKeyLed(g, { r.getCentreX(), r.getY() + 7.0f }, b.getToggleState());
}

void PanelLookAndFeel::drawButtonText(juce::Graphics& g, juce::TextButton& b, bool, bool down)
{
    const auto r = b.getLocalBounds().toFloat().translated(0.0f, down ? 1.0f : 0.0f);
    const int arrow = b.getProperties()[kArrow];
    g.setColour(b.isEnabled() ? colours::legend : colours::legendDim);
    if (arrow != 0)
    {
        // Hollow triangle, as on the unit's data keys.
        const float s = juce::jmin(r.getWidth(), r.getHeight()) * 0.5f;
        const auto c = r.getCentre();
        juce::Path t;
        const float dir = arrow > 0 ? -1.0f : 1.0f;
        t.addTriangle(c.x - s * 0.55f, c.y - dir * s * 0.4f, c.x + s * 0.55f, c.y - dir * s * 0.4f, c.x, c.y + dir * s * 0.5f);
        g.strokePath(t, juce::PathStrokeType(1.6f, juce::PathStrokeType::mitered));
        return;
    }
    const bool hasLed = b.getProperties()[kLed];
    g.setFont(panelFont(juce::jmin(11.5f, b.getHeight() * 0.36f)));
    g.drawFittedText(b.getButtonText(), r.toNearestInt().reduced(3, 2).withTrimmedTop(hasLed ? 8 : 0),
                     juce::Justification::centred, 2, 0.9f);
}

void PanelLookAndFeel::drawComboBox(juce::Graphics& g, int width, int height, bool, int, int, int, int, juce::ComboBox& box)
{
    drawGlass(g, juce::Rectangle<float>(0.0f, 0.0f, (float) width, (float) height), 2.5f);
    const float s = 5.0f;
    const auto c = juce::Point<float>((float) width - 14.0f, (float) height * 0.5f);
    juce::Path arrow;
    arrow.addTriangle(c.x - s, c.y - s * 0.5f, c.x + s, c.y - s * 0.5f, c.x, c.y + s * 0.6f);
    g.setColour(box.findColour(juce::ComboBox::arrowColourId).withAlpha(box.isEnabled() ? 0.9f : 0.3f));
    g.fillPath(arrow);
}

} // namespace glide90::ui
