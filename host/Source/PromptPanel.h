#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"
#include "GenerationProfiles.generated.h"
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <string>
#include <thread>

// ── PromptTextEditor ────────────────────────────────────────────────────────
// A multi-line prompt box with two custom key bindings:
//   • Cmd/Ctrl+Enter → submit (plain Enter inserts a newline, because the box
//                       runs with setReturnKeyStartsNewLine(true)).
//   • Up-arrow       → recall/CYCLE through prompt history (C6). Intercepted
//                       when the box is empty (first recall) OR when it is
//                       currently showing a just-recalled entry unmodified
//                       (browsingHistory, walks to the next-older entry).
//                       Otherwise Up is normal caret movement.
// Everything else falls through to juce::TextEditor's own editing. Keys verified
// against juce_KeyPress.h (returnKey:191, upKey:198, getKeyCode():109,
// getModifiers():115) and the overridable juce_TextEditor.h keyPressed (:725).
class PromptTextEditor : public juce::TextEditor
{
public:
    std::function<void()> onSubmit;          // Cmd/Ctrl+Enter
    std::function<void()> onRecallHistory;    // Up-arrow while empty

    bool keyPressed(const juce::KeyPress& key) override;

    // Set by PromptPanel right after it programmatically fills the box from
    // history (restoreFromHistory uses setText(..., dontSendNotification), so
    // this flag is untouched by that call) -- true while up-arrow should keep
    // walking to an OLDER entry instead of moving the caret. Cleared the
    // moment the user actually edits the text: unlike our own restore call,
    // TextEditor's onTextChange fires on every REAL keystroke regardless of
    // the notification flag a programmatic setText used, which is what tells
    // a genuine edit apart from a recall (C6).
    void setBrowsingHistory(bool browsing) { browsingHistory = browsing; }

    // NB: no JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR here — this class is a
    // default-constructed member (PromptPanel::promptInput), and the macro's
    // deleted copy-ctor counts as a user-declared constructor, which would
    // suppress the implicit default ctor. juce::TextEditor (via Component) is
    // already non-copyable, so nothing is lost.

private:
    bool browsingHistory = false;
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
// Threading contract (PF-006, fixed 2026-07-25 — was a detached thread):
//
//   * ONE persistent worker thread, OWNED by this panel and JOINED in the
//     destructor. Mirrors FaustEngine's compile worker (PF-003, commit d10f59e),
//     including its single-pending-slot supersede policy.
//   * A single pending job slot. A new Generate REPLACES an un-started request
//     and KILLS the in-flight subprocess, rather than stacking another detached
//     thread on top of it. The old code had no supersede at all: N rapid clicks
//     meant N detached threads, each able to call loadFaustCode() through a raw
//     processor reference, which is what produced the "Segmentation fault (core
//     dumped)" during the 2026-07-24 P6 battery (prompt #7).
//   * Every UI touch still hops to the message thread via
//     SafePointer<PromptPanel> + MessageManager::callAsync. That is still
//     required even with a joined worker: callAsync posted just before the join
//     runs AFTER ~PromptPanel returns, and SafePointer is what makes that safe.
//
// Why the raw PluginForgeProcessor& is now sound. juce_AudioProcessor.cpp:51-57
// asserts the editor is destroyed before its AudioProcessor
// (jassert(activeEditor == nullptr) in ~AudioProcessor). This panel is owned by
// the editor, and the worker cannot outlive this panel because the destructor
// joins it. So processor outlives the worker by construction — which is exactly
// what a DETACHED thread could not guarantee, since it outlived both.
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

    // Message-thread only. Hides and empties the error region. Called on every
    // submit (PF-021) so a previous failure never reads as the current result.
    void clearError();

    // Message-thread only. Enables/disables "Add" and "Redo" on refineSelector —
    // there is nothing to add to or redo from until a prior source exists. Two
    // call sites, both required: the constructor seeds this from
    // processor.currentSource() (covers a DAW project load and a reopened editor,
    // where onFaustCompileSuccess fires — if at all — before this panel exists),
    // and the shell calls it again from onFaustCompileSuccess so a same-session
    // first generation unlocks the modes without needing a reopen. Deliberately
    // NOT called from this panel's own generation-success handler: at that point
    // loadFaustCode() has only been posted, and currentFaustSource only commits
    // if the JIT compile actually succeeds (PluginProcessor.cpp) — gating here on
    // "LLM returned code" would arm Add after a patch that never went live, and
    // the next Add would send an empty prior_source.
    // If disabling while Add or Redo is selected, forces the selection back to
    // New — the closed box must never keep displaying a mode the user can no
    // longer re-pick from the dropdown.
    void setRefineModesAvailable(bool available);

