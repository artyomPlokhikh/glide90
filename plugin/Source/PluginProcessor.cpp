#include "PluginProcessor.h"

#include "PluginEditor.h"
#include "RomPrograms.h"

#include <cmath>
#include <cstring>
#include <limits>
#include <utility>

namespace glide90
{
namespace
{

const juce::Identifier kPresetProp { "preset" }, kPresetNameProp { "presetName" }, kScaleProp { "uiScale" };
// The preset as loaded, kept with each A/B slot: what the * after the preset's name compares against.
const juce::Identifier kBaselineTree { "BASELINE" };
const juce::Identifier kShapeTree { "SHAPE" }, kPointTree { "POINT" }, kXProp { "x" }, kYProp { "y" };
const juce::Identifier kPresetFileTree { "GLIDE90_PRESET" };
const juce::Identifier kPresetIsUserProp { "presetIsUser" };
const juce::Identifier kAbTree { "AB" }, kAbSlotProp { "slot" }, kAbIndexProp { "selected" };
// HOLD's frozen sound, saved with the project (not a preset's or an A/B slot's).
const juce::Identifier kHoldFrozenTree { "HoldFrozen" }, kHoldRateProp { "rate" }, kHoldReadFromProp { "readFrom" },
    kHoldSamplesProp { "samples" };
constexpr const char* kPresetExtension = ".glide90";

// The only parameters that belong to the plugin instance rather than to the sound: INPUT and the
// input and output modes, which match the plugin to the user's rig (the guitar's level, a mono or
// stereo track, one amp or two), and HOLD's FREEZE key, which is played like a footswitch. A and B
// share them and presets leave them alone; the editor marks the rig's ones with a lock. Everything
// else is part of the sound: in each A/B slot, in presets and in undo (INPUT and the modes are in
// undo too; FREEZE is not).
const juce::StringArray& instanceParameters()
{
    static const juce::StringArray ids { ids::input, ids::inputMode, ids::outputMode, ids::holdFreeze };
    return ids;
}

// Factory presets. The defaults are Glide Guitar, the first; the others are built on the plain
// base below (a short reverse gate, a slow free sine), each with its own values on top. The Glide
// values are starting points by ear, not documented settings.
struct FactoryPreset
{
    const char* name;
    bool onPlainBase;
    std::vector<std::pair<const char*, float>> values;
};

const std::vector<std::pair<const char*, float>>& plainBase()
{
    static const std::vector<std::pair<const char*, float>> base {
        { ids::gateRoomSize, 3.3f },   { ids::gateLiveness, 5.0f },     { ids::gatePreDelay, 25.0f },
        { ids::glideMode, 0.0f },    { ids::glidePeriod, 333.0f },  { ids::glideDepth, 30.0f },
        { ids::glideShape, (float) sway::ShapePreset::Sine },       { ids::glideRandom, 0.15f },
        { ids::glideSmooth, 0.2f },  { ids::pickSensitivity, 0.5f }, { ids::glideDynamics, 0.0f },
    };
    return base;
}

// GLIDE90's presets: GLIDE, the reverse gate and HOLD.
const std::vector<FactoryPreset>& rackPresets()
{
    using sway::ShapePreset;
    auto shape = [](ShapePreset p) { return (float) p; };
    static const std::vector<FactoryPreset> presets {
        // Pushing the bar down on each strum into a long reverse gate; a harder strike bends deeper.
        { "Glide Guitar", false, {} },
        { "Steep Swell", true,
          { { ids::gateRoomSize, 20.0f }, { ids::gateLiveness, 10.0f }, { ids::gatePreDelay, 0.1f }, { ids::glideMode, 2.0f }, { ids::glidePeriod, 450.0f }, { ids::glideDepth, 35.0f },
            { ids::glideShape, shape(ShapePreset::Dive) }, { ids::glideRandom, 0.2f }, { ids::glideSmooth, 0.2f } } },
        { "Drowned Shimmy", true,
          { { ids::gateRoomSize, 20.0f }, { ids::gateLiveness, 7.0f }, { ids::gatePreDelay, 75.0f }, { ids::glidePeriod, 900.0f }, { ids::glideDepth, 45.0f }, { ids::glideShape, shape(ShapePreset::Shimmy) },
            { ids::glideRandom, 0.5f }, { ids::glideSmooth, 0.35f } } },
        // Soft playing with the bar and the guitar's tone knob down.
        { "Dark Slow Bend", true,
          { { ids::gateRoomSize, 20.0f }, { ids::gateLiveness, 5.0f }, { ids::gatePreDelay, 25.0f }, { ids::gateLpf, 8.0f /* 2.5 kHz */ }, { ids::glidePeriod, 1200.0f }, { ids::glideDepth, 25.0f }, { ids::glideRandom, 0.4f },
            { ids::glideSmooth, 0.5f } } },
        // Hardly a bend at all: a clean part that never quite sits still.
        { "Slow Drift", true,
          { { ids::gateRoomSize, 8.0f }, { ids::gateLiveness, 5.0f }, { ids::gatePreDelay, 10.0f }, { ids::dryLevel, -5.2f }, { ids::gateLevel, -6.9f },
            { ids::glidePeriod, 2400.0f },
            { ids::glideDepth, 8.0f }, { ids::glideDirection, 1.0f }, { ids::glideRandom, 0.7f }, { ids::glideSmooth, 0.6f } } },
        // Everything at once, the bar after the reverb: a wall.
        { "Wall", true,
          { { ids::gateRoomSize, 20.0f }, { ids::gateLiveness, 10.0f }, { ids::gatePreDelay, 0.1f }, { ids::routing, 1.0f /* BENT TAIL */ }, { ids::glidePeriod, 1200.0f },
            { ids::glideDepth, 25.0f }, { ids::glideRandom, 0.4f }, { ids::glideSmooth, 0.4f } } },
        { "Seasick (Sync)", true,
          { { ids::glideMode, 1.0f }, { ids::glideDivision, 3.0f }, { ids::glideShape, shape(ShapePreset::Sine) },
            { ids::glideDepth, 15.0f }, { ids::glideDirection, 1.0f }, { ids::glideRandom, 0.3f } } },
        { "Bar-Long Swell (Sync)", true,
          { { ids::gateRoomSize, 8.0f }, { ids::gateLiveness, 5.0f }, { ids::gatePreDelay, 10.0f }, { ids::glideMode, 1.0f }, { ids::glideDivision, 2.0f }, { ids::glideShape, shape(ShapePreset::Swell) },
            { ids::glideDepth, 30.0f }, { ids::glideRandom, 0.1f } } },
        { "Pitch-Bend Bar", true,
          { { ids::glideMode, 3.0f }, { ids::glideManualSource, (float) ManualSource::PitchBend },
            { ids::glideDepth, 200.0f }, { ids::glideDirection, 1.0f }, { ids::glideSmooth, 0.15f } } },
        { "Expression Bar", true,
          { { ids::gateRoomSize, 20.0f }, { ids::gateLiveness, 4.0f }, { ids::gatePreDelay, 25.0f }, { ids::glideMode, 3.0f }, { ids::glideManualSource, (float) ManualSource::Expression },
            { ids::glideDepth, 100.0f }, { ids::glideSmooth, 0.3f } } },
        // HOLD: each strum held and bent by the bar on the next one, swelling backwards through the gate.
        { "Held Chord", true,
          { { ids::gateRoomSize, 20.0f }, { ids::gateLiveness, 7.0f }, { ids::gatePreDelay, 25.0f }, { ids::glideMode, 2.0f },
            { ids::glidePeriod, 600.0f }, { ids::glideDepth, 40.0f }, { ids::glideShape, shape(ShapePreset::Dive) },
            { ids::holdOn, 1.0f }, { ids::holdDuck, 0.7f }, { ids::holdLevel, -1.9f }, { ids::holdSpread, 6.0f } } },
        // Strum to capture, then play the held chord from a keyboard and bend it with the wheel.
        { "Sampled Feedback", true,
          { { ids::gateRoomSize, 20.0f }, { ids::gateLiveness, 5.0f }, { ids::glideMode, 3.0f },
            { ids::glideManualSource, (float) ManualSource::PitchBend }, { ids::glideDepth, 200.0f },
            { ids::glideDirection, 1.0f }, { ids::glideSmooth, 0.15f }, { ids::routing, 7.0f /* BENT SWELL */ }, { ids::holdOn, 1.0f },
            { ids::holdMode, (float) hold::Mode::Midi }, { ids::holdDuck, 0.3f }, { ids::holdLevel, -0.9f },
            { ids::holdSize, 250.0f }, { ids::holdDensity, 3.0f }, { ids::holdSpread, 4.0f } } },
        // FREEZE a chord into a slow, thick wall that keeps drifting with the bar.
        { "Frozen Wall", true,
          { { ids::gateRoomSize, 20.0f }, { ids::gateLiveness, 8.0f }, { ids::gatePreDelay, 50.0f }, { ids::glidePeriod, 2400.0f },
            { ids::glideDepth, 20.0f }, { ids::glideRandom, 0.6f }, { ids::glideSmooth, 0.5f }, { ids::holdOn, 1.0f },
            { ids::holdMode, (float) hold::Mode::Manual }, { ids::holdSize, 300.0f }, { ids::holdDensity, 6.0f },
            { ids::holdSmear, 400.0f }, { ids::holdSpread, 20.0f }, { ids::holdReverse, 0.2f }, { ids::holdDuck, 0.2f },
            { ids::holdLevel, -1.9f } } },
        // Grains of the last moments trailing behind the guitar, some backwards.
        { "Smear Trail", true,
          { { ids::gateRoomSize, 8.0f }, { ids::gateLiveness, 5.0f }, { ids::gatePreDelay, 10.0f }, { ids::dryLevel, -8.0f }, { ids::gateLevel, -4.4f },
            { ids::glidePeriod, 1200.0f }, { ids::glideDepth, 15.0f }, { ids::glideRandom, 0.4f }, { ids::holdOn, 1.0f },
            { ids::holdMode, (float) hold::Mode::Live }, { ids::holdSize, 120.0f }, { ids::holdDensity, 5.0f },
            { ids::holdSmear, 500.0f }, { ids::holdSpread, 12.0f }, { ids::holdReverse, 0.4f }, { ids::holdDuck, 0.5f },
            { ids::routing, 3.0f /* HOLD BESIDE */ }, { ids::holdLevel, -4.4f } } },
        { "Reverse Gate Only", true, { { ids::glideOn, 0.0f } } },
        { "Short Reverse", true,
          { { ids::gateRoomSize, 8.0f }, { ids::gateLiveness, 5.0f }, { ids::gatePreDelay, 10.0f }, { ids::glideOn, 0.0f } } },
    };
    return presets;
}

bool usesHold(const FactoryPreset& p)
{
    for (const auto& [id, value] : p.values)
        if (juce::String(id) == ids::holdOn && value > 0.5f)
            return true;
    return false;
}

// This product's presets: GLIDE90's own, or for LITE those without HOLD, the routing as the modules'
// order and DRY / LEVEL as MIX.
const std::vector<FactoryPreset>& factoryPresets()
{
    static const std::vector<FactoryPreset> presets = [] {
        if constexpr (!kLite)
            return rackPresets();
        std::vector<FactoryPreset> out;
        for (const auto& p : rackPresets())
        {
            if (usesHold(p))
                continue;
            FactoryPreset own { p.name, p.onPlainBase, {} };
            float dry = -1.0f, gate = -1.0f;
            for (const auto& [id, value] : p.values)
            {
                const juce::String key(id);
                if (key == ids::routing)
                    own.values.push_back({ ids::glidePosition, (float) (routingAt(juce::roundToInt(value)).gate == GateBend::Out
                                                                            ? GlidePosition::Post : GlidePosition::Pre) });
                else if (key == ids::dryLevel)
                    dry = gainOf(value);
                else if (key == ids::gateLevel)
                    gate = gainOf(value);
                else if (inProduct(key))
                    own.values.push_back({ id, value });
            }
            if (dry >= 0.0f || gate >= 0.0f)
            {
                // GLIDE90's defaults: DRY off, the gate at 0 dB.
                dry = juce::jmax(0.0f, dry);
                gate = gate < 0.0f ? 1.0f : gate;
                own.values.push_back({ ids::gateMix, gate / juce::jmax(1.0e-6f, dry + gate) });
            }
            out.push_back(std::move(own));
        }
        return out;
    }();
    return presets;
}

void raiseTo(std::atomic<float>& a, float v)
{
    float cur = a.load();
    while (v > cur && !a.compare_exchange_weak(cur, v))
    {
    }
}

// The SPX90 program that runs: 17, REVERSE GATE.
constexpr int kProgram = 17;

int choice(const std::atomic<float>* p)
{
    return juce::roundToInt(p->load());
}

} // namespace

// Several parameter changes made as one: one undo step when `record`, none otherwise. The gestures
// of the single parameters inside it are not steps of their own.
struct Glide90Processor::Batch
{
    Batch(Glide90Processor& p, bool recordIt) : proc(p), record(recordIt)
    {
        if (record && proc.quietEdits == 0 && proc.openEdits == 0)
            before = proc.editState();
        ++proc.quietEdits;
    }

