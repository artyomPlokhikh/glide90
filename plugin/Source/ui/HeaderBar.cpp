#include "HeaderBar.h"

#include "PanelLookAndFeel.h"
#include "PluginProcessor.h"

namespace glide90::ui
{

HeaderBar::HeaderBar(Glide90Processor& p) : proc(p)
{
    for (auto* c : std::initializer_list<juce::Component*> { &presets, &save, &a, &b, &undoKey, &redoKey, &zoom })
        addAndMakeVisible(c);

    presets.setTooltip("Preset: the whole plugin, reverb and glide. Arrows step through the list; * means changed");
    presets.getText = [this] { return proc.currentPresetName() + (proc.isPresetModified() ? " *" : ""); };
    presets.onStep = [this](int d) { stepPreset(d); };
    presets.fillMenu = [this](juce::PopupMenu& menu) {
        refreshUserFiles();
        menu.addSectionHeader("Factory");
        const auto factory = proc.factoryPresetNames();
        for (int i = 0; i < factory.size(); ++i)
            menu.addItem(factory[i], true, factory[i] == proc.currentPresetName(), [this, i] { proc.loadFactoryPreset(i); });
        menu.addSectionHeader("User");
        if (userFiles.isEmpty())
            menu.addItem("Empty", false, false, nullptr);
        for (const auto& f : userFiles)
            menu.addItem(f.getFileNameWithoutExtension(), true, f.getFileNameWithoutExtension() == proc.currentPresetName(),
                         [this, f] { proc.loadUserPreset(f); });
    };

    save.setTooltip(juce::String("Save as a user preset in ") + Glide90Processor::userPresetDirectory().getFullPathName());
    save.onClick = [this] { askToSave(); };

    for (auto* key : { &a, &b })
        key->getProperties().set(PanelLookAndFeel::kLed, true);
    a.setTooltip("Compare: two working versions of the setting");
    b.setTooltip("Compare: two working versions of the setting (B starts as a copy of A)");
    a.onClick = [this] { proc.selectAB(0); };
    b.onClick = [this] { proc.selectAB(1); };

    undoKey.onClick = [this] { proc.undoManager().undo(); };
    redoKey.onClick = [this] { proc.undoManager().redo(); };

    zoom.setTooltip("Window size");
    zoom.getText = [this] { return juce::String(juce::roundToInt(proc.uiScale() * 100.0f)) + "%"; };
    zoom.fillMenu = [this](juce::PopupMenu& menu) {
        for (float s : { 0.75f, 1.0f, 1.25f, 1.5f, 1.75f })
            menu.addItem(juce::String(juce::roundToInt(s * 100.0f)) + "%", true, std::abs(s - proc.uiScale()) < 0.01f, [this, s] {
                if (onZoom)
                    onZoom(s);
            });
    };

    refreshUserFiles();
    timerCallback();
    startTimerHz(6);
}

HeaderBar::~HeaderBar()
{
    stopTimer();
}

void HeaderBar::refreshUserFiles()
{
    userFiles = proc.userPresetFiles();
}

void HeaderBar::stepPreset(int delta)
{
    // One list: the factory presets, then the user presets.
    refreshUserFiles();
    const auto factory = proc.factoryPresetNames();
    juce::StringArray names(factory);
    for (const auto& f : userFiles)
        names.add(f.getFileNameWithoutExtension());
    const int current = names.indexOf(proc.currentPresetName());
    const int next = current < 0 ? 0 : (current + delta + names.size()) % names.size();
    if (next < factory.size())
        proc.loadFactoryPreset(next);
    else
        proc.loadUserPreset(userFiles[next - factory.size()]);
}

void HeaderBar::askToSave()
{
    saveDialog = std::make_unique<juce::AlertWindow>("Save preset", "Name:", juce::MessageBoxIconType::NoIcon, this);
    saveDialog->addTextEditor("name", proc.currentPresetName());
    saveDialog->addButton("Save", 1, juce::KeyPress(juce::KeyPress::returnKey));
    saveDialog->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));
    saveDialog->enterModalState(true, juce::ModalCallbackFunction::create([this](int result) {
        const auto name = saveDialog != nullptr ? saveDialog->getTextEditorContents("name") : juce::String();
        saveDialog.reset();
        if (result == 1 && proc.saveUserPreset(name))
            refreshUserFiles();
    }));
}

void HeaderBar::timerCallback()
{
    a.setToggleState(proc.abSlot() == 0, juce::dontSendNotification);
    b.setToggleState(proc.abSlot() == 1, juce::dontSendNotification);
    undoKey.setEnabled(proc.undoManager().canUndo());
    redoKey.setEnabled(proc.undoManager().canRedo());
}

void HeaderBar::paint(juce::Graphics& g)
{
    drawRackUnit(g, getLocalBounds());
    g.setFont(legendFont(24.0f, true));
    g.setColour(colours::silkscreen);
    g.drawText("GLIDE90", PanelLookAndFeel::kMargin, 0, 120, getHeight(), juce::Justification::centredLeft);
}

void HeaderBar::resized()
{
    const int h = 28, y = (getHeight() - h) / 2;
    presets.setBounds(240, y, 340, h);
    save.setBounds(586, y, 58, h);
    a.setBounds(668, y, 36, h);
    b.setBounds(708, y, 36, h);
    undoKey.setBounds(766, y, 54, h);
    redoKey.setBounds(824, y, 54, h);
    // Groups: preset + SAVE, A/B, UNDO/REDO, zoom; 4 px inside a group, 22 px between groups.
    zoom.setBounds(getWidth() - PanelLookAndFeel::kMargin - 74, y, 74, h);
}

} // namespace glide90::ui
