// Processor-level tests: presets, state, latency and host bypass of the plugin, and the editor.
#define DOCTEST_CONFIG_IMPLEMENT
#define JUCE_GUI_BASICS_INCLUDE_XHEADERS 1 // Linux: the X11 calls in main()
#include <doctest/doctest.h>

#include "PluginEditor.h"
#include "PluginProcessor.h"
#include "ui/Controls.h"
#include "ui/Dialog.h"
#include "ui/GatePanel.h"
#include "ui/GlidePanel.h"
#include "ui/HeaderBar.h"
#include "ui/PanelLookAndFeel.h"
#include "ui/ShapeDisplay.h"

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_events/juce_events.h>
#include <map>
#include <numeric>
#include <typeinfo>
#include <juce_gui_basics/juce_gui_basics.h>

using glide90::Glide90Processor;
namespace ids = glide90::ids;
namespace ui = glide90::ui;

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

// The unit's BALANCE: 0 the direct guitar alone, 1 the effect alone.
void mixAt(Glide90Processor& p, float balance)
{
    set(p, ids::gateMix, balance);
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

TEST_CASE("the reverb is the reverse gate, and a state keeps its edits")
{
    auto a = make();
    set(*a, ids::gateRoomSize, 7.7f);
    set(*a, ids::gateLiveness, 2.0f);
    const auto state = saveState(*a);

    auto b = make();
    b->setStateInformation(state.getData(), (int) state.getSize());
    CHECK(plain(*b, ids::gateRoomSize) == doctest::Approx(7.7f));
    CHECK(plain(*b, ids::gateLiveness) == doctest::Approx(2.0f));
}

TEST_CASE("the reported latency is where the direct signal comes out, with or without host bypass")
{
    auto p = make();
    mixAt(*p, 0.0f);
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

TEST_CASE("factory presets set the reverse gate and the glide")
{
    auto p = make();
    for (int i = 0; i < p->getNumPrograms(); ++i)
    {
        CAPTURE(p->getProgramName(i));
        p->setCurrentProgram(i);
        CHECK(p->getCurrentProgram() == i);
        CHECK(p->currentPresetName() == p->getProgramName(i));
        for (const char* foreign : { "MBV", "Loveless", "Whammy", "Init" })
            CHECK_FALSE(p->getProgramName(i).contains(foreign));
    }
    p->setCurrentProgram(p->factoryPresetNames().indexOf("Steep Swell")); // a dive on each pick
    CHECK(plain(*p, ids::gateRoomSize) == doctest::Approx(20.0f));
    CHECK(plain(*p, ids::gateLiveness) == doctest::Approx(10.0f));
    CHECK(plain(*p, ids::glideOn) > 0.5f);
    CHECK(juce::roundToInt(plain(*p, ids::glideMode)) == (int) sway::GlideMode::Trigger);

    // A preset leaves the rig alone (INPUT, both modes) and resets what it does not set, OUTPUT too.
    set(*p, ids::input, -6.0f);
    set(*p, ids::inputMode, (float) glide90::InputMode::Mono);
    set(*p, ids::outputMode, (float) glide90::OutputMode::MonoSum);
    set(*p, ids::output, -3.0f);
    set(*p, ids::glideDynamics, 0.1f);
    p->setCurrentProgram(1);
    CHECK(plain(*p, ids::input) == doctest::Approx(-6.0f));
    CHECK(juce::roundToInt(plain(*p, ids::inputMode)) == (int) glide90::InputMode::Mono);
    CHECK(juce::roundToInt(plain(*p, ids::outputMode)) == (int) glide90::OutputMode::MonoSum);
    CHECK(plain(*p, ids::output) == doctest::Approx(0.0f));
    CHECK(plain(*p, ids::glideDynamics) == doctest::Approx(0.0f)); // the presets' plain base
}

TEST_CASE("factory presets bring their own reverb settings and load unmodified")
{
    auto p = make();
    for (int i = 0; i < p->getNumPrograms(); ++i)
    {
        CAPTURE(p->getProgramName(i));
        p->setCurrentProgram(i);
        CHECK_FALSE(p->isPresetModified());
        if (p->getProgramName(i) == "Glide Guitar" || p->getProgramName(i) == "Steep Swell" || p->getProgramName(i) == "Wall")
            CHECK(plain(*p, ids::gateRoomSize) == doctest::Approx(20.0f));
    }
}

TEST_CASE("deleting a user preset removes its file only, and keeps the sound loaded")
{
    auto p = make();
    const juce::String name = "glide90 test " + juce::Uuid().toString();
    REQUIRE(p->saveUserPreset(name));
    const auto file = Glide90Processor::userPresetDirectory().getChildFile(name + Glide90Processor::presetExtension());
    CHECK(p->userPresetFiles().contains(file));

    CHECK(p->deleteUserPreset(file));
    CHECK_FALSE(file.existsAsFile());
    CHECK_FALSE(p->userPresetFiles().contains(file));
    CHECK_FALSE(p->deleteUserPreset(file));
    CHECK(p->currentPresetName() == name);

    // Nothing outside the preset folder, whatever its extension.
    juce::TemporaryFile outside(Glide90Processor::presetExtension());
    REQUIRE(outside.getFile().replaceWithText("x"));
    CHECK_FALSE(p->deleteUserPreset(outside.getFile()));
    CHECK(outside.getFile().existsAsFile());
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
    set(*b, ids::glideDynamics, 0.0f);
    b->setStateInformation(old.getData(), (int) old.getSize());
    CHECK(plain(*b, ids::glideDepth) == doctest::Approx(40.0f));
    CHECK(plain(*b, ids::glideDynamics) == doctest::Approx(0.6f));
    CHECK(plain(*b, ids::glideOn) > 0.5f);
}

TEST_CASE("the editor builds and lays out in every look; GLIDE90_SNAPSHOT=<file.png> saves a picture of each")
{
    const auto before = ui::theme().id;
    for (int look = 0; look < ui::kThemeCount; ++look)
    {
    CAPTURE(ui::themeAt(look).name);
    ui::setTheme(ui::themeAt(look).id);
    auto p = make();
    p->setCurrentProgram(p->factoryPresetNames().indexOf("Wall"));
    std::unique_ptr<juce::AudioProcessorEditor> editor(p->createEditor());
    REQUIRE(editor != nullptr);
    CHECK(editor->getWidth() > 0);
    if (const auto path = juce::SystemStats::getEnvironmentVariable("GLIDE90_SNAPSHOT", {}); path.isNotEmpty())
    {
        juce::MessageManager::getInstance()->runDispatchLoopUntil(300); // let the timers fill the displays
        const auto image = editor->createComponentSnapshot(editor->getLocalBounds(), true, 2.0f);
        const juce::File given(path);
        {
            // The same look in motion: the bar pushed, a swell most of the way through.
            ui::Motion m;
            m.cents = -48.0f;
            m.bend = 0.8f;
            m.swell = 0.85f;
            m.progress = 0.6f;
            m.room = 20.0f;
            m.time = 1.0;
            dynamic_cast<glide90::Glide90Editor&>(*editor).showMotion(m);
            const auto moving = editor->createComponentSnapshot(editor->getLocalBounds(), true, 1.0f);
            juce::File movingFile(given.getSiblingFile(given.getFileNameWithoutExtension() + "-" + ui::themeAt(look).key + "-motion.png"));
            movingFile.deleteFile();
            juce::FileOutputStream movingOut(movingFile);
            REQUIRE(juce::PNGImageFormat().writeImageToStream(moving, movingOut));
            dynamic_cast<glide90::Glide90Editor&>(*editor).showMotion({});
        }
        juce::File file(given.getSiblingFile(given.getFileNameWithoutExtension() + "-" + ui::themeAt(look).key + ".png"));
        file.deleteFile();
        juce::FileOutputStream out(file);
        REQUIRE(juce::PNGImageFormat().writeImageToStream(image, out));

        // A sample of the popup menu look next to it, drawn by the same look-and-feel calls.
        juce::Image menuImage(juce::Image::ARGB, 320, 260, true);
        juce::Graphics g(menuImage);
        auto& lf = editor->getLookAndFeel();
        lf.drawPopupMenuBackground(g, 300, 190);
        lf.drawPopupMenuSectionHeader(g, { 0, 4, 300, 22 }, "Factory");
        const char* items[] { "Glide Guitar", "Steep Swell", "Drowned Shimmy" };
        for (int i = 0; i < 3; ++i)
            lf.drawPopupMenuItem(g, { 0, 28 + i * 24, 300, 24 }, false, true, i == 2, i == 1, false, items[i], {}, nullptr, nullptr);
        lf.drawPopupMenuItem(g, { 0, 100, 300, 9 }, true, true, false, false, false, {}, {}, nullptr, nullptr);
        lf.drawPopupMenuSectionHeader(g, { 0, 112, 300, 22 }, "User");
        lf.drawPopupMenuItem(g, { 0, 136, 300, 24 }, false, false, false, false, false, "Empty", {}, nullptr, nullptr);
        // And a tooltip under it.
        const juce::String tip = "How long the swell lasts: at 20, about 1.4 s";
        const auto tipBounds = lf.getTooltipBounds(tip, { 0, 0 }, { 0, 0, 320, 400 });
        juce::Graphics::ScopedSaveState save(g);
        g.setOrigin(4, 196);
        lf.drawTooltip(g, tip, tipBounds.getWidth(), tipBounds.getHeight());
        juce::File menuFile(file.getSiblingFile(file.getFileNameWithoutExtension() + "-menu.png"));
        menuFile.deleteFile();
        juce::FileOutputStream menuOut(menuFile);
        REQUIRE(juce::PNGImageFormat().writeImageToStream(menuImage, menuOut));
    }
    }
    ui::setTheme(before);
}

TEST_CASE("GLIDE90_PROFILE=1 reports what painting the whole window costs in each look")
{
    if (juce::SystemStats::getEnvironmentVariable("GLIDE90_PROFILE", {}).isEmpty())
        return;
    const auto before = ui::theme().id;
    for (int look = 0; look < ui::kThemeCount; ++look)
    {
        ui::setTheme(ui::themeAt(look).id);
        auto p = make();
        std::unique_ptr<juce::AudioProcessorEditor> editor(p->createEditor());
        for (float scale : { 1.0f, 2.0f })
        {
            editor->createComponentSnapshot(editor->getLocalBounds(), true, scale); // caches built
            constexpr int frames = 30;
            const auto start = juce::Time::getMillisecondCounterHiRes();
            for (int i = 0; i < frames; ++i)
            {
                ui::Motion m;
                m.time = i / 60.0;
                m.cents = -20.0f * (float) (i % 7);
                dynamic_cast<glide90::Glide90Editor&>(*editor).showMotion(m);
                editor->createComponentSnapshot(editor->getLocalBounds(), true, scale);
            }
            MESSAGE(ui::themeAt(look).name << " x" << scale << ": "
                    << (juce::Time::getMillisecondCounterHiRes() - start) / frames << " ms per full frame");
        }
    }
    ui::setTheme(before);
}

TEST_CASE("moving GLIDE across the reverse gate does not click, and the direct sound plays on")
{
    // The largest sample-to-sample step of a sine through the plugin, with the bend moved twice or not
    // at all: the moves may not add a jump of their own. With the effect silent (MIX all direct) the
    // output stays the same: only the effect fades while the bender moves.
    auto run = [](bool flip, float gateMix) {
        auto p = make();
        mixAt(*p, gateMix);
        juce::AudioBuffer<float> buffer(2, kBlock), all(2, 300 * kBlock);
        juce::MidiBuffer midi;
        float previous = 0.0f, worst = 0.0f;
        for (int b = 0; b < 300; ++b)
        {
            if (flip && (b == 100 || b == 200))
                set(*p, ids::glidePosition, (float) (b == 100 ? glide90::GlidePosition::Post : glide90::GlidePosition::Pre));
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < kBlock; ++i)
                    buffer.setSample(ch, i, 0.5f * (float) std::sin(2.0 * 3.14159265 * 220.0 * (b * kBlock + i) / kRate));
            p->processBlock(buffer, midi);
            for (int i = 0; i < kBlock; ++i)
            {
                if (b > 20)
                    worst = std::max(worst, std::abs(buffer.getSample(0, i) - previous));
                previous = buffer.getSample(0, i);
            }
            for (int ch = 0; ch < 2; ++ch)
                all.copyFrom(ch, b * kBlock, buffer, ch, 0, kBlock);
        }
        return std::make_pair(worst, all);
    };
    const float steady = run(false, 0.5f).first, flipped = run(true, 0.5f).first;
    MESSAGE("largest step " << flipped << ", without moves " << steady);
    CHECK(flipped <= steady * 1.1f + 0.005f);
    const auto straight = run(false, 0.0f).second, moved = run(true, 0.0f).second;
    double apart = 0.0;
    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < straight.getNumSamples(); ++i)
            apart = std::max(apart, (double) std::abs(straight.getSample(ch, i) - moved.getSample(ch, i)));
    CHECK(apart < 1e-6); // rounding only
}

