#include "HoldPanel.h"

#include "PanelLookAndFeel.h"
#include "Parameters.h"
#include "PluginProcessor.h"

namespace glide90::ui
{
namespace
{

const juce::StringArray& modeNames()
{
    static const juce::StringArray names { "STRIKE", "LATCH", "LIVE", "MIDI" };
    return names;
}

void arrow(juce::Graphics& g, float x, float y, int side)
{
    juce::Path tri;
    tri.addTriangle(x - (float) side * 3.0f, y - 4.5f, x - (float) side * 3.0f, y + 4.5f, x + (float) side * 3.5f, y);
    g.fillPath(tri);
}

} // namespace

// --- HoldDisplay ---

HoldDisplay::HoldDisplay(Glide90Processor& p) : proc(p), source(p.holdDisplay())
{
    frame(0.0); // what there is now, before the first frame of the display
}

HoldDisplay::~HoldDisplay() = default;

void HoldDisplay::setCompact(bool c)
{
    compact = c;
    hover = Spot::Nowhere;
    setMouseCursor(compact ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
    setTooltip(compact ? "What HOLD holds. Click to open the strip" : "");
    repaint();
}

int HoldDisplay::choice(const char* id) const
{
    return juce::roundToInt(proc.state().getRawParameterValue(id)->load());
}

void HoldDisplay::frame(double seconds)
{
    bool changed = false;
    auto set = [&changed](auto& field, auto value) {
        bool differs;
        if constexpr (std::is_floating_point_v<std::decay_t<decltype(field)>>)
            differs = !juce::exactlyEqual(field, value);
        else
            differs = field != value;
        if (differs)
        {
            field = value;
            changed = true;
        }
    };
    set(on, choice(ids::holdOn) != 0);
    set(mode, choice(ids::holdMode));
    set(feed, choice(ids::holdFeed));
    set(density, choice(ids::holdDensity));
    set(character, choice(ids::holdCharacter));
    set(root, choice(ids::holdRoot));
    set(latched, choice(ids::holdFreeze) != 0);
    set(gateBypassed, choice(ids::gateOn) == 0);
    set(holding, source.holding.load());

    if ((hold::Mode) mode == hold::Mode::Live)
    {
        // The last moments, oldest on the left.
        const int head = source.liveHead.load();
        for (int col = 0; col < hold::HoldDisplay::kColumns; ++col)
        {
            const int from = (head + col) % hold::HoldDisplay::kColumns;
            for (int k = 0; k < 2; ++k)
                set(wave[(size_t) (2 * col + k)], source.live[(size_t) (2 * from + k)].load());
        }
        shownVersion = -1;
    }
    else if (const int version = source.heldVersion.load(); version != shownVersion)
    {
        shownVersion = version;
        wave = source.held[(size_t) source.heldIndex.load()];
        heldSeconds = source.heldSeconds.load();
        changed = true;
    }

    for (int i = 0; i < hold::HoldDisplay::kGrains; ++i)
    {
        const float pos = source.grainPos[(size_t) i].load(), amp = source.grainAmp[(size_t) i].load();
        if (std::abs(pos - grainPos[(size_t) i]) > 0.0005f || std::abs(amp - grainAmp[(size_t) i]) > 0.01f)
        {
            grainPos[(size_t) i] = pos;
            grainAmp[(size_t) i] = amp;
            changed = true;
        }
    }

    // The layer's level: up at once, down at a meter's pace.
    const float dt = lastFrame > 0.0 ? (float) juce::jlimit(0.0, 0.1, seconds - lastFrame) : 0.0f;
    lastFrame = seconds;
    const float db = juce::Decibels::gainToDecibels(source.level.exchange(0.0f), -100.0f);
    const float shown = juce::jmax(db, levelDb - 30.0f * dt);
    if (std::abs(shown - levelDb) > 0.2f)
        changed = true;
    levelDb = shown;

    if (changed)
        repaint();
}

juce::String HoldDisplay::status() const
{
    const auto m = (hold::Mode) mode;
    if (!on)
        return "HOLD OFF";
    if (m == hold::Mode::Live)
        return "LIVE";
    if (m == hold::Mode::Latch && !latched)
        return "FREEZE TO HOLD";
    if (!holding)
        return m == hold::Mode::Midi ? "PICK, THEN A NOTE" : "WAITING FOR A PICK";
    return (m == hold::Mode::Latch ? "FROZEN " : "HOLDING ") + juce::String(heldSeconds, 2) + " S";
}

// --- The screen's controls ---

juce::Rectangle<float> HoldDisplay::spotArea(Spot s) const
{
    const auto r = getLocalBounds().toFloat();
    const float line = 22.0f, w = r.getWidth();
    const juce::Rectangle<float> top { 0.0f, 0.0f, w, line }, bottom { 0.0f, r.getBottom() - line, w, line };
    switch (s)
    {
        case Spot::ModeDown: return top.withWidth(20.0f);
        case Spot::Mode: return top.withX(20.0f).withWidth(88.0f);
        case Spot::ModeUp: return top.withX(108.0f).withWidth(20.0f);
        case Spot::DensityDown: return top.withX(w - 72.0f).withWidth(18.0f);
        case Spot::Density: return top.withX(w - 54.0f).withWidth(36.0f);
        case Spot::DensityUp: return top.withX(w - 18.0f).withWidth(18.0f);
        case Spot::Character: return bottom.withWidth(78.0f);
        case Spot::RootDown: return bottom.withX(w - 86.0f).withWidth(18.0f);
        case Spot::Root: return bottom.withX(w - 68.0f).withWidth(50.0f);
        case Spot::RootUp: return bottom.withX(w - 18.0f).withWidth(18.0f);
        case Spot::Nowhere: break;
    }
    return {};
}

HoldDisplay::Spot HoldDisplay::spotAt(juce::Point<float> p) const
{
    if (compact)
        return Spot::Nowhere;
    const bool midi = (hold::Mode) mode == hold::Mode::Midi;
    for (auto s : { Spot::ModeDown, Spot::Mode, Spot::ModeUp, Spot::DensityDown, Spot::Density, Spot::DensityUp,
                    Spot::Character, Spot::RootDown, Spot::Root, Spot::RootUp })
    {
        const bool rootSpot = s == Spot::RootDown || s == Spot::Root || s == Spot::RootUp;
        if ((!rootSpot || midi) && spotArea(s).contains(p))
            return s;
    }
    return Spot::Nowhere;
}

void HoldDisplay::setTo(const char* id, int value)
{
    auto* param = proc.state().getParameter(id);
    param->beginChangeGesture();
    param->setValueNotifyingHost(param->convertTo0to1((float) value));
    param->endChangeGesture();
}

void HoldDisplay::step(const char* id, int delta, bool wrap)
{
    auto* param = proc.state().getParameter(id);
    const auto range = param->getNormalisableRange();
    const int lo = juce::roundToInt(range.start), hi = juce::roundToInt(range.end);
    int v = choice(id) + delta;
    if (wrap)
        v = lo + ((v - lo) % (hi - lo + 1) + (hi - lo + 1)) % (hi - lo + 1);
    setTo(id, juce::jlimit(lo, hi, v));
}

void HoldDisplay::mouseMove(const juce::MouseEvent& e)
{
    if (compact)
        return;
    const auto s = spotAt(e.position);
    if (s == hover)
        return;
    hover = s;
    setMouseCursor(s == Spot::Nowhere ? juce::MouseCursor::NormalCursor : juce::MouseCursor::PointingHandCursor);
    switch (s)
    {
        case Spot::ModeDown:
        case Spot::Mode:
        case Spot::ModeUp:
            setTooltip("STRIKE: each pick attack captures the chord and holds it. LATCH: FREEZE holds what just "
                       "played. LIVE: grains of the last moments, nothing held. MIDI: captured on each pick, heard "
                       "while a MIDI note is held, at that note's pitch");
            break;
        case Spot::DensityDown:
        case Spot::Density:
        case Spot::DensityUp: setTooltip("DENSITY: how many grains play at once"); break;
        case Spot::Character: setTooltip("12-BIT: captured like a late-80s sampler, grainy and dull at the top"); break;
        case Spot::RootDown:
        case Spot::Root:
        case Spot::RootUp: setTooltip("The MIDI note that plays the held chord at its own pitch"); break;
        case Spot::Nowhere:
            setTooltip("What HOLD holds: the chord captured at the last pick (or frozen, or the last moments in "
                       "LIVE), with the grains that play it");
            break;
    }
    repaint();
}

void HoldDisplay::mouseExit(const juce::MouseEvent&)
{
    if (hover != Spot::Nowhere)
    {
        hover = Spot::Nowhere;
        repaint();
    }
}

void HoldDisplay::mouseDown(const juce::MouseEvent& e)
{
    if (compact)
    {
        if (onUnfold)
            onUnfold();
        return;
    }
    switch (spotAt(e.position))
    {
        case Spot::ModeDown: step(ids::holdMode, -1, true); break;
        case Spot::ModeUp: step(ids::holdMode, 1, true); break;
        case Spot::Mode:
        {
            juce::PopupMenu menu;
            menu.setLookAndFeel(&getLookAndFeel());
            for (int i = 0; i < modeNames().size(); ++i)
                menu.addItem(modeNames()[i], true, i == mode, [this, i] { setTo(ids::holdMode, i); });
            menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this));
            break;
        }
        case Spot::DensityDown: step(ids::holdDensity, -1, false); break;
        case Spot::DensityUp: step(ids::holdDensity, 1, false); break;
        case Spot::Density:
        {
            juce::PopupMenu menu;
            menu.setLookAndFeel(&getLookAndFeel());
            for (int n = 1; n <= 8; ++n)
                menu.addItem(juce::String(n) + (n == 1 ? " grain" : " grains"), true, n == density,
                             [this, n] { setTo(ids::holdDensity, n); });
            menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this));
            break;
        }
        case Spot::Character: setTo(ids::holdCharacter, 1 - character); break;
        case Spot::RootDown: step(ids::holdRoot, -1, false); break;
        case Spot::RootUp: step(ids::holdRoot, 1, false); break;
        case Spot::Root:
        {
            // By octave, then the note.
            juce::PopupMenu menu;
            menu.setLookAndFeel(&getLookAndFeel());
            for (int octave = 24; octave <= 96; octave += 12)
            {
                juce::PopupMenu notes;
                for (int n = octave; n < octave + 12 && n <= 96; ++n)
                    notes.addItem(noteName(n), true, n == root, [this, n] { setTo(ids::holdRoot, n); });
                menu.addSubMenu(noteName(octave) + " ...", notes, true, nullptr, octave <= root && root < octave + 12);
            }
            menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this));
            break;
        }
        case Spot::Nowhere: break;
    }
}

