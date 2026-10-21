#include "HoldEngine.h"
#include "TestUtil.h"

#include <doctest/doctest.h>

#include <cmath>
#include <cstdint>
#include <limits>
#include <functional>
#include <ostream> // doctest prints string_view through it (MSVC does not pull it in)

using hold::Character;
using hold::Feed;
using hold::HoldEngine;
using hold::HoldSettings;
using hold::MidiEvent;
using hold::Mode;
using hold::Voicing;
using test::db;

namespace
{

constexpr double kRate = 48000.0;

struct Out
{
    std::vector<float> gate, l, r;
};

// The settings may change over time: `at(sample)` gives them for the block starting there.
using SettingsAt = std::function<HoldSettings(size_t)>;

Out run(const std::vector<float>& in, const SettingsAt& at, const std::vector<float>& bar = {},
        const std::vector<MidiEvent>& midi = {}, const std::vector<int>& blocks = { 256 }, HoldEngine* reuse = nullptr)
{
    HoldEngine own;
    HoldEngine& e = reuse != nullptr ? *reuse : own;
    if (reuse == nullptr)
        e.prepare(kRate);
    Out out { std::vector<float>(in.size()), std::vector<float>(in.size()), std::vector<float>(in.size()) };
    const std::vector<float> flat(in.size(), 0.0f);
    const auto& cents = bar.empty() ? flat : bar;
    size_t start = 0, b = 0;
    while (start < in.size())
    {
        const int n = (int) std::min<size_t>((size_t) blocks[b++ % blocks.size()], in.size() - start);
        std::vector<MidiEvent> events;
        for (const auto& m : midi)
            if (m.sample >= (int) start && m.sample < (int) start + n)
            {
                events.push_back(m);
                events.back().sample -= (int) start;
            }
        HoldSettings s = at(start);
        s.midi = events.data();
        s.numMidi = (int) events.size();
        e.process(in.data() + start, cents.data() + start, n, s, out.gate.data() + start, out.l.data() + start,
                  out.r.data() + start);
        start += (size_t) n;
    }
    return out;
}

Out run(const std::vector<float>& in, const HoldSettings& s, const std::vector<float>& bar = {})
{
    return run(in, [s](size_t) { return s; }, bar);
}

// A plain held tone to measure: no smear, no spread, no ducking, straight to the output.
HoldSettings plain()
{
    HoldSettings s;
    s.feed = Feed::Out;
    s.smearMs = 0.0f;
    s.spreadCents = 0.0f;
    s.duck = 0.0f;
    s.level = 1.0f;
    return s;
}

// One string struck at `at` seconds: a sine at `freq` dying away over `decay` seconds, with a short
// burst of pick noise on top.
void strike(std::vector<float>& v, double at, double freq, double decay, float amp = 0.3f, uint32_t seed = 5)
{
    const size_t start = (size_t) (at * kRate);
    for (size_t i = start; i < v.size(); ++i)
    {
        const double t = (double) (i - start) / kRate;
        double x = amp * std::sin(2.0 * test::kPi * freq * t) * std::exp(-t / decay);
        if (i - start < (size_t) (0.002 * kRate))
        {
            seed = seed * 1664525u + 1013904223u;
            x += 0.6 * ((double) (seed >> 8) / 8388608.0 - 1.0);
        }
        v[i] += (float) x;
    }
}

double cents(double measured, double reference)
{
    return 1200.0 * std::log2(measured / reference);
}

float maxStep(const std::vector<float>& v, size_t from, size_t to)
{
    float m = 0.0f;
    for (size_t i = from + 1; i < to; ++i)
        m = std::max(m, std::abs(v[i] - v[i - 1]));
    return m;
}

float peak(const std::vector<float>& v, size_t from, size_t to)
{
    float m = 0.0f;
    for (size_t i = from; i < to; ++i)
        m = std::max(m, std::abs(v[i]));
    return m;
}

size_t at(double seconds)
{
    return (size_t) (seconds * kRate);
}

// How strong `freq` is in v[from, to), as an amplitude (Goertzel, Hann window).
double toneAt(const std::vector<float>& v, size_t from, size_t to, double freq)
{
    const double w = 2.0 * test::kPi * freq / kRate, c = 2.0 * std::cos(w);
    double s1 = 0.0, s2 = 0.0;
    const double n = (double) (to - from);
    for (size_t i = from; i < to; ++i)
    {
        const double hann = 0.5 - 0.5 * std::cos(2.0 * test::kPi * (double) (i - from) / n);
        const double s0 = v[i] * hann + c * s1 - s2;
        s2 = s1;
        s1 = s0;
    }
    return 2.0 * std::sqrt(std::max(0.0, s1 * s1 + s2 * s2 - c * s1 * s2)) / (0.5 * n);
}

MidiEvent note(double seconds, int number, bool on, float velocity = 1.0f)
{
    return { (int) at(seconds), number, on, velocity, MidiEvent::Kind::Note };
}

MidiEvent pedal(double seconds, bool down)
{
    return { (int) at(seconds), 0, down, 1.0f, MidiEvent::Kind::Sustain };
}

// The lowest level over 20 ms windows in [from, to), in dB.
double lowestDb(const std::vector<float>& v, size_t from, size_t to)
{
    double lowest = 1e9;
    for (size_t a = from; a + at(0.02) <= to; a += at(0.02))
        lowest = std::min(lowest, db(test::rms(v, a, a + at(0.02))));
    return lowest;
}

} // namespace

