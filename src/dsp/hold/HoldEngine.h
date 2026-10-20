#pragma once

#include "PickDetector.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <limits>
#include <vector>

namespace hold
{

enum class Mode
{
    Strike, // each pick attack captures the chord and holds it
    Latch,  // the FREEZE key holds what just played, for as long as it is down
    Live,   // grains read the last moments as they pass: nothing is held
    Midi,   // captured like STRIKE, heard while a MIDI note is held, at that note's pitch
};

enum class Feed
{
    Gate, // into the reverse gate (the effect hears it, the direct signal does not)
    Out,  // straight to the output
};

enum class Character
{
    Clean,
    Bit12, // a late-80s sampler: 12 bits, a dull top, coarse interpolation
};

struct MidiEvent
{
    int sample; // within the block
    int note;
    bool on;
};

struct HoldSettings
{
    bool enabled = true;
    Mode mode = Mode::Strike;
    bool latched = false;      // LATCH: the FREEZE key
    float sensitivity = 0.55f; // STRIKE, MIDI: how easily a pick attack captures
    float follow = 1.0f;       // 0 - 2: how much the glide bar bends the layer
    bool followBar = true;     // the glide is on (the bar moves the layer only then)
    float sizeMs = 180.0f;     // grain length
    int density = 2;           // grains at once
    float smearMs = 60.0f;     // how far each grain may wander from the read point
    float spreadCents = 6.0f;  // detune (and stereo width) per grain
    float reverse = 0.0f;      // 0 - 1: chance that a grain plays backwards
    float duck = 0.7f;         // 0 - 1: how far the layer drops while you play
    float decaySeconds = std::numeric_limits<float>::infinity(); // how long a held chord lasts
    float level = 0.7f;
    Feed feed = Feed::Gate;
    Character character = Character::Clean;
    int rootNote = 60;         // MIDI: the note that plays the capture at its own pitch
    const MidiEvent* midi = nullptr;
    int numMidi = 0;
};

// What the screen shows, written by the audio thread and read by the editor's. Values may be a frame
// old or (rarely) torn; nothing depends on them but the picture.
struct HoldDisplay
{
    static constexpr int kColumns = 128, kGrains = 16;
    static constexpr double kLiveSeconds = 2.5; // what LIVE shows

    // The held chord as min/max pairs, double-buffered: `heldIndex` is the one to read, `heldVersion`
    // counts new captures.
    std::array<std::array<float, 2 * kColumns>, 2> held {};
    std::atomic<int> heldIndex { 0 }, heldVersion { 0 };
    std::atomic<float> heldSeconds { 0.0f };
    // The last kLiveSeconds as min/max pairs in a ring; the newest column is just before `liveHead`.
    std::array<std::atomic<float>, 2 * kColumns> live {};
    std::atomic<int> liveHead { 0 };
    // Each grain voice: where it reads (0 - 1 across the held chord, or across the live window), < 0
    // when idle, and how loud it is now (its window, 0 - 1).
    std::array<std::atomic<float>, kGrains> grainPos {}, grainAmp {};
    std::atomic<float> level { 0.0f };   // the layer's peak since the editor last took it
    std::atomic<bool> holding { false }; // a chord is held (or LIVE is running)
};

// HOLD: captures the dry guitar, holds it, and plays it back as a few long grains whose pitch follows
// the glide bar. Mono in, a mono send (FEED GATE) and a stereo layer (FEED OUT) out; it only reads
// the past, so it adds no delay.
class HoldEngine
{
public:
    static constexpr int kVoices = HoldDisplay::kGrains;
    static constexpr double kRingSeconds = 8.0, kSlotSeconds = 2.5;

    void prepare(double sampleRate);
    void reset();

    // `dry`: the guitar; `barCents`: the glide bar per sample. All three outputs are written (silence
    // while nothing plays); with FEED GATE the layer is in `toGate`, with FEED OUT in outL/outR.
    void process(const float* dry, const float* barCents, int numSamples, const HoldSettings& s, float* toGate,
                 float* outL, float* outR);

    // Something is (or is about to be) heard: process() and its outputs are worth using.
    bool audible() const { return enabledMix > 0.0f; }

    HoldDisplay& display() { return shown; }

    // For tests.
    int pickAttacks() const { return detector.attacks(); }
    int heldLength() const { return current >= 0 ? slots[(size_t) current].length : 0; }
    const std::vector<float>& heldSamples() const { return slots[(size_t) (current >= 0 ? current : 0)].data; }
    int activeGrains() const;

private:
    struct Slot
    {
        std::vector<float> data;
        int length = 0;     // samples recorded so far
        int target = 0;     // samples to record
        float decay = 1.0f; // how far the held chord has died away
    };
    struct Grain
    {
        bool active = false;
        int source = -1;    // slot index, or -1 for the rolling buffer (LIVE)
        double pos = 0.0;   // where it reads: within the slot, or an absolute sample index
        int age = 0, length = 1;
        float detuneRatio = 1.0f;
        float panL = 1.0f, panR = 1.0f;
        bool reversed = false;
        bool releasing = false; // ran out of material (or its slot is being recorded over): fading out
        float release = 1.0f;
    };

    float uniform();
    float capture(float x, Character c);
    void startRecording(int slot);
    void finishRecording();
    int freeSlot() const;
    void publishHeld(const Slot& slot);
    void spawn(const HoldSettings& s, int lengthSamples, int smearSamples);
    float readSlot(const Slot& slot, double pos, bool clean) const;
    float readRing(double pos, bool clean) const;
    void pushLiveColumn(float x);

    double rate = 48000.0;
    sway::PickDetector detector;

    std::vector<float> ring;
    int mask = 0;
    int64_t now = 0; // samples written to the ring so far

    std::array<Slot, 3> slots;
    int current = -1;   // the slot that plays
    int recording = -1; // the slot being recorded
    int startIn = -1;   // samples until recording starts (after the pick's transient)
    int regionSamples = 0; // how much a capture records (enough for a grain read fast, plus SMEAR)
    int64_t copyFrom = -1; // LATCH: where in the ring the frozen stretch starts (copying into `recording`)
    bool wasLatched = false;

    std::array<Grain, kVoices> grains;
    int spawnIn = 0;     // samples until the next grain
    int lastGrain = -1;  // the grain spawned last, to line the next one up with

    std::vector<float> window; // Hann, kWindow + 1 points
    static constexpr int kWindow = 2048;

    bool snapFeed = true; // the first block after a reset takes FEED as it is
    float enabledMix = 0.0f, followMix = 0.0f, feedMix = 0.0f, levelGain = 0.0f, gate = 0.0f, duckGain = 1.0f;
    float env = 0.0f, loudest = 1e-3f;
    float rampStep = 0.0f, releaseStep = 0.0f, envAttack = 0.0f, envRelease = 0.0f, loudestFall = 0.0f, duckCoeff = 0.0f;
    float gateAttack = 0.0f, gateRelease = 0.0f, levelCoeff = 0.0f;
    float lowpass = 0.0f, lowpassCoeff = 1.0f;

    std::array<int, 16> notes {}; // MIDI notes held, the newest last
    int numNotes = 0;
    float noteCents = 0.0f;

    int liveColumn = 0, liveCount = 0, liveColumnLength = 1;
    float liveMin = 0.0f, liveMax = 0.0f;
    float peak = 0.0f;
    uint32_t seed = 0x9e3779b9u;

    HoldDisplay shown;
};

} // namespace hold
