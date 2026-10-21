#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <functional>
#include <optional>

namespace glide90::ui
{

// Alt-click (and Cmd-click on macOS) puts a control back to its default, as in most hosts.
bool wantsDefault(const juce::ModifierKeys& mods);

// A one-line text field drawn like the plugin's values (the dot matrix, or the readout font), on a
// screen's glass: the knobs' typed values and the preset names. It opens with everything selected,
// so typing replaces it. Return, Escape and losing the focus are reported.
class ReadoutField : public juce::Component, private juce::Timer
{
public:
    explicit ReadoutField(bool large = false);
    ~ReadoutField() override;

    void setText(const juce::String& text); // selects it all
    juce::String getText() const { return text; }
    // Shown centred (a knob's value) or from the left (a name).
    void setCentred(bool c) { centred = c; }
    int maxLength = 64;

    std::function<void()> onReturn, onEscape, onFocusLost;

    void paint(juce::Graphics&) override;
    bool keyPressed(const juce::KeyPress&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void focusGained(FocusChangeType) override;
    void focusLost(FocusChangeType) override;

private:
    void timerCallback() override;
    void insert(const juce::String& s);
    void moveCaret(int to);
    float textX() const;

    juce::String text;
    int caret = 0;
    bool allSelected = false, caretOn = true, large, centred = false;
};

// A knob for one parameter: its name above, its value below.
// Drag up/down (Shift: fine), mouse wheel, double-click to type a value (Return or a click elsewhere
// applies it, Escape cancels), Alt-click or right-click to reset.
class Knob : public juce::Component, public juce::SettableTooltipClient
{
public:
    // Hero: the one knob that defines a module, two rows tall. Large: the main ones. Small: the rest.
    enum class Size
    {
        Hero,
        Large,
        Small
    };

    Knob(juce::AudioProcessorValueTreeState& state, const char* paramId, const juce::String& label, Size size = Size::Small);
    ~Knob() override;

    void setLabel(const juce::String& text);
    const juce::String& getLabel() const { return label; }
    // Where the dial is drawn, in this knob's coordinates.
    juce::Rectangle<float> dialArea() const { return knobArea(); }
    // Lights the pointer up (0-1): PICK SENS on each pick heard.
    void setFlash(float amount);

    // Every knob takes one column of the grid and one row (a hero two), whatever its size, so that
    // names and values line up.
    static constexpr int kHeight = 100, kColumn = 88;

    void paint(juce::Graphics&) override;
    void mouseEnter(const juce::MouseEvent&) override;
    void mouseExit(const juce::MouseEvent&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
    void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    void mouseDoubleClick(const juce::MouseEvent&) override;

private:
    juce::Rectangle<float> knobArea() const;
    juce::Rectangle<int> valueArea() const;
    float snapped(float normalised) const;
    void closeEditor(bool apply);

    juce::RangedAudioParameter& param;
    juce::ParameterAttachment attachment;
    juce::String label;
    Size size;
    float value = 0.0f; // plain value
    float flash = 0.0f;
    float dragNormalised = 0.0f;
    juce::Point<float> lastDrag;
    bool hover = false, dragging = false;
    class FieldGlass;
    std::unique_ptr<FieldGlass> editorBox; // the glass the field sits on
    ReadoutField* editor = nullptr;        // in editorBox
    std::unique_ptr<juce::MouseListener> outsideClick; // after `editor`: goes first
    bool closingEditor = false;
};

// A choice parameter as a short list under its name, the selected item marked. Click an item, or
// use the wheel.
class ChoiceList : public juce::Component, public juce::SettableTooltipClient
{
public:
    // `labels` top to bottom; `values` the parameter value (choice index) of each item.
    ChoiceList(juce::AudioProcessorValueTreeState& state, const char* paramId, const juce::String& label,
               const juce::StringArray& labels, const juce::Array<int>& values);

    static constexpr int kRowHeight = 17;
    const juce::String& getLabel() const { return label; }
    int numRows() const { return labels.size(); }
    // Where row `row` is drawn (its mark and its name).
    juce::Rectangle<float> rowArea(int row) const;

    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

private:
    void select(int row);

    juce::RangedAudioParameter& param;
    juce::ParameterAttachment attachment;
    juce::String label;
    juce::StringArray labels;
    juce::Array<int> values;
    int selected = 0; // row
};

// A key that toggles a two-state parameter and shows its state. `inverted`: lit while the parameter
// is off (ON for a bypass).
class ToggleKey : public juce::TextButton
{
public:
    ToggleKey(juce::AudioProcessorValueTreeState& state, const char* paramId, const juce::String& label, bool inverted = false);

private:
    juce::RangedAudioParameter& param;
    juce::ParameterAttachment attachment;
    bool inverted;
};

// A key for a choice parameter that reads its current choice; a click takes the next one (after the
// last, the first). Its light is on while the choice is `litChoice`.
class CycleKey : public juce::TextButton
{
public:
    CycleKey(juce::AudioProcessorValueTreeState& state, const char* paramId, int litChoice);

    // Written before the choice's name on the key ("> " makes "> GATE").
    void setLegendPrefix(const juce::String& prefix);
    // Shows `choice` whatever the parameter says, dimmed and not clickable (something else decides
    // for now); nothing goes back to the parameter.
    void setForcedChoice(std::optional<int> choice);

private:
    void show();

    juce::AudioParameterChoice& param;
    juce::ParameterAttachment attachment;
    int lit;
    int current = 0;
    juce::String legendPrefix;
    std::optional<int> forced;
};

// A small screen: text, optional step arrows at its ends, a menu on click.
class GlassSelector : public juce::Component, public juce::SettableTooltipClient, private juce::Timer
{
public:
    explicit GlassSelector(bool withArrows, bool largeText = false);
    ~GlassSelector() override;

    std::function<juce::String()> getText;
    std::function<void(juce::PopupMenu&)> fillMenu;
    std::function<void(int)> onStep;

    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;

private:
    void timerCallback() override;

    bool arrows, large;
    juce::String shown;
};

// A level meter: a column of segments, the top one for clipping, with a falling peak.
class LevelMeter : public juce::Component
{
public:
    // Reads the peak (linear, 1 = full scale) since the last call.
    explicit LevelMeter(std::function<float()> takePeak);
    ~LevelMeter() override;

    // 12 segments: 11 from -40 dB up in even steps, the top one only at full scale (clipping).
    static constexpr int kSegments = 12;
    static constexpr float kFloorDb = -40.0f;
    // Where a scale mark for `db` goes, in this meter's coordinates: at the edge where that segment
    // lights; full scale at the middle of the top segment.
    float markY(float db) const;

    void paint(juce::Graphics&) override;

private:
    void frame(double seconds);

    std::function<float()> takePeak;
    float levelDb = -100.0f;
    int litShown = 0;
    double lastFrame = 0.0;
    juce::VBlankAttachment vblank { this, [this](double seconds) { frame(seconds); } };
};

} // namespace glide90::ui
