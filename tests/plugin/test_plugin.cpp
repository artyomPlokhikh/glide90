// Processor-level tests: memories, state, latency and host bypass of the Glide90 plugin.
#define DOCTEST_CONFIG_IMPLEMENT
#include <doctest/doctest.h>

#include "PluginProcessor.h"
#include "ui/Controls.h"

#include <juce_events/juce_events.h>
#include <juce_gui_basics/juce_gui_basics.h>

using glide90::Glide90Processor;
namespace ids = glide90::ids;

namespace
{

constexpr double kRate = 48000.0;
constexpr int kBlock = 256;

float plain(Glide90Processor& p, const char* id)
{
    return p.state().getRawParameterValue(id)->load();
}

void set(Glide90Processor& p, const char* id, float value)
{
    auto* param = p.state().getParameter(id);
    param->setValueNotifyingHost(param->convertTo0to1(value));
}

std::unique_ptr<Glide90Processor> make()
{
    auto p = std::make_unique<Glide90Processor>();
    p->setPlayConfigDetails(2, 2, kRate, kBlock);
    p->prepareToPlay(kRate, kBlock);
    return p;
}

juce::MemoryBlock saveState(Glide90Processor& p)
{
    juce::MemoryBlock block;
    p.getStateInformation(block);
    return block;
}

// Runs an impulse through processBlock and returns the index of the output peak.
int impulsePeak(Glide90Processor& p, bool hostBypass)
{
    juce::AudioBuffer<float> buffer(2, kBlock);
    juce::MidiBuffer midi;
    int peakAt = -1;
    float peak = 0.0f;
    for (int start = 0; start < 16 * kBlock; start += kBlock)
    {
        buffer.clear();
        if (start == 0)
            buffer.setSample(0, 0, 0.5f), buffer.setSample(1, 0, 0.5f);
        if (hostBypass)
            p.processBlockBypassed(buffer, midi);
        else
            p.processBlock(buffer, midi);
        for (int i = 0; i < kBlock; ++i)
            if (std::abs(buffer.getSample(0, i)) > peak)
            {
                peak = std::abs(buffer.getSample(0, i));
                peakAt = start + i;
            }
    }
    return peakAt;
}

} // namespace

TEST_CASE("changing the memory number recalls the memory, like RECALL")
{
    auto p = make();
    CHECK(p->algorithm() == 17);
    set(*p, ids::memory, 5.0f);
    p->flushPendingRecall();
    CHECK(p->loadedMemory() == 5);
    CHECK(p->algorithm() == 5);
    CHECK(plain(*p, ids::erType) == doctest::Approx((float) spx::ErType::Hall));
    CHECK(plain(*p, ids::erRoomSize) == doctest::Approx(2.0f));
    CHECK(plain(*p, ids::erDelay) == doctest::Approx(10.0f));

    set(*p, ids::memory, 31.0f); // pre-filled user memory REV GATE 20/4
    p->flushPendingRecall();
    CHECK(p->algorithm() == 17);
    CHECK(plain(*p, ids::erRoomSize) == doctest::Approx(20.0f));
    CHECK(plain(*p, ids::erLiveness) == doctest::Approx(4.0f));
}

TEST_CASE("STORE writes user memories only, and an empty memory recalls nothing")
{
    auto p = make();
    set(*p, ids::erRoomSize, 12.3f);
    CHECK_FALSE(p->storeMemory(20, "MINE"));
    CHECK(p->storeMemory(45, "MINE"));
    CHECK(p->loadedMemory() == 45);

    p->recallMemory(5);
    CHECK(plain(*p, ids::erRoomSize) == doctest::Approx(2.0f));
    p->recallMemory(50); // empty
    CHECK(plain(*p, ids::erRoomSize) == doctest::Approx(2.0f));
    CHECK(p->algorithm() == 5);
    p->recallMemory(45);
    CHECK(plain(*p, ids::erRoomSize) == doctest::Approx(12.3f));
    CHECK(p->memories().get(45).title == "MINE");
}

