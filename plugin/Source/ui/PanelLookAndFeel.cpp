#include "PanelLookAndFeel.h"

#include "GlideFonts.h"
#include "LcdFont.h"

namespace glide90::ui
{
namespace
{

juce::Typeface::Ptr typeface(const char* data, int size)
{
    return juce::Typeface::createSystemTypefaceFor(data, (size_t) size);
}

} // namespace

juce::Font legendFont(float height, bool semiBold)
{
    static const auto medium = typeface(GlideFonts::BarlowCondensedMedium_ttf, GlideFonts::BarlowCondensedMedium_ttfSize);
    static const auto semi = typeface(GlideFonts::BarlowCondensedSemiBold_ttf, GlideFonts::BarlowCondensedSemiBold_ttfSize);
    return juce::Font(juce::FontOptions(semiBold ? semi : medium).withHeight(height));
}

juce::Font segmentFont(float height)
{
    static const auto dseg = typeface(GlideFonts::DSEG7ClassicBold_ttf, GlideFonts::DSEG7ClassicBold_ttfSize);
    return juce::Font(juce::FontOptions(dseg).withHeight(height));
}

float matrixTextWidth(const juce::String& text, float pitch)
{
    return text.isEmpty() ? 0.0f : ((float) text.length() * 6.0f - 1.0f) * pitch;
}

float drawMatrixText(juce::Graphics& g, const juce::String& text, juce::Rectangle<float> area, float pitch,
                     juce::Colour colour, juce::Justification justification)
{
    const float width = matrixTextWidth(text, pitch);
    float x = area.getX();
    if (justification.testFlags(juce::Justification::horizontallyCentred))
        x = area.getCentreX() - width * 0.5f;
    else if (justification.testFlags(juce::Justification::right))
        x = area.getRight() - width;
    const float y = area.getCentreY() - 3.5f * pitch;
    const float dot = pitch * 0.8f;

    g.setColour(colour);
    for (int i = 0; i < text.length(); ++i)
    {
        const auto ch = text[i];
        const auto& glyph = kLcdFont[ch >= 32 && ch <= 126 ? (int) ch - 32 : '?' - 32];
        for (int row = 0; row < 7; ++row)
            for (int col = 0; col < 5; ++col)
                if ((glyph[row] >> (4 - col)) & 1)
                    g.fillRect(x + (float) (i * 6 + col) * pitch, y + (float) row * pitch, dot, dot);
    }
    return width;
}

void drawRackUnit(juce::Graphics& g, juce::Rectangle<int> bounds)
{
    const auto r = bounds.toFloat();
    g.setGradientFill(juce::ColourGradient(colours::anodized, 0.0f, r.getY(), colours::anodizedDark, 0.0f, r.getBottom(), false));
    g.fillRect(r);
    // Fine vertical grain, deterministic so it does not shimmer between repaints.
    juce::Random grain(1234);
    for (int x = bounds.getX(); x < bounds.getRight(); x += 2)
    {
        g.setColour(juce::Colours::white.withAlpha(0.004f + 0.01f * grain.nextFloat()));
        g.drawVerticalLine(x, r.getY(), r.getBottom());
    }
    // Engraved edges between units.
    g.setColour(juce::Colours::white.withAlpha(0.06f));
    g.drawHorizontalLine(bounds.getY(), r.getX(), r.getRight());
    g.setColour(juce::Colours::black);
    g.drawHorizontalLine(bounds.getBottom() - 1, r.getX(), r.getRight());
}

void drawUnitName(juce::Graphics& g, const juce::String& name, juce::Rectangle<int> unit)
{
    g.setFont(legendFont(11.5f, true).withExtraKerningFactor(0.12f));
    g.setColour(colours::silkscreenDim);
    g.drawText(name, unit.getX() + PanelLookAndFeel::kMargin, unit.getY() + 5, 200, 14, juce::Justification::centredLeft);
}

void drawEngravedLine(juce::Graphics& g, float x, float top, float bottom)
{
    g.setColour(juce::Colours::black.withAlpha(0.8f));
    g.drawVerticalLine((int) x, top, bottom);
    g.setColour(juce::Colours::white.withAlpha(0.05f));
    g.drawVerticalLine((int) x + 1, top, bottom);
}

void drawEngravedRule(juce::Graphics& g, float y, float left, float right)
{
    g.setColour(juce::Colours::black.withAlpha(0.8f));
    g.drawHorizontalLine((int) y, left, right);
    g.setColour(juce::Colours::white.withAlpha(0.05f));
    g.drawHorizontalLine((int) y + 1, left, right);
}

void drawGlass(juce::Graphics& g, juce::Rectangle<float> r, float corner)
{
    g.setColour(juce::Colours::black);
    g.fillRoundedRectangle(r, corner + 1.0f);
    const auto inner = r.reduced(1.0f);
    g.setGradientFill(juce::ColourGradient(juce::Colour(0xff111816), inner.getX(), inner.getY(), colours::glass,
                                           inner.getX(), inner.getBottom(), false));
    g.fillRoundedRectangle(inner, corner);
    g.setColour(colours::glassEdge);
    g.drawRoundedRectangle(inner, corner, 1.0f);
}

void drawKeyLed(juce::Graphics& g, juce::Point<float> c, bool on)
{
    const auto r = juce::Rectangle<float>(10.0f, 3.0f).withCentre(c);
    if (on)
    {
        g.setColour(colours::ledRed.withAlpha(0.22f));
        g.fillRoundedRectangle(r.expanded(2.5f, 2.0f), 2.0f);
    }
    g.setColour(on ? colours::ledRed : colours::ledOff);
    g.fillRoundedRectangle(r, 0.8f);
}

void drawLed(juce::Graphics& g, juce::Rectangle<float> r, juce::Colour colour, bool on)
{
    g.setColour(on ? colour : colour.darker(2.8f).withSaturation(0.35f));
    g.fillRect(r);
}

void drawKnob(juce::Graphics& g, juce::Rectangle<float> area, float position, bool enabled, bool large, bool scale)
{
    constexpr float start = -2.36f, end = 2.36f; // +-135 degrees
    const float radius = juce::jmin(area.getWidth(), area.getHeight()) * 0.5f;
    const auto c = area.getCentre();
    const float alpha = enabled ? 1.0f : 0.4f;

    // Scale: dots, with longer marks at the ends and the middle.
    for (int i = 0; scale && i <= 10; ++i)
    {
        const float a = start + (float) i / 10.0f * (end - start);
        const auto dir = juce::Point<float>(std::sin(a), -std::cos(a));
        const bool major = i == 0 || i == 5 || i == 10;
        g.setColour(colours::silkscreen.withAlpha((major ? 0.85f : 0.5f) * alpha));
        if (major && large)
            g.drawLine(juce::Line<float>(c + dir * (radius * 0.86f), c + dir * radius), 1.2f);
        else
            g.fillEllipse(juce::Rectangle<float>(1.6f, 1.6f).withCentre(c + dir * (radius * 0.93f)));
    }

    const float knobR = radius * 0.74f;
    // Shadow and knurled skirt.
    g.setColour(juce::Colours::black.withAlpha(0.55f * alpha));
    g.fillEllipse(juce::Rectangle<float>(knobR * 2.0f, knobR * 2.0f).withCentre(c.translated(0.0f, 1.5f)).expanded(1.0f));
    juce::Path skirt;
    const int teeth = large ? 44 : 32;
    for (int i = 0; i < teeth * 2; ++i)
    {
        const float a = juce::MathConstants<float>::twoPi * (float) i / (float) (teeth * 2);
        const auto pt = c + juce::Point<float>(std::sin(a), -std::cos(a)) * (i % 2 == 0 ? knobR : knobR * 0.965f);
        if (i == 0)
            skirt.startNewSubPath(pt);
        else
            skirt.lineTo(pt);
    }
    skirt.closeSubPath();
    g.setGradientFill(juce::ColourGradient(juce::Colour(0xff2e2f31).withAlpha(alpha), c.x, c.y - knobR,
                                           juce::Colour(0xff0a0a0b).withAlpha(alpha), c.x, c.y + knobR, false));
    g.fillPath(skirt);

    // Machined cap: a soft conical highlight.
    const float capR = knobR * 0.8f;
    const auto cap = juce::Rectangle<float>(capR * 2.0f, capR * 2.0f).withCentre(c);
    g.setGradientFill(juce::ColourGradient(juce::Colour(0xff28292b).withAlpha(alpha), c.x, c.y - capR,
                                           juce::Colour(0xff121214).withAlpha(alpha), c.x, c.y + capR, false));
    g.fillEllipse(cap);
    g.setColour(juce::Colours::white.withAlpha(0.07f * alpha));
    g.drawEllipse(cap.reduced(0.5f), 0.8f);

    // Printed line.
    const float angle = start + juce::jlimit(0.0f, 1.0f, position) * (end - start);
    const auto dir = juce::Point<float>(std::sin(angle), -std::cos(angle));
    g.setColour(juce::Colour(0xffe8e8e2).withAlpha(alpha));
    g.drawLine(juce::Line<float>(c + dir * (capR * 0.35f), c + dir * (knobR * 0.94f)), large ? 1.8f : 1.5f);
}

PanelLookAndFeel::PanelLookAndFeel()
{
    setColour(juce::PopupMenu::backgroundColourId, juce::Colour(0xff141516));
    setColour(juce::PopupMenu::textColourId, colours::silkscreen);
    setColour(juce::PopupMenu::headerTextColourId, colours::silkscreenDim);
    setColour(juce::PopupMenu::highlightedBackgroundColourId, juce::Colour(0xff26282b));
    setColour(juce::PopupMenu::highlightedTextColourId, juce::Colours::white);
    setColour(juce::TextEditor::backgroundColourId, colours::glass);
    setColour(juce::TextEditor::textColourId, colours::display);
    setColour(juce::TextEditor::outlineColourId, colours::glassEdge);
    setColour(juce::TextEditor::focusedOutlineColourId, colours::displayDim);
    setColour(juce::TextEditor::highlightColourId, colours::display.withAlpha(0.3f));
    setColour(juce::CaretComponent::caretColourId, colours::display);
    setColour(juce::AlertWindow::backgroundColourId, juce::Colour(0xff17181a));
    setColour(juce::AlertWindow::textColourId, colours::silkscreen);
    setColour(juce::AlertWindow::outlineColourId, colours::glassEdge);
    setColour(juce::TooltipWindow::backgroundColourId, juce::Colour(0xff141516));
    setColour(juce::TooltipWindow::textColourId, colours::silkscreen);
    setColour(juce::TooltipWindow::outlineColourId, colours::glassEdge);
    setColour(juce::TextButton::textColourOffId, colours::silkscreen);
}

void PanelLookAndFeel::drawPopupMenuBackground(juce::Graphics& g, int width, int height)
{
    g.fillAll(colours::glass);
    g.setColour(colours::glassEdge);
    g.drawRect(0, 0, width, height, 1);
}

void PanelLookAndFeel::getIdealPopupMenuItemSize(const juce::String& text, bool isSeparator, int, int& width, int& height)
{
    height = isSeparator ? 9 : 24;
    width = (int) matrixTextWidth(text.toUpperCase(), kSmallTextPitch) + 52;
}

void PanelLookAndFeel::drawPopupMenuItem(juce::Graphics& g, const juce::Rectangle<int>& area, bool isSeparator,
                                         bool isActive, bool isHighlighted, bool isTicked, bool, const juce::String& text,
                                         const juce::String&, const juce::Drawable*, const juce::Colour*)
{
    const auto r = area.toFloat();
    if (isSeparator)
    {
        drawEngravedRule(g, r.getCentreY(), r.getX() + 8.0f, r.getRight() - 8.0f);
        return;
    }
    if (isHighlighted && isActive)
    {
        g.setColour(colours::display.withAlpha(0.12f));
        g.fillRect(r.reduced(2.0f, 1.0f));
    }
    // The current choice: a lit segment on the left, as on the unit's LEDs.
    if (isTicked)
    {
        g.setColour(colours::display);
        g.fillRect(juce::Rectangle<float>(3.0f, r.getHeight() - 10.0f).withPosition(r.getX() + 9.0f, r.getY() + 5.0f));
    }
    const auto colour = !isActive ? colours::display.withAlpha(0.25f)
                                  : (isHighlighted ? juce::Colour(0xffd8f5e6) : colours::display.withAlpha(isTicked ? 1.0f : 0.8f));
    drawMatrixText(g, text.toUpperCase(), r.withTrimmedLeft(22.0f).withTrimmedRight(8.0f), kSmallTextPitch, colour);
}

void PanelLookAndFeel::drawPopupMenuSectionHeader(juce::Graphics& g, const juce::Rectangle<int>& area, const juce::String& name)
{
    const auto r = area.toFloat();
    g.setFont(legendFont(12.0f, true).withExtraKerningFactor(0.12f));
    g.setColour(colours::silkscreenDim);
    g.drawText(name.toUpperCase(), r.withTrimmedLeft(12.0f).withTrimmedTop(4.0f), juce::Justification::centredLeft);
}

void PanelLookAndFeel::drawButtonBackground(juce::Graphics& g, juce::Button& b, const juce::Colour&, bool highlighted,
                                            bool down)
{
    // A flat square key: dark face, a hairline highlight on the top edge, a black outline.
    auto r = b.getLocalBounds().toFloat().reduced(0.5f);
    g.setColour(juce::Colours::black);
    g.fillRoundedRectangle(r, 1.5f);
    const auto face = r.reduced(1.0f).translated(0.0f, down ? 0.5f : 0.0f);
    g.setGradientFill(juce::ColourGradient(juce::Colour(down ? 0xff151617 : 0xff212224).brighter(highlighted ? 0.08f : 0.0f),
                                           face.getX(), face.getY(), juce::Colour(0xff151617), face.getX(), face.getBottom(), false));
    g.fillRoundedRectangle(face, 1.0f);
    g.setColour(juce::Colours::white.withAlpha(down ? 0.03f : 0.09f));
    g.drawHorizontalLine((int) face.getY(), face.getX() + 1.0f, face.getRight() - 1.0f);

    if (b.getProperties()[kLed])
        drawKeyLed(g, { face.getCentreX(), face.getY() + 6.0f }, b.getToggleState());
}

void PanelLookAndFeel::drawButtonText(juce::Graphics& g, juce::TextButton& b, bool, bool down)
{
    const auto r = b.getLocalBounds().toFloat().translated(0.0f, down ? 0.5f : 0.0f);
    const int arrow = b.getProperties()[kArrow];
    g.setColour(b.isEnabled() ? colours::silkscreen : colours::silkscreenDim);
    if (arrow != 0)
    {
        // Hollow triangle, as on the unit's data keys.
        const float s = juce::jmin(r.getWidth(), r.getHeight()) * 0.42f;
        const auto c = r.getCentre();
        const float dir = arrow > 0 ? -1.0f : 1.0f;
        juce::Path t;
        t.addTriangle(c.x - s * 0.6f, c.y - dir * s * 0.42f, c.x + s * 0.6f, c.y - dir * s * 0.42f, c.x, c.y + dir * s * 0.55f);
        g.strokePath(t, juce::PathStrokeType(1.3f, juce::PathStrokeType::mitered));
        return;
    }
    const bool hasLed = b.getProperties()[kLed];
    g.setFont(legendFont(12.5f));
    g.drawFittedText(b.getButtonText(), r.toNearestInt().reduced(3, 2).withTrimmedTop(hasLed ? 9 : 0),
                     juce::Justification::centred, 2, 1.0f);
}

} // namespace glide90::ui