void HoldDisplay::mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    const int delta = wheel.deltaY > 0.0f ? 1 : (wheel.deltaY < 0.0f ? -1 : 0);
    if (delta == 0)
        return;
    switch (spotAt(e.position))
    {
        case Spot::ModeDown:
        case Spot::Mode:
        case Spot::ModeUp: step(ids::holdMode, -delta, false); break;
        case Spot::RootDown:
        case Spot::Root:
        case Spot::RootUp: step(ids::holdRoot, delta, false); break;
        case Spot::DensityDown:
        case Spot::Density:
        case Spot::DensityUp: step(ids::holdDensity, delta, false); break;
        case Spot::Character:
        case Spot::Nowhere: Component::mouseWheelMove(e, wheel); break;
    }
}

// --- Painting ---

void HoldDisplay::paintWave(juce::Graphics& g, juce::Rectangle<float> plot, bool grains)
{
    const auto& t = theme();
    const auto ink = t.ink.withAlpha(on ? 1.0f : kOffInkAlpha);
    const float mid = plot.getCentreY();
    // The waveform, scaled to its loudest so a quiet chord still shows.
    float loudest = 1e-4f;
    for (float v : wave)
        loudest = juce::jmax(loudest, std::abs(v));
    const float scale = plot.getHeight() * 0.5f / loudest;
    const int columns = hold::HoldDisplay::kColumns;
    const float step = plot.getWidth() / (float) columns;
    if ((hold::Mode) mode == hold::Mode::Live || holding)
    {
        g.setColour(t.ink.withAlpha(0.2f));
        g.drawHorizontalLine((int) mid, plot.getX(), plot.getRight());
        juce::Path shape;
        for (int col = 0; col < columns; ++col)
        {
            const float x = plot.getX() + ((float) col + 0.5f) * step;
            const float hi = mid - wave[(size_t) (2 * col + 1)] * scale;
            if (col == 0)
                shape.startNewSubPath(x, hi);
            else
                shape.lineTo(x, hi);
        }
        for (int col = columns; --col >= 0;)
            shape.lineTo(plot.getX() + ((float) col + 0.5f) * step, mid - wave[(size_t) (2 * col)] * scale);
        shape.closeSubPath();
        g.setColour(ink.withMultipliedAlpha(0.3f));
        g.fillPath(shape);
        strokeGlowing(g, shape, ink.withMultipliedAlpha(0.6f), 0.8f);
    }
    if (!grains || !on)
        return;
    // The grains: each a mark where it reads, as tall as it is loud now.
    for (int i = 0; i < hold::HoldDisplay::kGrains; ++i)
    {
        const float pos = grainPos[(size_t) i], amp = grainAmp[(size_t) i];
        if (pos < 0.0f)
            continue;
        const float x = plot.getX() + juce::jlimit(0.0f, 1.0f, pos) * plot.getWidth();
        const float h = juce::jmax(2.0f, amp * plot.getHeight());
        g.setColour(ink.withMultipliedAlpha(0.25f + 0.6f * amp));
        g.fillRoundedRectangle(x - 1.5f, mid - h * 0.5f, 3.0f, h, 1.5f);
    }
}