TEST_CASE("STRIKE: a pick captures the chord and holds it after the strings stop")
{
    std::vector<float> in(at(8.0), 0.0f);
    strike(in, 0.2, 220.0, 0.3);
    const auto out = run(in, plain());

    CHECK(peak(out.l, 0, at(0.2)) == 0.0f); // nothing before the pick
    const double early = test::rms(out.l, at(1.5), at(2.5)), late = test::rms(out.l, at(7.0), at(8.0));
    MESSAGE("held level " << db(early) << " dB, five seconds later " << db(late) << " dB");
    CHECK(db(early) > -30.0);
    CHECK(std::abs(db(late) - db(early)) < 1.0); // DECAY at forever: it does not die away
    CHECK(peak(out.gate, 0, out.gate.size()) == 0.0f); // FEED OUT: nothing into the gate
}

TEST_CASE("the held loop is smooth and keeps the pitch")
{
    std::vector<float> in(at(6.0), 0.0f);
    strike(in, 0.2, 220.0, 0.5);
    for (int density : { 2, 4 })
    {
        CAPTURE(density);
        HoldSettings s = plain();
        s.density = density;
        const auto out = run(in, s);
        const float amp = peak(out.l, at(3.0), at(6.0));
        CHECK(std::abs(cents(test::measureFrequency(out.l, at(3.0), at(6.0), kRate), 220.0)) < 2.0);
        // A sine of this level steps at most amp * 2 pi f / rate per sample; a splice that does not
        // line up shows as a much larger step.
        CHECK(maxStep(out.l, at(3.0), at(6.0)) < 1.5f * amp * (float) (2.0 * test::kPi * 220.0 / kRate));
        // And no tremolo at the grains' rate.
        double lo = 1e9, hi = -1e9;
        for (size_t i = at(3.0); i + at(0.05) < at(6.0); i += at(0.05))
        {
            const double level = db(test::rms(out.l, i, i + at(0.05)));
            lo = std::min(lo, level);
            hi = std::max(hi, level);
        }
        MESSAGE("level varies by " << hi - lo << " dB");
        CHECK(hi - lo < 1.5);
    }
}

TEST_CASE("BEND: the glide bar bends the held chord once, by the bar")
{
    std::vector<float> in(at(5.0), 0.0f);
    strike(in, 0.2, 220.0, 0.5);
    for (float bar : { -100.0f, 150.0f })
    {
        CAPTURE(bar);
        const auto out = run(in, plain(), std::vector<float>(in.size(), bar));
        const double measured = cents(test::measureFrequency(out.l, at(2.0), at(5.0), kRate), 220.0);
        CHECK(std::abs(measured - bar) < 3.0);
    }

    // Not bent (the glide off, or where it plays), the bar does not move the layer.
    HoldSettings s = plain();
    s.bend = false;
    const auto out = run(in, s, std::vector<float>(in.size(), -100.0f));
    CHECK(std::abs(cents(test::measureFrequency(out.l, at(2.0), at(5.0), kRate), 220.0)) < 2.0);
}

TEST_CASE("DUCK: the layer stays down while you play and comes up when you stop")
{
    // A note held at a steady level for three seconds, then cut.
    std::vector<float> in(at(5.0), 0.0f);
    strike(in, 0.2, 220.0, 1000.0, 0.2f);
    for (size_t i = at(3.0); i < in.size(); ++i)
        in[i] = 0.0f;
    for (float duck : { 0.0f, 0.7f })
    {
        CAPTURE(duck);
        HoldSettings s = plain();
        s.duck = duck;
        const auto out = run(in, s);
        const double playing = test::rms(out.l, at(2.5), at(3.0)), stopped = test::rms(out.l, at(4.0), at(5.0));
        MESSAGE("playing " << db(playing) << " dB, stopped " << db(stopped) << " dB");
        if (duck > 0.0f)
            CHECK(db(playing) - db(stopped) < db(1.0 - duck) + 1.0);
        else
            CHECK(std::abs(db(playing) - db(stopped)) < 1.0);
    }
}

TEST_CASE("DECAY: a held chord falls 60 dB over DECAY")
{
    std::vector<float> in(at(4.0), 0.0f);
    strike(in, 0.2, 220.0, 0.3);
    for (float decay : { 1.0f, 2.0f, 5.0f })
    {
        CAPTURE(decay);
        HoldSettings s = plain();
        s.decaySeconds = decay;
        const auto out = run(in, s);
        // The fall over one second, once the chord is held: 60 dB per DECAY.
        const double first = test::rms(out.l, at(1.0), at(1.25)), second = test::rms(out.l, at(2.0), at(2.25));
        const double fall = db(first) - db(second);
        MESSAGE("falls " << fall << " dB in a second");
        CHECK(fall == doctest::Approx(60.0 / decay).epsilon(0.05));
    }
}