    ~Batch()
    {
        if (--proc.quietEdits == 0 && before.isValid())
            proc.commitEdit(before);
    }

    Glide90Processor& proc;
    bool record;
    juce::ValueTree before;
};

// One undo step: the whole setting before and after it.
class Glide90Processor::SnapshotAction : public juce::UndoableAction
{
public:
    SnapshotAction(Glide90Processor& p, juce::ValueTree from, juce::ValueTree to)
        : proc(p), before(std::move(from)), after(std::move(to))
    {
    }

    bool perform() override
    {
        if (std::exchange(alreadyDone, false)) // recorded after the edit happened
            return true;
        restore(after);
        return true;
    }

    bool undo() override
    {
        restore(before);
        return true;
    }

private:
    void restore(const juce::ValueTree& state)
    {
        const Batch quiet(proc, false);
        proc.applyPresetState(state);
    }

    Glide90Processor& proc;
    juce::ValueTree before, after;
    bool alreadyDone = true;
};

Glide90Processor::Glide90Processor()
    : AudioProcessor(BusesProperties()
                         .withInput("Input", juce::AudioChannelSet::stereo(), true)
                         .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "GLIDE90", createParameterLayout())
{
    auto p = [this](const char* id) {
        if (auto* v = apvts.getRawParameterValue(id))
            return v;
        return &fixedParams.emplace_back(fixedValue(id));
    };
    raw = { p(ids::gateRoomSize),        p(ids::gateLiveness),     p(ids::gatePreDelay),
            p(ids::gateLpf),          p(ids::gateLevel),           p(ids::output),         p(ids::input),
            p(ids::gateOn),           p(ids::glideOn),
            p(ids::glideMode),        p(ids::glidePeriod),       p(ids::glideDivision),  p(ids::glideDepth),
            p(ids::glideDirection),   p(ids::glideRandom),       p(ids::glideSmooth),    p(ids::glideShape),
            p(ids::glideTriggerSource), p(ids::pickSensitivity), p(ids::glideManualSource), p(ids::glideBar),
            p(ids::routing),
            p(ids::outputMode),       p(ids::glideDynamics),     p(ids::inputMode),
            p(ids::holdOn),           p(ids::holdMode),          p(ids::holdFreeze),
            p(ids::holdSize),         p(ids::holdDensity),       p(ids::holdSmear),      p(ids::holdSpread),
            p(ids::holdReverse),      p(ids::holdDuck),          p(ids::holdDecay),      p(ids::holdLevel),
            p(ids::holdCharacter),     p(ids::holdRoot),          p(ids::holdSwell),      p(ids::holdTail),
            p(ids::holdVoices),       p(ids::dryLevel),          p(ids::gateMix),        p(ids::glidePosition) };
    for (size_t i = 0; i < presetShapes.size(); ++i)
        presetShapes[i] = sway::makeShape((sway::ShapePreset) i);
    customShape = audioCustomShape = presetShapes[0];
    for (auto* param : getParameters())
        param->addListener(this);
    presetName = factoryPresets().front().name; // the defaults
    captureBaseline();
}

Glide90Processor::~Glide90Processor()
{
    for (auto* param : getParameters())
        param->removeListener(this);
}

bool Glide90Processor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    const auto in = layouts.getMainInputChannelSet();
    return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo()
           && (in == juce::AudioChannelSet::mono() || in == juce::AudioChannelSet::stereo());
}

