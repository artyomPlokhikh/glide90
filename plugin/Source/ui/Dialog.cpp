#include "Dialog.h"

#include "PanelLookAndFeel.h"

namespace glide90::ui
{
namespace
{
constexpr int kCardWidth = 380, kPadding = 22;
} // namespace

Dialog::Dialog(const juce::String& titleText, const juce::String& messageText, const juce::String& confirmLabel,
               bool withField, const juce::String& fieldText)
    : title(titleText), message(messageText), confirm(confirmLabel)
{
    setWantsKeyboardFocus(true);
    if (withField)
    {
        field = std::make_unique<ReadoutField>(true);
        field->maxLength = 40;
        field->setText(fieldText);
        field->onReturn = [this] { close(true); };
        field->onEscape = [this] { close(false); };
        addAndMakeVisible(*field);
    }
    for (auto* key : { &confirm, &cancel })
        addAndMakeVisible(key);
    confirm.onClick = [this] { close(true); };
    cancel.onClick = [this] { close(false); };
}

void Dialog::focus()
{
    if (field != nullptr)
        field->grabKeyboardFocus();
    else
        grabKeyboardFocus();
}

void Dialog::parentSizeChanged()
{
    if (auto* parent = getParentComponent())
        setBounds(parent->getLocalBounds());
}

juce::Rectangle<int> Dialog::card() const
{
    const int height = field != nullptr ? 196 : 152;
    return getLocalBounds().withSizeKeepingCentre(kCardWidth, height);
}

void Dialog::close(bool confirmed)
{
    exitModalState(confirmed ? 1 : 0);
}

bool Dialog::keyPressed(const juce::KeyPress& key)
{
    if (key == juce::KeyPress::returnKey || key == juce::KeyPress::escapeKey)
    {
        close(key == juce::KeyPress::returnKey);
        return true;
    }
    return false;
}

void Dialog::paint(juce::Graphics& g)
{
    const auto& t = theme();
    // The plugin behind it, dimmed.
    g.fillAll(juce::Colours::black.withAlpha(t.backdrop == Theme::Backdrop::Matte ? 0.55f : 0.35f));

    // A card like the modules', on the window's own ground (the cards of some looks are see-through).
    const auto c = card().toFloat();
    juce::DropShadow(juce::Colours::black.withAlpha(0.55f), 30, { 0, 12 }).drawForRectangle(g, card());
    g.setColour(t.base);
    g.fillRoundedRectangle(c, t.cardCorner);
    drawCard(g, c);

    auto inner = c.reduced((float) kPadding, 14.0f);
    drawCardTitle(g, title, inner.removeFromTop(26.0f));
    inner.removeFromTop(6.0f);
    g.setFont(legendFont(13.5f));
    g.setColour(t.textDim);
    g.drawFittedText(themed(message), inner.removeFromTop(18.0f).toNearestInt(), juce::Justification::centredLeft, 2);
    if (field != nullptr)
        drawGlass(g, field->getBounds().toFloat());
}

void Dialog::resized()
{
    const auto c = card().reduced(kPadding, 18);
    const auto inner = card().reduced(kPadding, 14);
    if (field != nullptr)
        field->setBounds(inner.getX(), inner.getY() + 58, inner.getWidth(), 36);
    const int keyW = 84, keyH = 32;
    confirm.setBounds(c.getRight() - keyW, c.getBottom() - keyH, keyW, keyH);
    cancel.setBounds(confirm.getX() - 8 - keyW, c.getBottom() - keyH, keyW, keyH);
}

} // namespace glide90::ui
