#include "PanelLookAndFeel.h"

namespace glide90::ui
{

juce::Font panelFont(float height, bool bold)
{
    return juce::Font(juce::FontOptions(height, bold ? juce::Font::bold : juce::Font::plain));
}

PanelLookAndFeel::PanelLookAndFeel()
{
    setColour(juce::ComboBox::backgroundColourId, juce::Colour(0xff0d0e10));
    setColour(juce::ComboBox::outlineColourId, juce::Colour(0xff3c3f45));
    setColour(juce::ComboBox::textColourId, colours::legend);
    setColour(juce::ComboBox::arrowColourId, colours::spxAccent);
    setColour(juce::PopupMenu::backgroundColourId, juce::Colour(0xff17181b));
    setColour(juce::PopupMenu::textColourId, colours::legend);
    setColour(juce::PopupMenu::headerTextColourId, colours::spxAccent);
    setColour(juce::PopupMenu::highlightedBackgroundColourId, colours::spxAccent.withAlpha(0.3f));
    setColour(juce::PopupMenu::highlightedTextColourId, juce::Colours::white);
    setColour(juce::Label::textColourId, colours::legend);
    setColour(juce::Slider::textBoxTextColourId, colours::legend);
    setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour(juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    setColour(juce::TextEditor::backgroundColourId, juce::Colour(0xff0d0e10));
    setColour(juce::TextEditor::textColourId, colours::legend);
    setColour(juce::TextEditor::outlineColourId, juce::Colour(0xff3c3f45));
    setColour(juce::AlertWindow::backgroundColourId, juce::Colour(0xff1b1c1f));
    setColour(juce::AlertWindow::textColourId, colours::legend);
    setColour(juce::TooltipWindow::backgroundColourId, juce::Colour(0xff111214));
    setColour(juce::TooltipWindow::textColourId, colours::legend);
}

juce::Label* PanelLookAndFeel::createSliderTextBox(juce::Slider& slider)
{
    auto* l = LookAndFeel_V4::createSliderTextBox(slider);
    l->setFont(panelFont(12.5f, true));
    l->setColour(juce::Label::outlineColourId, juce::Colours::transparentBlack);
    l->setColour(juce::Label::backgroundColourId, juce::Colours::transparentBlack);
    l->setColour(juce::Label::textColourId, colours::legend);
    return l;
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
    const float radius = juce::jmin(bounds.getWidth(), bounds.getHeight()) * 0.5f - 2.0f;
    const auto c = bounds.getCentre();
    const float angle = start + pos * (end - start);
    const float alpha = slider.isEnabled() ? 1.0f : 0.35f;
    const auto& props = slider.getProperties();
    const auto accent = props.contains(kAccent) ? juce::Colour((juce::uint32) (juce::int64) props[kAccent])
                                                : colours::spxAccent;

    // Groove and value arc.
    const float arcR = radius * 0.9f;
    juce::Path groove;
    groove.addCentredArc(c.x, c.y, arcR, arcR, 0.0f, start, end, true);
    g.setColour(juce::Colours::black.withAlpha(0.55f * alpha));
    g.strokePath(groove, juce::PathStrokeType(4.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    const float from = props[kBipolar] ? 0.5f * (start + end) : start;
    if (std::abs(angle - from) > 0.01f)
    {
        juce::Path arc;
        arc.addCentredArc(c.x, c.y, arcR, arcR, 0.0f, juce::jmin(from, angle), juce::jmax(from, angle), true);
        g.setColour(accent.withAlpha(alpha));
        g.strokePath(arc, juce::PathStrokeType(4.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    // Body: a cream cap with a dark rim and a soft shadow.
    const float knobR = radius * 0.7f;
    const auto body = juce::Rectangle<float>(knobR * 2.0f, knobR * 2.0f).withCentre(c);
    g.setColour(juce::Colours::black.withAlpha(0.5f * alpha));
    g.fillEllipse(body.translated(0.0f, 3.0f).expanded(1.0f));
    g.setColour(juce::Colour(0xff201c18).withAlpha(alpha));
    g.fillEllipse(body.expanded(1.5f));
    g.setGradientFill(juce::ColourGradient(juce::Colour(0xfff7f0e0).withAlpha(alpha), c.x - knobR * 0.4f,
                                           c.y - knobR * 0.6f, juce::Colour(0xffbfb193).withAlpha(alpha),
                                           c.x + knobR * 0.6f, c.y + knobR, true));
    g.fillEllipse(body);
    g.setColour(juce::Colours::black.withAlpha(0.12f * alpha));
    g.drawEllipse(body.reduced(knobR * 0.18f), 1.0f);

    const auto dir = juce::Point<float>(std::sin(angle), -std::cos(angle));
    g.setColour(juce::Colour(0xff2a2420).withAlpha(alpha));
    g.drawLine(juce::Line<float>(c + dir * (knobR * 0.3f), c + dir * (knobR * 0.9f)), 3.0f);
}

void PanelLookAndFeel::drawButtonBackground(juce::Graphics& g, juce::Button& b, const juce::Colour&, bool highlighted,
                                            bool down)
{
    auto r = b.getLocalBounds().toFloat().reduced(1.0f);
    g.setColour(juce::Colours::black.withAlpha(0.6f));
    g.fillRoundedRectangle(r.translated(0.0f, 1.5f), 3.0f);
    if (down)
        r = r.translated(0.0f, 1.0f);
    const auto top = colours::keyTop.brighter(highlighted ? 0.15f : 0.0f).darker(down ? 0.35f : 0.0f);
    const auto bottom = colours::keyBottom.darker(down ? 0.35f : 0.0f);
    g.setGradientFill(juce::ColourGradient(top, r.getX(), r.getY(), bottom, r.getX(), r.getBottom(), false));
    g.fillRoundedRectangle(r, 3.0f);
    g.setColour(juce::Colours::white.withAlpha(down ? 0.03f : 0.1f));
    g.drawHorizontalLine((int) r.getY() + 1, r.getX() + 3.0f, r.getRight() - 3.0f);
    g.setColour(juce::Colours::black.withAlpha(0.85f));
    g.drawRoundedRectangle(r, 3.0f, 1.0f);

    // A key with a toggle state carries its own LED (BYPASS, level switches).
    if (b.getProperties()["led"])
        drawLed(g, juce::Rectangle<float>(6.0f, 6.0f).withCentre({ r.getRight() - 8.0f, r.getY() + 7.0f }),
                colours::ledRed, b.getToggleState());
}

void PanelLookAndFeel::drawButtonText(juce::Graphics& g, juce::TextButton& b, bool, bool down)
{
    g.setFont(panelFont(juce::jmin(12.0f, b.getHeight() * 0.45f), true));
    g.setColour(b.isEnabled() ? colours::legend : colours::legendDim);
    g.drawFittedText(b.getButtonText(), b.getLocalBounds().translated(0, down ? 1 : 0).reduced(3, 0),
                     juce::Justification::centred, 1);
}

void PanelLookAndFeel::drawToggleButton(juce::Graphics& g, juce::ToggleButton& b, bool highlighted, bool)
{
    const auto r = b.getLocalBounds().toFloat();
    const float d = juce::jmin(14.0f, r.getHeight() * 0.7f);
    const auto accent = b.getProperties().contains(kAccent)
                            ? juce::Colour((juce::uint32) (juce::int64) b.getProperties()[kAccent])
                            : colours::spxAccent;
    drawLed(g, juce::Rectangle<float>(d, d).withCentre({ r.getX() + d * 0.5f + 2.0f, r.getCentreY() }), accent,
            b.getToggleState());
    g.setColour(highlighted ? colours::legend.brighter(0.2f) : colours::legend);
    g.setFont(panelFont(13.0f, true));
    g.drawFittedText(b.getButtonText(), b.getLocalBounds().withTrimmedLeft((int) d + 10),
                     juce::Justification::centredLeft, 1);
}

} // namespace glide90::ui
