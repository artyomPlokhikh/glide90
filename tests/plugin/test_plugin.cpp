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
#include "ui/HeaderBar.h"
#include "ui/HoldPanel.h"
#include "ui/PanelLookAndFeel.h"
#include "ui/RoutingPanel.h"
#include "ui/ShapeDisplay.h"

#include <juce_events/juce_events.h>
#include <map>
#include <numeric>
#include <typeinfo>
#include <juce_gui_basics/juce_gui_basics.h>

using glide90::Glide90Processor;
namespace ids = glide90::ids;

namespace
{

constexpr double kRate = 48000.0;
constexpr int kBlock = 256;
// Routings the tests use, by their place in glide90::kRoutings (the ROUTING test checks the names).
constexpr float kIntoTheSwell = 0.0f, kBentTail = 1.0f, kHoldBeside = 4.0f;

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
    set(*p, ids::gateMix, 0.0f);
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

    // A preset leaves the rig alone (INPUT, the input mode) and resets what it does not set, OUTPUT too.
    set(*p, ids::input, -6.0f);
    set(*p, ids::inputMode, (float) glide90::InputMode::Mono);
    set(*p, ids::output, -3.0f);
    set(*p, ids::glideTwinLevel, 0.9f);
    p->setCurrentProgram(1);
    CHECK(plain(*p, ids::input) == doctest::Approx(-6.0f));
    CHECK(juce::roundToInt(plain(*p, ids::inputMode)) == (int) glide90::InputMode::Mono);
    CHECK(plain(*p, ids::output) == doctest::Approx(0.0f));
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

TEST_CASE("the ROUTING strip: the arrows and the chart pick a routing, and every module is named as on its panel")
{
    namespace ui = glide90::ui;
    namespace names = glide90::names;
    auto p = make();
    std::unique_ptr<juce::AudioProcessorEditor> editor(p->createEditor());
    ui::GlidePanel* glidePanel = nullptr;
    ui::GatePanel* gatePanel = nullptr;
    ui::HoldPanel* holdPanel = nullptr;
    ui::RoutingPanel* routing = nullptr;
    ui::PickSense* pickSense = nullptr;
    std::vector<ui::IoColumn*> io;
    std::function<void(juce::Component&)> find = [&](juce::Component& c) {
        if (auto* g = dynamic_cast<ui::GlidePanel*>(&c))
            glidePanel = g;
        if (auto* g = dynamic_cast<ui::GatePanel*>(&c))
            gatePanel = g;
        if (auto* h = dynamic_cast<ui::HoldPanel*>(&c))
            holdPanel = h;
        if (auto* r = dynamic_cast<ui::RoutingPanel*>(&c))
            routing = r;
        if (auto* ps = dynamic_cast<ui::PickSense*>(&c))
            pickSense = ps;
        if (auto* column = dynamic_cast<ui::IoColumn*>(&c))
            io.push_back(column);
        for (auto* child : c.getChildren())
            find(*child);
    };
    find(*editor);
    REQUIRE(glidePanel != nullptr);
    REQUIRE(gatePanel != nullptr);
    REQUIRE(holdPanel != nullptr);
    REQUIRE(routing != nullptr);
    REQUIRE(pickSense != nullptr);
    REQUIRE(io.size() == 2);
    // The strip over the modules and OUT, beside PICK SENS; the modules stay where they were.
    CHECK(glidePanel->getX() < gatePanel->getX());
    CHECK(routing->getBottom() < glidePanel->getY());
    CHECK(routing->getX() == glidePanel->getX());
    CHECK(pickSense->getRight() < routing->getX());
    CHECK(pickSense->getY() == routing->getY());

    // The modules' names: the panels' titles are the names the ROUTING uses.
    CHECK(glidePanel->getTitle() == names::glide);
    CHECK(gatePanel->getTitle() == names::reverseGate);
    CHECK(holdPanel->getTitle() == names::hold);
    juce::StringArray titles { names::dry };
    for (auto* column : io)
        titles.add(column->getTitle());
    titles.addArray({ glidePanel->getTitle(), gatePanel->getTitle(), holdPanel->getTitle() });
    CHECK(titles.contains(names::in));
    CHECK(titles.contains(names::out));

    // The routings the other tests use, by name.
    CHECK(juce::String(glide90::routingAt((int) kIntoTheSwell).name) == "INTO THE SWELL");
    CHECK(juce::String(glide90::routingAt((int) kBentTail).name) == "BENT TAIL");
    CHECK(juce::String(glide90::routingAt((int) kHoldBeside).name) == "HOLD BESIDE");

    // Every word a diagram writes is a panel's title (or DRY), in every routing, large or in the chart,
    // with REVERSE GATE on or off; a GLIDE mark on each path GLIDE bends. With REVERSE GATE off HOLD goes
    // to OUT, bent if the routing bends the gate.
    for (int i = 0; i < glide90::kNumRoutings; ++i)
        for (bool gateOn : { true, false })
            for (bool small : { false, true })
            {
                CAPTURE(i);
                CAPTURE(gateOn);
                CAPTURE(small);
                const auto& r = glide90::routingAt(i);
                const auto drawn = ui::drawRouting(nullptr, { 0.0f, 0.0f, small ? 250.0f : 644.0f, 64.0f }, r,
                                                   { true, gateOn, true }, small, juce::Colours::black, juce::Colours::white);
                int marks = 0;
                for (const auto& label : drawn)
                {
                    CAPTURE(label.text);
                    CHECK(titles.contains(label.text));
                    marks += label.text == names::glide ? 1 : 0;
                }
                const bool holdOut = !r.holdIntoGate || !gateOn;
                const int expected = (r.bendDry ? 1 : 0) + (gateOn && r.gate != glide90::GateBend::Off ? 1 : 0)
                                     + (holdOut && (r.holdIntoGate ? r.gate != glide90::GateBend::Off : r.bendHold) ? 1 : 0);
                CHECK(marks == expected);
            }
    // The descriptions name the modules the same way: never "GATE" alone, never "BAR".
    for (const auto& r : glide90::kRoutings)
    {
        const juce::String d(r.description);
        CAPTURE(d);
        CHECK(d.replace(names::reverseGate, "").indexOf("GATE") < 0);
        CHECK(!d.containsWholeWord("BAR"));
    }

    // The arrows step through the routings, wrapping round; one step, one undo.
    auto value = [&] { return juce::roundToInt(plain(*p, ids::routing)); };
    CHECK(value() == 0);
    routing->step(1);
    CHECK(value() == 1);
    p->undoManager().undo();
    CHECK(value() == 0);
    routing->step(-1);
    CHECK(value() == glide90::kNumRoutings - 1);
    routing->step(1);
    CHECK(value() == 0);

    // The chart: a card takes its routing and closes the chart.
    routing->openChart();
    REQUIRE(routing->chart() != nullptr);
    if (const auto path = juce::SystemStats::getEnvironmentVariable("GLIDE90_SNAPSHOT", {}); path.isNotEmpty())
    {
        juce::MessageManager::getInstance()->runDispatchLoopUntil(300);
        const auto image = editor->createComponentSnapshot(editor->getLocalBounds(), true, 2.0f);
        juce::File file(juce::File(path).getSiblingFile(juce::File(path).getFileNameWithoutExtension() + "-chart.png"));
        file.deleteFile();
        juce::FileOutputStream out(file);
        REQUIRE(juce::PNGImageFormat().writeImageToStream(image, out));
    }
    routing->chart()->choose((int) kHoldBeside);
    CHECK(value() == (int) kHoldBeside);
    juce::MessageManager::getInstance()->runDispatchLoopUntil(50);
    CHECK(routing->chart() == nullptr);

    if (const auto path = juce::SystemStats::getEnvironmentVariable("GLIDE90_SNAPSHOT", {}); path.isNotEmpty())
    {
        set(*p, ids::holdOn, 1.0f);
        set(*p, ids::gateOn, 0.0f);
        pickSense->knob().setFlash(1.0f); // as on a pick heard
        juce::MessageManager::getInstance()->runDispatchLoopUntil(100);
        const auto image = editor->createComponentSnapshot(editor->getLocalBounds(), true, 2.0f);
        juce::File file(juce::File(path).getSiblingFile(juce::File(path).getFileNameWithoutExtension() + "-routing.png"));
        file.deleteFile();
        juce::FileOutputStream out(file);
        REQUIRE(juce::PNGImageFormat().writeImageToStream(image, out));
    }
}

TEST_CASE("moving the gate's bend between IN and OUT does not click, and the direct sound plays on")
{
    // The largest sample-to-sample step of a sine through the plugin, with the bend moved twice or not
    // at all: the moves may not add a jump of their own. With the effect silent (MIX all direct) the
    // output stays the same: only the effect fades while the bender moves.
    auto run = [](bool flip, float gateMix) {
        auto p = make();
        set(*p, ids::gateMix, gateMix);
        juce::AudioBuffer<float> buffer(2, kBlock), all(2, 300 * kBlock);
        juce::MidiBuffer midi;
        float previous = 0.0f, worst = 0.0f;
        for (int b = 0; b < 300; ++b)
        {
            if (flip && (b == 100 || b == 200))
                set(*p, ids::routing, b == 100 ? kBentTail : kIntoTheSwell);
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
    set(*p, ids::gateMix, 1.0f);

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
        CHECK(plain(*p, ids::gateMix) == doctest::Approx(0.5f));

        // Text that names no value leaves it alone; Escape-like cancel is the same path without applying.
        knob->mouseDoubleClick(event(juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier), 2));
        REQUIRE(fieldOf() != nullptr);
        fieldOf()->setText("abc");
        peer->handleMouseEvent(juce::MouseInputSource::InputSourceType::mouse, spot,
                               juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier), 1.0f, 0.0f, t + 1000);
        peer->handleMouseEvent(juce::MouseInputSource::InputSourceType::mouse, spot, juce::ModifierKeys(), 1.0f, 0.0f, t + 1020);
        juce::MessageManager::getInstance()->runDispatchLoopUntil(100);
        CHECK(fieldOf() == nullptr);
        CHECK(plain(*p, ids::gateMix) == doctest::Approx(0.5f));
    }
    else
    {
        MESSAGE("the test window is not on top at the click point here: click part skipped");
    }

