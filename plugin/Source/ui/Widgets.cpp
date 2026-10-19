#include "Widgets.h"

#include "PanelLookAndFeel.h"

namespace glide90::ui
{

Knob::Knob(juce::AudioProcessorValueTreeState& state, const char* paramId, const juce::String& n, juce::Colour accent,
           bool bipolar)
    : name(n), attachment(state, paramId, slider)
{
    slider.getProperties().set(PanelLookAndFeel::kAccent, (juce::int64) accent.getARGB());
    slider.getProperties().set(PanelLookAndFeel::kBipolar, bipolar);
    slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 90, 18);
    slider.setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    auto* p = state.getParameter(paramId);
    slider.setDoubleClickReturnValue(true, p->convertFrom0to1(p->getDefaultValue()));
    slider.setMouseDragSensitivity(220);
    addAndMakeVisible(slider);
}

void Knob::paint(juce::Graphics& g)
{
    g.setFont(panelFont(11.5f, true));
    g.setColour(isEnabled() ? colours::legend : colours::legendDim);
    g.drawText(name, getLocalBounds().removeFromTop(16), juce::Justification::centred);
}

void Knob::resized()
{
    slider.setBounds(getLocalBounds().withTrimmedTop(16));
}

ChoiceButtons::ChoiceButtons(juce::AudioProcessorValueTreeState& state, const char* paramId, juce::Colour a,
                             bool isVertical)
    : accent(a), vertical(isVertical),
      attachment(*state.getParameter(paramId), [this](float v) {
          selected = juce::roundToInt(v);
          repaint();
      })
{
    if (auto* choice = dynamic_cast<juce::AudioParameterChoice*>(state.getParameter(paramId)))
        labels = choice->choices;
    attachment.sendInitialUpdate();
}

juce::Rectangle<float> ChoiceButtons::segment(int index) const
{
    const auto r = getLocalBounds().toFloat().reduced(1.0f);
    const int n = juce::jmax(1, labels.size());
    if (vertical)
    {
        const float h = r.getHeight() / (float) n;
        return { r.getX(), r.getY() + h * (float) index, r.getWidth(), h };
    }
    const float w = r.getWidth() / (float) n;
    return { r.getX() + w * (float) index, r.getY(), w, r.getHeight() };
}

void ChoiceButtons::paint(juce::Graphics& g)
{
    const auto r = getLocalBounds().toFloat().reduced(1.0f);
    const float alpha = isEnabled() ? 1.0f : 0.4f;
    g.setColour(juce::Colour(0xff0b0c0d).withAlpha(alpha));
    g.fillRoundedRectangle(r, 4.0f);
    g.setFont(panelFont(juce::jmin(12.0f, (vertical ? r.getHeight() / (float) juce::jmax(1, labels.size()) : r.getHeight()) * 0.5f), true));
    for (int i = 0; i < labels.size(); ++i)
    {
        const auto s = segment(i).reduced(2.0f);
        if (i == selected)
        {
            g.setColour(accent.withAlpha(alpha));
            g.fillRoundedRectangle(s, 3.0f);
            g.setColour(juce::Colour(0xff1a1612));
        }
        else
            g.setColour(colours::legend.withAlpha(0.75f * alpha));
        g.drawFittedText(labels[i], s.toNearestInt().reduced(2, 0), juce::Justification::centred, 1);
    }
    g.setColour(juce::Colours::black);
    g.drawRoundedRectangle(r, 4.0f, 1.0f);
}

void ChoiceButtons::mouseDown(const juce::MouseEvent& e)
{
    for (int i = 0; i < labels.size(); ++i)
        if (segment(i).contains(e.position))
            attachment.setValueAsCompleteGesture((float) i);
}

ToggleKey::ToggleKey(juce::AudioProcessorValueTreeState& state, const char* paramId, const juce::String& label)
    : juce::TextButton(label),
      attachment(*state.getParameter(paramId), [this](float v) {
          setToggleState(v > 0.5f, juce::dontSendNotification);
          repaint();
      })
{
    getProperties().set("led", true);
    onClick = [this] { attachment.setValueAsCompleteGesture(getToggleState() ? 0.0f : 1.0f); };
    attachment.sendInitialUpdate();
}

} // namespace glide90::ui
