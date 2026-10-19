#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace glide90::ui
{

namespace colours
{
inline const juce::Colour panelTop { 0xff34363a }, panelBottom { 0xff222326 };
inline const juce::Colour swayTop { 0xff2a2f36 }, swayBottom { 0xff1b1f24 };
inline const juce::Colour legend { 0xffd9d9cf }, legendDim { 0xff8d8f8a };
inline const juce::Colour accent { 0xffe8a33a };
inline const juce::Colour keyTop { 0xff55585d }, keyBottom { 0xff3a3c40 };
inline const juce::Colour ledGreen { 0xff46f05a }, ledYellow { 0xffffcf3a }, ledRed { 0xffff3b2a };
inline const juce::Colour ledOff { 0xff2a2b2c };
} // namespace colours

juce::Font panelFont(float height, bool bold = false);

// Rubber keys, black knobs with a white pointer, LED toggles.
class PanelLookAndFeel : public juce::LookAndFeel_V4
{
public:
    PanelLookAndFeel();

    void drawRotarySlider(juce::Graphics&, int x, int y, int w, int h, float pos, float start, float end,
                          juce::Slider&) override;
    void drawButtonBackground(juce::Graphics&, juce::Button&, const juce::Colour&, bool highlighted,
                              bool down) override;
    void drawButtonText(juce::Graphics&, juce::TextButton&, bool highlighted, bool down) override;
    void drawToggleButton(juce::Graphics&, juce::ToggleButton&, bool highlighted, bool down) override;
    juce::Font getComboBoxFont(juce::ComboBox&) override { return panelFont(13.0f); }
    juce::Font getPopupMenuFont() override { return panelFont(14.0f); }
    juce::Font getLabelFont(juce::Label&) override { return panelFont(12.0f); }

    // Slider property: draw an arc from the start of the range to the value.
    static constexpr const char* kArc = "arc";
};

// A round LED.
void drawLed(juce::Graphics&, juce::Rectangle<float> area, juce::Colour colour, bool on);

} // namespace glide90::ui
