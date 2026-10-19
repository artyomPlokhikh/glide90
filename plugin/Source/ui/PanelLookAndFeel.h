#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace glide90::ui
{

// The look of a Yamaha rack unit: anodized black, grey silkscreen in a narrow grotesque, square
// keys, red LEDs only where a state matters, and green only behind the display glass.
namespace colours
{
inline const juce::Colour anodized { 0xff17181a }, anodizedDark { 0xff111214 };
inline const juce::Colour silkscreen { 0xffc4c4bd }, silkscreenDim { 0xff77786f };
inline const juce::Colour display { 0xff4fc79a }, displayDim { 0xff2c6b55 };
inline const juce::Colour glass { 0xff0c1210 }, glassEdge { 0xff26292a };
inline const juce::Colour ledRed { 0xffff2b1c }, ledOff { 0xff3b1714 };
inline const juce::Colour ledGreen { 0xff3fd36a }, ledYellow { 0xfff2c53a };
inline const juce::Colour memoryRed { 0xffff3a22 };
} // namespace colours

// Barlow Condensed (panel legends) and DSEG7 (the memory number), embedded.
juce::Font legendFont(float height, bool semiBold = false);
juce::Font segmentFont(float height);

// The two sizes of dot-matrix text outside the LCD: large for the preset name and the bar's reading,
// small for everything else.
inline constexpr float kLargeTextPitch = 2.0f, kSmallTextPitch = 1.5f;

// Text in the LCD's 5x7 dot matrix, for everything behind glass. `pitch` is the dot spacing;
// returns the width used.
float drawMatrixText(juce::Graphics&, const juce::String& text, juce::Rectangle<float> area, float pitch,
                     juce::Colour colour, juce::Justification justification = juce::Justification::centredLeft);
float matrixTextWidth(const juce::String& text, float pitch);

// A unit's face: anodized black with a fine vertical grain and engraved edges.
void drawRackUnit(juce::Graphics&, juce::Rectangle<int> bounds);
// The unit's name in the top left corner, small, as printed on a panel.
void drawUnitName(juce::Graphics&, const juce::String& name, juce::Rectangle<int> unit);
// An engraved vertical line separating groups of controls.
void drawEngravedLine(juce::Graphics&, float x, float top, float bottom);
// An engraved horizontal line.
void drawEngravedRule(juce::Graphics&, float y, float left, float right);
// A smoked-glass window.
void drawGlass(juce::Graphics&, juce::Rectangle<float> area, float corner = 2.0f);
// The small red LED of a key.
void drawKeyLed(juce::Graphics&, juce::Point<float> centre, bool on);
// A level meter LED.
void drawLed(juce::Graphics&, juce::Rectangle<float> area, juce::Colour colour, bool on);
// A knob: knurled skirt, machined cap, a thin printed line, and (with `scale`) a ring of ticks.
void drawKnob(juce::Graphics&, juce::Rectangle<float> area, float position, bool enabled, bool large, bool scale = true);

class PanelLookAndFeel : public juce::LookAndFeel_V4
{
public:
    // Side margin of the panels' content.
    static constexpr int kMargin = 28;

    PanelLookAndFeel();

    void drawButtonBackground(juce::Graphics&, juce::Button&, const juce::Colour&, bool highlighted,
                              bool down) override;
    void drawButtonText(juce::Graphics&, juce::TextButton&, bool highlighted, bool down) override;
    juce::Font getPopupMenuFont() override { return legendFont(16.0f); }
    juce::Font getAlertWindowTitleFont() override { return legendFont(18.0f, true); }
    juce::Font getAlertWindowMessageFont() override { return legendFont(15.0f); }
    juce::Font getAlertWindowFont() override { return legendFont(15.0f); }
    juce::Font getTextButtonFont(juce::TextButton&, int) override { return legendFont(13.0f, true); }
    // Menus look like the displays: smoked glass, dot-matrix items, silkscreen section headings.
    void drawPopupMenuBackground(juce::Graphics&, int width, int height) override;
    void drawPopupMenuItem(juce::Graphics&, const juce::Rectangle<int>& area, bool isSeparator, bool isActive,
                           bool isHighlighted, bool isTicked, bool hasSubMenu, const juce::String& text,
                           const juce::String& shortcutKeyText, const juce::Drawable* icon,
                           const juce::Colour* textColour) override;
    void drawPopupMenuSectionHeader(juce::Graphics&, const juce::Rectangle<int>& area, const juce::String& name) override;
    void getIdealPopupMenuItemSize(const juce::String& text, bool isSeparator, int standardHeight, int& width,
                                   int& height) override;
    int getPopupMenuBorderSize() override { return 5; }

    // Button properties: "led" (a red LED that shows the toggle state) and "arrow" (+1 up, -1 down:
    // a hollow triangle instead of text, as on the unit's data keys).
    static constexpr const char* kLed = "led";
    static constexpr const char* kArrow = "arrow";
};

} // namespace glide90::ui