TEST_CASE("LIVE: grains read the recent past without ever catching up with the present")
{
    // A steady tone, the bar twice as far up as it goes: the grains read 1.4 times as fast as the
    // signal arrives. Reading past the newest sample would hit eight-second-old silence.
    const auto in = test::sine(220.0, kRate, at(4.0), 0.2f);
    for (float size : { 20.0f, 180.0f, 500.0f })
    {
        CAPTURE(size);
        HoldSettings s = plain();
        s.mode = Mode::Live;
        s.sizeMs = size;
        s.smearMs = 300.0f;
        const auto out = run(in, s, std::vector<float>(in.size(), 600.0f));
        const float amp = peak(out.l, at(2.0), at(4.0));
        CHECK(amp > 0.05f);
        CHECK(maxStep(out.l, at(2.0), at(4.0)) < 2.5f * amp * (float) (2.0 * test::kPi * 220.0 * 1.42 / kRate));
    }
}

TEST_CASE("REVERSE: grains play their stretch backwards")
{
    // A tone sweeping up (an octave in half a second) after a pick. Played forwards each grain
    // sweeps up, backwards down: zero crossings come closer together, or further apart.
    std::vector<float> in(at(4.0), 0.0f);
    strike(in, 0.2, 300.0, 0.001, 0.0f);
    double phase = 0.0;
    for (size_t i = at(0.2); i < at(1.5); ++i)
    {
        const double t = (double) (i - at(0.2)) / kRate;
        phase += 2.0 * test::kPi * 300.0 * std::pow(2.0, t / 0.5) / kRate;
        in[i] += (float) (0.3 * std::sin(phase));
    }
    for (float reverse : { 0.0f, 1.0f })
    {
        CAPTURE(reverse);
        HoldSettings s = plain();
        s.reverse = reverse;
        s.density = 1; // one grain at a time
        const auto out = run(in, s);
        int closer = 0, further = 0;
        double previousCrossing = -1.0, previousGap = -1.0;
        for (size_t i = at(2.0) + 1; i < at(4.0); ++i)
            if (out.l[i - 1] < 0.0f && out.l[i] >= 0.0f && std::abs(out.l[i]) + std::abs(out.l[i - 1]) > 1e-3f)
            {
                const double t = (double) (i - 1) + out.l[i - 1] / (double) (out.l[i - 1] - out.l[i]);
                if (previousCrossing >= 0.0)
                {
                    const double gap = t - previousCrossing;
                    if (previousGap > 0.0 && gap < 400.0 && previousGap < 400.0)
                        (gap < previousGap ? closer : further)++;
                    previousGap = gap;
                }
                previousCrossing = t;
            }
        MESSAGE("crossings closer " << closer << ", further " << further);
        if (reverse > 0.0f)
            CHECK(further > 3 * closer);
        else
            CHECK(closer > 3 * further);
    }
}

TEST_CASE("MANUAL: silent until FREEZE, which holds what just played for as long as it is on")
{
    std::vector<float> in = test::sine(330.0, kRate, at(6.0), 0.2f);
    for (size_t i = at(2.0); i < in.size(); ++i)
        in[i] = 0.0f;
    HoldSettings s = plain();
    s.mode = Mode::Manual;
    const auto out = run(in, [s](size_t sample) {
        HoldSettings t = s;
        t.frozen = sample >= at(1.0) && sample < at(4.0);
        return t;
    });
    CHECK(peak(out.l, 0, at(1.0)) == 0.0f);
    CHECK(db(test::rms(out.l, at(3.0), at(4.0))) > -30.0); // frozen after the input stopped
    CHECK(std::abs(cents(test::measureFrequency(out.l, at(3.0), at(4.0), kRate), 330.0)) < 2.0);
    CHECK(peak(out.l, at(4.6), at(6.0)) < 1e-4f); // let go
}

TEST_CASE("MIDI: a note plays the held chord at its pitch, for as long as it is held")
{
    std::vector<float> in(at(6.0), 0.0f);
    strike(in, 0.2, 220.0, 0.5);
    HoldSettings s = plain();
    s.mode = Mode::Midi;
    s.rootNote = 60;
    const std::vector<MidiEvent> midi { { (int) at(2.0), 72, true }, { (int) at(4.0), 72, false } };
    const auto out = run(in, [s](size_t) { return s; }, {}, midi);
    CHECK(peak(out.l, 0, at(2.0)) == 0.0f); // captured, but no note yet
    CHECK(std::abs(cents(test::measureFrequency(out.l, at(2.5), at(3.9), kRate), 440.0)) < 3.0);
    CHECK(peak(out.l, at(4.0) + at(1.2), at(6.0)) < 1e-4f); // released

    // MONO: the newest of two held notes sounds; letting it go goes back to the first.
    HoldSettings mono = s;
    mono.voicing = Voicing::Mono;
    const std::vector<MidiEvent> two { { (int) at(2.0), 60, true }, { (int) at(3.0), 67, true },
                                       { (int) at(4.0), 67, false } };
    const auto both = run(in, [mono](size_t) { return mono; }, {}, two);
    CHECK(std::abs(cents(test::measureFrequency(both.l, at(3.3), at(3.9), kRate), 220.0) - 700.0) < 3.0);
    CHECK(std::abs(cents(test::measureFrequency(both.l, at(4.3), at(5.5), kRate), 220.0)) < 3.0);
}

