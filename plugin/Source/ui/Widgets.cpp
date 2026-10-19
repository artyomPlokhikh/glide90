#include "Widgets.h"

#include "PanelLookAndFeel.h"

namespace glide90::ui
{

Knob::Knob(juce::AudioProcessorValueTreeState& state, const char* paramId, const juce::String& n)
    : name(n), attachment(state, paramId, slider)
{
    slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 74, 20);
    // Set here rather than by the look-and-feel: the value box takes its colours when it is created,
    // before the editor's look-and-feel reaches this knob.
    slider.setColour(juce::Slider::textBoxTextColourId, colours::vfd);
    slider.setColour(juce::Slider::textBoxBackgroundColourId, colours::glass);
    slider.setColour(juce::Slider::textBoxOutlineColourId, colours::glassEdge);
    slider.setColour(juce::Slider::textBoxHighlightColourId, colours::vfd.withAlpha(0.3f));
    auto* p = state.getParameter(paramId);
    slider.setDoubleClickReturnValue(true, p->convertFrom0to1(p->getDefaultValue()));
    slider.setMouseDragSensitivity(220);
    addAndMakeVisible(slider);
}

void Knob::paint(juce::Graphics& g)
{
    g.setFont(panelFont(11.0f));
    g.setColour(isEnabled() ? colours::legend : colours::legendDim);
    g.drawText(name, getLocalBounds().removeFromTop(15), juce::Justification::centred);
}

void Knob::resized()
{
    slider.setBounds(getLocalBounds().withTrimmedTop(15));
}

ChoiceButtons::ChoiceButtons(juce::AudioProcessorValueTreeState& state, const char* paramId)
    : attachment(*state.getParameter(paramId), [this](float v) {
          const int selected = juce::roundToInt(v);
          for (int i = 0; i < keys.size(); ++i)
          {
              keys[i]->setToggleState(i == selected, juce::dontSendNotification);
              keys[i]->repaint();
          }
      })
{
    if (auto* choice = dynamic_cast<juce::AudioParameterChoice*>(state.getParameter(paramId)))
        for (int i = 0; i < choice->choices.size(); ++i)
        {
            auto* key = keys.add(new juce::TextButton(choice->choices[i]));
            key->getProperties().set(PanelLookAndFeel::kLed, true);
            key->onClick = [this, i] { attachment.setValueAsCompleteGesture((float) i); };
            addAndMakeVisible(key);
        }
    attachment.sendInitialUpdate();
}

void ChoiceButtons::resized()
{
    const int n = juce::jmax(1, keys.size()), gap = 4;
    const int w = (getWidth() - gap * (n - 1)) / n;
    for (int i = 0; i < keys.size(); ++i)
        keys[i]->setBounds(i * (w + gap), 0, i == n - 1 ? getWidth() - i * (w + gap) : w, getHeight());
}

ToggleKey::ToggleKey(juce::AudioProcessorValueTreeState& state, const char* paramId, const juce::String& label)
    : juce::TextButton(label),
      attachment(*state.getParameter(paramId), [this](float v) {
          setToggleState(v > 0.5f, juce::dontSendNotification);
          repaint();
      })
{
    getProperties().set(PanelLookAndFeel::kLed, true);
    onClick = [this] { attachment.setValueAsCompleteGesture(getToggleState() ? 0.0f : 1.0f); };
    attachment.sendInitialUpdate();
}

} // namespace glide90::ui
