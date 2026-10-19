#pragma once

#include "Controls.h"

namespace glide90::ui
{

// A dialog inside the plugin window, in the plugin's look: the window dimmed, a card in the menus'
// colours with a title, a line of text, an optional name field and two keys. Return confirms,
// Escape cancels. Run it with enterModalState(); it exits with 1 (confirmed) or 0.
class Dialog : public juce::Component
{
public:
    Dialog(const juce::String& title, const juce::String& message, const juce::String& confirmLabel,
           bool withField = false, const juce::String& fieldText = {});

    juce::String getFieldText() const { return field != nullptr ? field->getText().trim() : juce::String(); }
    // Takes the keyboard (the field's, or its own for Return and Escape). Call once it is showing.
    void focus();

    void paint(juce::Graphics&) override;
    void resized() override;
    bool keyPressed(const juce::KeyPress&) override;
    void parentSizeChanged() override;

private:
    juce::Rectangle<int> card() const;
    void close(bool confirmed);

    juce::String title, message;
    std::unique_ptr<ReadoutField> field;
    juce::TextButton confirm, cancel { "Cancel" };
};

} // namespace glide90::ui
