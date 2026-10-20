#pragma once

#include "Parameters.h"

#include "HoldEngine.h"
#include "Shape.h"
#include "SpxEngine.h"
#include "SwayProcessor.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <array>
#include <atomic>

namespace glide90
{

// Glide90: a vibrato-bar glide (before or after) and the SPX90's reverse gate, with HOLD beside
// them: the chord captured and held, bent by the same bar, into the gate or to the output. Mono or
// stereo in, stereo out. The reverb hears the sum of the inputs, as the unit's one input would; in
// STEREO the direct signal keeps its sides.
class Glide90Processor : public juce::AudioProcessor, private juce::AudioProcessorParameter::Listener
{
public:
    Glide90Processor();
    ~Glide90Processor() override;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    // Clears every delay line (the reverb's two seconds, the glide's heads): hosts call this on stop
    // and on jumps, and it runs when the host's bypass is released.
    void reset() override;
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
    // ROOM SIZE 20 plus 400 ms delay, and what HOLD still plays (a held chord lasts DECAY).
    double getTailLengthSeconds() const override;

    // Host programs are the factory presets.
    int getNumPrograms() override;
    int getCurrentProgram() override { return currentFactoryPreset; }
    void setCurrentProgram(int index) override;
    const juce::String getProgramName(int index) override;
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState& state() { return apvts; }

    // --- Glide shape (message thread) ---
    // The shape selected by the glide_shape parameter (a preset or the custom one).
    sway::Shape currentShape() const;
    // Replaces the custom shape and selects it.
    void setCustomShape(const sway::Shape& shape);

    // --- Presets (message thread) ---
    juce::StringArray factoryPresetNames() const;
    void loadFactoryPreset(int index);
    static juce::File userPresetDirectory();
    static juce::String presetExtension();
    juce::Array<juce::File> userPresetFiles() const;
    bool saveUserPreset(const juce::String& name);
    bool loadUserPreset(const juce::File& file);
    // Moves a user preset file to the trash (or deletes it). Refuses anything outside the preset
    // folder. The current sound stays loaded, so SAVE puts a deleted current preset back.
    bool deleteUserPreset(const juce::File& file);
    juce::String currentPresetName() const { return presetName; }
    // True when the current preset is a user preset file (whatever its name), false for a factory one.
    bool currentPresetIsUser() const { return presetIsUser; }
    // True once anything (but the level switches) differs from the preset as loaded.
    bool isPresetModified() const;

    // --- Editing (message thread) ---
    // Undo steps are what the editor does: each knob gesture and each beginEdit()/endEdit() pair
    // (drawing the shape). Host automation is not an edit. Loading a preset starts the history of
    // the current A/B slot afresh.
    // Each A/B slot has its own history: undo in B never brings back a state of A.
    juce::UndoManager& undoManager() { return undos[(size_t) abIndex]; }
    void beginEdit();
    void endEdit();
    // A/B: two working copies of the whole setting (see slotState()); switching keeps the one you leave.
    int abSlot() const { return abIndex; }
    void selectAB(int slot);
    // Editor zoom, stored with the project.
    float uiScale() const { return scale; }
    void setUiScale(float s) { scale = juce::jlimit(0.75f, 2.0f, s); }

    // --- Meters and display (any thread) ---
    // Peak at the A/D since the last call, relative to its full scale.
    float takeInputPeak() { return inputPeak.exchange(0.0f); }
    // Peak at the outputs since the last call.
    float takeOutputPeak() { return outputPeak.exchange(0.0f); }
    // Peak of the reverse gate's effect alone since the last call (0 while it is bypassed).
    float takeWetPeak() { return wetPeak.exchange(0.0f); }
    float barCents() const { return lastBarCents.load(); }
    float glidePhase() const { return lastGlidePhase.load(); }
    // Picking attacks detected so far (they restart the glide in TRIGGER mode).
    int pickAttacks() const { return lastPickAttacks.load(); }
    double hostBpm() const { return lastBpm.load(); }
    // What HOLD holds and plays, for its screen.
    hold::HoldDisplay& holdDisplay() { return holdEngine.display(); }

private:
    struct Batch;
    class SnapshotAction;