TEST_CASE("a knob's value field: a click elsewhere applies it; Alt-click resets the knob")
{
    auto p = make();
    std::unique_ptr<juce::AudioProcessorEditor> editor(p->createEditor());
    // Clear of the screen's corner, where macOS has its menu bar: a click is delivered only where the
    // window is really the one on top.
    editor->setTopLeftPosition(160, 160);
    // Above every other window: the OS (WindowFromPoint on Windows) decides which window a click lands in.
    editor->setAlwaysOnTop(true);
    editor->setVisible(true);
    editor->addToDesktop(0);
    editor->toFront(true);
    auto* peer = editor->getPeer();
    REQUIRE(peer != nullptr);
    juce::MessageManager::getInstance()->runDispatchLoopUntil(100);

    // The reverse gate's MIX knob.
    glide90::ui::Knob* knob = nullptr;
    std::function<void(juce::Component&)> find = [&](juce::Component& c) {
        if (auto* k = dynamic_cast<glide90::ui::Knob*>(&c); k != nullptr && k->getLabel() == "MIX")
            knob = k;
        for (auto* child : c.getChildren())
            find(*child);
    };
    find(*editor);
    REQUIRE(knob != nullptr);
    set(*p, ids::gateMix, 0.2f);

    auto event = [&](juce::ModifierKeys mods, int clicks) {
        const auto now = juce::Time::getCurrentTime();
        const auto centre = knob->getLocalBounds().getCentre().toFloat();
        return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), centre, mods, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                                knob, knob, now, centre, now, clicks, false);
    };
    auto fieldOf = [&]() -> glide90::ui::ReadoutField* {
        std::function<glide90::ui::ReadoutField*(juce::Component&)> search = [&](juce::Component& c) -> glide90::ui::ReadoutField* {
            for (auto* child : c.getChildren())
            {
                if (auto* f = dynamic_cast<glide90::ui::ReadoutField*>(child))
                    return f;
                if (auto* f = search(*child))
                    return f;
            }
            return nullptr;
        };
        return search(*knob);
    };

    knob->mouseDoubleClick(event(juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier), 2));
    REQUIRE(fieldOf() != nullptr);
    fieldOf()->setText("60");

    // A real click (through the window, as the mouse would) on the bare panel left of the header's name.
    const auto spot = editor->localPointToGlobal(juce::Point<int>(4, 4)).toFloat() - peer->getBounds().getPosition().toFloat();
    // On a CI desktop another window can still cover ours: the OS then sends the click elsewhere,
    // which says nothing about the plugin. Skip the click part rather than fail.
    if (editor->contains(juce::Point<int>(4, 4)))
    {
        const auto t = juce::Time::currentTimeMillis();
        peer->handleMouseEvent(juce::MouseInputSource::InputSourceType::mouse, spot,
                               juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier), 1.0f, 0.0f, t);
        peer->handleMouseEvent(juce::MouseInputSource::InputSourceType::mouse, spot, juce::ModifierKeys(), 1.0f, 0.0f, t + 20);
        juce::MessageManager::getInstance()->runDispatchLoopUntil(100);
        CHECK(fieldOf() == nullptr);
        CHECK(plain(*p, ids::gateMix) == doctest::Approx(0.6f));

        // Text that names no value leaves it alone; Escape-like cancel is the same path without applying.
        knob->mouseDoubleClick(event(juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier), 2));
        REQUIRE(fieldOf() != nullptr);
        fieldOf()->setText("abc");
        peer->handleMouseEvent(juce::MouseInputSource::InputSourceType::mouse, spot,
                               juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier), 1.0f, 0.0f, t + 1000);
        peer->handleMouseEvent(juce::MouseInputSource::InputSourceType::mouse, spot, juce::ModifierKeys(), 1.0f, 0.0f, t + 1020);
        juce::MessageManager::getInstance()->runDispatchLoopUntil(100);
        CHECK(fieldOf() == nullptr);
        CHECK(plain(*p, ids::gateMix) == doctest::Approx(0.6f));
    }
    else
    {
        MESSAGE("the test window is not on top at the click point here: click part skipped");
    }

    // Alt-click: back to the default (all effect).
    knob->mouseDown(event(juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier | juce::ModifierKeys::altModifier), 1));
    juce::MessageManager::getInstance()->runDispatchLoopUntil(50);
    CHECK(plain(*p, ids::gateMix) == doctest::Approx(1.0f));

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

TEST_CASE("input mode: new instances are stereo")
{
    auto a = make();
    CHECK(juce::roundToInt(plain(*a, ids::inputMode)) == (int) glide90::InputMode::Stereo);
}

namespace
{
// The plugin's output for `in` (two channels), glide off, at `balance`, in `inputMode`.
juce::AudioBuffer<float> renderInput(const juce::AudioBuffer<float>& in, float balance, glide90::InputMode inputMode,
                                     std::function<void(Glide90Processor&, int block)> perBlock = {})
{
    auto p = make();
    set(*p, ids::glideOn, 0.0f);
    mixAt(*p, balance);
    set(*p, ids::inputMode, (float) inputMode);
    p->reset();
    juce::AudioBuffer<float> buffer(2, kBlock), out(2, in.getNumSamples());
    juce::MidiBuffer midi;
    for (int start = 0, b = 0; start + kBlock <= in.getNumSamples(); start += kBlock, ++b)
    {
        if (perBlock)
            perBlock(*p, b);
        for (int ch = 0; ch < 2; ++ch)
            buffer.copyFrom(ch, 0, in, ch, start, kBlock);
        p->processBlock(buffer, midi);
        for (int ch = 0; ch < 2; ++ch)
            out.copyFrom(ch, start, buffer, ch, 0, kBlock);
    }
    return out;
}

juce::AudioBuffer<float> guitar(bool rightToo)
{
    juce::AudioBuffer<float> in(2, 64 * kBlock);
    in.clear();
    for (int i = 0; i < in.getNumSamples(); ++i)
    {
        const float v = 0.3f * (float) std::sin(0.05 * i) * (float) std::exp(-0.0001 * (i % 8000));
        in.setSample(0, i, v);
        if (rightToo)
            in.setSample(1, i, v);
    }
    return in;
}

double peakOf(const juce::AudioBuffer<float>& b, int ch)
{
    return b.getMagnitude(ch, 0, b.getNumSamples());
}
} // namespace

