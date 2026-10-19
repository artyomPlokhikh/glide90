#include "PanelLookAndFeel.h"

#include "GlideFonts.h"
#include "LcdFont.h"

#include <array>
#include <cmath>
#include <utility>

namespace glide90::ui
{
namespace
{

using C = juce::Colour;

const std::array<Theme, kThemeCount>& themes()
{
    static const std::array<Theme, kThemeCount> all { {
        { ThemeId::Crimson, "Crimson", "crimson",
          C(0xff141014), C(0xff1b1418), C(0xff2c2027),
          C(0xffe9d6dc), C(0xff8f7880), C(0xffff4f9a),
          C(0xff0c080a), C(0xff33222a), C(0xffff5aa0), C(0x66ff5aa0), C(0xff160f13),
          C(0xff241a1f), C(0xff000000), C(0xffe9d6dc), C(0xff241a1f), C(0xffffffff),
          C(0xff1d1d20), C(0xffff4f9a), C(0xff8f7880),
          Theme::Knob::Rack, Theme::Face::Condensed, Theme::Backdrop::Matte, true, true, false, 6.0f },
        { ThemeId::Smear, "Smear", "smear",
          C(0xff8e0f33), C(0x572a0010), C(0x1fffffff),
          C(0xfffff0f4), C(0xd4ffd0de), C(0xffffe6d0),
          C(0xc2fff4ec), C(0xfffff6ef), C(0xff5a0a22), C(0x735a0a22), C(0xfffbeee6),
          C(0x1affffff), C(0x80fff0e6), C(0xfffff0f4), C(0xffefe3d0), C(0xff5a0a22),
          C(0xffefe3d0), C(0xff5a0a22), C(0x80ffe6d0),
          Theme::Knob::Bakelite, Theme::Face::Barlow, Theme::Backdrop::Smear, false, false, true, 14.0f },
        { ThemeId::Duotone, "Duotone", "duotone",
          C(0xfff3a6bd), C(0xbff6b3c7), C(0xff1a0a0f),
          C(0xff1a0a0f), C(0xff6b2a3d), C(0xffc8173f),
          C(0xfff9cdd9), C(0xff1a0a0f), C(0xffc8173f), C(0x80c8173f), C(0xfff9cdd9),
          C(0x00000000), C(0xff1a0a0f), C(0xff1a0a0f), C(0xff1a0a0f), C(0xfff9cdd9),
          C(0xfff9cdd9), C(0xff1a0a0f), C(0xff1a0a0f),
          Theme::Knob::Flat, Theme::Face::Courier, Theme::Backdrop::Duotone, false, false, true, 0.0f },
        { ThemeId::Instant, "Instant", "instant",
          C(0xffeaa0b4), C(0x42ffffff), C(0x99ffffff),
          C(0xff5a1a2c), C(0xb35a1a2c), C(0xff8a2340),
          C(0xb8fffaf6), C(0xf2ffffff), C(0xff8a2340), C(0x738a2340), C(0xfffdf1f3),
          C(0x4dffffff), C(0x598a2340), C(0xff5a1a2c), C(0xff8a2340), C(0xfffff3f6),
          C(0x4dffffff), C(0xff8a2340), C(0x408a2340),
          Theme::Knob::Arc, Theme::Face::Barlow, Theme::Backdrop::Instant, false, false, true, 12.0f },
    } };
    return all;
}

int& currentTheme()
{
    static int index = 0;
    return index;
}

bool& themeChosen()
{
    static bool chosen = false;
    return chosen;
}

std::unique_ptr<juce::PropertiesFile> settings()
{
    juce::PropertiesFile::Options options;
    options.applicationName = "Glide90";
    options.folderName = "Glide90";
    options.filenameSuffix = ".settings";
    options.osxLibrarySubFolder = "Application Support";
    return std::make_unique<juce::PropertiesFile>(options);
}

constexpr const char* kLookKey = "look";
constexpr const char* kMotionKey = "motion";

MotionLevel& currentMotion()
{
    static MotionLevel level = [] {
        const auto saved = settings()->getValue(kMotionKey);
        return saved == "subtle" ? MotionLevel::Subtle : (saved == "off" ? MotionLevel::Off : MotionLevel::Full);
    }();
    return level;
}

// The red of the halftone, also the off-register second ink of the print theme.
const C kPrintRed { 0xffc8173f };

juce::Typeface::Ptr typeface(const char* data, int size)
{
    return juce::Typeface::createSystemTypefaceFor(data, (size_t) size);
}

// Fine grain: light and dark specks, 2 pixels per point so it stays sharp when zoomed. Transparent,
// so it goes over any colour. Built once, from a fixed seed so it is the same on every repaint.
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

void fillGrain(juce::Graphics& g, juce::Rectangle<float> area, float opacity)
{
    juce::Graphics::ScopedSaveState save(g);
    g.setFillType(juce::FillType(grain(), juce::AffineTransform::scale(0.5f)));
    g.setOpacity(opacity);
    g.fillRect(area);
}

juce::Path matrixPath(const juce::String& text, float x, float y, float pitch)
{
    juce::Path p;
    const float dot = pitch * 0.8f;
    for (int i = 0; i < text.length(); ++i)
    {
        const auto ch = text[i];
        const auto& glyph = kLcdFont[ch >= 32 && ch <= 126 ? (int) ch - 32 : '?' - 32];
        for (int row = 0; row < 7; ++row)
            for (int col = 0; col < 5; ++col)
                if ((glyph[row] >> (4 - col)) & 1)
                    p.addRectangle(x + (float) (i * 6 + col) * pitch, y + (float) row * pitch, dot, dot);
    }
    return p;
}

void fillWithHalo(juce::Graphics& g, const juce::Path& p, juce::Colour colour, bool halo)
{
    if (halo && theme().glow)
        juce::DropShadow(colour.withAlpha(0.75f), 9, {}).drawForPath(g, p);
    g.setColour(colour);
    g.fillPath(p);
}

// A soft round light.
void blob(juce::Graphics& g, float x, float y, float r, juce::Colour c)
{
    g.setGradientFill(juce::ColourGradient(c, x, y, c.withAlpha(0.0f), x + r, y, true));
    g.fillEllipse(x - r, y - r, 2.0f * r, 2.0f * r);
}

// Horizontal streaks, faded at both ends: light smeared by a moving camera.
void streak(juce::Graphics& g, float x, float y, float length, float thickness, juce::Colour c)
{
    juce::ColourGradient grad(c.withAlpha(0.0f), x, y, c.withAlpha(0.0f), x + length, y, false);
    grad.addColour(0.35, c);
    grad.addColour(0.7, c.withMultipliedAlpha(0.6f));
    g.setGradientFill(grad);
    g.fillRect(x, y - thickness * 0.5f, length, thickness);
}

struct Light
{
    float x, y, r; // fractions of the width (r) and of the width and height (x, y)
    C colour;
};

// A wash at a quarter of the size, blurred: drawn back at full size it stays soft.
juce::Image wash(int w, int h, C top, C bottom, std::initializer_list<Light> lights, int streakCount, juce::uint32 seed,
                 float streakAlpha)
{
    const int sw = juce::jmax(8, w / 4), sh = juce::jmax(8, h / 4);
    juce::Image im(juce::Image::ARGB, sw, sh, true);
    {
        juce::Graphics g(im);
        g.addTransform(juce::AffineTransform::scale((float) sw / (float) w, (float) sh / (float) h));
        const float fw = (float) w, fh = (float) h;
        g.setGradientFill(juce::ColourGradient(top, 0.0f, 0.0f, bottom, fw * 0.3f, fh, false));
        g.fillRect(0.0f, 0.0f, fw, fh);
        for (const auto& l : lights)
            blob(g, l.x * fw, l.y * fh, l.r * fw, l.colour);
        juce::Random random((juce::int64) seed);
        for (int i = 0; i < streakCount; ++i)
        {
            const float length = (0.25f + 0.7f * random.nextFloat()) * fw;
            const float x = random.nextFloat() * fw - length * 0.3f, y = random.nextFloat() * fh;
            const auto c = random.nextFloat() < 0.6f ? C(0xffffd6e4) : C(0xff4a0516);
            streak(g, x, y, length, 2.0f + random.nextFloat() * 16.0f, c.withAlpha(streakAlpha * (0.3f + random.nextFloat())));
        }
    }
    juce::ImageConvolutionKernel blur(13);
    blur.createGaussianBlur(4.0f);
    blur.applyToImage(im, im, im.getBounds());
    return im;
}

// The SMEAR streaks: transparent but for the streaks, wider than the window so they can move.
juce::Image smearStreaks(int w, int h)
{
    const int sw = juce::jmax(8, w * 8 / 20), sh = juce::jmax(8, h / 4); // 1.6 times the width, at a quarter
    juce::Image im(juce::Image::ARGB, sw, sh, true);
    {
        juce::Graphics g(im);
        const float fw = (float) w * 1.6f, fh = (float) h;
        g.addTransform(juce::AffineTransform::scale((float) sw / fw, (float) sh / fh));
        juce::Random random(77);
        for (int i = 0; i < 46; ++i)
        {
            const float length = (0.2f + 0.6f * random.nextFloat()) * fw * 0.6f;
            const float x = random.nextFloat() * fw - length * 0.3f, y = random.nextFloat() * fh;
            const auto c = random.nextFloat() < 0.65f ? C(0xffffc2d4) : C(0xff2a0010);
            streak(g, x, y, length, 3.0f + random.nextFloat() * 22.0f, c.withAlpha(0.12f + 0.22f * random.nextFloat()));
        }
    }
    juce::ImageConvolutionKernel blur(9);
    blur.createGaussianBlur(2.5f);
    blur.applyToImage(im, im, im.getBounds());
    return im;
}

// The print's halftone: a blurred guitar neck crossing the sheet, in dots of one ink `pitch` points
// apart, at `scale` pixels per point, on a transparent sheet.
juce::Image halftoneLayer(int w, int h, float scale, float pitch, C ink, float size)
{
    juce::Image im(juce::Image::ARGB, juce::jmax(1, juce::roundToInt((float) w * scale)), juce::jmax(1, juce::roundToInt((float) h * scale)), true);
    juce::Graphics g(im);
    g.addTransform(juce::AffineTransform::scale(scale));
    const float fw = (float) w, fh = (float) h;
    auto field = [fw, fh](float x, float y) {
        const float u = x / fw, v = y / fh * (fh / fw) * 2.2f; // the neck's angle kept for any height
        // The neck: a band from the lower left up to the right, the headstock at its end, the body's
        // edge in the corner.
        const float ax = 0.02f, ay = 1.25f, bx = 0.82f, by = -0.05f;
        const float dx = bx - ax, dy = by - ay, len = std::sqrt(dx * dx + dy * dy);
        const float d = std::abs((u - ax) * dy - (v - ay) * dx) / len;
        float f = 0.85f * std::exp(-(d * d) / (0.075f * 0.075f));
        f += 0.7f * std::exp(-((u - 0.84f) * (u - 0.84f) / 0.008f + (v - 0.02f) * (v - 0.02f) / 0.05f));
        f += 0.9f * std::exp(-((u + 0.02f) * (u + 0.02f) + (v - 1.3f) * (v - 1.3f)) / 0.06f);
        f += 0.12f * (0.5f + 0.5f * std::sin(u * 23.0f + v * 7.0f));
        return juce::jlimit(0.0f, 1.0f, f);
    };
    g.setColour(ink);
    int row = 0;
    for (float y = 0.0f; y < fh + pitch; y += pitch * 0.866f, ++row)
        for (float x = (row % 2) * pitch * 0.5f; x < fw + pitch; x += pitch)
        {
            const float r = pitch * 0.62f * size * std::sqrt(field(x, y));
            if (r > 0.35f)
                g.fillEllipse(x - r, y - r, 2.0f * r, 2.0f * r);
        }
    return im;
}


} // namespace

// --- Themes ---

const Theme& theme()
{
    return themes()[(size_t) currentTheme()];
}

const Theme& themeAt(int index)
{
    return themes()[(size_t) juce::jlimit(0, kThemeCount - 1, index)];
}

void setTheme(ThemeId id)
{
    currentTheme() = (int) id;
    themeChosen() = true;
}

void initialiseTheme()
{
    static bool done = false;
    if (std::exchange(done, true) || themeChosen())
        return;
    auto pick = [](const juce::String& wanted) {
        for (int i = 0; i < kThemeCount; ++i)
            if (wanted == themeAt(i).key || wanted == juce::String(i + 1))
            {
                setTheme(themeAt(i).id);
                return true;
            }
        return false;
    };
    if (pick(juce::SystemStats::getEnvironmentVariable("GLIDE90_THEME", {}).trim().toLowerCase()))
        return;
    pick(settings()->getValue(kLookKey).trim().toLowerCase());
}

MotionLevel motionLevel()
{
    return currentMotion();
}

void setMotionLevel(MotionLevel level)
{
    currentMotion() = level;
    auto file = settings();
    file->setValue(kMotionKey, level == MotionLevel::Full ? "full" : (level == MotionLevel::Subtle ? "subtle" : "off"));
    file->saveIfNeeded();
}

void rememberTheme()
{
    auto file = settings();
    file->setValue(kLookKey, juce::String(theme().key));
    file->saveIfNeeded();
}

juce::String themed(const juce::String& text)
{
    if (theme().matrix)
        return text.toUpperCase();
    return theme().lowercase ? text.toLowerCase() : text;
}

juce::Font legendFont(float height, bool strong)
{
    static const auto condensed = typeface(GlideFonts::BarlowCondensedMedium_ttf, GlideFonts::BarlowCondensedMedium_ttfSize);
    static const auto condensedStrong = typeface(GlideFonts::BarlowCondensedSemiBold_ttf, GlideFonts::BarlowCondensedSemiBold_ttfSize);
    static const auto barlow = typeface(GlideFonts::BarlowRegular_ttf, GlideFonts::BarlowRegular_ttfSize);
    static const auto barlowStrong = typeface(GlideFonts::BarlowMedium_ttf, GlideFonts::BarlowMedium_ttfSize);
    static const auto courier = typeface(GlideFonts::CourierPrimeRegular_ttf, GlideFonts::CourierPrimeRegular_ttfSize);
    static const auto courierStrong = typeface(GlideFonts::CourierPrimeBold_ttf, GlideFonts::CourierPrimeBold_ttfSize);
    switch (theme().face)
    {
        case Theme::Face::Barlow: return juce::Font(juce::FontOptions(strong ? barlowStrong : barlow).withHeight(height));
        case Theme::Face::Courier:
            return juce::Font(juce::FontOptions(strong ? courierStrong : courier).withHeight(height * 1.02f));
        case Theme::Face::Condensed:
        default: return juce::Font(juce::FontOptions(strong ? condensedStrong : condensed).withHeight(height));
    }
}

juce::Font readoutFont(float height, bool strong)
{
    static const auto light = typeface(GlideFonts::BarlowLight_ttf, GlideFonts::BarlowLight_ttfSize);
    if (theme().face == Theme::Face::Barlow && !strong)
        return juce::Font(juce::FontOptions(light).withHeight(height));
    return legendFont(height, strong || theme().face == Theme::Face::Courier);
}

// --- Text ---

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
    g.setColour(colour);
    g.fillPath(matrixPath(text, x, area.getCentreY() - 3.5f * pitch, pitch));
    return width;
}

