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
    : proc(p), inputAttachment(p.state(), ids::input, inputKnob), bypassKey(p.state(), ids::bypass, "BYPASS")
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
    inputKnob.setTooltip("INPUT: unity at 8, +12 dB at 10. Not stored with memories.");
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
        proc.recallMemory(pendingMemory);
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

    auto legend = [&g](const juce::String& text, juce::Rectangle<int> area) {
        g.setFont(panelFont(10.0f));
        g.setColour(colours::legend);
        g.drawText(text, area, juce::Justification::centred);
    };
    legend("INPUT", inputKnob.getBounds().withHeight(12).translated(0, -14));
    legend("0", { inputKnob.getX() - 2, inputKnob.getBottom() - 4, 12, 12 });
    legend("10", { inputKnob.getRight() - 12, inputKnob.getBottom() - 4, 14, 12 });

    // One smoked window over the logo, meter, memory number and LCD, as on the unit.
    const auto window = juce::Rectangle<int>(meter.getX() - 104, lcd.getY() - 6, lcd.getRight() + 8 - (meter.getX() - 104),
                                             lcd.getHeight() + 12)
                            .toFloat();
    drawGlass(g, window, 2.0f);
    g.setColour(colours::legend);
    g.setFont(panelFont(19.0f, true));
    g.drawText("Glide90", window.withWidth(96.0f).withTrimmedBottom(window.getHeight() * 0.42f).toNearestInt(),
               juce::Justification::centredBottom);
    g.setColour(colours::vfd.withAlpha(0.75f));
    g.setFont(panelFont(12.5f, true));
    g.drawText("SPX90-STYLE", window.withWidth(96.0f).withTrimmedTop(window.getHeight() * 0.58f).toNearestInt(),
               juce::Justification::centredTop);
    g.setColour(colours::legendDim);
    g.setFont(panelFont(9.0f));
    g.drawText("MEMORY", memoryLed.getBounds().withY(memoryLed.getBottom()).withHeight(12), juce::Justification::centred);
}

void SpxPanel::resized()
{
    inputKnob.setBounds(40, 34, 50, 50);
    meter.setBounds(212, 22, 34, 68);
    memoryLed.setBounds(252, 26, 58, 46);
    lcd.setBounds(316, 20, 236, 70);

    // Keys as on the unit: PARAMETER, data up/down, BALANCE | STORE, memory up/down, RECALL | BYPASS.
    const int keyTop = 33, keyH = 44, w = 52, arrowW = 34;
    int x = 576;
    auto key = [&](juce::Button& b) {
        b.setBounds(x, keyTop, w, keyH);
        x += w + 4;
    };
    auto arrows = [&](juce::Button& up, juce::Button& down) {
        up.setBounds(x, 14, arrowW, 40);
        down.setBounds(x, 56, arrowW, 40);
        x += arrowW + 4;
    };
    key(parameterKey);
    arrows(incKey, decKey);
    key(balanceKey);
    x += 8;
    key(storeKey);
    arrows(memUpKey, memDownKey);
    key(recallKey);
    x += 12;
    key(bypassKey);
    jassert(bypassKey.getRight() <= getWidth() - PanelLookAndFeel::kEar);
}

} // namespace glide90::ui
