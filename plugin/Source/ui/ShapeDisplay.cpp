#include "ShapeDisplay.h"

#include "PanelLookAndFeel.h"
#include "Parameters.h"
#include "PluginProcessor.h"

#include <utility>

namespace glide90::ui
{
namespace
{

constexpr float kPointSize = 7.0f;

} // namespace

ShapeDisplay::ShapeDisplay(Glide90Processor& p) : proc(p), shapeParam(*p.state().getParameter(ids::glideShape))
{
    setTooltip("Drag a point to reshape the cycle; double-click to add or remove one. Arrows at the top step "
               "through the shapes");
    shape = proc.currentShape();
}

ShapeDisplay::~ShapeDisplay() = default;

int ShapeDisplay::choice(const char* id) const
{
    return juce::roundToInt(proc.state().getRawParameterValue(id)->load());
}

bool ShapeDisplay::pushIsDown() const
{
    // Pushing the bar is drawn downwards unless the direction is UP.
    return (sway::BendDirection) choice(ids::glideDirection) != sway::BendDirection::Up;
}

bool ShapeDisplay::editable() const
{
    return (sway::GlideMode) choice(ids::glideMode) != sway::GlideMode::Manual;
}

juce::Rectangle<float> ShapeDisplay::plotArea() const
{
    // The right margin holds the depth scale.
    return getLocalBounds().toFloat().withTrimmedLeft(16.0f).withTrimmedRight(54.0f).withTrimmedTop(34.0f).withTrimmedBottom(28.0f);
}

juce::Rectangle<float> ShapeDisplay::nameArea() const
{
    // The name and its two arrows.
    const float w = readoutWidth(shapeParam.getCurrentValueAsText());
    return { 10.0f, 6.0f, w + 30.0f, 22.0f };
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
    switch ((sway::BendDirection) choice(ids::glideDirection))
    {
        case sway::BendDirection::Down: return juce::jlimit(0.0f, 1.0f, -c / depth);
        case sway::BendDirection::Up: return juce::jlimit(0.0f, 1.0f, c / depth);
        case sway::BendDirection::Both: default: return juce::jlimit(0.0f, 1.0f, 0.5f * (1.0f - c / depth));
    }
}

int ShapeDisplay::pointAt(juce::Point<float> p) const
{
    int best = -1;
    float bestDist = 11.0f;
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

juce::String ShapeDisplay::statusText() const
{
    const auto ms = juce::String(juce::roundToInt(proc.state().getRawParameterValue(ids::glidePeriod)->load())) + "MS";
    switch ((sway::GlideMode) choice(ids::glideMode))
    {
        case sway::GlideMode::Free: return "FREE " + ms;
        case sway::GlideMode::Sync:
            return "SYNC " + juce::String(kDivisions[juce::jlimit(0, (int) std::size(kDivisions) - 1, choice(ids::glideDivision))].label)
                   + " AT " + juce::String(juce::roundToInt(proc.hostBpm())) + " BPM";
        case sway::GlideMode::Trigger:
            return ((TriggerSource) choice(ids::glideTriggerSource) == TriggerSource::Pick ? "ON EACH PICK " : "ON EACH NOTE ") + ms;
        case sway::GlideMode::Manual:
        default: return "MANUAL";
    }
}

juce::String ShapeDisplay::settingsKey() const
{
    // Everything the screen shows that is not the moving bar.
    juce::String key;
    for (const char* id : { ids::glideOn, ids::glideMode, ids::glideDirection, ids::glideDepth, ids::glideShape,
                            ids::glideTriggerSource, ids::glidePeriod, ids::glideDivision })
        key << proc.state().getRawParameterValue(id)->load() << ' ';
    return key << juce::roundToInt(proc.hostBpm());
}

void ShapeDisplay::frame(double seconds)
{
    // Redrawn only when something on it moved: the screen is the largest thing in the window.
    auto next = proc.currentShape();
    bool changed = next.size() != shape.size();
    for (int i = 0; !changed && i < next.size(); ++i)
        changed = !juce::exactlyEqual(next.point(i).x, shape.point(i).x) || !juce::exactlyEqual(next.point(i).y, shape.point(i).y);
    shape = next;

    const float newPhase = proc.glidePhase(), newCents = proc.barCents();
    changed = changed || std::abs(newPhase - phase) > 1e-4f || std::abs(newCents - cents) > 0.05f;
    phase = newPhase;
    cents = newCents;

    if (const int attacks = proc.pickAttacks(); attacks != seenAttacks)
    {
        seenAttacks = attacks;
        attackFlashUntil = juce::Time::getMillisecondCounter() + 120;
    }
    const bool flash = juce::Time::getMillisecondCounter() < attackFlashUntil;
    changed = changed || flash != shownFlash;
    shownFlash = flash;

    if (auto key = settingsKey(); key != shownSettings)
    {
        shownSettings = key;
        changed = true;
    }

    // The trace takes 30 samples a second, whatever the display's rate: eight seconds of it.
    bool traced = false;
    if (seconds - lastTraceTime >= 1.0 / 30.0)
    {
        lastTraceTime = seconds;
        trace[(size_t) traceHead] = pushFromCents(cents);
        traceHead = (traceHead + 1) % (int) trace.size();
        traced = true;
    }
    // MANUAL scrolls its trace all the time.
    if (changed || (traced && !editable()))
        repaint();
}

juce::String ShapeDisplay::layerKey(float scale) const
{
    juce::String key = settingsKey();
    key << '|' << scale << '|' << getWidth() << 'x' << getHeight() << '|' << theme().key << '|' << hoverIndex << ' ' << dragIndex << '|';
    for (int i = 0; i < shape.size(); ++i)
        key << shape.point(i).x << ',' << shape.point(i).y << ' ';
    return key;
}

void ShapeDisplay::paint(juce::Graphics& g)
{
    const float scale = g.getInternalContext().getPhysicalPixelScaleFactor();
    if (const auto key = layerKey(scale); key != shownLayer || layer.isNull())
    {
        shownLayer = key;
        layerScale = scale;
        layer = juce::Image(juce::Image::ARGB, juce::jmax(1, juce::roundToInt((float) getWidth() * scale)),
                            juce::jmax(1, juce::roundToInt((float) getHeight() * scale)), true);
        juce::Graphics lg(layer);
        lg.addTransform(juce::AffineTransform::scale(scale));
        paintStill(lg);
    }
    g.drawImageTransformed(layer, juce::AffineTransform::scale(1.0f / layerScale));
    paintMoving(g);
}

void ShapeDisplay::paintStill(juce::Graphics& g)
{
    const auto& t = theme();
    const auto bounds = getLocalBounds().toFloat();
    drawGlass(g, bounds);

    const auto r = plotArea();
    const bool on = proc.state().getRawParameterValue(ids::glideOn)->load() > 0.5f;
    const auto ink = t.ink.withAlpha(on ? 1.0f : 0.35f);

    // Grid: a dot at each eighth of the cycle and quarter of the depth.
    g.setColour(t.ink.withAlpha(0.18f));
    for (int i = 0; i <= 8; ++i)
        for (int j = 0; j <= 4; ++j)
            g.fillRect(r.getX() + r.getWidth() * (float) i / 8.0f - 0.5f, r.getY() + r.getHeight() * (float) j / 4.0f - 0.5f, 1.2f, 1.2f);

    // The rest line (no bend), dashed.
    const auto direction = (sway::BendDirection) choice(ids::glideDirection);
    const float restY = direction == sway::BendDirection::Both ? toScreen(0.0f, 0.5f).y : toScreen(0.0f, 0.0f).y;
    g.setColour(t.ink.withAlpha(0.3f));
    for (float x = r.getX(); x < r.getRight(); x += 6.0f)
        g.fillRect(x, restY - 0.5f, 3.0f, 1.0f);

    // The depth at the top and bottom edges.
    const float depth = proc.state().getRawParameterValue(ids::glideDepth)->load();
    auto centsAt = [&](float push) {
        float c = 0.0f;
        switch (direction)
        {
            case sway::BendDirection::Down: c = -depth * push; break;
            case sway::BendDirection::Up: c = depth * push; break;
            case sway::BendDirection::Both: c = -depth * (2.0f * push - 1.0f); break;
        }
        return (c > 0.5f ? "+" : "") + juce::String(juce::roundToInt(c)) + "CT";
    };
    // The depth at the far edge (both edges for BOTH); the rest line needs no label.
    const float scaleX = r.getRight() + 4.0f, scaleW = bounds.getRight() - 8.0f - scaleX;
    auto scaleLabel = [&](float push) {
        const float y = toScreen(0.0f, push).y;
        drawReadout(g, centsAt(push), { scaleX, y - 6.0f, scaleW, 12.0f }, t.ink.withAlpha(0.45f), juce::Justification::right);
    };
    if (direction == sway::BendDirection::Both)
        scaleLabel(0.0f);
    scaleLabel(1.0f);

    if (editable())
    {
        // The curve, with a faint fill towards the rest line.
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
            fill.lineTo(pt);
            if (i > 0)
                curve.lineTo(pt);
        }
        fill.lineTo(r.getRight(), restY);
        fill.closeSubPath();
        // A wash that fades in away from the rest line, so the line itself carries the shape.
        const float farY = toScreen(0.0f, 1.0f).y;
        g.setGradientFill(juce::ColourGradient(t.ink.withAlpha(0.0f), 0.0f, restY,
                                               t.ink.withAlpha(on ? 0.1f : 0.04f), 0.0f, farY, false));
        g.fillPath(fill);
        strokeGlowing(g, curve, ink, 1.6f);

        // Editable points: small squares.
        for (int i = 0; i < shape.size(); ++i)
        {
            const auto pt = toScreen(shape.point(i).x, shape.point(i).y);
            const auto box = juce::Rectangle<float>(kPointSize, kPointSize).withCentre(pt);
            g.setColour(t.menu);
            g.fillRect(box);
            g.setColour(ink);
            if (i == hoverIndex || i == dragIndex)
                g.fillRect(box);
            else
                g.drawRect(box, 1.2f);
        }

        // The shape's name with its arrows right beside it.
        const auto name = nameArea();
        const auto label = shapeParam.getCurrentValueAsText();
        const float w = readoutWidth(label);
        const float x = name.getX() + 14.0f, cy = name.getCentreY();
        g.setColour(t.ink.withAlpha(0.8f));
        juce::Path left, right;
        left.addTriangle(name.getX() + 2.0f, cy, name.getX() + 8.0f, cy - 5.0f, name.getX() + 8.0f, cy + 5.0f);
        right.addTriangle(x + w + 12.0f, cy, x + w + 6.0f, cy - 5.0f, x + w + 6.0f, cy + 5.0f);
        g.fillPath(left);
        g.fillPath(right);
        drawReadout(g, label, { x, name.getY(), w + 2.0f, name.getHeight() }, t.ink);
    }

    // Bottom: what drives it.
    drawReadout(g, on ? statusText() : juce::String("GLIDE OFF"), { r.getX(), r.getBottom() + 8.0f, r.getWidth(), 14.0f },
                t.ink.withAlpha(on ? 0.7f : 0.35f));
}

void ShapeDisplay::paintMoving(juce::Graphics& g)
{
    const auto& t = theme();
    const auto bounds = getLocalBounds().toFloat();
    const auto r = plotArea();
    const bool on = proc.state().getRawParameterValue(ids::glideOn)->load() > 0.5f;
    const auto ink = t.ink.withAlpha(on ? 1.0f : 0.35f);
    // The dot that shows where the bar is: white on a dark screen, the ink on a light one.
    const auto dotColour = t.backdrop == Theme::Backdrop::Matte ? juce::Colours::white : t.ink;

    if (editable())
    {
        // Playhead and where the bar is right now.
        if (on)
        {
            const float px = r.getX() + phase * r.getWidth();
            g.setColour(t.ink.withAlpha(0.25f));
            g.drawVerticalLine((int) px, r.getY(), r.getBottom());
            g.setColour(dotColour);
            g.fillEllipse(juce::Rectangle<float>(6.0f, 6.0f).withCentre({ px, toScreen(0.0f, pushFromCents(cents)).y }));
        }
    }
    else
    {
        // MANUAL: where the bar has been over the last few seconds, newest on the right.
        juce::Path path;
        const int n = (int) trace.size();
        for (int i = 0; i < n; ++i)
        {
            const auto pt = toScreen((float) i / (float) (n - 1) * 0.999f, trace[(size_t) ((traceHead + i) % n)]);
            if (i == 0)
                path.startNewSubPath(pt);
            else
                path.lineTo(pt);
        }
        strokeGlowing(g, path, ink, 1.6f);
        g.setColour(dotColour);
        g.fillEllipse(juce::Rectangle<float>(6.0f, 6.0f).withCentre(toScreen(0.999f, pushFromCents(cents))));
    }

    // The bar now.
    drawReadout(g, on ? (cents > 0.5f ? "+" : "") + juce::String(juce::roundToInt(cents)) + "CT" : juce::String("OFF"),
                { bounds.getRight() - 120.0f, 6.0f, 108.0f, 22.0f }, ink, juce::Justification::right, true, true);

    // TRIGGER from the pick: flashes on every attack it hears, to set PICK SENS by.
    if (on && (sway::GlideMode) choice(ids::glideMode) == sway::GlideMode::Trigger
        && (TriggerSource) choice(ids::glideTriggerSource) == TriggerSource::Pick)
    {
        const bool flash = juce::Time::getMillisecondCounter() < attackFlashUntil;
        drawReadout(g, "PICK", { r.getX(), r.getBottom() + 8.0f, r.getWidth(), 14.0f },
                    flash ? dotColour : t.ink.withAlpha(0.25f), juce::Justification::right, false, flash);
    }
}

void ShapeDisplay::stepShape(int delta)
{
    const int n = (int) sway::kShapePresetNames.size() + 1; // the presets and Custom
    const int next = (choice(ids::glideShape) + delta + n) % n;
    shapeParam.beginChangeGesture();
    shapeParam.setValueNotifyingHost(shapeParam.convertTo0to1((float) next));
    shapeParam.endChangeGesture();
}

void ShapeDisplay::mouseMove(const juce::MouseEvent& e)
{
    const int i = editable() ? pointAt(e.position) : -1;
    const bool overName = editable() && nameArea().contains(e.position);
    if (i != hoverIndex)
    {
        hoverIndex = i;
        repaint();
    }
    setMouseCursor(i >= 0 ? juce::MouseCursor::DraggingHandCursor
                          : (overName ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor));
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
    const auto name = nameArea();
    if (name.contains(e.position))
    {
        if (e.position.x < name.getX() + 14.0f)
            stepShape(-1);
        else if (e.position.x > name.getRight() - 14.0f)
            stepShape(1);
        else
        {
            juce::PopupMenu menu;
            menu.setLookAndFeel(&getLookAndFeel());
            const auto names = dynamic_cast<juce::AudioParameterChoice&>(shapeParam).choices;
            for (int i = 0; i < names.size(); ++i)
                menu.addItem(names[i], true, i == choice(ids::glideShape), [this, i] { stepShape(i - choice(ids::glideShape)); });
            menu.showMenuAsync(juce::PopupMenu::Options().withTargetScreenArea(localAreaToGlobal(name.toNearestInt())));
        }
        return;
    }
    dragIndex = pointAt(e.position);
    if (dragIndex >= 0 && e.mods.isPopupMenu() && shape.size() > 2)
    {
        const int i = dragIndex;
        proc.beginEdit();
        editPoints([i](auto& pts) { pts.erase(pts.begin() + i); });
        proc.endEdit();
        dragIndex = -1;
    }
    else if (dragIndex >= 0 && !editing)
    {
        editing = true;
        proc.beginEdit();
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
    if (std::exchange(editing, false))
        proc.endEdit();
    repaint();
}

void ShapeDisplay::mouseDoubleClick(const juce::MouseEvent& e)
{
    if (!editable() || !plotArea().expanded(8.0f).contains(e.position))
        return;
    proc.beginEdit();
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
    proc.endEdit();
    dragIndex = -1;
}

} // namespace glide90::ui
