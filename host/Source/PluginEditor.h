#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"
#include "PromptPanel.h"
#include "CodeEditorPanel.h"
#include "ParamGridPanel.h"
#include "KeyboardPanel.h"

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
    // The Fresh/Refine toggle, driven and read without a click.
    void         setRefineForTest(bool on) { promptPanel.setRefineForTest(on); }
    bool         refineEnabledForTest() const { return promptPanel.refineEnabledForTest(); }
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

    // ── Layout budget ────────────────────────────────────────────────────────
    // Every non-grid vertical band, in the order resized() carves them. The grid
    // gets whatever is left.
    //
    // WHY A STRUCT AND NOT CONSTANTS. This used to be a list of constants plus a
    // hand-summed kChromeHeight, with a header comment insisting the two "MUST
    // match what resized() consumes". They were never linked by construction, and
    // they had already drifted: resized() carved the disclosure row with a bare
    // literal 24 while kCodeToggleRowH sat unread beside the sum. Now resized()
    // CONSUMES this struct and chromeHeight() SUMS it — one source, two readers,
    // and a band cannot be changed in one place only.
    struct Chrome
    {
        int margin     = 16;   // reduced() inset, counted top and bottom
        int titleH     = 36;   // title spacer (the title is painted full-width)
        int promptH    = 220;  // PromptPanel: multi-line prompt + buttons +
                               // progress + status + a scrollable error region
        int gapMeter   = 8;
        int meterH     = 14;
        int gapRow     = 10;
        int rowH       = 24;   // the disclosure / mode row
        int gapGrid    = 6;
        int gapKeyboard = 8;   // gap above the keyboard band
        int keyboardH   = 64;  // KeyboardPanel -- ALWAYS present (unlike codeH
                               // below, unavailability is dimming, not removal;
                               // see host/Source/KeyboardPanel.h)
        int codeH      = 240;  // CodeEditorPanel, reserved at the BOTTOM and only
                               // while that panel is visible
    };

    // Non-grid chrome in window px, EXCLUDING the code band (which is added by the
    // caller only when the panel is visible). 350 for the Console values above.
    static constexpr int chromeHeight(const Chrome& c)
    {
        return c.margin + c.titleH + c.promptH + c.gapMeter + c.meterH
             + c.gapRow + c.rowH + c.gapGrid + c.gapKeyboard + c.keyboardH + c.margin;
    }

    // The sum is pinned by a static_assert at the top of resized() — NOT here.
    // `Chrome{}` needs its default member initializers, which are not available
    // until the enclosing class is complete, so a class-scope assertion is
    // ill-formed ("default member initializer required before the end of its
    // enclosing class"). resized() is the right home anyway: it is the reader whose
    // agreement with this sum is the actual contract.

    static constexpr int kMinWindowH   = 400;  // matches setResizeLimits minimum
    static constexpr int kMaxGridRows  = 6;    // rows shown before the grid scrolls

    // The band budget in force. A single instance today; step 7 makes it per-mode.
    Chrome chrome;

    PluginForgeProcessor& processor;

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

    // ── Level meter (shell-owned) ────────────────────────────────────────────
    juce::Rectangle<int> meterBounds;      // set in resized(), painted in paint()
    float displayLevel = 0.0f;             // message-thread only

    // Edge-detect for the output guard's latched mute, polled by timerCallback.
    // Stored so the status label is written only on a transition -- rewriting it
    // at 30Hz would stomp compile/error messages every frame.
    bool wasOutputMuted = false;           // message-thread only

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginForgeEditor)
};
