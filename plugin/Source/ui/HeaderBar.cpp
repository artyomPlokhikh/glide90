#include "HeaderBar.h"

#include "PanelLookAndFeel.h"
#include "Parameters.h"
#include "PluginProcessor.h"

namespace glide90::ui
{

HeaderBar::HeaderBar(Glide90Processor& p) : proc(p)
{
    for (auto* c : std::initializer_list<juce::Component*> { &presets, &save, &a, &b, &undoKey, &redoKey, &setup })
        addAndMakeVisible(c);

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
        const auto current = currentUserFile();
        menu.addSeparator();
        menu.addItem("Delete " + (current.exists() ? "\"" + current.getFileNameWithoutExtension() + "\"" : juce::String("preset")),
                     current.exists(), false, [this, current] { askToDelete(current); });
    };

    save.setTooltip(juce::String("Save as a user preset in ") + Glide90Processor::userPresetDirectory().getFullPathName());
    save.onClick = [this] { askToSave(); };

    a.getProperties().set(PanelLookAndFeel::kSegment, "left");
    b.getProperties().set(PanelLookAndFeel::kSegment, "right");
    for (auto* key : { &a, &b })
        key->setTooltip("Two versions of the setting to compare; B starts as a copy of A. Locked controls stay as they are");
    a.onClick = [this] { proc.selectAB(0); };
    b.onClick = [this] { proc.selectAB(1); };

    undoKey.getProperties().set(PanelLookAndFeel::kIcon, "undo");
    redoKey.getProperties().set(PanelLookAndFeel::kIcon, "redo");
    undoKey.setTooltip("Undo");
    redoKey.setTooltip("Redo");
    undoKey.onClick = [this] { proc.undoManager().undo(); };
    redoKey.onClick = [this] { proc.undoManager().redo(); };

    setup.onClick = [this] { showSetup(); };

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

void HeaderBar::showSetup()
{
    juce::PopupMenu menu;
    menu.setLookAndFeel(&getLookAndFeel());
    menu.addSectionHeader("Window");
    juce::PopupMenu sizes;
    for (float s : { 0.75f, 1.0f, 1.25f, 1.5f, 1.75f })
        sizes.addItem(juce::String(juce::roundToInt(s * 100.0f)) + "%", true, std::abs(s - proc.uiScale()) < 0.01f, [this, s] {
            if (onZoom)
                onZoom(s);
        });
    menu.addSubMenu("Size", sizes);
    juce::PopupMenu looks;
    for (int i = 0; i < kThemeCount; ++i)
        looks.addItem(themeAt(i).name, true, themeAt(i).id == theme().id, [this, i] {
            if (onTheme)
                onTheme(i);
        });
    menu.addSubMenu("Look", looks);
    juce::PopupMenu motionMenu;
    const std::pair<const char*, MotionLevel> levels[] { { "Full", MotionLevel::Full },
                                                         { "Screens only", MotionLevel::Subtle },
                                                         { "Off", MotionLevel::Off } };
    for (const auto& [name, level] : levels)
        motionMenu.addItem(name, true, motionLevel() == level, [level] { setMotionLevel(level); });
    menu.addSubMenu("Motion", motionMenu);
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&setup));
}

void HeaderBar::showDialog(std::unique_ptr<Dialog> window, std::function<void(int)> onResult)
{
    dialog = std::move(window);
    if (auto* parent = getParentComponent())
    {
        parent->addAndMakeVisible(*dialog);
        dialog->setBounds(parent->getLocalBounds());
    }
    // The callback comes after the dialog is gone, possibly after this panel is gone too (the plugin
    // window closed with the dialog open): it must not touch either then.
    dialog->enterModalState(true, juce::ModalCallbackFunction::create(
                                      [safe = juce::Component::SafePointer<HeaderBar>(this), onResult](int result) {
                                          if (safe != nullptr)
                                              onResult(result);
                                      }));
    dialog->focus();
}

void HeaderBar::askToSave()
{
    showDialog(std::make_unique<Dialog>("Save preset", "Name of the user preset:", "Save", true, proc.currentPresetName()),
               [this](int result) {
                   const auto name = dialog != nullptr ? dialog->getFieldText() : juce::String();
                   dialog.reset();
                   if (result != 1 || name.isEmpty())
                       return;
                   // Saving over another preset than the one being edited asks first.
                   const auto file = Glide90Processor::userPresetDirectory().getChildFile(
                       juce::File::createLegalFileName(name) + Glide90Processor::presetExtension());
                   const bool same = proc.currentPresetIsUser() && file.getFileNameWithoutExtension() == proc.currentPresetName();
                   if (!file.existsAsFile() || same)
                       return savePreset(name);
                   showDialog(std::make_unique<Dialog>("Replace preset?",
                                                       "\"" + file.getFileNameWithoutExtension() + "\" already exists.", "Replace"),
                              [this, name](int answer) {
                                  dialog.reset();
                                  if (answer == 1)
                                      savePreset(name);
                              });
               });
}

juce::File HeaderBar::currentUserFile() const
{
    if (proc.currentPresetIsUser())
        for (const auto& f : userFiles)
            if (f.getFileNameWithoutExtension() == proc.currentPresetName())
                return f;
    return {};
}

void HeaderBar::askToDelete(const juce::File& file)
{
    showDialog(std::make_unique<Dialog>("Delete preset?", "\"" + file.getFileNameWithoutExtension() + "\" goes to the trash.",
                                        "Delete"),
               [this, file](int answer) {
        dialog.reset();
        if (answer == 1 && proc.deleteUserPreset(file))
            refreshUserFiles();
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
    const bool big = !theme().matrix;
    const auto logo = legendFont(big ? 30.0f : 26.0f, !big || theme().face == Theme::Face::Courier).withExtraKerningFactor(big ? -0.01f : 0.03f);
    const auto name = themed("GLIDE90");
    g.setFont(logo);
    g.setColour(theme().text);
    g.drawText(name, PanelLookAndFeel::kMargin + 2, 0, 140, getHeight(), juce::Justification::centredLeft);
}

void HeaderBar::resized()
{
    // Three groups, 24 apart: the preset and SAVE, the A/B switch, undo and redo; SETUP at the far end.
    // Everything as tall as the preset's window.
    const int h = 32, y = (getHeight() - h) / 2;
    presets.setBounds(160, y, 360, h);
    save.setBounds(presets.getRight() + 6, y, 62, h);
    a.setBounds(save.getRight() + 24, y, 40, h);
    b.setBounds(a.getRight(), y, 40, h);
    undoKey.setBounds(b.getRight() + 24, y, 36, h);
    redoKey.setBounds(undoKey.getRight() + 4, y, 36, h);
    setup.setBounds(getWidth() - PanelLookAndFeel::kMargin - 84, y, 84, h);
}

} // namespace glide90::ui
