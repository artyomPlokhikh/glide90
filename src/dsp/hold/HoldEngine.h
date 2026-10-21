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
    Manual, // silent until FREEZE: a freeze pedal
    Live,   // grains read the last moments as they pass: nothing is held
    Midi,   // captured like STRIKE, played from MIDI keys at their pitches
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

// MIDI: how the keys share the held sound.
enum class Voicing
{
    Poly,  // a voice per key, kMaxVoices at most
    Mono,  // one voice; a key played legato changes its pitch without starting it again
    Glide, // as MONO, the pitch sliding to the new key over glideMs
};

struct MidiEvent
{
    enum class Kind : uint8_t
    {
        Note,
        Sustain, // CC64: `on` while the pedal is down
    };

    int sample; // within the block
    int note;
    bool on;
    float velocity = 1.0f; // 0 - 1
    Kind kind = Kind::Note;
};

struct HoldSettings
{
    bool enabled = true;
    Mode mode = Mode::Strike;
    // FREEZE: hold the sound playing now and stop listening (outside MIDI the sustain pedal does too).
    bool frozen = false;
    float sensitivity = 0.55f; // STRIKE, MIDI: how easily a pick attack captures
    bool bend = true;          // the glide bar bends the layer (the plugin decides by where it plays)
    float sizeMs = 180.0f;     // grain length
    int density = 2;           // grains at once (per voice)
    float smearMs = 60.0f;     // how far each grain may wander from the read point
    float spreadCents = 6.0f;  // detune (and stereo width) per grain
    float reverse = 0.0f;      // 0 - 1: chance that a grain plays backwards
    float duck = 0.7f;         // 0 - 1: how far the layer drops while you play
    float decaySeconds = std::numeric_limits<float>::infinity(); // how long a held sound takes to fall 60 dB (not a frozen one)
    float level = 0.7f;
    Feed feed = Feed::Gate;
    Character character = Character::Clean;
    float swellMs = 10.0f; // how the layer comes in (and a new capture over the old one)
    float tailMs = 200.0f; // how it goes once let go
    int rootNote = 60;     // MIDI: the note that plays the capture at its own pitch
    // MIDI: ROOT from the capture itself: the note of the last capture that was one clear note (a chord
    // keeps the one before); rootNote until there is one.
    bool autoRoot = false;
    Voicing voicing = Voicing::Poly;
    float glideMs = 100.0f; // MIDI GLIDE: how long the pitch takes to reach a new key
    const MidiEvent* midi = nullptr;
    int numMidi = 0;
};

// What the screen shows, written by the audio thread and read by the editor's. Values may be a frame
// old or (rarely) torn; nothing depends on them but the picture.
struct HoldDisplay
{
    static constexpr int kColumns = 128, kGrains = 16;
    static constexpr double kLiveSeconds = 2.5; // what LIVE shows

    // The held sound as min/max pairs, double-buffered: `heldIndex` is the one to read, `heldVersion`
    // counts new captures.
    std::array<std::array<float, 2 * kColumns>, 2> held {};
    std::atomic<int> heldIndex { 0 }, heldVersion { 0 };
    std::atomic<float> heldSeconds { 0.0f };
    // The last kLiveSeconds as min/max pairs in a ring; the newest column is just before `liveHead`.
    std::array<std::atomic<float>, 2 * kColumns> live {};
    std::atomic<int> liveHead { 0 };
    // Grains of what plays now: where each reads (0 - 1 across the held sound, or across the live
    // window), < 0 when idle, and how loud it is now (its window, 0 - 1).
    std::array<std::atomic<float>, kGrains> grainPos {}, grainAmp {};
    std::atomic<float> level { 0.0f };   // the layer's peak since the editor last took it
    std::atomic<bool> holding { false }; // a sound is held (or LIVE is running)
    std::atomic<bool> frozen { false };  // FREEZE (or the pedal) holds it
    // MIDI keys sounding, a bit per note.
    std::array<std::atomic<uint64_t>, 2> notes {};
    // Every note-on, in any mode: how many, and the last one (for ROOT taken from a key).
    std::atomic<int> noteOns { 0 }, lastNoteOn { -1 };
    // ROOT found in the captures (AUTO), or -1 while none has been one clear note.
    std::atomic<int> autoRoot { -1 };
};

// HOLD: captures the dry guitar, holds it, and plays it back as a few long grains whose pitch follows
// the glide bar (and in MIDI the keys). Mono in, a mono send (FEED GATE) and a stereo layer (FEED OUT)
// out; it only reads the past, so it adds no delay.
class HoldEngine
{
public:
    static constexpr int kMaxVoices = 8, kGrainPool = 64;
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

    // The sound held now, for the project and for dragging out; call it where process() runs.
    // `samples` is resized (no allocation within its capacity); `readFrom` is where grains start
    // reading it (0 - 1 of its length). Empty when nothing is held.
    void copyHeld(std::vector<float>& samples, float& readFrom) const;
    bool heldIsFrozen() const { return current >= 0 && slots[(size_t) current].frozen; }
    // The last `count` samples heard (LIVE's material), oldest first.
    void copyRecent(std::vector<float>& samples, int count) const;
    // Plays `samples` (recorded at `sourceRate`) as the held sound, frozen, at once: a project that
    // reopens with FREEZE on. Taken as already frozen: FREEZE on in the next block captures nothing.
    void restoreHeld(const float* samples, int count, double sourceRate, float readFrom);

    double sampleRate() const { return rate; }

