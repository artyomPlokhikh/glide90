#include "ShapeDisplay.h"

#include "PanelLookAndFeel.h"
#include "Parameters.h"
#include "PluginProcessor.h"

namespace glide90::ui
{
namespace
{

constexpr float kPointRadius = 5.5f;

int choiceOf(Glide90Processor& p, const char* id)
{
    return juce::roundToInt(p.state().getRawParameterValue(id)->load());
}

} // namespace

ShapeDisplay::ShapeDisplay(Glide90Processor& p) : proc(p)
{
    if (auto* choice = dynamic_cast<juce::AudioParameterChoice*>(p.state().getParameter(ids::glideShape)))
        shapeBox.addItemList(choice->choices, 1);
    shapeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(p.state(), ids::glideShape,
                                                                                               shapeBox);
    shapeBox.setTooltip("Shape of one cycle. Drag the points on the screen to make your own");
    addAndMakeVisible(shapeBox);
    shape = proc.currentShape();
    startTimerHz(60);
}

ShapeDisplay::~ShapeDisplay()
{
    stopTimer();
}

bool ShapeDisplay::pushIsDown() const
{
    // Pushing the bar is drawn downwards unless the direction is UP.
    return (sway::BendDirection) choiceOf(proc, ids::glideDirection) != sway::BendDirection::Up;
}

bool ShapeDisplay::editable() const
{
    return (sway::GlideMode) choiceOf(proc, ids::glideMode) != sway::GlideMode::Manual;
}

juce::Rectangle<float> ShapeDisplay::plotArea() const
{
    return getLocalBounds().toFloat().reduced(14.0f, 0.0f).withTrimmedTop(40.0f).withTrimmedBottom(26.0f);
}

juce::Point<float> ShapeDisplay::toScreen(float x, float push) const
{
    const auto r = plotArea();
    const float fy = pushIsDown() ? push : 1.0f - push;
    return { r.getX() + x * r.getWidth(), r.getY() + fy * r.getHeight() };
}

juce::Point<float> ShapeDisplay::fromScreen(juce::Point<float> p) const
{
    const auto r = plotArea();
    const float x = juce::jlimit(0.0f, 0.999f, (p.x - r.getX()) / r.getWidth());
    const float fy = juce::jlimit(0.0f, 1.0f, (p.y - r.getY()) / r.getHeight());
    return { x, pushIsDown() ? fy : 1.0f - fy };
}

float ShapeDisplay::pushFromCents(float c) const
{
    const float depth = juce::jmax(0.1f, proc.state().getRawParameterValue(ids::glideDepth)->load());
    switch ((sway::BendDirection) choiceOf(proc, ids::glideDirection))
    {
        case sway::BendDirection::Down: return juce::jlimit(0.0f, 1.0f, -c / depth);
        case sway::BendDirection::Up: return juce::jlimit(0.0f, 1.0f, c / depth);
        case sway::BendDirection::Both: default: return juce::jlimit(0.0f, 1.0f, 0.5f * (1.0f - c / depth));
    }
}

int ShapeDisplay::pointAt(juce::Point<float> p) const
{
    int best = -1;
    float bestDist = kPointRadius * 2.2f;
    for (int i = 0; i < shape.size(); ++i)
    {
        const float d = p.getDistanceFrom(toScreen(shape.point(i).x, shape.point(i).y));
        if (d < bestDist)
        {
            bestDist = d;
            best = i;
        }
    }
    return best;
}

juce::String ShapeDisplay::rateText() const
{
    const auto mode = (sway::GlideMode) choiceOf(proc, ids::glideMode);
    const auto ms = juce::String(juce::roundToInt(proc.state().getRawParameterValue(ids::glidePeriod)->load())) + " ms";
    switch (mode)
    {
        case sway::GlideMode::Free: return "FREE  " + ms;
        case sway::GlideMode::Sync:
            return "SYNC  " + juce::String(kDivisions[juce::jlimit(0, (int) std::size(kDivisions) - 1,
                                                                    choiceOf(proc, ids::glideDivision))].label)
                   + "  @ " + juce::String(proc.hostBpm(), 1) + " BPM";
        case sway::GlideMode::Trigger:
            return juce::String((TriggerSource) choiceOf(proc, ids::glideTriggerSource) == TriggerSource::Pick
                                    ? "RESTART ON PICK ATTACK  "
                                    : "RESTART ON MIDI NOTE  ")
                   + ms;
        case sway::GlideMode::Manual:
        default:
        {
            static const char* sources[] { "BAR KNOB", "PITCH BEND", "MOD WHEEL", "EXPRESSION CC11" };
            return juce::String("MANUAL  ") + sources[juce::jlimit(0, 3, choiceOf(proc, ids::glideManualSource))];
        }
    }
}

void ShapeDisplay::timerCallback()
{
    shape = proc.currentShape();
    phase = proc.glidePhase();
    cents = proc.barCents();
    trace[(size_t) traceHead] = pushFromCents(cents);
    traceHead = (traceHead + 1) % (int) trace.size();
    repaint();
}

void ShapeDisplay::paint(juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();
    drawGlass(g, bounds, 3.0f);

    const auto r = plotArea();
    const bool on = proc.state().getRawParameterValue(ids::glideOn)->load() > 0.5f;
    const auto accent = colours::vfd.withAlpha(on ? 1.0f : 0.35f);

    // Grid: quarters of the cycle, and the rest line (no bend).
    g.setColour(colours::vfd.withAlpha(0.07f));
    for (int i = 1; i < 4; ++i)
        g.drawVerticalLine((int) (r.getX() + r.getWidth() * (float) i / 4.0f), r.getY(), r.getBottom());
    for (int i = 1; i < 4; ++i)
        g.drawHorizontalLine((int) (r.getY() + r.getHeight() * (float) i / 4.0f), r.getX(), r.getRight());
    const auto direction = (sway::BendDirection) choiceOf(proc, ids::glideDirection);
    const float restY = direction == sway::BendDirection::Both ? toScreen(0.0f, 0.5f).y : toScreen(0.0f, 0.0f).y;
    g.setColour(colours::vfd.withAlpha(0.25f));
    g.drawHorizontalLine((int) restY, r.getX(), r.getRight());

    // Axis labels: the pitch at the top and bottom of the screen.
    const float depth = proc.state().getRawParameterValue(ids::glideDepth)->load();
    g.setFont(panelFont(10.0f, true));
    g.setColour(colours::vfd.withAlpha(0.5f));
    auto centsLabel = [&](float push) {
        float c = 0.0f;
        switch (direction)
        {
            case sway::BendDirection::Down: c = -depth * push; break;
            case sway::BendDirection::Up: c = depth * push; break;
            case sway::BendDirection::Both: c = -depth * (2.0f * push - 1.0f); break;
        }
        return (c > 0.5f ? "+" : "") + juce::String(juce::roundToInt(c)) + " ct";
    };
    const float topPush = pushIsDown() ? 0.0f : 1.0f;
    g.drawText(centsLabel(topPush), juce::Rectangle<float>(r.getRight() - 70.0f, r.getY() + 2.0f, 66.0f, 12.0f),
               juce::Justification::centredRight);
    g.drawText(centsLabel(1.0f - topPush), juce::Rectangle<float>(r.getRight() - 70.0f, r.getBottom() - 14.0f, 66.0f, 12.0f),
               juce::Justification::centredRight);

    if (editable())
    {
        // The curve, filled towards the rest line.
        juce::Path curve, fill;
        constexpr int steps = 300;
        for (int i = 0; i <= steps; ++i)
        {
            const float x = (float) i / steps;
            const auto pt = toScreen(x, shape.at(juce::jmin(x, 0.9999f)));
            if (i == 0)
            {
                curve.startNewSubPath(pt);
                fill.startNewSubPath(pt.x, restY);
            }
            else
                curve.lineTo(pt);
            fill.lineTo(pt);
        }
        fill.lineTo(r.getRight(), restY);
        fill.closeSubPath();
        g.setGradientFill(juce::ColourGradient(accent.withAlpha(0.35f * accent.getFloatAlpha()), 0.0f, restY,
                                               accent.withAlpha(0.05f), 0.0f, toScreen(0.0f, 1.0f).y, false));
        g.fillPath(fill);
        g.setColour(accent);
        g.strokePath(curve, juce::PathStrokeType(2.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        // Editable points.
        for (int i = 0; i < shape.size(); ++i)
        {
            const auto pt = toScreen(shape.point(i).x, shape.point(i).y);
            const float rad = i == hoverIndex || i == dragIndex ? kPointRadius + 2.0f : kPointRadius;
            g.setColour(colours::glass);
            g.fillEllipse(juce::Rectangle<float>(rad * 2.0f, rad * 2.0f).withCentre(pt));
            g.setColour(i == dragIndex ? juce::Colours::white : colours::vfd);
            g.drawEllipse(juce::Rectangle<float>(rad * 2.0f, rad * 2.0f).withCentre(pt), 2.0f);
        }

        // Playhead and the bar right now.
        if (on)
        {
            const float px = r.getX() + phase * r.getWidth();
            g.setColour(colours::vfd.withAlpha(0.35f));
            g.drawVerticalLine((int) px, r.getY(), r.getBottom());
            const auto now = juce::Point<float>(px, toScreen(0.0f, pushFromCents(cents)).y);
            g.setColour(accent.withAlpha(0.3f));
            g.fillEllipse(juce::Rectangle<float>(18.0f, 18.0f).withCentre(now));
            g.setColour(juce::Colours::white);
            g.fillEllipse(juce::Rectangle<float>(8.0f, 8.0f).withCentre(now));
        }
    }
    else
    {
        // MANUAL: where the bar has been over the last few seconds, newest on the right.
        juce::Path path;
        const int n = (int) trace.size();
        for (int i = 0; i < n; ++i)
        {
            const float push = trace[(size_t) ((traceHead + i) % n)];
            const auto pt = toScreen((float) i / (float) (n - 1) * 0.999f, push);
            if (i == 0)
                path.startNewSubPath(pt);
            else
                path.lineTo(pt);
        }
        g.setColour(accent);
        g.strokePath(path, juce::PathStrokeType(2.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        const auto now = toScreen(0.999f, pushFromCents(cents));
        g.setColour(juce::Colours::white);
        g.fillEllipse(juce::Rectangle<float>(9.0f, 9.0f).withCentre(now));
    }

    // Readouts: the bar now, and the timing.
    g.setColour(on ? colours::vfd : colours::vfd.withAlpha(0.35f));
    g.setFont(panelFont(20.0f, true));
    g.drawText(on ? (cents > 0.5f ? "+" : "") + juce::String(juce::roundToInt(cents)) + " ct" : juce::String("OFF"),
               juce::Rectangle<float>(bounds.getRight() - 120.0f, 8.0f, 106.0f, 26.0f), juce::Justification::centredRight);
    g.setFont(panelFont(11.0f, true));
    g.setColour(accent);
    g.drawText(on ? rateText() : juce::String("GLIDE OFF"),
               juce::Rectangle<float>(r.getX(), r.getBottom() + 6.0f, r.getWidth(), 14.0f), juce::Justification::centredLeft);
    if (editable())
    {
        g.setColour(colours::vfd.withAlpha(0.4f));
        g.drawText("drag points / double-click to add or remove",
                   juce::Rectangle<float>(r.getX(), r.getBottom() + 6.0f, r.getWidth(), 14.0f),
                   juce::Justification::centredRight);
    }
}

void ShapeDisplay::resized()
{
    shapeBox.setBounds(12, 9, 150, 24);
}

void ShapeDisplay::mouseMove(const juce::MouseEvent& e)
{
    const int i = editable() ? pointAt(e.position) : -1;
    if (i != hoverIndex)
    {
        hoverIndex = i;
        setMouseCursor(i >= 0 ? juce::MouseCursor::DraggingHandCursor : juce::MouseCursor::NormalCursor);
        repaint();
    }
}

void ShapeDisplay::editPoints(const std::function<void(std::vector<sway::ShapePoint>&)>& edit)
{
    std::vector<sway::ShapePoint> points;
    for (int i = 0; i < shape.size(); ++i)
        points.push_back(shape.point(i));
    edit(points);
    sway::Shape edited;
    edited.setPoints(points.data(), (int) points.size());
    shape = edited;
    proc.setCustomShape(edited);
    repaint();
}

void ShapeDisplay::mouseDown(const juce::MouseEvent& e)
{
    if (!editable())
        return;
    dragIndex = pointAt(e.position);
    if (dragIndex >= 0 && e.mods.isPopupMenu() && shape.size() > 2)
    {
        const int i = dragIndex;
        editPoints([i](auto& pts) { pts.erase(pts.begin() + i); });
        dragIndex = -1;
    }
}

void ShapeDisplay::mouseDrag(const juce::MouseEvent& e)
{
    if (dragIndex < 0)
        return;
    const auto target = fromScreen(e.position);
    const int i = dragIndex;
    editPoints([i, target](auto& pts) {
        // Keep the order: a point stays between its neighbours.
        const float lo = i > 0 ? pts[(size_t) i - 1].x + 0.01f : 0.0f;
        const float hi = i + 1 < (int) pts.size() ? pts[(size_t) i + 1].x - 0.01f : 0.99f;
        pts[(size_t) i] = { juce::jlimit(lo, juce::jmax(lo, hi), target.x), target.y };
    });
}

void ShapeDisplay::mouseUp(const juce::MouseEvent&)
{
    dragIndex = -1;
    repaint();
}

void ShapeDisplay::mouseDoubleClick(const juce::MouseEvent& e)
{
    if (!editable() || !plotArea().expanded(8.0f).contains(e.position))
        return;
    if (const int i = pointAt(e.position); i >= 0)
    {
        if (shape.size() > 2)
            editPoints([i](auto& pts) { pts.erase(pts.begin() + i); });
    }
    else if (shape.size() < sway::Shape::kMaxPoints)
    {
        const auto p = fromScreen(e.position);
        editPoints([p](auto& pts) { pts.push_back({ p.x, p.y }); });
    }
    dragIndex = -1;
}

} // namespace glide90::ui
