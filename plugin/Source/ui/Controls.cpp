#include "Controls.h"

#include "PanelLookAndFeel.h"
#include "Parameters.h"

namespace glide90::ui
{
namespace
{

constexpr float kDragPixels = 200.0f, kFineDragPixels = 1600.0f;

juce::RangedAudioParameter& parameter(juce::AudioProcessorValueTreeState& state, const char* id)
{
    auto* p = state.getParameter(id);
    jassert(p != nullptr);
    return *p;
}

juce::StringArray choicesOf(juce::AudioProcessorValueTreeState& state, const char* id)
{
    if (auto* c = dynamic_cast<juce::AudioParameterChoice*>(state.getParameter(id)))
        return c->choices;
    return {};
}

void drawLegend(juce::Graphics& g, const juce::String& text, juce::Rectangle<float> area, bool enabled,
                juce::Justification j = juce::Justification::centred)
{
    g.setFont(legendFont(13.5f));
    g.setColour(enabled ? theme().text : theme().textDim.withMultipliedAlpha(0.6f));
    // A long name is narrowed a little rather than cut.
    g.drawFittedText(themed(text), area.toNearestInt().expanded(6, 0), j, 1, 0.75f);
}

float defaultOf(const juce::RangedAudioParameter& p)
{
    return p.convertFrom0to1(p.getDefaultValue());
}

// Calls back when the mouse goes down anywhere outside `inside` (in any of the plugin's windows).
class OutsideClick : public juce::MouseListener
{
public:
    OutsideClick(juce::Component& c, std::function<void()> callback) : inside(c), onOutside(std::move(callback))
    {
        juce::Desktop::getInstance().addGlobalMouseListener(this);
    }

    ~OutsideClick() override
    {
        juce::Desktop::getInstance().removeGlobalMouseListener(this);
    }

    void mouseDown(const juce::MouseEvent& e) override
    {
        if (e.eventComponent != &inside && !inside.isParentOf(e.eventComponent))
            onOutside();
    }

private:
    juce::Component& inside;
    std::function<void()> onOutside;
};

} // namespace

bool wantsDefault(const juce::ModifierKeys& mods)
{
#if JUCE_MAC
    return mods.isAltDown() || mods.isCommandDown();
#else
    return mods.isAltDown();
#endif
}

// --- Knob ---

Knob::Knob(juce::AudioProcessorValueTreeState& state, const char* paramId, const juce::String& text, Size s)
    : param(parameter(state, paramId)),
      attachment(param, [this](float v) {
          value = v;
          repaint();
      }),
      label(text), size(s)
{
    attachment.sendInitialUpdate();
    setRepaintsOnMouseActivity(false);
}

// --- ReadoutField ---

namespace
{
} // namespace

// The glass a field sits on, for a field that opens over something else (a knob's value).
class Knob::FieldGlass : public juce::Component
{
public:
    FieldGlass() { addAndMakeVisible(field); }
    void paint(juce::Graphics& g) override { drawGlass(g, getLocalBounds().toFloat(), 3.0f); }
    void resized() override { field.setBounds(getLocalBounds()); }

