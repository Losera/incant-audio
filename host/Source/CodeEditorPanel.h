#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"

// ── CodeEditorPanel ─────────────────────────────────────────────────────────
// Placeholder created by the Task-0 split (docs/FLEET.md Wave 0) so the shell
// already instantiates all three child panels and Session 2 has a real file to
// grow into. There is NO code-view behaviour in the pre-split editor, so this
// starts empty: the shell adds it as an invisible child and does not give it
// layout space, keeping the split zero-behaviour.
//
// Session 2 owns this file from here (Wave 1): read-only Faust view → editable +
// Compile through the same loadFaustCode() path → error-line highlight. Request
// window/layout space from the shell via docs/FLEET.md when the panel needs it.
class CodeEditorPanel : public juce::Component
{
public:
    explicit CodeEditorPanel(PluginForgeProcessor&);

    void resized() override;
    void paint(juce::Graphics&) override;

private:
    PluginForgeProcessor& processor;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(CodeEditorPanel)
};
