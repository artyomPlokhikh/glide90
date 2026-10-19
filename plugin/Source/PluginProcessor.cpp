#include "PluginProcessor.h"

#include "PluginEditor.h"
#include "RomPrograms.h"

#include <cmath>

namespace glide90
{
namespace
{

const juce::Identifier kAlgorithmProp { "algorithm" }, kLoadedProp { "loadedMemory" }, kPresetProp { "preset" };

// Host presets: a memory plus a Sway setting. The Sway values are starting points by ear, not
// documented settings.
struct Preset
{
    const char* name;
    int memory;
    bool sway;
    float depth, rate, shape, strum, twinDetune, twinLevel;
};

constexpr Preset kPresets[] {
    { "Init: Reverse Gate + Sway", 17, true, 25.0f, 3.0f, 0.4f, 0.3f, 8.0f, 0.0f },
    { "Loveless RG (roadie)", 31, true, 25.0f, 3.0f, 0.5f, 0.4f, 8.0f, 0.0f },
    { "Loveless RG A", 32, true, 30.0f, 2.5f, 0.5f, 0.4f, 10.0f, 0.5f },
    { "Loveless RG B", 33, true, 20.0f, 3.5f, 0.6f, 0.4f, 6.0f, 0.35f },
    { "Reverse Gate (no Sway)", 17, false, 25.0f, 3.0f, 0.4f, 0.3f, 8.0f, 0.0f },
    { "Gate Reverb", 16, false, 25.0f, 3.0f, 0.4f, 0.3f, 8.0f, 0.0f },
    { "Early Ref 1", 5, false, 25.0f, 3.0f, 0.4f, 0.3f, 8.0f, 0.0f },
    { "Early Ref 2", 6, false, 25.0f, 3.0f, 0.4f, 0.3f, 8.0f, 0.0f },
};

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

} // namespace

Glide90Processor::Glide90Processor()
    : AudioProcessor(BusesProperties()
                         .withInput("Input", juce::AudioChannelSet::stereo(), true)
                         .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "GLIDE90", createParameterLayout())
{
    auto p = [this](const char* id) { return apvts.getRawParameterValue(id); };
    raw = { p(ids::erType),       p(ids::erRoomSize),     p(ids::erLiveness),      p(ids::erDelay),
            p(ids::erLpf),        p(ids::balance),        p(ids::outLevel),        p(ids::input),
            p(ids::inputSwitch),  p(ids::outputSwitch),   p(ids::bypass),          p(ids::swayOn),
            p(ids::swayDepth),    p(ids::swayRate),       p(ids::swayShape),       p(ids::swayStrum),
            p(ids::swaySensitivity), p(ids::swayAsymmetry), p(ids::swayBar),       p(ids::swayBarSource),
            p(ids::swayTwinDetune), p(ids::swayTwinLevel), p(ids::swayPosition),   p(ids::swayMix) };
    apvts.addParameterListener(ids::memory, this);
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
    maxBlock = juce::jmax(1, samplesPerBlock);
    spxEngine.prepare(sampleRate, maxBlock);
    swayProcessor.prepare(sampleRate);
    mono.setSize(2, maxBlock); // channel 1 stands in for a missing right output
    const int latency = spxEngine.latencySamples() + swayProcessor.latencySamples();
    setLatencySamples(latency);
    bypassDelay.setSize(2, latency + 1);
    bypassDelay.clear();
    bypassPos = 0;
}

void Glide90Processor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;

    for (const auto meta : midi)
    {
        const auto m = meta.getMessage();
        if (m.isPitchWheel())
            pitchBend = (float) (m.getPitchWheelValue() - 8192) / 8192.0f;
        else if (m.isController() && m.getControllerNumber() == 1)
            modWheel = (float) m.getControllerValue() / 127.0f;
    }

    spx::SpxSettings s;
    s.program = currentAlgorithm.load();
    s.er = spx::romProgram(s.program).er; // keeps the program's density
    s.er.type = (spx::ErType) juce::roundToInt(raw.erType->load());
    s.er.roomSize = (float) juce::roundToInt(raw.erRoomSize->load() * 10.0f) / 10.0f;
    s.er.liveness = juce::roundToInt(raw.erLiveness->load());
    s.er.delayMs = (float) juce::roundToInt(raw.erDelay->load() * 10.0f) / 10.0f;
    s.er.lpfHz = spx::kLpfValuesHz[(size_t) juce::jlimit(0, (int) spx::kLpfValuesHz.size() - 1,
                                                         juce::roundToInt(raw.erLpf->load()))];
    s.balance = raw.balance->load();
    s.outLevel = raw.outLevel->load();
    s.inputKnob = raw.input->load();
    s.inputMinus20 = raw.inputSwitch->load() > 0.5f;
    s.outputMinus20 = raw.outputSwitch->load() > 0.5f;
    s.bypass = raw.bypass->load() > 0.5f;

