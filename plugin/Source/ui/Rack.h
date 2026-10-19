#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace glide90::ui
{

// The rack the units are mounted in: each child is a unit, and the rack paints its ears on both
// sides, with their oval mounting slots.
class Rack : public juce::Component
{
public:
    static constexpr int kEarWidth = 34;

    void paint(juce::Graphics&) override;
};

} // namespace glide90::ui
