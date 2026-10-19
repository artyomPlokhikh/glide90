#pragma once

#include "Memories.h"
#include "Parameters.h"

#include "SpxEngine.h"
#include "SwayProcessor.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <atomic>

namespace glide90
{

// Glide90: Sway (optional, before or after) and an SPX90-style processor. Mono in (stereo inputs
// are summed), stereo out, like the unit.
class Glide90Processor : public juce::AudioProcessor,
                         private juce::AudioProcessorValueTreeState::Listener,
                         private juce::AsyncUpdater
{
public:
    Glide90Processor();
    ~Glide90Processor() override;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void processBlockBypassed(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;
    using AudioProcessor::processBlockBypassed;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 2.2; } // ROOM SIZE 20 plus 400 ms delay

    int getNumPrograms() override;
    int getCurrentProgram() override { return currentPreset; }
    void setCurrentProgram(int index) override;
    const juce::String getProgramName(int index) override;
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    // --- SPX90 front panel (message thread) ---
    juce::AudioProcessorValueTreeState& state() { return apvts; }
    const MemoryBank& memories() const { return bank; }
    // Loads a memory into the edit buffer, as the RECALL key does. Empty user memories change nothing.
    void recallMemory(int number);
    // Stores the edit buffer, as the STORE key does. False for the read-only memories 1-30.
    bool storeMemory(int number, const juce::String& title);
    int loadedMemory() const { return loaded.load(); }
    // A memory change from the host is recalled on the message thread; this runs it right away.
    void flushPendingRecall() { handleUpdateNowIfNeeded(); }
    // The ROM program whose algorithm the edit buffer runs.
    int algorithm() const { return currentAlgorithm.load(); }

    // --- Meters (any thread) ---
    // Peak at the A/D since the last call, relative to its full scale.
    float takeInputPeak() { return inputPeak.exchange(0.0f); }
    float barCents() const { return lastBarCents.load(); }

private:
    void parameterChanged(const juce::String& id, float value) override;
    void handleAsyncUpdate() override;
    void loadMemory(int number);
    Memory editBuffer() const;
    void setParameter(const char* id, float plainValue);
    void processChunk(juce::AudioBuffer<float>& buffer, int start, int num, const spx::SpxSettings& spxSettings,
                      const sway::SwaySettings& swaySettings);

    juce::AudioProcessorValueTreeState apvts;
    MemoryBank bank;
    std::atomic<int> loaded { 17 };
    std::atomic<int> currentAlgorithm { 17 };
    int currentPreset = 0;

    spx::SpxEngine spxEngine;
    sway::SwayProcessor swayProcessor;
    juce::AudioBuffer<float> mono;
    int maxBlock = 0;

    // Host bypass keeps the latency.
    juce::AudioBuffer<float> bypassDelay;
    int bypassPos = 0;

    float pitchBend = 0.0f; // -1 .. 1
    float modWheel = 0.0f;  // 0 .. 1
    std::atomic<float> inputPeak { 0.0f }, lastBarCents { 0.0f };

    struct RawParams
    {
        std::atomic<float>*erType, *erRoomSize, *erLiveness, *erDelay, *erLpf, *balance, *outLevel, *input, *inputSwitch,
            *outputSwitch, *bypass, *swayOn, *swayDepth, *swayRate, *swayShape, *swayStrum, *swaySensitivity,
            *swayAsymmetry, *swayBar, *swayBarSource, *swayTwinDetune, *swayTwinLevel, *swayPosition, *swayMix;
    } raw;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Glide90Processor)
};

} // namespace glide90
