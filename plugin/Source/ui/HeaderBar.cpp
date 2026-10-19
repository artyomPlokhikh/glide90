#include "HeaderBar.h"

#include "PanelLookAndFeel.h"
#include "PluginProcessor.h"

namespace glide90::ui
{

HeaderBar::HeaderBar(Glide90Processor& p) : proc(p)
{
    for (auto* c : std::initializer_list<juce::Component*> { &presets, &prev, &next, &save })
        addAndMakeVisible(c);
    presets.setTooltip("Presets: the whole plugin, reverb and glide");
    presets.onChange = [this] {
        if (const int id = presets.getSelectedId(); id > 0)
            load(id);
    };
    prev.onClick = [this] { stepPreset(-1); };
    next.onClick = [this] { stepPreset(1); };
    save.setTooltip(juce::String("Save as a user preset in ") + Glide90Processor::userPresetDirectory().getFullPathName());
    save.onClick = [this] { askToSave(); };
    rebuildList();
    startTimerHz(4);
}

HeaderBar::~HeaderBar()
{
    stopTimer();
}

void HeaderBar::rebuildList()
{
    userFiles = proc.userPresetFiles();
    presets.clear(juce::dontSendNotification);
    presets.addSectionHeading("FACTORY");
    const auto factory = proc.factoryPresetNames();
    for (int i = 0; i < factory.size(); ++i)
        presets.addItem(factory[i], i + 1);
    if (!userFiles.isEmpty())
    {
        presets.addSectionHeading("USER");
        for (int i = 0; i < userFiles.size(); ++i)
            presets.addItem(userFiles[i].getFileNameWithoutExtension(), kUserIdBase + i);
    }
    shownName.clear();
    timerCallback();
}

void HeaderBar::load(int itemId)
{
    if (itemId >= kUserIdBase)
    {
        if (juce::isPositiveAndBelow(itemId - kUserIdBase, userFiles.size()))
            proc.loadUserPreset(userFiles[itemId - kUserIdBase]);
    }
    else
        proc.loadFactoryPreset(itemId - 1);
    shownName.clear();
    timerCallback();
}

void HeaderBar::stepPreset(int delta)
{
    juce::Array<int> ids;
    for (int i = 0; i < presets.getNumItems(); ++i)
        ids.add(presets.getItemId(i));
    if (ids.isEmpty())
        return;
    const int current = ids.indexOf(presets.getSelectedId());
    const int index = current < 0 ? 0 : (current + delta + ids.size()) % ids.size();
    presets.setSelectedId(ids[index], juce::dontSendNotification);
    load(ids[index]);
}

void HeaderBar::askToSave()
{
    saveDialog = std::make_unique<juce::AlertWindow>("Save preset", "Name of the new user preset:",
                                                     juce::MessageBoxIconType::NoIcon, this);
    saveDialog->addTextEditor("name", proc.currentPresetName());
    saveDialog->addButton("Save", 1, juce::KeyPress(juce::KeyPress::returnKey));
    saveDialog->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));
    saveDialog->enterModalState(true, juce::ModalCallbackFunction::create([this](int result) {
        const auto name = saveDialog != nullptr ? saveDialog->getTextEditorContents("name") : juce::String();
        saveDialog.reset();
        if (result == 1 && proc.saveUserPreset(name))
            rebuildList();
    }));
}

void HeaderBar::timerCallback()
{
    // Follow preset changes made by the host.
    const auto name = proc.currentPresetName();
    if (name == shownName)
        return;
    shownName = name;
    int id = 0;
    for (int i = 0; i < presets.getNumItems(); ++i)
        if (presets.getItemText(i) == name)
            id = presets.getItemId(i);
    if (id > 0)
        presets.setSelectedId(id, juce::dontSendNotification);
    else
        presets.setText(name, juce::dontSendNotification);
}

void HeaderBar::paint(juce::Graphics& g)
{
    drawRackUnit(g, getLocalBounds());
    g.setFont(panelFont(20.0f, true));
    g.setColour(colours::legend);
    g.drawText("GLIDE", 42, 0, 80, getHeight(), juce::Justification::centredLeft);
    g.setColour(colours::vfd);
    g.drawText("90", 107, 0, 40, getHeight(), juce::Justification::centredLeft);
    g.setFont(panelFont(10.0f));
    g.setColour(colours::legendDim);
    g.drawText("PRESET", presets.getX() - 96, 0, 50, getHeight(), juce::Justification::centredRight);
}

void HeaderBar::resized()
{
    const int h = 30, y = (getHeight() - h) / 2;
    int x = getWidth() / 2 - 160;
    prev.setBounds(x, y, 30, h);
    presets.setBounds(x + 34, y, 300, h);
    next.setBounds(x + 338, y, 30, h);
    save.setBounds(x + 380, y, 64, h);
}

} // namespace glide90::ui
