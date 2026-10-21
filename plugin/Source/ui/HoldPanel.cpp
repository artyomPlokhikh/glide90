#include "HoldPanel.h"

#include "PanelLookAndFeel.h"
#include "Parameters.h"
#include "PluginProcessor.h"

#include <juce_audio_formats/juce_audio_formats.h>

namespace glide90::ui
{
namespace
{

const juce::StringArray& modeNames()
{
    static const juce::StringArray names { "STRIKE", "MANUAL", "LIVE", "MIDI" };
    return names;
}

// The MIDI keys sounding: the first three by name, then how many more.
juce::String keysText(const std::array<uint64_t, 2>& notes)
{
    juce::StringArray names;
    int more = 0;
    for (int n = 0; n < 128; ++n)
        if ((notes[(size_t) (n >> 6)] >> (n & 63)) & 1u)
        {
            if (names.size() < 3)
                names.add(noteName(n));
            else
                ++more;
        }
    return names.joinIntoString(" ") + (more > 0 ? " +" + juce::String(more) : juce::String());
}

// VOICES on the screen: POLY, MONO, or GL and the glide time in seconds (".2S").
juce::String voicesText(int index)
{
    if (index <= 0)
        return "POLY";
    if (index == 1)
        return "MONO";
    const float ms = kHoldGlideMs[(size_t) juce::jlimit(0, (int) std::size(kHoldGlideMs) - 1, index - 2)];
    auto seconds = juce::String(ms / 1000.0f, 2);
    while (seconds.contains(".") && (seconds.endsWithChar('0') || seconds.endsWithChar('.')))
        seconds = seconds.dropLastCharacters(1);
    if (seconds.startsWith("0."))
        seconds = seconds.substring(1);
    return "GL " + seconds + "S";
}

void arrow(juce::Graphics& g, float x, float y, int side)
{
    juce::Path tri;
    tri.addTriangle(x - (float) side * 3.0f, y - 4.5f, x - (float) side * 3.0f, y + 4.5f, x + (float) side * 3.5f, y);
    g.fillPath(tri);
}

} // namespace

// --- A held sound as a file ---

juce::File holdCaptureFolder()
{
    return juce::File::getSpecialLocation(juce::File::userMusicDirectory).getChildFile("Glide90 Captures");
}

juce::File writeHoldCapture(const std::vector<float>& held, double rate, float readFrom, const hold::HoldSettings& s,
                            bool raw, const juce::File& folder)
{
    if (held.size() < 4 || rate <= 0.0 || !folder.createDirectory())
        return {};
    juce::AudioBuffer<float> audio;
    if (raw)
    {
        audio.setSize(1, (int) held.size());
        audio.copyFrom(0, 0, held.data(), (int) held.size());
    }
    else
    {
        const int n = (int) std::lround(kCaptureSeconds * rate);
        audio.setSize(2, n);
        hold::renderHeld(held.data(), (int) held.size(), rate, readFrom, s, audio.getWritePointer(0),
                         audio.getWritePointer(1), n);
        audio.applyGainRamp(0, juce::jmin(n, (int) (0.02 * rate)), 0.0f, 1.0f);
        const int fadeOut = juce::jmin(n, (int) (0.2 * rate));
        audio.applyGainRamp(n - fadeOut, fadeOut, 1.0f, 0.0f);
        if (const float peak = audio.getMagnitude(0, n); peak > 0.0f)
            audio.applyGain(juce::Decibels::decibelsToGain(-1.0f) / peak);
    }
    const auto name = "Glide90 HOLD " + juce::Time::getCurrentTime().formatted("%Y-%m-%d %H-%M-%S") + (raw ? " raw" : "");
    auto file = folder.getNonexistentChildFile(name, ".wav", false);
    std::unique_ptr<juce::OutputStream> stream = std::make_unique<juce::FileOutputStream>(file);
    if (static_cast<juce::FileOutputStream*>(stream.get())->failedToOpen())
        return {};
    juce::WavAudioFormat wav;
    auto writer = wav.createWriterFor(stream, juce::AudioFormatWriterOptions {}
                                                  .withSampleRate(rate)
                                                  .withNumChannels(audio.getNumChannels())
                                                  .withBitsPerSample(24));
    if (writer == nullptr || !writer->writeFromAudioSampleBuffer(audio, 0, audio.getNumSamples()))
        return {};
    writer.reset(); // flushes and closes the file
    return file;
}

// --- HoldDisplay ---

HoldDisplay::HoldDisplay(Glide90Processor& p) : proc(p), source(p.holdDisplay())
{
    frame(0.0); // what there is now, before the first frame of the display
}

HoldDisplay::~HoldDisplay() = default;

void HoldDisplay::setCompact(bool c)
{
    compact = c;
    hover = Spot::Nowhere;
    setMouseCursor(compact ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
    setTooltip(compact ? "What HOLD holds. Click to open the strip" : "");
    repaint();
}

int HoldDisplay::choice(const char* id) const
{
    return juce::roundToInt(proc.value(id));
}

void HoldDisplay::frame(double seconds)
{
    bool changed = false;
    auto set = [&changed](auto& field, auto value) {
        bool differs;
        if constexpr (std::is_floating_point_v<std::decay_t<decltype(field)>>)
            differs = !juce::exactlyEqual(field, value);
        else
            differs = field != value;
        if (differs)
        {
            field = value;
            changed = true;
        }
    };
    set(on, choice(ids::holdOn) != 0);
    set(mode, choice(ids::holdMode));
    set(feed, (int) (routingAt(choice(ids::routing)).holdIntoGate ? hold::Feed::Gate : hold::Feed::Out));
    set(density, choice(ids::holdDensity));
    set(character, choice(ids::holdCharacter));
    set(root, choice(ids::holdRoot));
    set(autoRoot, source.autoRoot.load());
    set(frozen, source.frozen.load());
    set(noteOns, source.noteOns.load());
    set(midiIn, proc.midiArrived());
    set(voices, choice(ids::holdVoices));
    if (learnFrom >= 0 && noteOns != learnFrom)
    {
        // ROOT from a key: the one just played.
        learnFrom = -1;
        setTo(ids::holdRoot, juce::jlimit(24, 96, source.lastNoteOn.load()));
        changed = true;
    }
    for (size_t i = 0; i < notes.size(); ++i)
        set(notes[i], source.notes[i].load());
    set(gateBypassed, choice(ids::gateOn) == 0);
    set(holding, source.holding.load());

    if ((hold::Mode) mode == hold::Mode::Live)
    {
        // The last moments, oldest on the left.
        const int head = source.liveHead.load();
        for (int col = 0; col < hold::HoldDisplay::kColumns; ++col)
        {
            const int from = (head + col) % hold::HoldDisplay::kColumns;
            for (int k = 0; k < 2; ++k)
                set(wave[(size_t) (2 * col + k)], source.live[(size_t) (2 * from + k)].load());
        }
        shownVersion = -1;
    }
    else if (const int version = source.heldVersion.load(); version != shownVersion)
    {
        shownVersion = version;
        wave = source.held[(size_t) source.heldIndex.load()];
        heldSeconds = source.heldSeconds.load();
        changed = true;
    }

    for (int i = 0; i < hold::HoldDisplay::kGrains; ++i)
    {
        const float pos = source.grainPos[(size_t) i].load(), amp = source.grainAmp[(size_t) i].load();
        if (std::abs(pos - grainPos[(size_t) i]) > 0.0005f || std::abs(amp - grainAmp[(size_t) i]) > 0.01f)
        {
            grainPos[(size_t) i] = pos;
            grainAmp[(size_t) i] = amp;
            changed = true;
        }
    }

    // The layer's level: up at once, down at a meter's pace.
    const float dt = lastFrame > 0.0 ? (float) juce::jlimit(0.0, 0.1, seconds - lastFrame) : 0.0f;
    lastFrame = seconds;
    const float db = juce::Decibels::gainToDecibels(source.level.exchange(0.0f), -100.0f);
    const float shown = juce::jmax(db, levelDb - 30.0f * dt);
    if (std::abs(shown - levelDb) > 0.2f)
        changed = true;
    levelDb = shown;

    if (changed)
        repaint();
}

juce::String HoldDisplay::status() const
{
    const auto m = (hold::Mode) mode;
    if (!on)
        return "HOLD OFF";
    if (m == hold::Mode::Live && !frozen)
        return "LIVE";
    if (m == hold::Mode::Manual && !frozen)
        return "FREEZE TO HOLD";
    if (!holding)
        return m == hold::Mode::Midi ? "PICK, THEN A KEY" : "WAITING FOR A PICK";
    return (frozen ? "FROZEN " : "HOLDING ") + juce::String(heldSeconds, 2) + " S";
}

// --- The screen's controls ---

juce::Rectangle<float> HoldDisplay::spotArea(Spot s) const
{
    const auto r = getLocalBounds().toFloat();
    const float line = 22.0f, w = r.getWidth();
    const juce::Rectangle<float> top { 0.0f, 0.0f, w, line }, bottom { 0.0f, r.getBottom() - line, w, line };
    switch (s)
    {
        case Spot::ModeDown: return top.withWidth(20.0f);
        case Spot::Mode: return top.withX(20.0f).withWidth(88.0f);
        case Spot::ModeUp: return top.withX(108.0f).withWidth(20.0f);
        case Spot::DensityDown: return top.withX(w - 72.0f).withWidth(18.0f);
        case Spot::Density: return top.withX(w - 54.0f).withWidth(36.0f);
        case Spot::DensityUp: return top.withX(w - 18.0f).withWidth(18.0f);
        // MIDI fits SAMPLER, VOICES and ROOT on the bottom line; otherwise SAMPLER has room for its arrows.
        case Spot::Character: return bottom.withWidth((hold::Mode) mode == hold::Mode::Midi ? 56.0f : 96.0f);
        case Spot::VoicesDown: return bottom.withX(56.0f).withWidth(14.0f);
        case Spot::Voices: return bottom.withX(70.0f).withWidth(50.0f);
        case Spot::VoicesUp: return bottom.withX(120.0f).withWidth(14.0f);
        case Spot::RootDown: return bottom.withX(w - 70.0f).withWidth(14.0f);
        case Spot::Root: return bottom.withX(w - 56.0f).withWidth(42.0f);
        case Spot::RootUp: return bottom.withX(w - 14.0f).withWidth(14.0f);
        case Spot::Nowhere: break;
    }
    return {};
}

HoldDisplay::Spot HoldDisplay::spotAt(juce::Point<float> p) const
{
    if (compact)
        return Spot::Nowhere;
    const bool midi = (hold::Mode) mode == hold::Mode::Midi;
    for (auto s : { Spot::ModeDown, Spot::Mode, Spot::ModeUp, Spot::DensityDown, Spot::Density, Spot::DensityUp,
                    Spot::Character, Spot::VoicesDown, Spot::Voices, Spot::VoicesUp, Spot::RootDown, Spot::Root,
                    Spot::RootUp })
    {
        const bool midiSpot = s == Spot::RootDown || s == Spot::Root || s == Spot::RootUp || s == Spot::VoicesDown
                              || s == Spot::Voices || s == Spot::VoicesUp;
        if ((!midiSpot || midi) && spotArea(s).contains(p))
            return s;
    }
    return Spot::Nowhere;
}

juce::Rectangle<float> HoldDisplay::plotArea() const
{
    return getLocalBounds().toFloat().reduced(10.0f, 0.0f).withTrimmedTop(24.0f).withTrimmedBottom(28.0f);
}

bool HoldDisplay::hasSound() const
{
    return on && ((hold::Mode) mode == hold::Mode::Live || holding);
}

void HoldDisplay::setTo(const char* id, int value)
{
    auto* param = proc.state().getParameter(id);
    param->beginChangeGesture();
    param->setValueNotifyingHost(param->convertTo0to1((float) value));
    param->endChangeGesture();
}

void HoldDisplay::step(const char* id, int delta, bool wrap)
{
    auto* param = proc.state().getParameter(id);
    const auto range = param->getNormalisableRange();
    const int lo = juce::roundToInt(range.start), hi = juce::roundToInt(range.end);
    int v = choice(id) + delta;
    if (wrap)
        v = lo + ((v - lo) % (hi - lo + 1) + (hi - lo + 1)) % (hi - lo + 1);
    setTo(id, juce::jlimit(lo, hi, v));
}

void HoldDisplay::mouseMove(const juce::MouseEvent& e)
{
    if (compact)
        return;
    const auto s = spotAt(e.position);
    const bool overSound = s == Spot::Nowhere && hasSound() && plotArea().contains(e.position);
    setMouseCursor(s != Spot::Nowhere ? juce::MouseCursor::PointingHandCursor
                   : overSound        ? juce::MouseCursor::DraggingHandCursor
                                      : juce::MouseCursor::NormalCursor);
    if (s == hover)
        return;
    hover = s;
    switch (s)
    {
        case Spot::ModeDown:
        case Spot::Mode:
        case Spot::ModeUp:
            setTooltip("STRIKE: each pick attack captures the chord and holds it. MANUAL: silent until FREEZE. "
                       "LIVE: grains of the last moments, nothing held. MIDI: captured on each pick, played from "
                       "MIDI keys at their pitches. FREEZE works in all of them");
            break;
        case Spot::DensityDown:
        case Spot::Density:
        case Spot::DensityUp: setTooltip("DENSITY: how many grains play at once"); break;
        case Spot::Character: setTooltip("12-BIT: captured like a late-80s sampler, grainy and dull at the top"); break;
        case Spot::VoicesDown:
        case Spot::Voices:
        case Spot::VoicesUp:
            setTooltip("VOICES: POLY plays a voice per key (8 at most); MONO one key at a time, legato; GL slides "
                       "between keys over the time shown");
            break;
        case Spot::RootDown:
        case Spot::Root:
        case Spot::RootUp:
            setTooltip("ROOT: the MIDI key that plays the held chord at its own pitch. AUTO: a single note picked "
                       "sets it (a chord keeps the last). Click for the list, AUTO, or to take it from the next key played");
            break;
        case Spot::Nowhere:
            setTooltip("What HOLD holds: the chord captured at the last pick (or frozen, or the last moments in "
                       "LIVE), with the grains that play it. Drag out the held sound as you hear it (8 s, stereo). "
                       "Alt-drag: the raw capture.");
            break;
    }
    repaint();
}

void HoldDisplay::mouseExit(const juce::MouseEvent&)
{
    if (hover != Spot::Nowhere)
    {
        hover = Spot::Nowhere;
        repaint();
    }
}

void HoldDisplay::mouseDown(const juce::MouseEvent& e)
{
    if (compact)
    {
        if (onUnfold)
            onUnfold();
        return;
    }
    switch (spotAt(e.position))
    {
        case Spot::ModeDown: step(ids::holdMode, -1, true); break;
        case Spot::ModeUp: step(ids::holdMode, 1, true); break;
        case Spot::Mode:
        {
            juce::PopupMenu menu;
            menu.setLookAndFeel(&getLookAndFeel());
            for (int i = 0; i < modeNames().size(); ++i)
                menu.addItem(modeNames()[i], true, i == mode, [this, i] { setTo(ids::holdMode, i); });
            menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this));
            break;
        }
        case Spot::DensityDown: step(ids::holdDensity, -1, false); break;
        case Spot::DensityUp: step(ids::holdDensity, 1, false); break;
        case Spot::Density:
        {
            juce::PopupMenu menu;
            menu.setLookAndFeel(&getLookAndFeel());
            for (int n = 1; n <= 8; ++n)
                menu.addItem(juce::String(n) + (n == 1 ? " grain" : " grains"), true, n == density,
                             [this, n] { setTo(ids::holdDensity, n); });
            menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this));
            break;
        }
        case Spot::Character: setTo(ids::holdCharacter, 1 - character); break;
        case Spot::VoicesDown: step(ids::holdVoices, -1, false); break;
        case Spot::VoicesUp: step(ids::holdVoices, 1, false); break;
        case Spot::Voices:
        {
            juce::PopupMenu menu;
            menu.setLookAndFeel(&getLookAndFeel());
            const auto* param = dynamic_cast<juce::AudioParameterChoice*>(proc.state().getParameter(ids::holdVoices));
            for (int i = 0; param != nullptr && i < param->choices.size(); ++i)
                menu.addItem(param->choices[i], true, i == voices, [this, i] { setTo(ids::holdVoices, i); });
            menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this));
            break;
        }
        case Spot::RootDown: step(ids::holdRoot, -1, false); break;
        case Spot::RootUp: step(ids::holdRoot, 1, false); break;
        case Spot::Root:
        {
            // From a key, or by octave, then the note.
            juce::PopupMenu menu;
            menu.setLookAndFeel(&getLookAndFeel());
            menu.addItem("Auto: from a single note picked", true, root == kHoldRootAuto, [this] {
                learnFrom = -1;
                setTo(ids::holdRoot, kHoldRootAuto);
            });
            menu.addItem("Learn from a key", true, learnFrom >= 0, [this] {
                learnFrom = learnFrom >= 0 ? -1 : source.noteOns.load();
                repaint();
            });
            menu.addSeparator();
            for (int octave = 24; octave <= 96; octave += 12)
            {
                juce::PopupMenu keys;
                for (int n = octave; n < octave + 12 && n <= 96; ++n)
                    keys.addItem(noteName(n), true, n == root, [this, n] { setTo(ids::holdRoot, n); });
                menu.addSubMenu(noteName(octave) + " ...", keys, true, nullptr, octave <= root && root < octave + 12);
            }
            menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this));
            break;
        }
        case Spot::Nowhere: break;
    }
}

