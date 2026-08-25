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
//
// PRIVATE juce::Timer: the Copy button's "Copied!" confirmation (T5) is a
// one-shot self-reverting timer, owned by this panel rather than a detached
// juce::Timer::callAfterDelay -- callAfterDelay's pending closure lives in
// JUCE's own global timer queue independent of ANY component's lifetime, so
// using it here would need a SafePointer guard against outliving this panel,
// AND would leave a real (if safely-inert) pending callback in that global
// queue for up to 900ms after this panel is destroyed. A Timer member has
// neither problem: juce_Timer.cpp's own ~Timer() calls stopTimer()
// unconditionally on destruction -- "add a call to stopTimer() to the
// destructor of your class which inherits from Timer" is its own header's
// literal advice, and inheriting Timer gets that for free.
class CodeEditorPanel : public juce::Component,
                        private juce::Timer
{
public:
    explicit CodeEditorPanel(PluginForgeProcessor&);
    ~CodeEditorPanel() override;

    void resized() override;
    void paint(juce::Graphics&) override;

    // Re-applies header/editor fonts once this panel is actually attached
    // under PluginForgeEditor. See ForgeLookAndFeel.h's resolveThemeFont()
    // header comment: the constructor's own setFont() calls run before that
    // attachment exists (PluginForgeEditor's member-initialiser list, before
    // addAndMakeVisible), so they resolve against the wrong LookAndFeel.
    // JUCE calls this automatically the moment addChildComponent() sets
    // parentComponent (juce_Component.cpp:1166-1187).
    void parentHierarchyChanged() override;

    // Message-thread only. Replaces the displayed source. Called by the shell
    // after a successful compile, and on becoming visible, so a panel opened
    // after the fact still shows the live patch rather than staying blank.
    void showSource(const juce::String& faustSource);

    // Message-thread only (C6). Selects and scrolls to a 1-based Faust source
    // line -- Faust's own diagnostic numbering (createDSPFactoryFromString's
    // errors read "dsp:<line> : ERROR : ...", FaustEngine.cpp:799's literal
    // "dsp" filename), converted to CodeDocument's 0-based lines internally.
    // A line <= 0 (no line info in this particular error) or a document with
    // no lines is a no-op, not a clamp to line 1 -- clamping would silently
    // point at the wrong line rather than pointing at nothing. Uses the
    // editor's own text-selection highlight rather than custom paint code:
    // proportionate to "point at the line", not a new rendering feature.
    void highlightErrorLine(int faustLineNumber);

    // T5: before this, the only way to get the source out was selecting it
    // by hand -- workable for a short patch, tedious for a 40-param one, and
    // this is exactly the export path the header comment above names for the
    // BYO-LLM flow (source + stderr pasted into someone else's model). One
    // click, whole document.
    void copySource();

    // Test-only. What is actually on screen.
    juce::String displayedSourceForTest() const { return document.getAllContent(); }
    bool         isReadOnlyForTest() const { return editor.isReadOnly(); }
    // T5: read-only means copy/paste is the only way out of this view (the
    // BYO-LLM flow the header comment above names) -- there was no copy
    // affordance at all before this. triggerClick() fires the SAME onClick
    // JUCE would fire from a real mouse click (juce_Button.h), so this is not
    // a bypass of the button, it is how a headless test presses it.
    void         clickCopyButtonForTest() { copyButton.triggerClick(); }
    juce::String copyButtonTextForTest() const { return copyButton.getButtonText(); }
    // Test-only. The 1-based Faust line last passed to highlightErrorLine(),
    // or 0 if it has never been called (or was called with no line info).
    // Tracked explicitly rather than inferred from getSelectionStart(), which
    // defaults to line 0 (Faust line 1) even when nothing has been
    // highlighted -- that would be indistinguishable from a real error on
    // line 1.
    int          highlightedLineForTest() const { return lastHighlightedLine; }

private:
    // One-shot: reverts the button caption and stops itself. See the class
    // header comment for why this owns a Timer instead of using
    // juce::Timer::callAfterDelay.
    void timerCallback() override;

    // Called from parentHierarchyChanged() below, once this panel is
    // actually attached under PluginForgeEditor -- see that override's
    // comment for why resolving any earlier would be both wrong and noisy.
    void applyFonts();

    PluginForgeProcessor& processor;

    // Declaration order matters: CodeEditorComponent holds a reference to the
    // document and does NOT own it (juce_CodeEditorComponent.h:50-51), so the
    // document must outlive the editor — i.e. be declared before it, and
    // therefore destroyed after it.
    juce::CodeDocument       document;
    juce::CodeEditorComponent editor { document, nullptr };
    juce::Label              header;
    juce::TextButton         copyButton { "Copy" };

    int lastHighlightedLine = 0;   // see highlightedLineForTest()

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(CodeEditorPanel)
};
