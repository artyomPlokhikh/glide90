#include "GlidePanel.h"

#include "PanelLookAndFeel.h"
#include "Parameters.h"
#include "PluginProcessor.h"

namespace glide90::ui
{

GlidePanel::GlidePanel(Glide90Processor& p)
    : ModulePanel(names::glide), proc(p), on(p.state(), ids::glideOn, "ON"), sync(p.state(), ids::glideSync, "SYNC"),
      mode(p.state(), ids::glideMode, "MODE", { "LOOP", "TRIGGER", "MANUAL" }, { 0, 1, 2 }),
      direction(p.state(), ids::glideDirection, "DIRECTION", { "UP", "BOTH", "DOWN" }, { 2, 1, 0 }), display(p),
      period(p.state(), ids::glidePeriod, "RATE", Knob::Size::Large),
      division(p.state(), ids::glideDivision, "RATE", Knob::Size::Large),
      bar(p.state(), ids::glideBar, "BAR", Knob::Size::Large),
      depth(p.state(), ids::glideDepth, "DEPTH", Knob::Size::Large), random(p.state(), ids::glideRandom, "RANDOM"),
      smooth(p.state(), ids::glideSmooth, "SMOOTH"), dynamics(p.state(), ids::glideDynamics, "DYNAMICS"),
      pickSense(p.state(), ids::pickSensitivity, [&p] { return p.pickAttacks(); })
{
    for (auto* c : std::initializer_list<juce::Component*> { &on, &mode, &display, &period, &division, &bar, &depth,
                                                             &random, &smooth, &dynamics, &direction, &sync, &pickSense })
        addAndMakeVisible(c);
    sync.getProperties().set(PanelLookAndFeel::kIcon, "note");
    pickSense.knob().setTooltip("How easily a pick restarts the shape");

    mode.setTooltip("LOOP: repeats the shape. TRIGGER: plays it once on each pick. MANUAL: no shape, the BAR knob is the bar");
    sync.setTooltip("RATE as a note value at the host tempo; LOOP stays on the beat");
    period.setTooltip("Length of one pass; larger = slower");
    division.setTooltip("Length of one pass as a note value at the host tempo");
    bar.setTooltip("Where the bar is. Automate it, or map a pedal or controller to it in your host");
    depth.setTooltip("How far the bar bends. 100 ct = a semitone");
    random.setTooltip("Each pass a little different in depth and timing, plus slow drift");
    smooth.setTooltip("How lazily the bar follows, up to 150 ms");
    dynamics.setTooltip("Pick strength sets the depth. 0: always full DEPTH. 100: soft = small bend, hard = full");
    direction.setTooltip("DOWN: the bar only lowers the pitch. BOTH: it swings around the open pitch. UP: it only raises it");

    updateForMode();
    startTimerHz(10);
}

GlidePanel::~GlidePanel()
{
    stopTimer();
}

int GlidePanel::choice(const char* id) const
{
    return juce::roundToInt(proc.value(id));
}

void GlidePanel::timerCallback()
{
    updateForMode();
}

void GlidePanel::updateForMode()
{
    const int m = choice(ids::glideMode), s = choice(ids::glideSync);
    if (m == shownMode && s == shownSync)
        return;
    shownMode = m;
    shownSync = s;

    // One RATE slot: milliseconds, a note value, or in MANUAL the bar itself.
    const auto glideMode = (sway::GlideMode) m;
    const bool manual = glideMode == sway::GlideMode::Manual;
    period.setVisible(!manual && s == 0);
    division.setVisible(!manual && s != 0);
    bar.setVisible(manual);
    sync.setVisible(!manual);
    random.setEnabled(!manual);
    // What listens to the pick: always in its place, dimmed where the mode does not listen.
    const bool trigger = glideMode == sway::GlideMode::Trigger;
    dynamics.setEnabled(trigger);
    pickSense.setEnabled(trigger);
    pickGroupActive = trigger;
    repaint();
}

void GlidePanel::paint(juce::Graphics& g)
{
    ModulePanel::paint(g);
    // PICK: what TRIGGER listens to. HAND: how the bar moves.
    drawGroupRule(g, "Pick", (float) columnX(0), (float) (2 * Knob::kColumn - 8), (float) kRuleY, !pickGroupActive);
    drawGroupRule(g, "Hand", (float) columnX(2) + 8.0f, (float) (2 * Knob::kColumn - 8), (float) kRuleY, false);
}

void GlidePanel::resized()
{
    const int h = Knob::kHeight, w = Knob::kColumn;
    on.setBounds(onKeyBounds());
    display.setBounds(columnX(0), kScreenTop, kColumns * w, kScreenHeight);

    // What the bar does: its mode, how long, how far, which way.
    mode.setBounds(columnX(0), kRow1, w, h);
    for (auto* k : { &period, &division, &bar })
        k->setBounds(columnX(1), kRow1, w, h);
    depth.setBounds(columnX(2), kRow1, w, h);
    direction.setBounds(columnX(3), kRow1, w, h);
    // SYNC belongs to RATE: in the corner of its slot, beside its name.
    sync.setBounds(columnX(1) + w - 26, kRow1 - 4, 26, 20);

    // What listens to the pick, then the hand on the bar.
    pickSense.setBounds(columnX(0), kRow2, w, h);
    dynamics.setBounds(columnX(1), kRow2, w, h);
    random.setBounds(columnX(2), kRow2, w, h);
    smooth.setBounds(columnX(3), kRow2, w, h);
}

} // namespace glide90::ui
