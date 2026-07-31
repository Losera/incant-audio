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
//       anything else → rotary — but that arm is UNREACHABLE today: Kind has
//       exactly five values and applyPresentation() handles all five, so no
//       generated plugin has ever shown a rotary. docs/ui_design_plan.md §3
//       describes rotary as the live fallback; it is dead code until a
//       presentation deliberately selects it;
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

    // Rebuild the control surface for a freshly compiled patch: (re)create one
    // kind-appropriate widget + attachment per param.
    //
    // It does NOT seed slot values. It used to, and that was PF-033 (`81fc75b`):
    // seeding here overwrote every restored value on a project reopen.
    // PluginProcessor::resetMappedSlotsToDefaults() owns seeding now, and only it.
    // The long note at the top of refreshParamKnobs() has the full argument.
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

    // ── Control style (purely visual) ───────────────────────────────────────
    // Which widget a CONTINUOUS parameter renders as. This is a view choice and
    // touches nothing below the editor: no DSP, no zone write, no parameter
    // declaration. The Slider and its SliderAttachment are created once per
    // compile in refreshParamKnobs and stay alive across every style change --
    // setControlStyle only restyles and relayouts them. Rebuilding attachments
    // on a style flip would thrash parameter values and flicker.
    //
    // Toggle-kind params (Faust Button / CheckButton) are DELIBERATELY EXEMPT and
    // stay ToggleButtons in every style. PF-005's promise is that a boolean never
    // renders as a continuous control; making it style-dependent would turn a
    // structural guarantee into a conditional one.
    enum class ControlStyle
    {
        Faithful,    // widget follows the Faust Kind: hslider→horizontal,
                     // vslider→vertical, nentry→inc/dec. The shipped behaviour.
        Rotary,      // every continuous param is a rotary
        Horizontal   // every continuous param is a horizontal slider
    };

    // Restyle + relayout in place. Cheap and idempotent; safe to call when the
    // style has not actually changed (it early-outs).
    void setControlStyle(ControlStyle s);
    ControlStyle controlStyle() const { return style; }

    static const char* controlStyleName(ControlStyle s);
    // Parses controlStyleName()'s output back. Unknown text yields Faithful, so a
    // corrupt or future state blob degrades to the shipped layout rather than
    // refusing to open.
    static ControlStyle controlStyleFromName(const juce::String& name);

    int          controlCountForTest() const { return static_cast<int>(controls.size()); }
    // Bumped once per refreshParamKnobs. A test cannot reliably wait for "the
    // grid caught up" by watching the control COUNT, because loading a 1-param
    // patch over a 1-param patch never changes it — the wait then resolves on the
    // compile thread's source assignment, before the callAsync hop that rebuilds
    // the widgets, and the test reads the PREVIOUS patch's labels. That raced
    // green on a dev box and failed on a CI runner. This counter is the
    // unambiguous signal.
    int          refreshCountForTest() const { return refreshCount; }
    WidgetKind   controlKindForTest(int index) const;
    juce::String controlLabelForTest(int index) const;
    // The enclosing hgroup/vgroup path Faust reported, slash-joined and
    // outermost-first; empty for a param declared outside any group. Read
    // straight off the retained ParamInfo, so it is what FaustEngine captured
    // rather than anything this panel derived. Nothing lays out by it yet --
    // it is the input a sectioned surface (demo Variant C) needs.
    juce::String controlGroupForTest(int index) const;
    // The widget's OWN value, not the APVTS slot's — so a test can catch an
    // attachment that silently stopped tracking its parameter.
    double       controlValueForTest(int index) const;
    // What the user actually READS: the text box's rendering of the current
    // value, pulled through the slider's own textFromValueFunction rather than
    // recomputed. Recomputing it here would agree with applyPresentation() by
    // construction and prove nothing — the claim under test is precisely that
    // the panel's lambda, not JUCE's default, is the one installed (PF-037).
    juce::String controlTextForTest(int index) const;
    static const char* widgetKindName(WidgetKind k);

private:
    // One control = its widget (Slider OR ToggleButton), a name label, and exactly
    // one attachment (the other stays null). Declaration order is deliberate: the
    // attachments come AFTER the label and the widget so they are destroyed BEFORE
    // them — an AudioProcessorValueTreeState attachment must never outlive the
    // widget it binds (juce_AudioProcessorValueTreeState.h keeps a reference to the
    // control). `meta` is first so it is destroyed last and can never sit between a
    // widget and its attachment.
    struct Control
    {
        // The ParamInfo this control was built from, kept so a presentation change
        // can restyle without a rebuild (and so the readout can name its own unit).
        //
        // ⚠️ `meta.zone` is NULLED at copy time in refreshParamKnobs. The pointer is
        // only valid while the DSP instance that produced it is alive
        // (FaustEngine.h:59-64), and this copy outlives it. Nulling turns a silent
        // use-after-free into a null deref.
        FaustEngine::ParamInfo meta;

        std::unique_ptr<juce::Component> widget;
        std::unique_ptr<juce::Label>     label;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> sliderAtt;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> buttonAtt;
    };

    void layoutControls();

    // Apply the CURRENT presentation's styling to one control. Owns every styling
    // decision; refreshParamKnobs owns only the widget TYPE choice.
    //
    // This early-returns unless the widget is a juce::Slider, which is what makes
    // "a toggle-kind param never renders as a rotary" structural rather than
    // conditional: a ToggleButton is unreachable from the rotary code path, so no
    // later edit to a boolean can undo the promise.
    void applyPresentation(Control& c);

    // The single height computation, shared by layoutControls() and
    // preferredContentHeight(). Two independent versions of this is the same defect
    // shape as the old kChromeHeight (see PluginEditor.h) — do not split it.
    int contentHeightForCurrentMode() const;

    PluginForgeProcessor& processor;

    // Current view style. Not persisted here — PluginForgeProcessor owns the
    // stored value (it rides the state blob alongside faustSource/prompt) and
    // pushes it in. The panel is the renderer, not the record.
    ControlStyle style = ControlStyle::Faithful;

    // Row height for the Horizontal style, which lays one full-width control per
    // row instead of a grid cell. Deliberately much shorter than kCellH: a
    // horizontal slider needs width, not height, and stacking them at 95px
    // wastes most of the window.
    static constexpr int kRowH = 34;

    // The grid lives inside a Viewport so N can exceed what the window shows.
    // `content` is the scrolled surface; the control widgets are ITS children.
    // Declared before `viewport` (and `controls` last) so teardown runs
    // controls → viewport → content: child widgets deregister while their parent
    // `content` is still alive, and the viewport drops its view before `content` dies.
    juce::Component      content;
    juce::Viewport       viewport;
    std::vector<Control> controls;

    // Message-thread only, like everything else here. See refreshCountForTest().
    int refreshCount = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ParamGridPanel)
};