TEST_CASE("12-BIT: what is captured sits on a 12-bit grid")
{
    std::vector<float> in(at(2.0), 0.0f);
    strike(in, 0.2, 220.0, 0.5);
    HoldSettings s = plain();
    s.character = Character::Bit12;
    HoldEngine e;
    e.prepare(kRate);
    run(in, [s](size_t) { return s; }, {}, {}, { 256 }, &e);
    REQUIRE(e.heldLength() > 1000);
    int offGrid = 0;
    for (int i = 0; i < e.heldLength(); ++i)
    {
        const float v = e.heldSamples()[(size_t) i] * 2048.0f;
        offGrid += std::abs(v - std::round(v)) > 1e-3f ? 1 : 0;
    }
    CHECK(offGrid == 0);
}

TEST_CASE("FEED: GATE sends the layer to the reverb in mono, OUT to the output in stereo")
{
    std::vector<float> in(at(3.0), 0.0f);
    strike(in, 0.2, 220.0, 0.5);
    HoldSettings s = plain();
    s.spreadCents = 30.0f;
    s.feed = Feed::Gate;
    const auto gate = run(in, s);
    CHECK(peak(gate.l, 0, gate.l.size()) == 0.0f);
    CHECK(db(test::rms(gate.gate, at(2.0), at(3.0))) > -30.0);
    s.feed = Feed::Out;
    const auto out = run(in, s);
    CHECK(peak(out.gate, 0, out.gate.size()) == 0.0f);
    double diff = 0.0;
    for (size_t i = at(2.0); i < at(3.0); ++i)
        diff += std::abs(out.l[i] - out.r[i]);
    CHECK(diff > 1.0); // SPREAD pans the grains apart
}

TEST_CASE("HOLD repeats itself exactly, whatever the block sizes")
{
    std::vector<float> in(at(4.0), 0.0f);
    strike(in, 0.2, 220.0, 0.5);
    strike(in, 2.1, 330.0, 0.5, 0.3f, 11);
    HoldSettings s; // the defaults: smear, spread, ducking, into the gate
    s.reverse = 0.3f;
    s.density = 5;
    std::vector<float> bar(in.size());
    for (size_t i = 0; i < bar.size(); ++i)
        bar[i] = -40.0f * (float) std::sin((double) i / kRate * 5.0);
    const auto fixed = run(in, [s](size_t) { return s; }, bar);
    const auto again = run(in, [s](size_t) { return s; }, bar);
    const auto irregular = run(in, [s](size_t) { return s; }, bar, {}, { 1, 7, 100, 513, 64, 2048, 3 });
    CHECK(fixed.gate == again.gate);
    CHECK(fixed.gate == irregular.gate);
    CHECK(db(test::rms(fixed.gate, at(3.0), at(4.0))) > -40.0);
}

TEST_CASE("after a reset nothing of the held chord comes back")
{
    std::vector<float> in(at(2.0), 0.0f);
    strike(in, 0.2, 220.0, 0.5);
    const HoldSettings s = plain();
    HoldEngine e;
    e.prepare(kRate);
    run(in, [s](size_t) { return s; }, {}, {}, { 256 }, &e);
    REQUIRE(e.heldLength() > 0);
    e.reset();
    CHECK(e.heldLength() == 0);
    const auto after = run(std::vector<float>(at(2.0), 0.0f), [s](size_t) { return s; }, {}, {}, { 256 }, &e);
    CHECK(peak(after.l, 0, after.l.size()) == 0.0f);
}

TEST_CASE("HOLD off plays nothing, and turning it on fades in")
{
    std::vector<float> in(at(3.0), 0.0f);
    strike(in, 0.2, 220.0, 0.5);
    HoldSettings s = plain();
    s.enabled = false;
    const auto off = run(in, s);
    CHECK(peak(off.l, 0, off.l.size()) == 0.0f);
    CHECK(peak(off.gate, 0, off.gate.size()) == 0.0f);

    // Turned on in the middle of a held chord (captured while off): no jump.
    s.mode = Mode::Live;
    const auto on = run(test::sine(220.0, kRate, at(3.0), 0.2f), [s](size_t sample) {
        HoldSettings t = s;
        t.enabled = sample >= at(1.5);
        return t;
    });
    CHECK(peak(on.l, 0, at(1.5)) == 0.0f);
    const float amp = peak(on.l, at(2.0), at(3.0));
    CHECK(maxStep(on.l, at(1.5), at(1.6)) < 2.0f * amp * (float) (2.0 * test::kPi * 220.0 / kRate));
}

