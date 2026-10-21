#include "PluginEditor.h"

namespace glide90
{

Glide90Editor::Glide90Editor(Glide90Processor& p)
    : AudioProcessorEditor(p), proc(p), header(p), pickSense(p.state(), ids::pickSensitivity, [&p] { return p.picksHeard(); }),
      routing(p), dry(p.state(), ids::dryLevel, names::dry),
      input(p.state(), ids::input, names::in, "INPUT", [&p] { return p.takeInputPeak(); }, ids::inputMode,
            (int) InputMode::Stereo),
      output(p.state(), ids::output, names::out, "OUTPUT", [&p] { return p.takeOutputPeak(); }, ids::outputMode,
             (int) OutputMode::Stereo),
      glide(p), gate(p), holdPanel(p)
{
    ui::initialiseTheme();
    lookAndFeel.applyTheme();
    setLookAndFeel(&lookAndFeel);
    addAndMakeVisible(content);
    for (auto* c : std::initializer_list<juce::Component*> { &header, &pickSense, &routing, &dry, &input, &glide, &gate, &holdPanel, &output })
        content.addAndMakeVisible(c);
    content.setSize(kWidth, contentHeight());
    input.markClip();
    // The rig's controls, which A/B and presets leave alone, carry a padlock.
    for (auto* key : { &input.modeKey(), &output.modeKey() })
        key->getProperties().set(ui::PanelLookAndFeel::kLocked, true);
    input.levelKnob().setLocked(true);
    const juce::String rig = ". Set for the rig: A/B and presets leave it alone";
    input.modeKey().setTooltip("Stereo: the direct sound keeps its sides, the reverse gate hears both. "
                               "Mono: one input, like the SPX90's" + rig);
    input.levelKnob().setTooltip("The guitar's level into everything: the glide, HOLD, the pick detector and the "
                                 "reverse gate. Set it so the meter's CLIP light stays dark" + rig);
    pickSense.knob().setTooltip("How easily a pick attack is heard: it restarts GLIDE (TRIGGER from PICK ATTACK) and "
                                "captures a new chord in HOLD (STRIKE, MIDI). Its pointer flashes on each pick heard. "
                                "At 0 it is off: no pick is heard");
    dry.setTooltip("The direct guitar, straight to OUT beside the reverse gate, bent whenever GLIDE is on. Off, only "
                   "the effects are heard: the reverse gate's swell without the attack. Part of the sound: each of A "
                   "and B has its own");
    output.modeKey().setTooltip("Stereo, the left output on both sides (one amp), or both outputs summed" + rig);
    output.levelKnob().setTooltip("The level of everything that leaves the plugin: the modules and HOLD, with the "
                                  "reverse gate on or off. Part of the sound: each of A and B has its own");
    content.themeChanged();

    // Each unit is kept as a picture: moving one, or repainting the moving background behind it,
    // copies the picture instead of drawing every knob again.
    for (auto* c : std::initializer_list<juce::Component*> { &header, &routing, &input, &glide, &gate, &holdPanel, &output })
        c->setBufferedToImage(true);

    gate.setMotion(&motion);
    header.onZoom = [this](float s) { setZoom(s); };
    header.onTheme = [this](int i) { setLook(i); };
    holdPanel.onFold = [this](bool folded) {
        ui::setHoldFolded(folded);
        content.setSize(kWidth, contentHeight());
        setZoom(proc.uiScale());
    };
    setZoom(fittingZoom(proc.uiScale()));
}

float Glide90Editor::fittingZoom(float wanted) const
{
    // The window is tall: on a small screen open it at the largest zoom that fits (never larger
    // than the one saved). The zoom menu still offers every size.
    const auto* display = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay();
    if (display == nullptr)
        return wanted;
    const double room = display->userBounds.getHeight() - 80.0; // the host's own window frame and title
    if (contentHeight() * (double) wanted <= room)
        return wanted;
    for (float s : { 1.75f, 1.5f, 1.25f, 1.0f })
        if (s < wanted && contentHeight() * (double) s <= room)
            return s;
    return 0.75f;
}

Glide90Editor::~Glide90Editor()
{
    setLookAndFeel(nullptr);
}

void Glide90Editor::setZoom(float scale)
{
    proc.setUiScale(scale);
    content.setTransform(juce::AffineTransform::scale(proc.uiScale()));
    setSize(juce::roundToInt(kWidth * proc.uiScale()), juce::roundToInt(contentHeight() * proc.uiScale()));
}

void Glide90Editor::setLook(int index)
{
    ui::setTheme(ui::themeAt(index).id);
    ui::rememberTheme();
    lookAndFeel.applyTheme();
    content.themeChanged();
    sendLookAndFeelChange();
    content.repaint();
}

void Glide90Editor::updateMotion(double seconds)
{
    const float dt = lastFrame > 0.0 ? (float) juce::jlimit(0.0, 0.1, seconds - lastFrame) : 0.0f;
    lastFrame = seconds;
    auto raw = [this](const char* id) { return proc.state().getRawParameterValue(id)->load(); };
    auto& m = motion;
    m.time = seconds;
    m.cents = proc.barCents();
    m.bend = juce::jmin(1.0f, std::abs(m.cents) / 60.0f);
    m.room = raw(ids::gateRoomSize);
    m.gateSeconds = 0.072f * m.room + raw(ids::gatePreDelay) / 1000.0f; // ROOM SIZE 20 swells for 1.44 s

    // The swell: the effect's level, up at once, down fast (the gate cuts sharply), against the
    // loudest it has been lately, so quiet playing moves the pictures as much as loud.
    const float db = juce::Decibels::gainToDecibels(proc.takeWetPeak(), -100.0f);
    wetDb = juce::jmax(db, wetDb - 120.0f * dt);
    loudDb = juce::jmax(wetDb, loudDb - 4.0f * dt, -40.0f);
    const bool gateOn = raw(ids::gateOn) > 0.5f;
    m.swell = gateOn && wetDb > -70.0f ? juce::jlimit(0.0f, 1.0f, 1.0f + (wetDb - loudDb) / 30.0f) : 0.0f;
    if (!rising && m.swell > 0.12f)
    {
        rising = true;
        riseStart = seconds;
    }
    else if (rising && m.swell < 0.06f)
    {
        rising = false;
        cutAt = seconds;
    }
    m.progress = rising ? juce::jmin(1.0f, (float) ((seconds - riseStart) / juce::jmax(0.05f, m.gateSeconds))) : -1.0f;
    m.sinceCut = (float) (seconds - cutAt);

    content.setMotion(m);
}

void Glide90Editor::showMotion(const ui::Motion& m)
{
    motion = m;
    content.setMotion(motion);
}

void Glide90Editor::frame(double seconds)
{
    updateMotion(seconds);
    // A mono track has nothing to keep apart.
    if (const bool twoInputs = proc.getTotalNumInputChannels() >= 2; input.modeKey().isEnabled() != twoInputs)
        input.modeKey().setEnabled(twoInputs);
}

void Glide90Editor::resized()
{
    const int m = ui::PanelLookAndFeel::kMargin;
    header.setBounds(0, 0, kWidth, kHeaderHeight);
    // PICK SENS above IN and DRY above OUT, bare; the ROUTING strip between them, over the modules.
    pickSense.setBounds(m, kRouteTop, ui::IoColumn::kWidth, ui::RoutingPanel::kHeight);
    routing.setBounds(modulesLeft(), kRouteTop, kModulesWidth, ui::RoutingPanel::kHeight);
    dry.setBounds(kWidth - m - ui::IoColumn::kWidth, kRouteTop, ui::IoColumn::kWidth, ui::RoutingPanel::kHeight);
    const int glideW = ui::GlidePanel::widthFor(ui::GlidePanel::kColumns);
    const int gateW = ui::GatePanel::widthFor(ui::GatePanel::kColumns);
    glide.setBounds(modulesLeft(), kBodyTop, glideW, ui::ModulePanel::kHeight);
    gate.setBounds(modulesLeft() + glideW + kGap, kBodyTop, gateW, ui::ModulePanel::kHeight);
    // IN and OUT run down the side of everything, HOLD included; their level knobs sit at their foot.
    const int columnHeight = contentHeight() - m - kBodyTop;
    input.setBounds(m, kBodyTop, ui::IoColumn::kWidth, columnHeight);
    output.setBounds(kWidth - m - ui::IoColumn::kWidth, kBodyTop, ui::IoColumn::kWidth, columnHeight);
    // Open, level with HOLD's knobs; folded, at the columns' foot, as far from it as when open.
    const int holdKnobs = kHoldTop + ui::HoldPanel::kRowTop - kBodyTop;
    const int footGap = ui::HoldPanel::kOpenHeight - ui::HoldPanel::kRowTop - ui::Knob::kHeight;
    const int lower = holdPanel.isFolded() ? columnHeight - ui::Knob::kHeight - footGap : holdKnobs;
    input.setKnobRow(lower);
    output.setKnobRow(lower);
    holdPanel.setBounds(modulesLeft(), kHoldTop, kModulesWidth, contentHeight() - m - kHoldTop);
}

void Glide90Editor::parentHierarchyChanged()
{
   #if JUCE_WINDOWS
    // Direct2D, JUCE's renderer on Windows, throws the window's picture away whenever the window
    // changes size and shows nothing (white in hosts) until a display frame has drawn it all again:
    // folding HOLD flashed the whole plugin. The software renderer keeps the old picture meanwhile.
    if (auto* peer = getPeer())
        if (const int software = peer->getAvailableRenderingEngines().indexOf("Software Renderer");
            software >= 0 && peer->getCurrentRenderingEngine() != software)
            peer->setCurrentRenderingEngine(software);
   #endif
}

} // namespace glide90
