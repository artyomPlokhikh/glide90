// GLIDE90 LITE: GLIDE and the reverse gate, ordered by dragging, MIX on the gate. The code it shares
// with GLIDE90 is tested there (test_plugin.cpp); this is what LITE does differently.
#define DOCTEST_CONFIG_IMPLEMENT
#define JUCE_GUI_BASICS_INCLUDE_XHEADERS 1 // Linux: the X11 calls in main()
#include <doctest/doctest.h>

#include "PluginEditor.h"
#include "PluginProcessor.h"
#include "ui/GatePanel.h"
#include "ui/GlidePanel.h"
#include "ui/HoldPanel.h"
#include "ui/RoutingPanel.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <cmath>

using glide90::Glide90Processor;
namespace ids = glide90::ids;
namespace ui = glide90::ui;

static_assert(glide90::kLite, "the LITE tests are built as LITE");

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

// `in` through a fresh processor set up by `setUp`, block by block; `perBlock` runs before each block.
juce::AudioBuffer<float> render(const juce::AudioBuffer<float>& in, const std::function<void(Glide90Processor&)>& setUp,
                                const std::function<void(Glide90Processor&, int sample)>& perBlock = {})
{
    auto p = make();
    setUp(*p);
    p->reset();
    juce::AudioBuffer<float> out(in), block(2, kBlock);
    juce::MidiBuffer midi;
    for (int start = 0; start + kBlock <= in.getNumSamples(); start += kBlock)
    {
        if (perBlock)
            perBlock(*p, start);
        for (int ch = 0; ch < 2; ++ch)
            block.copyFrom(ch, 0, in, ch, start, kBlock);
        p->processBlock(block, midi);
        for (int ch = 0; ch < 2; ++ch)
            out.copyFrom(ch, start, block, ch, 0, kBlock);
    }
    return out;
}

juce::AudioBuffer<float> tone(double seconds, double freq)
{
    juce::AudioBuffer<float> in(2, (int) (seconds * kRate));
    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < in.getNumSamples(); ++i)
            in.setSample(ch, i, 0.3f * (float) std::sin(2.0 * juce::MathConstants<double>::pi * freq * i / kRate));
    return in;
}

// How much of `freq` there is in `b` (left channel) over [from, to): Goertzel power per sample.
double toneEnergy(const juce::AudioBuffer<float>& b, int from, int to, double freq)
{
    const double coeff = 2.0 * std::cos(2.0 * juce::MathConstants<double>::pi * freq / kRate);
    double s1 = 0.0, s2 = 0.0;
    for (int i = from; i < to; ++i)
    {
        const double s0 = b.getSample(0, i) + coeff * s1 - s2;
        s2 = s1;
        s1 = s0;
    }
    return (s1 * s1 + s2 * s2 - coeff * s1 * s2) / (double) (to - from);
}

// The glide as a bar the test moves: MANUAL, 100 cents down at the top, at once.
void manualBar(Glide90Processor& p)
{
    set(p, ids::glideMode, 3.0f);
    set(p, ids::glideDepth, 100.0f);
    set(p, ids::glideDirection, 0.0f);
    set(p, ids::glideSmooth, 0.0f);
    set(p, ids::glideRandom, 0.0f);
}

template <typename T>
T* find(juce::Component& root)
{
    if (auto* t = dynamic_cast<T*>(&root))
        return t;
    for (auto* child : root.getChildren())
        if (auto* t = find<T>(*child))
            return t;
    return nullptr;
}

} // namespace

TEST_CASE("LITE has GLIDE and the reverse gate, MIX and the modules' order; no HOLD, ROUTING, DRY or gate LEVEL")
{
    auto p = make();
    juce::StringArray params;
    for (auto* param : p->getParameters())
        if (auto* r = dynamic_cast<juce::RangedAudioParameter*>(param))
            params.add(r->getParameterID());
    for (const auto& id : params)
    {
        CAPTURE(id);
        CHECK_FALSE(id.startsWith("hold_"));
    }
    for (const char* id : { ids::routing, ids::dryLevel, ids::gateLevel })
        CHECK_FALSE(params.contains(id));
    for (const char* id : { ids::gateMix, ids::glidePosition, ids::pickSensitivity, ids::gateOn, ids::glideOn })
        CHECK(params.contains(id));
    // The defaults: all effect, GLIDE before the gate (as GLIDE90's first routing).
    CHECK(plain(*p, ids::gateMix) == doctest::Approx(1.0f));
    CHECK(plain(*p, ids::glidePosition) == doctest::Approx((float) glide90::GlidePosition::Pre));
    // What LITE lacks stands where it does nothing.
    CHECK(p->value(ids::holdOn) == 0.0f);
}

