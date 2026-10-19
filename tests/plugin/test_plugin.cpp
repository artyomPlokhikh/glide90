// Processor-level tests: memories, state, latency and host bypass of the Glide90 plugin.
#define DOCTEST_CONFIG_IMPLEMENT
#include <doctest/doctest.h>

#include "PluginProcessor.h"

#include <juce_events/juce_events.h>

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

    set(*p, ids::memory, 31.0f); // factory user memory: the roadie's Loveless setting
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
    set(*p, ids::swayDepth, 0.0f);
    set(*p, ids::swayStrum, 0.0f);
    for (bool swayOn : { false, true })
    {
        CAPTURE(swayOn);
        set(*p, ids::swayOn, swayOn ? 1.0f : 0.0f);
        p->reset();
        p->prepareToPlay(kRate, kBlock);
        CHECK(impulsePeak(*p, false) == p->getLatencySamples());
    }
    CHECK(impulsePeak(*p, true) == p->getLatencySamples());
}

TEST_CASE("host presets recall their memory and set Sway")
{
    auto p = make();
    for (int i = 0; i < p->getNumPrograms(); ++i)
    {
        CAPTURE(p->getProgramName(i));
        p->setCurrentProgram(i);
        CHECK(p->getCurrentProgram() == i);
        CHECK(p->loadedMemory() == juce::roundToInt(plain(*p, ids::memory)));
    }
    p->setCurrentProgram(1); // Loveless RG (roadie)
    CHECK(p->loadedMemory() == 31);
    CHECK(plain(*p, ids::swayOn) > 0.5f);
}

int main(int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI juce;
    doctest::Context context(argc, argv);
    return context.run();
}
