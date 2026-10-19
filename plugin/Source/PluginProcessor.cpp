#include "PluginProcessor.h"

#include "PluginEditor.h"
#include "RomPrograms.h"

#include <cmath>

namespace glide90
{
namespace
{

const juce::Identifier kAlgorithmProp { "algorithm" }, kLoadedProp { "loadedMemory" }, kPresetProp { "preset" },
    kPresetNameProp { "presetName" }, kVersionProp { "version" }, kScaleProp { "uiScale" };
// 2: the Glide parameters. Older states had a different bar (Sway) and no glide_* values worth keeping.
constexpr int kStateVersion = 2;
const juce::Identifier kShapeTree { "SHAPE" }, kPointTree { "POINT" }, kXProp { "x" }, kYProp { "y" };
const juce::Identifier kPresetFileTree { "GLIDE90_PRESET" };
constexpr const char* kPresetExtension = ".glide90";

// Parameters a preset leaves alone: the gain staging of the user's own rig.
const juce::StringArray& rigParameters()
{
    static const juce::StringArray ids { ids::input, ids::inputSwitch, ids::outputSwitch };
    return ids;
}

// Factory presets: a factory program plus reverb and Glide settings on top of the defaults. They
// carry their own values rather than point at user memories, which can be overwritten. The Glide
// values are starting points by ear, not documented settings.
struct FactoryPreset
{
    const char* name;
    int program;
    std::vector<std::pair<const char*, float>> values;
};

const std::vector<FactoryPreset>& factoryPresets()
{
    using sway::ShapePreset;
    auto shape = [](ShapePreset p) { return (float) p; };
    static const std::vector<FactoryPreset> presets {
        { "Init Reverse Gate+Glide", 17, {} },
        // Pushing the bar down while strumming, into the roadie's reverse gate.
        { "MBV Loveless roadie", 17,
          { { ids::erRoomSize, 20.0f }, { ids::erLiveness, 4.0f }, { ids::erDelay, 25.0f }, { ids::glideMode, 2.0f }, { ids::glidePeriod, 600.0f }, { ids::glideDepth, 40.0f },
            { ids::glideShape, shape(ShapePreset::Dive) }, { ids::glideRandom, 0.25f }, { ids::glideSmooth, 0.25f },
            { ids::glideSensitivity, 0.55f }, { ids::glideDynamics, 0.6f } } },
        { "MBV To Here Knows When", 17,
          { { ids::erRoomSize, 20.0f }, { ids::erLiveness, 7.0f }, { ids::erDelay, 75.0f }, { ids::glidePeriod, 900.0f }, { ids::glideDepth, 45.0f }, { ids::glideShape, shape(ShapePreset::Shimmy) },
            { ids::glideRandom, 0.5f }, { ids::glideSmooth, 0.35f }, { ids::glideTwinDetune, 10.0f },
            { ids::glideTwinLevel, 0.4f } } },
        { "MBV Loveless LIV10", 17,
          { { ids::erRoomSize, 20.0f }, { ids::erLiveness, 10.0f }, { ids::erDelay, 0.1f }, { ids::glideMode, 2.0f }, { ids::glidePeriod, 450.0f }, { ids::glideDepth, 35.0f },
            { ids::glideShape, shape(ShapePreset::Dive) }, { ids::glideRandom, 0.2f }, { ids::glideSmooth, 0.2f } } },
        // Soft playing with the bar and the guitar's tone knob down.
        { "MBV Slow tone down", 17,
          { { ids::erRoomSize, 20.0f }, { ids::erLiveness, 5.0f }, { ids::erDelay, 25.0f }, { ids::erLpf, 8.0f /* 2.5 kHz */ }, { ids::glidePeriod, 1200.0f }, { ids::glideDepth, 25.0f }, { ids::glideRandom, 0.4f },
            { ids::glideSmooth, 0.5f } } },
        // Two strings tuned almost alike, bent together.
        { "MBV two-string unison", 17,
          { { ids::erRoomSize, 20.0f }, { ids::erLiveness, 4.0f }, { ids::erDelay, 25.0f }, { ids::glidePeriod, 1500.0f }, { ids::glideDepth, 15.0f }, { ids::glideRandom, 0.6f },
            { ids::glideSmooth, 0.5f }, { ids::glideTwinDetune, 15.0f }, { ids::glideTwinLevel, 1.0f } } },
        { "Whammy on pitch bend", 17,
          { { ids::glideMode, 3.0f }, { ids::glideManualSource, (float) ManualSource::PitchBend },
            { ids::glideDepth, 200.0f }, { ids::glideDirection, 1.0f }, { ids::glideSmooth, 0.15f } } },
        { "Glide pedal CC11", 17,
          { { ids::erRoomSize, 20.0f }, { ids::erLiveness, 4.0f }, { ids::erDelay, 25.0f }, { ids::glideMode, 3.0f }, { ids::glideManualSource, (float) ManualSource::Expression },
            { ids::glideDepth, 100.0f }, { ids::glideSmooth, 0.3f } } },
        { "Seasick twin synced", 17,
          { { ids::glideMode, 1.0f }, { ids::glideDivision, 3.0f }, { ids::glideShape, shape(ShapePreset::Sine) },
            { ids::glideDepth, 15.0f }, { ids::glideDirection, 1.0f }, { ids::glideRandom, 0.3f },
            { ids::glideTwinDetune, 12.0f }, { ids::glideTwinLevel, 0.5f } } },
        { "Synced swell 1 bar", 17,
          { { ids::erRoomSize, 8.0f }, { ids::erLiveness, 5.0f }, { ids::erDelay, 10.0f }, { ids::glideMode, 1.0f }, { ids::glideDivision, 2.0f }, { ids::glideShape, shape(ShapePreset::Swell) },
            { ids::glideDepth, 30.0f }, { ids::glideRandom, 0.1f } } },
        { "Reverse Gate no glide", 17, { { ids::glideOn, 0.0f } } },
        { "Short reverse swell", 17,
          { { ids::erRoomSize, 8.0f }, { ids::erLiveness, 5.0f }, { ids::erDelay, 10.0f }, { ids::glideOn, 0.0f } } },
        { "Gated slap", 16,
          { { ids::erRoomSize, 1.0f }, { ids::erLiveness, 3.0f }, { ids::erDelay, 5.0f }, { ids::erLpf, 16.0f /* 6.3 kHz */ }, { ids::glideOn, 0.0f } } },
        { "Gate Reverb", 16, { { ids::glideOn, 0.0f } } },
        { "Early Ref 1", 5, { { ids::glideOn, 0.0f } } },
        { "Early Ref 2", 6, { { ids::glideOn, 0.0f } } },
    };
    return presets;
}

int lpfIndex(float hz)
{
    int best = (int) spx::kLpfValuesHz.size() - 1; // THRU
    if (hz <= 0.0f)
        return best;
    float bestErr = 1e9f;
    for (int i = 0; i + 1 < (int) spx::kLpfValuesHz.size(); ++i)
    {
        const float err = std::abs(std::log(spx::kLpfValuesHz[(size_t) i] / hz));
        if (err < bestErr)
        {
            bestErr = err;
            best = i;
        }
    }
    return best;
}

void raiseTo(std::atomic<float>& a, float v)
{
    float cur = a.load();
    while (v > cur && !a.compare_exchange_weak(cur, v))
    {
    }
}

int choice(const std::atomic<float>* p)
{
    return juce::roundToInt(p->load());
}

} // namespace

Glide90Processor::Glide90Processor()
    : AudioProcessor(BusesProperties()
                         .withInput("Input", juce::AudioChannelSet::stereo(), true)
                         .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, &undo, "GLIDE90", createParameterLayout())
{
    auto p = [this](const char* id) { return apvts.getRawParameterValue(id); };
    raw = { p(ids::erType),           p(ids::erRoomSize),        p(ids::erLiveness),     p(ids::erDelay),
            p(ids::erLpf),            p(ids::balance),           p(ids::outLevel),       p(ids::input),
            p(ids::inputSwitch),      p(ids::outputSwitch),      p(ids::bypass),         p(ids::glideOn),
            p(ids::glideMode),        p(ids::glidePeriod),       p(ids::glideDivision),  p(ids::glideDepth),
            p(ids::glideDirection),   p(ids::glideRandom),       p(ids::glideSmooth),    p(ids::glideShape),
            p(ids::glideTriggerSource), p(ids::glideSensitivity), p(ids::glideManualSource), p(ids::glideBar),
            p(ids::glideTwinDetune),  p(ids::glideTwinLevel),    p(ids::glideMix),       p(ids::glidePosition),
            p(ids::outputMode),       p(ids::glideDynamics) };
    for (size_t i = 0; i < presetShapes.size(); ++i)
        presetShapes[i] = sway::makeShape((sway::ShapePreset) i);
    customShape = audioCustomShape = presetShapes[0];
    apvts.addParameterListener(ids::memory, this);
    captureBaseline();
}

Glide90Processor::~Glide90Processor()
{
    apvts.removeParameterListener(ids::memory, this);
    cancelPendingUpdate();
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
    mono.setSize(2, maxBlock); // channel 1 stands in for a missing right output
    const int latency = spxEngine.latencySamples() + glideProcessor.latencySamples();
    setLatencySamples(latency);
    bypassDelay.setSize(2, latency + 1);
    bypassDelay.clear();
    bypassPos = 0;
}

void Glide90Processor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();

