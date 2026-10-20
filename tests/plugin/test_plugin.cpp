// Processor-level tests: presets, state, latency and host bypass of the Glide90 plugin, and the editor.
#define DOCTEST_CONFIG_IMPLEMENT
#define JUCE_GUI_BASICS_INCLUDE_XHEADERS 1 // Linux: the X11 calls in main()
#include <doctest/doctest.h>

#include "PluginEditor.h"
#include "PluginProcessor.h"
#include "ui/Controls.h"
#include "ui/Dialog.h"
#include "ui/GatePanel.h"
#include "ui/GlidePanel.h"
#include "ui/HoldPanel.h"
#include "ui/PanelLookAndFeel.h"

#include <juce_events/juce_events.h>
#include <map>
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

TEST_CASE("the reverb is the reverse gate, and a state keeps its edits")
{
    auto a = make();
    REQUIRE(a->state().getParameter("er_type") == nullptr);
    REQUIRE(a->state().getParameter("memory") == nullptr);
    set(*a, ids::erRoomSize, 7.7f);
    set(*a, ids::erLiveness, 2.0f);
    const auto state = saveState(*a);

    auto b = make();
    b->setStateInformation(state.getData(), (int) state.getSize());
    CHECK(plain(*b, ids::erRoomSize) == doctest::Approx(7.7f));
    CHECK(plain(*b, ids::erLiveness) == doctest::Approx(2.0f));
}

TEST_CASE("a project saved with the SPX90 memories opens with its reverse gate settings")
{
    auto a = make();
    set(*a, ids::erRoomSize, 12.0f);
    set(*a, ids::erDelay, 80.0f);
    const auto block = saveState(*a);
    auto xml = juce::AudioProcessor::getXmlFromBinary(block.getData(), (int) block.getSize());
    REQUIRE(xml != nullptr);
    // What an older version saved besides: the memory number, the type, the memories, the program.
    for (auto [id, value] : { std::pair<const char*, double> { "memory", 16.0 }, { "er_type", 1.0 } })
    {
        auto* param = xml->createNewChildElement("PARAM");
        param->setAttribute("id", id);
        param->setAttribute("value", value);
    }
    xml->createNewChildElement("MEMORIES")->createNewChildElement("MEMORY")->setAttribute("number", 31);
    xml->setAttribute("algorithm", 16);
    xml->setAttribute("loadedMemory", 16);
    // ...and not yet: the preset as loaded, so what it opens with counts as unchanged.
    xml->deleteAllChildElementsWithTagName("BASELINE");
    juce::MemoryBlock old;
    juce::AudioProcessor::copyXmlToBinary(*xml, old);

    auto b = make();
    b->setStateInformation(old.getData(), (int) old.getSize());
    CHECK(plain(*b, ids::erRoomSize) == doctest::Approx(12.0f));
    CHECK(plain(*b, ids::erDelay) == doctest::Approx(80.0f));
    CHECK_FALSE(b->isPresetModified());
    // Saved again, nothing of the old memories is left.
    const auto again = saveState(*b);
    auto saved = juce::AudioProcessor::getXmlFromBinary(again.getData(), (int) again.getSize());
    REQUIRE(saved != nullptr);
    CHECK(saved->getChildByName("MEMORIES") == nullptr);
    CHECK(saved->getChildByAttribute("id", "memory") == nullptr);
    CHECK(saved->getChildByAttribute("id", "er_type") == nullptr);
    CHECK_FALSE(saved->hasAttribute("algorithm"));
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
    CHECK(plain(*p, ids::erRoomSize) == doctest::Approx(20.0f));
    CHECK(plain(*p, ids::erLiveness) == doctest::Approx(10.0f));
    CHECK(plain(*p, ids::glideOn) > 0.5f);
    CHECK(juce::roundToInt(plain(*p, ids::glideMode)) == (int) sway::GlideMode::Trigger);

    // A preset leaves the level switches alone and resets what it does not set, the INPUT knob too.
    set(*p, ids::inputSwitch, 1.0f);
    set(*p, ids::input, 6.0f);
    set(*p, ids::glideTwinLevel, 0.9f);
    p->setCurrentProgram(1);
    CHECK(plain(*p, ids::inputSwitch) == doctest::Approx(1.0f));
    CHECK(plain(*p, ids::input) == doctest::Approx(8.0f));
    CHECK(plain(*p, ids::glideTwinLevel) == doctest::Approx(0.0f));
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
            CHECK(plain(*p, ids::erRoomSize) == doctest::Approx(20.0f));
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
    set(*b, ids::glideMix, 0.0f);
    b->setStateInformation(old.getData(), (int) old.getSize());
    CHECK(plain(*b, ids::glideDepth) == doctest::Approx(40.0f));
    CHECK(plain(*b, ids::glideMix) == doctest::Approx(1.0f));
    CHECK(plain(*b, ids::glideOn) > 0.5f);
}