void Glide90Processor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    currentRate = sampleRate;
    maxBlock = juce::jmax(1, samplesPerBlock);
    spxEngine.prepare(sampleRate, maxBlock);
    glideProcessor.prepare(sampleRate);
    dryBender.prepare(sampleRate);
    gateBender.prepare(sampleRate);
    holdEngine.prepare(sampleRate);
    {
        // Room for a whole capture, so that the audio thread copies without allocating.
        const juce::SpinLock::ScopedLockType lock(heldLock);
        const auto room = (size_t) std::ceil(hold::HoldEngine::kSlotSeconds * sampleRate) + 8;
        heldCopy.reserve(room);
        recentCopy.reserve(room);
    }
    // The inputs, their sum, a stand-in for a missing right output; the bar; HOLD's send and output;
    // the effect's input and output.
    work.setSize(11, maxBlock);
    const int latency = spxEngine.latencySamples() + dryBender.latencySamples();
    setLatencySamples(latency);
    bypassDelay.setSize(2, latency + 1);
    dryOut.setSize(2, maxBlock);
    fadeLength = juce::jmax(1, juce::roundToInt(0.01 * sampleRate));
    for (auto* gain : { &inputGain, &outputGain, &dryGain, &gateGain })
        gain->reset(sampleRate, 0.02);
    reset();
}

void Glide90Processor::reset()
{
    gateBenderAfter = currentRouting().gate == GateBend::Out;
    moveFadeLeft = 0;
    effectRampLeft = 0;
    stereoMix = choice(raw.inputMode) == (int) InputMode::Stereo ? 1.0f : 0.0f;
    inputGain.setCurrentAndTargetValue(gainOf(raw.input->load()));
    outputGain.setCurrentAndTargetValue(gainOf(raw.output->load()));
    dryGain.setCurrentAndTargetValue(dryLevelGain());
    gateGain.setCurrentAndTargetValue(gateLevelGain());
    spxEngine.reset();
    glideProcessor.reset();
    dryBender.reset();
    gateBender.reset();
    holdEngine.reset();
    holdAgainAfterReset();
    bypassDelay.clear();
    bypassPos = 0;
    wasBypassed = false;
    bypassFadeLeft = resumeLeft = 0;
}

void Glide90Processor::delayDry(const juce::AudioBuffer<float>& buffer, int numSamples)
{
    if (dryOut.getNumSamples() < numSamples)
        dryOut.setSize(2, numSamples, false, false, true); // only if the host sends more than it announced
    const int size = bypassDelay.getNumSamples();
    const int numIn = juce::jmin(getTotalNumInputChannels(), buffer.getNumChannels());
    int pos = bypassPos;
    for (int ch = 0; ch < 2; ++ch)
    {
        const float* in = numIn > 0 ? buffer.getReadPointer(juce::jmin(ch, numIn - 1)) : nullptr;
        float* out = dryOut.getWritePointer(ch);
        float* line = bypassDelay.getWritePointer(ch);
        pos = bypassPos;
        for (int i = 0; i < numSamples; ++i)
        {
            line[pos] = in != nullptr ? in[i] : 0.0f;
            pos = (pos + 1) % size;
            out[i] = line[pos];
        }
    }
    bypassPos = pos;
}

void Glide90Processor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    delayDry(buffer, n);
    if (wasBypassed)
    {
        // The engines still hold what played before the bypass (two seconds of it in the reverb):
        // start them clean, and play the delayed input until their own output, one latency later,
        // can take over.
        spxEngine.reset();
        glideProcessor.reset();
        dryBender.reset();
        gateBender.reset();
        holdEngine.reset();
        holdAgainAfterReset();
        inputRampLeft = fadeLength;
        wasBypassed = false;
        bypassFadeLeft = 0;
        resumeLeft = getLatencySamples() + fadeLength;
    }
    processInternal(buffer, midi);

    const int latency = getLatencySamples();
    for (int i = 0; i < n && resumeLeft > 0; ++i, --resumeLeft)
    {
        const int pos = latency + fadeLength - resumeLeft;
        const float w = pos < latency ? 0.0f : (float) (pos - latency) / (float) fadeLength;
        for (int ch = 0; ch < 2; ++ch)
        {
            const float d = dryOut.getSample(ch, i);
            buffer.setSample(ch, i, d + w * (buffer.getSample(ch, i) - d));
        }
    }
}