TEST_CASE("state round trip keeps edits, user memories and the algorithm")
{
    auto a = make();
    a->recallMemory(16);
    set(*a, ids::erRoomSize, 7.7f); // edited, not stored
    a->storeMemory(60, "SIXTY");
    set(*a, ids::erLiveness, 2.0f); // edited after storing
    const auto state = saveState(*a);

    auto b = make();
    b->setStateInformation(state.getData(), (int) state.getSize());
    b->flushPendingRecall(); // must not recall memory 60 over the restored edit
    CHECK(b->loadedMemory() == 60);
    CHECK(b->algorithm() == 16);
    CHECK(plain(*b, ids::erRoomSize) == doctest::Approx(7.7f));
    CHECK(plain(*b, ids::erLiveness) == doctest::Approx(2.0f));
    CHECK(b->memories().get(60).title == "SIXTY");
    CHECK(b->memories().get(60).er.liveness == 5);
    CHECK(b->memories().get(31).used); // factory user memories survive
}

TEST_CASE("the reported latency is where the direct signal comes out, with or without host bypass")
{
    auto p = make();
    set(*p, ids::balance, 0.0f);
    set(*p, ids::glideDepth, 0.0f);
    for (bool glideOn : { false, true })
    {
        CAPTURE(glideOn);
        set(*p, ids::glideOn, glideOn ? 1.0f : 0.0f);
        p->reset();
        p->prepareToPlay(kRate, kBlock);
        CHECK(impulsePeak(*p, false) == p->getLatencySamples());
    }
    CHECK(impulsePeak(*p, true) == p->getLatencySamples());
}

TEST_CASE("factory presets recall their memory and set the glide")
{
    auto p = make();
    for (int i = 0; i < p->getNumPrograms(); ++i)
    {
        CAPTURE(p->getProgramName(i));
        p->setCurrentProgram(i);
        CHECK(p->getCurrentProgram() == i);
        CHECK(p->currentPresetName() == p->getProgramName(i));
        CHECK(p->loadedMemory() == juce::roundToInt(plain(*p, ids::memory)));
    }
    p->setCurrentProgram(3); // a dive on each pick
    CHECK(plain(*p, ids::erRoomSize) == doctest::Approx(20.0f));
    CHECK(plain(*p, ids::erLiveness) == doctest::Approx(10.0f));
    CHECK(plain(*p, ids::glideOn) > 0.5f);
    CHECK(juce::roundToInt(plain(*p, ids::glideMode)) == (int) sway::GlideMode::Trigger);

    // A preset leaves the rig's input gain alone and resets what it does not set.
    set(*p, ids::input, 6.0f);
    set(*p, ids::glideTwinLevel, 0.9f);
    p->setCurrentProgram(1);
    CHECK(plain(*p, ids::input) == doctest::Approx(6.0f));
    CHECK(plain(*p, ids::glideTwinLevel) == doctest::Approx(0.0f));
}

TEST_CASE("factory presets bring their own reverb settings and load unmodified")
{
    auto p = make();
    // Overwrite the user memories the presets might have relied on.
    for (int n = glide90::MemoryBank::kFirstUser; n < glide90::MemoryBank::kFirstUser + 8; ++n)
        p->storeMemory(n, "OVERWRITTEN");
    for (int i = 0; i < p->getNumPrograms(); ++i)
    {
        CAPTURE(p->getProgramName(i));
        p->setCurrentProgram(i);
        p->flushPendingRecall();
        CHECK_FALSE(p->isPresetModified());
        if (p->getProgramName(i).startsWith("MBV Loveless"))
            CHECK(plain(*p, ids::erRoomSize) == doctest::Approx(20.0f));
    }
}

TEST_CASE("memories added in a later version are filled when an older state is restored")
{
    glide90::MemoryBank bank;
    juce::ValueTree older(glide90::MemoryBank::treeType()); // a state saved before memory 34 existed
    bank.fromTree(older);
    CHECK(bank.get(34).used);
}

