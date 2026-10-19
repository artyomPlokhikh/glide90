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
        const bool user = proc.currentPresetIsUser();
        for (int i = 0; i < factory.size(); ++i)
            menu.addItem(factory[i], true, !user && i == proc.getCurrentProgram(), [this, i] { proc.loadFactoryPreset(i); });
        menu.addSectionHeader("User");
        if (userFiles.isEmpty())
            menu.addItem("Empty", false, false, nullptr);
        for (const auto& f : userFiles)
            menu.addItem(f.getFileNameWithoutExtension(), true, user && f.getFileNameWithoutExtension() == proc.currentPresetName(),
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
    // One list: the factory presets, then the user presets. Where we are is the factory index or the
    // user file, not the name, which a user preset may share with a factory one.
    refreshUserFiles();
    const auto factory = proc.factoryPresetNames();
    juce::StringArray userNames;
    for (const auto& f : userFiles)
        userNames.add(f.getFileNameWithoutExtension());
    const int count = factory.size() + userNames.size();
    int current = proc.getCurrentProgram();
    if (proc.currentPresetIsUser())
    {
        const int i = userNames.indexOf(proc.currentPresetName());
        current = i >= 0 ? factory.size() + i : (delta > 0 ? factory.size() - 1 : factory.size()); // file gone: step from where it was
    }
    const int next = (current + delta + count) % count;
    if (next < factory.size())
        proc.loadFactoryPreset(next);
    else
        proc.loadUserPreset(userFiles[next - factory.size()]);
}

void HeaderBar::showDialog(std::unique_ptr<juce::AlertWindow> window, std::function<void(int)> onResult)
{
    dialog = std::move(window);
    if (auto* parent = getParentComponent())
    {
        parent->addAndMakeVisible(*dialog);
        dialog->setCentrePosition(parent->getLocalBounds().getCentre());
    }
    // The callback comes after the dialog is gone, possibly after this panel is gone too (the plugin
    // window closed with the dialog open): it must not touch either then.
    dialog->enterModalState(true, juce::ModalCallbackFunction::create(
                                      [safe = juce::Component::SafePointer<HeaderBar>(this), onResult](int result) {
                                          if (safe != nullptr)
                                              onResult(result);
                                      }));
}

void HeaderBar::askToSave()
{
    auto window = std::make_unique<juce::AlertWindow>("Save preset", "Name:", juce::MessageBoxIconType::NoIcon);
    window->addTextEditor("name", proc.currentPresetName());
    window->addButton("Save", 1, juce::KeyPress(juce::KeyPress::returnKey));
    window->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));
    showDialog(std::move(window), [this](int result) {
        const auto name = dialog != nullptr ? dialog->getTextEditorContents("name").trim() : juce::String();
        dialog.reset();
        if (result != 1 || name.isEmpty())
            return;
        // Saving over another preset than the one being edited asks first.
        const auto file = Glide90Processor::userPresetDirectory().getChildFile(juce::File::createLegalFileName(name)
                                                                                + Glide90Processor::presetExtension());
        const bool same = proc.currentPresetIsUser() && file.getFileNameWithoutExtension() == proc.currentPresetName();
        if (!file.existsAsFile() || same)
            return savePreset(name);
        auto confirm = std::make_unique<juce::AlertWindow>("Replace preset?",
                                                           "\"" + file.getFileNameWithoutExtension() + "\" already exists.",
                                                           juce::MessageBoxIconType::NoIcon);
        confirm->addButton("Replace", 1, juce::KeyPress(juce::KeyPress::returnKey));
        confirm->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));
        showDialog(std::move(confirm), [this, name](int answer) {
            dialog.reset();
            if (answer == 1)
                savePreset(name);
        });
    });
}

void HeaderBar::savePreset(const juce::String& name)
{
    if (proc.saveUserPreset(name))
        refreshUserFiles();
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