void Glide90Processor::processInternal(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    const int n = buffer.getNumSamples();

    int noteAt = -1;
    float noteVelocity = 1.0f;
    numHoldMidi = 0;
    if (!midi.isEmpty())
        midiSeen.store(true);
    // A project that reopened frozen: its sound, once FREEZE is on (so that FREEZE captures nothing).
    if (heldPending.load(std::memory_order_acquire) && raw.holdFreeze->load() > 0.5f)
    {
        const juce::SpinLock::ScopedTryLockType lock(heldLock);
        if (lock.isLocked())
        {
            holdEngine.restoreHeld(pendingHeld.data(), (int) pendingHeld.size(), pendingRate, pendingReadFrom);
            heldPending.store(false);
        }
    }
    for (const auto meta : midi)
    {
        const auto m = meta.getMessage();
        if ((m.isNoteOn() || m.isNoteOff()) && numHoldMidi < kMaxHoldMidi)
            holdMidi[(size_t) numHoldMidi++] = { meta.samplePosition, m.getNoteNumber(), m.isNoteOn(),
                                                 m.getFloatVelocity(), hold::MidiEvent::Kind::Note };
        else if ((m.isSustainPedalOn() || m.isSustainPedalOff()) && numHoldMidi < kMaxHoldMidi)
            holdMidi[(size_t) numHoldMidi++] = { meta.samplePosition, 0, m.isSustainPedalOn(), 1.0f,
                                                 hold::MidiEvent::Kind::Sustain };
        if (m.isPitchWheel())
            pitchBend = (float) (m.getPitchWheelValue() - 8192) / 8192.0f;
        else if (m.isControllerOfType(1))
            modWheel = (float) m.getControllerValue() / 127.0f;
        else if (m.isControllerOfType(11))
            expression = (float) m.getControllerValue() / 127.0f;
        else if (m.isNoteOn() && noteAt < 0)
        {
            noteAt = meta.samplePosition;
            noteVelocity = m.getFloatVelocity();
        }
    }

    spx::SpxSettings s;
    s.program = kProgram;
    s.er = spx::romProgram(kProgram).er; // the reverse gate's type and density
    s.er.roomSize = (float) juce::roundToInt(raw.gateRoomSize->load() * 10.0f) / 10.0f;
    s.er.liveness = choice(raw.gateLiveness);
    s.er.delayMs = (float) juce::roundToInt(raw.gatePreDelay->load() * 10.0f) / 10.0f;
    s.er.lpfHz = spx::kLpfValuesHz[(size_t) juce::jlimit(0, (int) spx::kLpfValuesHz.size() - 1, choice(raw.gateLpf))];
    s.unmixed = true; // DRY and the gate's LEVEL instead of the unit's BALANCE
    s.bypass = raw.gateOn->load() < 0.5f;
    inputGain.setTargetValue(gainOf(raw.input->load()));
    outputGain.setTargetValue(gainOf(raw.output->load()));
    dryGain.setTargetValue(dryLevelGain());
    gateGain.setTargetValue(gateLevelGain());

    // Host tempo and position for SYNC.
    double bpm = 120.0, hostBeats = -1.0;
    if (auto* head = getPlayHead())
        if (const auto pos = head->getPosition())
        {
            if (const auto b = pos->getBpm())
                bpm = *b;
            if (const auto ppq = pos->getPpqPosition(); ppq && pos->getIsPlaying())
                hostBeats = *ppq;
        }
    lastBpm.store(bpm);

    sway::SwaySettings w;
    w.enabled = raw.glideOn->load() > 0.5f;
    w.mode = (sway::GlideMode) choice(raw.glideMode);
    w.cycleSeconds = raw.glidePeriod->load() / 1000.0;
    w.syncBeats = kDivisions[juce::jlimit(0, (int) std::size(kDivisions) - 1, choice(raw.glideDivision))].beats;
    w.bpm = bpm;
    w.hostBeats = hostBeats;
    w.depthCents = raw.glideDepth->load();
    w.direction = (sway::BendDirection) choice(raw.glideDirection);
    w.random = raw.glideRandom->load();
    w.smooth = raw.glideSmooth->load();
    w.triggerFromPick = (TriggerSource) choice(raw.glideTriggerSource) == TriggerSource::Pick;
    w.sensitivity = raw.pickSensitivity->load();
    w.dynamics = raw.glideDynamics->load();
    w.midiTriggerVelocity = noteVelocity;

    // MANUAL: where the bar is. Pitch bend is bipolar; the others push from rest, or sweep across
    // the open pitch when the direction is BOTH.
    if ((ManualSource) choice(raw.glideManualSource) == ManualSource::PitchBend)
        w.manual = -pitchBend; // wheel down pushes the bar
    else
    {
        float v = raw.glideBar->load();
        if ((ManualSource) choice(raw.glideManualSource) == ManualSource::ModWheel)
            v = modWheel;
        else if ((ManualSource) choice(raw.glideManualSource) == ManualSource::Expression)
            v = expression;
        w.manual = w.direction == sway::BendDirection::Both ? 2.0f * v - 1.0f : v;
    }

    const int shapeIndex = choice(raw.glideShape);
    if (shapeIndex >= (int) presetShapes.size())
    {
        const juce::SpinLock::ScopedTryLockType lock(shapeLock);
        // When the editor holds the lock, keep last block's copy.
        if (lock.isLocked())
            audioCustomShape = customShape;
        w.shape = &audioCustomShape;
    }
    else
        w.shape = &presetShapes[(size_t) juce::jmax(0, shapeIndex)];

    hold::HoldSettings h;
    h.enabled = raw.holdOn->load() > 0.5f;
    h.mode = (hold::Mode) choice(raw.holdMode);
    h.frozen = raw.holdFreeze->load() > 0.5f;
    h.swellMs = raw.holdSwell->load();
    h.tailMs = raw.holdTail->load();
    const int voices = choice(raw.holdVoices);
    h.voicing = voices == 0 ? hold::Voicing::Poly : voices == 1 ? hold::Voicing::Mono : hold::Voicing::Glide;
    h.glideMs = kHoldGlideMs[(size_t) juce::jlimit(0, (int) std::size(kHoldGlideMs) - 1, voices - 2)];
    // One PICK SENS for the plugin: how hard the player picks is the same for the glide and HOLD.
    h.sensitivity = raw.pickSensitivity->load();
    h.sizeMs = raw.holdSize->load();
    h.density = choice(raw.holdDensity);
    h.smearMs = raw.holdSmear->load();
    h.spreadCents = raw.holdSpread->load();
    h.reverse = raw.holdReverse->load();
    h.duck = raw.holdDuck->load();
    const float decay = raw.holdDecay->load();
    h.decaySeconds = decay >= kHoldDecayForever ? std::numeric_limits<float>::infinity() : decay;
    h.level = gainOf(raw.holdLevel->load());
    // The ROUTING: where HOLD goes and how GLIDE bends the gate (DRY it bends whenever it is on). With
    // the reverse gate bypassed there is no gate to feed: HOLD goes to the output instead.
    const auto& routing = currentRouting();
    h.feed = s.bypass || !routing.holdIntoGate ? hold::Feed::Out : hold::Feed::Gate;
    const auto gateBend = routing.gate;
    const bool wantAfter = gateBend == GateBend::Out ? true : gateBend == GateBend::In ? false : gateBenderAfter;
    h.bend = w.enabled && holdBendsItself(routing, h.feed == hold::Feed::Gate);
    h.character = (hold::Character) choice(raw.holdCharacter);
    h.autoRoot = choice(raw.holdRoot) == kHoldRootAuto;
    h.rootNote = h.autoRoot ? 60 : choice(raw.holdRoot); // AUTO plays at C3 until a clear note is picked

    const double beatsPerSample = bpm / 60.0 / currentRate;
    for (int start = 0; start < n;)
    {
        // The gate's bender moving to the other side of the reverb: fade the effect out; in the silence
        // move it and clear the reverb and the bender (they hold what the old way fed them); stay
        // silent until the new way's output arrives, one latency later; fade back in. No click, the
        // reverb's tail starts afresh, and the direct signal plays on throughout.
        const int silent = getLatencySamples();
        if (moveFadeLeft == 0 && wantAfter != gateBenderAfter)
            moveFadeLeft = 2 * fadeLength + silent;
        int num = juce::jmin(maxBlock, n - start);
        if (moveFadeLeft > fadeLength + silent)
            num = juce::jmin(num, moveFadeLeft - fadeLength - silent);
        else if (moveFadeLeft > 0)
            num = juce::jmin(num, moveFadeLeft);

        sway::SwaySettings chunk = w;
        if (hostBeats >= 0.0)
            chunk.hostBeats = hostBeats + start * beatsPerSample;
        hold::HoldSettings holdChunk = h;
        int chunkNotes = 0;
        for (int i = 0; i < numHoldMidi; ++i)
            if (const auto& e = holdMidi[(size_t) i]; e.sample >= start && (e.sample < start + num || start + num == n))
            {
                chunkMidi[(size_t) chunkNotes] = e;
                chunkMidi[(size_t) chunkNotes++].sample -= start;
            }
        holdChunk.midi = chunkMidi.data();
        holdChunk.numMidi = chunkNotes;
        Bends bends;
        bends.dry = w.enabled;
        // While it moves, the bender bends only where it is wanted (it fades out on the wrong side).
        bends.gate = w.enabled && gateBend != GateBend::Off && (gateBend == GateBend::Out) == gateBenderAfter;
        processChunk(buffer, start, num, s, chunk, noteAt >= start && noteAt < start + num ? noteAt - start : -1,
                     holdChunk, bends);

        if (moveFadeLeft > 0)
        {
            moveFadeLeft -= num;
            if (moveFadeLeft == fadeLength + silent)
            {
                gateBenderAfter = !gateBenderAfter;
                spxEngine.resetEffect();
                gateBender.reset();
                effectRampLeft = fadeLength;
            }
        }
        start += num;
    }
    for (int ch = 2; ch < buffer.getNumChannels(); ++ch)
        buffer.clear(ch, 0, n);
    lastBarCents.store(glideProcessor.barCents());
    lastGlidePhase.store(glideProcessor.phase());
    lastPickAttacks.store(glideProcessor.pickAttacks());
    lastPicksHeard.store(holdEngine.pickAttacks()); // HOLD's detector listens even while HOLD is off
    shareHeld();
}

