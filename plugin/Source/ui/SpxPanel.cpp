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

SpxPanel::SpxPanel(Glide90Processor& p) : proc(p), bypassKey(p.state(), ids::bypass, "BYPASS")
{
    for (auto* c : std::initializer_list<juce::Component*> { &lcd, &memoryLed, &meter, &parameterKey, &balanceKey,
                                                             &incKey, &decKey, &storeKey, &recallKey, &memUpKey,
                                                             &memDownKey, &bypassKey })
        addAndMakeVisible(c);

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
    const auto r = getLocalBounds().toFloat();
    g.setGradientFill(juce::ColourGradient(colours::stripTop, 0.0f, 0.0f, colours::stripBottom, 0.0f, r.getBottom(), false));
    g.fillRect(r);
    // Brushed texture.
    g.setColour(juce::Colours::white.withAlpha(0.015f));
    for (int y = 2; y < getHeight(); y += 3)
        g.drawHorizontalLine(y, 24.0f, r.getRight() - 24.0f);

    // Rack ears with their slots.
    for (float x : { 0.0f, r.getRight() - 24.0f })
    {
        g.setColour(juce::Colour(0xff161617));
        g.fillRect(x, 0.0f, 24.0f, r.getHeight());
        g.setColour(juce::Colour(0xff050505));
        for (float y : { 14.0f, r.getBottom() - 34.0f })
            g.fillRoundedRectangle(x + 7.0f, y, 10.0f, 20.0f, 5.0f);
    }

    g.setColour(colours::legend);
    g.setFont(panelFont(20.0f, true));
    g.drawText("Glide90", 40, 26, 120, 24, juce::Justification::centredLeft);
    g.setColour(colours::legendDim);
    g.setFont(panelFont(9.5f, true));
    g.drawText("SPX90-STYLE", 40, 50, 120, 12, juce::Justification::centredLeft);
    g.drawText("DIGITAL MULTI-EFFECT", 40, 62, 140, 12, juce::Justification::centredLeft);

    auto legend = [&g](const juce::String& text, juce::Rectangle<int> below) {
        g.setFont(panelFont(9.0f, true));
        g.setColour(colours::legendDim);
        g.drawText(text, below.withY(below.getBottom() + 3).withHeight(12).expanded(30, 0), juce::Justification::centred);
    };
    legend("MEMORY", memoryLed.getBounds());
    legend("PARAMETER", incKey.getBounds().getUnion(decKey.getBounds()));
    legend("MEMORY", memUpKey.getBounds().getUnion(memDownKey.getBounds()));
}

void SpxPanel::resized()
{
    meter.setBounds(158, 18, 40, 74);
    memoryLed.setBounds(206, 26, 60, 50);
    lcd.setBounds(274, 18, 280, 70);

    // Keys as on the unit: PARAMETER, data up/down, BALANCE | STORE, memory up/down, RECALL | BYPASS.
    const int top = 22, keyH = 34, w = 54, arrowW = 32;
    int x = 566;
    auto key = [&](juce::Button& b, int width) {
        b.setBounds(x, top + 18, width, keyH);
        x += width + 4;
    };
    auto arrows = [&](juce::Button& up, juce::Button& down) {
        up.setBounds(x, top, arrowW, 33);
        down.setBounds(x, top + 37, arrowW, 33);
        x += arrowW + 4;
    };
    key(parameterKey, w);
    arrows(incKey, decKey);
    key(balanceKey, w);
    x += 6;
    key(storeKey, w);
    arrows(memUpKey, memDownKey);
    key(recallKey, w);
    x += 8;
    key(bypassKey, 66);
    jassert(bypassKey.getRight() <= getWidth() - 24 - 8);
}

} // namespace glide90::ui