    ReadoutField field;
};

Knob::~Knob()
{
    if (editor != nullptr)
        editor->onFocusLost = nullptr;
}

ReadoutField::ReadoutField(bool largeText) : large(largeText)
{
    setWantsKeyboardFocus(true);
    setMouseCursor(juce::MouseCursor::IBeamCursor);
}

ReadoutField::~ReadoutField()
{
    stopTimer();
}

void ReadoutField::setText(const juce::String& t)
{
    text = t.substring(0, maxLength);
    caret = text.length();
    allSelected = text.isNotEmpty();
    repaint();
}

float ReadoutField::textX() const
{
    const float w = readoutWidth(text, large);
    return centred ? ((float) getWidth() - w) * 0.5f : 6.0f;
}

void ReadoutField::paint(juce::Graphics& g)
{
    const auto& t = theme();
    const auto area = getLocalBounds().toFloat();
    const float x = textX();
    if (allSelected)
    {
        g.setColour(t.ink.withAlpha(0.25f));
        g.fillRect(juce::Rectangle<float>(x - 2.0f, area.getCentreY() - (large ? 11.0f : 8.0f), readoutWidth(text, large) + 4.0f,
                                          large ? 22.0f : 16.0f));
    }
    drawReadout(g, text, area.withLeft(x), t.ink, juce::Justification::centredLeft, large);
    if (hasKeyboardFocus(false) && caretOn && !allSelected)
    {
        const float cx = x + readoutWidth(text.substring(0, caret), large) + (caret > 0 ? 1.5f : -1.0f);
        g.setColour(t.ink);
        g.fillRect(juce::Rectangle<float>(cx, area.getCentreY() - (large ? 10.0f : 7.0f), 1.5f, large ? 20.0f : 14.0f));
    }
}

void ReadoutField::timerCallback()
{
    caretOn = !caretOn;
    repaint();
}

void ReadoutField::focusGained(FocusChangeType)
{
    caretOn = true;
    startTimer(530);
    repaint();
}

void ReadoutField::focusLost(FocusChangeType)
{
    stopTimer();
    repaint();
    if (onFocusLost)
        onFocusLost();
}

void ReadoutField::moveCaret(int to)
{
    caret = juce::jlimit(0, text.length(), to);
    allSelected = false;
    caretOn = true;
    startTimer(530);
    repaint();
}

void ReadoutField::insert(const juce::String& s)
{
    if (allSelected)
    {
        text.clear();
        caret = 0;
    }
    const auto clean = s.removeCharacters("\r\n\t").substring(0, juce::jmax(0, maxLength - text.length()));
    text = text.substring(0, caret) + clean + text.substring(caret);
    moveCaret(caret + clean.length());
}

bool ReadoutField::keyPressed(const juce::KeyPress& key)
{
    const auto mods = key.getModifiers();
    const int code = key.getKeyCode();
    if (key == juce::KeyPress::returnKey)
    {
        if (onReturn)
            onReturn();
        return true;
    }
    if (key == juce::KeyPress::escapeKey)
    {
        if (onEscape)
            onEscape();
        return true;
    }
    if (mods.isCommandDown() && (code == 'A' || code == 'a'))
    {
        allSelected = text.isNotEmpty();
        caret = text.length();
        repaint();
        return true;
    }
    if (mods.isCommandDown() && (code == 'C' || code == 'c'))
    {
        juce::SystemClipboard::copyTextToClipboard(text);
        return true;
    }
    if (mods.isCommandDown() && (code == 'V' || code == 'v'))
    {
        insert(juce::SystemClipboard::getTextFromClipboard());
        return true;
    }
    if (key == juce::KeyPress::backspaceKey || key == juce::KeyPress::deleteKey)
    {
        if (allSelected)
        {
            text.clear();
            moveCaret(0);
        }
        else if (key == juce::KeyPress::backspaceKey && caret > 0)
        {
            text = text.substring(0, caret - 1) + text.substring(caret);
            moveCaret(caret - 1);
        }
        else if (key == juce::KeyPress::deleteKey && caret < text.length())
        {
            text = text.substring(0, caret) + text.substring(caret + 1);
            moveCaret(caret);
        }
        return true;
    }
    if (key == juce::KeyPress::leftKey)
        return moveCaret(allSelected ? 0 : caret - 1), true;
    if (key == juce::KeyPress::rightKey)
        return moveCaret(allSelected ? text.length() : caret + 1), true;
    if (key == juce::KeyPress::homeKey)
        return moveCaret(0), true;
    if (key == juce::KeyPress::endKey)
        return moveCaret(text.length()), true;
    const auto ch = key.getTextCharacter();
    if (ch >= ' ' && !mods.isCommandDown() && !mods.isCtrlDown())
    {
        insert(juce::String::charToString(ch));
        return true;
    }
    return false;
}

void ReadoutField::mouseDown(const juce::MouseEvent& e)
{
    // Click: the caret goes to the nearest gap between characters.
    grabKeyboardFocus();
    const float x = e.position.x - textX();
    int best = 0;
    float bestDist = 1e9f;
    for (int i = 0; i <= text.length(); ++i)
        if (const float d = std::abs(readoutWidth(text.substring(0, i), large) - x); d < bestDist)
        {
            bestDist = d;
            best = i;
        }
    moveCaret(best);
}

void Knob::setLabel(const juce::String& text)
{
    if (text != label)
    {
        label = text;
        repaint();
    }
}

juce::Rectangle<float> Knob::knobArea() const
{
    const auto r = getLocalBounds().toFloat().withTrimmedTop(16.0f).withTrimmedBottom(16.0f);
    const float wanted = size == Size::Hero ? 108.0f : (size == Size::Large ? 68.0f : 56.0f);
    const float d = juce::jmin(r.getWidth(), r.getHeight(), wanted);
    return juce::Rectangle<float>(d, d).withCentre(r.getCentre());
}

juce::Rectangle<int> Knob::valueArea() const
{
    return getLocalBounds().removeFromBottom(14);
}

float Knob::snapped(float normalised) const
{
    const auto& range = param.getNormalisableRange();
    return range.snapToLegalValue(range.convertFrom0to1(juce::jlimit(0.0f, 1.0f, normalised)));
}

void Knob::paint(juce::Graphics& g)
{
    const bool enabled = isEnabled();
    drawLegend(g, label, getLocalBounds().removeFromTop(15).toFloat(), enabled);
    drawKnob(g, knobArea(), param.convertTo0to1(value), enabled, size != Size::Small);
    if (editor == nullptr)
    {
        const auto colour = !enabled ? theme().textDim.withMultipliedAlpha(0.5f) : (hover || dragging ? theme().accent : theme().textDim);
        drawReadout(g, param.getCurrentValueAsText(), valueArea().toFloat(), colour, juce::Justification::centred);
    }
}

void Knob::mouseEnter(const juce::MouseEvent&)
{
    hover = true;
    repaint();
}

void Knob::mouseExit(const juce::MouseEvent&)
{
    hover = false;
    repaint();
}

void Knob::mouseDown(const juce::MouseEvent& e)
{
    if (!isEnabled())
        return;
    if (e.mods.isPopupMenu() || wantsDefault(e.mods))
    {
        attachment.setValueAsCompleteGesture(defaultOf(param));
            return;
    }
    attachment.beginGesture();
    dragging = true;
    dragNormalised = param.convertTo0to1(value);
    lastDrag = e.position;
    repaint();
}

void Knob::mouseDrag(const juce::MouseEvent& e)
{
    if (!dragging)
        return;
    const auto delta = e.position - lastDrag;
    lastDrag = e.position;
    const float pixels = e.mods.isShiftDown() ? kFineDragPixels : kDragPixels;
    dragNormalised = juce::jlimit(0.0f, 1.0f, dragNormalised + (delta.x - delta.y) / pixels);
    attachment.setValueAsPartOfGesture(snapped(dragNormalised));
}

void Knob::mouseUp(const juce::MouseEvent&)
{
    if (!dragging)
        return;
    attachment.endGesture();
    dragging = false;
    repaint();
}

void Knob::mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    if (!isEnabled() || editor != nullptr || std::abs(wheel.deltaY) < 1e-6f)
        return;
    const int dir = (wheel.deltaY > 0.0f) != wheel.isReversed ? 1 : -1;
    const auto& range = param.getNormalisableRange();
    float next;
    if (range.interval > 0.0f && (range.end - range.start) / range.interval <= 64.0f)
        next = range.snapToLegalValue(value + (float) dir * range.interval); // choices, small integer ranges
    else
        next = snapped(param.convertTo0to1(value) + (float) dir * (e.mods.isShiftDown() ? 0.002f : 0.01f));
    attachment.setValueAsCompleteGesture(next);
}