void HoldDisplay::mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    const int delta = wheel.deltaY > 0.0f ? 1 : (wheel.deltaY < 0.0f ? -1 : 0);
    if (delta == 0)
        return;
    switch (spotAt(e.position))
    {
        case Spot::ModeDown:
        case Spot::Mode:
        case Spot::ModeUp: step(ids::holdMode, -delta, false); break;
        case Spot::RootDown:
        case Spot::Root:
        case Spot::RootUp: step(ids::holdRoot, delta, false); break;
        case Spot::DensityDown:
        case Spot::Density:
        case Spot::DensityUp: step(ids::holdDensity, delta, false); break;
        case Spot::VoicesDown:
        case Spot::Voices:
        case Spot::VoicesUp: step(ids::holdVoices, delta, false); break;
        case Spot::Character:
        case Spot::Nowhere: Component::mouseWheelMove(e, wheel); break;
    }
}

void HoldDisplay::mouseDrag(const juce::MouseEvent& e)
{
    if (compact || dragging || !hasSound() || !plotArea().contains(e.mouseDownPosition) || e.getDistanceFromDragStart() < 6)
        return;
    dragging = true;
    std::vector<float> samples;
    float readFrom = 0.0f;
    double rate = 0.0;
    if (!proc.copyHoldSound(samples, readFrom, rate))
        return;
    const auto file = writeHoldCapture(samples, rate, readFrom, proc.holdSoundSettings(), e.mods.isAltDown(),
                                       holdCaptureFolder());
    if (file.existsAsFile())
        juce::DragAndDropContainer::performExternalDragDropOfFiles({ file.getFullPathName() }, false, this);
}

