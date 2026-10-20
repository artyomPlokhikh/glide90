#include "GatePanel.h"

#include "Constants.h"
#include "PanelLookAndFeel.h"
#include "Parameters.h"
#include "PluginProcessor.h"
#include "RomPrograms.h"

namespace glide90::ui
{
namespace
{

// A round length for the time scale, just over `ms`.
float scaleFor(float ms)
{
    for (float s : { 50.0f, 100.0f, 200.0f, 300.0f, 500.0f, 750.0f, 1000.0f, 1500.0f, 2000.0f, 2500.0f })
        if (s >= ms * 1.06f)
            return s;
    return 3000.0f;
}

juce::String timeText(float ms)
{
    return ms < 1000.0f ? juce::String(juce::roundToInt(ms)) + " MS" : juce::String(ms / 1000.0f, 2) + " S";
}

} // namespace

// --- GateDisplay ---

GateDisplay::GateDisplay(Glide90Processor& p) : proc(p)
{
    setTooltip("The reflections of the reverse gate: they swell up and stop. ROOM SIZE stretches them, LIVENESS "
               "sets how steeply they swell, PRE-DELAY moves them later");
    update();
    shownScaleMs = scaleFor(lengthMs);
}

GateDisplay::~GateDisplay() = default;

bool GateDisplay::update()
{
    auto value = [this](const char* id) { return proc.state().getRawParameterValue(id)->load(); };
    // The same taps the engine plays, on the same grid.
    spx::ErSettings s = spx::romProgram(17).er;
    s.roomSize = (float) juce::roundToInt(value(ids::gateRoomSize) * 10.0f) / 10.0f;
    s.liveness = juce::roundToInt(value(ids::gateLiveness));
    s.delayMs = (float) juce::roundToInt(value(ids::gatePreDelay) * 10.0f) / 10.0f;
    const bool nowOn = value(ids::gateOn) > 0.5f;
    if (s == settings && nowOn == on && !left.empty())
        return false;
    settings = s;
    on = nowOn;
    spx::ErEngine::buildTaps(s, 0, left);
    spx::ErEngine::buildTaps(s, 1, right);
    int last = 1;
    loudest = 1e-6f;
    for (const auto* taps : { &left, &right })
        for (const auto& tap : *taps)
        {
            last = juce::jmax(last, tap.delay);
            loudest = juce::jmax(loudest, std::abs(tap.gain));
        }
    lengthMs = (float) (last * 1000.0 / spx::kDspRate);
    return true;
}

void GateDisplay::frame(double seconds)
{
    bool changed = update();
    // The light running through the reflections with the swell.
    const float progress = motion != nullptr && motionLevel() != MotionLevel::Off && on ? motion->progress : -1.0f;
    if (std::abs(progress - shownProgress) > 0.002f)
    {
        shownProgress = progress;
        changed = true;
    }
    // The time scale glides to its new length instead of jumping.
    const float target = scaleFor(lengthMs);
    const float elapsed = lastFrame > 0.0 ? (float) juce::jlimit(0.0, 0.1, seconds - lastFrame) : 0.0f;
    lastFrame = seconds;
    if (std::abs(target - shownScaleMs) > 0.5f)
    {
        shownScaleMs += (target - shownScaleMs) * (1.0f - std::exp(-elapsed / 0.07f));
        changed = true;
    }
    else if (!juce::exactlyEqual(shownScaleMs, target))
    {
        shownScaleMs = target;
        changed = true;
    }
    if (changed)
        repaint();
}

void GateDisplay::paint(juce::Graphics& g)
{
    const auto& t = theme();
    const auto bounds = getLocalBounds().toFloat();
    drawGlass(g, bounds);
    const auto ink = t.ink.withAlpha(on ? 1.0f : kOffInkAlpha);
    auto msOf = [](int samples) { return (float) (samples * 1000.0 / spx::kDspRate); };
    const float scaleMs = shownScaleMs;
    const auto& s = settings;

    const auto plot = bounds.withTrimmedLeft(14.0f).withTrimmedRight(14.0f).withTrimmedTop(34.0f).withTrimmedBottom(26.0f);
    auto xOf = [&](float ms) { return plot.getX() + ms / scaleMs * plot.getWidth(); };

    // The pre-delay: nothing yet.
    const float preX = xOf(s.delayMs);
    g.setColour(t.ink.withAlpha(0.07f));
    g.fillRect(juce::Rectangle<float>(plot.getX(), plot.getY(), preX - plot.getX(), plot.getHeight()));

    // While a swell plays, a light runs through the reflections at its pace: the ones it has passed
    // are lit, the rest wait dimmed. A light band trails it.
    const float progress = motion != nullptr && motionLevel() != MotionLevel::Off && on ? motion->progress : -1.0f;
    const bool running = progress >= 0.0f;
    const float scanMs = s.delayMs + progress * (lengthMs - s.delayMs);
    if (running)
    {
        const float x = xOf(scanMs);
        const auto band = t.backdrop == Theme::Backdrop::Matte ? juce::Colours::white.withAlpha(0.22f) : t.ink.withAlpha(0.16f);
        g.setGradientFill(juce::ColourGradient(band.withAlpha(0.0f), x - 36.0f, 0.0f, band, x, 0.0f, false));
        g.fillRect(juce::Rectangle<float>(x - 36.0f, plot.getY(), 36.0f, plot.getHeight()));
    }

    // The baseline and the taps: left solid, right beside it, fainter.
    g.setColour(t.ink.withAlpha(0.3f));
    g.drawHorizontalLine((int) plot.getBottom(), plot.getX(), plot.getRight());
    juce::Path envelope;
    for (int channel = 1; channel >= 0; --channel)
    {
        const auto& taps = channel == 0 ? left : right;
        const auto colour = channel == 0 ? ink : t.inkDim.withMultipliedAlpha(on ? 1.0f : 0.5f);
        for (const auto& tap : taps)
        {
            const float x = xOf(msOf(tap.delay)) + (channel == 0 ? 0.0f : 1.5f);
            const float hgt = std::abs(tap.gain) / loudest * plot.getHeight();
            g.setColour(running && msOf(tap.delay) > scanMs ? colour.withMultipliedAlpha(0.55f) : colour);
            g.fillRect(x - 0.6f, plot.getBottom() - hgt, 1.3f, hgt);
            if (channel == 0)
            {
                if (envelope.isEmpty())
                    envelope.startNewSubPath(x, plot.getBottom() - hgt);
                else
                    envelope.lineTo(x, plot.getBottom() - hgt);
            }
        }
    }
    if (!envelope.isEmpty())
        strokeGlowing(g, envelope, ink.withMultipliedAlpha(0.55f), 1.0f);

    // The cut, dashed.
    const float cutX = xOf(lengthMs);
    g.setColour(ink.withMultipliedAlpha(0.6f));
    for (float y = plot.getY(); y < plot.getBottom(); y += 5.0f)
        g.fillRect(cutX + 2.0f, y, 1.0f, 2.5f);

    drawReadout(g, on ? juce::String("REVERSE") : juce::String("GATE OFF"), { 12.0f, 6.0f, 120.0f, 22.0f }, t.ink.withAlpha(on ? 0.8f : kOffInkAlpha));
    drawReadout(g, timeText(lengthMs), { bounds.getRight() - 132.0f, 6.0f, 120.0f, 22.0f }, ink, juce::Justification::right, true, true);
    const juce::Rectangle<float> axis { plot.getX(), plot.getBottom() + 6.0f, plot.getWidth(), 14.0f };
    drawReadout(g, "PRE " + timeText(s.delayMs), axis, t.ink.withAlpha(on ? 0.9f : 0.65f));
    drawReadout(g, timeText(scaleFor(lengthMs)), axis, t.ink.withAlpha(on ? 0.7f : 0.55f), juce::Justification::right);
}

// --- GatePanel ---

GatePanel::GatePanel(Glide90Processor& p)
    : ModulePanel("Reverse Gate"), on(p.state(), ids::gateOn, "ON"), display(p),
      roomSize(p.state(), ids::gateRoomSize, "ROOM SIZE", Knob::Size::Hero), liveness(p.state(), ids::gateLiveness, "LIVENESS"),
      delay(p.state(), ids::gatePreDelay, "PRE-DELAY"), lpf(p.state(), ids::gateLpf, "LPF"), mix(p.state(), ids::gateMix, "MIX")
{
    for (auto* c : std::initializer_list<juce::Component*> { &on, &display, &roomSize, &liveness, &delay, &lpf, &mix })
        addAndMakeVisible(c);

    on.setTooltip("Reverse gate on/off. Off is the unit's analog bypass: the direct sound is then full bandwidth, "
                  "and HOLD goes to the output");
    roomSize.setTooltip("Spacing of the reflections. At 20 the reverse gate swells for over a second");
    liveness.setTooltip("How steeply the reflections swell");
    delay.setTooltip("Time before the first reflection");
    lpf.setTooltip("Low-pass on the effect, 1-11 kHz or THRU");
    mix.setTooltip("0 = dry only, 100 = effect only. The unit mixes digitally, so even the dry signal is band-limited "
                   "to 12 kHz");
}

void GatePanel::paint(juce::Graphics& g)
{
    ModulePanel::paint(g);
    drawGroupRule(g, "Mix", (float) knobX(1), (float) (2 * Knob::kColumn), (float) kRuleY);
}

void GatePanel::resized()
{
    const int h = Knob::kHeight, w = Knob::kColumn;
    on.setBounds(onKeyBounds());
    display.setBounds(columnX(0), kScreenTop, kColumns * w, kScreenHeight);
    roomSize.setBounds(knobX(0), kRow1, w, kRow2 + h - kRow1);
    liveness.setBounds(knobX(1), kRow1, w, h);
    delay.setBounds(knobX(2), kRow1, w, h);
    lpf.setBounds(knobX(1), kRow2, w, h);
    mix.setBounds(knobX(2), kRow2, w, h);
}

// --- PickLight ---

PickLight::PickLight(std::function<int()> count) : picksHeard(std::move(count)) {}

void PickLight::frame(double seconds)
{
    const int now = picksHeard ? picksHeard() : 0;
    if (seen >= 0 && now != seen)
        litUntil = seconds + 0.08;
    seen = now;
    if (const bool shouldBeLit = seconds < litUntil; shouldBeLit != lit)
    {
        lit = shouldBeLit;
        repaint();
    }
}

void PickLight::paint(juce::Graphics& g)
{
    // Round, unlike the meter's segments above it: a light of its own.
    const auto& t = theme();
    const auto dot = getLocalBounds().toFloat().reduced(1.0f);
    if (lit)
    {
        g.setColour(t.accent.withAlpha(0.35f));
        g.fillEllipse(dot.expanded(1.0f));
        g.setColour(t.accent);
        g.fillEllipse(dot.reduced(1.0f));
    }
    else
    {
        g.setColour(t.textDim.withAlpha(0.25f));
        g.fillEllipse(dot.reduced(1.0f));
    }
    g.setColour(t.textDim.withAlpha(0.6f));
    g.drawEllipse(dot, 1.0f);
}

// --- IoColumn ---

IoColumn::IoColumn(juce::AudioProcessorValueTreeState& state, const char* paramId, const juce::String& text,
                   const juce::String& knobLabel, std::function<float()> takePeak, const char* modeParamId,
                   int stereoChoice)
    : title(text), meter(std::move(takePeak)), knob(state, paramId, knobLabel), mode(state, modeParamId, stereoChoice)
{
    mode.setComponentID(modeParamId);
    addAndMakeVisible(meter);
    addAndMakeVisible(knob);
    addAndMakeVisible(mode);
}

void IoColumn::addPickSense(juce::AudioProcessorValueTreeState& state, const char* paramId, std::function<int()> picksHeard)
{
    pick = std::make_unique<Knob>(state, paramId, "PICK SENS");
    light = std::make_unique<PickLight>(std::move(picksHeard));
    addAndMakeVisible(*pick);
    addAndMakeVisible(*light);
    resized();
}

void IoColumn::setKnobRows(int pickTop, int levelTop)
{
    pickRow = pickTop;
    levelRow = levelTop;
    resized();
    repaint();
}

namespace
{
// The meter's scale: the dB marks beside it.
constexpr float kScaleDb[] { 0.0f, -12.0f, -24.0f, -40.0f };
} // namespace

void IoColumn::paint(juce::Graphics& g)
{
    const auto& t = theme();
    drawCard(g, getLocalBounds().toFloat());
    g.setFont(legendFont(t.matrix ? 13.0f : 17.0f, true).withExtraKerningFactor(t.matrix ? 0.14f : 0.02f));
    g.setColour(t.text);
    g.drawText(themed(title), juce::Rectangle<int>(0, 8, getWidth(), 26), juce::Justification::centred);

    const auto m = meter.getBounds().toFloat();
    g.setFont(legendFont(11.0f));
    for (float db : kScaleDb)
    {
        const float y = m.getY() + meter.markY(db);
        // The input meter shows the reverse gate's A/D: its top is where that clips, a warning.
        const bool clip = db >= 0.0f && pick != nullptr;
        g.setColour(clip ? t.text : t.textDim.interpolatedWith(t.text, 0.3f));
        g.drawText(clip ? themed("CLIP") : juce::String(juce::roundToInt(db)),
                   scaleArea().withY(y - 6.0f).withHeight(12.0f), juce::Justification::centredLeft, false);
    }
}

juce::Rectangle<float> IoColumn::scaleArea() const
{
    const auto m = meter.getBounds().toFloat();
    return { m.getRight() + 5.0f, m.getY() - 6.0f, 30.0f, m.getHeight() + 12.0f };
}

void IoColumn::resized()
{
    mode.setBounds(6, ModulePanel::kScreenTop, getWidth() - 12, 28);
    const int meterTop = mode.getBottom() + 12;
    // The knobs where the editor says (the rows of knobs beside the column), the meter above them.
    const int gap = ModulePanel::kRow2 - ModulePanel::kRuleY + 14;
    const int knobX = (getWidth() - Knob::kColumn) / 2;
    knob.setBounds(knobX, levelRow, Knob::kColumn, Knob::kHeight);
    int firstKnob = levelRow;
    if (pick != nullptr)
    {
        pick->setBounds(knobX, pickRow, Knob::kColumn, Knob::kHeight);
        light->setBounds(juce::Rectangle<int>(10, 10).withCentre({ getWidth() / 2, pickRow - gap / 2 }));
        firstKnob = pickRow - 10; // room between the meter and the light
    }
    meter.setBounds(26, meterTop, 14, juce::jmax(0, firstKnob - gap - meterTop));
}

} // namespace glide90::ui