namespace
{
float readoutHeight(bool large)
{
    return large ? 21.0f : 14.0f;
}
} // namespace

float readoutWidth(const juce::String& text, bool large)
{
    const auto t = themed(text);
    if (theme().matrix)
        return matrixTextWidth(t, large ? kLargeTextPitch : kSmallTextPitch);
    return juce::GlyphArrangement::getStringWidth(readoutFont(readoutHeight(large), large), t);
}

float drawReadout(juce::Graphics& g, const juce::String& text, juce::Rectangle<float> area, juce::Colour colour,
                  juce::Justification justification, bool large, bool glow)
{
    const auto t = themed(text);
    const float width = readoutWidth(text, large);
    float x = area.getX();
    if (justification.testFlags(juce::Justification::horizontallyCentred))
        x = area.getCentreX() - width * 0.5f;
    else if (justification.testFlags(juce::Justification::right))
        x = area.getRight() - width;

    juce::Path p;
    if (theme().matrix)
    {
        const float pitch = large ? kLargeTextPitch : kSmallTextPitch;
        p = matrixPath(t, x, area.getCentreY() - 3.5f * pitch, pitch);
    }
    else
    {
        const auto font = readoutFont(readoutHeight(large), large);
        juce::GlyphArrangement glyphs;
        glyphs.addLineOfText(font, t, x, area.getCentreY() + (font.getAscent() - font.getDescent()) * 0.5f);
        glyphs.createPath(p);
    }
    fillWithHalo(g, p, colour, glow);
    return width;
}

