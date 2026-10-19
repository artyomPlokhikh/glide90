#include "PluginEditor.h"

namespace glide90
{

Glide90Editor::Glide90Editor(Glide90Processor& p) : AudioProcessorEditor(p), header(p), strip(p), spx(p), glide(p)
{
    setLookAndFeel(&lookAndFeel);
    for (auto* c : std::initializer_list<juce::Component*> { &header, &strip, &spx, &glide })
        addAndMakeVisible(c);
    setSize(kWidth, kHeaderHeight + kStripHeight + kSpxHeight + kGlideHeight);
}

Glide90Editor::~Glide90Editor()
{
    setLookAndFeel(nullptr);
}

void Glide90Editor::resized()
{
    auto r = getLocalBounds();
    header.setBounds(r.removeFromTop(kHeaderHeight));
    strip.setBounds(r.removeFromTop(kStripHeight));
    spx.setBounds(r.removeFromTop(kSpxHeight));
    glide.setBounds(r);
}

} // namespace glide90
