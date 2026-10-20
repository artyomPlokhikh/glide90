#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

namespace glide90::ui
{

// The looks, all after the Loveless sleeve (a pink-red wash, blur, grain). CRIMSON: a black panel
// lit pink. SMEAR: a blurred wash that the bar drags sideways. DUOTONE: a pink and red halftone
// print. INSTANT: a faded instant photo in its white frame.
enum class ThemeId
{
    Crimson,
    Smear,
    Duotone,
    Instant
};
inline constexpr int kThemeCount = 4;

struct Theme
{
    enum class Knob
    {
        Rack,     // matte black with a grip bar and a lit pointer
        Arc,      // a thin arc and a dot, no body
        Bakelite, // a cream knob
        Flat      // a printed disc with an outline, slightly off register
    };
    enum class Face
    {
        Condensed, // Barlow Condensed
        Barlow,
        Courier // Courier Prime
    };
    enum class Backdrop
    {
        Matte,
        Smear,
        Duotone,
        Instant
    };

    ThemeId id;
    const char* name; // in the menu
    const char* key;  // in GLIDE90_THEME and snapshot file names
    juce::Colour base;              // the window behind everything
    juce::Colour card, cardEdge;    // the modules' backing
    juce::Colour text, textDim;     // legends and values
    juce::Colour accent;            // what is lit outside the screens: a value being changed, meters
    juce::Colour glass, glassEdge;  // screens
    juce::Colour ink, inkDim;       // what the screens show
    juce::Colour menu;              // opaque: menus, dialogs, tooltips (their text is `ink`)
    juce::Colour keyFill, keyEdge, keyText, keyOnFill, keyOnText;
    juce::Colour knobFace, knobPointer, knobTrack;
    Knob knob;
    Face face;
    Backdrop backdrop;
    bool matrix;    // screens and values in the dot matrix
    bool glow;      // lines and big text on the screens glow
    bool lowercase; // legends and values in lower case
    float cardCorner;
};

const Theme& theme();
const Theme& themeAt(int index);
void setTheme(ThemeId id);
// The look a new window opens with, once per process and only if none was set yet: GLIDE90_THEME
// (a theme's key, "crimson", "haze", ..., or its number from 1), else the one last chosen in the
// menu, else Crimson.
void initialiseTheme();
// Keeps the current look for the next windows and sessions (a setting of the user, not of a project).
void rememberTheme();

// What the sound is doing, for the looks' motion: the editor fills it in once per frame.
struct Motion
{
    double time = 0.0;
    float cents = 0.0f;      // the bar now
    float bend = 0.0f;       // how far the bar is pushed, 0-1 (60 cents or more is 1)
    float swell = 0.0f;      // the reverse swell's level, 0-1, against the loudest lately
    float progress = -1.0f;  // 0-1 through the gate since the swell began to rise; -1 between swells
    float sinceCut = 1.0e6f; // seconds since the last swell was cut
    float room = 20.0f;      // ROOM SIZE
    float gateSeconds = 1.44f;
};

// How much moves: FULL, SMEAR's background too; SUBTLE, only the light running through the gate's
// screen; OFF, nothing but what the screens show of the sound. A setting of the user, kept like the
// look.
enum class MotionLevel
{
    Full,
    Subtle,
    Off
};
MotionLevel motionLevel();
void setMotionLevel(MotionLevel);

// HOLD's strip folded down to its title bar (the window shorter): a setting of the user, kept like
// the look.
bool holdFolded();
void setHoldFolded(bool);

// Text in the theme's case: legends, values and the screens' text all go through it.
juce::String themed(const juce::String& text);

// Legends.
juce::Font legendFont(float height, bool strong = false);
// Values and the screens' text, where they are not in the dot matrix.
juce::Font readoutFont(float height, bool strong = false);

// The two sizes of dot-matrix text: large for the preset name and the bar's reading, small for
// everything else.
inline constexpr float kLargeTextPitch = 2.0f, kSmallTextPitch = 1.5f;

// Text in a 5x7 dot matrix. `pitch` is the dot spacing; returns the width used.
float drawMatrixText(juce::Graphics&, const juce::String& text, juce::Rectangle<float> area, float pitch,
                     juce::Colour colour, juce::Justification justification = juce::Justification::centredLeft);
float matrixTextWidth(const juce::String& text, float pitch);

// A value or a screen's text: in the dot matrix or in the readout font, as the theme says, and in
// its case. `glow` adds the theme's halo (where it has one). Returns the width used.
float drawReadout(juce::Graphics&, const juce::String& text, juce::Rectangle<float> area, juce::Colour colour,
                  juce::Justification justification = juce::Justification::centredLeft, bool large = false,
                  bool glow = false);
float readoutWidth(const juce::String& text, bool large = false);

// A line on a screen, with the theme's halo under it.
void strokeGlowing(juce::Graphics&, const juce::Path&, juce::Colour, float width);

// A module's backing; `lifted` while it is being dragged.
void drawCard(juce::Graphics&, juce::Rectangle<float> area, bool lifted = false);
void drawCardTitle(juce::Graphics&, const juce::String& title, juce::Rectangle<float> area);
// A rule across a group of controls with the group's name in a gap in the middle (`quiet`: the
// group does nothing in the current mode).
void drawGroupRule(juce::Graphics&, const juce::String& name, float x, float width, float y, bool quiet = false);
// A screen's window.
void drawGlass(juce::Graphics&, juce::Rectangle<float> area, float corner = 4.0f);
// One segment of a meter.
void drawLed(juce::Graphics&, juce::Rectangle<float> area, juce::Colour colour, bool on);
// A knob in the theme's style, pointing at `position` (0-1 over +-135 degrees); with `scale`, a
// ring of ticks round it.
void drawKnob(juce::Graphics&, juce::Rectangle<float> area, float position, bool enabled, bool large, bool scale = true);

// The window's background, behind all the modules: matte, a blurred wash, a moving wash or a
// halftone print. Built once per size and theme; the SMEAR wash moves with the bar. While a module
// is dragged it also shows where the module will land and the shadow of the lifted one.
class Backdrop : public juce::Component
{
public:
    Backdrop();