void HoldDisplay::mouseUp(const juce::MouseEvent&)
{
    dragging = false;
}

// --- Painting ---

void HoldDisplay::paintWave(juce::Graphics& g, juce::Rectangle<float> plot, bool grains)
{
    const auto& t = theme();
    const auto ink = t.ink.withAlpha(on ? 1.0f : kOffInkAlpha);
    const float mid = plot.getCentreY();
    // The waveform, scaled to its loudest so a quiet chord still shows.
    float loudest = 1e-4f;
    for (float v : wave)
        loudest = juce::jmax(loudest, std::abs(v));
    const float scale = plot.getHeight() * 0.5f / loudest;
    const int columns = hold::HoldDisplay::kColumns;
    const float step = plot.getWidth() / (float) columns;
    if ((hold::Mode) mode == hold::Mode::Live || holding)
    {
        g.setColour(t.ink.withAlpha(0.2f));
        g.drawHorizontalLine((int) mid, plot.getX(), plot.getRight());
        juce::Path shape;
        for (int col = 0; col < columns; ++col)
        {
            const float x = plot.getX() + ((float) col + 0.5f) * step;
            const float hi = mid - wave[(size_t) (2 * col + 1)] * scale;
            if (col == 0)
                shape.startNewSubPath(x, hi);
            else
                shape.lineTo(x, hi);
        }
        for (int col = columns; --col >= 0;)
            shape.lineTo(plot.getX() + ((float) col + 0.5f) * step, mid - wave[(size_t) (2 * col)] * scale);
        shape.closeSubPath();
        g.setColour(ink.withMultipliedAlpha(0.3f));
        g.fillPath(shape);
        strokeGlowing(g, shape, ink.withMultipliedAlpha(0.6f), 0.8f);
    }
    if (!grains || !on)
        return;
    // The grains: each a mark where it reads, as tall as it is loud now.
    for (int i = 0; i < hold::HoldDisplay::kGrains; ++i)
    {
        const float pos = grainPos[(size_t) i], amp = grainAmp[(size_t) i];
        if (pos < 0.0f)
            continue;
        const float x = plot.getX() + juce::jlimit(0.0f, 1.0f, pos) * plot.getWidth();
        const float h = juce::jmax(2.0f, amp * plot.getHeight());
        g.setColour(ink.withMultipliedAlpha(0.25f + 0.6f * amp));
        g.fillRoundedRectangle(x - 1.5f, mid - h * 0.5f, 3.0f, h, 1.5f);
    }
}

