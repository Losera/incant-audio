#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"

// ── PromptTextEditor ────────────────────────────────────────────────────────
// A multi-line prompt box with two custom key bindings:
//   • Cmd/Ctrl+Enter  → submit (plain Enter inserts a newline, because the box
//                        runs with setReturnKeyStartsNewLine(true)).
//   • Up-arrow on an  → recall the most recent prompt from history.
//     empty box
// Everything else falls through to juce::TextEditor's own editing. Keys verified
// against juce_KeyPress.h (returnKey:191, upKey:198, getKeyCode():109,
// getModifiers():115) and the overridable juce_TextEditor.h keyPressed (:725).
class PromptTextEditor : public juce::TextEditor
{
public:
    std::function<void()> onSubmit;          // Cmd/Ctrl+Enter
    std::function<void()> onRecallHistory;    // Up-arrow while empty

    bool keyPressed(const juce::KeyPress& key) override;

    // NB: no JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR here — this class is a
    // default-constructed member (PromptPanel::promptInput), and the macro's
    // deleted copy-ctor counts as a user-declared constructor, which would
    // suppress the implicit default ctor. juce::TextEditor (via Component) is
    // already non-copyable, so nothing is lost.
};

// ── PromptPanel ─────────────────────────────────────────────────────────────
// The prompt-entry + generation surface (docs/FLEET.md Wave 1, S2). Owns:
//   • a multi-line prompt box (Cmd/Ctrl+Enter to generate)
//   • the Generate button + the whole generate.py subprocess bridge
//   • a History button (session-local recall of the last prompts)
//   • an indeterminate "Working…" progress animation (juce::Timer @ 30 Hz),
//     shown only while the subprocess is running (overseer FLEET ruling #2a:
//     the one-shot subprocess gives no live attempt count, so this is a pulse,
//     not a real 1-of-3 counter)
//   • a scrollable, read-only error region that keeps the LAST run's stderr /
//     compile error even after a later successful compile, so the user can
//     still read it.
//
// Threading contract (unchanged from the pre-split monolith): the Generate
// handler spawns a detached std::thread that talks to generate.py, and every UI
// touch hops back to the message thread via SafePointer<PromptPanel> +
// MessageManager::callAsync.
//
// KNOWN DEFECT LEFT IN PLACE: the detached worker is the PF-006 shutdown UAF
// (docs/BUGS.md, FLEET req #10/#16). Ownership of that Tier-2 fix is still being
// routed (S3 has a next-work plan for it, commit 137c2bf); this change is the UI
// rework only and deliberately does NOT restructure the thread, so the two edits
// don't collide. The TODO: VERIFY marker at the call site stays until PF-006 lands.
class PromptPanel : public juce::Component,
                    private juce::Timer
{
public:
    explicit PromptPanel(PluginForgeProcessor&);
    ~PromptPanel() override;

    void resized() override;

    // Message-thread only. The shell routes compile-success / output-guard-mute
    // status text through here, since this panel owns the one status line.
    void setStatus(const juce::String& text);

    // Message-thread only. Full, UNTRUNCATED error text for the scrollable error
    // region; it persists across a later successful compile so the user can still
    // review it. Public so the shell can route Faust-compiler stderr here instead
    // of truncating into the status label — see FLEET req (shell-side wire, S3);
    // the panel's own subprocess-error paths already call it.
    void setError(const juce::String& fullText);

private:
    void timerCallback() override;

    void submitPrompt();
    void startWorking();
    void stopWorking();
    void showHistoryMenu();
    void pushHistory(const juce::String& prompt);
    void restoreFromHistory(const juce::String& prompt);

    PluginForgeProcessor& processor;

    PromptTextEditor promptInput;
    juce::TextButton  generateButton { "Generate" };
    juce::TextButton  historyButton  { "History" };
    juce::Label       statusLabel;
    juce::Label       progressLabel;
    juce::TextEditor  errorBox;

    // Resolved once in the constructor; invalid juce::File (existsAsFile()==false)
    // if generate.py could not be located.
    juce::File generateScript;

    // Session-local prompt history, most-recent-first, de-duplicated. Durable
    // history restored from the persisted blob waits on the S1 source/prompt
    // getter (FLEET req #14) — this is the in-session half only.
    juce::StringArray promptHistory;
    static constexpr int kHistoryShown = 5;   // entries offered in the menu
    static constexpr int kHistoryMax   = 20;  // entries retained in the session

    // Progress-animation state (message thread only).
    bool        isWorking   = false;
    juce::int64 workStartMs = 0;
    float       pulsePhase  = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PromptPanel)
};