TEST_CASE("a drawn glide shape is selected and survives a state round trip")
{
    auto a = make();
    const sway::ShapePoint points[] { { 0.0f, 0.0f }, { 0.3f, 0.9f }, { 0.6f, 0.2f } };
    sway::Shape shape;
    shape.setPoints(points, 3);
    a->setCustomShape(shape);
    CHECK(juce::roundToInt(plain(*a, ids::glideShape)) == (int) sway::kShapePresetNames.size());
    const auto state = saveState(*a);

    auto b = make();
    b->setStateInformation(state.getData(), (int) state.getSize());
    const auto restored = b->currentShape();
    REQUIRE(restored.size() == 3);
    CHECK(restored.point(1).x == doctest::Approx(0.3f));
    CHECK(restored.point(1).y == doctest::Approx(0.9f));
}

TEST_CASE("parameters missing from a saved state come back at their defaults")
{
    auto a = make();
    juce::MemoryBlock block;
    a->getStateInformation(block);
    auto xml = juce::AudioProcessor::getXmlFromBinary(block.getData(), (int) block.getSize());
    REQUIRE(xml != nullptr);
    // An older state: no version, no glide parameters.
    xml->removeAttribute("version");
    for (auto* child = xml->getFirstChildElement(); child != nullptr;)
    {
        auto* nextChild = child->getNextElement();
        if (child->getStringAttribute("id").startsWith("glide_"))
            xml->removeChildElement(child, true);
        child = nextChild;
    }
    juce::MemoryBlock old;
    juce::AudioProcessor::copyXmlToBinary(*xml, old);

    auto b = make();
    set(*b, ids::glideDepth, 0.0f);
    set(*b, ids::glideMix, 0.0f);
    b->setStateInformation(old.getData(), (int) old.getSize());
    CHECK(plain(*b, ids::glideDepth) == doctest::Approx(30.0f));
    CHECK(plain(*b, ids::glideMix) == doctest::Approx(1.0f));
    CHECK(plain(*b, ids::glideOn) > 0.5f);
}

TEST_CASE("the editor builds and lays out; GLIDE90_SNAPSHOT=<file.png> saves a picture of it")
{
    auto p = make();
    std::unique_ptr<juce::AudioProcessorEditor> editor(p->createEditor());
    REQUIRE(editor != nullptr);
    CHECK(editor->getWidth() > 0);
    if (const auto path = juce::SystemStats::getEnvironmentVariable("GLIDE90_SNAPSHOT", {}); path.isNotEmpty())
    {
        juce::MessageManager::getInstance()->runDispatchLoopUntil(300); // let the timers fill the displays
        const auto image = editor->createComponentSnapshot(editor->getLocalBounds(), true, 2.0f);
        juce::File file(path);
        file.deleteFile();
        juce::FileOutputStream out(file);
        REQUIRE(juce::PNGImageFormat().writeImageToStream(image, out));

        // A sample of the popup menu look next to it, drawn by the same look-and-feel calls.
        juce::Image menuImage(juce::Image::ARGB, 300, 190, true);
        juce::Graphics g(menuImage);
        auto& lf = editor->getLookAndFeel();
        lf.drawPopupMenuBackground(g, 300, 190);
        lf.drawPopupMenuSectionHeader(g, { 0, 4, 300, 22 }, "Factory");
        const char* items[] { "Init Reverse Gate+Glide", "MBV Loveless roadie", "MBV To Here Knows When" };
        for (int i = 0; i < 3; ++i)
            lf.drawPopupMenuItem(g, { 0, 28 + i * 24, 300, 24 }, false, true, i == 2, i == 1, false, items[i], {}, nullptr, nullptr);
        lf.drawPopupMenuItem(g, { 0, 100, 300, 9 }, true, true, false, false, false, {}, {}, nullptr, nullptr);
        lf.drawPopupMenuSectionHeader(g, { 0, 112, 300, 22 }, "User");
        lf.drawPopupMenuItem(g, { 0, 136, 300, 24 }, false, false, false, false, false, "Empty", {}, nullptr, nullptr);
        juce::File menuFile(file.getSiblingFile(file.getFileNameWithoutExtension() + "-menu.png"));
        menuFile.deleteFile();
        juce::FileOutputStream menuOut(menuFile);
        REQUIRE(juce::PNGImageFormat().writeImageToStream(menuImage, menuOut));
    }
}