    // Test-only entry point, so the PF-006 threading contract (supersede,
    // teardown-mid-flight) can be exercised headlessly without synthesising
    // keyboard/mouse events. Mirrors PluginForgeProcessor::currentSourceForTest().
    // Sets the prompt box and submits, exactly as the Generate button does.
    void submitPromptForTest(const juce::String& text);

    // Test-only. True while the worker thread exists — lets a test assert that a
    // panel which never generated also never spawned a thread.
    bool hasWorkerForTest() const { return worker.joinable(); }

    // Test-only. Current error-region text, for asserting PF-021's clear-on-submit.
    juce::String errorTextForTest() const { return errorBox.getText(); }

    // Test-only. The one status line. The shell routes compile-success and
    // output-guard-mute text through setStatus(), so this is where a test reads
    // what the user is actually being told.
    juce::String statusTextForTest() const { return statusLabel.getText(); }

    // Test-only. The Refine selector's state (New/Add/Redo), and a way to set it
    // without a click. ID 1="New", 2="Add", 3="Redo". setSelectedId does not
    // consult isItemEnabled (only the popup's own selectIfEnabled does —
    // juce_ComboBox.cpp), so this works even while setRefineModesAvailable(false)
    // has Add/Redo disabled — deliberately, so a test can drive a mode no mouse
    // click could currently reach.
    int refineModeForTest() const { return refineSelector.getSelectedId(); }
    void setRefineForTest(int id) { refineSelector.setSelectedId(id, juce::sendNotification); }

    // Test-only. Whether Add/Redo are currently selectable — i.e. whether
    // setRefineModesAvailable(true) has ever been called. Reads the widget's own
    // state rather than a bookkeeping flag, so it can't drift from what the UI
    // actually offers.
    bool refineModeAvailableForTest() const { return refineSelector.isItemEnabled(2); }

    // Test-only. True when the LAST generation attempt (successful or not — a
    // refusal is itself a failure response, generate.py's prior_source_refused)
    // was refused because the prior source overflowed the token budget in
    // surgical (Add) mode.
    bool priorSourceRefusedForTest() const { return lastPriorSourceRefused; }

    // Test-only. The kind selector's selected text (Instrument / Effect) and a way
    // to set it without a click.
    juce::String kindForTest() const { return PF_IS_SYNTH != 0 ? "Instrument" : "Effect"; }
    void setKindForTest(const juce::String& kind)
    {
        if (kind.equalsIgnoreCase(kindForTest()))
            familySelector.setSelectedId(2, juce::sendNotification);
    }
    juce::String familyForTest() const;
    void setFamilyForTest(const juce::String& family);
    juce::String providerForTest() const;
    void setProviderForTest(const juce::String& provider);
    int fallbackModeForTest() const { return fallbackSelector.getSelectedId(); }
    void setFallbackModeForTest(int id)
    {
        fallbackSelector.setSelectedId(id, juce::dontSendNotification);
    }

    // Test-only. True if the last successful generation reported that the prior
    // source was dropped due to token-budget overflow (generate.py's
    // prior_source_dropped flag, generate.py:381-386).
    bool priorSourceDroppedForTest() const { return lastPriorSourceDropped; }
    juce::String providerUsedForTest() const { return lastProviderUsed; }
    bool fallbackUsedForTest() const { return lastFallbackUsed; }

    // Test-only. Number of entries currently in the in-session history list --
    // used to check the persisted-state round trip (C6) without depending on
    // menu text formatting.
    int historyCountForTest() const { return promptHistory.size(); }

    // Test-only (C6). The prompt box's raw current text, and a way to set it
    // WITHOUT submitting -- lets a test simulate "the user is typing
    // something new" so a later recall's effect is observable against text
    // that demonstrably did not come from history. sendNotification (unlike
    // submitPromptForTest's setText) so this fires onTextChange exactly like
    // real typing would, exercising the same reset-the-walk path.
    juce::String promptTextForTest() const { return promptInput.getText(); }
    void setPromptTextForTest(const juce::String& text)
    {
        promptInput.setText(text, juce::sendNotification);
    }