void Glide90Processor::processChunk(juce::AudioBuffer<float>& buffer, int start, int num, const spx::SpxSettings& s,
                                    sway::SwaySettings w, int midiNoteAt, const hold::HoldSettings& h,
                                    const Bends& bends)
{
    w.midiTriggerAt = midiNoteAt;
    const int numIn = juce::jmin(getTotalNumInputChannels(), buffer.getNumChannels());
    const bool twoInputs = numIn >= 2;
    float* inL = work.getWritePointer(0);
    float* inR = work.getWritePointer(1);
    float* sum = work.getWritePointer(2);
    if (twoInputs)
    {
        // MONO sends the sum on both sides, STEREO each input on its own; switching glides between
        // them. The reverb hears the sum either way.
        const float* a = buffer.getReadPointer(0, start);
        const float* b = buffer.getReadPointer(1, start);
        for (int i = 0; i < num; ++i)
            sum[i] = 0.5f * (a[i] + b[i]);
        const float target = choice(raw.inputMode) == (int) InputMode::Stereo ? 1.0f : 0.0f;
        if (juce::exactlyEqual(stereoMix, target))
        {
            juce::FloatVectorOperations::copy(inL, target > 0.5f ? a : sum, num);
            juce::FloatVectorOperations::copy(inR, target > 0.5f ? b : sum, num);
        }
        else
        {
            const float step = 1.0f / (float) fadeLength;
            for (int i = 0; i < num; ++i)
            {
                stereoMix = target > stereoMix ? juce::jmin(target, stereoMix + step) : juce::jmax(target, stereoMix - step);
                inL[i] = sum[i] + stereoMix * (a[i] - sum[i]);
                inR[i] = sum[i] + stereoMix * (b[i] - sum[i]);
            }
        }
    }
    else
    {
        if (numIn == 1)
            juce::FloatVectorOperations::copy(sum, buffer.getReadPointer(0, start), num);
        else
            juce::FloatVectorOperations::clear(sum, num);
        juce::FloatVectorOperations::copy(inL, sum, num);
        juce::FloatVectorOperations::copy(inR, sum, num);
    }
    // INPUT, before anything else hears the guitar: the pick detector, HOLD, the glide and the gate.
    if (inputGain.isSmoothing() || !juce::exactlyEqual(inputGain.getTargetValue(), 1.0f))
        for (int i = 0; i < num; ++i)
        {
            const float g = inputGain.getNextValue();
            inL[i] *= g;
            inR[i] *= g;
            sum[i] *= g;
        }
    // Into cleared engines the input fades in: a sudden start would come back from every reflection.
    for (int i = 0; i < num && inputRampLeft > 0; ++i, --inputRampLeft)
    {
        const float g = (float) (fadeLength - inputRampLeft) / (float) fadeLength;
        inL[i] *= g;
        inR[i] *= g;
        sum[i] *= g;
    }

    // The bar first, from the dry guitar: every path it bends follows it.
    float* bar = work.getWritePointer(4);
    glideProcessor.computeBar(sum, num, w, bar);
    // HOLD hears the dry guitar too, and plays beside the chain: into the gate, or to the output.
    float* holdGate = work.getWritePointer(5);
    float* holdL = work.getWritePointer(6);
    float* holdR = work.getWritePointer(7);
    // Heard in this piece if it was at its start or is at its end (fading in or out).
    const bool holdWasHeard = holdEngine.audible();
    holdEngine.process(sum, bar, num, h, holdGate, holdL, holdR);
    const bool holdHeard = holdWasHeard || holdEngine.audible();

    // What the reverse gate hears: the guitar's sum, bent on its way in (BEND IN), and HOLD, which
    // bends itself. With the gate's bender after the reverb, both go in as they are.
    float* effect = work.getWritePointer(10);
    juce::FloatVectorOperations::copy(effect, sum, num);
    if (!gateBenderAfter)
    {
        float* channels[] = { effect };
        gateBender.process(channels, 1, num, bar, bends.gate);
    }
    if (holdHeard)
        juce::FloatVectorOperations::add(effect, holdGate, num);
    for (int i = 0; i < num && effectRampLeft > 0; ++i, --effectRampLeft)
        effect[i] *= (float) (fadeLength - effectRampLeft) / (float) fadeLength;

    // The direct guitar: one channel from a mono track (the other side is the same), two (one bar for
    // both) from a stereo one, in either mode.
    {
        float* channels[] = { inL, inR };
        dryBender.process(channels, twoInputs ? 2 : 1, num, bar, bends.dry);
        if (!twoInputs)
            juce::FloatVectorOperations::copy(inR, inL, num);
    }

    for (const float* ch : { inL, inR })
    {
        raiseTo(inputPeak, juce::FloatVectorOperations::findMaximum(ch, num));
        raiseTo(inputPeak, -juce::FloatVectorOperations::findMinimum(ch, num));
    }

    float* l = buffer.getWritePointer(0, start);
    float* r = buffer.getNumChannels() > 1 ? buffer.getWritePointer(1, start) : work.getWritePointer(3);
    float* wetL = work.getWritePointer(8);
    float* wetR = work.getWritePointer(9);
    spxEngine.process(inL, inR, effect, l, r, wetL, wetR, num, s);
    raiseTo(wetPeak, s.bypass ? 0.0f : spxEngine.takeWetPeak());
    if (gateBenderAfter)
    {
        // BEND OUT: the whole reverb, and HOLD in it, moves at once.
        float* channels[] = { wetL, wetR };
        gateBender.process(channels, 2, num, bar, bends.gate);
    }
    if (moveFadeLeft > 0)
    {
        // The gate's bender is moving: the effect fades out, stays silent, and fades back in.
        const int silent = getLatencySamples();
        for (int i = 0; i < num; ++i)
        {
            const int left = moveFadeLeft - i;
            const float gain = left > fadeLength + silent ? (float) (left - fadeLength - silent) / (float) fadeLength
                               : left > fadeLength      ? 0.0f
                                                        : (float) (fadeLength - left) / (float) fadeLength;
            wetL[i] *= gain;
            wetR[i] *= gain;
        }
    }
    // DRY and the reverse gate (HOLD in it included), each at its own level.
    auto level = [num](juce::SmoothedValue<float>& gain, float* a, float* b) {
        if (!gain.isSmoothing() && juce::exactlyEqual(gain.getTargetValue(), 1.0f))
            return;
        for (int i = 0; i < num; ++i)
        {
            const float g = gain.getNextValue();
            a[i] *= g;
            b[i] *= g;
        }
    };
    level(dryGain, l, r);
    level(gateGain, wetL, wetR);
    juce::FloatVectorOperations::add(l, wetL, num);
    juce::FloatVectorOperations::add(r, wetR, num);
    if (holdHeard)
    {
        juce::FloatVectorOperations::add(l, holdL, num);
        juce::FloatVectorOperations::add(r, holdR, num);
    }

    // OUTPUT, after everything: the modules, HOLD on either path, the gate on or off.
    if (outputGain.isSmoothing() || !juce::exactlyEqual(outputGain.getTargetValue(), 1.0f))
        for (int i = 0; i < num; ++i)
        {
            const float g = outputGain.getNextValue();
            l[i] *= g;
            r[i] *= g;
        }

    switch ((OutputMode) choice(raw.outputMode))
    {
        case OutputMode::LeftOnly: juce::FloatVectorOperations::copy(r, l, num); break;
        case OutputMode::MonoSum:
            juce::FloatVectorOperations::add(l, r, num);
            juce::FloatVectorOperations::multiply(l, 0.5f, num);
            juce::FloatVectorOperations::copy(r, l, num);
            break;
        case OutputMode::Stereo: break;
    }
    for (const float* ch : { l, r })
    {
        raiseTo(outputPeak, juce::FloatVectorOperations::findMaximum(ch, num));
        raiseTo(outputPeak, -juce::FloatVectorOperations::findMinimum(ch, num));
    }
}