TEST_CASE("a knob's value field: a click elsewhere applies it; Alt-click resets the knob")
{
    auto p = make();
    std::unique_ptr<juce::AudioProcessorEditor> editor(p->createEditor());
    editor->setVisible(true);
    editor->addToDesktop(0);
    auto* peer = editor->getPeer();
    REQUIRE(peer != nullptr);

    // The reverb's MIX knob (BALANCE).
    glide90::ui::Knob* knob = nullptr;
    std::function<void(juce::Component&)> find = [&](juce::Component& c) {
        if (auto* k = dynamic_cast<glide90::ui::Knob*>(&c); k != nullptr && k->getTooltip().startsWith("BALANCE"))
            knob = k;
        for (auto* child : c.getChildren())
            find(*child);
    };
    find(*editor);
    REQUIRE(knob != nullptr);
    set(*p, ids::balance, 1.0f);

    auto event = [&](juce::ModifierKeys mods, int clicks) {
        const auto now = juce::Time::getCurrentTime();
        const auto centre = knob->getLocalBounds().getCentre().toFloat();
        return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), centre, mods, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                                knob, knob, now, centre, now, clicks, false);
    };
    auto fieldOf = [&]() -> juce::TextEditor* {
        for (auto* child : knob->getChildren())
            if (auto* t = dynamic_cast<juce::TextEditor*>(child))
                return t;
        return nullptr;
    };

    knob->mouseDoubleClick(event(juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier), 2));
    REQUIRE(fieldOf() != nullptr);
    fieldOf()->setText("50", false);

    // A real click (through the window, as the mouse would) on the bare panel left of the header's name.
    const auto spot = editor->localPointToGlobal(juce::Point<int>(4, 4)).toFloat() - peer->getBounds().getPosition().toFloat();
    const auto t = juce::Time::currentTimeMillis();
    peer->handleMouseEvent(juce::MouseInputSource::InputSourceType::mouse, spot,
                           juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier), 1.0f, 0.0f, t);
    peer->handleMouseEvent(juce::MouseInputSource::InputSourceType::mouse, spot, juce::ModifierKeys(), 1.0f, 0.0f, t + 20);
    juce::MessageManager::getInstance()->runDispatchLoopUntil(100);
    CHECK(fieldOf() == nullptr);
    CHECK(plain(*p, ids::balance) == doctest::Approx(0.5f));

    // Text that names no value leaves it alone; Escape-like cancel is the same path without applying.
    knob->mouseDoubleClick(event(juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier), 2));
    REQUIRE(fieldOf() != nullptr);
    fieldOf()->setText("abc", false);
    peer->handleMouseEvent(juce::MouseInputSource::InputSourceType::mouse, spot,
                           juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier), 1.0f, 0.0f, t + 1000);
    peer->handleMouseEvent(juce::MouseInputSource::InputSourceType::mouse, spot, juce::ModifierKeys(), 1.0f, 0.0f, t + 1020);
    juce::MessageManager::getInstance()->runDispatchLoopUntil(100);
    CHECK(fieldOf() == nullptr);
    CHECK(plain(*p, ids::balance) == doctest::Approx(0.5f));

    // Alt-click: back to the default (100 %).
    knob->mouseDown(event(juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier | juce::ModifierKeys::altModifier), 1));
    juce::MessageManager::getInstance()->runDispatchLoopUntil(50);
    CHECK(plain(*p, ids::balance) == doctest::Approx(1.0f));

    editor->removeFromDesktop();
}