    // Once per frame: what the sound does now (SMEAR's wash follows the bar).
    void setMotion(const Motion& m);
    // Where a dragged module will land and where it is now (empty: no drag).
    void setDragGuides(juce::Rectangle<int> landing, juce::Rectangle<int> lifted);
    // Call after the theme changed.
    void themeChanged();

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    // The still background at `scale` physical pixels per point, so painting it is a plain copy;
    // and SMEAR's streaks, at full size, so a frame only moves them.
    void render(float scale);
    float smearOffset() const;
    juce::String motionKey() const;

    juce::Image composite, streaks, shadow;
    float compositeScale = 0.0f;
    Motion motion;
    juce::String shownKey;
    juce::Rectangle<int> landing, lifted;
};

class PanelLookAndFeel : public juce::LookAndFeel_V4
{
public:
    // Side margin of the modules' content.
    static constexpr int kMargin = 16;

    PanelLookAndFeel();
    // Sets the colours of menus, dialogs, tooltips and text fields from the theme.
    void applyTheme();

    void drawButtonBackground(juce::Graphics&, juce::Button&, const juce::Colour&, bool highlighted, bool down) override;
    void drawButtonText(juce::Graphics&, juce::TextButton&, bool highlighted, bool down) override;
    juce::Font getPopupMenuFont() override { return legendFont(16.0f); }
    juce::Font getAlertWindowTitleFont() override { return legendFont(18.0f, true); }
    juce::Font getAlertWindowMessageFont() override { return legendFont(15.0f); }
    juce::Font getAlertWindowFont() override { return legendFont(15.0f); }
    juce::Font getTextButtonFont(juce::TextButton&, int) override { return legendFont(13.0f, true); }
    void drawPopupMenuBackground(juce::Graphics&, int width, int height) override;
    void drawPopupMenuItem(juce::Graphics&, const juce::Rectangle<int>& area, bool isSeparator, bool isActive,
                           bool isHighlighted, bool isTicked, bool hasSubMenu, const juce::String& text,
                           const juce::String& shortcutKeyText, const juce::Drawable* icon,
                           const juce::Colour* textColour) override;
    void drawPopupMenuSectionHeader(juce::Graphics&, const juce::Rectangle<int>& area, const juce::String& name) override;
    void getIdealPopupMenuItemSize(const juce::String& text, bool isSeparator, int standardHeight, int& width,
                                   int& height) override;
    int getPopupMenuBorderSize() override { return 5; }

    // Button properties: "led" (the key shows its toggle state), "icon" ("undo" or "redo": an arrow
    // instead of the text) and "segment" ("left" or "right": one half of a two-way switch, lit when
    // its toggle state is on).
    static constexpr const char* kLed = "led";
    static constexpr const char* kIcon = "icon";
    static constexpr const char* kSegment = "segment";
    // "onMenu": a key on a dialog's card, in the menus' colours rather than the panel's.
    static constexpr const char* kOnMenu = "onMenu";

    juce::Rectangle<int> getTooltipBounds(const juce::String& text, juce::Point<int> screenPos, juce::Rectangle<int> parentArea) override;
    void drawTooltip(juce::Graphics&, const juce::String& text, int width, int height) override;
};

} // namespace glide90::ui
