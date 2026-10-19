#include "SpxPanel.h"

#include "PanelLookAndFeel.h"
#include "Parameters.h"
#include "PluginProcessor.h"
#include "RomPrograms.h"

namespace glide90::ui
{
namespace
{

constexpr int kMessageMs = 1500;

} // namespace

SpxPanel::SpxPanel(Glide90Processor& p)
    : proc(p), inputKnob(p.state(), ids::input, "INPUT"), bypassKey(p.state(), ids::bypass, "BYPASS")
{
    for (auto* c : std::initializer_list<juce::Component*> { &lcd, &memoryLed, &meter, &inputKnob, &parameterKey, &balanceKey,
                                                             &incKey, &decKey, &storeKey, &recallKey, &memUpKey,
                                                             &memDownKey, &bypassKey })
        addAndMakeVisible(c);

    lcd.setFramed(false);
    memoryLed.setFramed(false);
    for (auto* key : { &parameterKey, &balanceKey })
        key->getProperties().set(PanelLookAndFeel::kLed, true);
    for (auto* key : { &incKey, &memUpKey })
        key->getProperties().set(PanelLookAndFeel::kArrow, 1);
    for (auto* key : { &decKey, &memDownKey })
        key->getProperties().set(PanelLookAndFeel::kArrow, -1);
    inputKnob.setTooltip("Input level before the A/D: unity at 8, +12 dB at 10. Keep the meter out of the red");
    inputKnob.onTouch = [this](const juce::RangedAudioParameter& param) { showParameter(param); };
    lcd.setTooltip("Mouse wheel or drag: change the value shown");
    lcd.onStep = [this](int dir) { step(dir); };
    bypassKey.setTooltip("SPX bypass (analog): full bandwidth, the effect and BALANCE / OUT LVL bypassed");

    parameterKey.onClick = [this] {
        message.clear();
        if (balanceMode)
        {
            balanceMode = false;
            pageIndex = 0;
        }
        else
            ++pageIndex;
        refresh();
    };
    balanceKey.onClick = [this] {
        message.clear();
        pageIndex = balanceMode ? pageIndex + 1 : 0;
        balanceMode = true;
        refresh();
    };

    for (auto* key : { &decKey, &incKey })
    {
        key->setRepeatSpeed(400, 60);
        key->onStateChange = [this] {
            if (!decKey.isDown() && !incKey.isDown())
                keyDownSince = 0;
            else if (keyDownSince == 0)
                keyDownSince = juce::Time::getMillisecondCounter();
        };
    }
    decKey.onClick = [this] { step(-1); };
    incKey.onClick = [this] { step(1); };

    memUpKey.setRepeatSpeed(400, 90);
    memDownKey.setRepeatSpeed(400, 90);
    memUpKey.onClick = [this] {
        pendingMemory = pendingMemory % MemoryBank::kCount + 1;
        refresh();
    };
    memDownKey.onClick = [this] {
        pendingMemory = (pendingMemory + MemoryBank::kCount - 2) % MemoryBank::kCount + 1;
        refresh();
    };
    recallKey.onClick = [this] {
        if (!proc.recallMemory(pendingMemory))
        {
            showMessage("NOT EMULATED YET");
            return;
        }
        lastLoaded = pendingMemory;
        balanceMode = false;
        pageIndex = 0;
        message.clear();
        refresh();
    };
    storeKey.onClick = [this] {
        if (pendingMemory < MemoryBank::kFirstUser)
        {
            showMessage("#1~#30 READ ONLY");
            return;
        }
        const auto loaded = proc.memories().get(proc.loadedMemory());
        const auto title = loaded.used ? loaded.title : MemoryBank::rom(proc.algorithm()).title;
        if (proc.storeMemory(pendingMemory, title))
        {
            lastLoaded = pendingMemory;
            showMessage("STORE COMPLETE");
        }
    };

    refresh();
    startTimerHz(30);
}

SpxPanel::~SpxPanel()
{
    stopTimer();
}

juce::String SpxPanel::titleOf(int number) const
{
    const auto m = proc.memories().get(number);
    return m.used ? m.title : juce::String("*** EMPTY ***");
}

juce::String SpxPanel::currentPage() const
{
    const auto pages = balanceMode ? balancePages() : parameterPages(proc.algorithm());
    if (pages.empty())
        return {};
    return pages[(size_t) pageIndex % pages.size()];
}

void SpxPanel::step(int direction)
{
    const auto id = currentPage();
    if (id.isEmpty())
        return;
    message.clear();
    auto* p = proc.state().getParameter(id);
    const auto& range = p->getNormalisableRange();
    const float interval = range.interval > 0.0f ? range.interval : 0.01f * (range.end - range.start);

    // Holding a key speeds up, as on the unit (DELAY has 4000 steps).
    float multiple = 1.0f;
    if (keyDownSince != 0)
    {
        const auto held = juce::Time::getMillisecondCounter() - keyDownSince;
        multiple = held > 3000 ? 100.0f : (held > 1200 ? 10.0f : 1.0f);
    }
    const float plain = range.convertFrom0to1(p->getValue());
    const float next = range.snapToLegalValue(juce::jlimit(range.start, range.end, plain + (float) direction * interval * multiple));
    p->beginChangeGesture();
    p->setValueNotifyingHost(range.convertTo0to1(next));
    p->endChangeGesture();
    refresh();
}

void SpxPanel::showParameter(const juce::RangedAudioParameter& param)
{
    showMessage(lcdText(param).substring(0, LcdDisplay::kColumns));
}

void SpxPanel::showMessage(const juce::String& text)
{
    message = text;
    messageUntil = juce::Time::getMillisecondCounter() + kMessageMs;
    refresh();
}

void SpxPanel::timerCallback()
{
    const float peak = proc.takeInputPeak();
    const float db = peak > 0.0f ? 20.0f * std::log10(peak) : -100.0f;
    meterDb = juce::jmax(db, meterDb - 1.2f); // about 36 dB/s fall-back
    meter.setLevelDb(meterDb);
    refresh();
}

void SpxPanel::refresh()
{
    const int loaded = proc.loadedMemory();
    if (loaded != lastLoaded)
    {
        // Recalled from elsewhere (knobs, host automation, preset): follow it.
        lastLoaded = loaded;
        pendingMemory = loaded;
    }
    if (message.isNotEmpty() && juce::Time::getMillisecondCounter() > messageUntil)
        message.clear();

    memoryLed.setValue(pendingMemory, pendingMemory != loaded);
    parameterKey.setToggleState(!balanceMode, juce::dontSendNotification);
    balanceKey.setToggleState(balanceMode, juce::dontSendNotification);

    juce::String bottom = message;
    if (bottom.isEmpty())
    {
        const auto id = currentPage();
        bottom = id.isEmpty() ? juce::String("NOT EMULATED YET") : lcdText(*proc.state().getParameter(id));
    }
    lcd.setText(titleOf(pendingMemory), bottom);
}

void SpxPanel::paint(juce::Graphics& g)
{
    drawRackUnit(g, getLocalBounds());

    // One smoked window over the name plate, meter, memory number and LCD, as on the unit.
    const auto window = juce::Rectangle<float>(130.0f, 14.0f, (float) lcd.getRight() + 10.0f - 130.0f, (float) getHeight() - 28.0f);
    drawGlass(g, window, 1.5f);
    // The model plate (the plugin's own name is in the header).
    const auto plate = window.withWidth(84.0f).translated(10.0f, 0.0f);
    g.setFont(legendFont(24.0f, true));
    g.setColour(colours::display.withAlpha(0.85f));
    g.drawText("SPX90", plate.withTrimmedBottom(plate.getHeight() * 0.45f), juce::Justification::centredBottom);
    g.setFont(legendFont(12.0f).withExtraKerningFactor(0.15f));
    g.setColour(colours::silkscreenDim);
    g.drawText("STYLE", plate.withTrimmedTop(plate.getHeight() * 0.58f), juce::Justification::centredTop);
    g.setFont(legendFont(11.0f));
    g.setColour(colours::silkscreenDim);
    g.drawText("MEMORY", memoryLed.getBounds().withY(memoryLed.getBottom() - 2).withHeight(13), juce::Justification::centredRight);

    // Legends under the arrow pairs, as printed on the panel.
    auto legend = [&g](const juce::String& text, juce::Rectangle<int> area) {
        g.setFont(legendFont(11.0f));
        g.setColour(colours::silkscreenDim);
        g.drawText(text, area.withY(area.getBottom() + 2).withHeight(13).expanded(24, 0), juce::Justification::centred);
    };
    legend("DATA", incKey.getBounds().getUnion(decKey.getBounds()));
    legend("MEMORY", memUpKey.getBounds().getUnion(memDownKey.getBounds()));
}

void SpxPanel::resized()
{
    const int m = PanelLookAndFeel::kMargin;
    inputKnob.setBounds(m - 4, (getHeight() - Knob::kHeight) / 2, Knob::widthFor(Knob::Size::Small), Knob::kHeight);
    meter.setBounds(232, 22, 30, getHeight() - 44);
    memoryLed.setBounds(268, 26, 64, 44);
    lcd.setBounds(340, 16, 230, getHeight() - 32);

    // Keys as on the unit: PARAMETER, data up/down, BALANCE | STORE, memory up/down, RECALL | BYPASS.
    const int keyH = 40, keyTop = (getHeight() - keyH) / 2 - 4, w = 52, arrowW = 32;
    int x = 596;
    auto key = [&](juce::Button& b) {
        b.setBounds(x, keyTop, w, keyH);
        x += w + 4;
    };
    auto arrows = [&](juce::Button& up, juce::Button& down) {
        up.setBounds(x, keyTop - 20, arrowW, 38);
        down.setBounds(x, keyTop + 22, arrowW, 38);
        x += arrowW + 4;
    };
    key(parameterKey);
    arrows(incKey, decKey);
    key(balanceKey);
    x += 8;
    key(storeKey);
    arrows(memUpKey, memDownKey);
    key(recallKey);
    x += 10;
    key(bypassKey);
    jassert(bypassKey.getRight() <= getWidth() - PanelLookAndFeel::kMargin);
}

} // namespace glide90::ui
