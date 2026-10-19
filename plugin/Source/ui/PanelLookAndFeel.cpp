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

// The panel's fine grain: light and dark specks, 2 pixels per point so it stays sharp when zoomed.
// Transparent, so it goes over the panel's colour. Built once, from a fixed seed so it is the same
// on every repaint.
const juce::Image& grain()
{
    static const juce::Image image = [] {
        constexpr int w = 1024, h = 512;
        juce::Image im(juce::Image::ARGB, w, h, true);
        const juce::Image::BitmapData data(im, juce::Image::BitmapData::writeOnly);
        juce::Random random(1234);
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x)
            {
                const float v = random.nextFloat() * 2.0f - 1.0f;
                data.setPixelColour(x, y, (v > 0.0f ? juce::Colours::white : juce::Colours::black).withAlpha(std::abs(v)));
            }
        return im;
    }();
    return image;
}

} // namespace

juce::Font legendFont(float height, bool semiBold)
{
    static const auto medium = typeface(GlideFonts::BarlowCondensedMedium_ttf, GlideFonts::BarlowCondensedMedium_ttfSize);
    static const auto semi = typeface(GlideFonts::BarlowCondensedSemiBold_ttf, GlideFonts::BarlowCondensedSemiBold_ttfSize);
    return juce::Font(juce::FontOptions(semiBold ? semi : medium).withHeight(height));
}

