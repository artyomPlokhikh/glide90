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
      input(p.state(), ids::input, "In", "INPUT", [&p] { return p.takeInputPeak(); }),
      output(p.state(), ids::outLevel, "Out", "LEVEL", [&p] { return p.takeOutputPeak(); }), glide(p), gate(p),
      position(*p.state().getParameter(ids::glidePosition), [this](float v) {
          glideFirst = (GlidePosition) juce::roundToInt(v) == GlidePosition::Pre;
          if (dragged == nullptr)
              layoutModules(isShowing());
      })
{
    ui::initialiseTheme();
    lookAndFeel.applyTheme();
    setLookAndFeel(&lookAndFeel);
    addAndMakeVisible(content);
    for (auto* c : std::initializer_list<juce::Component*> { &header, &input, &glide, &gate, &output })
        content.addAndMakeVisible(c);
    content.setSize(kWidth, kHeight);
    content.themeChanged();

    // Each unit is kept as a picture: moving one, or repainting the moving background behind it,
    // copies the picture instead of drawing every knob again.
    for (auto* c : std::initializer_list<juce::Component*> { &header, &input, &glide, &gate, &output })
        c->setBufferedToImage(true);

    gate.setMotion(&motion);
    for (auto* module : { static_cast<ui::ModulePanel*>(&glide), static_cast<ui::ModulePanel*>(&gate) })
    {
        module->onDragStart = [this, module] { startDrag(*module); };
        module->onDrag = [this, module](float dx) { dragTo(*module, dx); };
        module->onDragEnd = [this, module](bool cancelled) { endDrag(*module, cancelled); };
    }

    position.sendInitialUpdate();
    header.onZoom = [this](float s) { setZoom(s); };
    header.onTheme = [this](int i) { setLook(i); };
    setZoom(proc.uiScale());
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

void Glide90Editor::updateMotion(double seconds)
{
    const float dt = lastFrame > 0.0 ? (float) juce::jlimit(0.0, 0.1, seconds - lastFrame) : 0.0f;
    lastFrame = seconds;
    auto raw = [this](const char* id) { return proc.state().getRawParameterValue(id)->load(); };
    auto& m = motion;
    m.time = seconds;
    m.cents = proc.barCents();
    m.bend = juce::jmin(1.0f, std::abs(m.cents) / 60.0f);
    m.room = raw(ids::erRoomSize);
    m.gateSeconds = 0.072f * m.room + raw(ids::erDelay) / 1000.0f; // ROOM SIZE 20 swells for 1.44 s

    // The swell: the effect's level, up at once, down fast (the gate cuts sharply), against the
    // loudest it has been lately, so quiet playing moves the pictures as much as loud.
    const float db = juce::Decibels::gainToDecibels(proc.takeWetPeak(), -100.0f);
    wetDb = juce::jmax(db, wetDb - 120.0f * dt);
    loudDb = juce::jmax(wetDb, loudDb - 4.0f * dt, -40.0f);
    const bool gateOn = raw(ids::bypass) < 0.5f;
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

void Glide90Editor::resized()
{
    const int m = ui::PanelLookAndFeel::kMargin, h = ui::ModulePanel::kHeight;
    header.setBounds(0, 0, kWidth, kHeaderHeight);
    input.setBounds(m, kBodyTop, ui::IoColumn::kWidth, h);
    output.setBounds(kWidth - m - ui::IoColumn::kWidth, kBodyTop, ui::IoColumn::kWidth, h);
    if (dragged == nullptr)
        layoutModules(false);
}

// --- Dragging a module to the other place ---

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
    slides[&module == &glide ? 0 : 1].module = nullptr;
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