TEST_CASE("HOLD stays finite and bounded with every control at its extreme")
{
    std::vector<float> in(at(4.0), 0.0f);
    for (double t = 0.1; t < 3.5; t += 0.06)
        strike(in, t, 82.4 + 40.0 * t, 0.4, 0.4f, (uint32_t) (t * 1000));
    for (auto mode : { Mode::Strike, Mode::Manual, Mode::Live, Mode::Midi })
        for (float size : { 20.0f, 500.0f })
            for (int density : { 1, 8 })
            {
                HoldSettings s;
                s.mode = mode;
                s.frozen = true;
                s.sizeMs = size;
                s.density = density;
                s.smearMs = 1000.0f;
                s.spreadCents = 50.0f;
                s.reverse = 0.5f;
                s.duck = 1.0f;
                s.level = 1.0f;
                s.decaySeconds = 0.5f;
                s.feed = Feed::Out;
                s.character = Character::Bit12;
                const std::vector<MidiEvent> midi { { (int) at(1.0), 96, true } };
                const auto out = run(in, [s](size_t) { return s; }, std::vector<float>(in.size(), 600.0f), midi,
                                     { 333 });
                for (const auto* ch : { &out.l, &out.r })
                    for (float v : *ch)
                    {
                        REQUIRE(std::isfinite(v));
                        REQUIRE(std::abs(v) < 8.0f);
                    }
            }
}

// --- FREEZE in every mode ---

TEST_CASE("FREEZE pressed with the strum holds the chord, not the silence before it")
{
    std::vector<float> in(at(4.0), 0.0f);
    strike(in, 1.0, 220.0, 0.6);
    HoldSettings s = plain();
    s.mode = Mode::Manual;
    const auto out = run(in, [s](size_t sample) {
        HoldSettings t = s;
        t.frozen = sample >= at(1.02);
        return t;
    });
    CHECK(db(test::rms(out.l, at(2.0), at(4.0))) > -30.0);
    CHECK(std::abs(cents(test::measureFrequency(out.l, at(2.5), at(3.5), kRate), 220.0)) < 3.0);
}

TEST_CASE("FREEZE in STRIKE: what plays stays, new picks no longer replace it")
{
    std::vector<float> in(at(4.0), 0.0f);
    strike(in, 0.2, 220.0, 0.5);
    strike(in, 1.5, 330.0, 0.5, 0.3f, 9);
    HoldSettings s = plain();
    const auto free = run(in, s);
    CHECK(std::abs(cents(test::measureFrequency(free.l, at(2.5), at(3.5), kRate), 330.0)) < 3.0);
    const auto frozen = run(in, [s](size_t sample) {
        HoldSettings t = s;
        t.frozen = sample >= at(1.0);
        return t;
    });
    CHECK(std::abs(cents(test::measureFrequency(frozen.l, at(2.5), at(3.5), kRate), 220.0)) < 3.0);
}

TEST_CASE("FREEZE in LIVE holds the last moments after the guitar stops")
{
    std::vector<float> in = test::sine(330.0, kRate, at(4.0), 0.2f);
    for (size_t i = at(2.0); i < in.size(); ++i)
        in[i] = 0.0f;
    HoldSettings s = plain();
    s.mode = Mode::Live;
    const auto passing = run(in, s);
    CHECK(peak(passing.l, at(3.0), at(4.0)) < 1e-3f); // LIVE alone has nothing left to read
    const auto frozen = run(in, [s](size_t sample) {
        HoldSettings t = s;
        t.frozen = sample >= at(1.5);
        return t;
    });
    CHECK(db(test::rms(frozen.l, at(3.0), at(4.0))) > -30.0);
    CHECK(std::abs(cents(test::measureFrequency(frozen.l, at(3.0), at(4.0), kRate), 330.0)) < 3.0);
}

TEST_CASE("FREEZE in MIDI keeps the sample however the guitar plays on")
{
    std::vector<float> in(at(4.0), 0.0f);
    strike(in, 0.2, 220.0, 0.5);
    strike(in, 1.5, 330.0, 0.5, 0.3f, 9);
    HoldSettings s = plain();
    s.mode = Mode::Midi;
    s.rootNote = 60;
    const std::vector<MidiEvent> key { note(2.5, 60, true), note(3.9, 60, false) };
    const auto out = run(in, [s](size_t sample) {
        HoldSettings t = s;
        t.frozen = sample >= at(1.0);
        return t;
    }, {}, key);
    CHECK(std::abs(cents(test::measureFrequency(out.l, at(2.8), at(3.8), kRate), 220.0)) < 3.0);
}

TEST_CASE("outside MIDI the sustain pedal freezes too")
{
    std::vector<float> in = test::sine(330.0, kRate, at(4.0), 0.2f);
    for (size_t i = at(2.0); i < in.size(); ++i)
        in[i] = 0.0f;
    HoldSettings s = plain();
    s.mode = Mode::Manual;
    const auto out = run(in, [s](size_t) { return s; }, {}, { pedal(1.0, true), pedal(3.0, false) });
    CHECK(peak(out.l, 0, at(1.0)) == 0.0f);
    CHECK(db(test::rms(out.l, at(2.2), at(2.9))) > -30.0);
    CHECK(peak(out.l, at(3.6), at(4.0)) < 1e-4f);
}

TEST_CASE("STRIKE keeps up with fast strumming: every chord is captured")
{
    for (double gap : { 0.25, 0.4 })
    {
        CAPTURE(gap);
        std::vector<float> in(at(4.0), 0.0f);
        int strokes = 0;
        for (double t = 0.2; t < 3.0; t += gap, ++strokes)
            strike(in, t, strokes % 2 == 0 ? 196.0 : 247.0, 0.4, 0.3f, (uint32_t) (t * 1000));
        HoldEngine e;
        e.prepare(kRate);
        run(in, [](size_t) { return plain(); }, {}, {}, { 256 }, &e);
        MESSAGE("strokes " << strokes << ", captures " << e.capturesFinished());
        CHECK(e.capturesFinished() >= strokes - 1);
    }
}

