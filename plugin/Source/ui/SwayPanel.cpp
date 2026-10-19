#include "SwayPanel.h"

#include "PanelLookAndFeel.h"
#include "Parameters.h"
#include "PluginProcessor.h"

namespace glide90::ui
{

void BarIndicator::setCents(float c)
{
    if (std::abs(c - cents) > 0.2f)
    {
        cents = c;
        repaint();
    }
}

void BarIndicator::paint(juce::Graphics& g)
{
    // Scale from +50 cents (top) to -200 cents (bottom).
    constexpr float top = 50.0f, bottom = -200.0f;
    auto r = getLocalBounds().toFloat().reduced(2.0f);
    auto track = r.withTrimmedLeft(30.0f).withWidth(14.0f);
    g.setColour(juce::Colour(0xff0e1013));
    g.fillRoundedRectangle(track, 4.0f);
    g.setColour(juce::Colours::black);
    g.drawRoundedRectangle(track, 4.0f, 1.0f);

    auto yOf = [&](float c) { return track.getY() + (top - c) / (top - bottom) * track.getHeight(); };
    g.setFont(panelFont(9.5f));
    for (float c : { 0.0f, -50.0f, -100.0f, -150.0f, -200.0f })
    {
        g.setColour(colours::legendDim);
        g.drawText(juce::String((int) c), juce::Rectangle<float>(r.getX(), yOf(c) - 6.0f, 26.0f, 12.0f),
                   juce::Justification::centredRight);
        g.drawHorizontalLine((int) yOf(c), track.getX() - 3.0f, track.getX());
    }

    const float y0 = yOf(0.0f), y = yOf(juce::jlimit(bottom, top, cents));
    g.setColour(colours::accent.withAlpha(0.8f));
    g.fillRect(juce::Rectangle<float>(track.getX() + 3.0f, juce::jmin(y0, y), track.getWidth() - 6.0f, std::abs(y - y0)));
    g.setColour(colours::accent.brighter(0.4f));
    g.fillRect(juce::Rectangle<float>(track.getX() - 2.0f, y - 1.5f, track.getWidth() + 4.0f, 3.0f));
}

SwayPanel::SwayPanel(Glide90Processor& p) : proc(p)
{
    struct Spec
    {
        const char* id;
        const char* legend;
        const char* tip;
        bool arc;
    };
    static constexpr Spec specs[] {
        { ids::swayDepth, "DEPTH", "How far the hand moves the bar, in cents", true },
        { ids::swayRate, "RATE", "Speed of the wobble", true },
        { ids::swayShape, "SHAPE", "Steady LFO (left) to irregular hand movement (right)", true },
        { ids::swayStrum, "STRUM", "How much of the movement follows your picking", true },
        { ids::swaySensitivity, "SENS", "Pick attack sensitivity for STRUM", true },
        { ids::swayAsymmetry, "ASYM", "Bar only lowers the pitch (left) to symmetric around it (right)", true },
        { ids::swayBar, "BAR", "Manual bar position (or pitch bend / mod wheel, see BAR SOURCE)", false },
        { ids::swayTwinDetune, "TWIN DETUNE", "Second string tuned slightly sharp", true },
        { ids::swayTwinLevel, "TWIN LEVEL", "Level of the second string", true },
        { ids::swayMix, "MIX", "Sway wet / dry", true },
    };
    for (const auto& s : specs)
    {
        auto k = std::make_unique<Knob>();
        k->legend = s.legend;
        k->slider.getProperties().set(PanelLookAndFeel::kArc, s.arc); // BAR's range ends at 0: no arc
        k->slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 70, 16);
        k->slider.setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        k->slider.setTooltip(s.tip);
        k->attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(p.state(), s.id, k->slider);
        addAndMakeVisible(k->slider);
        knobs.push_back(std::move(k));
    }

    addAndMakeVisible(onButton);
    onAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(p.state(), ids::swayOn, onButton);

    position.addItemList({ "PRE (into SPX)", "POST (after SPX)" }, 1);
    position.setTooltip("PRE: the glide goes into the reverse gate, as Kevin Shields played it");
    addAndMakeVisible(position);
    positionAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(p.state(),
                                                                                                  ids::swayPosition, position);
    barSource.addItemList({ "BAR: KNOB", "BAR: PITCH BEND", "BAR: MOD WHEEL" }, 1);
    barSource.setTooltip("What sets the bar position: the BAR knob, MIDI pitch bend (+-200 ct) or the mod wheel / "
                         "expression (0 to -200 ct)");
    addAndMakeVisible(barSource);
    barSourceAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(p.state(),
                                                                                                   ids::swayBarSource, barSource);
    addAndMakeVisible(bar);
    startTimerHz(30);
}

SwayPanel::~SwayPanel()
{
    stopTimer();
}

void SwayPanel::timerCallback()
{
    bar.setCents(proc.barCents());
}

void SwayPanel::paint(juce::Graphics& g)
{
    const auto r = getLocalBounds().toFloat();
    g.setGradientFill(juce::ColourGradient(colours::swayTop, 0.0f, 0.0f, colours::swayBottom, 0.0f, r.getBottom(), false));
    g.fillRect(r);
    g.setColour(juce::Colours::black);
    g.drawHorizontalLine(0, 0.0f, r.getRight());
    g.setColour(colours::accent);
    g.fillRect(24.0f, 14.0f, 4.0f, 22.0f);
    g.setColour(colours::legend);
    g.setFont(panelFont(20.0f, true));
    g.drawText("SWAY", 36, 12, 100, 26, juce::Justification::centredLeft);
    g.setColour(colours::legendDim);
    g.setFont(panelFont(11.0f));
    g.drawText("JAZZMASTER BAR GLIDE", 100, 18, 220, 16, juce::Justification::centredLeft);

    g.setFont(panelFont(10.5f, true));
    g.setColour(colours::legend);
    for (const auto& k : knobs)
        g.drawText(k->legend, k->slider.getBounds().withHeight(14).translated(0, -15), juce::Justification::centred);
    g.drawText("BAR", bar.getBounds().withHeight(14).translated(0, -15), juce::Justification::centred);
}

void SwayPanel::resized()
{
    onButton.setBounds(24, 50, 110, 22);
    position.setBounds(24, 84, 150, 24);
    barSource.setBounds(24, 118, 150, 24);

    const int x0 = 196, w = 70, gap = 0;
    for (size_t i = 0; i < knobs.size(); ++i)
        knobs[i]->slider.setBounds(x0 + (int) i * (w + gap), 66, w, 96);
    bar.setBounds(getWidth() - 24 - 60, 52, 56, getHeight() - 70);
}

} // namespace glide90::ui
