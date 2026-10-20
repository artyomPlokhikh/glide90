#include "HoldPanel.h"

#include "PanelLookAndFeel.h"
#include "Parameters.h"
#include "PluginProcessor.h"

namespace glide90::ui
{

// --- HoldDisplay ---

HoldDisplay::HoldDisplay(Glide90Processor& p) : proc(p), source(p.holdDisplay())
{
    setTooltip("What HOLD holds: the chord captured at the last pick (or frozen, or the last moments in LIVE), "
               "with the grains that play it");
    frame(0.0); // what there is now, before the first frame of the display
}

HoldDisplay::~HoldDisplay() = default;

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
    set(on, proc.state().getRawParameterValue(ids::holdOn)->load() > 0.5f);
    set(mode, choice(ids::holdMode));
    set(latched, proc.state().getRawParameterValue(ids::holdLatch)->load() > 0.5f);
    set(toGate, choice(ids::holdFeed) == (int) hold::Feed::Gate
                    && proc.state().getRawParameterValue(ids::bypass)->load() < 0.5f);
    set(holding, source.holding.load());

    const bool live = (hold::Mode) mode == hold::Mode::Live;
    if (live)
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

void HoldDisplay::paint(juce::Graphics& g)
{
    const auto& t = theme();
    const auto bounds = getLocalBounds().toFloat();
    drawGlass(g, bounds);
    const auto ink = t.ink.withAlpha(on ? 1.0f : 0.35f);
    const auto plot = bounds.withTrimmedLeft(14.0f).withTrimmedRight(14.0f).withTrimmedTop(34.0f).withTrimmedBottom(26.0f);
    const float mid = plot.getCentreY();
    const auto m = (hold::Mode) mode;
    const bool live = m == hold::Mode::Live;

    // The waveform, scaled to its loudest so a quiet chord still shows.
    float loudest = 1e-4f;
    for (float v : wave)
        loudest = juce::jmax(loudest, std::abs(v));
    const float scale = plot.getHeight() * 0.5f / loudest;
    const int columns = hold::HoldDisplay::kColumns;
    const float step = plot.getWidth() / (float) columns;
    g.setColour(t.ink.withAlpha(0.25f));
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
    const bool showWave = live || holding;
    if (showWave)
    {
        g.setColour(ink.withMultipliedAlpha(0.32f));
        g.fillPath(shape);
        strokeGlowing(g, shape, ink.withMultipliedAlpha(0.6f), 0.8f);
    }

    // The grains: each a window over the stretch it reads, as tall as it is loud now.
    for (int i = 0; i < hold::HoldDisplay::kGrains; ++i)
    {
        const float pos = grainPos[(size_t) i], amp = grainAmp[(size_t) i];
        if (pos < 0.0f || !on)
            continue;
        const float x = plot.getX() + juce::jlimit(0.0f, 1.0f, pos) * plot.getWidth();
        const float h = juce::jmax(2.0f, amp * plot.getHeight() * 0.9f);
        g.setColour(ink.withMultipliedAlpha(0.25f + 0.6f * amp));
        g.fillRoundedRectangle(x - 1.5f, plot.getBottom() - h, 3.0f, h, 1.5f);
    }

    // The layer's level along the bottom, -60 to 0 dB.
    const float level = juce::jlimit(0.0f, 1.0f, (levelDb + 60.0f) / 60.0f);
    g.setColour(t.ink.withAlpha(0.12f));
    g.fillRect(plot.getX(), plot.getBottom() + 4.0f, plot.getWidth(), 2.0f);
    g.setColour(ink.withMultipliedAlpha(0.8f));
    g.fillRect(plot.getX(), plot.getBottom() + 4.0f, plot.getWidth() * level, 2.0f);

    juce::String status;
    if (!on)
        status = "HOLD OFF";
    else if (live)
        status = "LIVE";
    else if (m == hold::Mode::Latch && !latched)
        status = "FREEZE TO HOLD";
    else if (!holding)
        status = m == hold::Mode::Midi ? "PICK, THEN PLAY A NOTE" : "WAITING FOR A PICK";
    else
        status = (m == hold::Mode::Latch ? "FROZEN " : "HOLDING ") + juce::String(heldSeconds, 2) + " S";
    drawReadout(g, status, { 12.0f, 6.0f, 300.0f, 22.0f }, t.ink.withAlpha(on ? 0.8f : 0.35f));
    drawReadout(g, toGate ? "INTO GATE" : "TO OUTPUT", { bounds.getRight() - 212.0f, 6.0f, 200.0f, 22.0f }, ink,
                juce::Justification::right);
    const juce::Rectangle<float> axis { plot.getX(), plot.getBottom() + 8.0f, plot.getWidth(), 14.0f };
    if (live)
        drawReadout(g, juce::String(hold::HoldDisplay::kLiveSeconds, 1) + " S AGO", axis, t.ink.withAlpha(on ? 0.45f : 0.25f));
    drawReadout(g, live ? juce::String("NOW") : juce::String("LEVEL"), axis, t.ink.withAlpha(on ? 0.45f : 0.25f),
                juce::Justification::right);
}

// --- HoldPanel ---

HoldPanel::HoldPanel(Glide90Processor& p)
    : ModulePanel("Hold"), proc(p), on(p.state(), ids::holdOn, "ON"), freeze(p.state(), ids::holdLatch, "FREEZE"),
      mode(p.state(), ids::holdMode, "MODE", { "STRIKE", "LATCH", "LIVE", "MIDI" }, { 0, 1, 2, 3 }),
      feed(p.state(), ids::holdFeed, "FEED", { "GATE", "OUT" }, { 0, 1 }),
      character(p.state(), ids::holdCharacter, "SAMPLER", { "CLEAN", "12-BIT" }, { 0, 1 }), display(p),
      sensitivity(p.state(), ids::holdSensitivity, "PICK SENS"), size(p.state(), ids::holdSize, "SIZE", Knob::Size::Large),
      density(p.state(), ids::holdDensity, "DENSITY"), smear(p.state(), ids::holdSmear, "SMEAR"),
      spread(p.state(), ids::holdSpread, "SPREAD"), follow(p.state(), ids::holdFollow, "FOLLOW", Knob::Size::Hero),
      reverse(p.state(), ids::holdReverse, "REVERSE"), root(p.state(), ids::holdRoot, "ROOT"),
      duck(p.state(), ids::holdDuck, "DUCK"), decay(p.state(), ids::holdDecay, "DECAY"),
      level(p.state(), ids::holdLevel, "HOLD LEVEL")
{
    setDraggable(false);
    for (auto* c : std::initializer_list<juce::Component*> { &on, &freeze, &mode, &feed, &character, &display,
                                                             &sensitivity, &size, &density, &smear, &spread, &follow,
                                                             &reverse, &root, &duck, &decay, &level })
        addAndMakeVisible(c);

    on.setTooltip("Hold on/off. It plays beside the glide and the reverse gate and adds no delay");
    freeze.setTooltip("LATCH: holds what just played, for as long as it is down");
    mode.setTooltip("STRIKE: each pick attack captures the chord and holds it. LATCH: FREEZE holds what just played. "
                    "LIVE: grains of the last moments, nothing held. MIDI: captured on each pick, heard while a MIDI "
                    "note is held, at that note's pitch");
    feed.setTooltip("GATE: into the reverse gate, so the held chord swells backwards. OUT: straight to the output");
    character.setTooltip("12-BIT: captured like a late-80s sampler, grainy and dull at the top");
    sensitivity.setTooltip("How easily a pick attack captures a new chord");
    size.setTooltip("Length of each grain. Long is smooth, short is grainy");
    density.setTooltip("How many grains play at once");
    smear.setTooltip("How far each grain may wander from the start of the held chord. 0 is a steady loop");
    spread.setTooltip("Each grain a few cents off and panned apart: many slightly different guitars");
    follow.setTooltip("How much the glide bar bends the held chord. 100 bends it with the guitar, 200 twice as far, "
                      "0 holds it still");
    reverse.setTooltip("How many grains play backwards");
    root.setTooltip("MIDI: the note that plays the held chord at its own pitch");
    duck.setTooltip("How far the held chord drops while you play. It swells back as the strings die away");
    decay.setTooltip("How long a held chord takes to die away. INF holds it until the next pick");
    level.setTooltip("Level of the held chord");

    updateForMode();
    startTimerHz(10);
}

HoldPanel::~HoldPanel()
{
    stopTimer();
}

int HoldPanel::choice(const char* id) const
{
    return juce::roundToInt(proc.state().getRawParameterValue(id)->load());
}

int HoldPanel::colX(int column) const
{
    return (getWidth() - widthFor(kColumns)) / 2 + columnX(column);
}

void HoldPanel::timerCallback()
{
    updateForMode();
}

void HoldPanel::updateForMode()
{
    const int m = choice(ids::holdMode);
    if (m == shownMode)
        return;
    shownMode = m;
    // What each mode listens to: always in its place, dimmed where the mode does not use it.
    const auto holdMode = (hold::Mode) m;
    sensitivity.setEnabled(holdMode == hold::Mode::Strike || holdMode == hold::Mode::Midi);
    freeze.setEnabled(holdMode == hold::Mode::Latch);
    root.setEnabled(holdMode == hold::Mode::Midi);
    repaint();
}

void HoldPanel::paint(juce::Graphics& g)
{
    ModulePanel::paint(g);
    // FREEZE has no knob to carry its name.
    g.setFont(legendFont(13.5f));
    g.setColour(freeze.isEnabled() ? theme().text : theme().textDim.withMultipliedAlpha(0.6f));
    g.drawFittedText(themed("LATCH"), juce::Rectangle<int>(colX(2), kRow1, Knob::kColumn, 15), juce::Justification::centred, 1);

    drawGroupRule(g, "Colour", (float) colX(0), (float) (3 * Knob::kColumn - 8), (float) kRuleY);
    drawGroupRule(g, "Mix", (float) colX(3) + 8.0f, (float) (4 * Knob::kColumn - 16), (float) kRuleY);
}

void HoldPanel::resized()
{
    const int h = Knob::kHeight, w = Knob::kColumn;
    on.setBounds(onKeyBounds());
    display.setBounds(columnX(0), kScreenTop, getWidth() - 2 * PanelLookAndFeel::kMargin, kScreenHeight);

    // CAPTURE, GRAINS, and FOLLOW down both rows.
    mode.setBounds(colX(0), kRow1, w, h);
    sensitivity.setBounds(colX(1), kRow1, w, h);
    freeze.setBounds(juce::Rectangle<int>(w - 16, 28).withCentre({ colX(2) + w / 2, kRow1 + 50 }));
    size.setBounds(colX(3), kRow1, w, h);
    density.setBounds(colX(4), kRow1, w, h);
    smear.setBounds(colX(5), kRow1, w, h);
    spread.setBounds(colX(6), kRow1, w, h);
    follow.setBounds(colX(7), kRow1, w, kRow2 + h - kRow1);

    // COLOUR | MIX.
    reverse.setBounds(colX(0), kRow2, w, h);
    character.setBounds(colX(1), kRow2, w, h);
    root.setBounds(colX(2), kRow2, w, h);
    duck.setBounds(colX(3), kRow2, w, h);
    decay.setBounds(colX(4), kRow2, w, h);
    level.setBounds(colX(5), kRow2, w, h);
    feed.setBounds(colX(6), kRow2, w, h);
}

} // namespace glide90::ui