void strokeGlowing(juce::Graphics& g, const juce::Path& path, juce::Colour colour, float width)
{
    juce::Path outline;
    juce::PathStrokeType(width, juce::PathStrokeType::curved, juce::PathStrokeType::rounded).createStrokedPath(outline, path);
    fillWithHalo(g, outline, colour, true);
}

// --- Panels ---

void drawCard(juce::Graphics& g, juce::Rectangle<float> r, bool lifted)
{
    const auto& t = theme();
    if (t.backdrop == Theme::Backdrop::Duotone)
    {
        // Printed in two inks slightly off register.
        g.setColour(kPrintRed.withAlpha(0.55f));
        g.fillRect(r.translated(4.0f, 4.0f));
    }
    g.setColour(t.card);
    g.fillRoundedRectangle(r, t.cardCorner);
    if (t.cardEdge.getAlpha() > 0)
    {
        g.setColour(t.cardEdge);
        g.drawRoundedRectangle(r.reduced(0.5f), t.cardCorner, t.backdrop == Theme::Backdrop::Duotone ? 1.5f : 1.0f);
    }
    if (lifted)
    {
        g.setColour(t.accent.withAlpha(0.45f));
        g.drawRoundedRectangle(r.reduced(0.5f), t.cardCorner, 1.0f);
    }
}

void drawGroupRule(juce::Graphics& g, const juce::String& name, float x, float width, float y, bool quiet)
{
    const auto& t = theme();
    const auto font = legendFont(11.0f, true).withExtraKerningFactor(0.16f);
    const auto text = themed(name);
    const float textW = juce::GlyphArrangement::getStringWidth(font, text);
    const float gap = 8.0f, mid = x + width * 0.5f;
    g.setColour(t.textDim.withMultipliedAlpha(0.35f));
    g.drawHorizontalLine(juce::roundToInt(y), x, mid - textW * 0.5f - gap);
    g.drawHorizontalLine(juce::roundToInt(y), mid + textW * 0.5f + gap, x + width);
    g.setFont(font);
    g.setColour(quiet ? t.textDim.withMultipliedAlpha(0.5f) : t.textDim);
    g.drawText(text, juce::Rectangle<float>(textW + 4.0f, 12.0f).withCentre({ mid, y }), juce::Justification::centred, false);
}

