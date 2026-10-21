#pragma once

#include "Controls.h"
#include "Parameters.h"

namespace glide90
{
class Glide90Processor;
}

namespace glide90::ui
{

// Text a routing diagram draws, where, and whether it is faint on purpose (that path does nothing
// now), for the legibility test.
struct RoutingLabel
{
    juce::String text;
    juce::Rectangle<float> area;
    bool dimmed;
};

// Which modules are on: a module that is off draws its path faint and dashed; with GLIDE off nothing
// bends, the GLIDE marks are only outlined; with REVERSE GATE off HOLD goes to OUT.
struct RoutingModules
{
    bool glideOn = true, gateOn = true, holdOn = true;
};

// Draws routing `r` in `area`: IN splits three ways, DRY straight through, REVERSE GATE, HOLD rising
// into REVERSE GATE or running on to OUT; a GLIDE mark where GLIDE bends a path. Every module is named
// as on its panel (names::). `small`: the chart's version, without the IN, OUT and DRY words.
// `background` fills the boxes, `ink` draws the lines and words (the panel's text on the strip, the
// menus' ink on the chart). With no Graphics, only says what it would write.
std::vector<RoutingLabel> drawRouting(juce::Graphics* g, juce::Rectangle<float> area, const Routing& r,
                                      RoutingModules modules, bool small, juce::Colour background, juce::Colour ink);

// Every routing on a card of its own, over the window under the ROUTING strip: a click on one takes it,
// a click anywhere else or Escape closes the chart.
class RoutingChart : public juce::Component
{
public:
    RoutingChart(Glide90Processor& p, juce::Rectangle<int> card);

    // Where routing `index`'s card is.
    juce::Rectangle<float> cardArea(int index) const;
    // Clicks routing `index`'s card, as the mouse would.
    void choose(int index);
    // Called when the chart should go (after a choice, Escape or a click outside).
    std::function<void()> onClose;

    void paint(juce::Graphics&) override;
    void mouseMove(const juce::MouseEvent&) override;
    void mouseExit(const juce::MouseEvent&) override;
    void mouseDown(const juce::MouseEvent&) override;
    bool keyPressed(const juce::KeyPress&) override;
    bool hitTest(int, int) override { return true; }

private:
    int cardAt(juce::Point<float> p) const;

    Glide90Processor& proc;
    juce::Rectangle<int> panel;
    int hover = -1;
};

// The ROUTING strip under the header, beside PICK SENS: on the left the routing's name and what it
// does, stepped with the arrows; a click on the name opens the chart of all of them. On the right the
// routing's diagram, which only shows.
class RoutingPanel : public juce::Component, public juce::TooltipClient
{
public:
    explicit RoutingPanel(Glide90Processor& p);
    ~RoutingPanel() override;

    static constexpr int kHeight = 68;

    juce::Rectangle<float> selectorArea() const { return { 10.0f, 9.0f, 366.0f, 50.0f }; }
    juce::Rectangle<float> diagramArea() const;

    int current() const;
    // The previous or next routing (wrapping round), as the arrows do.
    void step(int delta);
    void openChart();
    void closeChart();
    RoutingChart* chart() const { return chartWindow.get(); }

    std::vector<RoutingLabel> labels() const;

    void paint(juce::Graphics&) override;
    void mouseMove(const juce::MouseEvent&) override;
    void mouseExit(const juce::MouseEvent&) override;
    void mouseDown(const juce::MouseEvent&) override;
    juce::String getTooltip() override;

private:
    enum class Spot
    {
        Nowhere,
        Previous,
        Next,
        Name
    };
    Spot spotAt(juce::Point<float> p) const;
    RoutingModules modules() const;
    juce::String nameText() const;
    void frame();

    Glide90Processor& proc;
    Spot hover = Spot::Nowhere;
    int shownRouting = -1;
    RoutingModules shownModules;
    std::unique_ptr<RoutingChart> chartWindow;
    juce::VBlankAttachment vblank { this, [this](double) { frame(); } };
};

} // namespace glide90::ui