TEST_CASE("a new capture comes in over the old one without a dip")
{
    // STRIKE: the same chord struck again.
    std::vector<float> in(at(4.0), 0.0f);
    strike(in, 0.2, 220.0, 3.0);
    strike(in, 2.0, 220.0, 3.0, 0.3f, 9);
    const auto out = run(in, plain());
    const double before = db(test::rms(out.l, at(1.5), at(1.9)));
    CHECK(lowestDb(out.l, at(1.9), at(3.0)) > before - 6.0);

    // MANUAL: FREEZE let go and pressed again at once.
    std::vector<float> tone = test::sine(330.0, kRate, at(4.0), 0.2f);
    HoldSettings s = plain();
    s.mode = Mode::Manual;
    const auto again = run(tone, [s](size_t sample) {
        HoldSettings t = s;
        t.frozen = sample >= at(1.0) && (sample < at(2.0) || sample >= at(2.05));
        return t;
    });
    const double held = db(test::rms(again.l, at(1.5), at(1.95)));
    CHECK(lowestDb(again.l, at(1.95), at(2.8)) > held - 9.0);
}

TEST_CASE("DECAY fades what is held, not what is frozen")
{
    std::vector<float> in(at(4.0), 0.0f);
    strike(in, 0.2, 220.0, 0.5);
    HoldSettings s = plain();
    s.decaySeconds = 1.0f;
    const auto frozen = run(in, [s](size_t sample) {
        HoldSettings t = s;
        t.frozen = sample >= at(0.8);
        return t;
    });
    const double early = db(test::rms(frozen.l, at(1.0), at(1.5))), late = db(test::rms(frozen.l, at(3.0), at(3.5)));
    MESSAGE("frozen: " << early << " dB, then " << late << " dB");
    CHECK(late > early - 3.0);
}

TEST_CASE("a held sound restored (a project reopening frozen) plays at once, at its pitch, from any rate")
{
    const double sourceRate = 44100.0;
    std::vector<float> saved((size_t) (0.6 * sourceRate));
    for (size_t i = 0; i < saved.size(); ++i)
        saved[i] = 0.2f * (float) std::sin(2.0 * test::kPi * 330.0 * (double) i / sourceRate);
    HoldEngine e;
    e.prepare(kRate);
    e.restoreHeld(saved.data(), (int) saved.size(), sourceRate, 0.0f);
    CHECK(e.heldIsFrozen());
    HoldSettings s = plain();
    s.mode = Mode::Manual;
    s.frozen = true; // as the project has it: nothing new is captured
    const std::vector<float> silence(at(2.0), 0.0f);
    const auto out = run(silence, [s](size_t) { return s; }, {}, {}, { 256 }, &e);
    CHECK(e.heldLength() == (int) std::lround(saved.size() * kRate / sourceRate));
    CHECK(db(test::rms(out.l, at(0.2), at(2.0))) > -30.0);
    CHECK(std::abs(cents(test::measureFrequency(out.l, at(0.5), at(1.8), kRate), 330.0)) < 3.0);

    // What is held comes back out as it went in.
    std::vector<float> copy;
    float readFrom = -1.0f;
    e.copyHeld(copy, readFrom);
    CHECK((int) copy.size() == e.heldLength());
    CHECK(readFrom >= 0.0f);
}

TEST_CASE("a held sound from a broken project (no sensible rate) is not held")
{
    std::vector<float> saved(4800, 0.1f);
    for (double rate : { std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::infinity(), -48000.0, 0.0, 1.0e9 })
    {
        CAPTURE(rate);
        HoldEngine e;
        e.prepare(kRate);
        e.restoreHeld(saved.data(), (int) saved.size(), rate, std::numeric_limits<float>::quiet_NaN());
        CHECK_FALSE(e.heldIsFrozen());
    }
}

// --- SWELL and TAIL ---

TEST_CASE("SWELL brings the layer in, TAIL lets it go")
{
    std::vector<float> in(at(6.0), 0.0f);
    strike(in, 0.2, 220.0, 0.5);
    HoldSettings s = plain();
    s.mode = Mode::Midi;
    const std::vector<MidiEvent> key { note(2.0, 60, true), note(4.0, 60, false) };

    HoldSettings slow = s;
    slow.swellMs = 1000.0f;
    slow.tailMs = 2000.0f;
    const auto out = run(in, [slow](size_t) { return slow; }, {}, key);
    const double full = db(test::rms(out.l, at(3.3), at(3.9)));
    CHECK(db(test::rms(out.l, at(2.0), at(2.2))) < full - 10.0); // still coming in
    CHECK(db(test::rms(out.l, at(4.8), at(5.0))) > full - 20.0); // still going
    const auto quick = run(in, [s](size_t) { return s; }, {}, key);
    CHECK(peak(quick.l, at(4.8), at(5.0)) < 1e-4f);
}

// --- MIDI ---