    // Test-only (C6). Drives the SAME callback a real up-arrow key event
    // calls (PromptTextEditor::onRecallHistory) -- not a synthetic KeyPress,
    // since nothing on this machine can synthesize real compositor input
    // (the same limitation STATUS.md's Broken #1 names for the keyboard
    // panel). This exercises the walking-index logic onRecallHistory owns,
    // not PromptTextEditor's own decision about whether to intercept Up in
    // the first place -- that gating is as untestable here as a real
    // keypress is, for the same reason.
    void recallHistoryForTest()
    {
        if (promptInput.onRecallHistory)
            promptInput.onRecallHistory();
    }

private:
    void timerCallback() override;

    void submitPrompt();
    void startWorking();
    void stopWorking();
    void showHistoryMenu();
    void pushHistory(const juce::String& prompt);
    void restoreFromHistory(const juce::String& prompt);
    void updateAutoFamilyLabel();
    juce::String selectedFamilyId() const;

    PluginForgeProcessor& processor;

    PromptTextEditor promptInput;
    juce::TextButton  generateButton { "Generate" };
    juce::TextButton  historyButton  { "History" };
    juce::ComboBox    familySelector;
    juce::ComboBox    refineSelector;
    juce::ComboBox    providerSelector;
    juce::ComboBox    fallbackSelector;
    juce::Label       statusLabel;
    juce::Label       progressLabel;
    juce::TextEditor  errorBox;

    // ── Fresh vs Refine (PF-020's open residual) ────────────────────────────
    // LoadMode has existed in the processor since 4a84c1c and defaults correctly;
    // the user simply had no way to choose it. The ComboBox below replaces the
    // original single ToggleButton with three modes:
    //   1 = "New"   → LoadMode::Fresh   (fresh generation, no prior source)
    //   2 = "Add"   → LoadMode::Iterate (surgical: prior source is authoritative)
    //   3 = "Redo"  → LoadMode::Iterate (context: prior source is reference)
    // Both "Add" and "Redo" use Iterate mode (keep surviving param values), but
    // differ in how the LLM is framed: surgical (minimal change) vs context
    // (full regeneration with reference).

    // Resolved once in the constructor; invalid juce::File (existsAsFile()==false)
    // if generate.py could not be located.
    juce::File generateScript;

    // Session-local prompt history, most-recent-first, de-duplicated. Durable
    // history restored from the persisted blob waits on the S1 source/prompt
    // getter (FLEET req #14) — this is the in-session half only.
    juce::StringArray promptHistory;
    static constexpr int kHistoryShown = 5;   // entries offered in the menu
    static constexpr int kHistoryMax   = 20;  // entries retained in the session

    // Message-thread only (C6). -1 = not currently browsing; N = the index
    // into promptHistory the box currently shows, walked one entry OLDER by
    // each up-arrow while PromptTextEditor::browsingHistory holds. Reset to
    // -1 the moment the user edits the recalled text (promptInput.onTextChange).
    int historyBrowseIndex = -1;

    // Backstop for a WEDGED interpreter, not the normal timeout path (PF-019).
    // generate.py owns a self-enforced wall-clock budget (_DEFAULT_GENERATION_BUDGET_S
    // = 100s) sized to finish inside this cap and report a typed reason. Before
    // PF-019 both numbers were 120s, so whichever fired first was a race and the
    // host won four times running in the 2026-07-24 battery — killing the child
    // before it could say why it failed.
    //
    // Keep the invariant: this must stay comfortably ABOVE generate.py's budget
    // plus one faust compile plus interpreter startup (~117s). If you lower it,
    // lower PLUGINFORGE_GENERATION_BUDGET first.
    static constexpr int kSubprocessTimeoutMs = 180 * 1000;

    // Progress-animation state (message thread only).
    bool        isWorking   = false;
    juce::int64 workStartMs = 0;
    float       pulsePhase  = 0.0f;

    // True when the last successful generation had prior_source_dropped in the
    // response JSON (generate.py:381-386). Read by the shell to surface a
    // warning. Reset on every new generation.
    bool        lastPriorSourceDropped = false;