double Glide90Processor::getTailLengthSeconds() const
{
    double tail = 2.2; // ROOM SIZE 20 plus 400 ms delay
    if (raw.holdOn->load() > 0.5f)
    {
        // A held chord falls 60 dB over DECAY, or never dies away: then as long as a host will sensibly
        // wait.
        const float decay = raw.holdDecay->load();
        tail += decay >= kHoldDecayForever ? 20.0 : std::min(20.0, (double) decay);
    }
    return tail;
}

void Glide90Processor::processBlockBypassed(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    // Pass the input through, delayed by the reported latency so the host's compensation still holds.
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    delayDry(buffer, n);
    if (!wasBypassed)
    {
        wasBypassed = true;
        bypassFadeLeft = fadeLength;
        resumeLeft = 0;
    }
    if (bypassFadeLeft > 0)
    {
        // Keep processing for a short fade into the delayed input, then let the engines rest.
        processInternal(buffer, midi);
        for (int i = 0; i < n; ++i)
        {
            const float w = (float) bypassFadeLeft / (float) fadeLength;
            for (int ch = 0; ch < 2; ++ch)
            {
                const float d = dryOut.getSample(ch, i);
                buffer.setSample(ch, i, d + w * (buffer.getSample(ch, i) - d));
            }
            if (bypassFadeLeft > 0)
                --bypassFadeLeft;
        }
    }
    else
        for (int ch = 0; ch < 2; ++ch)
            buffer.copyFrom(ch, 0, dryOut, ch, 0, n);
    for (int ch = 2; ch < buffer.getNumChannels(); ++ch)
        buffer.clear(ch, 0, n);
}

juce::AudioProcessorEditor* Glide90Processor::createEditor()
{
    return new Glide90Editor(*this);
}

// --- Glide shape ---

sway::Shape Glide90Processor::currentShape() const
{
    const int index = choice(raw.glideShape);
    if (index >= 0 && index < (int) presetShapes.size())
        return presetShapes[(size_t) index];
    const juce::SpinLock::ScopedLockType lock(shapeLock);
    return customShape;
}

void Glide90Processor::setCustomShape(const sway::Shape& shape)
{
    {
        const juce::SpinLock::ScopedLockType lock(shapeLock);
        customShape = shape;
    }
    if (choice(raw.glideShape) != (int) presetShapes.size())
        setParameter(ids::glideShape, (float) presetShapes.size());
}

juce::ValueTree Glide90Processor::shapeToTree() const
{
    juce::ValueTree tree(kShapeTree);
    const juce::SpinLock::ScopedLockType lock(shapeLock);
    for (int i = 0; i < customShape.size(); ++i)
    {
        juce::ValueTree p(kPointTree);
        p.setProperty(kXProp, customShape.point(i).x, nullptr).setProperty(kYProp, customShape.point(i).y, nullptr);
        tree.appendChild(p, nullptr);
    }
    return tree;
}

void Glide90Processor::shapeFromTree(const juce::ValueTree& tree)
{
    if (!tree.isValid())
        return;
    std::vector<sway::ShapePoint> points;
    for (const auto& p : tree)
        if (p.hasType(kPointTree) && (int) points.size() < sway::Shape::kMaxPoints)
            points.push_back({ (float) p.getProperty(kXProp, 0.0f), (float) p.getProperty(kYProp, 0.0f) });
    if (points.size() < 2)
        return;
    sway::Shape shape;
    shape.setPoints(points.data(), (int) points.size());
    const juce::SpinLock::ScopedLockType lock(shapeLock);
    customShape = shape;
}

// --- Presets ---

int Glide90Processor::getNumPrograms()
{
    return (int) factoryPresets().size();
}

const juce::String Glide90Processor::getProgramName(int index)
{
    return juce::isPositiveAndBelow(index, getNumPrograms()) ? factoryPresets()[(size_t) index].name : "";
}

void Glide90Processor::setCurrentProgram(int index)
{
    loadFactoryPreset(index);
}

juce::StringArray Glide90Processor::factoryPresetNames() const
{
    juce::StringArray names;
    for (const auto& p : factoryPresets())
        names.add(p.name);
    return names;
}

void Glide90Processor::resetForPreset()
{
    // Everything but the instance's own settings back to its default.
    for (auto* param : getParameters())
        if (auto* p = dynamic_cast<juce::RangedAudioParameter*>(param))
            if (!instanceParameters().contains(p->getParameterID()))
            {
                p->beginChangeGesture();
                p->setValueNotifyingHost(p->getDefaultValue());
                p->endChangeGesture();
            }
}

void Glide90Processor::loadFactoryPreset(int index)
{
    if (!juce::isPositiveAndBelow(index, getNumPrograms()))
        return;
    const auto& p = factoryPresets()[(size_t) index];
    const Batch quiet(*this, false); // a new start, not a step
    currentFactoryPreset = index;
    resetForPreset();
    if (p.onPlainBase)
        for (const auto& [id, value] : plainBase())
            setParameter(id, value);
    for (const auto& [id, value] : p.values)
        setParameter(id, value);
    presetName = p.name;
    presetIsUser = false;
    captureBaseline();
    undoManager().clearUndoHistory(); // of this A/B slot: the other keeps its own
}

juce::File Glide90Processor::userPresetDirectory()
{
    // LITE's presets apart: they have MIX and the modules' order, not HOLD and the ROUTING.
    const char* folder = kLite ? "LITE Presets" : "Presets";
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
        .getChildFile("Glide90")
        .getChildFile(folder);
}

juce::String Glide90Processor::presetExtension()
{
    return kPresetExtension;
}

juce::Array<juce::File> Glide90Processor::userPresetFiles() const
{
    auto files = userPresetDirectory().findChildFiles(juce::File::findFiles, false, juce::String("*") + kPresetExtension);
    files.sort();
    return files;
}

juce::ValueTree Glide90Processor::presetState(bool withInstance, const juce::Identifier& type)
{
    juce::ValueTree preset(type.isValid() ? type : kPresetFileTree);
    juce::ValueTree params(apvts.state.getType());
    for (const auto& child : apvts.copyState()) // the parameters only, not what a restored state left on it
        if (child.hasProperty("id") && (withInstance || !instanceParameters().contains(child.getProperty("id").toString())))
            params.appendChild(child.createCopy(), nullptr);
    preset.appendChild(params, nullptr);
    preset.appendChild(shapeToTree(), nullptr);
    return preset;
}

void Glide90Processor::applyPresetState(const juce::ValueTree& preset, bool missingToDefault, bool gestures)
{
    shapeFromTree(preset.getChildWithName(kShapeTree));
    setParametersExactly(preset.getChildWithName(apvts.state.getType()), missingToDefault, gestures);
}

juce::ValueTree Glide90Processor::slotState()
{
    auto slot = presetState();
    slot.setProperty(kPresetProp, currentFactoryPreset, nullptr)
        .setProperty(kPresetNameProp, presetName, nullptr)
        .setProperty(kPresetIsUserProp, presetIsUser, nullptr);
    slot.appendChild(baselineState.createCopy(), nullptr);
    return slot;
}