void HoldDisplay::paint(juce::Graphics& g)
{
    const auto& t = theme();
    const auto bounds = getLocalBounds().toFloat();
    drawGlass(g, bounds);
    const auto ink = t.ink.withAlpha(on ? 1.0f : kOffInkAlpha);
    const float level = juce::jlimit(0.0f, 1.0f, (levelDb + 60.0f) / 60.0f);
    const bool toGate = feed == (int) hold::Feed::Gate && !gateBypassed;

    if (compact)
    {
        // One line: what it does and where it goes; the waveform beside it, its level under it.
        auto line = status();
        if (on)
            line << "  " << modeNames()[mode] << " > " << (toGate ? names::reverseGate : names::out);
        drawReadout(g, line, bounds.reduced(10.0f, 0.0f).withWidth(bounds.getWidth() * 0.6f), t.ink.withAlpha(on ? 0.95f : kOffInkAlpha));
        const auto plot = bounds.withTrimmedLeft(bounds.getWidth() * 0.62f).reduced(8.0f, 5.0f).withTrimmedBottom(3.0f);
        paintWave(g, plot, true);
        g.setColour(ink.withMultipliedAlpha(0.8f));
        g.fillRect(plot.getX(), plot.getBottom() + 1.0f, plot.getWidth() * level, 1.5f);
        return;
    }

    const auto dimText = t.ink.withAlpha(on ? 0.7f : 0.55f);
    auto lit = [&](Spot a, Spot b = Spot::Nowhere, Spot c = Spot::Nowhere) {
        // Off, the ink is faint already: not fainter still.
        return ink.withMultipliedAlpha(hover == a || hover == b || hover == c || !on ? 1.0f : 0.9f);
    };

    // Top line: MODE with its arrows; DENSITY (grains at once) with its arrows on the right.
    g.setColour(lit(Spot::ModeDown, Spot::Mode, Spot::ModeUp));
    arrow(g, 10.0f, 11.0f, -1);
    arrow(g, spotArea(Spot::ModeUp).getCentreX(), 11.0f, 1);
    drawReadout(g, modeNames()[mode], spotArea(Spot::Mode), lit(Spot::ModeDown, Spot::Mode, Spot::ModeUp),
                juce::Justification::centred);
    g.setColour(lit(Spot::DensityDown, Spot::Density, Spot::DensityUp));
    arrow(g, spotArea(Spot::DensityDown).getCentreX(), 11.0f, -1);
    arrow(g, spotArea(Spot::DensityUp).getCentreX(), 11.0f, 1);
    drawReadout(g, juce::String(density) + "X", spotArea(Spot::Density), lit(Spot::DensityDown, Spot::Density, Spot::DensityUp),
                juce::Justification::centred);

    // The waveform with its grains and the level under it; what HOLD waits for, while nothing is held.
    const auto plot = bounds.reduced(10.0f, 0.0f).withTrimmedTop(24.0f).withTrimmedBottom(28.0f);
    paintWave(g, plot, true);
    g.setColour(t.ink.withAlpha(0.12f));
    g.fillRect(plot.getX(), plot.getBottom() + 2.0f, plot.getWidth(), 1.5f);
    g.setColour(ink.withMultipliedAlpha(0.8f));
    g.fillRect(plot.getX(), plot.getBottom() + 2.0f, plot.getWidth() * level, 1.5f);
    const bool sounding = on && ((hold::Mode) mode == hold::Mode::Live
                                 || (holding && ((hold::Mode) mode != hold::Mode::Manual || frozen)));
    if (!sounding)
        drawReadout(g, status(), plot, dimText, juce::Justification::centred);
    // On the picture, backed so that it reads over the waveform: FROZEN, and in MIDI the keys sounding
    // (or that no MIDI has arrived yet).
    auto tag = [&](const juce::String& text, bool right, juce::Colour colour) {
        const float w = juce::jmin(plot.getWidth() * 0.6f, 9.0f * (float) text.length() + 8.0f);
        const auto area = juce::Rectangle<float>(right ? plot.getRight() - w : plot.getX(), plot.getY() + 1.0f, w, 17.0f);
        g.setColour(t.glass.withAlpha(0.85f));
        g.fillRoundedRectangle(area, 2.0f);
        drawReadout(g, text, area.reduced(3.0f, 0.0f), colour, right ? juce::Justification::centredRight
                                                                     : juce::Justification::centredLeft);
    };
    if (sounding && frozen)
        tag("FROZEN", false, ink);
    if (on && (hold::Mode) mode == hold::Mode::Midi)
    {
        const auto keys = keysText(notes);
        if (!midiIn)
            tag("NO MIDI IN", true, dimText);
        else if (keys.isNotEmpty())
            tag(keys, true, ink);
        else if (root == kHoldRootAuto && autoRoot >= 0 && holding)
            tag("ROOT " + noteName(autoRoot), true, dimText); // what AUTO found
    }

    // Bottom line: SAMPLER; in MIDI VOICES and ROOT, otherwise how long the held chord is.
    const auto sampler = character == (int) hold::Character::Bit12 ? "12-BIT" : "CLEAN";
    const auto samplerArea = spotArea(Spot::Character);
    if ((hold::Mode) mode == hold::Mode::Midi)
    {
        drawReadout(g, sampler, samplerArea.withTrimmedLeft(8.0f), lit(Spot::Character));
        g.setColour(lit(Spot::VoicesDown, Spot::Voices, Spot::VoicesUp));
        arrow(g, spotArea(Spot::VoicesDown).getCentreX(), spotArea(Spot::VoicesDown).getCentreY(), -1);
        arrow(g, spotArea(Spot::VoicesUp).getCentreX(), spotArea(Spot::VoicesUp).getCentreY(), 1);
        drawReadout(g, voicesText(voices), spotArea(Spot::Voices), lit(Spot::VoicesDown, Spot::Voices, Spot::VoicesUp),
                    juce::Justification::centred);
        g.setColour(lit(Spot::RootDown, Spot::Root, Spot::RootUp));
        arrow(g, spotArea(Spot::RootDown).getCentreX(), spotArea(Spot::RootDown).getCentreY(), -1);
        arrow(g, spotArea(Spot::RootUp).getCentreX(), spotArea(Spot::RootUp).getCentreY(), 1);
        drawReadout(g, learnFrom >= 0 ? juce::String("KEY?") : root == kHoldRootAuto ? juce::String("AUTO") : noteName(root),
                    spotArea(Spot::Root),
                    lit(Spot::RootDown, Spot::Root, Spot::RootUp), juce::Justification::centred);
    }
    else
    {
        g.setColour(lit(Spot::Character));
        arrow(g, samplerArea.getX() + 10.0f, samplerArea.getCentreY(), -1);
        arrow(g, samplerArea.getRight() - 10.0f, samplerArea.getCentreY(), 1);
        drawReadout(g, sampler, samplerArea.reduced(18.0f, 0.0f), lit(Spot::Character), juce::Justification::centred);
        if (sounding && holding)
            drawReadout(g, juce::String(heldSeconds, 2) + " S",
                        juce::Rectangle<float>(bounds.getRight() - 90.0f, samplerArea.getY(), 82.0f, samplerArea.getHeight()),
                        dimText, juce::Justification::centredRight);
    }
}

