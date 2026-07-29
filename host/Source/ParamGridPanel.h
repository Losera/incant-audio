#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"
#include <memory>
#include <vector>

// ── ParamGridPanel ──────────────────────────────────────────────────────────
// Deterministic auto-layout for the compiled patch's parameters
// (docs/ui_design_plan.md §3). Replaces the old fixed 8-rotary grid:
//   • shows ALL mapped params, up to ParamPool::POOL_SIZE (64) — no MAX_KNOBS cap;
//   • picks a widget from each param's Faust Kind (FaustEngine.h:23-30):
//       Button / CheckButton → ToggleButton (never a rotary),
//       HSlider → horizontal, VSlider → vertical, NumEntry → inc/dec,
//       anything else → rotary;
//   • lays them on a sqrt-derived grid (cols = clamp(ceil(sqrt(N)),2,6)) inside a
//     vertically-scrolling Viewport, so a 40-param synth doesn't overflow.
// Per-slot Attachment wiring is preserved (Slider→SliderAttachment,
// Toggle→ButtonAttachment), one attachment per pool slot, rebuilt each compile.
//
// Message-thread only. refreshParamKnobs() is called by the shell from inside its
// onFaustCompileSuccess callAsync hop; the shell then reads preferredContentHeight()
// to grow the window to the row count.
class ParamGridPanel : public juce::Component
{
public:
    explicit ParamGridPanel(PluginForgeProcessor&);

    void resized() override;

    // Rebuild the control surface for a freshly compiled patch: seed every mapped
    // pool slot from the patch defaults, then (re)create one kind-appropriate
    // widget + attachment per param.
    void refreshParamKnobs(const FaustEngine::ParamList& params);

    // Pixel height the grid *content* wants for the current control count (rows ×
    // cell height). The shell uses it to size the window; when the shell grants
    // less (its resize cap), the Viewport scrolls the remainder.
    int preferredContentHeight() const;

    // Cell geometry — the single source of truth shared by layout and the shell's
    // height math.
    static constexpr int kCellH  = 95;   // label (kLabelH) + widget body
    static constexpr int kLabelH = 16;

    // ── Test-only observables ───────────────────────────────────────────────
    // The widget-kind promise above ("toggle-kind params must render as a
    // ToggleButton, never a rotary") was closed as PF-005 in 2026-07-23 and its
    // own closing note records it as "not confirmed by eye/runtime". Nothing
    // could confirm it: the controls are private and no test had ever
    // constructed this panel. These accessors are what EditorSessionTest reads.
    // Mirrors the *ForTest convention in PluginForgeProcessor and PromptPanel.
    enum class WidgetKind
    {
        Rotary, HorizontalSlider, VerticalSlider, IncDec, Toggle, Unknown
    };

    int          controlCountForTest() const { return static_cast<int>(controls.size()); }
    WidgetKind   controlKindForTest(int index) const;
    juce::String controlLabelForTest(int index) const;
    // The widget's OWN value, not the APVTS slot's — so a test can catch an
    // attachment that silently stopped tracking its parameter.
    double       controlValueForTest(int index) const;
    static const char* widgetKindName(WidgetKind k);

private:
    // One control = its widget (Slider OR ToggleButton), a name label, and exactly
    // one attachment (the other stays null). Declaration order is deliberate: the
    // attachments come AFTER the widget so they are destroyed BEFORE it — an
    // AudioProcessorValueTreeState attachment must never outlive the widget it
    // binds (juce_AudioProcessorValueTreeState.h keeps a reference to the control).
    struct Control
    {
        std::unique_ptr<juce::Component> widget;
        std::unique_ptr<juce::Label>     label;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> sliderAtt;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> buttonAtt;
    };

    void layoutControls();

    PluginForgeProcessor& processor;

    // The grid lives inside a Viewport so N can exceed what the window shows.
    // `content` is the scrolled surface; the control widgets are ITS children.
    // Declared before `viewport` (and `controls` last) so teardown runs
    // controls → viewport → content: child widgets deregister while their parent
    // `content` is still alive, and the viewport drops its view before `content` dies.
    juce::Component      content;
    juce::Viewport       viewport;
    std::vector<Control> controls;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ParamGridPanel)
};
