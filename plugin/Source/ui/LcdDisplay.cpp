#include "LcdDisplay.h"

#include "LcdFont.h"
#include "PanelLookAndFeel.h"

namespace glide90::ui
{

void LcdDisplay::setText(const juce::String& top, const juce::String& bottom)
{
    if (top == lines[0] && bottom == lines[1])
        return;
    lines[0] = top;
    lines[1] = bottom;
    repaint();
}

void LcdDisplay::paint(juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    // Bezel and glass.
    g.setColour(juce::Colour(0xff0b0b0c));
    g.fillRoundedRectangle(r, 5.0f);
    auto glass = r.reduced(6.0f);
    g.setGradientFill(juce::ColourGradient(juce::Colour(0xffb4d046), glass.getX(), glass.getY(),
                                           juce::Colour(0xff93b334), glass.getX(), glass.getBottom(), false));
    g.fillRoundedRectangle(glass, 3.0f);

    // 16 characters of 5 dots plus a 1-dot gap; two rows of 7 dots with a 3-dot gap between them.
    constexpr int dotsW = kColumns * 6 - 1, dotsH = 7 * 2 + 3;
    const float pitch = juce::jmin((glass.getWidth() - 12.0f) / dotsW, (glass.getHeight() - 10.0f) / dotsH);
    const float dot = pitch * 0.86f;
    const float x0 = glass.getCentreX() - pitch * dotsW * 0.5f;
    const float y0 = glass.getCentreY() - pitch * dotsH * 0.5f;
    const auto lit = juce::Colour(0xff1f2a12), unlit = juce::Colour(0x1a1f2a12);

    for (int row = 0; row < 2; ++row)
    {
        const auto text = lines[row].paddedRight(' ', kColumns);
        for (int col = 0; col < kColumns; ++col)
        {
            const auto ch = text[col];
            const int index = ch >= 32 && ch <= 126 ? (int) ch - 32 : '?' - 32;
            const auto& glyph = kLcdFont[index];
            for (int y = 0; y < 7; ++y)
                for (int x = 0; x < 5; ++x)
                {
                    const bool on = (glyph[y] >> (4 - x)) & 1;
                    g.setColour(on ? lit : unlit);
                    g.fillRect(x0 + (float) (col * 6 + x) * pitch, y0 + (float) (row * 10 + y) * pitch, dot, dot);
                }
        }
    }

    // Glare.
    g.setGradientFill(juce::ColourGradient(juce::Colours::white.withAlpha(0.10f), glass.getX(), glass.getY(),
                                           juce::Colours::transparentWhite, glass.getX(), glass.getCentreY(), false));
    g.fillRoundedRectangle(glass.withHeight(glass.getHeight() * 0.5f), 3.0f);
}

void LcdDisplay::mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails& wheel)
{
    wheelAccumulator += wheel.deltaY * (wheel.isReversed ? -1.0f : 1.0f);
    while (std::abs(wheelAccumulator) >= 0.1f)
    {
        const int dir = wheelAccumulator > 0.0f ? 1 : -1;
        wheelAccumulator -= 0.1f * (float) dir;
        if (onStep)
            onStep(dir);
    }
}

void LcdDisplay::mouseDown(const juce::MouseEvent& e)
{
    dragAnchor = e.getPosition().y;
}

void LcdDisplay::mouseDrag(const juce::MouseEvent& e)
{
    constexpr int pixelsPerStep = 6;
    while (std::abs(e.getPosition().y - dragAnchor) >= pixelsPerStep)
    {
        const int dir = e.getPosition().y < dragAnchor ? 1 : -1;
        dragAnchor -= dir * pixelsPerStep;
        if (onStep)
            onStep(dir);
    }
}

void SevenSegment::setValue(int v, bool blink)
{
    if (blink != blinking)
    {
        blinking = blink;
        blinkOn = true;
        if (blinking)
            startTimer(300);
        else
            stopTimer();
        repaint();
    }
    if (v != value)
    {
        value = v;
        repaint();
    }
}

void SevenSegment::timerCallback()
{
    blinkOn = !blinkOn;
    repaint();
}

