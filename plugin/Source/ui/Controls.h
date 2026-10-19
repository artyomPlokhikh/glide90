#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <functional>

namespace glide90::ui
{

// Alt-click (and Cmd-click on macOS) puts a control back to its default, as in most hosts.
bool wantsDefault(const juce::ModifierKeys& mods);

// A knob for one parameter: its name above, its value below in the display's dot matrix.
// Drag up/down (Shift: fine), mouse wheel, double-click to type a value (Return or a click elsewhere
// applies it, Escape cancels), Alt-click or right-click to reset.
class Knob : public juce::Component, public juce::SettableTooltipClient
{
public:
    enum class Size
    {
        Large,
        Small
    };

    Knob(juce::AudioProcessorValueTreeState& state, const char* paramId, const juce::String& label, Size size = Size::Small);
    ~Knob() override;

    void setLabel(const juce::String& text);
    // Called when the knob is grabbed, turned or typed into.
    std::function<void(juce::RangedAudioParameter&)> onTouch;

    // Every knob is the same height, so that names and values line up along a row whatever the size.
    static constexpr int kHeight = 100;
    static int widthFor(Size s) { return s == Size::Large ? 86 : 72; }

    void paint(juce::Graphics&) override;
    void mouseEnter(const juce::MouseEvent&) override;
    void mouseExit(const juce::MouseEvent&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
    void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    void mouseDoubleClick(const juce::MouseEvent&) override;
    juce::String getTooltip() override;

private:
    juce::Rectangle<float> knobArea() const;
    juce::Rectangle<int> valueArea() const;
    float snapped(float normalised) const;
    void touch();
    void closeEditor(bool apply);

    juce::RangedAudioParameter& param;
    juce::ParameterAttachment attachment;
    juce::String label;
    Size size;
    float value = 0.0f; // plain value
    float dragNormalised = 0.0f;
    juce::Point<float> lastDrag;
    bool hover = false, dragging = false;
    std::unique_ptr<juce::TextEditor> editor;
    std::unique_ptr<juce::MouseListener> outsideClick; // after `editor`: goes first
    bool closingEditor = false;
};

// A rotary selector switch: a small knob pointing at one of its printed positions. Click a
// position, click the knob to step, or use the wheel.
class SelectorSwitch : public juce::Component, public juce::SettableTooltipClient
{
public:
    SelectorSwitch(juce::AudioProcessorValueTreeState& state, const char* paramId, const juce::String& label,
                   const juce::StringArray& positions = {});

    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

private:
    float angleOf(int position) const;
    juce::Point<float> knobCentre() const;
    float knobRadius() const;
    juce::Rectangle<float> labelArea(int position) const;
    void select(int position);

    juce::RangedAudioParameter& param;
    juce::ParameterAttachment attachment;
    juce::String label;
    juce::StringArray positions;
    int selected = 0;
};

// A bat-lever toggle switch with two or three positions, labelled top to bottom.
class ToggleSwitch : public juce::Component, public juce::SettableTooltipClient
{
public:
    // `labels` top to bottom; `values` the parameter value (choice index or 0/1) of each position.
    ToggleSwitch(juce::AudioProcessorValueTreeState& state, const char* paramId, const juce::String& label,
                 const juce::StringArray& labels, const juce::Array<int>& values);

    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;

private:
    float positionY(int position) const;
    juce::Point<float> pivot() const;

    juce::RangedAudioParameter& param;
    juce::ParameterAttachment attachment;
    juce::String label;
    juce::StringArray labels;
    juce::Array<int> values;
    int selected = 0; // position, top to bottom
};

// A key with an LED that toggles a two-state parameter.
class ToggleKey : public juce::TextButton
{
public:
    ToggleKey(juce::AudioProcessorValueTreeState& state, const char* paramId, const juce::String& label);

private:
    juce::RangedAudioParameter& param;
    juce::ParameterAttachment attachment;
};

// A horizontal slide switch with its positions printed under the slot, for the rear panel.
class SlideSwitch : public juce::Component, public juce::SettableTooltipClient
{
public:
    SlideSwitch(juce::AudioProcessorValueTreeState& state, const char* paramId, const juce::String& label,
                const juce::StringArray& positions);

    // Width that fits the legend and the positions.
    int idealWidth() const;

    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;

private:
    juce::Rectangle<float> slot() const;
    float legendWidth() const;

    juce::RangedAudioParameter& param;
    juce::ParameterAttachment attachment;
    juce::String label;
    juce::StringArray positions;
    int selected = 0;
};

// A small display window: dot-matrix text, optional step arrows at its ends, a menu on click.
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

} // namespace glide90::ui