    sway::SwaySettings w;
    w.enabled = raw.swayOn->load() > 0.5f;
    w.depthCents = raw.swayDepth->load();
    w.rateHz = raw.swayRate->load();
    w.shape = raw.swayShape->load();
    w.strumSync = raw.swayStrum->load();
    w.sensitivity = raw.swaySensitivity->load();
    w.asymmetry = raw.swayAsymmetry->load();
    switch ((BarSource) juce::roundToInt(raw.swayBarSource->load()))
    {
        case BarSource::Knob: w.barCents = raw.swayBar->load(); break;
        case BarSource::PitchBend: w.barCents = 200.0f * pitchBend; break;
        case BarSource::ModWheel: w.barCents = -200.0f * modWheel; break;
    }
    w.twinDetuneCents = raw.swayTwinDetune->load();
    w.twinLevel = raw.swayTwinLevel->load();
    w.mix = raw.swayMix->load();

    const int n = buffer.getNumSamples();
    for (int start = 0; start < n; start += maxBlock)
        processChunk(buffer, start, juce::jmin(maxBlock, n - start), s, w);
    for (int ch = 2; ch < buffer.getNumChannels(); ++ch)
        buffer.clear(ch, 0, n);
    lastBarCents.store(swayProcessor.barCents());
}

void Glide90Processor::processChunk(juce::AudioBuffer<float>& buffer, int start, int num, const spx::SpxSettings& s,
                                    const sway::SwaySettings& w)
{
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

    const bool post = juce::roundToInt(raw.swayPosition->load()) == (int) SwayPosition::Post;
    if (!post)
        swayProcessor.process(&m, 1, num, w);

    const float gain = spx::SpxEngine::inputGain(s.inputKnob, s.inputMinus20);
    raiseTo(inputPeak, juce::FloatVectorOperations::findMaximum(m, num) * gain);
    raiseTo(inputPeak, -juce::FloatVectorOperations::findMinimum(m, num) * gain);

    float* l = buffer.getWritePointer(0, start);
    float* r = buffer.getNumChannels() > 1 ? buffer.getWritePointer(1, start) : mono.getWritePointer(1);
    spxEngine.process(m, l, r, num, s);

    if (post)
    {
        float* channels[] = { l, r };
        swayProcessor.process(channels, 2, num, w);
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

int Glide90Processor::getNumPrograms()
{
    return (int) std::size(kPresets);
}

const juce::String Glide90Processor::getProgramName(int index)
{
    return juce::isPositiveAndBelow(index, getNumPrograms()) ? kPresets[index].name : "";
}

void Glide90Processor::setCurrentProgram(int index)
{
    if (!juce::isPositiveAndBelow(index, getNumPrograms()))
        return;
    currentPreset = index;
    const auto& p = kPresets[index];
    recallMemory(p.memory);
    setParameter(ids::swayOn, p.sway ? 1.0f : 0.0f);
    setParameter(ids::swayDepth, p.depth);
    setParameter(ids::swayRate, p.rate);
    setParameter(ids::swayShape, p.shape);
    setParameter(ids::swayStrum, p.strum);
    setParameter(ids::swaySensitivity, 0.5f);
    setParameter(ids::swayAsymmetry, 0.0f);
    setParameter(ids::swayBar, 0.0f);
    setParameter(ids::swayTwinDetune, p.twinDetune);
    setParameter(ids::swayTwinLevel, p.twinLevel);
    setParameter(ids::swayPosition, (float) SwayPosition::Pre);
    setParameter(ids::swayMix, 1.0f);
}

void Glide90Processor::setParameter(const char* id, float plainValue)
{
    auto* p = apvts.getParameter(id);
    p->beginChangeGesture();
    p->setValueNotifyingHost(p->convertTo0to1(plainValue));
    p->endChangeGesture();
}

void Glide90Processor::parameterChanged(const juce::String& id, float)
{
    if (id == ids::memory)
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
    m.er.type = (spx::ErType) juce::roundToInt(raw.erType->load());
    m.er.roomSize = raw.erRoomSize->load();
    m.er.liveness = juce::roundToInt(raw.erLiveness->load());
    m.er.delayMs = raw.erDelay->load();
    m.er.lpfHz = spx::kLpfValuesHz[(size_t) juce::jlimit(0, (int) spx::kLpfValuesHz.size() - 1,
                                                         juce::roundToInt(raw.erLpf->load()))];
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
    loaded.store(number);
    setParameter(ids::memory, (float) number);
    return true;
}

void Glide90Processor::getStateInformation(juce::MemoryBlock& destData)
{
    auto st = apvts.copyState();
    st.removeChild(st.getChildWithName(MemoryBank::treeType()), nullptr);
    st.appendChild(bank.toTree(), nullptr);
    st.setProperty(kAlgorithmProp, currentAlgorithm.load(), nullptr);
    st.setProperty(kLoadedProp, loaded.load(), nullptr);
    st.setProperty(kPresetProp, currentPreset, nullptr);
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
    st.removeChild(st.getChildWithName(MemoryBank::treeType()), nullptr);
    currentAlgorithm.store(juce::jlimit(1, 30, (int) st.getProperty(kAlgorithmProp, 17)));
    currentPreset = juce::jlimit(0, getNumPrograms() - 1, (int) st.getProperty(kPresetProp, 0));
    // Set before the parameters arrive, so restoring the memory number does not recall it over the
    // restored (possibly edited) values.
    loaded.store((int) st.getProperty(kLoadedProp, 17));
    apvts.replaceState(st);
}

} // namespace glide90

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new glide90::Glide90Processor();
}