    // For tests.
    int pickAttacks() const { return detector.attacks(); }
    int heldLength() const { return current >= 0 ? slots[(size_t) current].length : 0; }
    const std::vector<float>& heldSamples() const { return slots[(size_t) (current >= 0 ? current : 0)].data; }
    int activeGrains() const;
    int activeVoices() const;
    int capturesFinished() const { return finished; }

private:
    struct Slot
    {
        std::vector<float> data;
        int length = 0;          // samples recorded so far
        int target = 0;          // samples to record
        int64_t from = 0;        // the ring index of its first sample
        int64_t pickAt = -1;     // the pick it was captured after, or -1
        int readFrom = 2;        // where grains start reading
        bool readFromEnd = false; // a stretch up to now: grains read its newest part
        double decay = 1.0;      // how far the held sound has died away
        float gain = 0.0f;       // coming in (the one that plays) or going out (the one before it)
        bool frozen = false;
        bool freezeWhenDone = false;
        int note = -1;     // its pitch when it is one clear note (AUTO ROOT), else -1
        int autoRoot = -1; // AUTO ROOT for it: its note, or the last clear note before it, or -1
    };
    struct Voice
    {
        bool active = false;
        int note = -1;
        double cents = 0.0, target = 0.0, glideStep = 0.0; // the key (100 per semitone; 0 outside MIDI)
        int glideLeft = 0;
        float velocity = 1.0f; // as a gain
        float env = 0.0f;
        bool held = false, sustained = false;
        int spawnIn = 0, lastGrain = -1;
        uint32_t order = 0; // when it started, for stealing the oldest
        double ratio = 1.0, ratioPrevious = 1.0; // this sample's speed, bar and key, reading what plays / what it replaced
    };
    struct Grain
    {
        bool active = false;
        int source = -1;    // slot index, or -1 for the rolling buffer (LIVE)
        int voice = 0;
        double pos = 0.0;   // where it reads: within the slot, or an absolute sample index
        int age = 0, length = 1;
        float detuneRatio = 1.0f;
        float panL = 1.0f, panR = 1.0f;
        bool reversed = false;
        bool releasing = false; // ran out of material (or its slot or voice is taken): fading out
        float release = 1.0f;
    };

    float uniform();
    float capture(float x, Character c);
    void startRecording(int slot, int64_t from, int64_t pickAt, bool readFromEnd, bool freezeWhenDone);
    void finishRecording();
    void freezeNow(Mode mode);
    int freeSlot() const;
    void releaseGrains(int slot, int voice);
    void publishHeld(const Slot& slot);
    void spawn(const HoldSettings& s, int voice, int source, int lengthSamples, int smearSamples);
    float readSlot(const Slot& slot, double pos, bool clean) const;
    float readRing(double pos, bool clean) const;
    void pushLiveColumn(float x);
    void takeMidi(const HoldSettings& s, int upTo, int& index);
    void noteOn(const HoldSettings& s, int note, float velocity);
    void noteOff(const HoldSettings& s, int note);
    void startVoice(Voice& v, int note, double cents, float velocity);
    void publishNotes();
    void findNote(Slot& slot);
    int rootFor(const HoldSettings& s) const { return s.autoRoot && autoNote >= 0 ? autoNote : s.rootNote; }
    // The key (in cents) that plays a slot at its own pitch: ROOT, or with AUTO the slot's own; 0 outside MIDI.
    double rootCents(const HoldSettings& s, int source) const;

    double rate = 48000.0;
    sway::PickDetector detector;

    std::vector<float> ring;
    int mask = 0;
    int64_t now = 0; // samples written to the ring so far
    int64_t lastPickAt = -1;

    std::array<Slot, 4> slots;
    int current = -1;   // the slot that plays
    int previous = -1;  // the slot it replaced, going out
    int recording = -1; // the slot being recorded
    int regionSamples = 0; // how much a capture records (enough for a grain read fast, plus SMEAR)
    int grainLength = 1, smearSamples = 0; // this block's SIZE and SMEAR
    bool wasFrozen = false, sustainDown = false;
    int autoNote = -1; // AUTO ROOT: the last capture's note that was one clear note
    Mode lastMode = Mode::Strike;
    int finished = 0;

    std::array<Voice, kMaxVoices> voices;
    uint32_t voiceOrder = 0;
    std::array<int, 16> notes {}; // MONO, GLIDE: keys held, the newest last
    int numNotes = 0;

    std::array<Grain, kGrainPool> grains;

    std::vector<float> window; // Hann, kWindow + 1 points
    static constexpr int kWindow = 2048;

    bool snapFeed = true; // the first block after a reset takes FEED as it is
    float enabledMix = 0.0f, bendMix = 0.0f, feedMix = 0.0f, levelGain = 0.0f, duckGain = 1.0f;
    float env = 0.0f, loudest = 1e-3f;
    float rampStep = 0.0f, releaseStep = 0.0f, envAttack = 0.0f, envRelease = 0.0f, loudestFall = 0.0f, duckCoeff = 0.0f;
    float levelCoeff = 0.0f;
    float lowpass = 0.0f, lowpassCoeff = 1.0f;

    int liveColumn = 0, liveCount = 0, liveColumnLength = 1;
    float liveMin = 0.0f, liveMax = 0.0f;
    float peak = 0.0f;
    uint32_t seed = 0x9e3779b9u;

    HoldDisplay shown;
};

// The pitch of `count` samples at `rate` when they are one clear note: its MIDI note (24 - 96), or -1 for a
// chord, a double stop, noise or silence. Allocates nothing.
int detectNote(const float* samples, int count, double rate);

// The held sound played on its own, as HOLD plays it frozen (no DUCK, no bar, unity level): `samples`
// recorded at `rate`, grains reading from `readFrom` (0 - 1). Fills `numSamples` of outL/outR.
void renderHeld(const float* samples, int count, double rate, float readFrom, HoldSettings settings, float* outL,
                float* outR, int numSamples);

} // namespace hold
