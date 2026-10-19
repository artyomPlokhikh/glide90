#include "GlidePanel.h"

#include "PanelLookAndFeel.h"
#include "Parameters.h"
#include "PluginProcessor.h"

namespace glide90::ui
{

GlidePanel::GlidePanel(Glide90Processor& p)
    : proc(p), on(p.state(), ids::glideOn, "ON"),
      mode(p.state(), ids::glideMode, "MODE", { "FREE", "SYNC", "TRIGGER", "MANUAL" }), display(p),
      period(p.state(), ids::glidePeriod, "RATE", Knob::Size::Large),
      division(p.state(), ids::glideDivision, "RATE", Knob::Size::Large),
      bar(p.state(), ids::glideBar, "BAR", Knob::Size::Large),
      depth(p.state(), ids::glideDepth, "DEPTH", Knob::Size::Large), random(p.state(), ids::glideRandom, "RANDOM"),
      smooth(p.state(), ids::glideSmooth, "SMOOTH"), twinDetune(p.state(), ids::glideTwinDetune, "TWIN"),
      twinLevel(p.state(), ids::glideTwinLevel, "TWIN LEVEL"), mix(p.state(), ids::glideMix, "MIX"),
      sensitivity(p.state(), ids::glideSensitivity, "PICK SENS"), dynamics(p.state(), ids::glideDynamics, "DYNAMICS"),
      direction(p.state(), ids::glideDirection, "DIRECTION", { "UP", "BOTH", "DOWN" }, { 2, 1, 0 }),
      position(p.state(), ids::glidePosition, "POSITION", { "PRE", "POST" }, { 0, 1 })
{
    for (auto* c : std::initializer_list<juce::Component*> { &on, &mode, &source, &display, &period, &division, &bar,
                                                             &depth, &random, &smooth, &twinDetune, &twinLevel, &mix,
                                                             &sensitivity, &dynamics, &direction, &position })
        addAndMakeVisible(c);

    // Short names that fit the window (ten characters); the menu has the full ones.
    source.getText = [this] {
        static const juce::StringArray trigger { "PICK", "MIDI NOTE" };
        static const juce::StringArray manual { "BAR KNOB", "PITCH BEND", "MOD WHEEL", "EXPR CC11" };
        switch ((sway::GlideMode) choice(ids::glideMode))
        {
            case sway::GlideMode::Trigger: return trigger[choice(ids::glideTriggerSource)];
            case sway::GlideMode::Manual: return manual[choice(ids::glideManualSource)];
            case sway::GlideMode::Free:
            case sway::GlideMode::Sync:
            default: return juce::String("INTERNAL");
        }
    };
    source.fillMenu = [this](juce::PopupMenu& menu) {
        const bool trigger = (sway::GlideMode) choice(ids::glideMode) == sway::GlideMode::Trigger;
        auto* param = proc.state().getParameter(trigger ? ids::glideTriggerSource : ids::glideManualSource);
        const auto names = dynamic_cast<juce::AudioParameterChoice&>(*param).choices;
        const int current = choice(trigger ? ids::glideTriggerSource : ids::glideManualSource);
        for (int i = 0; i < names.size(); ++i)
            menu.addItem(names[i], true, i == current, [param, i] {
                param->beginChangeGesture();
                param->setValueNotifyingHost(param->convertTo0to1((float) i));
                param->endChangeGesture();
            });
    };

    on.setTooltip("Glide on/off. Its delay (about 26 ms, compensated by the host) stays either way, so switching "
                  "does not move the timing");
    mode.setTooltip("FREE: loops at RATE. SYNC: locked to the song tempo. TRIGGER: restarts on each pick attack or "
                    "MIDI note. MANUAL: you move the bar (knob, pitch bend, mod wheel or expression pedal)");
    source.setTooltip("TRIGGER: what restarts the shape. MANUAL: what moves the bar");
    period.setTooltip("Length of one cycle in milliseconds");
    division.setTooltip("Length of one cycle as a note value at the host tempo");
    bar.setTooltip("Bar position for MANUAL with BAR KNOB as the source: automate it in the host");
    depth.setTooltip("How far the bar bends the pitch, in cents (100 = a semitone)");
    random.setTooltip("A human hand: each cycle a little different in depth and timing, plus drift");
    smooth.setTooltip("How lazily the bar follows the shape or the controller");
    twinDetune.setTooltip("A second string tuned slightly sharp, moved by the same bar: the two beat against each other");
    twinLevel.setTooltip("Level of the second string");
    mix.setTooltip("Glide wet / dry");
    sensitivity.setTooltip("How easily a pick attack restarts the shape (TRIGGER from PICK ATTACK)");
    dynamics.setTooltip("How much a strike's strength sets the depth: 0 = every strike bends the full DEPTH, "
                        "100 = a soft strike bends a little, a hard one all the way (pick attack or MIDI velocity)");
    direction.setTooltip("DOWN: like a Jazzmaster bar. BOTH: around the open pitch. UP: above it");
    position.setTooltip("PRE: the glide goes into the reverb, so the reverse swell smears the bends. POST: after it");

    updateForMode();
    startTimerHz(10);
}

GlidePanel::~GlidePanel()
{
    stopTimer();
}

int GlidePanel::choice(const char* id) const
{
    return juce::roundToInt(proc.state().getRawParameterValue(id)->load());
}

void GlidePanel::timerCallback()
{
    updateForMode();
}

void GlidePanel::updateForMode()
{
    const int m = choice(ids::glideMode), t = choice(ids::glideTriggerSource), s = choice(ids::glideManualSource);
    if (m == shownMode && t == shownTrigger && s == shownManual)
        return;
    shownMode = m;
    shownTrigger = t;
    shownManual = s;

    // One RATE slot: milliseconds, a note value, or the bar itself.
    const auto glideMode = (sway::GlideMode) m;
    const bool manual = glideMode == sway::GlideMode::Manual;
    period.setVisible(glideMode == sway::GlideMode::Free || glideMode == sway::GlideMode::Trigger);
    division.setVisible(glideMode == sway::GlideMode::Sync);
    bar.setVisible(manual);
    bar.setEnabled((ManualSource) s == ManualSource::BarKnob);
    random.setEnabled(!manual);
    source.setEnabled(glideMode == sway::GlideMode::Trigger || manual);
    sensitivity.setEnabled(glideMode == sway::GlideMode::Trigger && (TriggerSource) t == TriggerSource::Pick);
    dynamics.setEnabled(glideMode == sway::GlideMode::Trigger);
    repaint();
}

void GlidePanel::paint(juce::Graphics& g)
{
    drawRackUnit(g, getLocalBounds());
    drawUnitName(g, "GLIDE", getLocalBounds());
    g.setFont(legendFont(13.0f));
    g.setColour(source.isEnabled() ? colours::silkscreen : colours::silkscreenDim);
    g.drawText("SOURCE", source.getBounds().withY(kRow2).withHeight(15), juce::Justification::centred);

    // Drive | screen | motion over sound.
    const float bottom = (float) kRow2 + Knob::kHeight;
    drawEngravedLine(g, (float) display.getX() - 10.0f, (float) kRow1, bottom);
    drawEngravedLine(g, (float) display.getRight() + 10.0f, (float) kRow1, bottom);
    const float ruleY = (float) (kRow1 + Knob::kHeight + kRow2) * 0.5f;
    drawEngravedRule(g, ruleY, (float) PanelLookAndFeel::kMargin, (float) display.getX() - 20.0f);
    drawEngravedRule(g, ruleY, (float) display.getRight() + 20.0f, (float) getWidth() - PanelLookAndFeel::kMargin);
}

void GlidePanel::resized()
{
    const int m = PanelLookAndFeel::kMargin, h = Knob::kHeight;
    const int lw = Knob::widthFor(Knob::Size::Large), sw = Knob::widthFor(Knob::Size::Small);

    // DRIVE: what moves the bar. On and the mode above; what the mode uses below it.
    on.setBounds(m, kRow1 + 30, 56, 44);
    mode.setBounds(m + 70, kRow1, 216, h);
    for (auto* k : { &period, &division, &bar })
        k->setBounds(m, kRow2, lw, h);
    source.setBounds(m + lw + 4, kRow2 + 28, 104, 28);
    sensitivity.setBounds(source.getRight() + 4, kRow2, sw, h);
    dynamics.setBounds(sensitivity.getRight(), kRow2, sw, h);

    // The screen, the full height of both rows.
    constexpr int column = 72;
    const int screenLeft = dynamics.getRight() + 22, screenRight = getWidth() - m - 4 * column - 22;
    display.setBounds(screenLeft, kRow1, screenRight - screenLeft, kRow2 + h - kRow1);

    // MOTION over SOUND: four columns.
    const int x0 = getWidth() - m - 4 * column;
    auto cell = [&](juce::Component& c, int col, int top) { c.setBounds(x0 + col * column, top, column, h); };
    cell(depth, 0, kRow1);
    cell(direction, 1, kRow1);
    cell(random, 2, kRow1);
    cell(smooth, 3, kRow1);
    cell(twinDetune, 0, kRow2);
    cell(twinLevel, 1, kRow2);
    cell(mix, 2, kRow2);
    cell(position, 3, kRow2);
}

} // namespace glide90::ui
