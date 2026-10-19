#include "Controls.h"

#include "PanelLookAndFeel.h"
#include "Parameters.h"

namespace glide90::ui
{
namespace
{

constexpr float kValuePitch = kSmallTextPitch;
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
    g.setFont(legendFont(13.0f));
    g.setColour(enabled ? colours::silkscreen : colours::silkscreenDim);
    g.drawText(text, area, j);
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

Knob::~Knob()
{
    if (editor != nullptr)
        editor->onFocusLost = nullptr;
}

juce::String Knob::getTooltip()
{
#if JUCE_MAC
    const juce::String reset = "Alt- or Cmd-click: default";
#else
    const juce::String reset = "Alt-click: default";
#endif
    const auto own = SettableTooltipClient::getTooltip();
    return (own.isEmpty() ? juce::String() : own + "\n") + "Double-click: type a value. " + reset;
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
    const float d = juce::jmin(r.getWidth(), r.getHeight(), size == Size::Large ? 68.0f : 54.0f);
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

void Knob::touch()
{
    if (onTouch)
        onTouch(param);
}

void Knob::paint(juce::Graphics& g)
{
    const bool enabled = isEnabled();
    drawLegend(g, label, getLocalBounds().removeFromTop(15).toFloat(), enabled);
    drawKnob(g, knobArea(), param.convertTo0to1(value), enabled, size == Size::Large);
    if (editor == nullptr)
    {
        const auto colour = !enabled ? colours::silkscreenDim.withAlpha(0.5f)
                                     : (hover || dragging ? colours::display : colours::silkscreenDim);
        drawMatrixText(g, param.getCurrentValueAsText().toUpperCase(), valueArea().toFloat(), kValuePitch, colour,
                       juce::Justification::centred);
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
        touch();
        return;
    }
    attachment.beginGesture();
    dragging = true;
    dragNormalised = param.convertTo0to1(value);
    lastDrag = e.position;
    touch();
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
    touch();
}

void Knob::mouseDoubleClick(const juce::MouseEvent&)
{
    if (!isEnabled())
        return;
    // Type a value: "25", "50 %", "1.2 s", "2.5k", "THRU", "1/4 D".
    outsideClick.reset();
    editor = std::make_unique<juce::TextEditor>();
    closingEditor = false;
    editor->setFont(legendFont(14.0f));
    editor->setJustification(juce::Justification::centred);
    editor->setText(param.getCurrentValueAsText(), false);
    editor->selectAll();
    editor->setBounds(valueArea().withSizeKeepingCentre(juce::jmax(60, getWidth() - 6), 18));
    addAndMakeVisible(*editor);
    editor->grabKeyboardFocus();
    editor->onReturnKey = [this] { closeEditor(true); };
    editor->onEscapeKey = [this] { closeEditor(false); };
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
            safe->editor.reset();
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
        touch();
    }
}

// --- SelectorSwitch ---

SelectorSwitch::SelectorSwitch(juce::AudioProcessorValueTreeState& state, const char* paramId, const juce::String& text,
                               const juce::StringArray& names)
    : param(parameter(state, paramId)),
      attachment(param, [this](float v) {
          selected = juce::roundToInt(v);
          repaint();
      }),
      label(text), positions(names.isEmpty() ? choicesOf(state, paramId) : names)
{
    attachment.sendInitialUpdate();
}

float SelectorSwitch::angleOf(int position) const
{
    // Spread over +-70 degrees around straight up.
    const int n = juce::jmax(2, positions.size());
    return juce::degreesToRadians(-70.0f + 140.0f * (float) position / (float) (n - 1));
}

// Laid out from the top: the legend, the position labels on their arc right under it, the knob.
juce::Point<float> SelectorSwitch::knobCentre() const
{
    return { getWidth() * 0.5f, 16.0f + 8.0f + (knobRadius() + 22.0f) * std::cos(angleOf(positions.size() / 2)) + 4.0f };
}

float SelectorSwitch::knobRadius() const
{
    return 19.0f;
}

juce::Rectangle<float> SelectorSwitch::labelArea(int position) const
{
    const float a = angleOf(position);
    const float radius = knobRadius() + 22.0f;
    const auto p = knobCentre() + juce::Point<float>(std::sin(a) * radius * 1.35f, -std::cos(a) * radius);
    return juce::Rectangle<float>(64.0f, 16.0f).withCentre(p);
}

void SelectorSwitch::paint(juce::Graphics& g)
{
    const bool enabled = isEnabled();
    drawLegend(g, label, getLocalBounds().removeFromTop(15).toFloat(), enabled);
    const auto c = knobCentre();
    const float r = knobRadius();
    for (int i = 0; i < positions.size(); ++i)
    {
        const float a = angleOf(i);
        const auto dir = juce::Point<float>(std::sin(a), -std::cos(a));
        g.setColour(colours::silkscreen.withAlpha(i == selected ? 0.9f : 0.45f));
        g.drawLine(juce::Line<float>(c + dir * (r + 3.0f), c + dir * (r + 8.0f)), 1.2f);
        g.setFont(legendFont(12.5f, i == selected));
        g.setColour(i == selected && enabled ? colours::silkscreen : colours::silkscreenDim);
        g.drawText(positions[i], labelArea(i), juce::Justification::centred);
    }
    // The knob: drawKnob with its line set to the selected position (its scale spans +-135 degrees).
    const float position = (angleOf(selected) + 2.36f) / 4.72f;
    drawKnob(g, juce::Rectangle<float>(r * 2.0f / 0.74f, r * 2.0f / 0.74f).withCentre(c), position, enabled, false, false);
}

void SelectorSwitch::select(int position)
{
    position = juce::jlimit(0, positions.size() - 1, position);
    if (position != selected)
        attachment.setValueAsCompleteGesture((float) position);
}

void SelectorSwitch::mouseDown(const juce::MouseEvent& e)
{
    if (!isEnabled())
        return;
    if (wantsDefault(e.mods))
        return select(juce::roundToInt(defaultOf(param)));
    for (int i = 0; i < positions.size(); ++i)
        if (labelArea(i).expanded(2.0f).contains(e.position))
            return select(i);
    if (e.position.getDistanceFrom(knobCentre()) < knobRadius() + 6.0f)
        select(e.mods.isPopupMenu() ? selected - 1 : (selected + 1) % positions.size());
}

void SelectorSwitch::mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails& wheel)
{
    if (isEnabled() && std::abs(wheel.deltaY) > 1e-6f)
        select(selected + ((wheel.deltaY > 0.0f) != wheel.isReversed ? 1 : -1));
}

// --- ToggleSwitch ---

ToggleSwitch::ToggleSwitch(juce::AudioProcessorValueTreeState& state, const char* paramId, const juce::String& text,
                           const juce::StringArray& names, const juce::Array<int>& paramValues)
    : param(parameter(state, paramId)),
      attachment(param, [this](float v) {
          const int i = values.indexOf(juce::roundToInt(v));
          selected = juce::jmax(0, i);
          repaint();
      }),
      label(text), labels(names), values(paramValues)
{
    jassert(labels.size() == values.size() && labels.size() >= 2);
    attachment.sendInitialUpdate();
}

// The lever sits under the legend; its positions are a fixed 15 px apart, labelled on the right.
juce::Point<float> ToggleSwitch::pivot() const
{
    // Lever and labels as one group, centred under the legend and in the height below it.
    return { (float) getWidth() * 0.5f - 18.0f, 16.0f + ((float) getHeight() - 32.0f) * 0.5f };
}

float ToggleSwitch::positionY(int position) const
{
    return pivot().y + 15.0f * ((float) position - (float) (labels.size() - 1) * 0.5f);
}

void ToggleSwitch::paint(juce::Graphics& g)
{
    const bool enabled = isEnabled();
    drawLegend(g, label, getLocalBounds().removeFromTop(15).toFloat(), enabled);
    const auto p = pivot();

    // Labels beside each position.
    for (int i = 0; i < labels.size(); ++i)
    {
        g.setFont(legendFont(12.5f, i == selected));
        g.setColour(i == selected && enabled ? colours::silkscreen : colours::silkscreenDim);
        g.drawText(labels[i], juce::Rectangle<float>(p.x + 14.0f, positionY(i) - 7.0f, (float) getWidth() - p.x - 14.0f, 14.0f),
                   juce::Justification::centredLeft);
    }

    // Hex nut.
    juce::Path nut;
    nut.addPolygon(p, 6, 8.0f, juce::MathConstants<float>::pi / 6.0f);
    g.setGradientFill(juce::ColourGradient(juce::Colour(0xff8a8a86), p.x, p.y - 8.0f, juce::Colour(0xff3a3a38), p.x, p.y + 8.0f, false));
    g.fillPath(nut);
    g.setColour(juce::Colours::black.withAlpha(0.6f));
    g.strokePath(nut, juce::PathStrokeType(0.8f));

    // Bat lever, pointing at the selected position (straight at you in the middle of three).
    const float targetY = positionY(selected);
    const float dy = juce::jlimit(-1.0f, 1.0f, (targetY - p.y) / 15.0f);
    if (std::abs(dy) < 0.2f)
    {
        g.setGradientFill(juce::ColourGradient(juce::Colours::white, p.x - 2.0f, p.y - 3.0f, juce::Colour(0xff6b6b68), p.x + 3.0f, p.y + 4.0f, true));
        g.fillEllipse(juce::Rectangle<float>(9.0f, 9.0f).withCentre(p));
    }
    else
    {
        const auto tip = p.translated(0.0f, dy * 14.0f);
        juce::Path lever;
        lever.startNewSubPath(p.x - 2.5f, p.y);
        lever.lineTo(tip.x - 3.5f, tip.y);
        lever.lineTo(tip.x + 3.5f, tip.y);
        lever.lineTo(p.x + 2.5f, p.y);
        lever.closeSubPath();
        g.setGradientFill(juce::ColourGradient(juce::Colour(0xffe6e6e2), p.x - 3.0f, p.y, juce::Colour(0xff5a5a57), p.x + 3.0f, p.y, false));
        g.fillPath(lever);
        g.setColour(juce::Colour(0xfff2f2ee));
        g.fillEllipse(juce::Rectangle<float>(8.0f, 6.0f).withCentre(tip));
    }
    if (!enabled)
    {
        g.setColour(colours::anodized.withAlpha(0.5f));
        g.fillRect(getLocalBounds().withTrimmedTop(16));
    }
}

void ToggleSwitch::mouseDown(const juce::MouseEvent& e)
{
    if (!isEnabled())
        return;
    if (wantsDefault(e.mods))
    {
        attachment.setValueAsCompleteGesture(defaultOf(param));
        return;
    }
    // Click a position (or its label) to throw the lever there; click the lever to step.
    int target = -1;
    for (int i = 0; i < labels.size(); ++i)
        if (std::abs(e.position.y - positionY(i)) < 7.5f && e.position.x > pivot().x + 8.0f)
            target = i;
    if (target < 0)
        target = e.position.y < pivot().y - 4.0f ? selected - 1 : (e.position.y > pivot().y + 4.0f ? selected + 1 : selected + 1);
    target = (target + labels.size()) % labels.size();
    attachment.setValueAsCompleteGesture((float) values[target]);
}

// --- ToggleKey ---

ToggleKey::ToggleKey(juce::AudioProcessorValueTreeState& state, const char* paramId, const juce::String& legend)
    : juce::TextButton(legend),
      param(parameter(state, paramId)),
      attachment(param, [this](float v) {
          setToggleState(v > 0.5f, juce::dontSendNotification);
          repaint();
      })
{
    getProperties().set(PanelLookAndFeel::kLed, true);
    onClick = [this] {
        if (wantsDefault(juce::ModifierKeys::currentModifiers))
            attachment.setValueAsCompleteGesture(defaultOf(param));
        else
            attachment.setValueAsCompleteGesture(getToggleState() ? 0.0f : 1.0f);
    };
    attachment.sendInitialUpdate();
}

// --- SlideSwitch ---

SlideSwitch::SlideSwitch(juce::AudioProcessorValueTreeState& state, const char* paramId, const juce::String& text,
                         const juce::StringArray& names)
    : param(parameter(state, paramId)),
      attachment(param, [this](float v) {
          selected = juce::roundToInt(v);
          repaint();
      }),
      label(text), positions(names)
{
    attachment.sendInitialUpdate();
}

float SlideSwitch::legendWidth() const
{
    return juce::GlyphArrangement::getStringWidth(legendFont(13.0f), label) + 10.0f;
}

int SlideSwitch::idealWidth() const
{
    return (int) (legendWidth() + 46.0f * (float) positions.size());
}

juce::Rectangle<float> SlideSwitch::slot() const
{
    return { legendWidth() + 10.0f, 7.0f, 46.0f * (float) positions.size() - 20.0f, 9.0f };
}

void SlideSwitch::paint(juce::Graphics& g)
{
    const auto s = slot();
    drawLegend(g, label, { 0.0f, 0.0f, legendWidth() - 6.0f, (float) getHeight() }, true, juce::Justification::centredRight);

    g.setColour(juce::Colours::black);
    g.fillRoundedRectangle(s, 2.0f);
    g.setColour(juce::Colours::white.withAlpha(0.05f));
    g.drawRoundedRectangle(s.expanded(0.5f), 2.5f, 1.0f);
    const int n = positions.size();
    auto xOf = [&](int i) { return s.getX() + 6.0f + (s.getWidth() - 12.0f) * (n > 1 ? (float) i / (float) (n - 1) : 0.5f); };
    // The thumb, and the positions printed under the slot.
    const auto thumb = juce::Rectangle<float>(14.0f, 13.0f).withCentre({ xOf(selected), s.getCentreY() });
    g.setGradientFill(juce::ColourGradient(juce::Colour(0xffd8d8d4), thumb.getX(), thumb.getY(), juce::Colour(0xff6e6e6a),
                                           thumb.getX(), thumb.getBottom(), false));
    g.fillRoundedRectangle(thumb, 1.5f);
    g.setColour(juce::Colours::black.withAlpha(0.5f));
    g.drawVerticalLine((int) thumb.getCentreX(), thumb.getY() + 2.0f, thumb.getBottom() - 2.0f);
    for (int i = 0; i < n; ++i)
    {
        g.setFont(legendFont(11.5f, i == selected));
        g.setColour(i == selected ? colours::silkscreen : colours::silkscreenDim);
        g.drawText(positions[i], juce::Rectangle<float>(46.0f, 13.0f).withCentre({ xOf(i), s.getBottom() + 10.0f }),
                   juce::Justification::centred);
    }
}

void SlideSwitch::mouseDown(const juce::MouseEvent& e)
{
    if (wantsDefault(e.mods))
    {
        attachment.setValueAsCompleteGesture(defaultOf(param));
        return;
    }
    const auto s = slot();
    const int n = positions.size();
    if (e.position.x < s.getX() - 10.0f)
    {
        attachment.setValueAsCompleteGesture((float) ((selected + 1) % n)); // click the legend: next position
        return;
    }
    const float t = (e.position.x - s.getX()) / juce::jmax(1.0f, s.getWidth());
    attachment.setValueAsCompleteGesture((float) juce::jlimit(0, n - 1, juce::roundToInt(t * (float) (n - 1))));
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
    const auto r = getLocalBounds().toFloat();
    drawGlass(g, r);
    auto textArea = r.reduced(10.0f, 0.0f);
    g.setColour(colours::display.withAlpha(isEnabled() ? 0.85f : 0.3f));
    if (arrows)
    {
        for (int side : { -1, 1 })
        {
            const float x = side < 0 ? r.getX() + 9.0f : r.getRight() - 9.0f;
            juce::Path t;
            t.addTriangle(x - side * 3.0f, r.getCentreY() - 4.5f, x - side * 3.0f, r.getCentreY() + 4.5f, x + side * 3.5f, r.getCentreY());
            g.fillPath(t);
        }
        textArea = r.reduced(22.0f, 0.0f);
    }
    // The dots shrink for a long name, down to a limit; past it the name is cut, as on a character display.
    auto text = shown.toUpperCase();
    const float fullPitch = large ? kLargeTextPitch : kSmallTextPitch;
    const float pitch = juce::jlimit(juce::jmin(1.4f, fullPitch), fullPitch,
                                     (textArea.getWidth() + 1.0f) / juce::jmax(1.0f, 6.0f * (float) text.length()));
    const int fits = (int) ((textArea.getWidth() + pitch) / (6.0f * pitch));
    if (text.length() > fits)
        text = text.substring(0, juce::jmax(0, fits));
    drawMatrixText(g, text, textArea, pitch, colours::display.withAlpha(isEnabled() ? 1.0f : 0.35f),
                   arrows ? juce::Justification::centred : juce::Justification::centredLeft);
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

} // namespace glide90::ui