TEST_CASE("input mode: STEREO keeps a one-sided guitar on its side, MONO centres it")
{
    const auto left = guitar(false);
    const auto dryStereo = renderInput(left, 0.0f, glide90::InputMode::Stereo);
    CHECK(peakOf(dryStereo, 0) > 0.1);
    CHECK(peakOf(dryStereo, 1) < 1e-6);

    const auto dryMono = renderInput(left, 0.0f, glide90::InputMode::Mono);
    CHECK(peakOf(dryMono, 1) > 0.05);
    double differ = 0.0;
    for (int i = 0; i < dryMono.getNumSamples(); ++i)
        differ = juce::jmax(differ, (double) std::abs(dryMono.getSample(0, i) - dryMono.getSample(1, i)));
    CHECK(differ < 1e-6);

    // The reverse gate hears both inputs in either mode: its swell is on both sides.
    const auto wet = renderInput(left, 1.0f, glide90::InputMode::Stereo);
    MESSAGE("swell peaks " << peakOf(wet, 0) << " / " << peakOf(wet, 1));
    CHECK(peakOf(wet, 0) > 0.002);
    CHECK(peakOf(wet, 1) > 0.5 * peakOf(wet, 0));
}

TEST_CASE("input mode: a centred source sounds the same in both modes")
{
    const auto centred = guitar(true);
    for (float balance : { 0.0f, 0.5f, 1.0f })
    {
        CAPTURE(balance);
        const auto stereo = renderInput(centred, balance, glide90::InputMode::Stereo);
        const auto mono = renderInput(centred, balance, glide90::InputMode::Mono);
        double err = 0.0;
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < stereo.getNumSamples(); ++i)
                err = juce::jmax(err, (double) std::abs(stereo.getSample(ch, i) - mono.getSample(ch, i)));
        CHECK(err == 0.0);
    }
}

TEST_CASE("input mode: switching it while playing does not click")
{
    // A one-sided guitar, direct only, flipped between the modes: each flip may not jump further
    // than the signal itself does.
    const auto left = guitar(false);
    auto worstStep = [](const juce::AudioBuffer<float>& b) {
        float worst = 0.0f;
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 20 * kBlock; i < b.getNumSamples(); ++i)
                worst = juce::jmax(worst, std::abs(b.getSample(ch, i) - b.getSample(ch, i - 1)));
        return worst;
    };
    const float steady = worstStep(renderInput(left, 0.0f, glide90::InputMode::Stereo));
    const float flipped = worstStep(renderInput(left, 0.0f, glide90::InputMode::Stereo, [](Glide90Processor& p, int b) {
        if (b == 30 || b == 45)
            set(p, ids::inputMode, (float) (b == 30 ? glide90::InputMode::Mono : glide90::InputMode::Stereo));
    }));
    MESSAGE("largest step " << flipped << ", without flips " << steady);
    CHECK(flipped <= steady * 1.1f + 0.005f);
}

TEST_CASE("the IN and OUT keys switch the input and output modes")
{
    auto p = make();
    std::unique_ptr<juce::AudioProcessorEditor> editor(p->createEditor());
    auto keyFor = [&](const char* id) {
        std::function<juce::Button*(juce::Component&)> search = [&](juce::Component& c) -> juce::Button* {
            for (auto* child : c.getChildren())
            {
                if (auto* b = dynamic_cast<juce::Button*>(child); b != nullptr && child->getComponentID() == id)
                    return b;
                if (auto* b = search(*child))
                    return b;
            }
            return nullptr;
        };
        return search(*editor);
    };
    auto* in = keyFor(ids::inputMode);
    auto* out = keyFor(ids::outputMode);
    REQUIRE(in != nullptr);
    REQUIRE(out != nullptr);
    CHECK(in->getButtonText() == "STEREO");
    CHECK(in->getToggleState());

    in->triggerClick();
    juce::MessageManager::getInstance()->runDispatchLoopUntil(20);
    CHECK(juce::roundToInt(plain(*p, ids::inputMode)) == (int) glide90::InputMode::Mono);
    CHECK(in->getButtonText() == "MONO");
    CHECK_FALSE(in->getToggleState());

    const char* expected[] { "L ONLY", "MONO", "STEREO" };
    for (int i = 0; i < 3; ++i)
    {
        out->triggerClick();
        juce::MessageManager::getInstance()->runDispatchLoopUntil(20);
        CHECK(juce::roundToInt(plain(*p, ids::outputMode)) == (i + 1) % 3);
        CHECK(out->getButtonText() == expected[i]);
        CHECK(out->getToggleState() == (i == 2));
    }
}

// --- A/B, presets and the project: checked for every parameter there is, so a new one is covered
// without a test of its own. Only INPUT and the input and output modes belong to the instance
// rather than the sound.

namespace
{
bool isInstanceParameter(const juce::String& id)
{
    return id == ids::input || id == ids::inputMode || id == ids::outputMode;
}

std::vector<juce::RangedAudioParameter*> allParameters(Glide90Processor& p)
{
    std::vector<juce::RangedAudioParameter*> out;
    for (auto* param : p.getParameters())
        if (auto* r = dynamic_cast<juce::RangedAudioParameter*>(param))
            out.push_back(r);
    return out;
}

// Sets every parameter to one of two values (`which` 0 or 1) that differ for each of them, and
// returns what each one reads afterwards (normalised, on its own grid).
std::map<juce::String, float> setEveryParameter(Glide90Processor& p, int which)
{
    std::map<juce::String, float> values;
    for (auto* r : allParameters(p))
    {
        r->setValueNotifyingHost(r->convertTo0to1(r->convertFrom0to1(which == 0 ? 0.2f : 0.8f)));
        values[r->getParameterID()] = r->getValue();
    }
    return values;
}

// Every parameter as `sound` has it, the instance's own as `instance` has them.
void checkEveryParameter(Glide90Processor& p, const std::map<juce::String, float>& sound,
                         const std::map<juce::String, float>& instance)
{
    for (auto* r : allParameters(p))
    {
        const auto id = r->getParameterID();
        CAPTURE(id);
        CHECK(r->getValue() == doctest::Approx((isInstanceParameter(id) ? instance : sound).at(id)).epsilon(1e-4));
    }
}
} // namespace

TEST_CASE("A and B each keep every parameter but INPUT and the modes, which they share")
{
    auto p = make();
    const auto a = setEveryParameter(*p, 0);
    p->selectAB(1);
    const auto b = setEveryParameter(*p, 1);
    for (auto* r : allParameters(*p))
    {
        CAPTURE(r->getParameterID());
        CHECK(a.at(r->getParameterID()) != doctest::Approx(b.at(r->getParameterID()))); // or the test proves nothing
    }
    p->selectAB(0);
    checkEveryParameter(*p, a, b);
    p->selectAB(1);
    checkEveryParameter(*p, b, b);

    // ...and a project keeps both.
    const auto state = saveState(*p);
    auto q = make();
    q->setStateInformation(state.getData(), (int) state.getSize());
    CHECK(q->abSlot() == 1);
    checkEveryParameter(*q, b, b);
    q->selectAB(0);
    checkEveryParameter(*q, a, b);
}

TEST_CASE("A and B each keep their preset: its name, whether it is a user one, and its changes")
{
    auto p = make();
    p->setCurrentProgram(1);
    set(*p, ids::glideDepth, 123.0f); // A: preset 1, changed
    p->selectAB(1);                    // B starts as a copy of A
    CHECK(p->currentPresetName() == p->getProgramName(1));
    CHECK(p->isPresetModified());
    p->setCurrentProgram(2); // B: preset 2 as it is
    CHECK_FALSE(p->isPresetModified());

    p->selectAB(0);
    CHECK(p->currentPresetName() == p->getProgramName(1));
    CHECK(p->getCurrentProgram() == 1);
    CHECK(p->isPresetModified());
    p->selectAB(1);
    CHECK(p->currentPresetName() == p->getProgramName(2));
    CHECK(p->getCurrentProgram() == 2);
    CHECK_FALSE(p->isPresetModified());

    // B as a user preset; A stays the factory one.
    const juce::String name = "glide90 test " + juce::Uuid().toString();
    REQUIRE(p->saveUserPreset(name));
    const auto file = Glide90Processor::userPresetDirectory().getChildFile(name + Glide90Processor::presetExtension());
    CHECK(p->currentPresetIsUser());
    p->selectAB(0);
    CHECK_FALSE(p->currentPresetIsUser());

    // A project keeps all of it.
    const auto state = saveState(*p);
    auto q = make();
    q->setStateInformation(state.getData(), (int) state.getSize());
    CHECK(q->abSlot() == 0);
    CHECK(q->currentPresetName() == p->getProgramName(1));
    CHECK(q->isPresetModified());
    CHECK_FALSE(q->currentPresetIsUser());
    q->selectAB(1);
    CHECK(q->currentPresetName() == name);
    CHECK(q->currentPresetIsUser());
    CHECK_FALSE(q->isPresetModified());
    p->deleteUserPreset(file);
}

TEST_CASE("a user preset keeps every parameter but INPUT and the modes")
{
    auto p = make();
    const auto saved = setEveryParameter(*p, 0);
    const juce::String name = "glide90 test " + juce::Uuid().toString();
    REQUIRE(p->saveUserPreset(name));
    const auto file = Glide90Processor::userPresetDirectory().getChildFile(name + Glide90Processor::presetExtension());
    const auto other = setEveryParameter(*p, 1);
    REQUIRE(p->loadUserPreset(file));
    checkEveryParameter(*p, saved, other);
    CHECK_FALSE(p->isPresetModified());
    p->deleteUserPreset(file);
}