void HoldDisplay::paint(juce::Graphics& g)
{
    const auto& t = theme();
    const auto bounds = getLocalBounds().toFloat();
    drawGlass(g, bounds);
    const auto ink = t.ink.withAlpha(on ? 1.0f : kOffInkAlpha);
    const float level = juce::jlimit(0.0f, 1.0f, (levelDb + 60.0f) / 60.0f);
    const bool toGate = feed == (int) hold::Feed::Gate && !gateBypassed;

    if (compact)
    {
        // One line: what it does and where it goes; the waveform beside it, its level under it.
        auto line = status();
        if (on)
            line << "  " << modeNames()[mode] << (toGate ? " > GATE" : " > OUT");
        drawReadout(g, line, bounds.reduced(10.0f, 0.0f).withWidth(bounds.getWidth() * 0.6f), t.ink.withAlpha(on ? 0.95f : kOffInkAlpha));
        const auto plot = bounds.withTrimmedLeft(bounds.getWidth() * 0.62f).reduced(8.0f, 5.0f).withTrimmedBottom(3.0f);
        paintWave(g, plot, true);
        g.setColour(ink.withMultipliedAlpha(0.8f));
        g.fillRect(plot.getX(), plot.getBottom() + 1.0f, plot.getWidth() * level, 1.5f);
        return;
    }

    const auto dimText = t.ink.withAlpha(on ? 0.7f : 0.55f);
    auto lit = [&](Spot a, Spot b = Spot::Nowhere, Spot c = Spot::Nowhere) {
        // Off, the ink is faint already: not fainter still.
        return ink.withMultipliedAlpha(hover == a || hover == b || hover == c || !on ? 1.0f : 0.9f);
    };

    // Top line: MODE with its arrows; DENSITY (grains at once) with its arrows on the right.
    g.setColour(lit(Spot::ModeDown, Spot::Mode, Spot::ModeUp));
    arrow(g, 10.0f, 11.0f, -1);
    arrow(g, spotArea(Spot::ModeUp).getCentreX(), 11.0f, 1);
    drawReadout(g, modeNames()[mode], spotArea(Spot::Mode), lit(Spot::ModeDown, Spot::Mode, Spot::ModeUp),
                juce::Justification::centred);
    g.setColour(lit(Spot::DensityDown, Spot::Density, Spot::DensityUp));
    arrow(g, spotArea(Spot::DensityDown).getCentreX(), 11.0f, -1);
    arrow(g, spotArea(Spot::DensityUp).getCentreX(), 11.0f, 1);
    drawReadout(g, juce::String(density) + "X", spotArea(Spot::Density), lit(Spot::DensityDown, Spot::Density, Spot::DensityUp),
                juce::Justification::centred);

    // The waveform with its grains and the level under it; what HOLD waits for, while nothing is held.
    const auto plot = bounds.reduced(10.0f, 0.0f).withTrimmedTop(24.0f).withTrimmedBottom(28.0f);
    paintWave(g, plot, true);
    g.setColour(t.ink.withAlpha(0.12f));
    g.fillRect(plot.getX(), plot.getBottom() + 2.0f, plot.getWidth(), 1.5f);
    g.setColour(ink.withMultipliedAlpha(0.8f));
    g.fillRect(plot.getX(), plot.getBottom() + 2.0f, plot.getWidth() * level, 1.5f);
    const bool sounding = on && ((hold::Mode) mode == hold::Mode::Live
                                 || (holding && ((hold::Mode) mode != hold::Mode::Latch || latched)));
    if (!sounding)
        drawReadout(g, status(), plot, dimText, juce::Justification::centred);

    // Bottom line: SAMPLER; ROOT in MIDI, or how long the held chord is.
    drawReadout(g, character == (int) hold::Character::Bit12 ? "12-BIT" : "CLEAN",
                spotArea(Spot::Character).withTrimmedLeft(8.0f), lit(Spot::Character));
    if ((hold::Mode) mode == hold::Mode::Midi)
    {
        g.setColour(lit(Spot::RootDown, Spot::Root, Spot::RootUp));
        arrow(g, spotArea(Spot::RootDown).getCentreX(), spotArea(Spot::RootDown).getCentreY(), -1);
        arrow(g, spotArea(Spot::RootUp).getCentreX(), spotArea(Spot::RootUp).getCentreY(), 1);
        drawReadout(g, noteName(root), spotArea(Spot::Root), lit(Spot::RootDown, Spot::Root, Spot::RootUp),
                    juce::Justification::centred);
    }
    else if (sounding && holding)
        drawReadout(g, juce::String(heldSeconds, 2) + " S", spotArea(Spot::Root).withRight(bounds.getRight() - 8.0f),
                    dimText, juce::Justification::centredRight);
}