    // True when the last generation in surgical (Add) mode was refused because
    // the prior source exceeded the token budget (generate.py's
    // prior_source_refused flag). Read by the shell to surface an error.
    // Reset on every new generation.
    bool        lastPriorSourceRefused = false;
    juce::String lastProviderUsed;
    bool         lastFallbackUsed = false;

    // ── Generation worker (PF-006) ──────────────────────────────────────────
    // Mirrors FaustEngine's compile worker (FaustEngine.h "Compile worker",
    // commit d10f59e): persistent thread, single pending slot, joined on
    // teardown. Started lazily on first submit so a panel that never generates
    // never spawns a thread.
    void workerLoop();
    void runGeneration(const juce::String& prompt, juce::uint64 myGeneration,
                       PluginForgeProcessor::LoadMode mode,
                       const juce::String& priorSource,
                       const juce::String& kind,
                       const juce::String& family,
                       const juce::String& provider,
                       int fallbackMode);
    void shutdownWorker();

    std::thread             worker;
    std::mutex              jobMutex;
    std::condition_variable jobCv;
    juce::String            pendingPrompt;
    juce::uint64            pendingGeneration = 0;   // guarded by jobMutex
    // The load mode AS OF SUBMIT, published under jobMutex with the prompt and
    // the stamp. Deliberately NOT re-read from refineToggle on the worker: the
    // toggle is a message-thread component, reading it from the worker would be a
    // data race, and a user who ticks Refine while a run is in flight means it for
    // the NEXT run, not the one already going. Same reasoning as the `generation`
    // stamp below, which was a real race caught by the PF-006 test.
    PluginForgeProcessor::LoadMode pendingMode
        = PluginForgeProcessor::LoadMode::Fresh;      // guarded by jobMutex
    // A4: processor.currentSource() AS OF SUBMIT, read on the message thread
    // (submitPrompt) before jobMutex is taken -- currentSource() has its own
    // metaMutex and doesn't need to nest with jobMutex, so there is no reason to
    // invent a lock order. Empty when Refine is off, or on for a first
    // generation (nothing to refine yet) -- runGeneration's empty check is what
    // degrades that case to a plain --prompt request rather than sending a
    // hollow prior_source.
    juce::String            pendingPriorSource;       // guarded by jobMutex
    // Kind (instrument/effect) AS OF SUBMIT, same thread/mutex reasoning as
    // pendingPriorSource: read on message thread, published with the job.
    juce::String            pendingKind;              // guarded by jobMutex
    juce::String            pendingFamily;            // guarded by jobMutex
    juce::String            pendingProvider;          // guarded by jobMutex
    int                     pendingFallbackMode = 1;  // guarded by jobMutex
    bool                    hasJob   = false;
    bool                    stopping = false;   // guarded by jobMutex

    // The subprocess currently in flight. Registered by runGeneration for exactly
    // as long as the ChildProcess lives on the worker's stack, so submit and
    // teardown can KILL it instead of waiting out its 180s cap.
    //
    // SUBTLE: kill() across threads is safe here, and that is a property of the
    // implementation, not an assumption — juce_SharedCode_posix.h:1217
    // killProcess() is `::kill(childPID, SIGKILL)`, const and noexcept, reading
    // only a pid fixed at start(). It never touches the stream state that
    // waitForProcessToFinish is concurrently polling.
    std::mutex          childMutex;
    juce::ChildProcess* activeChild = nullptr;

    // Bumped on every submit and on teardown. A run whose stamp no longer matches
    // is superseded and publishes nothing — that is what stops a stale patch
    // overwriting a newer one.
    //
    // SUBTLE: the bump happens INSIDE jobMutex, and the resulting stamp is handed
    // to the worker through pendingGeneration rather than re-read from here at
    // pickup. Re-reading was wrong and the PF-006 test caught it: the worker could
    // wake on hasJob and load `generation` BEFORE submitPrompt's bump landed, so a
    // run compared its own pre-bump stamp against the post-bump value and
    // discarded ITSELF. Publishing the job and its stamp under one lock removes
    // the window. The atomic remains because runGeneration re-reads it to detect
    // *later* supersedes while it is running.
    std::atomic<juce::uint64> generation { 0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PromptPanel)
};