void Knob::mouseDoubleClick(const juce::MouseEvent&)
{
    if (!isEnabled())
        return;
    // Type a value: "25", "50 %", "1.2 s", "2.5k", "THRU", "1/4 D".
    outsideClick.reset();
    editorBox = std::make_unique<FieldGlass>();
    editor = &editorBox->field;
    closingEditor = false;
    editor->setCentred(true);
    editor->maxLength = 16;
    editor->setText(param.getCurrentValueAsText());
    editorBox->setBounds(valueArea().withSizeKeepingCentre(juce::jmax(64, getWidth() - 4), 20));
    addAndMakeVisible(*editorBox);
    editor->grabKeyboardFocus();
    editor->onReturn = [this] { closeEditor(true); };
    editor->onEscape = [this] { closeEditor(false); };
    editor->onFocusLost = [this] { closeEditor(true); };
    // A click anywhere else applies the value, as in a host's own fields: clicks on the panel or on
    // another knob do not take the keyboard focus, so focus loss alone would leave the editor open.
    outsideClick = std::make_unique<OutsideClick>(*editor, [this] { closeEditor(true); });
    repaint();
}

void Knob::closeEditor(bool apply)
{
    // Return, a click outside and the focus loss that follows can all land here: act once.
    if (editor == nullptr || closingEditor)
        return;
    closingEditor = true;
    const auto text = editor->getText();
    // The editor or the click watcher may be the caller: delete them once it has returned.
    juce::MessageManager::callAsync([safe = juce::Component::SafePointer<Knob>(this)] {
        if (safe != nullptr)
        {
            safe->outsideClick.reset();
            safe->editor = nullptr;
            safe->editorBox.reset();
            safe->repaint();
        }
    });
    if (!apply)
        return;
    // Text that names no value ("", "abc") leaves the parameter as it is.
    if (const auto typed = parseParameterText(param, text))
    {
        const auto& range = param.getNormalisableRange();
        attachment.setValueAsCompleteGesture(range.snapToLegalValue(juce::jlimit(range.start, range.end, *typed)));
    }
}

