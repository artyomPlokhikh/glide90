#include "RearPanel.h"

#include "PanelLookAndFeel.h"
#include "Parameters.h"

namespace glide90::ui
{

RearPanel::RearPanel(juce::AudioProcessorValueTreeState& state)
    : inLevel(state, ids::inputSwitch, "INPUT LEVEL", { "+4 dBm", "-20 dBm" }),
      outLevel(state, ids::outputSwitch, "OUTPUT LEVEL", { "+4 dBm", "-20 dBm" }),
      outputMode(state, ids::outputMode, "OUTPUTS", { "STEREO", "L ONLY", "MONO" })
{
    for (auto* c : { &inLevel, &outLevel, &outputMode })
        addAndMakeVisible(c);
    inLevel.setTooltip("-20 dBm adds 24 dB before the A/D, for a weak source");
    outLevel.setTooltip("-20 dBm is 24 dB quieter (analog, after the D/A), for an instrument amp input");
    outputMode.setTooltip("STEREO: both outputs of the unit. L ONLY: the left output on both channels, for a single "
                          "amp. MONO: the two summed (their different reflections colour each other)");
}

void RearPanel::paint(juce::Graphics& g)
{
    drawRackUnit(g, getLocalBounds());
}

void RearPanel::resized()
{
    // Spread across the width with equal gaps, flush with the margins.
    const int m = PanelLookAndFeel::kMargin;
    const int total = inLevel.idealWidth() + outLevel.idealWidth() + outputMode.idealWidth();
    const int gap = (getWidth() - 2 * m - total) / 2;
    int x = m;
    for (auto* s : { &inLevel, &outLevel, &outputMode })
    {
        s->setBounds(x, (getHeight() - 32) / 2, s->idealWidth(), 32);
        x += s->idealWidth() + gap;
    }
}

} // namespace glide90::ui
