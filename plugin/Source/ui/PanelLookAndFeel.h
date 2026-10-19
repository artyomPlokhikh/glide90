#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace glide90::ui
{

// The SPX90's look: black brushed panels, small knurled knobs, square black keys with red LEDs,
// green text behind smoked glass.
namespace colours
{
inline const juce::Colour panelTop { 0xff1d1d1e }, panelBottom { 0xff0f0f10 };
inline const juce::Colour ear { 0xff151516 };
inline const juce::Colour legend { 0xffe4e4e0 }, legendDim { 0xff85857f };
inline const juce::Colour glass { 0xff0a0e0c }, glassEdge { 0xff2a2c2b };
inline const juce::Colour vfd { 0xff7de0a6 }, vfdDim { 0xff1f3a2a };
inline const juce::Colour ledRed { 0xffff3322 }, ledGreen { 0xff46e06a }, ledYellow { 0xffffcf3a };
inline const juce::Colour ledOff { 0xff3a1512 };
} // namespace colours

juce::Font panelFont(float height, bool bold = false);

// A 1U-style unit: brushed black face with rack ears and slots.
void drawRackUnit(juce::Graphics&, juce::Rectangle<int> bounds);
// A smoked-glass window.
void drawGlass(juce::Graphics&, juce::Rectangle<float> area, float corner = 3.0f);
// The small red LED of a key.
void drawKeyLed(juce::Graphics&, juce::Point<float> centre, bool on);
// A round LED (level meter).
void drawLed(juce::Graphics&, juce::Rectangle<float> area, juce::Colour colour, bool on);

class PanelLookAndFeel : public juce::LookAndFeel_V4
{
public:
    // Margin of the rack ears; content starts inside it.
    static constexpr int kEar = 30;

    PanelLookAndFeel();

    void drawRotarySlider(juce::Graphics&, int x, int y, int w, int h, float pos, float start, float end,
                          juce::Slider&) override;
    void drawButtonBackground(juce::Graphics&, juce::Button&, const juce::Colour&, bool highlighted,
                              bool down) override;
    void drawButtonText(juce::Graphics&, juce::TextButton&, bool highlighted, bool down) override;
    void drawComboBox(juce::Graphics&, int width, int height, bool down, int bx, int by, int bw, int bh,
                      juce::ComboBox&) override;
    juce::Font getComboBoxFont(juce::ComboBox&) override { return panelFont(14.0f); }
    juce::Font getPopupMenuFont() override { return panelFont(14.0f); }
    juce::Font getLabelFont(juce::Label&) override { return panelFont(13.0f); }
    juce::Label* createSliderTextBox(juce::Slider&) override;

    // Button properties: "led" (a red LED that shows the toggle state) and "arrow" (+1 up, -1 down:
    // a hollow triangle instead of text, as on the unit's data keys).
    static constexpr const char* kLed = "led";
    static constexpr const char* kArrow = "arrow";
};

} // namespace glide90::ui
