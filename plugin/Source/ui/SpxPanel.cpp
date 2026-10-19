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
const juce::Identifier kLed { "led" };

} // namespace

SpxPanel::SpxPanel(Glide90Processor& p)
    : proc(p), inputAttachment(p.state(), ids::input, inputKnob)
{
    for (auto* c : std::initializer_list<juce::Component*> { &lcd, &memoryLed, &meter, &inputKnob, &parameterKey,
                                                             &balanceKey, &decKey, &incKey, &memDownKey, &memUpKey,
                                                             &storeKey, &recallKey, &bypassKey, &inSwitch, &outSwitch })
        addAndMakeVisible(c);

    inputKnob.setTooltip("INPUT: unity at 8, +12 dB at 10. Not stored with memories.");
    lcd.setTooltip("Mouse wheel or drag: change the value shown");
    lcd.onStep = [this](int dir) { step(dir); };

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

    auto attachToggle = [this](juce::TextButton& key, const char* id, std::unique_ptr<juce::ParameterAttachment>& att) {
        key.getProperties().set(kLed, true);
        att = std::make_unique<juce::ParameterAttachment>(*proc.state().getParameter(id), [&key](float v) {
            key.setToggleState(v > 0.5f, juce::dontSendNotification);
            key.repaint();
        });
        key.onClick = [&key, a = att.get()] { a->setValueAsCompleteGesture(key.getToggleState() ? 0.0f : 1.0f); };
        att->sendInitialUpdate();
    };
    attachToggle(bypassKey, ids::bypass, bypassAttachment);
    attachToggle(inSwitch, ids::inputSwitch, inSwitchAttachment);
    attachToggle(outSwitch, ids::outputSwitch, outSwitchAttachment);
    bypassKey.setTooltip("Analog bypass: full bandwidth, BALANCE and OUT LVL bypassed");
    inSwitch.setTooltip("Rear-panel input level switch: -20 dBm adds 24 dB before the A/D");
    outSwitch.setTooltip("Rear-panel output level switch: -20 dBm lowers the output by 24 dB");

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
        // Recalled from elsewhere (host automation, preset): follow it.
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
    g.setGradientFill(juce::ColourGradient(colours::panelTop, 0.0f, 0.0f, colours::panelBottom, 0.0f, r.getBottom(), false));
    g.fillRect(r);

    // Rack ears with their slots.
    for (float x : { 0.0f, r.getRight() - 24.0f })
    {
        g.setColour(juce::Colour(0xff1a1b1d));
        g.fillRect(x, 0.0f, 24.0f, r.getHeight());
        g.setColour(juce::Colour(0xff060606));
        for (float y : { 22.0f, r.getBottom() - 46.0f })
            g.fillRoundedRectangle(x + 7.0f, y, 10.0f, 24.0f, 5.0f);
    }
    g.setColour(juce::Colours::white.withAlpha(0.06f));
    g.drawHorizontalLine(1, 24.0f, r.getRight() - 24.0f);

    g.setColour(colours::legend);
    g.setFont(panelFont(22.0f, true));
    g.drawText("Glide90", 40, 10, 140, 26, juce::Justification::centredLeft);
    g.setColour(colours::legendDim);
    g.setFont(panelFont(11.0f));
    g.drawText("SPX90-STYLE DIGITAL MULTI-EFFECT PROCESSOR", 142, 16, 320, 16, juce::Justification::centredLeft);

    auto legend = [&g](const juce::String& text, juce::Rectangle<int> above) {
        g.setFont(panelFont(10.5f, true));
        g.setColour(colours::legend);
        g.drawText(text, above.withHeight(14).translated(0, -16), juce::Justification::centred);
    };
    legend("INPUT", inputKnob.getBounds().withY(inputKnob.getBottom() + 18));
    legend("LEVEL", meter.getBounds().withTrimmedTop(-2).translated(0, -12));
    legend("IN LEVEL", inSwitch.getBounds());
    legend("OUT LEVEL", outSwitch.getBounds());
    legend("MEMORY", memoryLed.getBounds());
    legend("DATA", decKey.getBounds().getUnion(incKey.getBounds()));
    legend("MEMORY", memDownKey.getBounds().getUnion(memUpKey.getBounds()));
    legend("PROGRAM", parameterKey.getBounds().getUnion(balanceKey.getBounds()));
    legend("MEMORY", storeKey.getBounds().getUnion(recallKey.getBounds()));
}

void SpxPanel::resized()
{
    inputKnob.setBounds(44, 50, 88, 88);
    meter.setBounds(146, 62, 154, 38);
    inSwitch.setBounds(148, 136, 72, 26);
    outSwitch.setBounds(228, 136, 72, 26);
    memoryLed.setBounds(316, 56, 80, 68);
    lcd.setBounds(410, 46, 288, 86);
    bypassKey.setBounds(410, 146, 96, 30);

    // Keys right-aligned, clear of the LCD.
    constexpr int pitch = 62, keyWidth = 58;
    const int x0 = getWidth() - 24 - 16 - 4 * pitch;
    jassert(x0 > lcd.getRight() + 8);
    auto key = [x0](int col, int row) { return juce::Rectangle<int>(x0 + col * pitch + 2, 60 + row * 72, keyWidth, 32); };
    parameterKey.setBounds(key(0, 0));
    balanceKey.setBounds(key(1, 0));
    decKey.setBounds(key(2, 0));
    incKey.setBounds(key(3, 0));
    memDownKey.setBounds(key(0, 1));
    memUpKey.setBounds(key(1, 1));
    storeKey.setBounds(key(2, 1));
    recallKey.setBounds(key(3, 1));
}

} // namespace glide90::ui
