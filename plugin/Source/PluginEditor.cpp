#include "PluginEditor.h"

namespace glide90
{

Glide90Editor::Glide90Editor(Glide90Processor& p) : AudioProcessorEditor(p), spx(p), sway(p)
{
    setLookAndFeel(&lookAndFeel);
    addAndMakeVisible(spx);
    addAndMakeVisible(sway);
    setSize(kWidth, kSpxHeight + kSwayHeight);
}

Glide90Editor::~Glide90Editor()
{
    setLookAndFeel(nullptr);
}

void Glide90Editor::resized()
{
    auto r = getLocalBounds();
    spx.setBounds(r.removeFromTop(kSpxHeight));
    sway.setBounds(r);
}

} // namespace glide90
