#pragma once

#include "PluginProcessor.h"
#include "ui/GatePanel.h"
#include "ui/GlidePanel.h"
#include "ui/HeaderBar.h"
#include "ui/PanelLookAndFeel.h"

#include <array>
#include <optional>

namespace glide90
{

// Under the header, IN, the two modules in the order they play, OUT. Dragging a module by its title bar
// to the other place turns the order round.
class Glide90Editor : public juce::AudioProcessorEditor
{
public:
    static constexpr int kGap = 12;
    static constexpr int kModulesWidth = ui::GlidePanel::widthFor(ui::GlidePanel::kColumns) + kGap
                                         + ui::GatePanel::widthFor(ui::GatePanel::kColumns);
    static constexpr int kWidth = 2 * ui::PanelLookAndFeel::kMargin + 2 * ui::IoColumn::kWidth + 2 * kGap + kModulesWidth;
    static constexpr int kHeaderHeight = 56, kBodyTop = 64;
    // The height at 100 %.
    static constexpr int kHeight = kBodyTop + ui::ModulePanel::kHeight + ui::PanelLookAndFeel::kMargin;

    explicit Glide90Editor(Glide90Processor&);
    ~Glide90Editor() override;

    void resized() override;
    void parentHierarchyChanged() override;

    // Shows the looks' motion for `m` (a still of a look in motion, for snapshots); the next display
    // frame goes back to what the sound does.
    void showMotion(const ui::Motion& m);

private:
    void setZoom(float scale);
    // `wanted`, or a smaller zoom if the window would not fit the screen at it.
    float fittingZoom(float wanted) const;
    void setLook(int index);
    int modulesLeft() const { return ui::PanelLookAndFeel::kMargin + ui::IoColumn::kWidth + kGap; }
    void frame(double seconds);
    // Reads what the sound does into `motion`, for the background and the screens.
    void updateMotion(double seconds);

    // --- The modules' order, dragged ---
    // Puts the modules in their places for the order the parameter says, sliding them there or not.
    void layoutModules(bool animate);
    // Where each module goes with GLIDE first or second.
    juce::Rectangle<int> slotOf(const ui::ModulePanel& module, bool glideFirstOrder) const;
    void startDrag(ui::ModulePanel& module);
    void dragTo(ui::ModulePanel& module, float dx);
    void endDrag(ui::ModulePanel& module, bool cancelled);
    // Slides a module to `target` over a few frames of the display (at once while not on screen).
    void slide(ui::ModulePanel& module, juce::Rectangle<int> target, bool animate);
    void moveSlides(double seconds);
    // The order the dragged module's place gives.
    bool glideFirstWhileDragging() const;

    Glide90Processor& proc;
    ui::PanelLookAndFeel lookAndFeel;
    // Everything is laid out at 100 % inside `content`, which is scaled to the window.
    ui::Backdrop content;
    ui::HeaderBar header;
    ui::IoColumn input, output;
    ui::GlidePanel glide;
    ui::GatePanel gate;
    std::optional<juce::ParameterAttachment> position;
    bool glideFirst = true;
    ui::ModulePanel* dragged = nullptr;
    int dragStartX = 0;
    bool shownOrder = true; // the order the other module currently makes room for
    struct Slide
    {
        ui::ModulePanel* module = nullptr;
        juce::Rectangle<int> from, to;
        double start = -1.0; // the first frame's time, once it comes
    };
    std::array<Slide, 2> slides;

    ui::Motion motion;
    double lastFrame = 0.0, riseStart = 0.0, cutAt = -1.0e6;
    float wetDb = -100.0f, loudDb = -40.0f;
    bool rising = false;
    juce::VBlankAttachment vblank { this, [this](double seconds) { frame(seconds); } };
    juce::TooltipWindow tooltips { this, 700 };
};

} // namespace glide90