// --- HoldPanel ---

HoldPanel::HoldPanel(Glide90Processor& p)
    : ModulePanel("Hold"), proc(p), on(p.state(), ids::holdOn, "ON"), freeze(p.state(), ids::holdFreeze, "FREEZE"),
      feed(p.state(), ids::holdFeed, (int) hold::Feed::Gate), display(p),
      follow(p.state(), ids::holdFollow, "FOLLOW", Knob::Size::Large), size(p.state(), ids::holdSize, "SIZE"),
      smear(p.state(), ids::holdSmear, "SMEAR"), spread(p.state(), ids::holdSpread, "SPREAD"),
      reverse(p.state(), ids::holdReverse, "REVERSE"), duck(p.state(), ids::holdDuck, "DUCK"),
      decay(p.state(), ids::holdDecay, "DECAY"), level(p.state(), ids::holdLevel, "LEVEL"),
      modeWatch(*p.state().getParameter(ids::holdMode),
                [this](float v) { freeze.setEnabled((hold::Mode) juce::roundToInt(v) == hold::Mode::Latch); }),
      gateWatch(*p.state().getParameter(ids::gateOn),
                [this](float v) {
                    feed.setForcedChoice(v > 0.5f ? std::nullopt : std::optional<int>((int) hold::Feed::Out));
                }),
      glideWatch(*p.state().getParameter(ids::glideOn), [this](float v) { follow.setEnabled(v > 0.5f); })
{
    setDraggable(false);
    feed.setComponentID(ids::holdFeed);
    feed.setLegendPrefix("> ");
    for (auto* c : std::initializer_list<juce::Component*> { &on, &freeze, &feed, &display, &follow, &size, &smear,
                                                             &spread, &reverse, &duck, &decay, &level })
        addAndMakeVisible(c);

    on.setTooltip("Hold on/off. It plays beside the glide and the reverse gate and adds no delay");
    freeze.setTooltip("LATCH: holds what just played, for as long as it is down");
    feed.setTooltip("Where HOLD goes. GATE: into the reverse gate, so the held chord swells backwards. OUT: straight "
                    "to the output. With the reverse gate off, always OUT");
    follow.setTooltip("How much the glide bar bends the held chord. 100 bends it with the guitar, 200 twice as far, "
                      "0 holds it still. The bar moves only while GLIDE is on");
    size.setTooltip("Length of each grain. Long is smooth, short is grainy");
    smear.setTooltip("How far each grain may wander from the start of the held chord. 0 is a steady loop");
    spread.setTooltip("Each grain a few cents off and panned apart: many slightly different guitars");
    reverse.setTooltip("How many grains play backwards");
    duck.setTooltip("How far the held chord drops while you play. It swells back as the strings die away");
    decay.setTooltip("How long a held chord takes to die away. INF holds it until the next pick");
    level.setTooltip("Level of the held chord");

    display.onUnfold = [this] {
        setFolded(false);
        if (onFold)
            onFold(false);
    };
    modeWatch.sendInitialUpdate();
    gateWatch.sendInitialUpdate();
    glideWatch.sendInitialUpdate();
    setFolded(holdFolded());
}