TEST_CASE("a project reopens exactly as it was saved, both A/B slots included")
{
    // Changes to a preset are still changes after reopening.
    {
        auto a = make();
        set(*a, ids::glideDepth, 99.0f);
        const auto state = saveState(*a);
        auto b = make();
        b->setStateInformation(state.getData(), (int) state.getSize());
        CHECK(b->isPresetModified());
    }

    auto p = make();
    setEveryParameter(*p, 0);
    p->selectAB(1);
    p->setCurrentProgram(3);
    setEveryParameter(*p, 1);
    const sway::ShapePoint points[] { { 0.0f, 0.0f }, { 0.4f, 0.8f }, { 0.7f, 0.1f } };
    sway::Shape shape;
    shape.setPoints(points, 3);
    p->setCustomShape(shape);

    const auto first = saveState(*p);
    auto q = make();
    q->setStateInformation(first.getData(), (int) first.getSize());
    const auto second = saveState(*q);
    auto x1 = juce::AudioProcessor::getXmlFromBinary(first.getData(), (int) first.getSize());
    auto x2 = juce::AudioProcessor::getXmlFromBinary(second.getData(), (int) second.getSize());
    REQUIRE(x1 != nullptr);
    REQUIRE(x2 != nullptr);
    CHECK(x1->isEquivalentTo(x2.get(), true));
    if (!x1->isEquivalentTo(x2.get(), true))
        MESSAGE(x1->toString() << "\n---\n" << x2->toString());
}

int main(int argc, char** argv)
{
   #if JUCE_LINUX
    // On a bare X server (CI's Xvfb, no window manager) the window manager's atoms do not exist, and
    // JUCE fails with BadAtom when it sets them on a new window. Create them before JUCE looks them
    // up, on a connection kept open: the server forgets its atoms when its last client leaves.
    // These are the ones JUCE looks up only if they exist (juce_XWindowSystem_linux.cpp).
    if (auto* x11 = juce::X11Symbols::getInstance(); x11->loadAllSymbols())
        if (auto* display = x11->xOpenDisplay(nullptr)) // closed when the tests exit
            for (const char* name :
                 { "WM_PROTOCOLS", "WM_TAKE_FOCUS", "WM_DELETE_WINDOW", "WM_CHANGE_STATE", "WM_STATE", "_NET_WM_PING",
                   "_NET_WM_STATE", "_NET_WM_STATE_HIDDEN", "_NET_WM_STATE_MAXIMIZED_HORZ", "_NET_WM_STATE_MAXIMIZED_VERT",
                   "_NET_WM_STATE_FULLSCREEN", "_NET_WM_STATE_ABOVE", "_NET_WM_STATE_SKIP_TASKBAR", "_NET_WM_WINDOW_TYPE",
                   "_NET_WM_WINDOW_TYPE_NORMAL", "_NET_WM_WINDOW_TYPE_COMBO", "_NET_WM_ALLOWED_ACTIONS",
                   "_NET_WM_ACTION_CLOSE", "_NET_WM_ACTION_MINIMIZE", "_NET_WM_ACTION_RESIZE", "_NET_WM_ACTION_FULLSCREEN",
                   "_NET_WM_MOVERESIZE", "_NET_FRAME_EXTENTS", "_NET_WORKAREA", "_MOTIF_WM_HINTS" })
                x11->xInternAtom(display, name, False);
   #endif
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
    for (const char* id : { ids::gateMix, ids::glideRandom, ids::glideSmooth, ids::glideDynamics, ids::glideBar })
    {
        CAPTURE(id);
        CHECK(value(id, "50") == doctest::Approx(0.5f));
        CHECK(value(id, "50%") == doctest::Approx(0.5f));
        CHECK(value(id, "50 %") == doctest::Approx(0.5f));
        CHECK(value(id, "100%") == doctest::Approx(1.0f));
        CHECK(value(id, "12,5") == doctest::Approx(0.125f));
    }
    // Levels in dB: plain numbers are dB, "-inf" and "off" are silence (INPUT's bottom, which has none).
    for (const char* id : { ids::input, ids::output })
    {
        CAPTURE(id);
        CHECK(value(id, "-6") == doctest::Approx(-6.0f));
        CHECK(value(id, "-6 dB") == doctest::Approx(-6.0f));
        CHECK(value(id, "+3") == doctest::Approx(3.0f));
        CHECK(value(id, "3") == doctest::Approx(3.0f));
        CHECK(value(id, "-inf") == doctest::Approx(glide90::kOffDb));
        CHECK(value(id, "off") == doctest::Approx(glide90::kOffDb));
        CHECK(value(id, "-inf dB") == doctest::Approx(glide90::kOffDb));
    }
    auto shown = [&](const char* id, float v) {
        auto* param = p->state().getParameter(id);
        return param->getText(param->convertTo0to1(v), 16);
    };
    CHECK(shown(ids::output, -60.0f) == "-inf dB");
    CHECK(shown(ids::output, 0.0f) == "0.0 dB");
    CHECK(shown(ids::output, 3.0f) == "+3.0 dB");
    CHECK(shown(ids::input, -24.0f) == "-24.0 dB");
    auto* input = p->state().getParameter(ids::input);
    CHECK(input->convertFrom0to1(input->getValueForText("-inf")) == doctest::Approx(-24.0f));

    // What the host's own field sends goes through the same reading.
    auto* level = p->state().getParameter(ids::output);
    CHECK(level->convertFrom0to1(level->getValueForText("-6")) == doctest::Approx(-6.0f));

    // Text that names no value changes nothing.
    for (const char* id : { ids::output, ids::gateRoomSize, ids::gateLiveness, ids::gateLpf, ids::glideDivision })
    {
        CAPTURE(id);
        CHECK_FALSE(typed(id, "").has_value());
        CHECK_FALSE(typed(id, "abc").has_value());
    }

    // Plain numbers, with or without the shown unit; seconds and semitones when said so.
    CHECK(value(ids::gateRoomSize, "3.3") == doctest::Approx(3.3f));
    CHECK(value(ids::gatePreDelay, "25.0ms") == doctest::Approx(25.0f));
    CHECK(value(ids::gatePreDelay, "0.2 s") == doctest::Approx(200.0f));
    CHECK(value(ids::glidePeriod, "333 ms") == doctest::Approx(333.0f));
    CHECK(value(ids::glidePeriod, "1.2s") == doctest::Approx(1200.0f));
    CHECK(value(ids::glidePeriod, "1,2 сек") == doctest::Approx(1200.0f));
    CHECK(value(ids::glideDepth, "30 ct") == doctest::Approx(30.0f));
    CHECK(value(ids::glideDepth, "2 st") == doctest::Approx(200.0f));
    CHECK(value(ids::gateLiveness, "7") == doctest::Approx(7.0f));

    // LPF: kHz, Hz, the shown label in any case, THRU.
    auto lpf = [&](const char* text) {
        return p->state().getParameter(ids::gateLpf)->getText(
            p->state().getParameter(ids::gateLpf)->convertTo0to1(value(ids::gateLpf, text)), 16);
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
    CHECK(value(ids::glideMode, "trigger") == doctest::Approx((float) sway::GlideMode::Trigger));
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
    set(*p, ids::glideOn, 0.0f); // the effect alone (MIX 100 %): the two paths differ completely
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

TEST_CASE("the plugin opens as Glide Guitar, the first preset, which bends with each strike's strength")
{
    auto p = make();
    CHECK(p->currentPresetName() == "Glide Guitar");
    CHECK(p->factoryPresetNames()[0] == "Glide Guitar");
    CHECK_FALSE(p->isPresetModified());
    // Its values are the defaults: Alt-click on a knob comes back to it.
    for (auto* param : p->getParameters())
        if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*>(param))
        {
            CAPTURE(ranged->getParameterID());
            CHECK(ranged->getValue() == doctest::Approx(ranged->getDefaultValue()));
        }
    CHECK(plain(*p, ids::gateRoomSize) == doctest::Approx(20.0f));
    CHECK(plain(*p, ids::gateLiveness) == doctest::Approx(4.0f));
    CHECK(plain(*p, ids::glideDynamics) == doctest::Approx(0.6f));
    CHECK(juce::roundToInt(plain(*p, ids::glideMode)) == (int) sway::GlideMode::Trigger);
    p->setCurrentProgram(3);
    p->setCurrentProgram(0);
    CHECK_FALSE(p->isPresetModified());
    CHECK(plain(*p, ids::gateRoomSize) == doctest::Approx(20.0f));
}

TEST_CASE("A and B each keep their own undo history")
{
    auto p = make();
    auto gesture = [&](const char* id, float value) {
        auto* param = p->state().getParameter(id);
        param->beginChangeGesture();
        param->setValueNotifyingHost(param->convertTo0to1(value));
        param->endChangeGesture();
    };
    gesture(ids::glideDepth, 100.0f); // in A
    p->selectAB(1);                    // B starts as a copy of A
    CHECK_FALSE(p->undoManager().canUndo());
    gesture(ids::gateRoomSize, 12.0f); // in B
    p->undoManager().undo();
    CHECK(plain(*p, ids::gateRoomSize) == doctest::Approx(20.0f));
    CHECK(plain(*p, ids::glideDepth) == doctest::Approx(100.0f)); // B's copy of A's edit stays
    CHECK_FALSE(p->undoManager().canUndo());

    p->selectAB(0);
    REQUIRE(p->undoManager().canUndo());
    p->undoManager().undo();
    CHECK(plain(*p, ids::glideDepth) == doctest::Approx(40.0f));
    p->selectAB(1);
    CHECK(plain(*p, ids::glideDepth) == doctest::Approx(100.0f)); // A's undo left B alone
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

    gesture(ids::gateRoomSize, 12.0f);
    gesture(ids::glideDepth, 200.0f);
    REQUIRE(undo.canUndo());
    undo.undo();
    CHECK(plain(*p, ids::glideDepth) == doctest::Approx(59.0f));
    CHECK(plain(*p, ids::gateRoomSize) == doctest::Approx(12.0f));
    undo.undo();
    CHECK(plain(*p, ids::gateRoomSize) == doctest::Approx(20.0f));
    undo.redo();
    CHECK(plain(*p, ids::gateRoomSize) == doctest::Approx(12.0f));

    // Loading a preset starts this slot's history afresh; the other slot keeps its own.
    p->selectAB(1);
    gesture(ids::glideDepth, 150.0f);
    p->selectAB(0);
    p->setCurrentProgram(1);
    CHECK(plain(*p, ids::gateRoomSize) == doctest::Approx(20.0f));
    CHECK_FALSE(p->undoManager().canUndo());
    CHECK_FALSE(p->undoManager().canRedo());
    p->selectAB(1);
    CHECK(p->undoManager().canUndo());
    p->selectAB(0);

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
    const juce::String name = "glide90 test " + juce::Uuid().toString();
    REQUIRE(p->saveUserPreset(name)); // ...and is now the preset as loaded
    p->deleteUserPreset(Glide90Processor::userPresetDirectory().getChildFile(name + Glide90Processor::presetExtension()));
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

    glide90::ui::Dialog* dialog = nullptr;
    std::function<void(juce::Component&)> findDialog = [&](juce::Component& c) {
        if (auto* d = dynamic_cast<glide90::ui::Dialog*>(&c))
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
    CHECK(p->currentPresetName() == "Glide Guitar");
}

namespace
{
// A string struck every `every` seconds (the first at 0.1 s): a decaying tone with a pick click.
juce::AudioBuffer<float> strummed(double seconds, double every = 10.0)
{
    juce::AudioBuffer<float> in(2, (int) (seconds * kRate));
    uint32_t seed = 3;
    for (int i = 0; i < in.getNumSamples(); ++i)
    {
        const double t = (double) i / kRate - 0.1;
        double v = 0.0;
        if (t >= 0.0)
        {
            const double since = std::fmod(t, every);
            v = 0.3 * std::sin(2.0 * juce::MathConstants<double>::pi * 220.0 * since) * std::exp(-since / 0.4);
            if (since < 0.002)
            {
                seed = seed * 1664525u + 1013904223u;
                v += 0.6 * ((double) (seed >> 8) / 8388608.0 - 1.0);
            }
        }
        in.setSample(0, i, (float) v);
        in.setSample(1, i, (float) v);
    }
    return in;
}

juce::AudioBuffer<float> render(Glide90Processor& p, const juce::AudioBuffer<float>& in)
{
    p.prepareToPlay(kRate, kBlock);
    juce::AudioBuffer<float> out(in), block(2, kBlock);
    juce::MidiBuffer midi;
    for (int start = 0; start + kBlock <= in.getNumSamples(); start += kBlock)
    {
        for (int ch = 0; ch < 2; ++ch)
            block.copyFrom(ch, 0, in, ch, start, kBlock);
        p.processBlock(block, midi);
        for (int ch = 0; ch < 2; ++ch)
            out.copyFrom(ch, start, block, ch, 0, kBlock);
    }
    return out;
}

} // namespace

// A fresh processor (the glide's randomness starts the same in each), set up, playing `in`.
juce::AudioBuffer<float> renderFresh(const juce::AudioBuffer<float>& in, const std::function<void(Glide90Processor&)>& setUp)
{
    auto p = make();
    setUp(*p);
    return render(*p, in);
}

namespace
{
// How much of `freq` there is in `b` (left channel) over [from, to): Goertzel power per sample.
double toneEnergy(const juce::AudioBuffer<float>& b, int from, int to, double freq)
{
    const double w = 2.0 * juce::MathConstants<double>::pi * freq / kRate, coeff = 2.0 * std::cos(w);
    double s1 = 0.0, s2 = 0.0;
    for (int i = from; i < to; ++i)
    {
        const double s0 = b.getSample(0, i) + coeff * s1 - s2;
        s2 = s1;
        s1 = s0;
    }
    return (s1 * s1 + s2 * s2 - coeff * s1 * s2) / (double) (to - from);
}

// A tone that bends by -100 cents with the bar.
constexpr double kOpen = 220.0;
const double kBent = kOpen * std::pow(2.0, -100.0 / 1200.0);

// The glide as a bar that the test moves: MANUAL, 100 cents down at the top, at once.
void manualBar(Glide90Processor& p)
{
    set(p, ids::glideMode, (float) sway::GlideMode::Manual);
    set(p, ids::glideDepth, 100.0f);
    set(p, ids::glideDirection, 0.0f);
    set(p, ids::glideSmooth, 0.0f);
    set(p, ids::glideRandom, 0.0f);
}
} // namespace

TEST_CASE("the bar bends the direct guitar and the reverse gate, before it or after it; with GLIDE off nothing bends")
{
    // A steady tone, the bar held down from the start.
    juce::AudioBuffer<float> in(2, (int) (3.0 * kRate));
    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < in.getNumSamples(); ++i)
            in.setSample(ch, i, 0.3f * (float) std::sin(2.0 * juce::MathConstants<double>::pi * kOpen * i / kRate));
    const int from = (int) (1.5 * kRate), to = (int) (2.9 * kRate);
    auto bentOf = [&](float balance, glide90::GlidePosition position) {
        const auto out = renderFresh(in, [&](Glide90Processor& p) {
            manualBar(p);
            set(p, ids::glideBar, 1.0f);
            mixAt(p, balance);
            set(p, ids::glidePosition, (float) position);
        });
        return toneEnergy(out, from, to, kBent) > 50.0 * toneEnergy(out, from, to, kOpen);
    };
    for (auto position : { glide90::GlidePosition::Pre, glide90::GlidePosition::Post })
        for (float balance : { 0.0f, 1.0f })
        {
            CAPTURE((int) position);
            CAPTURE(balance);
            CHECK(bentOf(balance, position));
        }
    const auto off = renderFresh(in, [&](Glide90Processor& p) {
        manualBar(p);
        set(p, ids::glideBar, 1.0f);
        set(p, ids::glideOn, 0.0f);
        mixAt(p, 0.5f);
    });
    CHECK(toneEnergy(off, from, to, kBent) < 0.02 * toneEnergy(off, from, to, kOpen));
}

namespace
{
// What render() has processed: whole blocks (the rest is the input as it was).
int processed(const juce::AudioBuffer<float>& b)
{
    return b.getNumSamples() / kBlock * kBlock;
}

// How far `b` is from `a` scaled by `gain`, relative to that (0 = exactly the same, scaled).
double scaledError(const juce::AudioBuffer<float>& a, const juce::AudioBuffer<float>& b, double gain)
{
    double err = 0.0, ref = 0.0;
    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < processed(a); ++i)
        {
            err += std::pow(b.getSample(ch, i) - gain * a.getSample(ch, i), 2.0);
            ref += std::pow(gain * a.getSample(ch, i), 2.0);
        }
    return ref > 0.0 ? err / ref : 1.0;
}
} // namespace

