#pragma once

#include "Memories.h"
#include "Parameters.h"

#include "Shape.h"
#include "SpxEngine.h"
#include "SwayProcessor.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <array>
#include <atomic>

namespace glide90
{

// Glide90: a vibrato-bar glide (before or after) and an SPX90-style processor. Mono in (stereo
// inputs are summed), stereo out, like the unit.
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

    // Host programs are the factory presets.
    int getNumPrograms() override;
    int getCurrentProgram() override { return currentFactoryPreset; }
    void setCurrentProgram(int index) override;
    const juce::String getProgramName(int index) override;
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState& state() { return apvts; }

    // --- SPX90 memories (message thread) ---
    const MemoryBank& memories() const { return bank; }
    // Bumped whenever a memory is stored or the bank is restored, so lists can refresh.
    int memoriesRevision() const { return bankRevision.load(); }
    // Loads a memory into the edit buffer, as the RECALL key does. Empty user memories change nothing.
    void recallMemory(int number);
    // Stores the edit buffer, as the STORE key does. False for the read-only memories 1-30.
    bool storeMemory(int number, const juce::String& title);
    int loadedMemory() const { return loaded.load(); }
    // A memory change from the host is recalled on the message thread; this runs it right away.
    void flushPendingRecall() { handleUpdateNowIfNeeded(); }
    // The ROM program whose algorithm the edit buffer runs.
    int algorithm() const { return currentAlgorithm.load(); }

    // --- Glide shape (message thread) ---
    // The shape selected by the glide_shape parameter (a preset or the custom one).
    sway::Shape currentShape() const;
    // Replaces the custom shape and selects it.
    void setCustomShape(const sway::Shape& shape);

    // --- Presets (message thread) ---
    juce::StringArray factoryPresetNames() const;
    void loadFactoryPreset(int index);
    static juce::File userPresetDirectory();
    juce::Array<juce::File> userPresetFiles() const;
    bool saveUserPreset(const juce::String& name);
    bool loadUserPreset(const juce::File& file);
    juce::String currentPresetName() const { return presetName; }

    // --- Meters and display (any thread) ---
    // Peak at the A/D since the last call, relative to its full scale.
    float takeInputPeak() { return inputPeak.exchange(0.0f); }
    float barCents() const { return lastBarCents.load(); }
    float glidePhase() const { return lastGlidePhase.load(); }
    double hostBpm() const { return lastBpm.load(); }

private:
    void parameterChanged(const juce::String& id, float value) override;
    void handleAsyncUpdate() override;
    void loadMemory(int number);
    Memory editBuffer() const;
    void setParameter(const char* id, float plainValue);
    void resetForPreset();
    juce::ValueTree shapeToTree() const;
    void shapeFromTree(const juce::ValueTree& tree);
    void setParametersExactly(const juce::ValueTree& paramsTree, bool missingToDefault);
    void processChunk(juce::AudioBuffer<float>& buffer, int start, int num, const spx::SpxSettings& spxSettings,
                      sway::SwaySettings glide, int midiNoteAt);

    juce::AudioProcessorValueTreeState apvts;
    MemoryBank bank;
    std::atomic<int> bankRevision { 0 };
    std::atomic<int> loaded { 17 };
    std::atomic<int> currentAlgorithm { 17 };
    int currentFactoryPreset = 0;
    juce::String presetName { "Init" };

    // The custom shape is edited on the message thread and copied for the audio thread under a lock
    // that the audio thread only ever tries.
    sway::Shape customShape;
    juce::SpinLock shapeLock;
    sway::Shape audioCustomShape;
    std::array<sway::Shape, sway::kShapePresetNames.size()> presetShapes;

    spx::SpxEngine spxEngine;
    sway::SwayProcessor glideProcessor;
    juce::AudioBuffer<float> mono;
    int maxBlock = 0;
    double currentRate = 48000.0;

    // Host bypass keeps the latency.
    juce::AudioBuffer<float> bypassDelay;
    int bypassPos = 0;

    float pitchBend = 0.0f;  // -1 .. 1
    float modWheel = 0.0f;   // 0 .. 1
    float expression = 0.0f; // 0 .. 1
    std::atomic<float> inputPeak { 0.0f }, lastBarCents { 0.0f }, lastGlidePhase { 0.0f };
    std::atomic<double> lastBpm { 120.0 };

    struct RawParams
    {
        std::atomic<float>*erType, *erRoomSize, *erLiveness, *erDelay, *erLpf, *balance, *outLevel, *input, *inputSwitch,
            *outputSwitch, *bypass, *glideOn, *glideMode, *glidePeriod, *glideDivision, *glideDepth, *glideDirection,
            *glideRandom, *glideSmooth, *glideShape, *glideTriggerSource, *glideSensitivity, *glideManualSource,
            *glideBar, *glideTwinDetune, *glideTwinLevel, *glideMix, *glidePosition;
    } raw;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Glide90Processor)
};

} // namespace glide90