// --- FreezeKey ---

FreezeKey::FreezeKey(juce::AudioProcessorValueTreeState& state, const char* paramId)
    : juce::TextButton("FREEZE"),
      param(*state.getParameter(paramId)),
      attachment(param, [this](float v) {
          setToggleState(v > 0.5f, juce::dontSendNotification);
          repaint();
      })
{
    getProperties().set(PanelLookAndFeel::kLed, true);
    attachment.sendInitialUpdate();
}

void FreezeKey::mouseDown(const juce::MouseEvent& e)
{
    juce::TextButton::mouseDown(e);
    if (e.mods.isLeftButtonDown())
        press(juce::Time::getMillisecondCounter());
}

void FreezeKey::mouseUp(const juce::MouseEvent& e)
{
    juce::TextButton::mouseUp(e);
    release(juce::Time::getMillisecondCounter());
}

void FreezeKey::press(juce::uint32 nowMs)
{
    pressed = true;
    wasOn = getToggleState();
    downAt = nowMs;
    if (!wasOn)
        attachment.setValueAsCompleteGesture(1.0f); // frozen the moment it goes down
}

void FreezeKey::release(juce::uint32 nowMs)
{
    if (!pressed)
        return;
    pressed = false;
    // A click on a lit key lets it go; a press held long freezes only while held.
    if (wasOn || nowMs - downAt >= kHoldMs)
        attachment.setValueAsCompleteGesture(0.0f);
}

