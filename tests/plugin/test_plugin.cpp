// Processor-level tests: memories, state, latency and host bypass of the Glide90 plugin.
#define DOCTEST_CONFIG_IMPLEMENT
#include <doctest/doctest.h>

#include "PluginProcessor.h"

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
