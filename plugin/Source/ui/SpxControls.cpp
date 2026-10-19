#include "SpxControls.h"

#include "PanelLookAndFeel.h"
#include "Parameters.h"
#include "PluginProcessor.h"

namespace glide90::ui
{

SpxControls::SpxControls(Glide90Processor& p)
    : proc(p), type(p.state(), ids::erType, colours::spxAccent),
      roomSize(p.state(), ids::erRoomSize, "ROOM SIZE", colours::spxAccent),
      liveness(p.state(), ids::erLiveness, "LIVENESS", colours::spxAccent),
      delay(p.state(), ids::erDelay, "PRE-DELAY", colours::spxAccent),
      lpf(p.state(), ids::erLpf, "LPF", colours::spxAccent), mix(p.state(), ids::balance, "MIX", colours::spxAccent),
      output(p.state(), ids::outLevel, "OUTPUT", colours::spxAccent),
      input(p.state(), ids::input, "INPUT", colours::spxAccent), inPad(p.state(), ids::inputSwitch, "IN -20"),
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
    const auto r = getLocalBounds().toFloat();
    g.setGradientFill(juce::ColourGradient(colours::spxTop, 0.0f, 0.0f, colours::spxBottom, 0.0f, r.getBottom(), false));
    g.fillRect(r);
    g.setColour(juce::Colours::black);
    g.drawHorizontalLine(0, 0.0f, r.getRight());

    g.setColour(colours::spxAccent);
    g.fillRect(24.0f, 14.0f, 4.0f, 20.0f);
    g.setColour(colours::legend);
    g.setFont(panelFont(18.0f, true));
    g.drawText("REVERB", 36, 12, 120, 24, juce::Justification::centredLeft);

    g.setFont(panelFont(11.5f, true));
    g.drawText("PROGRAM", program.getBounds().translated(0, -16).withHeight(14), juce::Justification::centredLeft);
    g.drawText("TYPE", type.getBounds().translated(0, -16).withHeight(14), juce::Justification::centredLeft);

    // Separators between the reflection, level and rig groups.
    g.setColour(juce::Colours::black.withAlpha(0.35f));
    for (auto* c : std::initializer_list<juce::Component*> { &mix, &inPad })
        g.fillRect((float) c->getX() - 9.0f, 18.0f, 2.0f, r.getHeight() - 36.0f);
}

void SpxControls::resized()
{
    program.setBounds(24, 60, 232, 28);
    type.setBounds(24, 112, 232, 28);

    const int knobW = 84, knobH = 116, y = 22;
    int x = 274;
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
    inPad.setBounds(x, 48, getWidth() - 24 - x, 30);
    outPad.setBounds(x, 92, getWidth() - 24 - x, 30);
}

} // namespace glide90::ui