// --- HoldPanel ---

HoldPanel::HoldPanel(Glide90Processor& p)
    : ModulePanel(names::hold), proc(p), on(p.state(), ids::holdOn, "ON"), freeze(p.state(), ids::holdFreeze),
      display(p), size(p.state(), ids::holdSize, "SIZE"),
      smear(p.state(), ids::holdSmear, "SMEAR"), spread(p.state(), ids::holdSpread, "SPREAD"),
      reverse(p.state(), ids::holdReverse, "REVERSE"), swell(p.state(), ids::holdSwell, "SWELL"),
      tail(p.state(), ids::holdTail, "TAIL"), decay(p.state(), ids::holdDecay, "DECAY"),
      duck(p.state(), ids::holdDuck, "DUCK"), level(p.state(), ids::holdLevel, "LEVEL")
{
    for (auto* c : std::initializer_list<juce::Component*> { &on, &freeze, &display })
        addAndMakeVisible(c);
    for (auto* k : knobs())
        addAndMakeVisible(k);

    on.setTooltip("Hold on/off. It plays beside the glide and the reverse gate and adds no delay");
    freeze.setTooltip("Holds the sound now and stops listening. Click to latch, hold to freeze only while held");
    size.setTooltip("Length of each grain. Long is smooth, short is grainy");
    smear.setTooltip("How far each grain may wander from the start of the held chord. 0 is a steady loop");
    spread.setTooltip("Each grain a few cents off and panned apart: many slightly different guitars");
    reverse.setTooltip("How many grains play backwards");
    swell.setTooltip("How HOLD comes in: on FREEZE, on a key, and a new chord over the old one");
    tail.setTooltip("How HOLD goes once let go: FREEZE off, a key up");
    duck.setTooltip("How far the held chord drops while you play. It swells back as the strings die away");
    decay.setTooltip("How long a held chord takes to fall 60 dB. INF holds it until the next pick. Not while frozen");
    level.setTooltip("Level of the held chord");

    display.onUnfold = [this] {
        setFolded(false);
        if (onFold)
            onFold(false);
    };
    setFolded(holdFolded());
}