TEST_CASE("OUTPUT is the last stage: it scales everything, the gate on or off")
{
    const auto in = strummed(3.0, 1.0);
    for (float gateOn : { 1.0f, 0.0f })
    {
        CAPTURE(gateOn);
        auto setUp = [gateOn](float outputDb) {
            return [gateOn, outputDb](Glide90Processor& p) {
                set(p, ids::gateOn, gateOn);
                set(p, ids::output, outputDb);
            };
        };
        const auto unity = renderFresh(in, setUp(0.0f));
        const auto lowered = renderFresh(in, setUp(-6.0f));
        CHECK(scaledError(unity, lowered, std::pow(10.0, -6.0 / 20.0)) < 1e-9);
    }
    // At its bottom, silence.
    const auto off = renderFresh(in, [](Glide90Processor& p) { set(p, ids::output, glide90::kOffDb); });
    CHECK(off.getMagnitude(0, processed(off)) == 0.0f);
}

TEST_CASE("INPUT is the first stage: everything follows it")
{
    // The direct sound, linear: 6 dB less in, 6 dB less out.
    const auto in = strummed(3.0, 1.0);
    auto setUp = [](float inputDb) {
        return [inputDb](Glide90Processor& p) {
            mixAt(p, 0.0f);
            set(p, ids::input, inputDb);
        };
    };
    const auto unity = renderFresh(in, setUp(0.0f));
    const auto lowered = renderFresh(in, setUp(-6.0f));
    // (The direct sound passes the reverse gate's 16-bit converters: not exactly linear.)
    CHECK(scaledError(unity, lowered, std::pow(10.0, -6.0 / 20.0)) < 1e-6);

    // INPUT is part of undo (as an edit of the rig), though presets and A/B leave it alone.
    auto p = make();
    auto* param = p->state().getParameter(ids::input);
    param->beginChangeGesture();
    param->setValueNotifyingHost(param->convertTo0to1(-9.0f));
    param->endChangeGesture();
    REQUIRE(p->undoManager().canUndo());
    p->undoManager().undo();
    CHECK(plain(*p, ids::input) == doctest::Approx(0.0f));
}