HoldPanel::~HoldPanel() = default;

void HoldPanel::setFolded(bool f)
{
    folded = f;
    for (auto* c : std::initializer_list<juce::Component*> { &follow, &size, &smear, &spread, &reverse, &duck, &decay,
                                                             &level })
        c->setVisible(!folded);
    display.setCompact(folded);
    resized();
    repaint();
}

juce::Rectangle<int> HoldPanel::freezeBounds() const
{
    return onKeyBounds().translated(-84, 0).withWidth(76);
}

juce::Rectangle<int> HoldPanel::feedBounds() const
{
    return freezeBounds().translated(-84, 0);
}

juce::Rectangle<int> HoldPanel::titleArea() const
{
    // Where a click folds or opens the strip: the title, up to the screen (folded) or the keys.
    const int right = folded ? display.getX() - 4 : feedBounds().getX() - 8;
    return { 0, 0, right, kTitleHeight };
}

void HoldPanel::paint(juce::Graphics& g)
{
    ModulePanel::paint(g);
    // Where the other modules have their grip: which way the strip opens.
    g.setColour(theme().textDim);
    juce::Path chevron;
    const float x = 17.0f, y = (float) kTitleHeight * 0.5f;
    if (folded)
        chevron.addTriangle(x - 3.0f, y - 5.0f, x - 3.0f, y + 5.0f, x + 4.0f, y);
    else
        chevron.addTriangle(x - 5.0f, y - 3.0f, x + 5.0f, y - 3.0f, x, y + 4.0f);
    g.fillPath(chevron);
}

