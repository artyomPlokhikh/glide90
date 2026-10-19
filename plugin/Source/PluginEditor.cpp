#include "PluginEditor.h"

namespace glide90
{

Glide90Editor::Glide90Editor(Glide90Processor& p)
    : AudioProcessorEditor(p), proc(p), header(p), strip(p), spx(p), glide(p), rear(p.state())
{
    setLookAndFeel(&lookAndFeel);
    addAndMakeVisible(content);
    for (auto* c : std::initializer_list<juce::Component*> { &header, &strip, &spx, &glide, &rear })
        content.addAndMakeVisible(c);
    content.setSize(kWidth, kHeight);

    spx.onTouch = [this](const juce::RangedAudioParameter& param) { strip.showParameter(param); };
    header.onZoom = [this](float s) { setZoom(s); };
    setZoom(proc.uiScale());
    startTimerHz(3);
}

Glide90Editor::~Glide90Editor()
{
    setLookAndFeel(nullptr);
}

void Glide90Editor::setZoom(float scale)
{
    proc.setUiScale(scale);
    content.setTransform(juce::AffineTransform::scale(proc.uiScale()));
    setSize(juce::roundToInt(kWidth * proc.uiScale()), juce::roundToInt(kHeight * proc.uiScale()));
}

void Glide90Editor::timerCallback()
{
    // Group edits into undo steps: everything between two pauses with the mouse released.
    if (!juce::ModifierKeys::currentModifiers.isAnyMouseButtonDown())
        proc.undoManager().beginNewTransaction();
}

void Glide90Editor::resized()
{
    auto r = juce::Rectangle<int>(0, 0, kWidth, kHeight);
    header.setBounds(r.removeFromTop(kHeaderHeight));
    strip.setBounds(r.removeFromTop(kStripHeight));
    spx.setBounds(r.removeFromTop(kSpxHeight));
    glide.setBounds(r.removeFromTop(kGlideHeight));
    rear.setBounds(r);
}

} // namespace glide90