TEST_CASE("MIDI POLY: a chord from the keys sounds as a chord")
{
    std::vector<float> in(at(4.0), 0.0f);
    strike(in, 0.2, 220.0, 0.5);
    HoldSettings s = plain();
    s.mode = Mode::Midi;
    s.rootNote = 57; // the captured A3 is A3
    const std::vector<MidiEvent> chord { note(2.0, 57, true), note(2.0, 61, true), note(2.0, 64, true),
                                         note(3.8, 57, false), note(3.8, 61, false), note(3.8, 64, false) };
    const auto out = run(in, [s](size_t) { return s; }, {}, chord);
    const auto semis = [](int n) { return 220.0 * std::pow(2.0, n / 12.0); };
    const double a = toneAt(out.l, at(2.5), at(3.7), semis(0)), cs = toneAt(out.l, at(2.5), at(3.7), semis(4)),
                 e = toneAt(out.l, at(2.5), at(3.7), semis(7)), none = toneAt(out.l, at(2.5), at(3.7), semis(2));
    MESSAGE("A " << a << ", C# " << cs << ", E " << e << ", B (not played) " << none);
    CHECK(a > 10.0 * none);
    CHECK(cs > 10.0 * none);
    CHECK(e > 10.0 * none);

    // The same, whatever the block sizes.
    const auto other = run(in, [s](size_t) { return s; }, {}, chord, { 37, 512, 100 });
    CHECK(other.l == out.l);
}

TEST_CASE("MIDI POLY: a ninth key takes the oldest voice")
{
    std::vector<float> in(at(4.0), 0.0f);
    strike(in, 0.2, 220.0, 0.5);
    HoldSettings s = plain();
    s.mode = Mode::Midi;
    s.rootNote = 57;
    std::vector<MidiEvent> keys;
    for (int k = 0; k < 9; ++k)
        keys.push_back(note(1.5 + 0.1 * k, 57 + k, true));
    HoldEngine e;
    e.prepare(kRate);
    const auto out = run(in, [s](size_t) { return s; }, {}, keys, { 256 }, &e);
    CHECK(e.activeVoices() == HoldEngine::kMaxVoices);
    const double ninth = toneAt(out.l, at(3.0), at(4.0), 220.0 * std::pow(2.0, 8 / 12.0));
    const double first = toneAt(out.l, at(3.0), at(4.0), 220.0);
    MESSAGE("ninth " << ninth << ", first (taken) " << first);
    CHECK(ninth > 10.0 * first);
}

TEST_CASE("MIDI MONO plays legato; GLIDE slides to the new key")
{
    std::vector<float> in(at(5.0), 0.0f);
    strike(in, 0.2, 220.0, 0.5);
    HoldSettings s = plain();
    s.mode = Mode::Midi;
    s.rootNote = 57;
    const std::vector<MidiEvent> keys { note(2.0, 57, true), note(3.0, 69, true), note(4.5, 69, false),
                                        note(4.5, 57, false) };
    HoldSettings mono = s;
    mono.voicing = Voicing::Mono;
    const auto jumped = run(in, [mono](size_t) { return mono; }, {}, keys);
    CHECK(std::abs(cents(test::measureFrequency(jumped.l, at(3.05), at(3.3), kRate), 440.0)) < 5.0);
    CHECK(lowestDb(jumped.l, at(2.5), at(4.0)) > db(test::rms(jumped.l, at(2.5), at(2.9))) - 6.0); // not started again

    HoldSettings glide = s;
    glide.voicing = Voicing::Glide;
    glide.glideMs = 600.0f;
    const auto slid = run(in, [glide](size_t) { return glide; }, {}, keys);
    const double midway = cents(test::measureFrequency(slid.l, at(3.25), at(3.35), kRate), 220.0);
    MESSAGE("midway through the glide: " << midway << " cents above the first key");
    CHECK(midway > 300.0);
    CHECK(midway < 900.0);
    CHECK(std::abs(cents(test::measureFrequency(slid.l, at(3.8), at(4.4), kRate), 440.0)) < 5.0);
}

TEST_CASE("MIDI: soft keys play quieter, the sustain pedal holds keys let go")
{
    std::vector<float> in(at(5.0), 0.0f);
    strike(in, 0.2, 220.0, 0.5);
    HoldSettings s = plain();
    s.mode = Mode::Midi;
    const auto hard = run(in, [s](size_t) { return s; }, {}, { note(2.0, 60, true, 1.0f), note(3.5, 60, false) });
    const auto soft = run(in, [s](size_t) { return s; }, {}, { note(2.0, 60, true, 0.1f), note(3.5, 60, false) });
    const double drop = db(test::rms(hard.l, at(2.5), at(3.4))) - db(test::rms(soft.l, at(2.5), at(3.4)));
    MESSAGE("soft is " << drop << " dB down");
    CHECK(drop > 5.0);
    CHECK(drop < 9.0);

    const auto held = run(in, [s](size_t) { return s; }, {},
                          { pedal(1.9, true), note(2.0, 60, true), note(2.5, 60, false), pedal(3.5, false) });
    CHECK(db(test::rms(held.l, at(2.8), at(3.4))) > -30.0);
    CHECK(peak(held.l, at(4.0), at(5.0)) < 1e-4f);
}