void HoldPanel::resized()
{
    on.setBounds(onKeyBounds());
    freeze.setBounds(freezeBounds());
    feed.setBounds(feedBounds());
    if (folded)
    {
        // The screen shrinks into the title bar, between the title and the keys.
        display.setBounds(juce::Rectangle<int>(110, 6, feedBounds().getX() - 12 - 110, kTitleHeight - 12));
        return;
    }
    const int m = PanelLookAndFeel::kMargin, w = Knob::kColumn, h = Knob::kHeight;
    display.setBounds(m, kRowTop, kScreenWidth, h);
    int x = getWidth() - m - kKnobs * w;
    for (auto* k : { &follow, &size, &smear, &spread, &reverse, &duck, &decay, &level })
    {
        k->setBounds(x, kRowTop, w, h);
        x += w;
    }
}

void HoldPanel::mouseMove(const juce::MouseEvent& e)
{
    setMouseCursor(titleArea().contains(e.getPosition()) ? juce::MouseCursor::PointingHandCursor
                                                         : juce::MouseCursor::NormalCursor);
}

void HoldPanel::mouseDown(const juce::MouseEvent& e)
{
    if (!e.mods.isLeftButtonDown() || !titleArea().contains(e.getPosition()))
        return;
    setFolded(!folded);
    if (onFold)
        onFold(folded);
}

} // namespace glide90::ui
