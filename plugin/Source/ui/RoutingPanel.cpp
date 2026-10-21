#include "RoutingPanel.h"

#include "PanelLookAndFeel.h"
#include "PluginProcessor.h"

namespace glide90::ui
{
namespace
{

int choiceOf(Glide90Processor& p, const char* id)
{
    return juce::roundToInt(p.state().getRawParameterValue(id)->load());
}

void setRouting(Glide90Processor& p, int index)
{
    auto* param = p.state().getParameter(ids::routing);
    param->beginChangeGesture();
    param->setValueNotifyingHost(param->convertTo0to1((float) index));
    param->endChangeGesture();
}

juce::String nameOf(int index)
{
    return juce::String(index + 1) + ". " + routingAt(index).name;
}

void arrow(juce::Graphics& g, float x, float y, int side)
{
    juce::Path tri;
    tri.addTriangle(x - (float) side * 3.0f, y - 4.5f, x - (float) side * 3.0f, y + 4.5f, x + (float) side * 3.5f, y);
    g.fillPath(tri);
}

} // namespace

// --- The diagram ---

std::vector<RoutingLabel> drawRouting(juce::Graphics* g, juce::Rectangle<float> area, const Routing& r,
                                      RoutingModules on, bool small, juce::Colour background, juce::Colour ink)
{
    const auto& t = theme();
    std::vector<RoutingLabel> labels;

    const auto font = legendFont(small ? 9.0f : 11.0f, true).withExtraKerningFactor(small ? 0.1f : 0.16f);
    const auto pillFont = legendFont(small ? 9.0f : 11.5f, true).withExtraKerningFactor(0.1f);
    auto widthOf = [](const juce::Font& f, const juce::String& s) { return juce::GlyphArrangement::getStringWidth(f, themed(s)); };

    const float x0 = area.getX(), w = area.getWidth(), top = area.getY(), h = area.getHeight();
    const float y[3] { top + h * 0.25f, top + h * 0.5f, top + h * 0.76f }; // DRY, REVERSE GATE, HOLD
    const float inX = small ? x0 + 6.0f : x0 + 8.0f + widthOf(font, names::in) + 10.0f;
    const float outX = small ? x0 + w - 6.0f : x0 + w - 8.0f - widthOf(font, names::out) - 10.0f;
    const float split = inX + (small ? 8.0f : 16.0f), join = outX - (small ? 8.0f : 16.0f);
    const float boxH = small ? 14.0f : 20.0f, boxPad = small ? 5.0f : 12.0f;
    const float pillW = widthOf(pillFont, names::glide) + (small ? 10.0f : 16.0f), pillH = small ? 12.0f : 16.0f;
    const float holdX = split + (small ? 8.0f : 22.0f), holdW = widthOf(font, names::hold) + 2.0f * boxPad;
    const float riseX = holdX + holdW + (small ? 8.0f : 26.0f);
    const float gateW = widthOf(font, names::reverseGate) + 2.0f * boxPad;
    const float gateX = small ? riseX + 6.0f + pillW + 8.0f : juce::jmax(x0 + w * 0.52f, riseX + pillW + 16.0f);
    const float gateInX = small ? gateX - 4.0f - pillW * 0.5f : gateX - 10.0f - pillW * 0.5f;
    const float gateOutX = (gateX + gateW + join) * 0.5f;
    const float dryX = small ? split + (gateInX - split) * 0.45f : x0 + w * 0.3f;
    const float holdPillX = juce::jmax(dryX, holdX + holdW + (small ? 6.0f : 16.0f) + pillW * 0.5f);

    // With REVERSE GATE off there is no gate to go into: HOLD goes to OUT, bent if the gate would be.
    const bool holdIntoGate = r.holdIntoGate && on.gateOn;
    const bool holdBent = !holdIntoGate && (r.holdIntoGate ? r.gate != GateBend::Off : r.bendHold);

    const auto live = ink.withMultipliedAlpha(0.6f), faint = ink.withMultipliedAlpha(0.25f);
    const float lw = small ? 1.2f : 1.5f;
    auto hLine = [&](float a, float b, float yy, juce::Colour c, bool dashed) {
        if (g == nullptr || b <= a)
            return;
        g->setColour(c);
        if (!dashed)
            g->fillRect(juce::Rectangle<float>(a, yy - lw * 0.5f, b - a, lw));
        else
            for (float x = a; x < b; x += 7.0f)
                g->fillRect(juce::Rectangle<float>(x, yy - lw * 0.5f, juce::jmin(3.5f, b - x), lw));
    };
    auto vLine = [&](float x, float a, float b, juce::Colour c, bool dashed) {
        if (g == nullptr || b <= a)
            return;
        g->setColour(c);
        if (!dashed)
            g->fillRect(juce::Rectangle<float>(x - lw * 0.5f, a - lw * 0.5f, lw, b - a + lw));
        else
            for (float yy = a; yy < b; yy += 6.0f)
                g->fillRect(juce::Rectangle<float>(x - lw * 0.5f, yy, lw, juce::jmin(3.0f, b - yy)));
    };
    auto dot = [&](float x, float yy) {
        if (g == nullptr)
            return;
        const float rr = small ? 2.5f : 3.5f;
        g->setColour(ink);
        g->fillEllipse(x - rr, yy - rr, 2.0f * rr, 2.0f * rr);
    };
    auto text = [&](const juce::String& s, juce::Rectangle<float> where, bool dimmed, juce::Justification j) {
        labels.push_back({ s, where, dimmed });
        if (g == nullptr)
            return;
        g->setFont(font);
        g->setColour(dimmed ? ink.withMultipliedAlpha(kDimmedAlpha * 0.8f) : ink);
        g->drawText(themed(s), where, j, false);
    };
    auto box = [&](const juce::String& s, float x, float bw, float yy, bool moduleOn) {
        const juce::Rectangle<float> b { x, yy - boxH * 0.5f, bw, boxH };
        if (g != nullptr)
        {
            g->setColour(background);
            g->fillRoundedRectangle(b, 3.0f);
            g->setColour(moduleOn ? live : faint);
            if (moduleOn)
                g->drawRoundedRectangle(b, 3.0f, lw);
            else
            {
                juce::Path outline, dashedOutline;
                outline.addRoundedRectangle(b, 3.0f);
                const float dashes[] { 3.0f, 3.0f };
                juce::PathStrokeType(lw).createDashedStroke(dashedOutline, outline, dashes, 2);
                g->fillPath(dashedOutline);
            }
        }
        text(s, b, !moduleOn, juce::Justification::centred);
    };
    auto pill = [&](float x, float yy, bool pathOn) {
        const bool lit = on.glideOn && pathOn;
        const juce::Rectangle<float> p { x - pillW * 0.5f, yy - pillH * 0.5f, pillW, pillH };
        // The word inside the pill (what the legibility test measures, the pill round it).
        labels.push_back({ names::glide, p.reduced(3.0f), !lit });
        if (g == nullptr)
            return;
        if (lit)
        {
            g->setColour(t.accent);
            g->fillRoundedRectangle(p, pillH * 0.5f);
        }
        else
        {
            g->setColour(background);
            g->fillRoundedRectangle(p, pillH * 0.5f);
            g->setColour(ink.withMultipliedAlpha(0.55f));
            g->drawRoundedRectangle(p.reduced(0.6f), pillH * 0.5f, 1.2f);
        }
        g->setFont(pillFont);
        // On the lit pill: near black on a light accent, white on a dark one.
        const bool lightAccent = t.accent.getPerceivedBrightness() > 0.45f;
        g->setColour(lit ? (lightAccent ? juce::Colour(0xff140a0f) : juce::Colours::white) : ink.withMultipliedAlpha(kDimmedAlpha));
        g->drawText(themed(names::glide), p, juce::Justification::centred, false);
    };

    // IN splits three ways; everything meets again before OUT.
    const auto gateCol = on.gateOn ? live : faint, holdCol = on.holdOn ? live : faint;
    hLine(inX, split, y[0], live, false);
    vLine(split, y[0], y[1], live, false);
    vLine(split, y[1], y[2], holdCol, !on.holdOn);
    vLine(join, y[0], y[1], on.gateOn || (on.holdOn && !holdIntoGate) ? live : faint, false);
    if (!holdIntoGate)
        vLine(join, y[1], y[2], holdCol, !on.holdOn);
    hLine(join, outX, y[0], live, false);
    hLine(split, join, y[0], live, false); // DRY
    hLine(split, gateX, y[1], gateCol, !on.gateOn);
    hLine(gateX + gateW, join, y[1], gateCol, !on.gateOn);
    hLine(split, holdX, y[2], holdCol, !on.holdOn);
    if (holdIntoGate)
    {
        hLine(holdX + holdW, riseX, y[2], holdCol, !on.holdOn);
        vLine(riseX, y[1] + 3.0f, y[2], holdCol, !on.holdOn);
        if (g != nullptr)
        {
            juce::Path head;
            head.startNewSubPath(riseX - 4.0f, y[1] + 8.0f);
            head.lineTo(riseX, y[1] + 2.5f);
            head.lineTo(riseX + 4.0f, y[1] + 8.0f);
            g->setColour(holdCol);
            g->strokePath(head, juce::PathStrokeType(lw));
        }
    }
    else
        hLine(holdX + holdW, join, y[2], holdCol, !on.holdOn);
    dot(inX, y[0]);
    dot(outX, y[0]);

    box(names::reverseGate, gateX, gateW, y[1], on.gateOn);
    box(names::hold, holdX, holdW, y[2], on.holdOn);

    if (r.bendDry)
        pill(dryX, y[0], true);
    if (on.gateOn && r.gate == GateBend::In)
        pill(gateInX, y[1], true);
    if (on.gateOn && r.gate == GateBend::Out)
        pill(gateOutX, y[1], true);
    if (holdBent)
        pill(holdPillX, y[2], on.holdOn);

    if (!small)
    {
        const float lineH = 12.0f;
        text(names::in, { x0 + 8.0f, y[0] - lineH * 0.5f, inX - x0 - 12.0f, lineH }, false, juce::Justification::centredLeft);
        text(names::out, { outX + 6.0f, y[0] - lineH * 0.5f, x0 + w - outX - 6.0f, lineH }, false, juce::Justification::centredLeft);
        text(names::dry, { split + 10.0f, y[0] - lineH - 2.0f, widthOf(font, names::dry) + 4.0f, lineH }, false,
             juce::Justification::centredLeft);
    }
    return labels;
}

// --- The chart ---

RoutingChart::RoutingChart(Glide90Processor& p, juce::Rectangle<int> card) : proc(p), panel(card)
{
    setWantsKeyboardFocus(true);
}

juce::Rectangle<float> RoutingChart::cardArea(int index) const
{
    constexpr int kColumns = 4, kPad = 14, kGap = 10;
    const auto inner = panel.toFloat().reduced((float) kPad);
    const float w = (inner.getWidth() - (kColumns - 1) * kGap) / kColumns;
    const float h = (inner.getHeight() - kGap) / 2.0f;
    return { inner.getX() + (float) (index % kColumns) * (w + kGap), inner.getY() + (float) (index / kColumns) * (h + kGap), w, h };
}

int RoutingChart::cardAt(juce::Point<float> p) const
{
    for (int i = 0; i < kNumRoutings; ++i)
        if (cardArea(i).contains(p))
            return i;
    return -1;
}

void RoutingChart::choose(int index)
{
    setRouting(proc, index);
    if (onClose)
        onClose();
}

void RoutingChart::paint(juce::Graphics& g)
{
    const auto& t = theme();
    const auto r = panel.toFloat();
    g.setColour(juce::Colours::black.withAlpha(0.45f));
    g.fillRoundedRectangle(r.translated(0.0f, 8.0f).expanded(4.0f), 10.0f);
    g.setColour(t.menu);
    g.fillRoundedRectangle(r, 8.0f);
    g.setColour(t.textDim.withMultipliedAlpha(0.35f));
    g.drawRoundedRectangle(r.reduced(0.5f), 8.0f, 1.0f);

    const int now = choiceOf(proc, ids::routing);
    for (int i = 0; i < kNumRoutings; ++i)
    {
        const auto c = cardArea(i);
        g.setColour(t.textDim.withMultipliedAlpha(i == hover ? 0.22f : 0.1f));
        g.fillRoundedRectangle(c, 6.0f);
        if (i == now)
        {
            g.setColour(t.accent);
            g.drawRoundedRectangle(c.reduced(1.0f), 6.0f, 2.0f);
        }
        else
        {
            g.setColour(t.textDim.withMultipliedAlpha(i == hover ? 0.6f : 0.3f));
            g.drawRoundedRectangle(c.reduced(0.5f), 6.0f, 1.0f);
        }
        auto inner = c.reduced(10.0f, 8.0f);
        drawReadout(g, nameOf(i), inner.removeFromTop(18.0f), t.ink);
        drawRouting(&g, inner.removeFromTop(64.0f), routingAt(i), {}, true, t.menu, t.ink);
        g.setFont(legendFont(12.5f));
        g.setColour(t.ink.withMultipliedAlpha(0.8f));
        g.drawFittedText(themed(routingAt(i).description), inner.toNearestInt(), juce::Justification::topLeft, 2, 0.9f);
    }
}

void RoutingChart::mouseMove(const juce::MouseEvent& e)
{
    const int i = cardAt(e.position);
    setMouseCursor(i >= 0 ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
    if (i != hover)
    {
        hover = i;
        repaint(panel);
    }
}

void RoutingChart::mouseExit(const juce::MouseEvent&)
{
    if (hover >= 0)
    {
        hover = -1;
        repaint(panel);
    }
}

void RoutingChart::mouseDown(const juce::MouseEvent& e)
{
    if (const int i = cardAt(e.position); i >= 0)
        choose(i);
    else if (!panel.toFloat().contains(e.position) && onClose)
        onClose();
}

bool RoutingChart::keyPressed(const juce::KeyPress& key)
{
    if (key == juce::KeyPress::escapeKey && onClose)
    {
        onClose();
        return true;
    }
    return false;
}

// --- The strip ---

RoutingPanel::RoutingPanel(Glide90Processor& p) : proc(p) {}

RoutingPanel::~RoutingPanel()
{
    closeChart();
}

juce::Rectangle<float> RoutingPanel::diagramArea() const
{
    const float x = selectorArea().getRight() + 16.0f;
    return { x, 0.0f, (float) getWidth() - x - 8.0f, (float) getHeight() };
}

int RoutingPanel::current() const
{
    return choiceOf(proc, ids::routing);
}

RoutingModules RoutingPanel::modules() const
{
    return { choiceOf(proc, ids::glideOn) != 0, choiceOf(proc, ids::gateOn) != 0, choiceOf(proc, ids::holdOn) != 0 };
}

juce::String RoutingPanel::nameText() const
{
    return nameOf(current());
}

void RoutingPanel::step(int delta)
{
    setRouting(proc, ((current() + delta) % kNumRoutings + kNumRoutings) % kNumRoutings);
    frame();
}

void RoutingPanel::openChart()
{
    auto* parent = getParentComponent();
    if (parent == nullptr || chartWindow != nullptr)
        return;
    // Under the strip, across the window's width.
    const auto card = juce::Rectangle<int>(PanelLookAndFeel::kMargin, getBottom() + 8,
                                           parent->getWidth() - 2 * PanelLookAndFeel::kMargin, 310);
    chartWindow = std::make_unique<RoutingChart>(proc, card);
    chartWindow->onClose = [safe = juce::Component::SafePointer<RoutingPanel>(this)] {
        // After the chart's own mouse or key handler has returned.
        juce::MessageManager::callAsync([safe] {
            if (safe != nullptr)
                safe->closeChart();
        });
    };
    parent->addAndMakeVisible(*chartWindow);
    chartWindow->setBounds(parent->getLocalBounds());
    chartWindow->grabKeyboardFocus();
    repaint();
}

void RoutingPanel::closeChart()
{
    if (chartWindow == nullptr)
        return;
    if (auto* parent = chartWindow->getParentComponent())
        parent->removeChildComponent(chartWindow.get());
    chartWindow.reset();
    frame();
    repaint();
}

void RoutingPanel::frame()
{
    const int now = current();
    const auto mods = modules();
    if (now != shownRouting || mods.glideOn != shownModules.glideOn || mods.gateOn != shownModules.gateOn
        || mods.holdOn != shownModules.holdOn)
    {
        shownRouting = now;
        shownModules = mods;
        repaint();
        if (chartWindow != nullptr)
            chartWindow->repaint();
    }
}

RoutingPanel::Spot RoutingPanel::spotAt(juce::Point<float> p) const
{
    const auto s = selectorArea();
    if (!s.contains(p))
        return Spot::Nowhere;
    if (p.x < s.getX() + 26.0f && p.y < s.getY() + 30.0f)
        return Spot::Previous;
    if (p.x > s.getRight() - 26.0f && p.y < s.getY() + 30.0f)
        return Spot::Next;
    return Spot::Name;
}

std::vector<RoutingLabel> RoutingPanel::labels() const
{
    const auto s = selectorArea();
    std::vector<RoutingLabel> out;
    const float nameW = readoutWidth(nameText(), true);
    out.push_back({ nameText(), juce::Rectangle<float>(nameW + 4.0f, 22.0f).withCentre({ s.getCentreX(), s.getY() + 16.0f }), false });
    out.push_back({ routingAt(current()).description, { s.getX() + 10.0f, s.getY() + 32.0f, s.getWidth() - 20.0f, 14.0f }, false });
    for (auto& l : drawRouting(nullptr, diagramArea(), routingAt(current()), modules(), false, theme().card, theme().text))
        out.push_back(l);
    return out;
}

void RoutingPanel::paint(juce::Graphics& g)
{
    const auto& t = theme();
    drawCard(g, getLocalBounds().toFloat());

    // The selector: the name on its glass, stepped with the arrows, what it does under it.
    const auto s = selectorArea();
    drawGlass(g, s);
    const float nameY = s.getY() + 16.0f;
    for (int side : { -1, 1 })
    {
        const bool hot = hover == (side < 0 ? Spot::Previous : Spot::Next);
        g.setColour(t.ink.withMultipliedAlpha(hot ? 1.0f : 0.8f));
        arrow(g, side < 0 ? s.getX() + 13.0f : s.getRight() - 13.0f, nameY, side);
    }
    drawReadout(g, nameText(), { s.getX() + 26.0f, nameY - 11.0f, s.getWidth() - 52.0f, 22.0f },
                t.ink.withMultipliedAlpha(hover == Spot::Name || chartWindow != nullptr ? 1.0f : 0.92f),
                juce::Justification::centred, true, true);
    g.setFont(legendFont(12.5f));
    g.setColour(t.ink.withMultipliedAlpha(0.8f));
    g.drawFittedText(themed(routingAt(current()).description),
                     juce::Rectangle<float>(s.getX() + 10.0f, s.getY() + 32.0f, s.getWidth() - 20.0f, 14.0f).toNearestInt(),
                     juce::Justification::centred, 1, 0.85f);

    drawRouting(&g, diagramArea(), routingAt(current()), modules(), false, t.card, t.text);
}

void RoutingPanel::mouseMove(const juce::MouseEvent& e)
{
    const auto s = spotAt(e.position);
    setMouseCursor(s == Spot::Nowhere ? juce::MouseCursor::NormalCursor : juce::MouseCursor::PointingHandCursor);
    if (s != hover)
    {
        hover = s;
        repaint(selectorArea().expanded(2.0f).getSmallestIntegerContainer());
    }
}

void RoutingPanel::mouseExit(const juce::MouseEvent&)
{
    if (hover != Spot::Nowhere)
    {
        hover = Spot::Nowhere;
        repaint(selectorArea().expanded(2.0f).getSmallestIntegerContainer());
    }
}

void RoutingPanel::mouseDown(const juce::MouseEvent& e)
{
    if (!e.mods.isLeftButtonDown())
        return;
    switch (spotAt(e.position))
    {
        case Spot::Previous: step(-1); break;
        case Spot::Next: step(1); break;
        case Spot::Name: chartWindow != nullptr ? closeChart() : openChart(); break;
        case Spot::Nowhere: break;
    }
}

juce::String RoutingPanel::getTooltip()
{
    switch (hover)
    {
        case Spot::Previous: return "The previous routing";
        case Spot::Next: return "The next routing";
        case Spot::Name: return "Click for the chart of every routing";
        case Spot::Nowhere: break;
    }
    return "Where HOLD goes and which paths GLIDE bends: a lit GLIDE mark bends the path it sits on. "
           "With REVERSE GATE off, HOLD goes to OUT";
}

} // namespace glide90::ui
