#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"
#include "PromptPanel.h"
#include "CodeEditorPanel.h"
#include "ParamGridPanel.h"

// ── PluginForgeEditor ───────────────────────────────────────────────────────
// Thin top-level shell (docs/FLEET.md Wave 0, Task 0). It owns the window, the
// title + output level meter, and three child panels — PromptPanel (S2),
// CodeEditorPanel (S2, placeholder), ParamGridPanel (S3) — and wires the
// processor's compile callbacks to the panels. All prompt/LLM behaviour lives in
// PromptPanel; all knob behaviour in ParamGridPanel. This split is
// ZERO-behaviour-change vs. the pre-split monolith.
//
// Window sizing / top-level resized() is S3-owned; S2 requests layout space for
// the code editor via docs/FLEET.md.
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
    ParamGridPanel::WidgetKind gridControlKindForTest(int i) const;
    juce::String gridControlLabelForTest(int i) const;
    double       gridControlValueForTest(int i) const;
    // Drives the 30Hz meter/mute tick directly. The Timer fires on wall-clock, so a
    // test that waited for it would be timing-dependent; this makes the edge-detect
    // in timerCallback() observable without a sleep.
    void         pumpMeterTickForTest() { timerCallback(); }
    // The Fresh/Refine toggle, driven and read without a click.
    void         setRefineForTest(bool on) { promptPanel.setRefineForTest(on); }
    bool         refineEnabledForTest() const { return promptPanel.refineEnabledForTest(); }

private:
    // 30Hz UI tick: pulls processor.outputLevel (relaxed atomic, written on the
    // audio thread) into displayLevel with instant attack / exponential decay,
    // and repaints only the meter strip.
    void timerCallback() override;

    // After a compile remaps the params, grow/shrink the window to the grid's row
    // count (docs/ui_design_plan.md §3 "dynamic window height"). Clamped so the
    // window never violates setResizeLimits; past the cap the grid Viewport scrolls.
    void updateWindowSizeForParams();

    // ── Layout budget (docs/FLEET.md req #17; posted to S2) ──────────────────
    // PromptPanel band: fixed, generous enough for S2's stacked contents
    // (multi-line prompt + button/History row + progress + status + a scrollable
    // error region). CodeEditorPanel band: reserved at the BOTTOM only while that
    // panel is visible (it starts hidden), so today the grid keeps the full
    // remainder. These MUST match what resized() consumes.
    static constexpr int kPromptBandH = 220;
    static constexpr int kCodeBandH   = 240;

    // Non-grid vertical chrome in window px when the code panel is hidden:
    // top margin 16 + title 36 + prompt 220 + gap 8 + meter 14 + gap 10 + bottom
    // margin 16 = 320. (When the code panel is visible, add kCodeBandH.)
    static constexpr int kChromeHeight = 16 + 36 + kPromptBandH + 8 + 14 + 10 + 16; // 320
    static constexpr int kMinWindowH   = 400;  // matches setResizeLimits minimum
    static constexpr int kMaxGridRows  = 6;    // rows shown before the grid scrolls

    PluginForgeProcessor& processor;

    // ── Child panels ─────────────────────────────────────────────────────────
    PromptPanel     promptPanel;
    CodeEditorPanel codeEditorPanel;
    ParamGridPanel  paramGridPanel;

    // ── Level meter (shell-owned) ────────────────────────────────────────────
    juce::Rectangle<int> meterBounds;      // set in resized(), painted in paint()
    float displayLevel = 0.0f;             // message-thread only

    // Edge-detect for the output guard's latched mute, polled by timerCallback.
    // Stored so the status label is written only on a transition -- rewriting it
    // at 30Hz would stomp compile/error messages every frame.
    bool wasOutputMuted = false;           // message-thread only

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginForgeEditor)
};
