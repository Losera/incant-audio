#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"
#include "PromptPanel.h"
#include "CodeEditorPanel.h"
#include "ParamGridPanel.h"
#include "KeyboardPanel.h"
#include "ForgeLookAndFeel.h"

// ── PluginForgeEditor ───────────────────────────────────────────────────────
// Thin top-level shell. It owns the window, the title + output level meter, and
// three child panels — PromptPanel, CodeEditorPanel (read-only Faust view) and
// ParamGridPanel — and wires the processor's compile callbacks to the panels. All
// prompt/LLM behaviour lives in PromptPanel; all knob behaviour in ParamGridPanel.
// This split was ZERO-behaviour-change vs. the pre-split monolith.
//
// (The board this was carved against, docs/FLEET.md, has since been deleted; the
// lane names S1/S2/S3 in the comments below are historical. `git log` has it.)
//
// Window sizing / top-level resized() belongs to this shell, and the code panel's
// band is part of the Chrome budget below rather than negotiated per-panel.
class PluginForgeEditor : public juce::AudioProcessorEditor,
                          private juce::Timer
{
public:
    explicit PluginForgeEditor(PluginForgeProcessor&);
    ~PluginForgeEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

    // ── Test-only surface (host/tests/EditorSessionTest.cpp) ────────────────
    // Forwarders, not accessors to the panels themselves: a test that could reach
    // `promptPanel` could also reach past what it is asserting, and the panels'
    // privacy is load-bearing (the shell owns the layout contract, the panels own
    // their widgets). Everything here is one line and message-thread only.
    //
    // These exist because nothing had ever constructed this class. PluginForgeEditor,
    // ParamGridPanel and the meter/mute edge-detect below had zero coverage;
    // PromptPanelThreadingTest drives the PANEL, never the editor that owns it.
    void         submitPromptForTest(const juce::String& text);
    juce::String statusTextForTest() const;
    juce::String errorTextForTest() const;
    int          gridControlCountForTest() const;
    int          gridRefreshCountForTest() const;
    ParamGridPanel::WidgetKind gridControlKindForTest(int i) const;
    juce::String gridControlLabelForTest(int i) const;
    juce::String gridControlGroupForTest(int i) const;
    double       gridControlValueForTest(int i) const;
    juce::String gridControlTextForTest(int i) const;
    // Drives the 30Hz meter/mute tick directly. The Timer fires on wall-clock, so a
    // test that waited for it would be timing-dependent; this makes the edge-detect
    // in timerCallback() observable without a sleep.
    void         pumpMeterTickForTest() { timerCallback(); }
    // The Fresh/Refine toggle, driven and read without a click. PromptPanel's
    // refineToggle became a 3-mode ComboBox (New=1/Add=2/Redo=3); these forwarders
    // keep the old boolean surface EditorSessionTest.cpp already depends on:
    // "on" is the closest equivalent of the old Refine-ON behavior — "Add" sends
    // the prior source, same as the toggle used to.
    void         setRefineForTest(bool on) { promptPanel.setRefineForTest(on ? 2 : 1); }
    bool         refineEnabledForTest() const { return promptPanel.refineModeForTest() != 1; }
    // The full 3-mode surface, for scenarios that need mode 3 (Redo) or to
    // assert the exact selection -- deliberately a DIFFERENT name from
    // setRefineForTest, not an int overload of it: setRefineForTest(1) and
    // setRefineForTest(true) would then mean opposite things (New vs Add) and
    // both would silently compile via bool->int promotion.
    void         setRefineModeForTest(int id) { promptPanel.setRefineForTest(id); }
    int          refineModeForTest() const { return promptPanel.refineModeForTest(); }
    // Whether Add/Redo are currently selectable in the real UI (as opposed to
    // reachable only via setRefineModeForTest, which bypasses isItemEnabled the
    // same way a test-only accessor is allowed to).
    bool         refineModeAvailableForTest() const { return promptPanel.refineModeAvailableForTest(); }
    // True when the LAST generation was refused in surgical (Add) mode because
    // the prior source overflowed the token budget (generate.py's
    // prior_source_refused, PromptPanel's failure-branch handling).
    bool         priorSourceRefusedForTest() const { return promptPanel.priorSourceRefusedForTest(); }
    // The kind selector (generation target) and the prior-source-dropped warning
    // flag (generate.py:381-386), driven and read without a click.
    juce::String kindForTest() const { return promptPanel.kindForTest(); }
    void         setKindForTest(const juce::String& kind) { promptPanel.setKindForTest(kind); }
    bool         priorSourceDroppedForTest() const { return promptPanel.priorSourceDroppedForTest(); }
    // Dev-cockpit state export — OFF by default. Nothing is written anywhere
    // until a caller (the Standalone app or the /cockpit skill) opts in via
    // setCockpitStatePath(path), which sets the path AND arms the 30Hz timer
    // export. No test currently drives this; the accessors are the seam the
    // cockpit uses to enable it, not an EditorSessionTest feature.
    void         setCockpitStatePath(const juce::String& path)
    {
        cockpitStatePath = path;
        cockpitEnabled  = !path.isEmpty();
    }
    juce::String cockpitStatePathForTest() const { return cockpitStatePath; }
    bool         cockpitEnabledForTest() const   { return cockpitEnabled; }
    // The code-view disclosure, and what the view is actually showing.
    // The style the PANEL is actually rendering, not the one the processor
    // stored — so a test can wait for the callAsync hop to have landed rather
    // than sleeping, and can catch the two drifting apart.
    juce::String controlStyleForTest() const
    {
        return ParamGridPanel::controlStyleName(paramGridPanel.controlStyle());
    }
    juce::String styleButtonTextForTest() const { return styleToggle.getButtonText(); }

    void         setCodeVisibleForTest(bool on) { setCodeViewVisible(on); }
    bool         codeVisibleForTest() const { return codeEditorPanel.isVisible(); }
    juce::String codeTextForTest() const { return codeEditorPanel.displayedSourceForTest(); }
    bool         codeIsReadOnlyForTest() const { return codeEditorPanel.isReadOnlyForTest(); }

    // ── On-screen / computer keyboard (host/Source/KeyboardPanel.*) ─────────
    // Forwarders, same rationale as the grid accessors above: the panel stays
    // private, these are message-thread-only one-liners. noteOnForTest/
    // noteOffForTest drive KeyboardPanel's OWN juce::MidiKeyboardState::
    // Listener callback -- the same one a click or a mapped computer-keypress
    // reaches -- not PluginForgeProcessor::pushKeyboardNote or NoteRing
    // directly. See KeyboardPanel.h for why that distinction matters.
    void keyboardNoteOnForTest(int note, float velocity)  { keyboardPanel.noteOnForTest(note, velocity); }
    void keyboardNoteOffForTest(int note)                 { keyboardPanel.noteOffForTest(note); }
    bool keyboardPlayableForTest() const                  { return keyboardPanel.isPlayableForTest(); }

private:
    // 30Hz UI tick: pulls processor.outputLevel (relaxed atomic, written on the
    // audio thread) into displayLevel with instant attack / exponential decay,
    // and repaints only the meter strip.
    void timerCallback() override;

    // After a compile remaps the params, grow/shrink the window to the grid's row
    // count (docs/ui_design_plan.md §3 "dynamic window height"). Clamped so the
    // window never violates setResizeLimits; past the cap the grid Viewport scrolls.
    void updateWindowSizeForParams();

    // Show/hide the read-only Faust view and re-run the window sizing, which
    // already accounts for Chrome::codeH when the panel is visible. Pushes the live
    // source in on the way up so a view revealed after a compile is not blank.
    void setCodeViewVisible(bool shouldBeVisible);

    // ── Layout budget (two-panel authoring screen) ────────────────────────────
    // Track 1.1: the window is a left preview/grid column and a right prompt column,
    // split at kLeftFraction with a dividerW gap, under a full-width title bar and
    // above a full-width keyboard band. The vertical bands below (promptH, meterH,
    // rowH) now describe the RIGHT column's fixed content, not the whole window.
    //
    // WHY A STRUCT AND NOT CONSTANTS. This used to be a list of constants plus a
    // hand-summed kChromeHeight, with a header comment insisting the two "MUST
    // match what resized() consumes". They were never linked by construction, and
    // they had already drifted. Now resized() CONSUMES this struct and the
    // rightColumnHeight()/verticalChrome() sums below READ it — one source, and a
    // band cannot be changed in one place only. The pin moved from a single
    // whole-window sum to those two, asserted at the top of resized().
    struct Chrome
    {
        int margin      = 16;   // reduced() inset, counted every edge
        int titleH      = 32;   // full-width title bar: title text (left) + the
                                // disclosure row (right) since 2026-08-12
        int dividerW    = 4;    // gap between the left and right columns

        // Right column, top to bottom (fixed content; the grid column flexes).
        // The disclosure/mode row (codeToggle, styleToggle, auditionSelector)
        // used to live here as its own rowH band; moved into the title bar
        // 2026-08-12 (see rightColumnHeight()) because 65/35 leaves the right
        // column too narrow to hold both that row and PromptPanel's own button
        // row (generateButton/historyButton/kindSelector/refineSelector) --
        // at the 900px default, refineSelector was dropping to 0px.
        int promptH     = 220;  // PromptPanel: multi-line prompt + buttons +
                                // progress + status + a scrollable error region
        int gapMeter    = 8;
        int meterH      = 14;

        // Full-width bottom bands.
        int gapKeyboard = 8;    // gap above the keyboard band
        int keyboardH   = 72;   // KeyboardPanel -- ALWAYS present (unlike codeH
                                // below, unavailability is dimming, not removal;
                                // see host/Source/KeyboardPanel.h). 64->72
                                // (docs/sessions/010 §3): reserves room for the
                                // inline octave/scale controls step 3 adds; the
                                // 8px is unfilled until that step lands.
        int gapCode     = 8;    // gap above the code band
        int codeH       = 240;  // CodeEditorPanel, a full-width band ABOVE the
                                // keyboard and only while that panel is visible.
                                // (Design note: the plan places this "in the left
                                // column"; a full-width band below the split is
                                // used instead so that revealing code always grows
                                // the window — the grow-on-show contract scenario 11
                                // pins — even when the right prompt column is the
                                // taller of the two and would otherwise absorb it.)
    };

    // The left column's share of the split region's width.
    // 0.5f -> 0.65f (docs/sessions/010 §3): the grid column was the one that will
    // hold the sectioned UiIr preview and was judged to deserve more than half.
    static constexpr float kLeftFraction = 0.65f;

    // The right column's fixed vertical content: prompt + meter. The disclosure
    // row moved to the title bar 2026-08-12 (see Chrome::promptH's comment), so
    // this dropped gapRow(10) + rowH(24) -- 276 -> 242.
    // The split region is the taller of this and the (variable) grid column.
    static constexpr int rightColumnHeight(const Chrome& c)
    {
        return c.promptH + c.gapMeter + c.meterH;
    }

    // Everything outside the split region: title bar, both margins, keyboard band.
    // The code band is added by the caller only when the panel is visible.
    static constexpr int verticalChrome(const Chrome& c)
    {
        return c.margin + c.titleH + c.gapKeyboard + c.keyboardH + c.margin;
    }

    // The two sums are pinned by a static_assert at the top of resized() — NOT here.
    // `Chrome{}` needs its default member initializers, which are not available
    // until the enclosing class is complete, so a class-scope assertion is
    // ill-formed. resized() is the right home anyway: it is the reader whose
    // agreement with these sums is the actual contract.

    static constexpr int kMinWindowH   = 400;  // matches setResizeLimits minimum
    // 800->700 (docs/sessions/010 §3): 65/35 needs less width than 50/50 did.
    // At 700 (see resized()): splitW = 700 - margin*2(32) = 668; leftW =
    // round((668 - dividerW(4)) * 0.65) = 432; rightW = 668 - 4 - 432 = 232.
    // Session 010 §3 originally claimed 400/264 (a 60/40 ratio, not 65/35) --
    // wrong arithmetic, corrected in the 2026-08-12 reconciliation note in
    // docs/sessions/010-alpha-ui-architecture.md.
    static constexpr int kMinWindowW   = 700;  // matches setResizeLimits minimum
    static constexpr int kMaxGridRows  = 6;    // rows shown before the grid scrolls

    // The band budget in force. A single instance today; step 7 makes it per-mode.
    Chrome chrome;

    // The x of the divider between the two columns, set in resized(), read in
    // paint() to draw the seam. In window coordinates.
    int dividerX = 0;

    // The title band's leftover after the disclosure controls (codeToggle,
    // styleToggle, auditionSelector — moved here 2026-08-12 from the right
    // column's own row, which the 65/35 split left too narrow for them; see
    // rightColumnHeight()'s comment) claim their space on the right. Set in
    // resized(), painted in paint() — same pattern as dividerX/meterBounds.
    juce::Rectangle<int> titleTextBounds;

    PluginForgeProcessor& processor;

    // Session 002 Part B, item B2. Declared BEFORE the child panels below so
    // that (in reverse-declaration-order C++ destruction) every panel that may
    // reference it through Component::getLookAndFeel()'s parent-chain lookup is
    // torn down first -- see ForgeLookAndFeel.h's header comment and
    // docs/sessions/002-handoff-README.md for why the ordering is load-bearing
    // (~LookAndFeel() asserts if anything still points at it).
    ForgeLookAndFeel lnf;

    // ── Child panels ─────────────────────────────────────────────────────────
    PromptPanel     promptPanel;
    CodeEditorPanel codeEditorPanel;
    ParamGridPanel  paramGridPanel;
    KeyboardPanel   keyboardPanel;

    // Disclosure for the read-only Faust view. Off by default: the code is for
    // the user who wants it, and a no-code tool must not open on a wall of DSL.
    juce::TextButton codeToggle { "Show code" };

    // Control-style selector. A view control, not a parameter — it never reaches
    // the DSP (see PluginForgeProcessor::setUiStyle). Cycles rather than opening a
    // menu: three options is below the threshold where a popup earns its click.
    juce::TextButton styleToggle { "Knobs: auto" };
    void cycleControlStyle();
    // Push a stored style name into the panel and refresh the button caption.
    // Called from the processor's onUiStyleChanged and on construction, so a
    // reopened project and a second editor both come up in the chosen style.
    void applyControlStyle(const juce::String& styleName);

    // ── Audition dropdown ──────────────────────────────────────────────────
    // Selects a reference sample to feed through the live DSP for auditioning
    // generated effects. "Off" disables audition; selecting a sample loads and
    // activates it. Samples live in artifacts/samples/ and are discovered
    // relative to the executable.
    juce::ComboBox auditionSelector { "Audition" };
    void auditionSampleChanged();

    // ── Level meter (shell-owned) ────────────────────────────────────────────
    juce::Rectangle<int> meterBounds;      // set in resized(), painted in paint()
    float displayLevel = 0.0f;             // message-thread only

    // Edge-detect for the output guard's latched mute, polled by timerCallback.
    // Stored so the status label is written only on a transition -- rewriting it
    // at 30Hz would stomp compile/error messages every frame.
    bool wasOutputMuted = false;           // message-thread only

    // Dev-cockpit mirror state. Written from the message-thread timer every third
    // tick (~10Hz), never from the audio thread — and only while ARMED: a
    // non-empty path set through setCockpitStatePath(). Default is OFF (empty
    // path, enabled=false), so tests and headless builds never write /tmp state
    // unless a caller opted in. Singleton path by design: the cockpit is a
    // development mirror for one running Standalone instance.
    juce::String cockpitStatePath;   // empty until setCockpitStatePath() arms it
    bool         cockpitEnabled = false;
    int          cockpitStateTick = 0;
    void writeCockpitState();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginForgeEditor)
};
