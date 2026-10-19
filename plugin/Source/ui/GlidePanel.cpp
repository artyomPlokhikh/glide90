#include "GlidePanel.h"

#include "PanelLookAndFeel.h"
#include "Parameters.h"
#include "PluginProcessor.h"

namespace glide90::ui
{

GlidePanel::GlidePanel(Glide90Processor& p)
    : proc(p), on(p.state(), ids::glideOn, "GLIDE"), mode(p.state(), ids::glideMode),
      direction(p.state(), ids::glideDirection),
      position(p.state(), ids::glidePosition), display(p),
      period(p.state(), ids::glidePeriod, "RATE"),
      division(p.state(), ids::glideDivision, "RATE"),
      depth(p.state(), ids::glideDepth, "DEPTH"),
      random(p.state(), ids::glideRandom, "RANDOM"),
      smooth(p.state(), ids::glideSmooth, "SMOOTH"),
      sensitivity(p.state(), ids::glideSensitivity, "SENSITIVITY"),
      bar(p.state(), ids::glideBar, "BAR"),
      twinDetune(p.state(), ids::glideTwinDetune, "TWIN DETUNE"),
      twinLevel(p.state(), ids::glideTwinLevel, "TWIN LEVEL"),
      mix(p.state(), ids::glideMix, "MIX")
{
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
    drawRackUnit(g, getLocalBounds());
    g.setColour(colours::legend);
    g.setFont(panelFont(15.0f, true));
    g.drawText("GLIDE", 42, 12, 80, 20, juce::Justification::centredLeft);
    g.setFont(panelFont(10.0f));
    g.setColour(colours::legendDim);
    g.drawText("VIBRATO BAR", 98, 13, 120, 18, juce::Justification::centredLeft);

    g.setFont(panelFont(11.0f));
    g.setColour(colours::legend);
    g.drawText("DIRECTION", direction.getBounds().translated(0, -15).withHeight(13), juce::Justification::centredLeft);
    g.drawText("POSITION", position.getBounds().translated(0, -15).withHeight(13), juce::Justification::centredLeft);
    const auto current = (sway::GlideMode) shownMode;
    if (current == sway::GlideMode::Trigger || current == sway::GlideMode::Manual)
        g.drawText(current == sway::GlideMode::Trigger ? "TRIGGER" : "SOURCE",
                   triggerSource.getBounds().translated(0, -15).withHeight(13), juce::Justification::centredLeft);
}

void GlidePanel::resized()
{
    on.setBounds(42, 36, 74, 40);
    mode.setBounds(130, 36, 420, 40);
    display.setBounds(42, 88, 508, 222);

    // Controls: two rows of four knobs with the direction, source and position between them.
    const int x0 = 566, knobW = 96, knobH = 118;
    auto slot = [&](Knob& k, int col, int row) { k.setBounds(x0 + col * knobW, 14 + row * 180, knobW, knobH); };
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

    const int midY = 152;
    direction.setBounds(x0 + 4, midY, 150, 32);
    triggerSource.setBounds(x0 + 164, midY + 2, 140, 28);
    manualSource.setBounds(x0 + 164, midY + 2, 140, 28);
    position.setBounds(x0 + 314, midY, getWidth() - PanelLookAndFeel::kEar - 4 - (x0 + 314), 32);
}

} // namespace glide90::ui