TEST_CASE("MIDI: keys up to two octaves above ROOT keep their level; higher ones stay silent")
{
    // A chord still ringing: a key up high reads the capture faster, and there is enough of it.
    std::vector<float> in(at(4.0), 0.0f);
    strike(in, 0.2, 220.0, 8.0);
    HoldSettings s = plain();
    s.mode = Mode::Midi;
    s.sizeMs = 250.0f;
    s.density = 3;
    const auto level = [&](int key) {
        const auto out = run(in, [s](size_t) { return s; }, {}, { note(1.5, key, true), note(3.9, key, false) });
        return db(test::rms(out.l, at(2.0), at(3.8)));
    };
    const double root = level(60), octave = level(72), two = level(84);
    MESSAGE("root " << root << " dB, +12 " << octave << " dB, +24 " << two << " dB");
    CHECK(std::abs(octave - root) < 1.5);
    CHECK(std::abs(two - root) < 1.5);
    CHECK(level(85) < -100.0);
}

// --- AUTO ROOT ---

namespace
{
// A plucked string: the fundamental and five harmonics dying away, a burst of pick noise on top.
void pluck(std::vector<float>& v, double at_, double freq, double rate = kRate, float amp = 0.25f)
{
    const auto start = (size_t) (at_ * rate);
    uint32_t seed = 11;
    for (size_t i = start; i < v.size(); ++i)
    {
        const double t = (double) (i - start) / rate;
        double x = 0.0;
        for (int k = 1; k <= 6; ++k)
            x += std::sin(2.0 * test::kPi * freq * k * t) / k * std::exp(-t * (0.8 + 0.3 * k));
        x *= amp;
        if (i - start < (size_t) (0.002 * rate))
        {
            seed = seed * 1664525u + 1013904223u;
            x += 0.6 * ((double) (seed >> 8) / 8388608.0 - 1.0);
        }
        v[i] += (float) x;
    }
}

double semitones(int n)
{
    return std::pow(2.0, n / 12.0);
}
} // namespace

TEST_CASE("detectNote: a single note anywhere on the neck, at any rate; not a chord, not silence")
{
    for (double rate : { 44100.0, 48000.0, 96000.0 })
        for (int note : { 40, 45, 52, 57, 64, 69, 76, 81 }) // E2 up to A5
        {
            CAPTURE(rate);
            CAPTURE(note);
            std::vector<float> v((size_t) (0.4 * rate), 0.0f);
            pluck(v, 0.0, 440.0 * semitones(note - 69), rate);
            const auto from = (size_t) (0.05 * rate);
            CHECK(hold::detectNote(v.data() + from, (int) (v.size() - from), rate) == note);
        }
    std::vector<float> chord(at(0.4), 0.0f);
    for (int n : { 57, 61, 64 }) // A major, equal tempered
        pluck(chord, 0.0, 440.0 * semitones(n - 69));
    CHECK(hold::detectNote(chord.data() + at(0.05), (int) (chord.size() - at(0.05)), kRate) == -1);
    std::vector<float> third(at(0.4), 0.0f);
    for (int n : { 57, 61 })
        pluck(third, 0.0, 440.0 * semitones(n - 69));
    CHECK(hold::detectNote(third.data() + at(0.05), (int) (third.size() - at(0.05)), kRate) == -1);
    const std::vector<float> quiet(at(0.4), 0.0f);
    CHECK(hold::detectNote(quiet.data(), (int) quiet.size(), kRate) == -1);
}

TEST_CASE("AUTO ROOT: a single note sets ROOT to itself, a chord keeps the last one")
{
    HoldSettings s = plain();
    s.mode = Mode::Midi;
    s.autoRoot = true;
    s.rootNote = 60;
    const auto settings = [s](size_t) { return s; };
    HoldEngine e;
    e.prepare(kRate);
    const std::vector<MidiEvent> keyA { note(1.0, 57, true), note(1.95, 57, false) };

    // A3 picked: the key A3 plays it as it is.
    std::vector<float> a(at(2.0), 0.0f);
    pluck(a, 0.2, 220.0);
    const auto first = run(a, settings, {}, keyA, { 256 }, &e);
    CHECK(e.display().autoRoot.load() == 57);
    // (the key held before the capture is in plays it at its pitch once it is)
    CHECK(std::abs(cents(test::measureFrequency(first.l, at(1.6), at(1.9), kRate), 220.0)) < 5.0);

    // An A major chord: ROOT stays A3.
    std::vector<float> chord(at(2.0), 0.0f);
    for (int n : { 57, 61, 64 })
        pluck(chord, 0.2, 440.0 * semitones(n - 69));
    run(chord, settings, {}, {}, { 256 }, &e);
    CHECK(e.display().autoRoot.load() == 57);

    // G3 picked: ROOT G3, so the key A3 plays it a tone up.
    std::vector<float> g(at(2.0), 0.0f);
    pluck(g, 0.2, 196.0);
    const auto third = run(g, settings, {}, keyA, { 256 }, &e);
    CHECK(e.display().autoRoot.load() == 55);
    CHECK(std::abs(cents(test::measureFrequency(third.l, at(1.6), at(1.9), kRate), 220.0)) < 5.0);
}
