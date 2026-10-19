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
// points (double-click adds or removes a point). In MANUAL mode it shows a scrolling trace of the
// bar instead.
class ShapeDisplay : public juce::Component, private juce::Timer
{
public:
    explicit ShapeDisplay(Glide90Processor& p);
    ~ShapeDisplay() override;

    void paint(juce::Graphics&) override;
    void resized() override;
    void mouseMove(const juce::MouseEvent&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
    void mouseDoubleClick(const juce::MouseEvent&) override;

private:
    void timerCallback() override;
    juce::Rectangle<float> plotArea() const;
    juce::Point<float> toScreen(float x, float push) const;
    juce::Point<float> fromScreen(juce::Point<float> p) const; // (x, push)
    int pointAt(juce::Point<float> p) const;
    bool pushIsDown() const;
    bool editable() const;
    float pushFromCents(float cents) const;
    juce::String rateText() const;
    void editPoints(const std::function<void(std::vector<sway::ShapePoint>&)>& edit);

    Glide90Processor& proc;
    juce::ComboBox shapeBox;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> shapeAttachment;

    sway::Shape shape;
    float phase = 0.0f, cents = 0.0f;
    int dragIndex = -1, hoverIndex = -1;
    std::array<float, 240> trace {};
    int traceHead = 0;
};

} // namespace glide90::ui