// --- ChoiceList ---

ChoiceList::ChoiceList(juce::AudioProcessorValueTreeState& state, const char* paramId, const juce::String& text,
                       const juce::StringArray& names, const juce::Array<int>& paramValues)
    : param(parameter(state, paramId)),
      attachment(param, [this](float v) {
          selected = juce::jmax(0, values.indexOf(juce::roundToInt(v)));
          repaint();
      }),
      label(text), labels(names), values(paramValues)
{
    jassert(labels.size() == values.size() && labels.size() >= 2);
    attachment.sendInitialUpdate();
}

juce::Rectangle<float> ChoiceList::rowArea(int row) const
{
    // The rows as one block, centred in the height under the name.
    const float top = 16.0f + ((float) getHeight() - 32.0f - (float) (labels.size() * kRowHeight)) * 0.5f;
    return { 0.0f, top + (float) (row * kRowHeight), (float) getWidth(), (float) kRowHeight };
}

void ChoiceList::paint(juce::Graphics& g)
{
    const auto& t = theme();
    const bool enabled = isEnabled();
    drawLegend(g, label, getLocalBounds().removeFromTop(15).toFloat(), enabled);

    // Left-aligned as a block, centred as a whole: the marks line up.
    float widest = 0.0f;
    for (int i = 0; i < labels.size(); ++i)
        widest = juce::jmax(widest, juce::GlyphArrangement::getStringWidth(legendFont(13.5f, true), themed(labels[i])));
    const float x = ((float) getWidth() - widest - 14.0f) * 0.5f;
    for (int i = 0; i < labels.size(); ++i)
    {
        const auto row = rowArea(i);
        const bool on = i == selected;
        const auto mark = juce::Rectangle<float>(5.0f, 5.0f).withCentre({ x + 3.0f, row.getCentreY() });
        if (on)
        {
            g.setColour(enabled ? t.accent : t.textDim);
            if (t.glow && enabled)
                juce::DropShadow(t.accent.withAlpha(0.8f), 6, {}).drawForRectangle(g, mark.toNearestInt());
            g.fillEllipse(mark);
        }
        else
        {
            g.setColour(t.textDim.withMultipliedAlpha(0.6f));
            g.drawEllipse(mark.reduced(0.5f), 1.0f);
        }
        g.setFont(legendFont(13.5f, on));
        g.setColour(on && enabled ? t.text : t.textDim.withMultipliedAlpha(enabled ? 1.0f : 0.6f));
        g.drawText(themed(labels[i]), row.withLeft(x + 14.0f), juce::Justification::centredLeft);
    }
}