void SevenSegment::paint(juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour(juce::Colour(0xff0b0b0c));
    g.fillRoundedRectangle(r, 4.0f);
    g.setColour(juce::Colour(0xff1d0806));
    g.fillRoundedRectangle(r.reduced(4.0f), 2.0f);

    // Segment bits: a b c d e f g.
    static constexpr uint8_t digits[10] { 0x3f, 0x06, 0x5b, 0x4f, 0x66, 0x6d, 0x7d, 0x07, 0x7f, 0x6f };
    const auto lit = juce::Colour(0xffff3a22), unlit = juce::Colour(0xff3a100b);
    const float h = r.getHeight() * 0.68f, w = h * 0.5f, t = h * 0.11f, skew = h * 0.08f;
    const float gap = w * 0.45f;
    float x = r.getCentreX() - (2.0f * w + gap) * 0.5f;
    const float y = r.getCentreY() - h * 0.5f;

    auto segment = [&](float x1, float y1, float x2, float y2) {
        // A bar with pointed ends from (x1,y1) to (x2,y2), slanted like a real display.
        auto sk = [&](float px, float py) { return juce::Point<float>(px + skew * (1.0f - (py - y) / h), py); };
        juce::Path p;
        if (std::abs(y1 - y2) < 0.01f) // horizontal
        {
            p.startNewSubPath(sk(x1 + t * 0.5f, y1));
            p.lineTo(sk(x1 + t, y1 - t * 0.5f));
            p.lineTo(sk(x2 - t, y1 - t * 0.5f));
            p.lineTo(sk(x2 - t * 0.5f, y1));
            p.lineTo(sk(x2 - t, y1 + t * 0.5f));
            p.lineTo(sk(x1 + t, y1 + t * 0.5f));
        }
        else
        {
            p.startNewSubPath(sk(x1, y1 + t * 0.5f));
            p.lineTo(sk(x1 + t * 0.5f, y1 + t));
            p.lineTo(sk(x1 + t * 0.5f, y2 - t));
            p.lineTo(sk(x1, y2 - t * 0.5f));
            p.lineTo(sk(x1 - t * 0.5f, y2 - t));
            p.lineTo(sk(x1 - t * 0.5f, y1 + t));
        }
        p.closeSubPath();
        return p;
    };

    const bool show = !blinking || blinkOn;
    for (int d = 0; d < 2; ++d)
    {
        const int digit = d == 0 ? (value / 10) % 10 : value % 10;
        const uint8_t bits = (d == 0 && value < 10) ? 0 : digits[digit]; // no leading zero
        const float m = y + h * 0.5f;
        const juce::Path segs[7] { segment(x, y, x + w, y),         segment(x + w, y, x + w, m),
                                   segment(x + w, m, x + w, y + h), segment(x, y + h, x + w, y + h),
                                   segment(x, m, x, y + h),         segment(x, y, x, m),
                                   segment(x, m, x + w, m) };
        for (int s = 0; s < 7; ++s)
        {
            const bool on = show && ((bits >> s) & 1);
            g.setColour(on ? lit : unlit);
            g.fillPath(segs[s]);
        }
        x += w + gap;
    }
}

void LevelMeter::setLevelDb(float db)
{
    if (std::abs(db - levelDb) > 0.05f)
    {
        levelDb = db;
        repaint();
    }
}

void LevelMeter::paint(juce::Graphics& g)
{
    const auto r = getLocalBounds().toFloat();
    const float cell = r.getWidth() / 7.0f;
    const float d = juce::jmin(cell * 0.5f, r.getHeight() * 0.45f);
    g.setFont(panelFont(9.5f));
    static const char* labels[7] { "-36", "-24", "-15", "-9", "-5", "-2", "CLIP" };
    for (int i = 0; i < 7; ++i)
    {
        const auto colour = i == 6 ? colours::ledRed : (i == 5 ? colours::ledYellow : colours::ledGreen);
        const float cx = r.getX() + cell * ((float) i + 0.5f);
        drawLed(g, juce::Rectangle<float>(d, d).withCentre({ cx, r.getBottom() - d * 0.5f - 2.0f }), colour,
                levelDb >= kPointsDb[i]);
        g.setColour(colours::legendDim);
        g.drawText(labels[i], juce::Rectangle<float>(cell + 6.0f, 12.0f).withCentre({ cx, r.getY() + 6.0f }),
                   juce::Justification::centred);
    }
}

} // namespace glide90::ui