    void parameterValueChanged(int, float) override {}
    void parameterGestureChanged(int, bool starting) override;
    void commitEdit(const juce::ValueTree& before);
    juce::ValueTree editState(); // presetState() with the level switches
    void processInternal(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi);
    // The input, delayed by the latency, into `dryOut` (the host-bypass path, kept current always).
    void delayDry(const juce::AudioBuffer<float>& buffer, int numSamples);
    void setParameter(const char* id, float plainValue);
    void resetForPreset();
    juce::ValueTree shapeToTree() const;
    void shapeFromTree(const juce::ValueTree& tree);
    void setParametersExactly(const juce::ValueTree& paramsTree, bool missingToDefault, bool gestures = true);
    // The sound: every parameter but the instance's own (or with them) and the drawn shape. A user
    // preset file holds this.
    juce::ValueTree presetState(bool withInstance = false, const juce::Identifier& type = {});
    void applyPresetState(const juce::ValueTree& preset, bool missingToDefault = false, bool gestures = true);
    // One A/B slot: presetState() and which preset it is (name, factory or user), with that preset as
    // loaded (for the * mark). The project is saved from slots alone, so this is the one place for
    // anything the sound gains that is not a parameter; a new parameter is in it already.
    juce::ValueTree slotState();
    void applySlotState(const juce::ValueTree& slot, bool missingToDefault, bool gestures);
    void captureBaseline();
    void processChunk(juce::AudioBuffer<float>& buffer, int start, int num, const spx::SpxSettings& spxSettings,
                      sway::SwaySettings glide, int midiNoteAt, const hold::HoldSettings& holdSettings);

    std::array<juce::UndoManager, 2> undos;
    int openEdits = 0;  // gestures (or beginEdit) under way
    int quietEdits = 0; // inside a Batch: single parameter gestures are not steps of their own
    juce::ValueTree editBefore;
    juce::AudioProcessorValueTreeState apvts;
    int currentFactoryPreset = 0;
    juce::String presetName { "Glide Guitar" };
    bool presetIsUser = false;
    juce::ValueTree baselineState; // the preset as loaded: presetState() at that moment
    std::array<juce::ValueTree, 2> abStates;
    int abIndex = 0;
    float scale = 1.0f;

    // The custom shape is edited on the message thread and copied for the audio thread under a lock
    // that the audio thread only ever tries.
    sway::Shape customShape;
    juce::SpinLock shapeLock;
    sway::Shape audioCustomShape;
    std::array<sway::Shape, sway::kShapePresetNames.size()> presetShapes;

    spx::SpxEngine spxEngine;
    sway::SwayProcessor glideProcessor;
    hold::HoldEngine holdEngine;
    // MIDI notes of the block for HOLD, and those of the piece being processed.
    static constexpr int kMaxHoldMidi = 64;
    std::array<hold::MidiEvent, kMaxHoldMidi> holdMidi {}, chunkMidi {};
    int numHoldMidi = 0;
    juce::AudioBuffer<float> work;
    // Where the input stands between MONO (0) and STEREO (1), gliding when the mode changes.
    float stereoMix = 1.0f;
    int maxBlock = 0;
    double currentRate = 48000.0;

    // Host bypass keeps the latency, and crossfades: into the delayed input when it starts, back once
    // the cleared engines have caught up when it ends.
    juce::AudioBuffer<float> bypassDelay, dryOut;
    int bypassPos = 0;
    bool wasBypassed = false;
    int fadeLength = 1;
    // The order that plays (it follows glide_position through a fade) and what is left of the fade.
    bool activePost = false;
    int orderFadeLeft = 0;
    int inputRampLeft = 0; // the input fading into engines just cleared
    int bypassFadeLeft = 0; // still processing while fading into the bypass
    int resumeLeft = 0;     // the latency and then a fade, after the bypass

    float pitchBend = 0.0f;  // -1 .. 1
    float modWheel = 0.0f;   // 0 .. 1
    float expression = 0.0f; // 0 .. 1
    std::atomic<float> inputPeak { 0.0f }, outputPeak { 0.0f }, wetPeak { 0.0f }, lastBarCents { 0.0f }, lastGlidePhase { 0.0f };
    std::atomic<int> lastPickAttacks { 0 };
    std::atomic<double> lastBpm { 120.0 };

    struct RawParams
    {
        std::atomic<float>*erRoomSize, *erLiveness, *erDelay, *erLpf, *balance, *outLevel, *input, *inputSwitch,
            *outputSwitch, *bypass, *glideOn, *glideMode, *glidePeriod, *glideDivision, *glideDepth, *glideDirection,
            *glideRandom, *glideSmooth, *glideShape, *glideTriggerSource, *glideSensitivity, *glideManualSource,
            *glideBar, *glideTwinDetune, *glideTwinLevel, *glideMix, *glidePosition, *outputMode, *glideDynamics,
            *inputMode, *holdOn, *holdMode, *holdLatch, *holdSensitivity, *holdFollow, *holdSize, *holdDensity,
            *holdSmear, *holdSpread, *holdReverse, *holdDuck, *holdDecay, *holdLevel, *holdFeed, *holdCharacter,
            *holdRoot;
    } raw;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Glide90Processor)
};

} // namespace glide90
