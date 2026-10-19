#pragma once

#include "Shape.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <array>

namespace glide90
{
class Glide90Processor;
}

namespace glide90::ui
{

// The Glide screen: one cycle of the bar's movement with a playhead, editable by dragging its
// points (double-click adds or removes a point, right-click removes). The shape's name at the top
// steps with its arrows or opens a menu. In MANUAL mode it shows a scrolling trace of the bar.
class ShapeDisplay : public juce::Component, public juce::SettableTooltipClient, private juce::Timer
{
public:
    explicit ShapeDisplay(Glide90Processor& p);
    ~ShapeDisplay() override;

    void paint(juce::Graphics&) override;
    void mouseMove(const juce::MouseEvent&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
    void mouseDoubleClick(const juce::MouseEvent&) override;

private:
    void timerCallback() override;
    juce::Rectangle<float> plotArea() const;
    juce::Rectangle<float> nameArea() const;
    juce::Point<float> toScreen(float x, float push) const;
    juce::Point<float> fromScreen(juce::Point<float> p) const; // (x, push)
    int pointAt(juce::Point<float> p) const;
    int choice(const char* id) const;
    bool pushIsDown() const;
    bool editable() const;
    float pushFromCents(float cents) const;
    juce::String statusText() const;
    juce::String settingsKey() const;
    void stepShape(int delta);
    void editPoints(const std::function<void(std::vector<sway::ShapePoint>&)>& edit);

    Glide90Processor& proc;
    juce::RangedAudioParameter& shapeParam;
    sway::Shape shape;
    float phase = 0.0f, cents = 0.0f;
    int dragIndex = -1, hoverIndex = -1;
    bool editing = false; // a drag under way: one undo step
    std::array<float, 240> trace {};
    int traceHead = 0;
    int seenAttacks = 0;
    juce::uint32 attackFlashUntil = 0;
    juce::String shownSettings;
    bool shownFlash = false;
};

} // namespace glide90::ui