    int noteAt = -1;
    float noteVelocity = 1.0f;
    for (const auto meta : midi)
    {
        const auto m = meta.getMessage();
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
    s.program = currentAlgorithm.load();
    s.er = spx::romProgram(s.program).er; // keeps the program's density
    s.er.type = (spx::ErType) choice(raw.erType);
    s.er.roomSize = (float) juce::roundToInt(raw.erRoomSize->load() * 10.0f) / 10.0f;
    s.er.liveness = choice(raw.erLiveness);
    s.er.delayMs = (float) juce::roundToInt(raw.erDelay->load() * 10.0f) / 10.0f;
    s.er.lpfHz = spx::kLpfValuesHz[(size_t) juce::jlimit(0, (int) spx::kLpfValuesHz.size() - 1, choice(raw.erLpf))];
    s.balance = raw.balance->load();
    s.outLevel = raw.outLevel->load();
    s.inputKnob = raw.input->load();
    s.inputMinus20 = raw.inputSwitch->load() > 0.5f;
    s.outputMinus20 = raw.outputSwitch->load() > 0.5f;
    s.bypass = raw.bypass->load() > 0.5f;

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
    w.sensitivity = raw.glideSensitivity->load();
    w.dynamics = raw.glideDynamics->load();
    w.midiTriggerVelocity = noteVelocity;
    w.twinDetuneCents = raw.glideTwinDetune->load();
    w.twinLevel = raw.glideTwinLevel->load();
    w.mix = raw.glideMix->load();

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

    const double beatsPerSample = bpm / 60.0 / currentRate;
    for (int start = 0; start < n; start += maxBlock)
    {
        const int num = juce::jmin(maxBlock, n - start);
        sway::SwaySettings chunk = w;
        if (hostBeats >= 0.0)
            chunk.hostBeats = hostBeats + start * beatsPerSample;
        processChunk(buffer, start, num, s, chunk, noteAt >= start && noteAt < start + num ? noteAt - start : -1);
    }
    for (int ch = 2; ch < buffer.getNumChannels(); ++ch)
        buffer.clear(ch, 0, n);
    lastBarCents.store(glideProcessor.barCents());
    lastGlidePhase.store(glideProcessor.phase());
    lastPickAttacks.store(glideProcessor.pickAttacks());
}

void Glide90Processor::processChunk(juce::AudioBuffer<float>& buffer, int start, int num, const spx::SpxSettings& s,
                                    sway::SwaySettings w, int midiNoteAt)
{
    w.midiTriggerAt = midiNoteAt;
    const int numIn = juce::jmin(getTotalNumInputChannels(), buffer.getNumChannels());
    float* m = mono.getWritePointer(0);
    if (numIn >= 2)
    {
        const float* a = buffer.getReadPointer(0, start);
        const float* b = buffer.getReadPointer(1, start);
        for (int i = 0; i < num; ++i)
            m[i] = 0.5f * (a[i] + b[i]);
    }
    else if (numIn == 1)
        juce::FloatVectorOperations::copy(m, buffer.getReadPointer(0, start), num);
    else
        juce::FloatVectorOperations::clear(m, num);

    const bool post = (GlidePosition) choice(raw.glidePosition) == GlidePosition::Post;
    if (!post)
        glideProcessor.process(&m, 1, num, w);

    const float gain = spx::SpxEngine::inputGain(s.inputKnob, s.inputMinus20);
    raiseTo(inputPeak, juce::FloatVectorOperations::findMaximum(m, num) * gain);
    raiseTo(inputPeak, -juce::FloatVectorOperations::findMinimum(m, num) * gain);

    float* l = buffer.getWritePointer(0, start);
    float* r = buffer.getNumChannels() > 1 ? buffer.getWritePointer(1, start) : mono.getWritePointer(1);
    spxEngine.process(m, l, r, num, s);

    if (post)
    {
        float* channels[] = { l, r };
        glideProcessor.process(channels, 2, num, w);
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
}

void Glide90Processor::processBlockBypassed(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    // Pass the input through, delayed by the reported latency so the host's compensation still holds.
    const int n = buffer.getNumSamples();
    const int size = bypassDelay.getNumSamples();
    const int numIn = getTotalNumInputChannels();
    int pos = bypassPos;
    for (int ch = 0; ch < juce::jmin(2, buffer.getNumChannels()); ++ch)
    {
        const float* in = buffer.getReadPointer(juce::jmin(ch, juce::jmax(0, numIn - 1)));
        float* out = buffer.getWritePointer(ch);
        float* line = bypassDelay.getWritePointer(ch);
        pos = bypassPos;
        for (int i = 0; i < n; ++i)
        {
            const float x = numIn > 0 ? in[i] : 0.0f;
            line[pos] = x;
            pos = (pos + 1) % size;
            out[i] = line[pos];
        }
    }
    bypassPos = pos;
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
    // Everything but the rig's gain staging back to its default.
    for (auto* param : getParameters())
        if (auto* p = dynamic_cast<juce::RangedAudioParameter*>(param))
            if (!rigParameters().contains(p->getParameterID()) && p->getParameterID() != ids::memory)
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
    currentFactoryPreset = index;
    resetForPreset();
    recallMemory(p.program);
    for (const auto& [id, value] : p.values)
        setParameter(id, value);
    presetName = p.name;
    captureBaseline();
}

juce::File Glide90Processor::userPresetDirectory()
{
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
        .getChildFile("Glide90")
        .getChildFile("Presets");
}

juce::Array<juce::File> Glide90Processor::userPresetFiles() const
{
    auto files = userPresetDirectory().findChildFiles(juce::File::findFiles, false, juce::String("*") + kPresetExtension);
    files.sort();
    return files;
}

juce::ValueTree Glide90Processor::presetState()
{
    juce::ValueTree preset(kPresetFileTree);
    auto params = apvts.copyState();
    for (int i = params.getNumChildren(); --i >= 0;)
        if (rigParameters().contains(params.getChild(i).getProperty("id").toString()))
            params.removeChild(i, nullptr);
    preset.appendChild(params, nullptr);
    preset.appendChild(shapeToTree(), nullptr);
    preset.setProperty(kAlgorithmProp, currentAlgorithm.load(), nullptr);
    preset.setProperty(kLoadedProp, loaded.load(), nullptr);
    return preset;
}

void Glide90Processor::applyPresetState(const juce::ValueTree& preset)
{
    shapeFromTree(preset.getChildWithName(kShapeTree));
    currentAlgorithm.store(juce::jlimit(1, 30, (int) preset.getProperty(kAlgorithmProp, 17)));
    // Set before the memory number arrives, so it does not recall that memory over the preset.
    loaded.store(juce::jlimit(1, MemoryBank::kCount, (int) preset.getProperty(kLoadedProp, 17)));
    setParametersExactly(preset.getChildWithName(apvts.state.getType()), false);
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
    applyPresetState(preset);
    presetName = file.getFileNameWithoutExtension();
    captureBaseline();
    return true;
}

void Glide90Processor::captureBaseline()
{
    baseline.clear();
    for (auto* p : getParameters())
        baseline.push_back(p->getValue());
}

bool Glide90Processor::isPresetModified() const
{
    const auto& params = getParameters();
    if (baseline.size() != (size_t) params.size())
        return false;
    for (int i = 0; i < params.size(); ++i)
        if (auto* p = dynamic_cast<juce::RangedAudioParameter*>(params[i]))
            if (!rigParameters().contains(p->getParameterID()) && std::abs(p->getValue() - baseline[(size_t) i]) > 1e-4f)
                return true;
    return false;
}

void Glide90Processor::selectAB(int slot)
{
    slot = juce::jlimit(0, 1, slot);
    if (slot == abIndex)
        return;
    abStates[(size_t) abIndex] = presetState();
    abIndex = slot;
    if (abStates[(size_t) slot].isValid())
        applyPresetState(abStates[(size_t) slot]);
    else
        abStates[(size_t) slot] = presetState(); // B starts as a copy of A
}

void Glide90Processor::setParametersExactly(const juce::ValueTree& paramsTree, bool missingToDefault)
{
    for (auto* param : getParameters())
        if (auto* p = dynamic_cast<juce::RangedAudioParameter*>(param))
        {
            const auto child = paramsTree.getChildWithProperty("id", p->getParameterID());
            if (!child.isValid() && !missingToDefault)
                continue;
            p->beginChangeGesture();
            p->setValueNotifyingHost(child.isValid() ? p->convertTo0to1((float) child.getProperty("value"))
                                                     : p->getDefaultValue());
            p->endChangeGesture();
        }
}

// --- SPX90 memories ---

void Glide90Processor::setParameter(const char* id, float plainValue)
{
    auto* p = apvts.getParameter(id);
    p->beginChangeGesture();
    p->setValueNotifyingHost(p->convertTo0to1(plainValue));
    p->endChangeGesture();
}

void Glide90Processor::parameterChanged(const juce::String& id, float value)
{
    if (id != ids::memory)
        return;
    if (undo.isPerformingUndoRedo())
    {
        // Undo restores the edit buffer's values along with the number: do not recall over them.
        const int n = juce::jlimit(1, MemoryBank::kCount, juce::roundToInt(value));
        loaded.store(n);
        if (const auto m = bank.get(n); m.used)
            currentAlgorithm.store(m.algorithm);
        return;
    }
    triggerAsyncUpdate(); // may come from the audio thread (automation)
}

void Glide90Processor::handleAsyncUpdate()
{
    const int n = juce::roundToInt(apvts.getRawParameterValue(ids::memory)->load());
    if (n != loaded.load())
        loadMemory(n);
}

void Glide90Processor::recallMemory(int number)
{
    number = juce::jlimit(1, MemoryBank::kCount, number);
    loadMemory(number); // sets `loaded` first, so the listener's async update has nothing left to do
    setParameter(ids::memory, (float) number);
}

void Glide90Processor::loadMemory(int number)
{
    loaded.store(number);
    const Memory m = bank.get(number);
    if (!m.used)
        return;
    currentAlgorithm.store(m.algorithm);
    setParameter(ids::erType, (float) m.er.type);
    setParameter(ids::erRoomSize, m.er.roomSize);
    setParameter(ids::erLiveness, (float) m.er.liveness);
    setParameter(ids::erDelay, m.er.delayMs);
    setParameter(ids::erLpf, (float) lpfIndex(m.er.lpfHz));
    setParameter(ids::balance, m.balance);
    setParameter(ids::outLevel, m.outLevel);
}

Memory Glide90Processor::editBuffer() const
{
    Memory m;
    m.used = true;
    m.algorithm = currentAlgorithm.load();
    m.er = spx::romProgram(m.algorithm).er;
    m.er.type = (spx::ErType) choice(raw.erType);
    m.er.roomSize = raw.erRoomSize->load();
    m.er.liveness = choice(raw.erLiveness);
    m.er.delayMs = raw.erDelay->load();
    m.er.lpfHz = spx::kLpfValuesHz[(size_t) juce::jlimit(0, (int) spx::kLpfValuesHz.size() - 1, choice(raw.erLpf))];
    m.balance = raw.balance->load();
    m.outLevel = raw.outLevel->load();
    return m;
}

bool Glide90Processor::storeMemory(int number, const juce::String& title)
{
    Memory m = editBuffer();
    m.title = title;
    if (!bank.store(number, m))
        return false;
    ++bankRevision;
    loaded.store(number);
    setParameter(ids::memory, (float) number);
    return true;
}

// --- State ---

void Glide90Processor::getStateInformation(juce::MemoryBlock& destData)
{
    auto st = apvts.copyState();
    for (const auto& type : { MemoryBank::treeType(), kShapeTree })
        st.removeChild(st.getChildWithName(type), nullptr);
    st.appendChild(bank.toTree(), nullptr);
    st.appendChild(shapeToTree(), nullptr);
    st.setProperty(kAlgorithmProp, currentAlgorithm.load(), nullptr);
    st.setProperty(kLoadedProp, loaded.load(), nullptr);
    st.setProperty(kPresetProp, currentFactoryPreset, nullptr);
    st.setProperty(kPresetNameProp, presetName, nullptr);
    st.setProperty(kVersionProp, kStateVersion, nullptr);
    st.setProperty(kScaleProp, scale, nullptr);
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

    bank.fromTree(st.getChildWithName(MemoryBank::treeType()));
    ++bankRevision;
    shapeFromTree(st.getChildWithName(kShapeTree));
    for (const auto& type : { MemoryBank::treeType(), kShapeTree })
        st.removeChild(st.getChildWithName(type), nullptr);
    currentAlgorithm.store(juce::jlimit(1, 30, (int) st.getProperty(kAlgorithmProp, 17)));
    currentFactoryPreset = juce::jlimit(0, getNumPrograms() - 1, (int) st.getProperty(kPresetProp, 0));
    presetName = st.getProperty(kPresetNameProp, "Init").toString();
    setUiScale((float) st.getProperty(kScaleProp, 1.0f));
    // Set before the parameters arrive, so restoring the memory number does not recall it over the
    // restored (possibly edited) values.
    loaded.store(juce::jlimit(1, MemoryBank::kCount, (int) st.getProperty(kLoadedProp, 17)));
    if ((int) st.getProperty(kVersionProp, 1) < kStateVersion)
        for (int i = st.getNumChildren(); --i >= 0;)
            if (st.getChild(i).getProperty("id").toString().startsWith("glide_"))
                st.removeChild(i, nullptr);
    const auto saved = st.createCopy(); // replaceState shares `st` and adds the missing parameters to it
    apvts.replaceState(st);

    // replaceState skips parameters whose stored value already maps to the current one (a bool left
    // at a raw 0.57 stays there instead of the stored 1), and leaves parameters missing from older
    // states at whatever they were. Set every value exactly, missing ones to their default.
    setParametersExactly(saved, true);
    captureBaseline();
    undo.clearUndoHistory();
}

} // namespace glide90

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new glide90::Glide90Processor();
}