void ChoiceList::select(int row)
{
    row = juce::jlimit(0, labels.size() - 1, row);
    if (row != selected)
        attachment.setValueAsCompleteGesture((float) values[row]);
}

void ChoiceList::mouseDown(const juce::MouseEvent& e)
{
    if (!isEnabled())
        return;
    if (wantsDefault(e.mods))
    {
        attachment.setValueAsCompleteGesture(defaultOf(param));
        return;
    }
    for (int i = 0; i < labels.size(); ++i)
        if (rowArea(i).contains(e.position))
            return select(i);
}

void ChoiceList::mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails& wheel)
{
    if (isEnabled() && std::abs(wheel.deltaY) > 1e-6f)
        select(selected + ((wheel.deltaY > 0.0f) != wheel.isReversed ? -1 : 1));
}

// --- ToggleKey ---

ToggleKey::ToggleKey(juce::AudioProcessorValueTreeState& state, const char* paramId, const juce::String& legend, bool invert)
    : juce::TextButton(legend),
      param(parameter(state, paramId)),
      attachment(param, [this](float v) {
          setToggleState((v > 0.5f) != inverted, juce::dontSendNotification);
          repaint();
      }),
      inverted(invert)
{
    getProperties().set(PanelLookAndFeel::kLed, true);
    onClick = [this] {
        if (wantsDefault(juce::ModifierKeys::currentModifiers))
            attachment.setValueAsCompleteGesture(defaultOf(param));
        else
            attachment.setValueAsCompleteGesture((getToggleState() != inverted) ? 0.0f : 1.0f);
    };
    attachment.sendInitialUpdate();
}

// --- CycleKey ---

CycleKey::CycleKey(juce::AudioProcessorValueTreeState& state, const char* paramId, int litChoice)
    : param(*dynamic_cast<juce::AudioParameterChoice*>(&parameter(state, paramId))),
      attachment(param, [this](float v) {
          const int index = juce::jlimit(0, param.choices.size() - 1, juce::roundToInt(v));
          setButtonText(param.choices[index]);
          setToggleState(index == lit, juce::dontSendNotification);
          repaint();
      }),
      lit(litChoice)
{
    getProperties().set(PanelLookAndFeel::kLed, true);
    onClick = [this] {
        if (wantsDefault(juce::ModifierKeys::currentModifiers))
            attachment.setValueAsCompleteGesture(defaultOf(param));
        else
            attachment.setValueAsCompleteGesture((float) ((param.getIndex() + 1) % param.choices.size()));
    };
    attachment.sendInitialUpdate();
}

// --- GlassSelector ---

GlassSelector::GlassSelector(bool withArrows, bool largeText) : arrows(withArrows), large(largeText)
{
    startTimerHz(8);
}

GlassSelector::~GlassSelector()
{
    stopTimer();
}