TEST_CASE("output mode: left only copies the left output, mono sums them")
{
    auto p = make();
    juce::AudioBuffer<float> buffer(2, kBlock);
    juce::MidiBuffer midi;
    auto render = [&](float mode) {
        set(*p, ids::outputMode, mode);
        p->reset();
        p->prepareToPlay(kRate, kBlock);
        juce::AudioBuffer<float> out(2, 32 * kBlock);
        for (int start = 0; start < out.getNumSamples(); start += kBlock)
        {
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < kBlock; ++i)
                    buffer.setSample(ch, i, 0.2f * (float) std::sin(0.05 * (start + i)) * (float) std::exp(-0.0003 * (start + i)));
            p->processBlock(buffer, midi);
            for (int ch = 0; ch < 2; ++ch)
                out.copyFrom(ch, start, buffer, ch, 0, kBlock);
        }
        return out;
    };
    set(*p, ids::glideOn, 0.0f);
    const auto stereo = render(0.0f), left = render(1.0f), mono = render(2.0f);
    double differ = 0.0, leftErr = 0.0, monoErr = 0.0;
    for (int i = 0; i < stereo.getNumSamples(); ++i)
    {
        const float l = stereo.getSample(0, i), r = stereo.getSample(1, i);
        differ = juce::jmax(differ, (double) std::abs(l - r));
        leftErr = juce::jmax(leftErr, (double) std::abs(left.getSample(1, i) - l));
        monoErr = juce::jmax(monoErr, (double) std::abs(mono.getSample(0, i) - 0.5f * (l + r)));
    }
    CHECK(differ > 1e-3); // the unit's two outputs differ
    CHECK(leftErr < 1e-6);
    CHECK(monoErr < 1e-6);
}

int main(int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI juce;
    doctest::Context context(argc, argv);
    return context.run();
}

TEST_CASE("typed values are read the way they are shown")
{
    auto p = make();
    auto typed = [&](const char* id, const char* text) {
        return glide90::parseParameterText(*p->state().getParameter(id), juce::String::fromUTF8(text));
    };
    auto value = [&](const char* id, const char* text) { return typed(id, text).value_or(-999.0f); };

    // Percentages: shown as "50%", typed as percentages.
    for (const char* id : { ids::balance, ids::outLevel, ids::glideMix, ids::glideDynamics, ids::glideBar })
    {
        CAPTURE(id);
        CHECK(value(id, "50") == doctest::Approx(0.5f));
        CHECK(value(id, "50%") == doctest::Approx(0.5f));
        CHECK(value(id, "50 %") == doctest::Approx(0.5f));
        CHECK(value(id, "100%") == doctest::Approx(1.0f));
        CHECK(value(id, "12,5") == doctest::Approx(0.125f));
    }
    // What the host's own field sends goes through the same reading.
    auto* mix = p->state().getParameter(ids::balance);
    CHECK(mix->convertFrom0to1(mix->getValueForText("50")) == doctest::Approx(0.5f));

    // Text that names no value changes nothing.
    for (const char* id : { ids::balance, ids::erRoomSize, ids::erLiveness, ids::erLpf, ids::glideDivision })
    {
        CAPTURE(id);
        CHECK_FALSE(typed(id, "").has_value());
        CHECK_FALSE(typed(id, "abc").has_value());
    }

    // Plain numbers, with or without the shown unit; seconds and semitones when said so.
    CHECK(value(ids::erRoomSize, "3.3") == doctest::Approx(3.3f));
    CHECK(value(ids::erDelay, "25.0ms") == doctest::Approx(25.0f));
    CHECK(value(ids::erDelay, "0.2 s") == doctest::Approx(200.0f));
    CHECK(value(ids::glidePeriod, "333 ms") == doctest::Approx(333.0f));
    CHECK(value(ids::glidePeriod, "1.2s") == doctest::Approx(1200.0f));
    CHECK(value(ids::glidePeriod, "1,2 сек") == doctest::Approx(1200.0f));
    CHECK(value(ids::glideDepth, "30 ct") == doctest::Approx(30.0f));
    CHECK(value(ids::glideDepth, "2 st") == doctest::Approx(200.0f));
    CHECK(value(ids::erLiveness, "7") == doctest::Approx(7.0f));

    // LPF: kHz, Hz, the shown label in any case, THRU.
    auto lpf = [&](const char* text) {
        return p->state().getParameter(ids::erLpf)->getText(
            p->state().getParameter(ids::erLpf)->convertTo0to1(value(ids::erLpf, text)), 16);
    };
    CHECK(lpf("2.5") == "2.5kHz");
    CHECK(lpf("2.5k") == "2.5kHz");
    CHECK(lpf("2500") == "2.5kHz");
    CHECK(lpf("2.5KHZ") == "2.5kHz");
    CHECK(lpf("6300 Hz") == "6.3kHz");
    CHECK(lpf("11 kHz") == "11kHz");
    CHECK(lpf("thru") == "THRU");
    CHECK(lpf("OFF") == "THRU");

    // Note values without caring for case or spaces.
    auto division = [&](const char* text) { return glide90::kDivisions[(size_t) value(ids::glideDivision, text)].label; };
    CHECK(juce::String(division("1/4 D")) == "1/4 D");
    CHECK(juce::String(division("1/4d")) == "1/4 D");
    CHECK(juce::String(division("1/8 t")) == "1/8 T");
    CHECK(juce::String(division("1/16 dotted")) == "1/16 D");
    CHECK(juce::String(division("2 bar")) == "2 BARS");
    CHECK(juce::String(division("1 bar")) == "1 BAR");

    // Other choices by name, in any case.
    CHECK(value(ids::glideMode, "trigger") == doctest::Approx(2.0f));
    CHECK(value(ids::glideShape, "ramp down") == doctest::Approx(4.0f));
}