TEST_CASE("the editor builds and lays out in every look; GLIDE90_SNAPSHOT=<file.png> saves a picture of each")
{
    namespace ui = glide90::ui;
    const auto before = ui::theme().id;
    for (int look = 0; look < ui::kThemeCount; ++look)
    {
    CAPTURE(ui::themeAt(look).name);
    ui::setTheme(ui::themeAt(look).id);
    auto p = make();
    p->setCurrentProgram(p->factoryPresetNames().indexOf("Two-String Unison"));
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
        const juce::String tip = "Spacing of the reflections. At 20 the reverse gate swells for over a second";
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
    namespace ui = glide90::ui;
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

TEST_CASE("dragging a module by its title bar to the other place turns the order round")
{
    auto p = make();
    std::unique_ptr<juce::AudioProcessorEditor> editor(p->createEditor());
    glide90::ui::GlidePanel* glidePanel = nullptr;
    glide90::ui::GatePanel* gatePanel = nullptr;
    std::function<void(juce::Component&)> find = [&](juce::Component& c) {
        if (auto* g = dynamic_cast<glide90::ui::GlidePanel*>(&c))
            glidePanel = g;
        if (auto* g = dynamic_cast<glide90::ui::GatePanel*>(&c))
            gatePanel = g;
        for (auto* child : c.getChildren())
            find(*child);
    };
    find(*editor);
    REQUIRE(glidePanel != nullptr);
    REQUIRE(gatePanel != nullptr);
    auto settle = [] { juce::MessageManager::getInstance()->runDispatchLoopUntil(400); }; // the slides
    auto glideFirst = [&] { return juce::roundToInt(plain(*p, ids::glidePosition)) == (int) glide90::GlidePosition::Pre; };
    auto drag = [&](glide90::ui::ModulePanel& m, float dx, bool cancel) {
        m.onDragStart();
        for (int step = 1; step <= 10; ++step)
            m.onDrag(dx * (float) step / 10.0f);
        m.onDragEnd(cancel);
        settle();
    };
    REQUIRE(glideFirst());
    CHECK(glidePanel->getX() < gatePanel->getX());

    if (const auto path = juce::SystemStats::getEnvironmentVariable("GLIDE90_SNAPSHOT", {}); path.isNotEmpty())
    {
        // Half way: the glide lifted and past the middle, the gate gone to the left, the landing marked.
        glidePanel->onDragStart();
        glidePanel->onDrag(220.0f);
        settle();
        const auto image = editor->createComponentSnapshot(editor->getLocalBounds(), true, 2.0f);
        juce::File file(juce::File(path).getSiblingFile(juce::File(path).getFileNameWithoutExtension() + "-drag.png"));
        file.deleteFile();
        juce::FileOutputStream out(file);
        REQUIRE(juce::PNGImageFormat().writeImageToStream(image, out));
        glidePanel->onDragEnd(true);
        settle();
    }

    // Not far enough: back where it was.
    const int glideX = glidePanel->getX();
    drag(*glidePanel, 60.0f, false);
    CHECK(glideFirst());
    CHECK(glidePanel->getX() == glideX);

    // Past the gate: the gate plays first now, and both are in their new places.
    drag(*glidePanel, 400.0f, false);
    CHECK_FALSE(glideFirst());
    CHECK(gatePanel->getX() < glidePanel->getX());
    CHECK(gatePanel->getY() == glidePanel->getY()); // nothing left raised

    // Escape: nothing changes.
    drag(*glidePanel, -400.0f, true);
    CHECK_FALSE(glideFirst());
    CHECK(gatePanel->getX() < glidePanel->getX());

    // One step to undo.
    p->undoManager().undo();
    settle();
    CHECK(glideFirst());
    CHECK(glidePanel->getX() < gatePanel->getX());
}

TEST_CASE("changing the order of the glide and the reverb does not click")
{
    // The largest sample-to-sample step of a sine through the plugin, with the order flipped twice
    // or not at all: the flips may not add a jump of their own.
    auto run = [](bool flip) {
        auto p = make();
        juce::AudioBuffer<float> buffer(2, kBlock);
        juce::MidiBuffer midi;
        float previous = 0.0f, worst = 0.0f;
        for (int b = 0; b < 300; ++b)
        {
            if (flip && (b == 100 || b == 200))
                set(*p, ids::glidePosition, b == 100 ? 1.0f : 0.0f);
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
        }
        return worst;
    };
    const float steady = run(false), flipped = run(true);
    MESSAGE("largest step " << flipped << ", without flips " << steady);
    CHECK(flipped <= steady * 1.1f + 0.005f);
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

    // The reverse gate's VERB MIX knob (BALANCE).
    glide90::ui::Knob* knob = nullptr;
    std::function<void(juce::Component&)> find = [&](juce::Component& c) {
        if (auto* k = dynamic_cast<glide90::ui::Knob*>(&c); k != nullptr && k->getTooltip().startsWith("0 = dry only"))
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
    fieldOf()->setText("50");

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
        CHECK(plain(*p, ids::balance) == doctest::Approx(0.5f));

        // Text that names no value leaves it alone; Escape-like cancel is the same path without applying.
        knob->mouseDoubleClick(event(juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier), 2));
        REQUIRE(fieldOf() != nullptr);
        fieldOf()->setText("abc");
        peer->handleMouseEvent(juce::MouseInputSource::InputSourceType::mouse, spot,
                               juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier), 1.0f, 0.0f, t + 1000);
        peer->handleMouseEvent(juce::MouseInputSource::InputSourceType::mouse, spot, juce::ModifierKeys(), 1.0f, 0.0f, t + 1020);
        juce::MessageManager::getInstance()->runDispatchLoopUntil(100);
        CHECK(fieldOf() == nullptr);
        CHECK(plain(*p, ids::balance) == doctest::Approx(0.5f));
    }
    else
    {
        MESSAGE("the test window is not on top at the click point here: click part skipped");
    }

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

TEST_CASE("input mode: new instances are stereo, older projects open in mono")
{
    auto a = make();
    CHECK(juce::roundToInt(plain(*a, ids::inputMode)) == (int) glide90::InputMode::Stereo);

    // A project from before the input had a mode: it summed its inputs, and still does.
    auto xml = juce::AudioProcessor::getXmlFromBinary(saveState(*a).getData(), (int) saveState(*a).getSize());
    REQUIRE(xml != nullptr);
    for (auto* child = xml->getFirstChildElement(); child != nullptr; child = child->getNextElement())
        if (child->getStringAttribute("id") == ids::inputMode)
        {
            xml->removeChildElement(child, true);
            break;
        }
    juce::MemoryBlock old;
    juce::AudioProcessor::copyXmlToBinary(*xml, old);
    auto b = make();
    b->setStateInformation(old.getData(), (int) old.getSize());
    CHECK(juce::roundToInt(plain(*b, ids::inputMode)) == (int) glide90::InputMode::Mono);
}

namespace
{
// The plugin's output for `in` (two channels), glide off, at `balance`, in `inputMode`.
juce::AudioBuffer<float> renderInput(const juce::AudioBuffer<float>& in, float balance, glide90::InputMode inputMode,
                                     std::function<void(Glide90Processor&, int block)> perBlock = {})
{
    auto p = make();
    set(*p, ids::glideOn, 0.0f);
    set(*p, ids::balance, balance);
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
// without a test of its own. Only the level switches belong to the instance rather than the sound.

namespace
{
bool isInstanceParameter(const juce::String& id)
{
    return id == ids::inputSwitch || id == ids::outputSwitch || id == ids::holdLatch;
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

// Every parameter as `sound` has it, the level switches as `instance` has them.
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

TEST_CASE("A and B each keep every parameter but the level switches and FREEZE, which they share")
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

TEST_CASE("a user preset keeps every parameter but the level switches and FREEZE")
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
    CHECK(plain(*p, ids::erRoomSize) == doctest::Approx(20.0f));
    CHECK(plain(*p, ids::erLiveness) == doctest::Approx(4.0f));
    CHECK(plain(*p, ids::glideDynamics) == doctest::Approx(0.6f));
    CHECK(juce::roundToInt(plain(*p, ids::glideMode)) == (int) sway::GlideMode::Trigger);
    p->setCurrentProgram(3);
    p->setCurrentProgram(0);
    CHECK_FALSE(p->isPresetModified());
    CHECK(plain(*p, ids::erRoomSize) == doctest::Approx(20.0f));
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
    gesture(ids::erRoomSize, 12.0f); // in B
    p->undoManager().undo();
    CHECK(plain(*p, ids::erRoomSize) == doctest::Approx(20.0f));
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

    gesture(ids::erRoomSize, 12.0f);
    gesture(ids::glideDepth, 200.0f);
    REQUIRE(undo.canUndo());
    undo.undo();
    CHECK(plain(*p, ids::glideDepth) == doctest::Approx(59.0f));
    CHECK(plain(*p, ids::erRoomSize) == doctest::Approx(12.0f));
    undo.undo();
    CHECK(plain(*p, ids::erRoomSize) == doctest::Approx(20.0f));
    undo.redo();
    CHECK(plain(*p, ids::erRoomSize) == doctest::Approx(12.0f));

    // Loading a preset starts this slot's history afresh; the other slot keeps its own.
    p->selectAB(1);
    gesture(ids::glideDepth, 150.0f);
    p->selectAB(0);
    p->setCurrentProgram(1);
    CHECK(plain(*p, ids::erRoomSize) == doctest::Approx(20.0f));
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

double difference(const juce::AudioBuffer<float>& a, const juce::AudioBuffer<float>& b, int from)
{
    double acc = 0.0;
    for (int ch = 0; ch < 2; ++ch)
        for (int i = from; i < a.getNumSamples(); ++i)
            acc += std::pow((double) a.getSample(ch, i) - b.getSample(ch, i), 2.0);
    return acc;
}
} // namespace

// A fresh processor (the glide's randomness starts the same in each), set up, playing `in`.
juce::AudioBuffer<float> renderFresh(const juce::AudioBuffer<float>& in, const std::function<void(Glide90Processor&)>& setUp)
{
    auto p = make();
    setUp(*p);
    return render(*p, in);
}

TEST_CASE("HOLD is off in new instances and old projects, and while off its controls change nothing")
{
    CHECK(plain(*make(), ids::holdOn) < 0.5f);
    const auto in = strummed(3.0, 1.0);
    const auto before = renderFresh(in, [](Glide90Processor&) {});
    const auto after = renderFresh(in, [](Glide90Processor& p) {
        for (const char* id : { ids::holdMode, ids::holdLatch, ids::holdFollow, ids::holdSize, ids::holdDensity,
                                ids::holdSmear, ids::holdSpread, ids::holdReverse, ids::holdDuck, ids::holdDecay,
                                ids::holdLevel, ids::holdFeed, ids::holdCharacter, ids::holdRoot, ids::holdSensitivity })
        {
            auto* param = p.state().getParameter(id);
            param->setValueNotifyingHost(param->getValue() > 0.5f ? 0.1f : 0.9f);
        }
    });
    CHECK(difference(before, after, 0) == 0.0);
}

TEST_CASE("HOLD holds the strum into the gate, adds no latency, and goes to the output when the gate is bypassed")
{
    const auto in = strummed(4.0);
    {
        auto p = make();
        const int latency = p->getLatencySamples();
        set(*p, ids::holdOn, 1.0f);
        p->prepareToPlay(kRate, kBlock);
        CHECK(p->getLatencySamples() == latency);
        CHECK(p->getTailLengthSeconds() > 2.2);
    }
    // The gate's effect silent: only the direct signal comes out.
    auto dryOnly = [](float holdOn, float feed) {
        return [holdOn, feed](Glide90Processor& p) {
            set(p, ids::balance, 0.0f);
            set(p, ids::holdOn, holdOn);
            set(p, ids::holdFeed, feed);
        };
    };
    const auto off = renderFresh(in, dryOnly(0.0f, 0.0f));
    // Into the gate, with the gate's effect at 0: not heard.
    CHECK(difference(off, renderFresh(in, dryOnly(1.0f, 0.0f)), 0) == 0.0);
    // To the output: heard, long after the string has died.
    CHECK(difference(off, renderFresh(in, dryOnly(1.0f, 1.0f)), (int) (3.0 * kRate)) > 1.0);
    // Into a bypassed gate: to the output instead.
    auto bypassed = [](float holdOn) {
        return [holdOn](Glide90Processor& p) {
            set(p, ids::bypass, 1.0f);
            set(p, ids::holdOn, holdOn);
        };
    };
    CHECK(difference(renderFresh(in, bypassed(0.0f)), renderFresh(in, bypassed(1.0f)), (int) (3.0 * kRate)) > 1.0);
}

TEST_CASE("FREEZE is played, not part of the sound: presets, A/B and undo leave it alone")
{
    auto p = make();
    auto* freeze = p->state().getParameter(ids::holdLatch);
    freeze->beginChangeGesture();
    freeze->setValueNotifyingHost(1.0f);
    freeze->endChangeGesture();
    CHECK_FALSE(p->undoManager().canUndo());
    p->loadFactoryPreset(1);
    CHECK(plain(*p, ids::holdLatch) > 0.5f);
    p->selectAB(1);
    CHECK(plain(*p, ids::holdLatch) > 0.5f);
}

TEST_CASE("HOLD's typed values are read the way they are shown")
{
    auto p = make();
    auto typed = [&](const char* id, const char* text) {
        auto* param = p->state().getParameter(id);
        return param->convertFrom0to1(param->getValueForText(text));
    };
    CHECK(typed(ids::holdSize, "250 ms") == doctest::Approx(250.0f));
    CHECK(typed(ids::holdSmear, "0.4 s") == doctest::Approx(400.0f));
    CHECK(typed(ids::holdDecay, "inf") >= glide90::kHoldDecayForever);
    CHECK(typed(ids::holdDecay, "4.5") == doctest::Approx(4.5f));
    CHECK(typed(ids::holdDecay, "800 ms") == doctest::Approx(0.8f));
    CHECK(typed(ids::holdFollow, "150") == doctest::Approx(1.5f));
    CHECK(typed(ids::holdSpread, "0.2 st") == doctest::Approx(20.0f));
    CHECK(typed(ids::holdRoot, "C4") == doctest::Approx(72.0f));
    CHECK(typed(ids::holdRoot, "eb2") == doctest::Approx(51.0f));
    CHECK(typed(ids::holdRoot, "64") == doctest::Approx(64.0f));
    CHECK(p->state().getParameter(ids::holdRoot)->getCurrentValueAsText() == "C3");
    CHECK(p->state().getParameter(ids::holdDecay)->getCurrentValueAsText() == "INF");
}

TEST_CASE("HOLD sits under the glide and the gate, stays put, and shows what it holds")
{
    auto p = make();
    set(*p, ids::holdOn, 1.0f);
    set(*p, ids::holdSpread, 20.0f);
    render(*p, strummed(1.5));
    std::unique_ptr<juce::AudioProcessorEditor> editor(p->createEditor());
    glide90::ui::HoldPanel* holdPanel = nullptr;
    glide90::ui::GlidePanel* glidePanel = nullptr;
    glide90::ui::GatePanel* gatePanel = nullptr;
    std::function<void(juce::Component&)> find = [&](juce::Component& c) {
        if (auto* h = dynamic_cast<glide90::ui::HoldPanel*>(&c))
            holdPanel = h;
        if (auto* g = dynamic_cast<glide90::ui::GlidePanel*>(&c))
            glidePanel = g;
        if (auto* g = dynamic_cast<glide90::ui::GatePanel*>(&c))
            gatePanel = g;
        for (auto* child : c.getChildren())
            find(*child);
    };
    find(*editor);
    REQUIRE(holdPanel != nullptr);
    CHECK(holdPanel->getY() >= glidePanel->getBottom());
    CHECK(holdPanel->getX() == glidePanel->getX());
    CHECK(holdPanel->getRight() == gatePanel->getRight());
    CHECK(holdPanel->getBottom() <= glide90::Glide90Editor::kHeight);

    // Its title bar is no handle.
    bool started = false;
    holdPanel->onDragStart = [&] { started = true; };
    juce::MouseEvent press(juce::Desktop::getInstance().getMainMouseSource(), { 120.0f, 20.0f }, {}, 1.0f, 0.0f, 0.0f,
                           0.0f, 0.0f, holdPanel, holdPanel, juce::Time::getCurrentTime(), { 120.0f, 20.0f },
                           juce::Time::getCurrentTime(), 1, false);
    holdPanel->mouseDown(press.withNewPosition(juce::Point<float>(120.0f, 20.0f)));
    holdPanel->mouseDrag(press.withNewPosition(juce::Point<float>(300.0f, 20.0f)));
    holdPanel->mouseUp(press);
    CHECK_FALSE(started);

    if (const auto path = juce::SystemStats::getEnvironmentVariable("GLIDE90_SNAPSHOT", {}); path.isNotEmpty())
    {
        juce::MessageManager::getInstance()->runDispatchLoopUntil(300);
        const auto image = editor->createComponentSnapshot(editor->getLocalBounds(), true, 2.0f);
        juce::File file(juce::File(path).getSiblingFile(juce::File(path).getFileNameWithoutExtension() + "-hold.png"));
        file.deleteFile();
        juce::FileOutputStream out(file);
        REQUIRE(juce::PNGImageFormat().writeImageToStream(image, out));
    }
}
