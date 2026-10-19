#include "GlidePanel.h"

#include "PanelLookAndFeel.h"
#include "Parameters.h"
#include "PluginProcessor.h"

namespace glide90::ui
{

GlidePanel::GlidePanel(Glide90Processor& p)
    : proc(p), mode(p.state(), ids::glideMode, colours::glideAccent),
      direction(p.state(), ids::glideDirection, colours::glideAccent),
      position(p.state(), ids::glidePosition, colours::glideAccent), display(p),
      period(p.state(), ids::glidePeriod, "RATE", colours::glideAccent),
      division(p.state(), ids::glideDivision, "RATE", colours::glideAccent),
      depth(p.state(), ids::glideDepth, "DEPTH", colours::glideAccent),
      random(p.state(), ids::glideRandom, "RANDOM", colours::glideAccent),
      smooth(p.state(), ids::glideSmooth, "SMOOTH", colours::glideAccent),
      sensitivity(p.state(), ids::glideSensitivity, "SENSITIVITY", colours::glideAccent),
      bar(p.state(), ids::glideBar, "BAR", colours::glideAccent),
      twinDetune(p.state(), ids::glideTwinDetune, "TWIN DETUNE", colours::glideAccent),
      twinLevel(p.state(), ids::glideTwinLevel, "TWIN LEVEL", colours::glideAccent),
      mix(p.state(), ids::glideMix, "MIX", colours::glideAccent)
{
    on.getProperties().set(PanelLookAndFeel::kAccent, (juce::int64) colours::glideAccent.getARGB());
    onAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(p.state(), ids::glideOn, on);

    auto fill = [&p](juce::ComboBox& box, const char* id) {
        if (auto* choice = dynamic_cast<juce::AudioParameterChoice*>(p.state().getParameter(id)))
            box.addItemList(choice->choices, 1);
    };
    fill(triggerSource, ids::glideTriggerSource);
    fill(manualSource, ids::glideManualSource);
    triggerAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        p.state(), ids::glideTriggerSource, triggerSource);
    manualAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        p.state(), ids::glideManualSource, manualSource);

    for (auto* c : std::initializer_list<juce::Component*> { &on, &mode, &direction, &position, &display, &period,
                                                             &division, &depth, &random, &smooth, &sensitivity, &bar,
                                                             &twinDetune, &twinLevel, &mix, &triggerSource, &manualSource })
        addAndMakeVisible(c);

    on.setTooltip("Glide on/off");
    mode.setTooltip("FREE: loops at RATE. SYNC: locked to the song tempo. TRIGGER: restarts on each pick attack or "
                    "MIDI note. MANUAL: you move the bar (knob, pitch bend, mod wheel or expression pedal)");
    direction.setTooltip("DOWN: like a Jazzmaster bar. BOTH: around the open pitch. UP: above it");
    position.setTooltip("PRE: the glide goes into the reverb (as Kevin Shields played it). POST: after it");
    period.setTooltip("Length of one cycle in milliseconds");
    division.setTooltip("Length of one cycle as a note value at the host tempo");
    depth.setTooltip("How far the bar bends the pitch, in cents (100 = a semitone)");
    random.setTooltip("A human hand: each cycle a little different in depth and timing, plus drift");
    smooth.setTooltip("How lazily the bar follows the shape or the controller");
    sensitivity.setTooltip("How easily a pick attack restarts the shape");
    bar.setTooltip("Bar position for MANUAL / BAR KNOB: automate it in the host");
    twinDetune.setTooltip("A second string tuned slightly sharp, moved by the same bar: MBV-style beating");
    twinLevel.setTooltip("Level of the second string");
    mix.setTooltip("Glide wet / dry");

    updateForMode();
    startTimerHz(10);
}

GlidePanel::~GlidePanel()
{
    stopTimer();
}

void GlidePanel::timerCallback()
{
    updateForMode();
}

void GlidePanel::updateForMode()
{
    const auto choice = [this](const char* id) { return juce::roundToInt(proc.state().getRawParameterValue(id)->load()); };
    const int m = choice(ids::glideMode), t = choice(ids::glideTriggerSource), s = choice(ids::glideManualSource);
    if (m == shownMode && t == shownTrigger && s == shownManual)
        return;
    shownMode = m;
    shownTrigger = t;
    shownManual = s;

    const auto glideMode = (sway::GlideMode) m;
    const bool manual = glideMode == sway::GlideMode::Manual;
    period.setVisible(glideMode != sway::GlideMode::Sync);
    division.setVisible(glideMode == sway::GlideMode::Sync);
    period.setEnabled(!manual);
    random.setEnabled(!manual);
    triggerSource.setVisible(glideMode == sway::GlideMode::Trigger);
    manualSource.setVisible(manual);

    // One slot for whichever of SENSITIVITY and BAR applies.
    const bool showBar = manual && (ManualSource) s == ManualSource::BarKnob;
    bar.setVisible(showBar);
    sensitivity.setVisible(!showBar);
    sensitivity.setEnabled(glideMode == sway::GlideMode::Trigger && (TriggerSource) t == TriggerSource::Pick);
    repaint();
}

void GlidePanel::paint(juce::Graphics& g)
{
    const auto r = getLocalBounds().toFloat();
    g.setGradientFill(juce::ColourGradient(colours::glideTop, 0.0f, 0.0f, colours::glideBottom, 0.0f, r.getBottom(), false));
    g.fillRect(r);
    g.setColour(juce::Colours::black);
    g.drawHorizontalLine(0, 0.0f, r.getRight());

    g.setColour(colours::legendDim);
    g.setFont(panelFont(10.5f, true));
    g.drawText("VIBRATO BAR", 120, 18, 120, 14, juce::Justification::centredLeft);
    g.setFont(panelFont(11.5f, true));
    g.setColour(colours::legend);
    g.drawText("DIRECTION", direction.getBounds().translated(0, -16).withHeight(14), juce::Justification::centredLeft);
    g.drawText("POSITION", position.getBounds().translated(0, -16).withHeight(14), juce::Justification::centredLeft);
    const auto current = (sway::GlideMode) shownMode;
    if (current == sway::GlideMode::Trigger || current == sway::GlideMode::Manual)
        g.drawText(current == sway::GlideMode::Trigger ? "TRIGGER" : "SOURCE",
                   triggerSource.getBounds().translated(0, -16).withHeight(14), juce::Justification::centredLeft);
}

void GlidePanel::resized()
{
    on.setBounds(22, 12, 96, 26);
    mode.setBounds(250, 12, 300, 28);

    display.setBounds(22, 52, 528, 252);

    // Controls: two rows of four knobs with the direction, source and position between them.
    const int x0 = 570, knobW = 100, knobH = 108;
    auto slot = [&](Knob& k, int col, int row) { k.setBounds(x0 + col * knobW, 14 + row * 178, knobW, knobH); };
    slot(period, 0, 0);
    slot(division, 0, 0);
    slot(depth, 1, 0);
    slot(random, 2, 0);
    slot(smooth, 3, 0);
    slot(sensitivity, 0, 1);
    slot(bar, 0, 1);
    slot(twinDetune, 1, 1);
    slot(twinLevel, 2, 1);
    slot(mix, 3, 1);

    const int midY = 146;
    direction.setBounds(x0 + 4, midY, 150, 26);
    triggerSource.setBounds(x0 + 166, midY, 150, 26);
    manualSource.setBounds(x0 + 166, midY, 150, 26);
    position.setBounds(x0 + 328, midY, getWidth() - 24 - (x0 + 328), 26);
}

} // namespace glide90::ui