    // Alt-click: back to the default (100 %).
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
    set(*p, ids::gateMix, balance);
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
// without a test of its own. Only INPUT, the input mode and FREEZE belong to the instance rather than
// the sound.

namespace
{
bool isInstanceParameter(const juce::String& id)
{
    return id == ids::input || id == ids::inputMode || id == ids::holdFreeze;
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

TEST_CASE("A and B each keep every parameter but INPUT, the input mode and FREEZE, which they share")
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

TEST_CASE("a user preset keeps every parameter but INPUT, the input mode and FREEZE")
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
    for (const char* id : { ids::gateMix, ids::glideTwinLevel, ids::glideMix, ids::glideDynamics, ids::glideBar })
    {
        CAPTURE(id);
        CHECK(value(id, "50") == doctest::Approx(0.5f));
        CHECK(value(id, "50%") == doctest::Approx(0.5f));
        CHECK(value(id, "50 %") == doctest::Approx(0.5f));
        CHECK(value(id, "100%") == doctest::Approx(1.0f));
        CHECK(value(id, "12,5") == doctest::Approx(0.125f));
    }
    // Levels in dB: plain numbers are dB, "-inf" and "off" are silence (INPUT's bottom, which has none).
    for (const char* id : { ids::input, ids::output, ids::holdLevel })
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
    auto* mix = p->state().getParameter(ids::gateMix);
    CHECK(mix->convertFrom0to1(mix->getValueForText("50")) == doctest::Approx(0.5f));

    // Text that names no value changes nothing.
    for (const char* id : { ids::gateMix, ids::gateRoomSize, ids::gateLiveness, ids::gateLpf, ids::glideDivision })
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

TEST_CASE("HOLD is off in new instances, and while off its controls change nothing")
{
    CHECK(plain(*make(), ids::holdOn) < 0.5f);
    const auto in = strummed(3.0, 1.0);
    const auto before = renderFresh(in, [](Glide90Processor&) {});
    const auto after = renderFresh(in, [](Glide90Processor& p) {
        for (const char* id : { ids::holdMode, ids::holdFreeze, ids::holdSize, ids::holdDensity, ids::holdSmear,
                                ids::holdSpread, ids::holdReverse, ids::holdDuck, ids::holdDecay, ids::holdLevel,
                                ids::holdCharacter, ids::holdRoot })
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
    auto dryOnly = [](float holdOn, float toOut) {
        return [holdOn, toOut](Glide90Processor& p) {
            set(p, ids::gateMix, 0.0f);
            set(p, ids::holdOn, holdOn);
            set(p, ids::routing, toOut > 0.5f ? kHoldBeside : kIntoTheSwell);
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
            set(p, ids::gateOn, 0.0f);
            set(p, ids::holdOn, holdOn);
        };
    };
    CHECK(difference(renderFresh(in, bypassed(0.0f)), renderFresh(in, bypassed(1.0f)), (int) (3.0 * kRate)) > 1.0);
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

// A tone that bends by -100 cents with the bar, and the same bent twice.
constexpr double kOpen = 220.0;
const double kBent = kOpen * std::pow(2.0, -100.0 / 1200.0), kBentTwice = kOpen * std::pow(2.0, -200.0 / 1200.0);

// The first routing that bends DRY as `dry` and REVERSE GATE as `gate`, HOLD into REVERSE GATE.
int routingWith(bool dry, glide90::GateBend gate)
{
    for (int i = 0; i < glide90::kNumRoutings; ++i)
        if (const auto& r = glide90::routingAt(i); r.bendDry == dry && r.gate == gate && r.holdIntoGate)
            return i;
    FAIL("no routing bends DRY " << dry << " and REVERSE GATE " << (int) gate);
    return 0;
}

// The glide as a bar that the test moves: MANUAL, 100 cents down at the top, at once.
void manualBar(Glide90Processor& p)
{
    set(p, ids::glideMode, 3.0f);
    set(p, ids::glideDepth, 100.0f);
    set(p, ids::glideDirection, 0.0f);
    set(p, ids::glideSmooth, 0.0f);
    set(p, ids::glideRandom, 0.0f);
}
} // namespace

TEST_CASE("HOLD in the gate: bent at the gate's output it moves at once and once; bent going in, the reflections repeat the move")
{
    // One strike, held forever into the gate; only the effect is heard. At 3 s the bar goes down.
    const auto in = strummed(7.0);
    const int jump = (int) (3.0 * kRate);
    auto run = [&](glide90::GateBend bend) {
        auto p = make();
        manualBar(*p);
        set(*p, ids::holdOn, 1.0f);
        set(*p, ids::holdSpread, 0.0f);
        set(*p, ids::holdDuck, 0.0f);
        set(*p, ids::gateMix, 1.0f);
        set(*p, ids::gateRoomSize, 20.0f);
        set(*p, ids::routing, bend == glide90::GateBend::Out ? kBentTail : kIntoTheSwell);
        p->prepareToPlay(kRate, kBlock);
        juce::AudioBuffer<float> out(in), block(2, kBlock);
        juce::MidiBuffer midi;
        for (int start = 0; start + kBlock <= in.getNumSamples(); start += kBlock)
        {
            if (start >= jump && plain(*p, ids::glideBar) < 0.5f)
                set(*p, ids::glideBar, 1.0f);
            for (int ch = 0; ch < 2; ++ch)
                block.copyFrom(ch, 0, in, ch, start, kBlock);
            p->processBlock(block, midi);
            for (int ch = 0; ch < 2; ++ch)
                out.copyFrom(ch, start, block, ch, 0, kBlock);
        }
        return std::make_pair(out, jump + p->getLatencySamples());
    };
    auto at = [](int from, double seconds) { return from + (int) (seconds * kRate); };

    {
        const auto [out, heard] = run(glide90::GateBend::Out);
        // Before: the open pitch. Soon after the move: only the bent one, the old pitch gone at once.
        CHECK(toneEnergy(out, at(heard, -0.8), at(heard, -0.1), kOpen) > 20.0 * toneEnergy(out, at(heard, -0.8), at(heard, -0.1), kBent));
        const double old = toneEnergy(out, at(heard, 0.1), at(heard, 0.6), kOpen);
        const double bent = toneEnergy(out, at(heard, 0.1), at(heard, 0.6), kBent);
        MESSAGE("BEND OUT, after the move: old pitch " << old / bent << " of the new one");
        CHECK(old < 0.02 * bent);
        // Bent once: not twice (HOLD bending itself, then the gate's bender again).
        CHECK(toneEnergy(out, at(heard, 0.6), at(heard, 2.6), kBentTwice) < 0.02 * toneEnergy(out, at(heard, 0.6), at(heard, 2.6), kBent));
    }
    {
        const auto [out, heard] = run(glide90::GateBend::In);
        // The reflections still play what went in before the move.
        const double old = toneEnergy(out, at(heard, 0.1), at(heard, 0.6), kOpen);
        const double bent = toneEnergy(out, at(heard, 0.1), at(heard, 0.6), kBent);
        MESSAGE("BEND IN, after the move: old pitch " << old / bent << " of the new one");
        CHECK(old > 0.1 * bent);
        // Once they have gone by, the new pitch, bent once.
        CHECK(toneEnergy(out, at(heard, 2.0), at(heard, 3.0), kOpen) < 0.02 * toneEnergy(out, at(heard, 2.0), at(heard, 3.0), kBent));
        CHECK(toneEnergy(out, at(heard, 2.0), at(heard, 3.0), kBentTwice) < 0.02 * toneEnergy(out, at(heard, 2.0), at(heard, 3.0), kBent));
    }
}

TEST_CASE("DRY and the gate bend apart: the direct guitar can stay straight under a bending reverb, and the other way round")
{
    // A steady tone, the bar held down from the start.
    juce::AudioBuffer<float> in(2, (int) (3.0 * kRate));
    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < in.getNumSamples(); ++i)
            in.setSample(ch, i, 0.3f * (float) std::sin(2.0 * juce::MathConstants<double>::pi * kOpen * i / kRate));
    auto pitchOf = [&](float gateMix, bool dry, glide90::GateBend gate) {
        const auto out = renderFresh(in, [&](Glide90Processor& p) {
            manualBar(p);
            set(p, ids::glideBar, 1.0f);
            set(p, ids::gateMix, gateMix);
            set(p, ids::routing, (float) routingWith(dry, gate));
        });
        const int from = (int) (1.5 * kRate), to = (int) (2.9 * kRate);
        const double open = toneEnergy(out, from, to, kOpen), bent = toneEnergy(out, from, to, kBent);
        return bent > 50.0 * open ? -100 : open > 50.0 * bent ? 0 : 1; // 1: both, or neither
    };
    // The direct guitar alone (MIX all direct).
    CHECK(pitchOf(0.0f, true, glide90::GateBend::Off) == -100);
    CHECK(pitchOf(0.0f, false, glide90::GateBend::In) == 0);
    // The effect alone, bent going in or coming out, or not at all, whatever the direct guitar does.
    CHECK(pitchOf(1.0f, false, glide90::GateBend::In) == -100);
    CHECK(pitchOf(1.0f, true, glide90::GateBend::Out) == -100); // the direct guitar silent at MIX 1
    CHECK(pitchOf(1.0f, true, glide90::GateBend::Off) == 0);
    // With the glide off nothing bends.
    const auto off = renderFresh(in, [&](Glide90Processor& p) {
        manualBar(p);
        set(p, ids::glideBar, 1.0f);
        set(p, ids::glideOn, 0.0f);
        set(p, ids::gateMix, 0.5f);
    });
    const int from = (int) (1.5 * kRate), to = (int) (2.9 * kRate);
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

TEST_CASE("OUTPUT is the last stage: it scales everything, HOLD on either path, the gate on or off")
{
    const auto in = strummed(3.0, 1.0);
    for (float gateOn : { 1.0f, 0.0f })
        for (float feed : { kIntoTheSwell, kHoldBeside })
        {
            CAPTURE(gateOn);
            CAPTURE(feed);
            auto setUp = [gateOn, feed](float outputDb) {
                return [gateOn, feed, outputDb](Glide90Processor& p) {
                    set(p, ids::holdOn, 1.0f);
                    set(p, ids::routing, feed);
                    set(p, ids::gateOn, gateOn);
                    set(p, ids::output, outputDb);
                };
            };
            const auto unity = renderFresh(in, setUp(0.0f));
            const auto lowered = renderFresh(in, setUp(-6.0f));
            CHECK(scaledError(unity, lowered, std::pow(10.0, -6.0 / 20.0)) < 1e-9);
        }
    // At its bottom, silence.
    const auto off = renderFresh(in, [](Glide90Processor& p) {
        set(p, ids::holdOn, 1.0f);
        set(p, ids::routing, kHoldBeside);
        set(p, ids::output, glide90::kOffDb);
    });
    CHECK(off.getMagnitude(0, processed(off)) == 0.0f);
}

TEST_CASE("INPUT is the first stage: everything follows it, HOLD on its own path too")
{
    // The direct sound and HOLD to the output, both linear: 6 dB less in, 6 dB less out.
    const auto in = strummed(3.0, 1.0);
    auto setUp = [](float inputDb) {
        return [inputDb](Glide90Processor& p) {
            set(p, ids::gateMix, 0.0f);
            set(p, ids::holdOn, 1.0f);
            set(p, ids::routing, kHoldBeside);
            set(p, ids::input, inputDb);
        };
    };
    const auto unity = renderFresh(in, setUp(0.0f));
    const auto lowered = renderFresh(in, setUp(-6.0f));
    // (The direct sound passes the reverse gate's 16-bit converters: not exactly linear.)
    CHECK(scaledError(unity, lowered, std::pow(10.0, -6.0 / 20.0)) < 1e-6);
    // HOLD alone (what is left after the direct sound) follows it as well.
    const auto dry = renderFresh(in, [](Glide90Processor& p) { set(p, ids::gateMix, 0.0f); });
    auto holdOf = [&](const juce::AudioBuffer<float>& all) {
        juce::AudioBuffer<float> layer(all);
        for (int ch = 0; ch < 2; ++ch)
            layer.addFrom(ch, 0, dry, ch, 0, dry.getNumSamples(), -1.0f);
        return layer;
    };
    const auto lowDry = renderFresh(in, [](Glide90Processor& p) {
        set(p, ids::gateMix, 0.0f);
        set(p, ids::input, -6.0f);
    });
    juce::AudioBuffer<float> holdLow(lowered);
    for (int ch = 0; ch < 2; ++ch)
        holdLow.addFrom(ch, 0, lowDry, ch, 0, lowDry.getNumSamples(), -1.0f);
    const auto holdUnity = holdOf(unity);
    REQUIRE(holdUnity.getMagnitude(0, processed(holdUnity)) > 0.01f);
    CHECK(scaledError(holdUnity, holdLow, std::pow(10.0, -6.0 / 20.0)) < 1e-6);

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

TEST_CASE("FREEZE is played, not part of the sound: presets, A/B and undo leave it alone")
{
    auto p = make();
    auto* freeze = p->state().getParameter(ids::holdFreeze);
    freeze->beginChangeGesture();
    freeze->setValueNotifyingHost(1.0f);
    freeze->endChangeGesture();
    CHECK_FALSE(p->undoManager().canUndo());
    p->loadFactoryPreset(1);
    CHECK(plain(*p, ids::holdFreeze) > 0.5f);
    p->selectAB(1);
    CHECK(plain(*p, ids::holdFreeze) > 0.5f);
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
    CHECK(typed(ids::holdSpread, "0.2 st") == doctest::Approx(20.0f));
    CHECK(typed(ids::holdRoot, "C4") == doctest::Approx(72.0f));
    CHECK(typed(ids::holdRoot, "eb2") == doctest::Approx(51.0f));
    CHECK(typed(ids::holdRoot, "64") == doctest::Approx(64.0f));
    CHECK(p->state().getParameter(ids::holdRoot)->getCurrentValueAsText() == "C3");
    CHECK(p->state().getParameter(ids::holdDecay)->getCurrentValueAsText() == "INF");
}

TEST_CASE("HOLD is a strip under the glide and the gate, between IN and OUT, that folds to its title bar")
{
    namespace ui = glide90::ui;
    using Editor = glide90::Glide90Editor;
    const bool wasFolded = ui::holdFolded();
    ui::setHoldFolded(false);
    auto p = make();
    set(*p, ids::holdOn, 1.0f);
    set(*p, ids::holdSpread, 20.0f);
    render(*p, strummed(1.5));
    std::unique_ptr<juce::AudioProcessorEditor> editor(p->createEditor());
    ui::HoldPanel* holdPanel = nullptr;
    ui::GlidePanel* glidePanel = nullptr;
    std::vector<ui::IoColumn*> io;
    std::function<void(juce::Component&)> find = [&](juce::Component& c) {
        if (auto* h = dynamic_cast<ui::HoldPanel*>(&c))
            holdPanel = h;
        if (auto* g = dynamic_cast<ui::GlidePanel*>(&c))
            glidePanel = g;
        if (auto* column = dynamic_cast<ui::IoColumn*>(&c))
            io.push_back(column);
        for (auto* child : c.getChildren())
            find(*child);
    };
    find(*editor);
    REQUIRE(holdPanel != nullptr);
    REQUIRE(io.size() == 2);
    auto windowHeight = [&](bool folded) { return juce::roundToInt(Editor::heightFor(folded) * p->uiScale()); };

    // IN and OUT frame everything: from the modules' top down to HOLD's bottom, open or folded; HOLD
    // lies between them, under the modules, the gaps as between the modules.
    auto* in = io[0]->getX() < io[1]->getX() ? io[0] : io[1];
    auto* out = in == io[0] ? io[1] : io[0];
    auto framed = [&] {
        CHECK(in->getY() == glidePanel->getY());
        CHECK(out->getY() == glidePanel->getY());
        CHECK(in->getBottom() == holdPanel->getBottom());
        CHECK(out->getBottom() == holdPanel->getBottom());
        CHECK(holdPanel->getX() == in->getRight() + Editor::kGap);
        CHECK(holdPanel->getRight() == out->getX() - Editor::kGap);
        CHECK(holdPanel->getY() >= glidePanel->getBottom());
        // The level knobs sit at the columns' foot: open, on HOLD's row of knobs; folded, as far from the
        // foot as when open (not left up on GLIDE's row).
        const int footGap = ui::HoldPanel::kOpenHeight - ui::HoldPanel::kRowTop - ui::Knob::kHeight;
        const int lowest = holdPanel->isFolded() ? in->getBottom() - footGap - ui::Knob::kHeight
                                                 : holdPanel->getY() + ui::HoldPanel::kRowTop;
        CHECK(in->getBottom() - (in->getY() + in->levelKnob().getBottom()) == footGap);
        CHECK(in->getY() + in->levelKnob().getY() == lowest);
        CHECK(out->getY() + out->levelKnob().getY() == lowest);
    };

    // Open: the window with the ROUTING strip on top and HOLD under the modules, wider for the gate's
    // fourth column.
    CHECK(Editor::kWidth == 1168);
    CHECK(Editor::heightFor(false) < 760);
    CHECK(editor->getHeight() == windowHeight(false));
    CHECK(holdPanel->getBottom() <= Editor::heightFor(false));
    framed();

    // PICK SENS is on its own, above IN (the glide and HOLD share it): not in IN, OUT or GLIDE, and
    // only once in the window.
    ui::PickSense* pickSense = nullptr;
    std::function<void(juce::Component&)> findPick = [&](juce::Component& c) {
        if (auto* ps = dynamic_cast<ui::PickSense*>(&c))
            pickSense = ps;
        for (auto* child : c.getChildren())
            findPick(*child);
    };
    findPick(*editor);
    REQUIRE(pickSense != nullptr);
    CHECK(pickSense->getX() == in->getX());
    CHECK(pickSense->getRight() == in->getRight());
    CHECK(pickSense->getBottom() < in->getY());
    std::function<int(juce::Component&)> pickKnobs = [&](juce::Component& c) {
        int n = 0;
        if (auto* k = dynamic_cast<ui::Knob*>(&c); k != nullptr && k->getLabel() == "PICK SENS")
            ++n;
        for (auto* child : c.getChildren())
            n += pickKnobs(*child);
        return n;
    };
    CHECK(pickKnobs(*in) == 0);
    CHECK(pickKnobs(*out) == 0);
    CHECK(pickKnobs(*glidePanel) == 0);
    CHECK(pickKnobs(*pickSense) == 1);
    CHECK(pickKnobs(*editor) == 1);

    // Where HOLD goes and whether GLIDE bends it is the ROUTING's: no FEED key, no FOLLOW.
    std::function<bool(juce::Component&)> hasFollow = [&](juce::Component& c) {
        if (auto* k = dynamic_cast<ui::Knob*>(&c); k != nullptr && k->getLabel() == "FOLLOW")
            return true;
        for (auto* child : c.getChildren())
            if (hasFollow(*child))
                return true;
        return false;
    };
    CHECK_FALSE(hasFollow(*editor));

    if (const auto path = juce::SystemStats::getEnvironmentVariable("GLIDE90_SNAPSHOT", {}); path.isNotEmpty())
    {
        juce::MessageManager::getInstance()->runDispatchLoopUntil(300);
        const auto image = editor->createComponentSnapshot(editor->getLocalBounds(), true, 2.0f);
        juce::File file(juce::File(path).getSiblingFile(juce::File(path).getFileNameWithoutExtension() + "-hold.png"));
        file.deleteFile();
        juce::FileOutputStream out(file);
        REQUIRE(juce::PNGImageFormat().writeImageToStream(image, out));
    }

    // A click on the title folds it: the window shrinks to the bar, and the setting is kept.
    auto mouse = [&](juce::Component& c, juce::Point<float> at) {
        return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), at,
                                juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier), 1.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                                &c, &c, juce::Time::getCurrentTime(), at, juce::Time::getCurrentTime(), 1, false);
    };
    holdPanel->mouseDown(mouse(*holdPanel, { 60.0f, 20.0f }));
    CHECK(holdPanel->isFolded());
    CHECK(ui::holdFolded());
    CHECK(editor->getHeight() == windowHeight(true));
    CHECK(holdPanel->getHeight() == ui::HoldPanel::kFoldedHeight);
    framed();
    if (const auto path = juce::SystemStats::getEnvironmentVariable("GLIDE90_SNAPSHOT", {}); path.isNotEmpty())
    {
        const auto image = editor->createComponentSnapshot(editor->getLocalBounds(), true, 2.0f);
        juce::File file(juce::File(path).getSiblingFile(juce::File(path).getFileNameWithoutExtension() + "-folded.png"));
        file.deleteFile();
        juce::FileOutputStream out(file);
        REQUIRE(juce::PNGImageFormat().writeImageToStream(image, out));
    }
    // A new window opens folded too; a click on the folded screen opens it again.
    {
        std::unique_ptr<juce::AudioProcessorEditor> second(p->createEditor());
        CHECK(second->getHeight() == windowHeight(true));
    }
    holdPanel->mouseDown(mouse(*holdPanel, { 60.0f, 20.0f }));
    CHECK_FALSE(holdPanel->isFolded());
    CHECK(editor->getHeight() == windowHeight(false));
    framed();
    ui::setHoldFolded(wasFolded);
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
    namespace ui = glide90::ui;
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
        else if (dynamic_cast<ui::ShapeDisplay*>(&c) || dynamic_cast<ui::GateDisplay*>(&c) || dynamic_cast<ui::HoldDisplay*>(&c))
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
        else if (auto* module = dynamic_cast<ui::ModulePanel*>(&c))
        {
            measure(c, { 28.0f, 8.0f, 200.0f, 26.0f }, "module title", dim);
            if (dynamic_cast<ui::HoldPanel*>(module) == nullptr)
                measure(c, { 0.0f, (float) ui::ModulePanel::kRuleY - 7.0f, r.getWidth(), 14.0f }, "group names", dim);
        }
        else if (dynamic_cast<ui::HeaderBar*>(&c) != nullptr)
            measure(c, { 16.0f, 8.0f, 140.0f, 40.0f }, "logo", dim);
        else if (auto* routing = dynamic_cast<ui::RoutingPanel*>(&c))
            for (const auto& label : routing->labels())
                measure(c, label.area.expanded(3.0f), "routing " + label.text, dim || label.dimmed);
        for (auto* child : c.getChildren())
            walk(*child);
    };
    walk(editor);
    return out;
}
} // namespace

TEST_CASE("every look: text stands out from what is behind it, in every state (GLIDE90_SNAPSHOT also saves a tour)")
{
    namespace ui = glide90::ui;
    const auto before = ui::theme().id;
    const bool wasFolded = ui::holdFolded();
    const auto snapshotPath = juce::SystemStats::getEnvironmentVariable("GLIDE90_SNAPSHOT", {});
    const bool report = juce::SystemStats::getEnvironmentVariable("GLIDE90_CONTRAST", {}).isNotEmpty();
    struct State
    {
        const char* name;
        std::function<void(Glide90Processor&)> setUp;
        bool folded;
    };
    const std::vector<State> states {
        { "playing", [](Glide90Processor& p) { p.loadFactoryPreset(p.factoryPresetNames().indexOf("Held Chord")); }, false },
        { "folded", [](Glide90Processor& p) { p.loadFactoryPreset(p.factoryPresetNames().indexOf("Held Chord")); }, true },
        { "manual-midi",
          [](Glide90Processor& p) {
              p.loadFactoryPreset(p.factoryPresetNames().indexOf("Sampled Feedback"));
              set(p, ids::glideMode, 3.0f);
          },
          false },
        { "all-off",
          [](Glide90Processor& p) {
              set(p, ids::glideOn, 0.0f);
              set(p, ids::glideMode, 0.0f);
              set(p, ids::gateOn, 0.0f);
              set(p, ids::holdOn, 0.0f);
              set(p, ids::holdMode, 1.0f);
          },
          false },
    };
    double worstText = 21.0, worstDimmed = 21.0;
    for (int look = 0; look < ui::kThemeCount; ++look)
        for (const auto& state : states)
        {
            CAPTURE(ui::themeAt(look).name);
            CAPTURE(state.name);
            ui::setTheme(ui::themeAt(look).id);
            ui::setHoldFolded(state.folded);
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
    ui::setHoldFolded(wasFolded);
}
