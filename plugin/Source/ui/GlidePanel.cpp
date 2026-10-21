#include "GlidePanel.h"

#include "PanelLookAndFeel.h"
#include "Parameters.h"
#include "PluginProcessor.h"

namespace glide90::ui
{

GlidePanel::GlidePanel(Glide90Processor& p)
    : ModulePanel(names::glide), proc(p), on(p.state(), ids::glideOn, "ON"),
      mode(p.state(), ids::glideMode, "MODE", { "FREE", "SYNC", "TRIGGER", "MANUAL" }, { 0, 1, 2, 3 }),
      direction(p.state(), ids::glideDirection, "DIRECTION", { "UP", "BOTH", "DOWN" }, { 2, 1, 0 }), display(p),
      period(p.state(), ids::glidePeriod, "RATE", Knob::Size::Large),
      division(p.state(), ids::glideDivision, "RATE", Knob::Size::Large),
      bar(p.state(), ids::glideBar, "BAR", Knob::Size::Large),
      depth(p.state(), ids::glideDepth, "DEPTH", Knob::Size::Large), random(p.state(), ids::glideRandom, "RANDOM"),
      smooth(p.state(), ids::glideSmooth, "SMOOTH"), dynamics(p.state(), ids::glideDynamics, "DYNAMICS")
{
    for (auto* c : std::initializer_list<juce::Component*> { &on, &mode, &source, &display, &period, &division, &bar,
                                                             &depth, &random, &smooth, &dynamics, &direction })
        addAndMakeVisible(c);

    // Names that fit the window: these where the look has room, the shorter ones where it has not (the
    // dot matrix takes eight characters). The menu has the full ones.
    source.getText = [this] {
        static const juce::StringArray trigger { "PICK", "MIDI NOTE" }, triggerShort { "PICK", "MIDI" };
        static const juce::StringArray manual { "BAR KNOB", "PITCH BEND", "MOD WHEEL", "EXPR CC11" },
            manualShort { "BAR KNOB", "BEND", "MOD WHL", "CC11" };
        auto fitting = [this](const juce::String& name, const juce::String& shortName) {
            return readoutWidth(name) <= (float) source.getWidth() - 12.0f ? name : shortName;
        };
        switch ((sway::GlideMode) choice(ids::glideMode))
        {
            case sway::GlideMode::Trigger:
            {
                const int i = choice(ids::glideTriggerSource);
                return fitting(trigger[i], triggerShort[i]);
            }
            case sway::GlideMode::Manual:
            {
                const int i = choice(ids::glideManualSource);
                return fitting(manual[i], manualShort[i]);
            }
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
    dynamics.setTooltip("How much a strike's strength sets the depth: 0 = every strike bends the full DEPTH, "
                        "100 = a soft strike bends a little, a hard one all the way (pick attack or MIDI velocity)");
    direction.setTooltip("DOWN: like a Jazzmaster bar. BOTH: around the open pitch. UP: above it");

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
    // What the mode listens to: always in its place, dimmed where the mode does not listen.
    const bool trigger = glideMode == sway::GlideMode::Trigger;
    source.setEnabled(trigger || manual);
    dynamics.setEnabled(trigger);
    controlGroupActive = trigger || manual;
    repaint();
}

void GlidePanel::paint(juce::Graphics& g)
{
    ModulePanel::paint(g);
    // SOURCE has no knob to carry its name.
    g.setFont(legendFont(13.5f));
    g.setColour(source.isEnabled() ? theme().text : theme().textDim.withMultipliedAlpha(kDimmedAlpha));
    g.drawFittedText(themed("SOURCE"), juce::Rectangle<int>(columnX(0), kRow2, Knob::kColumn, 15), juce::Justification::centred, 1);

    drawGroupRule(g, "Control", (float) columnX(0), (float) (2 * Knob::kColumn - 8), (float) kRuleY, !controlGroupActive);
}

void GlidePanel::resized()
{
    const int h = Knob::kHeight, w = Knob::kColumn;
    on.setBounds(onKeyBounds());
    display.setBounds(columnX(0), kScreenTop, kColumns * w, kScreenHeight);

    // MOTION.
    mode.setBounds(columnX(0), kRow1, w, h);
    for (auto* k : { &period, &division, &bar })
        k->setBounds(columnX(1), kRow1, w, h);
    depth.setBounds(columnX(2), kRow1, w, h);
    direction.setBounds(columnX(3), kRow1, w, h);
    random.setBounds(columnX(4), kRow1, w, h);
    smooth.setBounds(columnX(5), kRow1, w, h);

    // CONTROL: what TRIGGER and MANUAL listen to, under MODE.
    source.setBounds(juce::Rectangle<int>(Knob::kColumn, 26).withCentre({ columnX(0) + w / 2, kRow2 + 50 }));
    dynamics.setBounds(columnX(1), kRow2, w, h);
}

} // namespace glide90::ui
