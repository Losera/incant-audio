#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_extra/juce_gui_extra.h>
#include "PluginProcessor.h"

// ── CodeEditorPanel ─────────────────────────────────────────────────────────
// Read-only view of the Faust source currently live (docs/ux_roadmap.md Phase
// 3a). Created as an empty placeholder by the Task-0 split and left that way —
// an invisible child with zero layout space — while the roadmap that described
// it moved on. It shows the source now.
//
// WHY READ-ONLY, FOR NOW. Phase 3a is deliberately the view alone; 3b is
// editable + a Compile button through the same loadFaustCode() path, and 3c is
// error-line highlighting. Shipping the view first means the user can see and
// copy what the model wrote — which is most of the value, and all of the value
// for the BYO-LLM flow where the source and the compiler's stderr get pasted
// into someone else's model. setReadOnly(true) is enforced in the constructor
// (juce_CodeEditorComponent.h:237); nothing here writes back to the processor.
//
// NO FAUST TOKENISER. JUCE ships exactly three (C++, Lua, XML —
// juce_gui_extra/code_editor/), none of them Faust, and
// CodeEditorComponent takes a nullptr tokeniser (juce_CodeEditorComponent.h:55)
// which renders monochrome. That is what this uses. A hand-written
// Faust-lite tokeniser (keywords, comments, strings) is the roadmap's follow-on,
// and passing C++'s would be worse than none: it would confidently colour the
// wrong things, and Faust's `:` `<:` `:>` `~` operators are exactly where a
// reader most needs to trust what they see.
//
// Message-thread only. The shell pushes new source in from inside its
// onFaustCompileSuccess callAsync hop.
class CodeEditorPanel : public juce::Component
{
public:
    explicit CodeEditorPanel(PluginForgeProcessor&);
    ~CodeEditorPanel() override;

    void resized() override;
    void paint(juce::Graphics&) override;

    // Message-thread only. Replaces the displayed source. Called by the shell
    // after a successful compile, and on becoming visible, so a panel opened
    // after the fact still shows the live patch rather than staying blank.
    void showSource(const juce::String& faustSource);

    // Test-only. What is actually on screen.
    juce::String displayedSourceForTest() const { return document.getAllContent(); }
    bool         isReadOnlyForTest() const { return editor.isReadOnly(); }

private:
    PluginForgeProcessor& processor;

    // Declaration order matters: CodeEditorComponent holds a reference to the
    // document and does NOT own it (juce_CodeEditorComponent.h:50-51), so the
    // document must outlive the editor — i.e. be declared before it, and
    // therefore destroyed after it.
    juce::CodeDocument       document;
    juce::CodeEditorComponent editor { document, nullptr };
    juce::Label              header;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(CodeEditorPanel)
};