void drawCardTitle(juce::Graphics& g, const juce::String& title, juce::Rectangle<float> area)
{
    g.setFont(legendFont(theme().matrix ? 13.0f : 17.0f, true).withExtraKerningFactor(theme().matrix ? 0.14f : 0.02f));
    g.setColour(theme().text);
    g.drawText(themed(title), area, juce::Justification::centredLeft);
}

void drawGlass(juce::Graphics& g, juce::Rectangle<float> r, float corner)
{
    const auto& t = theme();
    switch (t.backdrop)
    {
        case Theme::Backdrop::Matte:
            g.setColour(juce::Colours::black);
            g.fillRoundedRectangle(r, corner + 1.0f);
            g.setGradientFill(juce::ColourGradient(t.glass.brighter(0.15f), r.getX(), r.getY(), t.glass, r.getX(), r.getBottom(), false));
            g.fillRoundedRectangle(r.reduced(1.0f), corner);
            g.setColour(t.glassEdge);
            g.drawRoundedRectangle(r.reduced(1.0f), corner, 1.0f);
            break;
        case Theme::Backdrop::Instant:
        case Theme::Backdrop::Smear:
            g.setColour(t.glass);
            g.fillRoundedRectangle(r, corner * 2.0f);
            g.setColour(t.glassEdge.withMultipliedAlpha(0.7f));
            g.drawRoundedRectangle(r.reduced(0.5f), corner * 2.0f, 1.0f);
            break;
        case Theme::Backdrop::Duotone:
            g.setColour(t.glass);
            g.fillRect(r);
            g.setColour(t.glassEdge);
            g.drawRect(r, 1.5f);
            break;
    }
}

void drawLed(juce::Graphics& g, juce::Rectangle<float> r, juce::Colour colour, bool on)
{
    g.setColour(on ? colour : colour.withMultipliedAlpha(0.16f));
    if (theme().matrix)
        g.fillRect(r);
    else
        g.fillRoundedRectangle(r, juce::jmin(r.getWidth(), r.getHeight()) * 0.5f);
}

