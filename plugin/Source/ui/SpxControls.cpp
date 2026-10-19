#include "SpxControls.h"

#include "PanelLookAndFeel.h"
#include "Parameters.h"
#include "PluginProcessor.h"

namespace glide90::ui
{

SpxControls::SpxControls(Glide90Processor& p)
    : proc(p), type(p.state(), ids::erType),
      roomSize(p.state(), ids::erRoomSize, "ROOM SIZE"),
      liveness(p.state(), ids::erLiveness, "LIVENESS"),
      delay(p.state(), ids::erDelay, "PRE-DELAY"),
      lpf(p.state(), ids::erLpf, "LPF"), mix(p.state(), ids::balance, "MIX"),
      output(p.state(), ids::outLevel, "OUTPUT"),
      input(p.state(), ids::input, "INPUT"), inPad(p.state(), ids::inputSwitch, "IN -20"),
      outPad(p.state(), ids::outputSwitch, "OUT -20")
{
    for (auto* c : std::initializer_list<juce::Component*> { &program, &type, &roomSize, &liveness, &delay, &lpf, &mix,
                                                             &output, &input, &inPad, &outPad })
        addAndMakeVisible(c);

    program.setTooltip("SPX90 memory: 1-30 factory programs, 31-90 user memories");
    program.onChange = [this] {
        if (const int id = program.getSelectedId(); id > 0 && id != proc.loadedMemory())
            proc.recallMemory(id);
    };
    roomSize.setTooltip("Spacing of the reflections. 20 = the huge Loveless swell");
    liveness.setTooltip("How the reflections fade (REVERSE: how steeply they swell)");
    delay.setTooltip("Time before the first reflection");
    lpf.setTooltip("Low-pass on the effect, 1-11 kHz or THRU");
    mix.setTooltip("BALANCE: 0 = dry only, 100 = effect only (the unit's mix is digital, so even the dry signal is 12 kHz band-limited)");
    output.setTooltip("OUT LVL, digital");
    input.setTooltip("Input level before the A/D: unity at 8, +12 dB at 10. Watch the meter, keep it out of the red");
    inPad.setTooltip("Rear input switch -20 dBm: adds 24 dB before the A/D (for weak sources)");
    outPad.setTooltip("Rear output switch -20 dBm: 24 dB quieter, analog (for an instrument amp input)");

    rebuildProgramList();
    startTimerHz(10);
}

SpxControls::~SpxControls()
{
    stopTimer();
}

void SpxControls::rebuildProgramList()
{
    listedRevision = proc.memoriesRevision();
    program.clear(juce::dontSendNotification);
    program.addSectionHeading("FACTORY");
    for (int n = 1; n <= MemoryBank::kCount; ++n)
    {
        if (n == MemoryBank::kFirstUser)
            program.addSectionHeading("USER");
        const auto m = proc.memories().get(n);
        const auto label = juce::String(n).paddedLeft('0', 2) + "  " + (m.used ? m.title : juce::String("(empty)"));
        program.addItem(label, n);
        const bool emulated = !parameterPages(m.algorithm).empty();
        if (!m.used || !emulated)
            program.setItemEnabled(n, m.used && n >= MemoryBank::kFirstUser);
    }
    program.setSelectedId(proc.loadedMemory(), juce::dontSendNotification);
}

void SpxControls::timerCallback()
{
    if (proc.memoriesRevision() != listedRevision)
        rebuildProgramList();
    if (program.getSelectedId() != proc.loadedMemory())
        program.setSelectedId(proc.loadedMemory(), juce::dontSendNotification);

    // The early-reflection knobs only mean something for the programs that are emulated.
    if (proc.algorithm() != shownAlgorithm)
    {
        shownAlgorithm = proc.algorithm();
        const bool er = !parameterPages(shownAlgorithm).empty();
        for (auto* c : std::initializer_list<juce::Component*> { &type, &roomSize, &liveness, &delay, &lpf })
            c->setEnabled(er);
    }
}

void SpxControls::paint(juce::Graphics& g)
{
    drawRackUnit(g, getLocalBounds());
    g.setColour(colours::legend);
    g.setFont(panelFont(15.0f, true));
    g.drawText("REVERB", 42, 12, 120, 20, juce::Justification::centredLeft);
    g.setFont(panelFont(10.0f));
    g.setColour(colours::legendDim);
    g.drawText("EARLY REFLECTIONS / REVERSE GATE", 112, 13, 260, 18, juce::Justification::centredLeft);

    g.setColour(colours::legend);
    g.setFont(panelFont(11.0f));
    g.drawText("PROGRAM", program.getBounds().translated(0, -15).withHeight(13), juce::Justification::centredLeft);
    g.drawText("TYPE", type.getBounds().translated(0, -15).withHeight(13), juce::Justification::centredLeft);

    // Engraved lines between the reflection, level and rig groups.
    for (auto* c : std::initializer_list<juce::Component*> { &mix, &inPad })
    {
        g.setColour(juce::Colours::black.withAlpha(0.7f));
        g.drawVerticalLine(c->getX() - 9, 22.0f, (float) getHeight() - 22.0f);
        g.setColour(juce::Colours::white.withAlpha(0.06f));
        g.drawVerticalLine(c->getX() - 8, 22.0f, (float) getHeight() - 22.0f);
    }
}

void SpxControls::resized()
{
    program.setBounds(42, 56, 226, 28);
    type.setBounds(42, 104, 226, 34);

    const int knobW = 82, knobH = 118, y = 18;
    int x = 280;
    for (auto* k : { &roomSize, &liveness, &delay, &lpf })
    {
        k->setBounds(x, y, knobW, knobH);
        x += knobW;
    }
    x += 18;
    for (auto* k : { &mix, &output, &input })
    {
        k->setBounds(x, y, knobW, knobH);
        x += knobW;
    }
    x += 18;
    const int padW = getWidth() - PanelLookAndFeel::kEar - 6 - x;
    inPad.setBounds(x, 34, padW, 44);
    outPad.setBounds(x, 86, padW, 44);
}

} // namespace glide90::ui