namespace
{
// --- Legibility: how well each piece of text stands out from what is behind it, as drawn ---

double luminance(juce::Colour c)
{
    auto channel = [](float v) { return v <= 0.04045f ? v / 12.92 : std::pow((v + 0.055) / 1.055, 2.4); };
    return 0.2126 * channel(c.getFloatRed()) + 0.7152 * channel(c.getFloatGreen()) + 0.0722 * channel(c.getFloatBlue());
}

// WCAG contrast of the text in `area` (of `image`) against its background: the background is the
// area's most common luminance (its median), the text what its strokes reach, a few pixels deep, so
// anti-aliased edges and single specks of grain do not count.
double contrastIn(const juce::Image& image, juce::Rectangle<int> area)
{
    area = area.getIntersection(image.getBounds());
    std::vector<double> l;
    l.reserve((size_t) area.getWidth() * (size_t) area.getHeight());
    for (int y = area.getY(); y < area.getBottom(); ++y)
        for (int x = area.getX(); x < area.getRight(); ++x)
            l.push_back(luminance(image.getPixelAt(x, y)));
    if (l.size() < 50)
        return 21.0;
    auto sorted = l;
    std::nth_element(sorted.begin(), sorted.begin() + (long) sorted.size() / 2, sorted.end());
    const double bg = sorted[sorted.size() / 2];
    std::vector<double> distance;
    distance.reserve(l.size());
    for (double v : l)
        distance.push_back(std::abs(v - bg));
    // The text's own colour: what its strokes reach, a few pixels deep (not a stray speck of grain).
    const size_t deep = std::max<size_t>(8, distance.size() * 3 / 1000);
    const size_t at = distance.size() - std::min(deep, distance.size());
    std::vector<size_t> order(l.size());
    std::iota(order.begin(), order.end(), 0);
    std::nth_element(order.begin(), order.begin() + (long) at, order.end(),
                     [&](size_t a, size_t b) { return distance[a] < distance[b]; });
    const double text = l[order[at]];
    return (std::max(text, bg) + 0.05) / (std::min(text, bg) + 0.05);
}

struct Reading
{
    juce::String what;
    double ratio;
    bool dimmed; // a control that does nothing in the current mode (drawn faint on purpose)
};

bool enabledHere(const juce::Component& c)
{
    for (auto* p = &c; p != nullptr; p = p->getParentComponent())
        if (!p->isEnabled())
            return false;
    return true;
}

std::vector<Reading> readText(juce::AudioProcessorEditor& editor, const juce::Image& image, float scale, bool crop)
{
    std::vector<Reading> out;
    auto measure = [&](juce::Component& c, juce::Rectangle<float> local, const juce::String& what, bool dimmed) {
        const auto inEditor = editor.getLocalArea(&c, local);
        const auto area = (inEditor * scale).getSmallestIntegerContainer();
        out.push_back({ what, contrastIn(image, area), dimmed });
        // GLIDE90_CONTRAST_CROPS=<folder>: each measured area as a picture, to check what was measured (of
// the look and state GLIDE90_CONTRAST_ONLY names, "crimson/playing").
        if (const auto crops = juce::SystemStats::getEnvironmentVariable("GLIDE90_CONTRAST_CROPS", {}); crop && crops.isNotEmpty())
        {
            juce::File file(juce::File(crops).getChildFile(juce::String((int) out.size()) + " " + what.removeCharacters("/") + ".png"));
            file.deleteFile();
            juce::FileOutputStream stream(file);
            juce::PNGImageFormat().writeImageToStream(image.getClippedImage(area.getIntersection(image.getBounds())), stream);
        }
    };
    std::function<void(juce::Component&)> walk = [&](juce::Component& c) {
        if (!c.isVisible() && &c != &editor) // the editor itself is not on the desktop here
            return;
        const bool dim = !enabledHere(c);
        const auto r = c.getLocalBounds().toFloat();
        if (auto* k = dynamic_cast<ui::Knob*>(&c))
        {
            measure(c, r.withHeight(15.0f), k->getLabel() + " (name)", dim);
            measure(c, r.withTrimmedTop(r.getHeight() - 14.0f), k->getLabel() + " (value)", dim);
        }
        else if (auto* list = dynamic_cast<ui::ChoiceList*>(&c))
        {
            measure(c, r.withHeight(15.0f), list->getLabel() + " (name)", dim);
            for (int i = 0; i < list->numRows(); ++i)
                measure(c, list->rowArea(i), list->getLabel() + " (row " + juce::String(i + 1) + ")", dim);
        }
        else if (auto* key = dynamic_cast<juce::TextButton*>(&c))
            measure(c, r.withTrimmedTop(r.getHeight() * 0.35f), "key " + key->getButtonText(), dim);
        else if (dynamic_cast<ui::GlassSelector*>(&c) != nullptr)
            measure(c, r, "selector in " + c.getParentComponent()->getComponentID() + juce::String(typeid(*c.getParentComponent()).name()).fromLastOccurrenceOf("ui", false, false), dim);
        else if (dynamic_cast<ui::ShapeDisplay*>(&c) || dynamic_cast<ui::GateDisplay*>(&c))
        {
            const auto name = juce::String(typeid(c).name()).fromLastOccurrenceOf("ui", false, false);
            measure(c, r.withHeight(juce::jmin(28.0f, r.getHeight())), name + " top line", dim);
            if (r.getHeight() > 60.0f)
                measure(c, r.withTrimmedTop(r.getHeight() - 26.0f), name + " bottom line", dim);
        }
        else if (auto* column = dynamic_cast<ui::IoColumn*>(&c))
        {
            measure(c, { 0.0f, 8.0f, r.getWidth(), 26.0f }, "IN/OUT title", dim);
            measure(c, column->scaleArea(), "IN/OUT dB scale", dim);
        }
        else if (dynamic_cast<ui::ModulePanel*>(&c) != nullptr)
        {
            measure(c, { 28.0f, 8.0f, 200.0f, 26.0f }, "module title", dim);
            measure(c, { 0.0f, (float) ui::ModulePanel::kRuleY - 7.0f, r.getWidth(), 14.0f }, "group names", dim);
        }
        else if (dynamic_cast<ui::HeaderBar*>(&c) != nullptr)
            measure(c, { 16.0f, 8.0f, 140.0f, 40.0f }, "logo", dim);
        for (auto* child : c.getChildren())
            walk(*child);
    };
    walk(editor);
    return out;
}
} // namespace

TEST_CASE("every look: text stands out from what is behind it, in every state (GLIDE90_SNAPSHOT also saves a tour)")
{
    const auto before = ui::theme().id;
    const auto snapshotPath = juce::SystemStats::getEnvironmentVariable("GLIDE90_SNAPSHOT", {});
    const bool report = juce::SystemStats::getEnvironmentVariable("GLIDE90_CONTRAST", {}).isNotEmpty();
    struct State
    {
        const char* name;
        std::function<void(Glide90Processor&)> setUp;
    };
    const std::vector<State> states {
        { "trigger", [](Glide90Processor&) {} },
        { "loop-sync",
          [](Glide90Processor& p) {
              set(p, ids::glideMode, (float) sway::GlideMode::Loop);
              set(p, ids::glideSync, 1.0f);
          } },
        { "manual", [](Glide90Processor& p) { p.loadFactoryPreset(p.factoryPresetNames().indexOf("Manual Bar")); } },
        { "all-off",
          [](Glide90Processor& p) {
              set(p, ids::glideOn, 0.0f);
              set(p, ids::glideMode, (float) sway::GlideMode::Loop);
              set(p, ids::gateOn, 0.0f);
          } },
    };
    double worstText = 21.0, worstDimmed = 21.0;
    for (int look = 0; look < ui::kThemeCount; ++look)
        for (const auto& state : states)
        {
            CAPTURE(ui::themeAt(look).name);
            CAPTURE(state.name);
            ui::setTheme(ui::themeAt(look).id);
            auto p = make();
            state.setUp(*p);
            render(*p, strummed(1.5));
            std::unique_ptr<juce::AudioProcessorEditor> editor(p->createEditor());
            juce::MessageManager::getInstance()->runDispatchLoopUntil(50);
            const float scale = 2.0f;
            const auto image = editor->createComponentSnapshot(editor->getLocalBounds(), true, scale);
            if (snapshotPath.isNotEmpty())
            {
                const juce::File given(snapshotPath);
                juce::File file(given.getSiblingFile(given.getFileNameWithoutExtension() + "-tour-" + ui::themeAt(look).key + "-"
                                                     + state.name + ".png"));
                file.deleteFile();
                juce::FileOutputStream out(file);
                REQUIRE(juce::PNGImageFormat().writeImageToStream(image, out));
            }
            const bool crop = juce::SystemStats::getEnvironmentVariable("GLIDE90_CONTRAST_ONLY", {})
                              == juce::String(ui::themeAt(look).key) + "/" + state.name;
            auto readings = readText(*editor, image, scale, crop);
            // A module switched off shows its screen faint on purpose: held to the dimmed standard.
            if (juce::String(state.name) == "all-off")
                for (auto& r : readings)
                    if (r.what.contains("Display"))
                        r.dimmed = true;
            std::sort(readings.begin(), readings.end(), [](const Reading& a, const Reading& b) { return a.ratio < b.ratio; });
            for (const auto& r : readings)
            {
                // WCAG AA for text (4.5:1, less a little for how it is measured); what is dimmed on
                // purpose still at 3:1. Measured on Linux's font rendering; Windows and macOS draw
                // small text thinner or bolder, so there the readings are reported, not required.
                CAPTURE(r.what);
               #if JUCE_LINUX
                CHECK(r.ratio >= (r.dimmed ? 3.0 : 4.4));
               #else
                WARN(r.ratio >= (r.dimmed ? 3.0 : 4.4));
               #endif
                (r.dimmed ? worstDimmed : worstText) = std::min(r.dimmed ? worstDimmed : worstText, r.ratio);
                if (report && (r.dimmed ? r.ratio < 2.5 : r.ratio < 4.5))
                {
                    const juce::String line = juce::String(ui::themeAt(look).name) + " | " + state.name + " | " + r.what
                                              + (r.dimmed ? " [dimmed]" : "") + " | " + juce::String(r.ratio, 2);
                    MESSAGE(line.toStdString());
                }
            }
        }
    MESSAGE("lowest contrast: text " << worstText << ", dimmed controls " << worstDimmed);
    ui::setTheme(before);
}

