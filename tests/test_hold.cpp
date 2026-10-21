#include "HoldEngine.h"
#include "TestUtil.h"

#include <doctest/doctest.h>

#include <cmath>
#include <cstdint>
#include <functional>
#include <ostream> // doctest prints string_view through it (MSVC does not pull it in)

using hold::Character;
using hold::Feed;
using hold::HoldEngine;
using hold::HoldSettings;
using hold::MidiEvent;
using hold::Mode;
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
                events.push_back({ m.sample - (int) start, m.note, m.on });
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

TEST_CASE("LATCH: FREEZE holds what just played, for as long as it is down")
{
    std::vector<float> in = test::sine(330.0, kRate, at(6.0), 0.2f);
    for (size_t i = at(2.0); i < in.size(); ++i)
        in[i] = 0.0f;
    HoldSettings s = plain();
    s.mode = Mode::Latch;
    const auto out = run(in, [s](size_t sample) {
        HoldSettings t = s;
        t.latched = sample >= at(1.0) && sample < at(4.0);
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

    // The newest of two held notes sounds; letting it go goes back to the first.
    const std::vector<MidiEvent> two { { (int) at(2.0), 60, true }, { (int) at(3.0), 67, true },
                                       { (int) at(4.0), 67, false } };
    const auto both = run(in, [s](size_t) { return s; }, {}, two);
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
    for (auto mode : { Mode::Strike, Mode::Latch, Mode::Live, Mode::Midi })
        for (float size : { 20.0f, 500.0f })
            for (int density : { 1, 8 })
            {
                HoldSettings s;
                s.mode = mode;
                s.latched = true;
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