void GlassSelector::timerCallback()
{
    const auto text = getText ? getText() : juce::String();
    if (text != shown)
    {
        shown = text;
        repaint();
    }
}

void GlassSelector::paint(juce::Graphics& g)
{
    const auto& t = theme();
    const auto r = getLocalBounds().toFloat();
    drawGlass(g, r);
    auto textArea = r.reduced(5.0f, 0.0f);
    const auto ink = t.ink.withMultipliedAlpha(isEnabled() ? 1.0f : 0.35f);
    if (arrows)
    {
        g.setColour(ink.withMultipliedAlpha(0.85f));
        for (int side : { -1, 1 })
        {
            const float x = side < 0 ? r.getX() + 10.0f : r.getRight() - 10.0f;
            juce::Path tri;
            tri.addTriangle(x - side * 3.0f, r.getCentreY() - 4.5f, x - side * 3.0f, r.getCentreY() + 4.5f, x + side * 3.5f, r.getCentreY());
            g.fillPath(tri);
        }
        textArea = r.reduced(22.0f, 0.0f);
    }
    // A name too long for the window is cut, as on a character display.
    auto text = shown;
    while (text.isNotEmpty() && readoutWidth(text, large) > textArea.getWidth())
        text = text.dropLastCharacters(1);
    drawReadout(g, text, textArea, ink, arrows ? juce::Justification::centred : juce::Justification::centredLeft, large,
                large);
}

void GlassSelector::mouseDown(const juce::MouseEvent& e)
{
    if (!isEnabled())
        return;
    if (arrows && onStep && (e.position.x < 22.0f || e.position.x > (float) getWidth() - 22.0f))
    {
        onStep(e.position.x < 22.0f ? -1 : 1);
        return;
    }
    if (!fillMenu)
        return;
    juce::PopupMenu menu;
    menu.setLookAndFeel(&getLookAndFeel());
    fillMenu(menu);
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this).withMinimumWidth(getWidth()));
}

// --- LevelMeter ---

namespace
{
constexpr int kSegments = 12;
constexpr float kFloorDb = -40.0f;
} // namespace

LevelMeter::LevelMeter(std::function<float()> peak) : takePeak(std::move(peak)) {}

LevelMeter::~LevelMeter() = default;

void LevelMeter::frame(double seconds)
{
    const float peak = takePeak ? takePeak() : 0.0f;
    const float db = juce::Decibels::gainToDecibels(peak, -100.0f);
    // Up at once, down at 24 dB a second whatever the display's rate.
    const float elapsed = lastFrame > 0.0 ? (float) juce::jlimit(0.0, 0.1, seconds - lastFrame) : 0.0f;
    lastFrame = seconds;
    levelDb = juce::jmax(db, levelDb - 24.0f * elapsed);
    // Segments from -40 dB up in even steps; the last one only at full scale.
    int lit = 0;
    while (lit < kSegments)
    {
        const float threshold = lit == kSegments - 1 ? -0.1f : kFloorDb * (1.0f - (float) lit / (float) (kSegments - 1));
        if (levelDb < threshold)
            break;
        ++lit;
    }
    if (lit != litShown)
    {
        litShown = lit;
        repaint();
    }
}

void LevelMeter::paint(juce::Graphics& g)
{
    // Loudest at the top; the top segment is clipping.
    const auto r = getLocalBounds().toFloat();
    const float cell = r.getHeight() / (float) kSegments;
    for (int i = 0; i < kSegments; ++i)
    {
        const auto segment = juce::Rectangle<float>(r.getX(), r.getBottom() - cell * (float) (i + 1), r.getWidth(), cell).reduced(0.0f, cell * 0.18f);
        const auto colour = i == kSegments - 1 ? juce::Colour(0xffff3b2c) : theme().accent;
        drawLed(g, segment, colour, i < litShown);
    }
}

} // namespace glide90::ui

