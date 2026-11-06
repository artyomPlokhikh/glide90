#include "PluginEditor.h"

namespace glide90
{

namespace
{
constexpr double kSlideSeconds = 0.22;
constexpr int kLift = 3; // a dragged module rises this far
} // namespace

Glide90Editor::Glide90Editor(Glide90Processor& p)
    : AudioProcessorEditor(p), proc(p), header(p),
      input(p.state(), ids::input, names::in, "INPUT", [&p] { return p.takeInputPeak(); }, ids::inputMode,
            (int) InputMode::Stereo),
      output(p.state(), ids::output, names::out, "OUTPUT", [&p] { return p.takeOutputPeak(); }, ids::outputMode,
             (int) OutputMode::Stereo),
      glide(p), gate(p)
{
    ui::initialiseTheme();
    lookAndFeel.applyTheme();
    setLookAndFeel(&lookAndFeel);
    addAndMakeVisible(content);
    for (auto* c : std::initializer_list<juce::Component*> { &header, &input, &glide, &gate, &output })
        content.addAndMakeVisible(c);
    content.setSize(kWidth, kHeight);
    input.markClip();
    // The rig's controls, which A/B and presets leave alone, carry a padlock.
    for (auto* key : { &input.modeKey(), &output.modeKey() })
        key->getProperties().set(ui::PanelLookAndFeel::kLocked, true);
    input.levelKnob().setLocked(true);
    const juce::String padlock = " Locked: presets and A/B don't change it";
    input.modeKey().setTooltip("STEREO: sides stay in the direct sound; the reverse gate hears their sum. MONO: one input."
                               + padlock);
    input.levelKnob().setTooltip("Level into the plugin, the pick detector included. Keep the meter's CLIP dark." + padlock);
    output.levelKnob().setTooltip("Final output level, in dB");
    output.modeKey().setTooltip("L ONLY: the left output on both sides, for one amp. MONO: both outputs summed." + padlock);
    content.themeChanged();

    // Each unit is kept as a picture: moving one, or repainting the moving background behind it,
    // copies the picture instead of drawing every knob again.
    for (auto* c : std::initializer_list<juce::Component*> { &header, &input, &glide, &gate, &output })
        c->setBufferedToImage(true);

    gate.setMotion(&motion);
    header.onZoom = [this](float s) { setZoom(s); };
    header.onTheme = [this](int i) { setLook(i); };
    // The modules swap places by their title bars; the order is a parameter (one undo step).
    for (auto* module : { static_cast<ui::ModulePanel*>(&glide), static_cast<ui::ModulePanel*>(&gate) })
    {
        module->setDraggable(true);
        module->setDragTooltip("Drag to swap GLIDE and REVERSE GATE. GLIDE first: bends the gate's input. "
                               "GATE first: bends its whole tail");
        module->onDragStart = [this, module] { startDrag(*module); };
        module->onDrag = [this, module](float dx) { dragTo(*module, dx); };
        module->onDragEnd = [this, module](bool cancelled) { endDrag(*module, cancelled); };
    }
    position.emplace(*p.state().getParameter(ids::glidePosition), [this](float v) {
        glideFirst = (GlidePosition) juce::roundToInt(v) == GlidePosition::Pre;
        if (dragged == nullptr)
            layoutModules(isShowing());
    });
    position->sendInitialUpdate();
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
    if (kHeight * (double) wanted <= room)
        return wanted;
    for (float s : { 1.75f, 1.5f, 1.25f, 1.0f })
        if (s < wanted && kHeight * (double) s <= room)
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
    setSize(juce::roundToInt(kWidth * proc.uiScale()), juce::roundToInt(kHeight * proc.uiScale()));
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
    auto raw = [this](const char* id) { return proc.value(id); };
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
    moveSlides(seconds);
}

void Glide90Editor::resized()
{
    const int m = ui::PanelLookAndFeel::kMargin;
    header.setBounds(0, 0, kWidth, kHeaderHeight);
    // IN and OUT down the sides, their level knobs with the modules' lower row.
    const int columnHeight = kHeight - m - kBodyTop;
    input.setBounds(m, kBodyTop, ui::IoColumn::kWidth, columnHeight);
    output.setBounds(kWidth - m - ui::IoColumn::kWidth, kBodyTop, ui::IoColumn::kWidth, columnHeight);
    input.setKnobRow(ui::ModulePanel::kRow2);
    output.setKnobRow(ui::ModulePanel::kRow2);
    if (dragged == nullptr)
        layoutModules(false);
}

void Glide90Editor::parentHierarchyChanged()
{
   #if JUCE_WINDOWS
    // Direct2D, JUCE's renderer on Windows, throws the window's picture away whenever the window
    // changes size and shows nothing (white in hosts) until a display frame has drawn it all again.
    // The software renderer keeps the old picture meanwhile.
    if (auto* peer = getPeer())
        if (const int software = peer->getAvailableRenderingEngines().indexOf("Software Renderer");
            software >= 0 && peer->getCurrentRenderingEngine() != software)
            peer->setCurrentRenderingEngine(software);
   #endif
}

// --- Dragging a module to the other place ---

juce::Rectangle<int> Glide90Editor::slotOf(const ui::ModulePanel& module, bool glideFirstOrder) const
{
    const int glideW = ui::GlidePanel::widthFor(ui::GlidePanel::kColumns), gateW = ui::GatePanel::widthFor(ui::GatePanel::kColumns);
    const bool isGlide = &module == &glide;
    const bool first = isGlide == glideFirstOrder;
    const int x = first ? modulesLeft() : modulesLeft() + (glideFirstOrder ? glideW : gateW) + kGap;
    return { x, kBodyTop, isGlide ? glideW : gateW, ui::ModulePanel::kHeight };
}

void Glide90Editor::layoutModules(bool animate)
{
    for (auto* module : { static_cast<ui::ModulePanel*>(&glide), static_cast<ui::ModulePanel*>(&gate) })
        slide(*module, slotOf(*module, glideFirst), animate);
}

void Glide90Editor::slide(ui::ModulePanel& module, juce::Rectangle<int> target, bool animate)
{
    auto& s = slides[&module == &glide ? 0 : 1];
    if (!animate || !isShowing() || module.getBounds().isEmpty())
    {
        s.module = nullptr;
        module.setBounds(target);
        return;
    }
    if (s.module != nullptr && s.to == target)
        return; // already on its way there
    s = { &module, module.getBounds(), target, -1.0 };
}

void Glide90Editor::moveSlides(double seconds)
{
    for (auto& s : slides)
    {
        if (s.module == nullptr)
            continue;
        if (s.start < 0.0)
            s.start = seconds;
        const double p = juce::jlimit(0.0, 1.0, (seconds - s.start) / kSlideSeconds);
        const double eased = 1.0 - std::pow(1.0 - p, 3.0); // fast, then settling
        auto lerp = [eased](int a, int b) { return juce::roundToInt(a + (b - a) * eased); };
        s.module->setBounds(lerp(s.from.getX(), s.to.getX()), lerp(s.from.getY(), s.to.getY()), s.to.getWidth(), s.to.getHeight());
        if (p >= 1.0)
            s.module = nullptr;
    }
}

void Glide90Editor::startDrag(ui::ModulePanel& module)
{
    // Whatever was sliding, finish it now.
    for (auto& s : slides)
        if (s.module != nullptr)
        {
            s.module->setBounds(s.to);
            s.module = nullptr;
        }
    dragged = &module;
    dragStartX = module.getX();
    shownOrder = glideFirst;
    module.toFront(false);
    module.setLifted(true);
    module.setTopLeftPosition(module.getX(), kBodyTop - kLift);
    content.setDragGuides(slotOf(module, glideFirst), module.getBounds());
}

bool Glide90Editor::glideFirstWhileDragging() const
{
    // The dragged module lands in whichever of its two places it is nearer to (the modules differ in
    // width, so their centres would not do).
    const int x = dragged->getX();
    const int here = slotOf(*dragged, glideFirst).getX(), there = slotOf(*dragged, !glideFirst).getX();
    return std::abs(x - there) < std::abs(x - here) ? !glideFirst : glideFirst;
}

void Glide90Editor::dragTo(ui::ModulePanel& module, float dx)
{
    if (dragged != &module)
        return;
    // Along the row only, with a little give past its ends.
    const int lo = modulesLeft() - 20, hi = modulesLeft() + kModulesWidth - module.getWidth() + 20;
    module.setTopLeftPosition(juce::jlimit(lo, hi, dragStartX + juce::roundToInt(dx)), kBodyTop - kLift);

    // The other module makes room as soon as the dragged one passes it.
    const bool order = glideFirstWhileDragging();
    if (order != shownOrder)
    {
        shownOrder = order;
        auto& other = &module == &glide ? static_cast<ui::ModulePanel&>(gate) : static_cast<ui::ModulePanel&>(glide);
        slide(other, slotOf(other, order), true);
    }
    content.setDragGuides(slotOf(module, shownOrder), module.getBounds());
}

void Glide90Editor::endDrag(ui::ModulePanel& module, bool cancelled)
{
    if (dragged != &module)
        return;
    const bool wanted = cancelled ? glideFirst : glideFirstWhileDragging();
    dragged = nullptr;
    module.setLifted(false);
    content.setDragGuides({}, {});
    if (wanted != glideFirst)
    {
        // One step to undo; the parameter's callback slides both modules into place.
        auto* param = proc.state().getParameter(ids::glidePosition);
        param->beginChangeGesture();
        param->setValueNotifyingHost(param->convertTo0to1((float) (wanted ? GlidePosition::Pre : GlidePosition::Post)));
        param->endChangeGesture();
    }
    else
        layoutModules(true);
}

} // namespace glide90
