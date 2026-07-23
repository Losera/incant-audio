#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"

// ── PromptPanel ─────────────────────────────────────────────────────────────
// The prompt-entry + generation surface: the multi-/single-line prompt box, the
// Generate button, the status line, and the whole LLM subprocess bridge
// (generate.py discovery, the detached worker thread, and its message-thread
// callbacks).
//
// This is a mechanical lift out of the old monolithic PluginForgeEditor
// (docs/FLEET.md Wave 0, Task 0): ZERO behaviour change. Session 2 owns this
// file from here (multi-line prompt, progress/attempt counter, readable
// multi-line errors, prompt history — docs/FLEET.md Wave 1).
//
// Threading contract is unchanged from the original: the Generate handler spawns
// a detached std::thread that talks to generate.py, and every UI touch hops back
// to the message thread via SafePointer<PromptPanel> + MessageManager::callAsync.
class PromptPanel : public juce::Component
{
public:
    explicit PromptPanel(PluginForgeProcessor&);

    void resized() override;

    // Message-thread only. The shell routes compile-error / compile-success /
    // output-guard-mute status text through here, since this panel owns the
    // one status label.
    void setStatus(const juce::String& text);

private:
    PluginForgeProcessor& processor;

    juce::TextEditor promptInput;
    juce::TextButton  generateButton { "Generate" };
    juce::Label       statusLabel;

    // Resolved once in the constructor; empty juce::File (existsAsFile() == false)
    // if not found.
    juce::File generateScript;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PromptPanel)
};
