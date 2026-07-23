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

private:
    // 30Hz UI tick: pulls processor.outputLevel (relaxed atomic, written on the
    // audio thread) into displayLevel with instant attack / exponential decay,
    // and repaints only the meter strip.
    void timerCallback() override;

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
