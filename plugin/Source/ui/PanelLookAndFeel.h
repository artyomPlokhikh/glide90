#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace glide90::ui
{

namespace colours
{
inline const juce::Colour header { 0xff121314 };
inline const juce::Colour stripTop { 0xff1d1d1f }, stripBottom { 0xff0e0e0f };
inline const juce::Colour spxTop { 0xff223042 }, spxBottom { 0xff161f2b };
inline const juce::Colour glideTop { 0xff17332f }, glideBottom { 0xff0e221f };
inline const juce::Colour legend { 0xffefe6d2 }, legendDim { 0xff9a9888 };
inline const juce::Colour spxAccent { 0xfff2a23a }, glideAccent { 0xffff8a3d };
inline const juce::Colour keyTop { 0xff3a3b3e }, keyBottom { 0xff252628 };
inline const juce::Colour ledGreen { 0xff46f05a }, ledYellow { 0xffffcf3a }, ledRed { 0xffff3b2a };
inline const juce::Colour ledOff { 0xff2a2b2c };
inline const juce::Colour screen { 0xff07090a };
} // namespace colours

juce::Font panelFont(float height, bool bold = false);

// Cream knobs with a coloured value arc, rubber keys, LED toggles.
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
    juce::Font getComboBoxFont(juce::ComboBox&) override { return panelFont(13.0f, true); }
    juce::Font getPopupMenuFont() override { return panelFont(14.0f); }
    juce::Font getLabelFont(juce::Label&) override { return panelFont(12.5f, true); }
    juce::Label* createSliderTextBox(juce::Slider&) override;

    // Slider properties: the arc colour ("accent", a Colour as int64) and whether the arc starts
    // from the centre ("bipolar").
    static constexpr const char* kAccent = "accent";
    static constexpr const char* kBipolar = "bipolar";
};

// A round LED.
void drawLed(juce::Graphics&, juce::Rectangle<float> area, juce::Colour colour, bool on);

} // namespace glide90::ui