namespace
{

// Peak of the output while silence goes in, `blocks` blocks long.
float silencePeak(Glide90Processor& p, int blocks)
{
    juce::AudioBuffer<float> buffer(2, kBlock);
    juce::MidiBuffer midi;
    float peak = 0.0f;
    for (int b = 0; b < blocks; ++b)
    {
        buffer.clear();
        p.processBlock(buffer, midi);
        peak = std::max(peak, buffer.getMagnitude(0, kBlock));
    }
    return peak;
}

void playLoud(Glide90Processor& p, int blocks, bool hostBypass = false)
{
    juce::AudioBuffer<float> buffer(2, kBlock);
    juce::MidiBuffer midi;
    for (int b = 0; b < blocks; ++b)
    {
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < kBlock; ++i)
                buffer.setSample(ch, i, 0.5f * (float) std::sin(0.05 * (b * kBlock + i)));
        if (hostBypass)
            p.processBlockBypassed(buffer, midi);
        else
            p.processBlock(buffer, midi);
    }
}

} // namespace

TEST_CASE("after the host's bypass or a reset, nothing of what played before comes back")
{
    // The reverse gate holds two seconds: released from a bypass, it must not play out a swell of
    // the audio from before it.
    auto p = make();
    playLoud(*p, 100); // half a second into the reverb
    juce::AudioBuffer<float> buffer(2, kBlock);
    juce::MidiBuffer midi;
    for (int b = 0; b < 20; ++b) // bypassed, silence
    {
        buffer.clear();
        p->processBlockBypassed(buffer, midi);
    }
    CHECK(silencePeak(*p, 200) < 1e-4f);

    playLoud(*p, 100);
    p->reset(); // a host stopping or jumping
    CHECK(silencePeak(*p, 200) < 1e-4f);
}