namespace
{

// `in` through a fresh processor set up by `setUp`, block by block; `perBlock` runs before each block.
juce::AudioBuffer<float> renderBlocks(const juce::AudioBuffer<float>& in, const std::function<void(Glide90Processor&)>& setUp,
                                const std::function<void(Glide90Processor&, int sample)>& perBlock = {})
{
    auto p = make();
    setUp(*p);
    p->reset();
    juce::AudioBuffer<float> out(in), block(2, kBlock);
    juce::MidiBuffer midi;
    for (int start = 0; start + kBlock <= in.getNumSamples(); start += kBlock)
    {
        if (perBlock)
            perBlock(*p, start);
        for (int ch = 0; ch < 2; ++ch)
            block.copyFrom(ch, 0, in, ch, start, kBlock);
        p->processBlock(block, midi);
        for (int ch = 0; ch < 2; ++ch)
            out.copyFrom(ch, start, block, ch, 0, kBlock);
    }
    return out;
}

juce::AudioBuffer<float> tone(double seconds, double freq)
{
    juce::AudioBuffer<float> in(2, (int) (seconds * kRate));
    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < in.getNumSamples(); ++i)
            in.setSample(ch, i, 0.3f * (float) std::sin(2.0 * juce::MathConstants<double>::pi * freq * i / kRate));
    return in;
}

template <typename T>
T* find(juce::Component& root)
{
    if (auto* t = dynamic_cast<T*>(&root))
        return t;
    for (auto* child : root.getChildren())
        if (auto* t = find<T>(*child))
            return t;
    return nullptr;
}

// `visit` on `root` and everything in it.
void forEachIn(juce::Component& root, const std::function<void(juce::Component&)>& visit)
{
    visit(root);
    for (auto* child : root.getChildren())
        forEachIn(*child, visit);
}

juce::Button* findKey(juce::Component& root, const juce::String& legend)
{
    juce::Button* key = nullptr;
    forEachIn(root, [&](juce::Component& c) {
        if (auto* b = dynamic_cast<juce::Button*>(&c); b != nullptr && b->getButtonText() == legend)
            key = b;
    });
    return key;
}

// The knob called `label` that shows now (RATE, as time or as a note value).
ui::Knob* shownKnob(juce::Component& root, const juce::String& label)
{
    ui::Knob* found = nullptr;
    forEachIn(root, [&](juce::Component& c) {
        if (auto* k = dynamic_cast<ui::Knob*>(&c); k != nullptr && k->getLabel() == label && k->isVisible())
            found = k;
    });
    return found;
}

// Lets the panels see what changed.
void settle()
{
    juce::MessageManager::getInstance()->runDispatchLoopUntil(200);
}

} // namespace

TEST_CASE("GLIDE90 is GLIDE and the reverse gate: no HOLD, no ROUTING, no DRY or gate LEVEL, no source to choose")
{
    auto p = make();
    juce::StringArray params;
    for (auto* param : p->getParameters())
        if (auto* r = dynamic_cast<juce::RangedAudioParameter*>(param))
            params.add(r->getParameterID());
    for (const auto& id : params)
    {
        CAPTURE(id);
        CHECK_FALSE(id.startsWith("hold_"));
    }
    for (const char* id : { "routing", "dry_level", "gate_level", "glide_trigger_source", "glide_manual_source" })
        CHECK_FALSE(params.contains(id));
    for (const char* id : { ids::gateMix, ids::glidePosition, ids::pickSensitivity, ids::gateOn, ids::glideOn })
        CHECK(params.contains(id));
    // The defaults: all effect, GLIDE before the gate.
    CHECK(plain(*p, ids::gateMix) == doctest::Approx(1.0f));
    CHECK(plain(*p, ids::glidePosition) == doctest::Approx((float) glide90::GlidePosition::Pre));
    // GLIDE listens to the pick and to its own BAR: no MIDI.
    CHECK_FALSE(p->acceptsMidi());
}

TEST_CASE("the presets: GLIDE and the reverse gate only, each in the order and at the MIX it needs")
{
    auto p = make();
    const auto names = p->factoryPresetNames();
    CHECK(names[0] == "Glide Guitar");
    for (const char* held : { "Held Chord", "Sampled Feedback", "Frozen Wall", "Smear Trail" })
        CHECK_FALSE(names.contains(held));
    CHECK(p->currentPresetName() == "Glide Guitar");
    CHECK_FALSE(p->isPresetModified());

    p->loadFactoryPreset(names.indexOf("Wall")); // GLIDE after the gate
    CHECK(plain(*p, ids::glidePosition) == doctest::Approx((float) glide90::GlidePosition::Post));
    p->loadFactoryPreset(names.indexOf("Slow Drift")); // MIX 45 %
    CHECK(plain(*p, ids::gateMix) == doctest::Approx(0.45f).epsilon(0.02));
    CHECK(plain(*p, ids::glidePosition) == doctest::Approx((float) glide90::GlidePosition::Pre));
    CHECK(p->userPresetDirectory().getFileName() == "Presets");
}

TEST_CASE("MODE is LOOP, TRIGGER or MANUAL, and SYNC turns RATE into a note value in the first two")
{
    auto p = make();
    const auto& modes = dynamic_cast<juce::AudioParameterChoice&>(*p->state().getParameter(ids::glideMode)).choices;
    CHECK(modes == juce::StringArray { "LOOP", "TRIGGER", "MANUAL" });
    CHECK(juce::roundToInt(plain(*p, ids::glideMode)) == (int) sway::GlideMode::Trigger);
    CHECK(plain(*p, ids::glideSync) == 0.0f);

    std::unique_ptr<juce::AudioProcessorEditor> editor(p->createEditor());
    auto* sync = findKey(*editor, "SYNC");
    REQUIRE(sync != nullptr);
    auto rate = [&] { return shownKnob(*editor, "RATE"); };

    for (auto mode : { sway::GlideMode::Loop, sway::GlideMode::Trigger })
    {
        CAPTURE((int) mode);
        set(*p, ids::glideMode, (float) mode);
        set(*p, ids::glideSync, 0.0f);
        settle();
        REQUIRE(rate() != nullptr);
        CHECK(sync->isVisible());
        CHECK(rate()->getTooltip().contains("larger = slower"));
        set(*p, ids::glideSync, 1.0f);
        settle();
        REQUIRE(rate() != nullptr);
        CHECK(rate()->getTooltip().contains("note value"));
    }

    // MANUAL: the BAR in RATE's slot, nothing to sync; it remembers SYNC for the way back.
    set(*p, ids::glideMode, (float) sway::GlideMode::Manual);
    settle();
    CHECK(rate() == nullptr);
    CHECK(shownKnob(*editor, "BAR") != nullptr);
    CHECK_FALSE(sync->isVisible());
    CHECK(plain(*p, ids::glideSync) == 1.0f);
}

TEST_CASE("SYNC is part of the sound: presets set it, a preset that does not resets it, a project keeps it")
{
    auto a = make();
    const auto names = a->factoryPresetNames();
    CHECK_FALSE(names.contains("Pitch-Bend Bar"));
    CHECK_FALSE(names.contains("Expression Bar"));
    a->loadFactoryPreset(names.indexOf("Manual Bar"));
    CHECK(juce::roundToInt(plain(*a, ids::glideMode)) == (int) sway::GlideMode::Manual);

    a->loadFactoryPreset(names.indexOf("Seasick (Sync)"));
    CHECK(juce::roundToInt(plain(*a, ids::glideMode)) == (int) sway::GlideMode::Loop);
    CHECK(plain(*a, ids::glideSync) == 1.0f);
    juce::MemoryBlock state;
    a->getStateInformation(state);
    auto b = make();
    b->setStateInformation(state.getData(), (int) state.getSize());
    CHECK(plain(*b, ids::glideSync) == 1.0f);

    a->loadFactoryPreset(names.indexOf("Glide Guitar"));
    CHECK(plain(*a, ids::glideSync) == 0.0f);
}

TEST_CASE("the window: GLIDE fills its four columns with no gaps, SYNC in the corner of RATE")
{
    auto p = make();
    std::unique_ptr<juce::AudioProcessorEditor> editor(p->createEditor());
    CHECK(glide90::Glide90Editor::kWidth == 904);
    auto* glide = find<ui::GlidePanel>(*editor);
    REQUIRE(glide != nullptr);

    // Every slot of the two rows holds one control.
    std::set<std::pair<int, int>> slots;
    forEachIn(*glide, [&](juce::Component& c) {
        const bool control = dynamic_cast<ui::Knob*>(&c) != nullptr || dynamic_cast<ui::ChoiceList*>(&c) != nullptr;
        if (control && c.getParentComponent() != nullptr && c.isVisible() && c.getBounds().getWidth() == ui::Knob::kColumn)
        {
            const auto where = glide->getLocalArea(c.getParentComponent(), c.getBounds());
            slots.insert({ where.getX(), where.getY() });
        }
    });
    CHECK(slots.size() == 8);
    for (int column = 0; column < 4; ++column)
        for (int row : { ui::ModulePanel::kRow1, ui::ModulePanel::kRow2 })
            CHECK(slots.count({ ui::ModulePanel::columnX(column), row }) == 1);

    auto* sync = findKey(*glide, "SYNC");
    auto* rate = shownKnob(*glide, "RATE");
    REQUIRE(sync != nullptr);
    REQUIRE(rate != nullptr);
    // Over RATE's name line, off its dial.
    CHECK(sync->getBounds().getRight() <= rate->getRight());
    CHECK_FALSE(rate->dialArea().toNearestInt().translated(rate->getX(), rate->getY()).intersects(sync->getBounds()));
}