juce::Font keyFont(float height)
{
    static const auto regular = typeface(GlideFonts::LiberationSansRegular_ttf, GlideFonts::LiberationSansRegular_ttfSize);
    return juce::Font(juce::FontOptions(regular).withHeight(height));
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

void fillPanel(juce::Graphics& g, juce::Rectangle<float> area)
{
    g.setColour(colours::panel);
    g.fillRect(area);
    g.setFillType(juce::FillType(grain(), juce::AffineTransform::scale(0.5f).translated(area.getTopLeft())));
    g.setOpacity(0.06f);
    g.fillRect(area);
}

void drawRackUnit(juce::Graphics& g, juce::Rectangle<int> bounds)
{
    const auto r = bounds.toFloat();
    fillPanel(g, r);
    // Light from above.
    g.setGradientFill(juce::ColourGradient(juce::Colours::white.withAlpha(0.03f), 0.0f, r.getY(),
                                           juce::Colours::black.withAlpha(0.12f), 0.0f, r.getBottom(), false));
    g.fillRect(r);
    // Engraved edges between units.
    g.setColour(juce::Colours::white.withAlpha(0.07f));
    g.drawHorizontalLine(bounds.getY(), r.getX(), r.getRight());
    g.setColour(juce::Colours::black.withAlpha(0.8f));
    g.drawHorizontalLine(bounds.getBottom() - 1, r.getX(), r.getRight());
}

void drawRackSlot(juce::Graphics& g, juce::Rectangle<float> r)
{
    const float corner = r.getHeight() * 0.5f;
    // The lit lower lip, then the hole with the dark rail behind it.
    g.setColour(juce::Colours::white.withAlpha(0.1f));
    g.fillRoundedRectangle(r.translated(0.0f, 1.0f), corner);
    g.setGradientFill(juce::ColourGradient(juce::Colour(0xff020202), 0.0f, r.getY(), juce::Colour(0xff151618), 0.0f,
                                           r.getBottom(), false));
    g.fillRoundedRectangle(r, corner);
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

void drawGlass(juce::Graphics& g, juce::Rectangle<float> r, float corner, juce::Colour fill)
{
    g.setColour(juce::Colours::black);
    g.fillRoundedRectangle(r, corner + 1.0f);
    const auto inner = r.reduced(1.0f);
    g.setGradientFill(juce::ColourGradient(fill.brighter(0.12f), inner.getX(), inner.getY(), fill, inner.getX(),
                                           inner.getBottom(), false));
    g.fillRoundedRectangle(inner, corner);
    g.setColour(colours::glassEdge);
    g.drawRoundedRectangle(inner, corner, 1.0f);
}

void drawKeyLed(juce::Graphics& g, juce::Point<float> c, bool on, float width)
{
    const auto r = juce::Rectangle<float>(width, juce::jmax(3.0f, width * 0.27f)).withCentre(c);
    if (on)
    {
        g.setColour(colours::ledRed.withAlpha(0.2f));
        g.fillRoundedRectangle(r.expanded(2.0f), 2.0f);
    }
    g.setColour(on ? colours::ledRed : colours::ledOff);
    g.fillRect(r);
    g.setColour(juce::Colours::black.withAlpha(0.35f));
    g.drawRect(r, 0.6f);
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
    auto at = [c](float angle, float distance) { return c + juce::Point<float>(std::sin(angle), -std::cos(angle)) * distance; };

    // Scale: printed ticks, longer at the ends and the middle.
    for (int i = 0; scale && i <= 10; ++i)
    {
        const float a = start + (float) i / 10.0f * (end - start);
        const bool major = i == 0 || i == 5 || i == 10;
        g.setColour(colours::silkscreen.withAlpha((major ? 0.9f : 0.6f) * alpha));
        g.drawLine(juce::Line<float>(at(a, radius * (major ? 0.82f : 0.86f)), at(a, radius * 0.98f)), major && large ? 1.4f : 1.1f);
    }

    // Body: smooth matte black, its rim lit from above, with a shadow on the panel.
    const float knobR = radius * 0.74f;
    const auto body = juce::Rectangle<float>(knobR * 2.0f, knobR * 2.0f).withCentre(c);
    g.setColour(juce::Colours::black.withAlpha(0.6f * alpha));
    g.fillEllipse(body.translated(0.0f, 1.8f).expanded(1.2f));
    g.setGradientFill(juce::ColourGradient(juce::Colour(0xff36373a).withAlpha(alpha), c.x, body.getY(),
                                           juce::Colour(0xff09090a).withAlpha(alpha), c.x, body.getBottom(), false));
    g.fillEllipse(body);

    // The flat top.
    const float topR = knobR * 0.86f;
    const auto top = juce::Rectangle<float>(topR * 2.0f, topR * 2.0f).withCentre(c);
    g.setGradientFill(juce::ColourGradient(juce::Colour(0xff1f2022).withAlpha(alpha), c.x, top.getY(),
                                           juce::Colour(0xff151517).withAlpha(alpha), c.x, top.getBottom(), false));
    g.fillEllipse(top);

    // The grip bar across it, along the pointer.
    const float angle = start + juce::jlimit(0.0f, 1.0f, position) * (end - start);
    const float barW = knobR * 0.36f, barL = topR * 1.96f;
    juce::Path bar;
    bar.addRoundedRectangle(-barW * 0.5f, -barL * 0.5f, barW, barL, barW * 0.45f);
    bar.applyTransform(juce::AffineTransform::rotation(angle).translated(c));
    g.setColour(juce::Colours::black.withAlpha(0.45f * alpha));
    g.fillPath(bar, juce::AffineTransform::translation(0.0f, 1.2f));
    g.setGradientFill(juce::ColourGradient(juce::Colour(0xff2d2e31).withAlpha(alpha), c.x, top.getY(),
                                           juce::Colour(0xff121214).withAlpha(alpha), c.x, top.getBottom(), false));
    g.fillPath(bar);
    g.setColour(juce::Colours::white.withAlpha(0.08f * alpha));
    g.strokePath(bar, juce::PathStrokeType(0.8f));

    // The white line at the pointing end.
    juce::Path line;
    line.startNewSubPath(at(angle, topR * 0.3f));
    line.lineTo(at(angle, topR * 0.9f));
    g.setColour(juce::Colour(0xffecece6).withAlpha(alpha));
    g.strokePath(line, juce::PathStrokeType(large ? 2.0f : 1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
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
                                  : (isHighlighted ? juce::Colour(0xffdde9c6) : colours::display.withAlpha(isTicked ? 1.0f : 0.8f));
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
    // A raised key as on the unit: a matte cap with a soft shadow on the panel, down and to the
    // right; pressed, it goes down and the shadow shortens.
    const auto cap = kKeyShadow.subtractedFrom(b.getLocalBounds()).toFloat().translated(0.0f, down ? 1.0f : 0.0f);
    juce::DropShadow(juce::Colours::black.withAlpha(down ? 0.45f : 0.6f), down ? 2 : 5,
                     down ? juce::Point<int>(0, 0) : juce::Point<int>(1, 2))
        .drawForRectangle(g, cap.toNearestInt());
    // Smooth plastic, unlike the panel.
    g.setColour(colours::key.brighter(highlighted && !down ? 0.1f : 0.0f).darker(down ? 0.1f : 0.0f));
    g.fillRoundedRectangle(cap, 1.5f);

    // Its edges: dark all round, catching the light along the top and left.
    g.setColour(juce::Colours::black.withAlpha(0.6f));
    g.drawRoundedRectangle(cap.expanded(0.5f), 2.0f, 1.0f);
    g.setColour(juce::Colours::white.withAlpha(down ? 0.04f : 0.11f));
    g.drawHorizontalLine((int) cap.getY(), cap.getX() + 1.0f, cap.getRight() - 1.0f);
    g.setColour(juce::Colours::white.withAlpha(0.04f));
    g.drawVerticalLine((int) cap.getX(), cap.getY() + 1.0f, cap.getBottom() - 1.0f);
    g.setColour(juce::Colours::black.withAlpha(0.25f));
    g.drawHorizontalLine((int) cap.getBottom() - 1, cap.getX() + 1.0f, cap.getRight() - 1.0f);

    if (b.getProperties()[kLed])
        drawKeyLed(g, { cap.getCentreX(), cap.getY() + juce::jmax(5.0f, cap.getHeight() * 0.19f) }, b.getToggleState(),
                   juce::jmin(cap.getWidth() * 0.37f, 18.0f));
}

void PanelLookAndFeel::drawButtonText(juce::Graphics& g, juce::TextButton& b, bool, bool down)
{
    const auto cap = kKeyShadow.subtractedFrom(b.getLocalBounds()).toFloat().translated(0.0f, down ? 1.0f : 0.0f);
    const int arrow = b.getProperties()[kArrow];
    g.setColour(b.isEnabled() ? colours::keyLegend : colours::silkscreenDim);
    if (arrow != 0)
    {
        // A hollow arrow with a short broad stem, as printed on the unit's keys (drawn pointing up).
        const float halfBase = cap.getWidth() * 0.33f, halfStem = halfBase * 0.62f, height = cap.getHeight() * 0.72f;
        const float y0 = -height * 0.5f, yBase = y0 + height * 0.84f, yEnd = height * 0.5f;
        juce::Path p;
        p.startNewSubPath(0.0f, y0);
        p.lineTo(halfBase, yBase);
        p.lineTo(halfStem, yBase);
        p.lineTo(halfStem, yEnd);
        p.lineTo(-halfStem, yEnd);
        p.lineTo(-halfStem, yBase);
        p.lineTo(-halfBase, yBase);
        p.closeSubPath();
        p.applyTransform(juce::AffineTransform::scale(1.0f, arrow > 0 ? 1.0f : -1.0f).translated(cap.getCentre()));
        g.strokePath(p, juce::PathStrokeType(1.3f, juce::PathStrokeType::mitered));
        return;
    }
    const bool hasLed = b.getProperties()[kLed];
    // Small, as on the unit; a long word is narrowed a little rather than cut.
    g.setFont(keyFont(juce::jlimit(9.0f, 10.5f, cap.getHeight() * 0.24f)));
    g.drawFittedText(b.getButtonText(), cap.toNearestInt().reduced(2, 1).withTrimmedTop(hasLed ? (int) (cap.getHeight() * 0.28f) : 0),
                     juce::Justification::centred, 2, 0.8f);
}

} // namespace glide90::ui