void Glide90Processor::applySlotState(const juce::ValueTree& slot, bool missingToDefault, bool gestures)
{
    applyPresetState(slot, missingToDefault, gestures);
    // A slot saved before it had these keeps what is there now.
    currentFactoryPreset = juce::jlimit(0, getNumPrograms() - 1, (int) slot.getProperty(kPresetProp, currentFactoryPreset));
    presetName = slot.getProperty(kPresetNameProp, presetName).toString();
    presetIsUser = (bool) slot.getProperty(kPresetIsUserProp, presetIsUser);
    if (const auto base = slot.getChildWithName(kBaselineTree); base.isValid())
        baselineState = base.createCopy();
    else
        captureBaseline();
}

bool Glide90Processor::saveUserPreset(const juce::String& name)
{
    const auto clean = juce::File::createLegalFileName(name.trim());
    if (clean.isEmpty())
        return false;
    const auto dir = userPresetDirectory();
    if (!dir.createDirectory())
        return false;
    auto xml = presetState().createXml();
    if (xml == nullptr || !xml->writeTo(dir.getChildFile(clean + kPresetExtension)))
        return false;
    presetName = clean;
    presetIsUser = true;
    captureBaseline();
    return true;
}

bool Glide90Processor::loadUserPreset(const juce::File& file)
{
    auto xml = juce::XmlDocument::parse(file);
    if (xml == nullptr)
        return false;
    const auto preset = juce::ValueTree::fromXml(*xml);
    if (!preset.hasType(kPresetFileTree))
        return false;
    const Batch quiet(*this, false); // a new start, not a step
    applyPresetState(preset);
    presetName = file.getFileNameWithoutExtension();
    presetIsUser = true;
    captureBaseline();
    undoManager().clearUndoHistory(); // of this A/B slot: the other keeps its own
    return true;
}

bool Glide90Processor::deleteUserPreset(const juce::File& file)
{
    if (!file.existsAsFile() || !file.isAChildOf(userPresetDirectory()) || !file.hasFileExtension(kPresetExtension))
        return false;
    return file.moveToTrash() || file.deleteFile();
}

void Glide90Processor::captureBaseline()
{
    baselineState = presetState(false, kBaselineTree);
}

bool Glide90Processor::isPresetModified() const
{
    const auto params = baselineState.getChildWithName(apvts.state.getType());
    for (auto* param : getParameters())
        if (auto* p = dynamic_cast<juce::RangedAudioParameter*>(param))
            if (!instanceParameters().contains(p->getParameterID()))
            {
                const auto stored = params.getChildWithProperty("id", p->getParameterID());
                const float loaded = stored.isValid() ? p->convertTo0to1((float) stored.getProperty("value")) : p->getDefaultValue();
                if (std::abs(p->getValue() - loaded) > 1e-4f)
                    return true;
            }
    // A redrawn custom shape is a change too (while it is the one that plays).
    return choice(raw.glideShape) == (int) presetShapes.size()
           && !shapeToTree().isEquivalentTo(baselineState.getChildWithName(kShapeTree));
}

juce::ValueTree Glide90Processor::editState()
{
    // FREEZE is played, not set: pressing it is not a step to undo.
    auto state = presetState(true);
    auto params = state.getChildWithName(apvts.state.getType());
    for (int i = params.getNumChildren(); --i >= 0;)
        if (params.getChild(i).getProperty("id").toString() == ids::holdFreeze)
            params.removeChild(i, nullptr);
    return state;
}

void Glide90Processor::beginEdit()
{
    if (openEdits++ == 0 && quietEdits == 0)
        editBefore = editState();
}

void Glide90Processor::endEdit()
{
    if (openEdits > 0 && --openEdits == 0 && quietEdits == 0 && editBefore.isValid())
        commitEdit(std::exchange(editBefore, {}));
}

void Glide90Processor::parameterGestureChanged(int, bool starting)
{
    // The editor's gestures; anything else (a host thread) is not an edit made here.
    if (quietEdits > 0 || !juce::MessageManager::existsAndIsCurrentThread())
        return;
    if (starting)
        beginEdit();
    else
        endEdit();
}

void Glide90Processor::commitEdit(const juce::ValueTree& before)
{
    auto after = editState();
    if (after.isEquivalentTo(before))
        return;
    auto& undo = undoManager();
    undo.beginNewTransaction();
    undo.perform(new SnapshotAction(*this, before, std::move(after)));
}

void Glide90Processor::selectAB(int slot)
{
    slot = juce::jlimit(0, 1, slot);
    if (slot == abIndex)
        return;
    const Batch quiet(*this, false); // a comparison, not an edit
    abStates[(size_t) abIndex] = slotState();
    abIndex = slot;
    if (abStates[(size_t) slot].isValid())
        applySlotState(abStates[(size_t) slot], false, true);
    else
        abStates[(size_t) slot] = slotState(); // B starts as a copy of A
}

void Glide90Processor::setParametersExactly(const juce::ValueTree& paramsTree, bool missingToDefault, bool gestures)
{
    for (auto* param : getParameters())
        if (auto* p = dynamic_cast<juce::RangedAudioParameter*>(param))
        {
            const auto child = paramsTree.getChildWithProperty("id", p->getParameterID());
            if (!child.isValid() && !missingToDefault)
                continue;
            // Restoring a project is not an edit: without gestures a host in touch or latch mode does
            // not take it as something to record.
            if (gestures)
                p->beginChangeGesture();
            p->setValueNotifyingHost(child.isValid() ? p->convertTo0to1((float) child.getProperty("value"))
                                                     : p->getDefaultValue());
            if (gestures)
                p->endChangeGesture();
        }
}

const Routing& Glide90Processor::currentRouting() const
{
    // LITE: the modules' order. Before the gate GLIDE bends what goes in, after it what comes out.
    if constexpr (kLite)
        return routingAt(choice(raw.glidePosition) == (int) GlidePosition::Pre ? 0 /* INTO THE SWELL */ : 1 /* BENT TAIL */);
    return routingAt(choice(raw.routing));
}

float Glide90Processor::dryLevelGain() const
{
    // LITE: MIX, the unit's BALANCE, is the direct guitar's share; with the gate off (the unit's analog
    // bypass) the direct guitar plays at full.
    if constexpr (kLite)
        return raw.gateOn->load() < 0.5f ? 1.0f : 1.0f - juce::jlimit(0.0f, 1.0f, raw.gateMix->load());
    return gainOf(raw.dryLevel->load());
}

float Glide90Processor::gateLevelGain() const
{
    if constexpr (kLite)
        return juce::jlimit(0.0f, 1.0f, raw.gateMix->load());
    return gainOf(raw.gateLevel->load());
}

float Glide90Processor::value(const char* id) const
{
    if (auto* v = apvts.getRawParameterValue(id))
        return v->load();
    return fixedValue(id);
}

void Glide90Processor::setParameter(const char* id, float plainValue)
{
    auto* p = apvts.getParameter(id);
    if (p == nullptr)
        return; // not in this product
    p->beginChangeGesture();
    p->setValueNotifyingHost(p->convertTo0to1(plainValue));
    p->endChangeGesture();
}

// --- HOLD's held sound, shared ---

void Glide90Processor::shareHeld()
{
    // After a new capture (or when FREEZE takes or lets go of it), and when the editor asks for LIVE's
    // last moments: copied under a lock the message thread may hold, else next block.
    const int version = holdEngine.display().heldVersion.load();
    const bool frozen = holdEngine.heldIsFrozen();
    if (version == heldCopiedVersion && frozen == heldCopyFrozen && !recentWanted.load())
        return;
    const juce::SpinLock::ScopedTryLockType lock(heldLock);
    if (!lock.isLocked())
        return;
    if (version != heldCopiedVersion)
    {
        holdEngine.copyHeld(heldCopy, heldCopyReadFrom);
        heldCopyRate = currentRate;
        heldCopiedVersion = version;
    }
    heldCopyFrozen = frozen;
    if (recentWanted.exchange(false))
    {
        holdEngine.copyRecent(recentCopy, (int) (hold::HoldEngine::kSlotSeconds * currentRate));
        recentReady.store(true);
    }
}