TEST_CASE("the host's bypass fades in and out instead of switching")
{
    auto p = make();
    set(*p, ids::glideOn, 0.0f); // the effect alone (BALANCE 100 %): the two paths differ completely
    juce::AudioBuffer<float> buffer(2, kBlock);
    juce::MidiBuffer midi;
    float previous = 0.0f, worst = 0.0f;
    for (int b = 0; b < 400; ++b)
    {
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < kBlock; ++i)
                buffer.setSample(ch, i, 0.5f * (float) std::sin(2.0 * 3.14159265 * 220.0 * (b * kBlock + i) / kRate));
        if (b >= 100 && b < 200)
            p->processBlockBypassed(buffer, midi);
        else
            p->processBlock(buffer, midi);
        if (b > 20)
            for (int i = 0; i < kBlock; ++i)
            {
                worst = std::max(worst, std::abs(buffer.getSample(0, i) - previous));
                previous = buffer.getSample(0, i);
            }
        else
            previous = buffer.getSample(0, kBlock - 1);
    }
    // A 220 Hz sine at 0.5 moves at most 0.0144 per sample, the reverb's output a few times that;
    // switching between them would jump by up to 1.
    MESSAGE("largest step " << worst);
    CHECK(worst < 0.08f);
}

TEST_CASE("programs that are not emulated yet do not replace the one that plays")
{
    auto p = make();
    CHECK_FALSE(p->recallMemory(1)); // REV 1 HALL
    CHECK(p->algorithm() == 17);
    CHECK(p->loadedMemory() == 17);
    set(*p, ids::memory, 18.0f); // automation to ADR NOISE GATE
    p->flushPendingRecall();
    CHECK(p->algorithm() == 17);
    CHECK(p->recallMemory(16));
    CHECK(p->algorithm() == 16);
}

TEST_CASE("restoring a project is not reported to the host as an edit, and keeps both A/B slots")
{
    auto a = make();
    set(*a, ids::glideDepth, 50.0f);
    a->selectAB(1);
    set(*a, ids::glideDepth, 120.0f);
    set(*a, ids::glideDynamics, 0.4f);
    const auto state = saveState(*a);

    struct GestureCounter : juce::AudioProcessorListener
    {
        int gestures = 0;
        void audioProcessorParameterChanged(juce::AudioProcessor*, int, float) override {}
        void audioProcessorChanged(juce::AudioProcessor*, const ChangeDetails&) override {}
        void audioProcessorParameterChangeGestureBegin(juce::AudioProcessor*, int) override { ++gestures; }
    } counter;
    auto b = make();
    b->addListener(&counter);
    b->setStateInformation(state.getData(), (int) state.getSize());
    b->removeListener(&counter);
    CHECK(counter.gestures == 0);

    CHECK(b->abSlot() == 1);
    CHECK(plain(*b, ids::glideDepth) == doctest::Approx(120.0f));
    CHECK(plain(*b, ids::glideDynamics) == doctest::Approx(0.4f));
    b->selectAB(0);
    CHECK(plain(*b, ids::glideDepth) == doctest::Approx(50.0f));
}

TEST_CASE("the Loveless roadie preset bends with the strength of each strike")
{
    auto p = make();
    const int index = p->factoryPresetNames().indexOf("MBV Loveless roadie");
    REQUIRE(index >= 0);
    p->setCurrentProgram(index);
    CHECK(plain(*p, ids::glideDynamics) == doctest::Approx(0.6f));
    CHECK(juce::roundToInt(plain(*p, ids::glideMode)) == (int) sway::GlideMode::Trigger);
}