HoldPanel::~HoldPanel() = default;

void HoldPanel::setFolded(bool f)
{
    folded = f;
    for (auto* k : knobs())
        k->setVisible(!folded);
    display.setCompact(folded);
    resized();
    repaint();
}

juce::Rectangle<int> HoldPanel::freezeBounds() const
{
    return onKeyBounds().translated(-112, 0).withWidth(104);
}

std::vector<Knob*> HoldPanel::knobs()
{
    return { &size, &smear, &spread, &reverse, &swell, &tail, &decay, &duck, &level };
}

int HoldPanel::knobX(int index)
{
    // Right-aligned, a column each.
    return getWidth() - PanelLookAndFeel::kMargin - (int) knobs().size() * kKnobColumn + index * kKnobColumn;
}

juce::Rectangle<int> HoldPanel::titleArea() const
{
    // Where a click folds or opens the strip: the title, up to the screen (folded) or the keys.
    const int right = folded ? display.getX() - 4 : freezeBounds().getX() - 8;
    return { 0, 0, right, kTitleHeight };
}

void HoldPanel::paint(juce::Graphics& g)
{
    ModulePanel::paint(g);
    // Before the title: which way the strip opens.
    g.setColour(theme().textDim);
    juce::Path chevron;
    const float x = 17.0f, y = (float) kTitleHeight * 0.5f;
    if (folded)
        chevron.addTriangle(x - 3.0f, y - 5.0f, x - 3.0f, y + 5.0f, x + 4.0f, y);
    else
        chevron.addTriangle(x - 5.0f, y - 3.0f, x + 5.0f, y - 3.0f, x, y + 4.0f);
    g.fillPath(chevron);
    if (folded)
        return;
    // GRAINS and PLAY over their knobs, as SOUND and CONTROL in GLIDE.
    const int count = (int) knobs().size();
    drawGroupRule(g, "Grains", (float) knobX(0) + 4.0f, (float) (kGrainKnobs * kKnobColumn - 8), (float) kRuleY);
    drawGroupRule(g, "Play", (float) knobX(kGrainKnobs) + 4.0f, (float) ((count - kGrainKnobs) * kKnobColumn - 8),
                  (float) kRuleY);
}

void HoldPanel::resized()
{
    on.setBounds(onKeyBounds());
    freeze.setBounds(freezeBounds());
    if (folded)
    {
        // The screen shrinks into the title bar, between the title and the keys.
        display.setBounds(juce::Rectangle<int>(110, 6, freezeBounds().getX() - 12 - 110, kTitleHeight - 12));
        return;
    }
    const int m = PanelLookAndFeel::kMargin, h = Knob::kHeight;
    display.setBounds(m, kRowTop, kScreenWidth, h);
    const auto all = knobs();
    for (int i = 0; i < (int) all.size(); ++i)
        all[(size_t) i]->setBounds(knobX(i), kRowTop, kKnobColumn, h);
}

void HoldPanel::mouseMove(const juce::MouseEvent& e)
{
    setMouseCursor(titleArea().contains(e.getPosition()) ? juce::MouseCursor::PointingHandCursor
                                                         : juce::MouseCursor::NormalCursor);
}

void HoldPanel::mouseDown(const juce::MouseEvent& e)
{
    if (!e.mods.isLeftButtonDown() || !titleArea().contains(e.getPosition()))
        return;
    setFolded(!folded);
    if (onFold)
        onFold(folded);
}

} // namespace glide90::ui