void Glide90Processor::holdAgainAfterReset()
{
    // A reset (the host's bypass, a new sample rate) clears HOLD; what FREEZE holds comes back.
    if (raw.holdFreeze->load() < 0.5f)
        return;
    const juce::SpinLock::ScopedTryLockType lock(heldLock);
    if (lock.isLocked() && heldCopyFrozen && !heldCopy.empty())
        holdEngine.restoreHeld(heldCopy.data(), (int) heldCopy.size(), heldCopyRate, heldCopyReadFrom);
}

bool Glide90Processor::copyHoldSound(std::vector<float>& samples, float& readFrom, double& rate)
{
    bool live;
    {
        const juce::SpinLock::ScopedLockType lock(heldLock);
        live = choice(raw.holdMode) == (int) hold::Mode::Live && !heldCopyFrozen;
        if (!live)
        {
            samples = heldCopy;
            readFrom = heldCopyReadFrom;
            rate = heldCopyRate;
            return samples.size() >= 4;
        }
    }
    // LIVE: the audio thread copies the last moments at its next block; wait for it a little.
    recentReady.store(false);
    recentWanted.store(true);
    for (int i = 0; i < 50 && !recentReady.load(); ++i)
        juce::Thread::sleep(2);
    const juce::SpinLock::ScopedLockType lock(heldLock);
    if (!recentReady.exchange(false))
        return false;
    samples = recentCopy;
    readFrom = 0.0f;
    rate = currentRate;
    return samples.size() >= 4;
}

hold::HoldSettings Glide90Processor::holdSoundSettings() const
{
    hold::HoldSettings h;
    h.sizeMs = raw.holdSize->load();
    h.density = choice(raw.holdDensity);
    h.smearMs = raw.holdSmear->load();
    h.spreadCents = raw.holdSpread->load();
    h.reverse = raw.holdReverse->load();
    h.character = (hold::Character) choice(raw.holdCharacter);
    return h;
}

// --- State ---

void Glide90Processor::getStateInformation(juce::MemoryBlock& destData)
{
    // The project is made of A/B slots only, plus the instance's own settings: the slot that plays,
    // laid out on the parameter tree (the instance's own parameters included), and
    // both slots again under AB. What a slot holds is saved with no further step.
    const auto active = slotState();
    juce::ValueTree st(apvts.state.getType());
    for (const auto& child : presetState(true).getChildWithName(st.getType()))
        st.appendChild(child.createCopy(), nullptr);
    for (int i = 0; i < active.getNumProperties(); ++i)
        st.setProperty(active.getPropertyName(i), active.getProperty(active.getPropertyName(i)), nullptr);
    for (const auto& child : active)
        if (!child.hasType(st.getType()))
            st.appendChild(child.createCopy(), nullptr);

    st.setProperty(kScaleProp, scale, nullptr);
    juce::ValueTree ab(kAbTree);
    ab.setProperty(kAbIndexProp, abIndex, nullptr);
    for (int i = 0; i < (int) abStates.size(); ++i)
    {
        const auto& slot = i == abIndex ? active : abStates[(size_t) i];
        if (slot.isValid())
            ab.appendChild(slot.createCopy().setProperty(kAbSlotProp, i, nullptr), nullptr);
    }
    st.appendChild(ab, nullptr);
    {
        // What FREEZE holds (or a reopened project's frozen sound not yet played).
        const juce::SpinLock::ScopedLockType lock(heldLock);
        const bool pending = heldPending.load();
        const auto& samples = pending ? pendingHeld : heldCopy;
        if (raw.holdFreeze->load() > 0.5f && (pending || heldCopyFrozen) && samples.size() >= 4)
        {
            juce::MemoryBlock block(samples.data(), samples.size() * sizeof(float));
            juce::ValueTree frozen(kHoldFrozenTree);
            frozen.setProperty(kHoldRateProp, pending ? pendingRate : heldCopyRate, nullptr)
                .setProperty(kHoldReadFromProp, pending ? pendingReadFrom : heldCopyReadFrom, nullptr)
                .setProperty(kHoldSamplesProp, block.toBase64Encoding(), nullptr);
            st.appendChild(frozen, nullptr);
        }
    }
    if (auto xml = st.createXml())
        copyXmlToBinary(*xml, destData);
}

void Glide90Processor::setStateInformation(const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary(data, sizeInBytes);
    if (xml == nullptr)
        return;
    auto st = juce::ValueTree::fromXml(*xml);
    if (!st.hasType(apvts.state.getType()))
        return;

    // The instance's own settings.
    setUiScale((float) st.getProperty(kScaleProp, 1.0f));
    abStates = {};
    abIndex = 0;
    if (const auto ab = st.getChildWithName(kAbTree); ab.isValid())
    {
        abIndex = juce::jlimit(0, 1, (int) ab.getProperty(kAbIndexProp, 0));
        for (const auto& slot : ab)
            if (slot.hasType(kPresetFileTree))
                abStates[(size_t) juce::jlimit(0, 1, (int) slot.getProperty(kAbSlotProp, 0))] = slot.createCopy();
    }

    st.removeChild(st.getChildWithName(kAbTree), nullptr);
    st.removeProperty(kScaleProp, nullptr);

    // HOLD's frozen sound: handed to the audio thread before FREEZE is set, so that a block seeing
    // FREEZE on finds it there.
    bool frozenSound = false;
    if (const auto frozen = st.getChildWithName(kHoldFrozenTree); frozen.isValid())
    {
        juce::MemoryBlock block;
        if (block.fromBase64Encoding(frozen.getProperty(kHoldSamplesProp).toString()) && block.getSize() >= 4 * sizeof(float))
        {
            const juce::SpinLock::ScopedLockType lock(heldLock);
            pendingHeld.resize(block.getSize() / sizeof(float));
            std::memcpy(pendingHeld.data(), block.getData(), pendingHeld.size() * sizeof(float));
            pendingRate = (double) frozen.getProperty(kHoldRateProp, 48000.0);
            pendingReadFrom = (float) frozen.getProperty(kHoldReadFromProp, 0.0f);
            frozenSound = true;
        }
        st.removeChild(frozen, nullptr);
    }
    heldPending.store(frozenSound, std::memory_order_release);

    // The rest is the slot that plays: the parameters (the instance's own too), and the slot's
    // other parts beside them.
    juce::ValueTree active(kPresetFileTree), params(st.getType());
    for (int i = 0; i < st.getNumProperties(); ++i)
        active.setProperty(st.getPropertyName(i), st.getProperty(st.getPropertyName(i)), nullptr);
    for (const auto& child : st)
        (child.hasProperty("id") ? params : active).appendChild(child.createCopy(), nullptr);
    active.appendChild(params, nullptr);
    apvts.replaceState(params.createCopy());

    // replaceState skips parameters whose stored value already maps to the current one (a bool left
    // at a raw 0.57 stays there instead of the stored 1), and leaves parameters missing from a
    // state at whatever they were. Set every value exactly, missing ones to their default; not as
    // edits, so a host in touch or latch mode records nothing.
    applySlotState(active, true, false);
    // FREEZE on with nothing saved to hold would hold silence: it opens off.
    if (!frozenSound && raw.holdFreeze->load() > 0.5f)
        apvts.getParameter(ids::holdFreeze)->setValueNotifyingHost(0.0f);
    for (auto& u : undos)
        u.clearUndoHistory();
}

} // namespace glide90

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new glide90::Glide90Processor();
}
