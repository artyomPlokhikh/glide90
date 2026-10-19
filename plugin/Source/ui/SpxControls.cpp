#include "SpxControls.h"

#include "PanelLookAndFeel.h"
#include "Parameters.h"
#include "PluginProcessor.h"

namespace glide90::ui
{

SpxControls::SpxControls(Glide90Processor& p)
    : proc(p), type(p.state(), ids::erType, "TYPE"),
      roomSize(p.state(), ids::erRoomSize, "ROOM SIZE", Knob::Size::Large),
      liveness(p.state(), ids::erLiveness, "LIVENESS"), delay(p.state(), ids::erDelay, "PRE-DELAY"),
      lpf(p.state(), ids::erLpf, "LPF"), mix(p.state(), ids::balance, "MIX", Knob::Size::Large),
      output(p.state(), ids::outLevel, "OUTPUT")
{
    for (auto* c : std::initializer_list<juce::Component*> { &program, &type, &roomSize, &liveness, &delay, &lpf, &mix,
                                                             &output })
        addAndMakeVisible(c);

    program.setTooltip("SPX90 memory: 1-30 factory programs, 31-90 user memories. Arrows step through the ones that play");
    program.getText = [this] {
        const auto m = proc.memories().get(proc.loadedMemory());
        return juce::String(proc.loadedMemory()).paddedLeft('0', 2) + " " + (m.used ? m.title : juce::String("EMPTY"));
    };
    program.onStep = [this](int d) { stepProgram(d); };
    program.fillMenu = [this](juce::PopupMenu& menu) {
        menu.addSectionHeader("Factory");
        for (int n = 1; n <= MemoryBank::kCount; ++n)
        {
            if (n == MemoryBank::kFirstUser)
                menu.addSectionHeader("User");
            const auto m = proc.memories().get(n);
            if (n >= MemoryBank::kFirstUser && !m.used)
                continue;
            const bool plays = m.used && !parameterPages(m.algorithm).empty();
            const auto label = juce::String(n).paddedLeft('0', 2) + "   " + m.title + (plays ? "" : "   (not emulated yet)");
            menu.addItem(label, plays, n == proc.loadedMemory(), [this, n] { proc.recallMemory(n); });
        }
    };

    type.setTooltip("Pattern of the reflections. REVERSE swells up: the reverse gate");
    roomSize.setTooltip("Spacing of the reflections. At 20 the reverse gate swells for over a second");
    liveness.setTooltip("How the reflections fade (REVERSE: how steeply they swell)");
    delay.setTooltip("Time before the first reflection");
    lpf.setTooltip("Low-pass on the effect, 1-11 kHz or THRU");
    mix.setTooltip("BALANCE: 0 = dry only, 100 = effect only. The unit mixes digitally, so even the dry signal "
                   "is band-limited to 12 kHz");
    output.setTooltip("OUT LVL, digital");

    for (auto* k : { &roomSize, &liveness, &delay, &lpf, &mix, &output })
        k->onTouch = [this](const juce::RangedAudioParameter& param) {
            if (onTouch)
                onTouch(param);
        };

    startTimerHz(10);
}

SpxControls::~SpxControls()
{
    stopTimer();
}

void SpxControls::stepProgram(int delta)
{
    // Only the memories that play: the emulated factory programs and the stored user memories.
    for (int n = proc.loadedMemory(), tries = 0; tries < MemoryBank::kCount; ++tries)
    {
        n = (n - 1 + delta + MemoryBank::kCount) % MemoryBank::kCount + 1;
        const auto m = proc.memories().get(n);
        if (m.used && !parameterPages(m.algorithm).empty())
        {
            proc.recallMemory(n);
            return;
        }
    }
}

void SpxControls::timerCallback()
{
    // The early-reflection controls only mean something for the programs that are emulated.
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
    drawUnitName(g, "REVERB", getLocalBounds());
    g.setFont(legendFont(13.0f));
    g.setColour(colours::silkscreen);
    g.drawText("PROGRAM", program.getBounds().withY(kRowTop).withHeight(15), juce::Justification::centredLeft);
    drawEngravedLine(g, (float) type.getRight() + 6.0f, (float) kRowTop, (float) kRowTop + Knob::kHeight);
    drawEngravedLine(g, (float) mix.getX() - 8.0f, (float) kRowTop, (float) kRowTop + Knob::kHeight);
}

void SpxControls::resized()
{
    const int m = PanelLookAndFeel::kMargin;
    program.setBounds(m, kRowTop + 26, 212, 28);
    type.setBounds(m + 220, kRowTop, 160, Knob::kHeight);

    // Six knobs over the rest of the width, with a double gap before MIX.
    const int left = type.getRight() + 20, right = getWidth() - m;
    const int lw = Knob::widthFor(Knob::Size::Large), sw = Knob::widthFor(Knob::Size::Small);
    const int gap = (right - left - 2 * lw - 4 * sw) / 6;
    int x = left;
    auto place = [&](Knob& k, int w, int after) {
        k.setBounds(x, kRowTop, w, Knob::kHeight);
        x += w + after;
    };
    place(roomSize, lw, gap);
    place(liveness, sw, gap);
    place(delay, sw, gap);
    place(lpf, sw, 2 * gap);
    place(mix, lw, gap);
    place(output, sw, 0);
}

} // namespace glide90::ui