TEST_CASE("LITE's presets: GLIDE90's without HOLD, the routing as the order and DRY / LEVEL as MIX")
{
    auto p = make();
    const auto names = p->factoryPresetNames();
    CHECK(names[0] == "Glide Guitar");
    for (const char* held : { "Held Chord", "Sampled Feedback", "Frozen Wall", "Smear Trail" })
        CHECK_FALSE(names.contains(held));
    CHECK(p->currentPresetName() == "Glide Guitar");
    CHECK_FALSE(p->isPresetModified());

    p->loadFactoryPreset(names.indexOf("Wall")); // BENT TAIL: GLIDE after the gate
    CHECK(plain(*p, ids::glidePosition) == doctest::Approx((float) glide90::GlidePosition::Post));
    p->loadFactoryPreset(names.indexOf("Slow Drift")); // DRY -5.2 dB, the gate -6.9 dB: MIX 45 %
    CHECK(plain(*p, ids::gateMix) == doctest::Approx(0.45f).epsilon(0.02));
    CHECK(plain(*p, ids::glidePosition) == doctest::Approx((float) glide90::GlidePosition::Pre));
    CHECK(p->userPresetDirectory().getFileName() == "LITE Presets");
}

TEST_CASE("MIX: 0 is the direct guitar alone, 1 the reverse gate alone")
{
    const auto in = tone(2.0, 220.0);
    auto at = [&](float mix, bool gateOn) {
        return render(in, [=](Glide90Processor& p) {
            set(p, ids::glideOn, 0.0f);
            set(p, ids::gateMix, mix);
            set(p, ids::gateOn, gateOn ? 1.0f : 0.0f);
        });
    };
    const int from = (int) (1.0 * kRate), to = (int) (1.9 * kRate);
    const auto direct = at(0.0f, true), effect = at(1.0f, true), bypassed = at(1.0f, false);
    const double tone = toneEnergy(direct, from, to, 220.0);
    REQUIRE(tone > 0.01);
    // The gate off: the direct guitar at full, whatever MIX says (the unit's analog bypass).
    CHECK(toneEnergy(bypassed, from, to, 220.0) == doctest::Approx(tone).epsilon(0.1));
    // Half way: both, each at half.
    const auto half = at(0.5f, true);
    juce::AudioBuffer<float> sum(direct);
    for (int ch = 0; ch < 2; ++ch)
    {
        sum.applyGain(ch, 0, sum.getNumSamples(), 0.5f);
        sum.addFrom(ch, 0, effect, ch, 0, effect.getNumSamples(), 0.5f);
    }
    double err = 0.0, ref = 0.0;
    for (int i = 0; i < (in.getNumSamples() / kBlock) * kBlock; ++i)
    {
        err += std::pow(half.getSample(0, i) - sum.getSample(0, i), 2.0);
        ref += std::pow(sum.getSample(0, i), 2.0);
    }
    CHECK(err / ref < 1e-4); // the unit's 16 bits round the two apart
}

TEST_CASE("the modules' order: GLIDE after the gate bends the whole tail at once, before it the reflections repeat the move")
{
    // A steady tone into the gate alone; at 2 s the bar goes down.
    const auto in = tone(4.0, 220.0);
    const double open = 220.0, bent = 220.0 * std::pow(2.0, -100.0 / 1200.0);
    const int move = (int) (2.0 * kRate);
    auto run = [&](glide90::GlidePosition position) {
        int heard = 0;
        const auto out = render(
            in,
            [&](Glide90Processor& p) {
                manualBar(p);
                set(p, ids::glidePosition, (float) position);
                heard = move + p.getLatencySamples();
            },
            [&](Glide90Processor& p, int sample) {
                if (sample >= move)
                    set(p, ids::glideBar, 1.0f);
            });
        const int from = heard + (int) (0.1 * kRate), to = heard + (int) (0.6 * kRate);
        return toneEnergy(out, from, to, open) / toneEnergy(out, from, to, bent);
    };
    const double after = run(glide90::GlidePosition::Post), before = run(glide90::GlidePosition::Pre);
    MESSAGE("old pitch after the move: GLIDE after the gate " << after << ", before it " << before);
    CHECK(after < 0.02);
    CHECK(before > 0.1);
}

