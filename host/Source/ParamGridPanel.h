#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"
#include "Theme.h"
#include "UiIr.h"
#include <map>
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

    // Sectioned-layout row geometry -- shared by layoutSectioned() (which needs
    // the running total to place each control) and contentHeightForCurrentMode()
    // (which needs only the final total, for the shell's window-size request).
    // ONE constant each, not two independently-typed literals: this is the exact
    // "two copies of one layout arithmetic" shape that produced the kChromeHeight
    // defect (see PluginEditor.h) -- discovered here because wiring applyUiIr()
    // for the first time made contentHeightForCurrentMode() reachable with
    // activeSections non-empty for the first time. Before that, its sqrt-grid
    // formula was the only formula anyone had ever asked it for.
    static constexpr int kHeadingH    = 20;   // section title row
    static constexpr int kSectionGapH = 4;    // gap after a section's last row

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

    // ── UI IR rendering (ADR-024 / Phase 1a) ─────────────────────────────────
    // Applies a renderer-agnostic layout description to the grid. When a valid IR
    // is present, controls are laid out by section (grouped, with headings) rather
    // than the default sqrt-grid. Any compiled param not in the IR is appended to
    // a trailing "Parameters" section — a parameter is never invisible.
    //
    // Pass an empty/default UiIr::Layout to revert to the default grid. The IR
    // does NOT change widget types — applyPresentation still decides that from
    // the Faust Kind. The IR only controls grouping, section headings, and
    // column-span.
    //
    // Called from the shell after refreshParamKnobs, on the message thread.
    void applyUiIr(const UiIr::Layout& ir);

    // Heuristic IR derivation (ADR-022 Track 1.2): builds a UiIr::Layout purely
    // from Faust group nesting already present in `params` -- no prompt change,
    // no LLM involvement, zero headroom cost. Returns UiIr::empty() (schema 0)
    // when sectioning would not help: <=1 non-empty group, or fewer than 4
    // occupied slots. A heading over two knobs, or one section wrapping the
    // whole patch, reads worse than the flat grid it would replace.
    //
    // Sections are ordered by a canonical rank (osc -> filter -> env -> mod ->
    // fx -> out), NOT Faust's alphabetical group order (FaustEngine.h's `group`
    // doc: "Faust emits the groups ALPHABETICALLY"), because alphabetical reads
    // as Env/Filter/Fx/Osc, which is not how a musician scans a synth panel.
    // Unranked group names keep the order they were first seen in, sorted after
    // every ranked bucket.
    //
    // Pure function of `params` -- no JUCE Component state, callable from a unit
    // test without constructing a panel.
    // isInstrument added for ADR-029 §4 -- deriveComponents() below needs it,
    // and this is the one call site (PluginEditor.cpp) that already has both
    // params and processor.isInstrumentForTest() in hand.
    static UiIr::Layout deriveLayoutFromGroups(const FaustEngine::ParamList& params,
                                                bool isInstrument);

    // Heuristic per-generation accent (ADR-022 §3 / T7): a deterministic pick
    // from Theme::GeneratedAccent::swatches, keyed on the same group names
    // deriveLayoutFromGroups() reads plus the patch's instrument-vs-effect
    // status (FaustEngine::isInstrument()). Pure function of its arguments --
    // no JUCE Component state, callable from a unit test without constructing
    // a panel, same convention as deriveLayoutFromGroups above.
    //
    // Same swatch every time for the same (params, isInstrument) pair within
    // one process. WHICH of the four it lands on is not a promise -- it comes
    // from std::hash<std::string>, whose value is implementation-defined --
    // only that it is stable and always one of the four.
    static juce::Colour derivePalette(const FaustEngine::ParamList& params, bool isInstrument);

    // ADR-029 §4: which fixed UI bands this compile has -- keyboard from the
    // voice contract (the same bool that already drives PluginEditor's
    // includeKeyboard, 89268ec), sampleBrowser always true (89268ec's own
    // "unconditional by design" call, restated here rather than changed),
    // meter from whether any captured param is FaustEngine::Kind::Meter with
    // a live zone. Pure function of its arguments, same convention as
    // derivePalette/deriveLayoutFromGroups above.
    static UiIr::Components deriveComponents(const FaustEngine::ParamList& params,
                                              bool isInstrument);

    // ADR-029 §5: a short name from the SAME (params, isInstrument) hash
    // derivePalette() above already computes — formatted as a title instead of
    // thrown at a colour index. No new captured data: the accent name (Ember/
    // Amber/Rust/Coral) is the same vocabulary Theme.h's GeneratedAccent
    // swatches are already commented with, so a patch's title and its accent
    // colour always agree.
    static juce::String deriveTitle(const FaustEngine::ParamList& params, bool isInstrument);

    // Test-only: the currently active IR sections (empty if no IR applied).
    const std::vector<UiIr::Section>& activeSectionsForTest() const { return activeSections; }

    // Test-only: the currently active component descriptor (ADR-029 §4), set
    // by applyUiIr() from whatever Layout it was last given -- default-false
    // components if applyUiIr() has never run.
    UiIr::Components activeComponentsForTest() const { return activeComponents; }

    // Test-only: the palette currently applied to this compile's controls.
    juce::Colour currentPaletteForTest() const { return currentPalette; }

    // The generated title for the currently-live compile, cached alongside
    // currentPalette rather than recomputed per paint() call -- PluginEditor
    // does not retain the ParamList deriveTitle needs, and paint() may run
    // every frame.
    juce::String getGeneratedTitle() const { return currentTitle; }

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
    // ADR-029 §2/§3: the raw [style:...] value and the innermost enclosing
    // group's hgroup/vgroup/tgroup axis, read straight off the retained
    // ParamInfo the same way controlGroupForTest reads .group. Nothing renders
    // by either yet — these expose FaustEngine's capture, not a layout decision.
    juce::String controlStyleForTest(int index) const;
    juce::String controlOrientationForTest(int index) const;
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

        // The macro slot this control is attached to.
        //
        // ⚠️ NOT the control's index. It was, for the whole life of this panel,
        // because ParamPool::remap was positional -- params[i] WAS slot i, so
        // attaching control i to slotId(i) happened to be right. Identity-keyed
        // assignment breaks that: a param can reclaim any slot, so a control's
        // position in this vector says nothing about which parameter it drives.
        // Binding by index after that change attached knobs to the wrong
        // parameters, which is exactly what EditorSessionTest scenario 15 caught.
        int slot = -1;

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

    // The sectioned-layout total: sum of (heading + one row per control +
    // gap) across activeSections. Pure function of activeSections, no widget
    // measurement needed -- every control is one full-width row regardless of
    // section span (layoutSectioned() places width via span, height via row
    // count only). Called from BOTH contentHeightForCurrentMode() (the
    // shell's window-size request) and layoutSectioned() (the actual
    // placement pass), so the two can never disagree about how tall a
    // section is.
    int contentHeightForSections() const;

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

    // The Viewport's scrolled surface. A section heading must scroll WITH the
    // grid (it sits inline between rows of knobs), not stay fixed against
    // ParamGridPanel's own bounds -- so painting belongs to the scrolled
    // component, not the outer panel. `headings` is populated by
    // layoutSectioned() and consumed by paint(); paint() never re-derives the
    // geometry, which is the exact kChromeHeight drift shape PluginEditor.h
    // warns about (two independent copies of the same layout arithmetic).
    class ContentArea : public juce::Component
    {
    public:
        void paint(juce::Graphics&) override;

        struct Heading { juce::Rectangle<int> bounds; juce::String title; };
        std::vector<Heading> headings;
    };

    // The grid lives inside a Viewport so N can exceed what the window shows.
    // `content` is the scrolled surface; the control widgets are ITS children.
    // Declared before `viewport` (and `controls` last) so teardown runs
    // controls → viewport → content: child widgets deregister while their parent
    // `content` is still alive, and the viewport drops its view before `content` dies.
    ContentArea           content;
    juce::Viewport        viewport;
    std::vector<Control>  controls;

    // Message-thread only, like everything else here. See refreshCountForTest().
    int refreshCount = 0;

    // This compile's derived accent (ADR-022 §3 / T7), computed once per
    // refreshParamKnobs() call and applied to each control by
    // applyPresentation() -- see derivePalette()'s header comment. Defaults
    // to the first swatch (== Theme::accent) so a panel that has never
    // compiled anything still has a defined colour rather than black.
    juce::Colour currentPalette = Theme::GeneratedAccent::swatches[0];

    // ADR-029 §5: this compile's derived title, computed alongside
    // currentPalette from the same hash so the two always agree. Defaults to
    // the swatch-0 name, matching currentPalette's own default above.
    juce::String currentTitle = "Ember Effect";

    // The UI IR layout currently in effect (empty when none). Read by resized()
    // to decide sectioned vs default grid layout. Stored separately from the
    // controls vector so a presentation change doesn't destroy the IR.
    std::vector<UiIr::Section> activeSections;
    juce::String activeArchetype;
    juce::String activeTokens;
    UiIr::Components activeComponents;   // ADR-029 §4, set by applyUiIr() below
    void layoutSectioned();

    // Lookup from IR control label → the actual widget Control pointer.
    // Built by applyUiIr(), consumed by layoutSectioned().
    std::map<std::string, Control*> irLookup;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ParamGridPanel)
};
