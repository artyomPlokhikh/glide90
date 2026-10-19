#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

namespace glide90::ui
{

// 16 x 2 character dot-matrix LCD with a yellow-green backlight.
class LcdDisplay : public juce::Component, public juce::SettableTooltipClient
{
public:
    static constexpr int kColumns = 16;

    void setText(const juce::String& top, const juce::String& bottom);
    // Without its own glass, for a display that sits behind a shared window.
    void setFramed(bool f) { framed = f; }

    // Mouse wheel or vertical drag over the display: one step up (+1) or down (-1).
    std::function<void(int)> onStep;

    void paint(juce::Graphics&) override;
    void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;

private:
    juce::String lines[2];
    bool framed = true;
    float wheelAccumulator = 0.0f;
    int dragAnchor = 0;
};

// Two-digit red 7-segment LED (MEMORY number). Blinks while a memory is selected but not recalled.
class SevenSegment : public juce::Component, private juce::Timer
{
public:
    void setValue(int value, bool blinking);
    void setFramed(bool f) { framed = f; }
    void paint(juce::Graphics&) override;

private:
    void timerCallback() override;

    int value = 0;
    bool blinking = false, blinkOn = true, framed = true;
};

// The 7-point input level meter.
class LevelMeter : public juce::Component
{
public:
    // Thresholds in dB relative to A/D full scale; the last one means clipping.
    static constexpr float kPointsDb[7] { -36.0f, -24.0f, -15.0f, -9.0f, -5.0f, -2.0f, -0.1f };

    void setLevelDb(float db);
    void paint(juce::Graphics&) override;

private:
    float levelDb = -100.0f;
};

} // namespace glide90::ui