TEST_CASE("the LITE window: dragging a module by its title bar to the other place turns the order round")
{
    auto p = make();
    std::unique_ptr<juce::AudioProcessorEditor> editor(p->createEditor());
    auto* glide = find<ui::GlidePanel>(*editor);
    auto* gate = find<ui::GatePanel>(*editor);
    REQUIRE(glide != nullptr);
    REQUIRE(gate != nullptr);
    CHECK(find<ui::RoutingPanel>(*editor) == nullptr);
    CHECK(find<ui::HoldPanel>(*editor) == nullptr);
    // PICK SENS is GLIDE's, in its CONTROL group.
    auto* pickSense = find<ui::PickSense>(*editor);
    REQUIRE(pickSense != nullptr);
    CHECK(pickSense->getParentComponent() == glide);
    CHECK(glide->isDraggable());
    CHECK(gate->isDraggable());
    CHECK(editor->getHeight() > 0);

    CHECK(glide->getX() < gate->getX());
    // GLIDE dragged past the gate.
    glide->onDragStart();
    glide->onDrag((float) (gate->getRight() - glide->getRight()));
    glide->onDragEnd(false);
    CHECK(plain(*p, ids::glidePosition) == doctest::Approx((float) glide90::GlidePosition::Post));
    juce::MessageManager::getInstance()->runDispatchLoopUntil(100);
    CHECK(gate->getX() < glide->getX());
    // One step to undo.
    p->undoManager().undo();
    CHECK(plain(*p, ids::glidePosition) == doctest::Approx((float) glide90::GlidePosition::Pre));
    juce::MessageManager::getInstance()->runDispatchLoopUntil(100);
    CHECK(glide->getX() < gate->getX());
    // A drag that does not get far enough, or Escape, changes nothing.
    gate->onDragStart();
    gate->onDrag(-30.0f);
    gate->onDragEnd(false);
    gate->onDragStart();
    gate->onDrag((float) (glide->getX() - gate->getX()));
    gate->onDragEnd(true);
    CHECK(plain(*p, ids::glidePosition) == doctest::Approx((float) glide90::GlidePosition::Pre));

    if (const auto path = juce::SystemStats::getEnvironmentVariable("GLIDE90_SNAPSHOT", {}); path.isNotEmpty())
    {
        for (auto position : { glide90::GlidePosition::Pre, glide90::GlidePosition::Post })
        {
            set(*p, ids::glidePosition, (float) position);
            juce::MessageManager::getInstance()->runDispatchLoopUntil(200);
            const auto image = editor->createComponentSnapshot(editor->getLocalBounds(), true, 2.0f);
            juce::File file(juce::File(path).getSiblingFile(juce::File(path).getFileNameWithoutExtension()
                                                            + (position == glide90::GlidePosition::Pre ? "-lite.png" : "-lite-swapped.png")));
            file.deleteFile();
            juce::FileOutputStream out(file);
            REQUIRE(juce::PNGImageFormat().writeImageToStream(image, out));
        }
    }
}

TEST_CASE("a LITE project reopens as it was")
{
    auto a = make();
    set(*a, ids::gateMix, 0.3f);
    set(*a, ids::glidePosition, (float) glide90::GlidePosition::Post);
    juce::MemoryBlock state;
    a->getStateInformation(state);
    auto b = make();
    b->setStateInformation(state.getData(), (int) state.getSize());
    CHECK(plain(*b, ids::gateMix) == doctest::Approx(0.3f));
    CHECK(plain(*b, ids::glidePosition) == doctest::Approx((float) glide90::GlidePosition::Post));
}

int main(int argc, char** argv)
{
   #if JUCE_LINUX
    // On a bare X server (CI's Xvfb, no window manager) the window manager's atoms do not exist (see
    // test_plugin.cpp): create them before JUCE looks them up.
    if (auto* x11 = juce::X11Symbols::getInstance(); x11->loadAllSymbols())
        if (auto* display = x11->xOpenDisplay(nullptr))
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
