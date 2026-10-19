#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace glide90::ui
{

// The look of a Yamaha rack unit: a matte panel, light grey silkscreen in a narrow grotesque, raised
// keys casting soft shadows, red LEDs only where a state matters, and the display's yellow-green
// behind the glass.
namespace colours
{
inline const juce::Colour panel { 0xff1d1e21 }, key { 0xff222224 };
inline const juce::Colour silkscreen { 0xffcfcfc8 }, silkscreenDim { 0xff83847c }, keyLegend { 0xffe4e4e2 };
inline const juce::Colour display { 0xff9cb478 }, displayDim { 0xff4f6238 }, displayUnlit { 0xff25371a };
inline const juce::Colour lcd { 0xff182418 }, glass { 0xff142014 }, glassEdge { 0xff2a2c2e }, window { 0xff0c0c0d };
inline const juce::Colour ledRed { 0xffff3b2c }, ledOff { 0xff4e4f52 };
inline const juce::Colour ledGreen { 0xff7ecc66 }, ledYellow { 0xfff2c53a };
inline const juce::Colour memoryRed { 0xffff2440 };
inline const juce::Colour logoTeal { 0xff50c0a8 };
} // namespace colours

// Barlow Condensed (panel legends), Liberation Sans (key legends, as on the unit) and DSEG7 (the
// memory number), embedded.
juce::Font legendFont(float height, bool semiBold = false);
juce::Font keyFont(float height);
juce::Font segmentFont(float height);

// The two sizes of dot-matrix text outside the LCD: large for the preset name and the bar's reading,
// small for everything else.
inline constexpr float kLargeTextPitch = 2.0f, kSmallTextPitch = 1.5f;

// Text in the LCD's 5x7 dot matrix, for everything behind glass. `pitch` is the dot spacing;
// returns the width used.
float drawMatrixText(juce::Graphics&, const juce::String& text, juce::Rectangle<float> area, float pitch,
                     juce::Colour colour, juce::Justification justification = juce::Justification::centredLeft);
float matrixTextWidth(const juce::String& text, float pitch);

// A unit's face: the panel's finish, lit from above, with engraved edges.
void drawRackUnit(juce::Graphics&, juce::Rectangle<int> bounds);
// The finish alone, over any area.
void fillPanel(juce::Graphics&, juce::Rectangle<float> area);
// An oval mounting slot of a rack ear.
void drawRackSlot(juce::Graphics&, juce::Rectangle<float> area);
// The unit's name in the top left corner, small, as printed on a panel.
void drawUnitName(juce::Graphics&, const juce::String& name, juce::Rectangle<int> unit);
// An engraved vertical line separating groups of controls.
void drawEngravedLine(juce::Graphics&, float x, float top, float bottom);
// An engraved horizontal line.
void drawEngravedRule(juce::Graphics&, float y, float left, float right);
// A smoked-glass window: an LCD's (green-black) unless told otherwise.
void drawGlass(juce::Graphics&, juce::Rectangle<float> area, float corner = 2.0f, juce::Colour fill = colours::glass);
// The small red LED bar of a key.
void drawKeyLed(juce::Graphics&, juce::Point<float> centre, bool on, float width = 14.0f);
// A level meter LED.
void drawLed(juce::Graphics&, juce::Rectangle<float> area, juce::Colour colour, bool on);
// A knob as on the unit: smooth matte black, a grip bar across its flat top with a white line at
// the pointing end, and (with `scale`) a ring of printed ticks.
void drawKnob(juce::Graphics&, juce::Rectangle<float> area, float position, bool enabled, bool large, bool scale = true);

class PanelLookAndFeel : public juce::LookAndFeel_V4
{
public:
    // Side margin of the panels' content.
    static constexpr int kMargin = 28;
    // A key's bounds are its cap plus room for its shadow on the panel, down and to the right.
    static inline const juce::BorderSize<int> kKeyShadow { 1, 2, 5, 4 };
    static juce::Rectangle<int> keyBounds(juce::Rectangle<int> cap) { return kKeyShadow.addedTo(cap); }

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
    // a hollow arrow instead of text, as on the unit's data and memory keys).
    static constexpr const char* kLed = "led";
    static constexpr const char* kArrow = "arrow";
};

} // namespace glide90::ui