TEST_CASE("undo takes the editor's edits back, one gesture at a time, and never the host's automation")
{
    auto p = make();
    auto& undo = p->undoManager();
    auto gesture = [&](const char* id, float value) {
        auto* param = p->state().getParameter(id);
        param->beginChangeGesture();
        param->setValueNotifyingHost(param->convertTo0to1(value * 0.5f)); // a drag passes through values
        param->setValueNotifyingHost(param->convertTo0to1(value));
        param->endChangeGesture();
    };

    for (int i = 0; i < 50; ++i)
        set(*p, ids::glideDepth, 10.0f + (float) i); // automation: no gestures
    CHECK_FALSE(undo.canUndo());

    gesture(ids::erRoomSize, 12.0f);
    gesture(ids::glideDepth, 200.0f);
    REQUIRE(undo.canUndo());
    undo.undo();
    CHECK(plain(*p, ids::glideDepth) == doctest::Approx(59.0f));
    CHECK(plain(*p, ids::erRoomSize) == doctest::Approx(12.0f));
    undo.undo();
    CHECK(plain(*p, ids::erRoomSize) == doctest::Approx(3.3f));
    undo.redo();
    CHECK(plain(*p, ids::erRoomSize) == doctest::Approx(12.0f));

    // A preset load is one step.
    p->setCurrentProgram(1);
    CHECK(plain(*p, ids::erRoomSize) == doctest::Approx(20.0f));
    undo.undo();
    CHECK(plain(*p, ids::erRoomSize) == doctest::Approx(12.0f));
    CHECK(p->algorithm() == 17);

    // Redrawing the custom shape: marks the preset changed, and comes back with undo.
    p->setCurrentProgram(0);
    CHECK_FALSE(p->isPresetModified());
    const sway::ShapePoint first[] { { 0.0f, 0.0f }, { 0.4f, 1.0f } };
    const sway::ShapePoint second[] { { 0.0f, 0.0f }, { 0.2f, 0.7f }, { 0.6f, 0.3f } };
    sway::Shape shape;
    shape.setPoints(first, 2);
    p->beginEdit();
    p->setCustomShape(shape);
    p->endEdit();
    p->setCurrentProgram(0); // the custom shape stays custom...
    set(*p, ids::glideShape, (float) sway::kShapePresetNames.size());
    juce::MemoryBlock keep;
    p->getStateInformation(keep);
    p->setStateInformation(keep.getData(), (int) keep.getSize()); // ...and is now the baseline
    CHECK_FALSE(p->isPresetModified());
    shape.setPoints(second, 3);
    p->beginEdit();
    p->setCustomShape(shape);
    p->endEdit();
    CHECK(p->isPresetModified());
    undo.undo();
    CHECK(p->currentShape().size() == 2);
    CHECK_FALSE(p->isPresetModified());
}

TEST_CASE("the save dialog opens inside the plugin window, and closing the window with it open is safe")
{
    auto p = make();
    std::unique_ptr<juce::AudioProcessorEditor> editor(p->createEditor());
    editor->setVisible(true);
    editor->addToDesktop(0);
    juce::TextButton* saveKey = nullptr;
    std::function<void(juce::Component&)> find = [&](juce::Component& c) {
        if (auto* b = dynamic_cast<juce::TextButton*>(&c); b != nullptr && b->getButtonText() == "SAVE")
            saveKey = b;
        for (auto* child : c.getChildren())
            find(*child);
    };
    find(*editor);
    REQUIRE(saveKey != nullptr);
    saveKey->onClick();

    juce::AlertWindow* dialog = nullptr;
    std::function<void(juce::Component&)> findDialog = [&](juce::Component& c) {
        if (auto* d = dynamic_cast<juce::AlertWindow*>(&c))
            dialog = d;
        for (auto* child : c.getChildren())
            findDialog(*child);
    };
    findDialog(*editor);
    REQUIRE(dialog != nullptr); // a child of the editor, not a window of its own
    CHECK(dialog->isCurrentlyModal());
    if (const auto path = juce::SystemStats::getEnvironmentVariable("GLIDE90_SNAPSHOT", {}); path.isNotEmpty())
    {
        juce::MessageManager::getInstance()->runDispatchLoopUntil(200);
        const auto image = editor->createComponentSnapshot(editor->getLocalBounds(), true, 1.0f);
        juce::File file(juce::File(path).getSiblingFile(juce::File(path).getFileNameWithoutExtension() + "-save.png"));
        file.deleteFile();
        juce::FileOutputStream out(file);
        REQUIRE(juce::PNGImageFormat().writeImageToStream(image, out));
    }

    editor.reset(); // the host closes the window
    juce::MessageManager::getInstance()->runDispatchLoopUntil(100); // the dialog's callback arrives now
    CHECK(p->currentPresetName() == "Init");
}
