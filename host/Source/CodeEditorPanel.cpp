#include "CodeEditorPanel.h"

CodeEditorPanel::CodeEditorPanel(PluginForgeProcessor& p)
    : processor(p)
{
    // Intentionally empty. See the header: no code-view behaviour exists in the
    // pre-split editor, so there is nothing to lift here. Session 2 fills this in.
    juce::ignoreUnused(processor);
}

void CodeEditorPanel::resized() {}

void CodeEditorPanel::paint(juce::Graphics&) {}