void drawKnob(juce::Graphics& g, juce::Rectangle<float> area, float position, bool enabled, bool large, bool scale)
{
    const auto& t = theme();
    constexpr float start = -2.36f, end = 2.36f; // +-135 degrees
    const float radius = juce::jmin(area.getWidth(), area.getHeight()) * 0.5f;
    const auto c = area.getCentre();
    const float alpha = enabled ? 1.0f : 0.4f;
    const float angle = start + juce::jlimit(0.0f, 1.0f, position) * (end - start);
    auto at = [c](float a, float distance) { return c + juce::Point<float>(std::sin(a), -std::cos(a)) * distance; };
    auto ticks = [&](juce::Colour colour) {
        for (int i = 0; scale && i <= 10; ++i)
        {
            const float a = start + (float) i / 10.0f * (end - start);
            const bool major = i == 0 || i == 5 || i == 10;
            g.setColour(colour.withMultipliedAlpha((major ? 0.9f : 0.55f) * alpha));
            g.drawLine(juce::Line<float>(at(a, radius * (major ? 0.82f : 0.86f)), at(a, radius * 0.98f)), major && large ? 1.4f : 1.1f);
        }
    };
    auto pointer = [&](float from, float to, juce::Colour colour, float width) {
        juce::Path line;
        line.startNewSubPath(at(angle, from));
        line.lineTo(at(angle, to));
        g.setColour(colour.withMultipliedAlpha(alpha));
        g.strokePath(line, juce::PathStrokeType(width, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    };

    switch (t.knob)
    {
        case Theme::Knob::Rack:
        {
            ticks(t.knobTrack);
            // Smooth matte black, its rim lit from above, with a shadow; a grip bar across the top.
            const float knobR = radius * 0.74f;
            const auto body = juce::Rectangle<float>(knobR * 2.0f, knobR * 2.0f).withCentre(c);
            g.setColour(juce::Colours::black.withAlpha(0.6f * alpha));
            g.fillEllipse(body.translated(0.0f, 1.8f).expanded(1.2f));
            g.setGradientFill(juce::ColourGradient(C(0xff38333a).withAlpha(alpha), c.x, body.getY(), C(0xff0a090a).withAlpha(alpha),
                                                   c.x, body.getBottom(), false));
            g.fillEllipse(body);
            const float topR = knobR * 0.86f;
            const auto top = juce::Rectangle<float>(topR * 2.0f, topR * 2.0f).withCentre(c);
            g.setColour(C(0xff1b191c).withAlpha(alpha));
            g.fillEllipse(top);
            const float barW = knobR * 0.36f, barL = topR * 1.96f;
            juce::Path bar;
            bar.addRoundedRectangle(-barW * 0.5f, -barL * 0.5f, barW, barL, barW * 0.45f);
            bar.applyTransform(juce::AffineTransform::rotation(angle).translated(c));
            g.setGradientFill(juce::ColourGradient(C(0xff2e2a2f).withAlpha(alpha), c.x, top.getY(), C(0xff121013).withAlpha(alpha),
                                                   c.x, top.getBottom(), false));
            g.fillPath(bar);
            juce::Path line;
            line.startNewSubPath(at(angle, topR * 0.3f));
            line.lineTo(at(angle, topR * 0.9f));
            if (enabled)
                strokeGlowing(g, line, t.knobPointer, large ? 2.0f : 1.6f);
            else
                pointer(topR * 0.3f, topR * 0.9f, t.knobPointer, 1.6f);
            break;
        }
        case Theme::Knob::Arc:
        {
            // A thin track and the value along it, a dot at its end; a faint disc to aim at.
            const float r = radius * 0.8f;
            g.setColour(t.knobFace.withMultipliedAlpha(alpha));
            g.fillEllipse(juce::Rectangle<float>(r * 1.5f, r * 1.5f).withCentre(c));
            juce::Path track, value;
            track.addCentredArc(c.x, c.y, r, r, 0.0f, start, end, true);
            value.addCentredArc(c.x, c.y, r, r, 0.0f, start, angle, true);
            g.setColour(t.knobTrack.withMultipliedAlpha(alpha));
            g.strokePath(track, juce::PathStrokeType(large ? 2.0f : 1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            g.setColour(t.knobPointer.withMultipliedAlpha(alpha));
            g.strokePath(value, juce::PathStrokeType(large ? 2.6f : 2.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            g.fillEllipse(juce::Rectangle<float>(large ? 8.0f : 7.0f, large ? 8.0f : 7.0f).withCentre(at(angle, r)));
            pointer(r * 0.12f, r * 0.5f, t.knobPointer.withMultipliedAlpha(0.6f), 1.2f);
            break;
        }
        case Theme::Knob::Bakelite:
        {
            ticks(t.knobTrack);
            const float knobR = radius * 0.72f;
            const auto body = juce::Rectangle<float>(knobR * 2.0f, knobR * 2.0f).withCentre(c);
            g.setColour(C(0xff2a0010).withAlpha(0.35f * alpha));
            g.fillEllipse(body.translated(0.0f, 2.5f).expanded(1.5f));
            g.setGradientFill(juce::ColourGradient(C(0xfff7efe2).withAlpha(alpha), c.x, body.getY(), C(0xffd5c0a2).withAlpha(alpha),
                                                   c.x, body.getBottom(), false));
            g.fillEllipse(body);
            const auto top = body.reduced(knobR * 0.16f);
            g.setGradientFill(juce::ColourGradient(C(0xffe8dac4).withAlpha(alpha), c.x, top.getY(), C(0xfff3e9da).withAlpha(alpha),
                                                   c.x, top.getBottom(), false));
            g.fillEllipse(top);
            pointer(knobR * 0.25f, knobR * 0.86f, t.knobPointer, large ? 2.4f : 2.0f);
            break;
        }
        case Theme::Knob::Flat:
        {
            ticks(t.knobTrack);
            const float knobR = radius * 0.7f;
            const auto body = juce::Rectangle<float>(knobR * 2.0f, knobR * 2.0f).withCentre(c);
            g.setColour(kPrintRed.withAlpha(0.6f * alpha));
            g.fillEllipse(body.translated(2.5f, 2.5f));
            g.setColour(t.knobFace.withMultipliedAlpha(alpha));
            g.fillEllipse(body);
            g.setColour(t.knobPointer.withMultipliedAlpha(alpha));
            g.drawEllipse(body, 1.6f);
            pointer(0.0f, knobR * 0.92f, t.knobPointer, 2.0f);
            break;
        }
    }
}

// --- Backdrop ---

namespace
{
constexpr int kShadowRadius = 26, kShadowDrop = 12;
} // namespace

Backdrop::Backdrop()
{
    setOpaque(true);
}

void Backdrop::setDragGuides(juce::Rectangle<int> landingArea, juce::Rectangle<int> liftedArea)
{
    auto dirty = [this] {
        return landing.expanded(4).getUnion(lifted.expanded(kShadowRadius + 4).withBottom(lifted.getBottom() + kShadowRadius + kShadowDrop + 4));
    };
    if (!landing.isEmpty() || !lifted.isEmpty())
        repaint(dirty());
    // The shadow is the same all through a drag: blurred once.
    if (!liftedArea.isEmpty() && (shadow.isNull() || liftedArea.getWidth() + 2 * kShadowRadius != shadow.getWidth()))
    {
        shadow = juce::Image(juce::Image::ARGB, liftedArea.getWidth() + 2 * kShadowRadius, liftedArea.getHeight() + 2 * kShadowRadius, true);
        juce::Graphics g(shadow);
        juce::DropShadow(juce::Colours::black.withAlpha(0.6f), kShadowRadius, {})
            .drawForRectangle(g, liftedArea.withPosition(kShadowRadius, kShadowRadius));
    }
    landing = landingArea;
    lifted = liftedArea;
    if (!landing.isEmpty() || !lifted.isEmpty())
        repaint(dirty());
}

void Backdrop::themeChanged()
{
    composite = {};
    streaks = {};
    shownKey.clear();
    repaint();
}

void Backdrop::resized()
{
    themeChanged();
}

void Backdrop::setMotion(const Motion& m)
{
    // Only SMEAR's background moves, and only at FULL.
    if (theme().backdrop != Theme::Backdrop::Smear || motionLevel() != MotionLevel::Full)
        return;
    motion = m;
    if (const auto key = motionKey(); key != shownKey)
    {
        shownKey = key;
        repaint();
    }
}

float Backdrop::smearOffset() const
{
    // The wash drifts slowly, and the bar drags it sideways.
    const float w = (float) getWidth();
    return (float) std::sin(motion.time * 0.35) * 14.0f + juce::jlimit(-0.25f * w, 0.25f * w, motion.cents * 1.6f);
}

juce::String Backdrop::motionKey() const
{
    // Repainted only when the picture moves by a whole pixel or the flush changes visibly: the slow
    // drift alone repaints a few times a second.
    return juce::String(juce::roundToInt(smearOffset())) + " " + juce::String(juce::roundToInt(std::abs(motion.cents) / 4.0f));
}

void Backdrop::render(float scale)
{
    const int w = juce::jmax(1, getWidth()), h = juce::jmax(1, getHeight());
    const auto r = getLocalBounds().toFloat();
    const auto& t = theme();
    const float fw = (float) w, fh = (float) h;

    composite = juce::Image(juce::Image::ARGB, juce::roundToInt(fw * scale), juce::roundToInt(fh * scale), true);
    {
        juce::Graphics g(composite);
        g.addTransform(juce::AffineTransform::scale(scale));
        g.setImageResamplingQuality(juce::Graphics::highResamplingQuality);
        g.fillAll(t.base);
        switch (t.backdrop)
        {
            case Theme::Backdrop::Matte:
                // Light leaking in at the top, as on a frame at the end of a film.
                blob(g, fw * 0.12f, -20.0f, fw * 0.42f, C(0xffff2a6d).withAlpha(0.3f));
                blob(g, fw * 0.88f, fh * 0.02f, fw * 0.28f, C(0xffff7ab0).withAlpha(0.14f));
                blob(g, fw * 0.6f, fh * 1.15f, fw * 0.5f, C(0xffa3123a).withAlpha(0.16f));
                fillGrain(g, r, 0.06f);
                break;
            case Theme::Backdrop::Smear:
                g.drawImage(wash(w, h, C(0xff6e0a26), C(0xffc21848),
                                 { { 0.3f, 0.3f, 0.5f, C(0xffff7aa8).withAlpha(0.6f) },
                                   { 0.8f, 0.7f, 0.4f, C(0xff2a0010).withAlpha(0.5f) },
                                   { 0.62f, 0.08f, 0.3f, C(0xffffc2d4).withAlpha(0.45f) } },
                                 30, 11, 0.1f),
                            r);
                fillGrain(g, r, 0.11f);
                break;
            case Theme::Backdrop::Duotone:
                g.drawImageTransformed(halftoneLayer(w, h, scale, 6.0f, kPrintRed.withAlpha(0.82f), 1.0f),
                                       juce::AffineTransform::scale(1.0f / scale));
                fillGrain(g, r, 0.08f);
                break;
            case Theme::Backdrop::Instant:
                g.drawImage(wash(w, h, C(0xfff2b6c4), C(0xffe78aa3),
                                 { { 0.25f, 0.2f, 0.5f, C(0xffffece2).withAlpha(0.7f) },
                                   { 0.8f, 0.8f, 0.5f, C(0xffc8466e).withAlpha(0.35f) },
                                   { 0.55f, 0.55f, 0.25f, C(0xfff7c6d2).withAlpha(0.5f) } },
                                 10, 3, 0.05f),
                            r);
                // A chemical leak at the top edge, and the instant frame.
                blob(g, fw * 0.7f, -10.0f, 270.0f, C(0xffff9650).withAlpha(0.45f));
                fillGrain(g, r, 0.06f);
                g.setColour(C(0xfff7f1ea));
                for (const auto& edge : { juce::Rectangle<float>(0.0f, 0.0f, fw, 8.0f), { 0.0f, fh - 8.0f, fw, 8.0f },
                                          { 0.0f, 0.0f, 8.0f, fh }, { fw - 8.0f, 0.0f, 8.0f, fh } })
                    g.fillRect(edge);
                break;
        }
    }
    if (t.backdrop == Theme::Backdrop::Smear)
    {
        // The streaks, at full size once, so a frame only moves them.
        const auto small = smearStreaks(w, h);
        streaks = juce::Image(juce::Image::ARGB, juce::roundToInt(fw * 1.6f * scale), composite.getHeight(), true);
        juce::Graphics g(streaks);
        g.setImageResamplingQuality(juce::Graphics::highResamplingQuality);
        g.drawImage(small, streaks.getBounds().toFloat());
    }
    compositeScale = scale;
}

void Backdrop::paint(juce::Graphics& g)
{
    const auto& t = theme();
    const float scale = g.getInternalContext().getPhysicalPixelScaleFactor();
    if (composite.isNull() || std::abs(scale - compositeScale) > 0.01f)
        render(scale);
    // One physical pixel per pixel: a copy, not a resampling.
    g.drawImageTransformed(composite, juce::AffineTransform::scale(1.0f / compositeScale));
    if (streaks.isValid())
    {
        const float x = (float) juce::roundToInt(-0.3f * (float) getWidth() + smearOffset());
        g.drawImageTransformed(streaks, juce::AffineTransform::scale(1.0f / compositeScale).translated(x, 0.0f));
        // A deep bend flushes the picture.
        g.setColour(C(0xffff2a6d).withAlpha(juce::jmin(0.22f, std::abs(motion.cents) / 400.0f)));
        g.fillRect(getLocalBounds());
    }

    // A module being dragged: its shadow, and where it will land.
    if (!lifted.isEmpty() && shadow.isValid())
        g.drawImageAt(shadow, lifted.getX() - kShadowRadius, lifted.getY() - kShadowRadius + kShadowDrop);
    if (!landing.isEmpty())
    {
        juce::Path outline, dashed;
        outline.addRoundedRectangle(landing.toFloat().reduced(0.5f), juce::jmax(2.0f, t.cardCorner));
        const float dashes[] { 5.0f, 4.0f };
        juce::PathStrokeType(1.2f).createDashedStroke(dashed, outline, dashes, 2);
        g.setColour(t.accent.withAlpha(0.5f));
        g.fillPath(dashed);
    }
}

// --- Look and feel ---

PanelLookAndFeel::PanelLookAndFeel()
{
    applyTheme();
}

void PanelLookAndFeel::applyTheme()
{
    const auto& t = theme();
    // LookAndFeel_V4 draws dialogs (their title and message) from its colour scheme, not from the
    // colour IDs: give it the theme's, then set the IDs over it.
    setColourScheme({ t.menu, t.menu, t.menu, t.ink.withAlpha(0.4f), t.ink, t.ink.withAlpha(0.1f), t.ink, t.ink.withAlpha(0.15f),
                      t.ink });
    setColour(juce::PopupMenu::backgroundColourId, t.menu);
    setColour(juce::PopupMenu::textColourId, t.ink);
    setColour(juce::PopupMenu::headerTextColourId, t.ink.withMultipliedAlpha(0.6f));
    setColour(juce::PopupMenu::highlightedBackgroundColourId, t.ink.withAlpha(0.1f));
    setColour(juce::PopupMenu::highlightedTextColourId, t.ink);
    setColour(juce::TextEditor::backgroundColourId, t.menu);
    setColour(juce::TextEditor::textColourId, t.ink);
    setColour(juce::TextEditor::outlineColourId, t.ink.withAlpha(0.4f));
    setColour(juce::TextEditor::focusedOutlineColourId, t.ink.withAlpha(0.7f));
    setColour(juce::TextEditor::highlightColourId, t.ink.withAlpha(0.25f));
    setColour(juce::CaretComponent::caretColourId, t.ink);
    setColour(juce::AlertWindow::backgroundColourId, t.menu);
    setColour(juce::AlertWindow::textColourId, t.ink);
    setColour(juce::AlertWindow::outlineColourId, t.ink.withAlpha(0.3f));
    setColour(juce::TooltipWindow::backgroundColourId, t.menu);
    setColour(juce::TooltipWindow::textColourId, t.ink);
    setColour(juce::TooltipWindow::outlineColourId, t.ink.withAlpha(0.3f));
    setColour(juce::TextButton::textColourOffId, t.keyText);
}

namespace
{
constexpr float kTipWidth = 300.0f, kTipPadX = 11.0f, kTipPadY = 8.0f;

juce::TextLayout tipLayout(const juce::String& text)
{
    // Set like the panel's legends: their face, their case.
    juce::AttributedString s;
    s.setJustification(juce::Justification::topLeft);
    s.append(themed(text), legendFont(14.0f).withExtraKerningFactor(theme().matrix ? 0.03f : 0.0f), theme().ink);
    juce::TextLayout layout;
    layout.createLayout(s, kTipWidth);
    return layout;
}
} // namespace

juce::Rectangle<int> PanelLookAndFeel::getTooltipBounds(const juce::String& text, juce::Point<int> screenPos,
                                                        juce::Rectangle<int> parentArea)
{
    const auto layout = tipLayout(text);
    const int w = (int) std::ceil(layout.getWidth() + 2.0f * kTipPadX), h = (int) std::ceil(layout.getHeight() + 2.0f * kTipPadY);
    // Below and right of the pointer, kept inside the window.
    return juce::Rectangle<int>(screenPos.x > parentArea.getCentreX() ? screenPos.x - (w + 12) : screenPos.x + 12,
                                screenPos.y > parentArea.getCentreY() ? screenPos.y - (h + 6) : screenPos.y + 18, w, h)
        .constrainedWithin(parentArea);
}

void PanelLookAndFeel::drawTooltip(juce::Graphics& g, const juce::String& text, int width, int height)
{
    const auto& t = theme();
    const auto r = juce::Rectangle<float>((float) width, (float) height);
    const float corner = juce::jmin(t.cardCorner, 6.0f);
    g.setColour(t.menu);
    g.fillRoundedRectangle(r, corner);
    g.setColour(t.backdrop == Theme::Backdrop::Duotone ? t.cardEdge : t.ink.withAlpha(0.3f));
    g.drawRoundedRectangle(r.reduced(0.5f), corner, t.backdrop == Theme::Backdrop::Duotone ? 1.5f : 1.0f);
    tipLayout(text).draw(g, r.reduced(kTipPadX, kTipPadY));
}

void PanelLookAndFeel::drawPopupMenuBackground(juce::Graphics& g, int width, int height)
{
    g.fillAll(theme().menu);
    g.setColour(theme().ink.withAlpha(0.3f));
    g.drawRect(0, 0, width, height, 1);
}

void PanelLookAndFeel::getIdealPopupMenuItemSize(const juce::String& text, bool isSeparator, int, int& width, int& height)
{
    height = isSeparator ? 9 : 24;
    width = (int) readoutWidth(text) + 52;
}

void PanelLookAndFeel::drawPopupMenuItem(juce::Graphics& g, const juce::Rectangle<int>& area, bool isSeparator,
                                         bool isActive, bool isHighlighted, bool isTicked, bool hasSubMenu,
                                         const juce::String& text, const juce::String&, const juce::Drawable*,
                                         const juce::Colour*)
{
    const auto& t = theme();
    const auto r = area.toFloat();
    if (isSeparator)
    {
        g.setColour(t.ink.withAlpha(0.2f));
        g.drawHorizontalLine((int) r.getCentreY(), r.getX() + 8.0f, r.getRight() - 8.0f);
        return;
    }
    if (isHighlighted && isActive)
    {
        g.setColour(t.ink.withAlpha(0.1f));
        g.fillRect(r.reduced(2.0f, 1.0f));
    }
    if (isTicked)
    {
        g.setColour(t.ink);
        g.fillRect(juce::Rectangle<float>(3.0f, r.getHeight() - 10.0f).withPosition(r.getX() + 9.0f, r.getY() + 5.0f));
    }
    const auto colour = !isActive ? t.ink.withAlpha(0.3f) : t.ink.withAlpha(isTicked || isHighlighted ? 1.0f : 0.8f);
    drawReadout(g, text, r.withTrimmedLeft(22.0f).withTrimmedRight(hasSubMenu ? 20.0f : 8.0f), colour);
    if (hasSubMenu)
    {
        juce::Path arrow;
        const float x = r.getRight() - 12.0f, y = r.getCentreY();
        arrow.addTriangle(x - 3.0f, y - 4.0f, x - 3.0f, y + 4.0f, x + 2.0f, y);
        g.setColour(colour);
        g.fillPath(arrow);
    }
}

void PanelLookAndFeel::drawPopupMenuSectionHeader(juce::Graphics& g, const juce::Rectangle<int>& area, const juce::String& name)
{
    g.setFont(legendFont(12.0f, true).withExtraKerningFactor(0.1f));
    g.setColour(theme().ink.withAlpha(0.6f));
    g.drawText(themed(name), area.toFloat().withTrimmedLeft(12.0f).withTrimmedTop(4.0f), juce::Justification::centredLeft);
}

void PanelLookAndFeel::drawButtonBackground(juce::Graphics& g, juce::Button& b, const juce::Colour&, bool highlighted,
                                            bool down)
{
    const auto& t = theme();
    const bool on = b.getProperties()[kLed] && b.getToggleState();
    auto cap = b.getLocalBounds().toFloat().reduced(2.0f, 2.0f);

    // Half of a two-way switch: its inner side touches the other half.
    if (const auto side = b.getProperties()[kSegment].toString(); side.isNotEmpty())
    {
        const bool left = side == "left", lit = b.getToggleState();
        cap = b.getLocalBounds().toFloat().reduced(0.0f, 2.0f).withTrimmedLeft(left ? 2.0f : 0.0f).withTrimmedRight(left ? 0.0f : 2.0f);
        const float corner = t.backdrop == Theme::Backdrop::Duotone ? 0.0f
                             : (t.backdrop == Theme::Backdrop::Matte ? 3.0f : cap.getHeight() * 0.5f);
        juce::Path shape;
        shape.addRoundedRectangle(cap.getX(), cap.getY(), cap.getWidth(), cap.getHeight(), corner, corner, left, !left, left, !left);
        switch (t.backdrop)
        {
            case Theme::Backdrop::Matte:
                if (left)
                    juce::DropShadow(juce::Colours::black.withAlpha(0.55f), 5, { 1, 2 })
                        .drawForRectangle(g, cap.withWidth(cap.getWidth() * 2.0f).toNearestInt());
                g.setColour(lit ? C(0xff3a1f2b) : t.keyFill.brighter(highlighted && !down ? 0.12f : 0.0f));
                g.fillPath(shape);
                if (lit)
                {
                    juce::DropShadow(t.accent.withAlpha(0.45f), 8, {}).drawForPath(g, shape);
                    g.setColour(C(0xff3a1f2b));
                    g.fillPath(shape);
                    g.setColour(t.accent);
                    g.strokePath(shape, juce::PathStrokeType(1.0f));
                }
                else
                {
                    g.setColour(juce::Colours::black.withAlpha(0.6f));
                    g.strokePath(shape, juce::PathStrokeType(1.0f));
                }
                break;
            case Theme::Backdrop::Instant:
            case Theme::Backdrop::Smear:
                g.setColour(lit ? t.keyOnFill : (highlighted ? t.keyFill.withMultipliedAlpha(2.0f) : t.keyFill));
                g.fillPath(shape);
                g.setColour(t.keyEdge);
                g.strokePath(shape, juce::PathStrokeType(1.0f));
                break;
            case Theme::Backdrop::Duotone:
                if (!lit)
                {
                    g.setColour(kPrintRed.withAlpha(0.6f));
                    g.fillRect(cap.translated(2.5f, 2.5f));
                }
                g.setColour(lit ? t.keyOnFill : (highlighted ? t.knobFace : t.card.withAlpha(1.0f)));
                g.fillRect(cap);
                g.setColour(t.keyEdge);
                g.drawRect(cap, 1.5f);
                break;
        }
        return;
    }

    // A dialog's buttons sit on the dialog's own colour.
    if ((bool) b.getProperties()[kOnMenu])
    {
        g.setColour(t.ink.withAlpha(highlighted ? 0.14f : 0.07f));
        g.fillRoundedRectangle(cap, 4.0f);
        g.setColour(t.ink.withAlpha(0.5f));
        g.drawRoundedRectangle(cap, 4.0f, 1.0f);
        return;
    }

    switch (t.backdrop)
    {
        case Theme::Backdrop::Matte:
        {
            // A raised key with a soft shadow; pressed, it goes down. An LED shows the toggle state.
            cap = cap.withTrimmedRight(1.0f).withTrimmedBottom(2.0f).translated(0.0f, down ? 1.0f : 0.0f);
            juce::DropShadow(juce::Colours::black.withAlpha(down ? 0.45f : 0.6f), down ? 2 : 5,
                             down ? juce::Point<int>(0, 0) : juce::Point<int>(1, 2))
                .drawForRectangle(g, cap.toNearestInt());
            g.setColour(t.keyFill.brighter(highlighted && !down ? 0.12f : 0.0f));
            g.fillRoundedRectangle(cap, 2.0f);
            g.setColour(juce::Colours::black.withAlpha(0.6f));
            g.drawRoundedRectangle(cap.expanded(0.5f), 2.5f, 1.0f);
            g.setColour(juce::Colours::white.withAlpha(down ? 0.04f : 0.1f));
            g.drawHorizontalLine((int) cap.getY(), cap.getX() + 1.0f, cap.getRight() - 1.0f);
            if (b.getProperties()[kLed])
            {
                const auto led = juce::Rectangle<float>(juce::jmin(cap.getWidth() * 0.37f, 16.0f), 3.5f)
                                     .withCentre({ cap.getCentreX(), cap.getY() + 6.0f });
                if (on)
                    juce::DropShadow(t.accent.withAlpha(0.8f), 6, {}).drawForRectangle(g, led.toNearestInt());
                g.setColour(on ? t.accent : C(0xff4a3f45));
                g.fillRect(led);
            }
            break;
        }
        case Theme::Backdrop::Instant:
        case Theme::Backdrop::Smear:
        {
            const float corner = cap.getHeight() * 0.5f;
            auto fill = on ? t.keyOnFill : t.keyFill;
            if (highlighted && !on)
                fill = fill.withMultipliedAlpha(2.0f);
            g.setColour(down ? fill.withMultipliedAlpha(1.5f) : fill);
            g.fillRoundedRectangle(cap, corner);
            g.setColour(t.keyEdge);
            g.drawRoundedRectangle(cap.reduced(0.5f), corner, 1.0f);
            break;
        }
        case Theme::Backdrop::Duotone:
        {
            cap = cap.translated(down ? 1.5f : 0.0f, down ? 1.5f : 0.0f);
            if (!down)
            {
                g.setColour(kPrintRed.withAlpha(0.6f));
                g.fillRect(cap.translated(2.5f, 2.5f));
            }
            g.setColour(on ? t.keyOnFill : (highlighted ? t.knobFace : t.card.withAlpha(1.0f)));
            g.fillRect(cap);
            g.setColour(t.keyEdge);
            g.drawRect(cap, 1.5f);
            break;
        }
    }
}

void PanelLookAndFeel::drawButtonText(juce::Graphics& g, juce::TextButton& b, bool, bool down)
{
    const auto& t = theme();
    const bool dialog = (bool) b.getProperties()[kOnMenu];
    const bool segment = b.getProperties()[kSegment].toString().isNotEmpty();
    const bool on = (b.getProperties()[kLed] || segment) && b.getToggleState();
    const bool rack = t.backdrop == Theme::Backdrop::Matte;
    auto cap = b.getLocalBounds().toFloat().reduced(2.0f, 2.0f);
    if (segment)
    {
        g.setFont(legendFont(rack ? 13.0f : 14.0f, true));
        g.setColour(on ? (rack ? juce::Colours::white : t.keyOnText) : t.keyText.withMultipliedAlpha(0.75f));
        g.drawText(themed(b.getButtonText()), b.getLocalBounds(), juce::Justification::centred);
        return;
    }
    if (rack)
        cap = cap.withTrimmedRight(1.0f).withTrimmedBottom(2.0f);
    if (down)
        cap = cap.translated(t.backdrop == Theme::Backdrop::Duotone ? 1.5f : 0.0f, rack || t.backdrop == Theme::Backdrop::Duotone ? 1.0f : 0.0f);

    auto colour = dialog ? t.ink : ((on && !rack) ? t.keyOnText : t.keyText);
    if (!b.isEnabled())
        colour = colour.withMultipliedAlpha(0.35f);
    g.setColour(colour);

    if (const auto icon = b.getProperties()[kIcon].toString(); icon == "undo" || icon == "redo")
    {
        // A hooked arrow: back (undo) or, mirrored, forward (redo). Drawn in a 16-point box.
        auto p = juce::Drawable::parseSVGPath("M5 3 2 6l3 3M2 6h7.5a3.75 3.75 0 0 1 0 7.5H7");
        const auto c = cap.getCentre();
        p.applyTransform(juce::AffineTransform::translation(-8.0f, -8.0f)
                             .scaled(icon == "redo" ? -1.0f : 1.0f, 1.0f)
                             .translated(c.x, c.y + 0.5f));
        g.strokePath(p, juce::PathStrokeType(1.6f, juce::PathStrokeType::mitered, juce::PathStrokeType::rounded));
        return;
    }
    const bool led = rack && b.getProperties()[kLed];
    g.setFont(legendFont(rack && !dialog ? 12.0f : 14.0f, rack));
    g.drawFittedText(themed(b.getButtonText()),
                     cap.toNearestInt().reduced(2, 1).withTrimmedTop(led ? (int) (cap.getHeight() * 0.3f) : 0),
                     juce::Justification::centred, 2, 0.8f);
}

} // namespace glide90::ui
