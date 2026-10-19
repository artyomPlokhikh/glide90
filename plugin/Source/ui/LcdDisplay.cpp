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
    auto glass = getLocalBounds().toFloat();
    if (framed)
    {
        drawGlass(g, glass);
        glass = glass.reduced(6.0f);
    }

    // 16 characters of 5 dots plus a 1-dot gap; two rows of 7 dots with a 3-dot gap between them.
    constexpr int dotsW = kColumns * 6 - 1, dotsH = 7 * 2 + 3;
    const float pitch = juce::jmin((glass.getWidth() - 12.0f) / dotsW, (glass.getHeight() - 10.0f) / dotsH);
    const float dot = pitch * 0.82f;
    const float x0 = glass.getCentreX() - pitch * dotsW * 0.5f;
    const float y0 = glass.getCentreY() - pitch * dotsH * 0.5f;
    const auto lit = colours::display, unlit = colours::displayUnlit;
    if (!framed)
    {
        // The LCD's own backlit panel, behind the window's glass.
        g.setColour(colours::lcd);
        g.fillRect(juce::Rectangle<float>(x0, y0, pitch * dotsW, pitch * dotsH).expanded(pitch * 2.5f));
    }

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
                    const float px = x0 + (float) (col * 6 + x) * pitch, py = y0 + (float) (row * 10 + y) * pitch;
                    if (on)
                    {
                        g.setColour(lit.withAlpha(0.12f)); // glow
                        g.fillRect(px - pitch * 0.3f, py - pitch * 0.3f, dot + pitch * 0.6f, dot + pitch * 0.6f);
                    }
                    g.setColour(on ? lit : unlit);
                    g.fillRect(px, py, dot, dot);
                }
        }
    }

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
    v = juce::jlimit(0, 99, v); // two digits

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
    if (framed)
    {
        drawGlass(g, r);
        r = r.reduced(4.0f);
    }
    // Unlit segments behind the number, as on a real LED display.
    g.setFont(segmentFont(r.getHeight() * 0.86f));
    g.setColour(colours::memoryRed.withAlpha(0.09f));
    g.drawText("88", r, juce::Justification::centredRight);
    if (!blinking || blinkOn)
    {
        g.setColour(colours::memoryRed);
        g.drawText(value < 10 ? "!" + juce::String(value) : juce::String(value), r, juce::Justification::centredRight);
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
    // A column of seven bars, loudest at the top, with their thresholds on the right.
    const auto r = getLocalBounds().toFloat();
    const float cell = r.getHeight() / 7.0f;
    g.setFont(legendFont(10.0f));
    static const char* labels[7] { "-36", "-24", "-15", "-9", "-5", "-2", "0" };
    for (int i = 0; i < 7; ++i)
    {
        const auto colour = i == 6 ? colours::ledRed : (i == 5 ? colours::ledYellow : colours::ledGreen);
        const float cy = r.getBottom() - cell * ((float) i + 0.5f);
        drawLed(g, juce::Rectangle<float>(r.getX(), cy - cell * 0.32f, 12.0f, cell * 0.64f), colour, levelDb >= kPointsDb[i]);
        g.setColour(colours::silkscreenDim);
        g.drawText(labels[i], juce::Rectangle<float>(r.getX() + 15.0f, cy - 5.0f, r.getWidth() - 15.0f, 10.0f),
                   juce::Justification::centredLeft);
    }
}

} // namespace glide90::ui