TEST_CASE("a tooltip says what the control cannot: not its own name, nor anything GLIDE90 does not have")
{
    auto p = make();
    std::unique_ptr<juce::AudioProcessorEditor> editor(p->createEditor());
    auto check = [&](juce::Component& c) {
        auto* client = dynamic_cast<juce::TooltipClient*>(&c);
        if (client == nullptr)
            return;
        const auto tip = client->getTooltip();
        if (tip.isEmpty())
            return;
        CAPTURE(tip);
        for (const char* gone : { "MIDI", "SOURCE", "HOLD", "FREEZE", "FREE ", "PITCH BEND", "pitch bend", "mod wheel" })
            CHECK_FALSE(tip.contains(gone));
        CHECK(tip.length() <= 170);
        if (auto* k = dynamic_cast<ui::Knob*>(&c))
            CHECK_FALSE(tip.equalsIgnoreCase(k->getLabel()));
        if (auto* l = dynamic_cast<ui::ChoiceList*>(&c))
            CHECK_FALSE(tip.equalsIgnoreCase(l->getLabel()));
        // A key with an icon has no legend: its tooltip may name it.
        if (auto* b = dynamic_cast<juce::Button*>(&c); b != nullptr && !b->getProperties().contains(ui::PanelLookAndFeel::kIcon))
            CHECK_FALSE(tip.equalsIgnoreCase(b->getButtonText()));
    };
    for (auto mode : { sway::GlideMode::Loop, sway::GlideMode::Trigger, sway::GlideMode::Manual })
    {
        set(*p, ids::glideMode, (float) mode);
        settle();
        forEachIn(*editor, check);
    }

    // Short tooltips for PRE-DELAY and OUTPUT; none where the control explains itself.
    REQUIRE(shownKnob(*editor, "PRE-DELAY") != nullptr);
    CHECK(shownKnob(*editor, "PRE-DELAY")->getTooltip().isNotEmpty());
    REQUIRE(shownKnob(*editor, "OUTPUT") != nullptr);
    CHECK(shownKnob(*editor, "OUTPUT")->getTooltip().isNotEmpty());
    REQUIRE(findKey(*editor, "SETUP") != nullptr);
    CHECK(findKey(*editor, "SETUP")->getTooltip().isEmpty());
    // The shape screen: only where a shape can be drawn.
    auto* screen = find<ui::ShapeDisplay>(*editor);
    REQUIRE(screen != nullptr);
    CHECK(screen->getTooltip().isEmpty()); // MANUAL, from the loop above
    set(*p, ids::glideMode, (float) sway::GlideMode::Loop);
    CHECK(screen->getTooltip().isNotEmpty());
}

TEST_CASE("MIX: 0 is the direct guitar alone, 1 the reverse gate alone")
{
    const auto in = tone(2.0, 220.0);
    auto at = [&](float mix, bool gateOn) {
        return renderBlocks(in, [=](Glide90Processor& p) {
            set(p, ids::glideOn, 0.0f);
            set(p, ids::gateMix, mix);
            set(p, ids::gateOn, gateOn ? 1.0f : 0.0f);
        });
    };
    const int from = (int) (1.0 * kRate), to = (int) (1.9 * kRate);
    const auto direct = at(0.0f, true), effect = at(1.0f, true), bypassed = at(1.0f, false);
    const double tone = toneEnergy(direct, from, to, 220.0);
    REQUIRE(tone > 0.01);
    // The gate off: the direct guitar at full, whatever MIX says (the unit's analog bypass).
    CHECK(toneEnergy(bypassed, from, to, 220.0) == doctest::Approx(tone).epsilon(0.1));
    // Half way: both, each at half.
    const auto half = at(0.5f, true);
    juce::AudioBuffer<float> sum(direct);
    for (int ch = 0; ch < 2; ++ch)
    {
        sum.applyGain(ch, 0, sum.getNumSamples(), 0.5f);
        sum.addFrom(ch, 0, effect, ch, 0, effect.getNumSamples(), 0.5f);
    }
    double err = 0.0, ref = 0.0;
    for (int i = 0; i < (in.getNumSamples() / kBlock) * kBlock; ++i)
    {
        err += std::pow(half.getSample(0, i) - sum.getSample(0, i), 2.0);
        ref += std::pow(sum.getSample(0, i), 2.0);
    }
    CHECK(err / ref < 1e-4); // the unit's 16 bits round the two apart
}

TEST_CASE("the modules' order: GLIDE after the gate bends the whole tail at once, before it the reflections repeat the move")
{
    // A steady tone into the gate alone; at 2 s the bar goes down.
    const auto in = tone(4.0, 220.0);
    const double open = 220.0, bent = 220.0 * std::pow(2.0, -100.0 / 1200.0);
    const int move = (int) (2.0 * kRate);
    auto run = [&](glide90::GlidePosition position) {
        int heard = 0;
        const auto out = renderBlocks(
            in,
            [&](Glide90Processor& p) {
                manualBar(p);
                set(p, ids::glidePosition, (float) position);
                heard = move + p.getLatencySamples();
            },
            [&](Glide90Processor& p, int sample) {
                if (sample >= move)
                    set(p, ids::glideBar, 1.0f);
            });
        const int from = heard + (int) (0.1 * kRate), to = heard + (int) (0.6 * kRate);
        return toneEnergy(out, from, to, open) / toneEnergy(out, from, to, bent);
    };
    const double after = run(glide90::GlidePosition::Post), before = run(glide90::GlidePosition::Pre);
    MESSAGE("old pitch after the move: GLIDE after the gate " << after << ", before it " << before);
    CHECK(after < 0.02);
    CHECK(before > 0.1);
}

TEST_CASE("the window: dragging a module by its title bar to the other place turns the order round")
{
    auto p = make();
    std::unique_ptr<juce::AudioProcessorEditor> editor(p->createEditor());
    auto* glide = find<ui::GlidePanel>(*editor);
    auto* gate = find<ui::GatePanel>(*editor);
    REQUIRE(glide != nullptr);
    REQUIRE(gate != nullptr);
    // PICK SENS is GLIDE's, in its PICK group.
    auto* pickSense = find<ui::PickSense>(*editor);
    REQUIRE(pickSense != nullptr);
    CHECK(pickSense->getParentComponent() == glide);
    CHECK(glide->isDraggable());
    CHECK(gate->isDraggable());
    CHECK(editor->getHeight() > 0);

    CHECK(glide->getX() < gate->getX());
    // GLIDE dragged past the gate.
    glide->onDragStart();
    glide->onDrag((float) (gate->getRight() - glide->getRight()));
    glide->onDragEnd(false);
    CHECK(plain(*p, ids::glidePosition) == doctest::Approx((float) glide90::GlidePosition::Post));
    juce::MessageManager::getInstance()->runDispatchLoopUntil(100);
    CHECK(gate->getX() < glide->getX());
    // One step to undo.
    p->undoManager().undo();
    CHECK(plain(*p, ids::glidePosition) == doctest::Approx((float) glide90::GlidePosition::Pre));
    juce::MessageManager::getInstance()->runDispatchLoopUntil(100);
    CHECK(glide->getX() < gate->getX());
    // A drag that does not get far enough, or Escape, changes nothing.
    gate->onDragStart();
    gate->onDrag(-30.0f);
    gate->onDragEnd(false);
    gate->onDragStart();
    gate->onDrag((float) (glide->getX() - gate->getX()));
    gate->onDragEnd(true);
    CHECK(plain(*p, ids::glidePosition) == doctest::Approx((float) glide90::GlidePosition::Pre));

    if (const auto path = juce::SystemStats::getEnvironmentVariable("GLIDE90_SNAPSHOT", {}); path.isNotEmpty())
    {
        for (auto position : { glide90::GlidePosition::Pre, glide90::GlidePosition::Post })
        {
            set(*p, ids::glidePosition, (float) position);
            juce::MessageManager::getInstance()->runDispatchLoopUntil(200);
            const auto image = editor->createComponentSnapshot(editor->getLocalBounds(), true, 2.0f);
            juce::File file(juce::File(path).getSiblingFile(juce::File(path).getFileNameWithoutExtension()
                                                            + (position == glide90::GlidePosition::Pre ? "-window.png" : "-swapped.png")));
            file.deleteFile();
            juce::FileOutputStream out(file);
            REQUIRE(juce::PNGImageFormat().writeImageToStream(image, out));
        }
        // GLIDE's modes, for looking at.
        set(*p, ids::glidePosition, (float) glide90::GlidePosition::Pre);
        const struct
        {
            const char* name;
            sway::GlideMode mode;
            bool sync;
        } states[] { { "loop", sway::GlideMode::Loop, false },
                     { "loop-sync", sway::GlideMode::Loop, true },
                     { "trigger-sync", sway::GlideMode::Trigger, true },
                     { "manual", sway::GlideMode::Manual, false } };
        for (const auto& state : states)
        {
            set(*p, ids::glideMode, (float) state.mode);
            set(*p, ids::glideSync, state.sync ? 1.0f : 0.0f);
            juce::MessageManager::getInstance()->runDispatchLoopUntil(300);
            const auto image = editor->createComponentSnapshot(editor->getLocalBounds(), true, 2.0f);
            juce::File file(juce::File(path).getSiblingFile(juce::File(path).getFileNameWithoutExtension() + "-" + state.name + ".png"));
            file.deleteFile();
            juce::FileOutputStream out(file);
            REQUIRE(juce::PNGImageFormat().writeImageToStream(image, out));
        }
    }
}

TEST_CASE("MIX and the modules' order reopen with the project")
{
    auto a = make();
    set(*a, ids::gateMix, 0.3f);
    set(*a, ids::glidePosition, (float) glide90::GlidePosition::Post);
    juce::MemoryBlock state;
    a->getStateInformation(state);
    auto b = make();
    b->setStateInformation(state.getData(), (int) state.getSize());
    CHECK(plain(*b, ids::gateMix) == doctest::Approx(0.3f));
    CHECK(plain(*b, ids::glidePosition) == doctest::Approx((float) glide90::GlidePosition::Post));
}
