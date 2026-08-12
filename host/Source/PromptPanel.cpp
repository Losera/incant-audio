#include "PromptPanel.h"
#include "Theme.h"
#include <thread>
#include <cmath>
#include <optional>

// ── PromptTextEditor ────────────────────────────────────────────────────────
bool PromptTextEditor::keyPressed(const juce::KeyPress& key)
{
    // Cmd/Ctrl+Enter submits. Verified: KeyPress::getKeyCode() (juce_KeyPress.h:109),
    // getModifiers() (:115), returnKey (:191); ModifierKeys::isCommandDown()
    // (juce_ModifierKeys.h:68) / isCtrlDown() (:108). Accept either so the same
    // build feels native on macOS (Cmd) and Linux/Windows (Ctrl).
    if (key.getKeyCode() == juce::KeyPress::returnKey
        && (key.getModifiers().isCommandDown() || key.getModifiers().isCtrlDown()))
    {
        if (onSubmit)
            onSubmit();
        return true;   // consumed — do NOT also insert a newline
    }

    // Up-arrow on an empty box recalls the most recent prompt (upKey :198). When
    // the box has text, Up is normal caret movement, so only intercept when empty.
    if (key.getKeyCode() == juce::KeyPress::upKey && getText().isEmpty() && onRecallHistory)
    {
        onRecallHistory();
        return true;
    }

    return juce::TextEditor::keyPressed(key);
}

// ── ADR-011 `reason` → status line ──────────────────────────────────────────
// PF-019. During the 2026-07-24 P6 battery every failure read the same to the
// user ("LLM error"), so a rate limit that would clear in ten seconds was
// indistinguishable from a wedged run. The reason field makes them distinct, and
// each string here says what to DO, not just what happened.
//
// An unknown or absent reason falls back to the generic text — a newer host must
// keep working against an older generate.py.
static juce::String statusForReason(const juce::String& reason)
{
    if (reason == "rate_limited")
        return "Rate limited by the provider — wait a moment and try again.";
    if (reason == "timeout")
        return "Timed out before a valid patch — try a simpler prompt.";
    if (reason == "invalid_faust")
        return "The model's Faust did not compile after 3 attempts (see errors below).";
    // Deliberately NOT folded into invalid_faust: a truncated program never
    // reached the compiler, the model did nothing wrong, and the useful advice is
    // "ask for something simpler" rather than "the generator produced bad Faust".
    if (reason == "truncated")
        return "The plugin was too large to finish — try asking for something simpler.";
    if (reason == "no_credentials")
        return "No API key for the selected provider (see errors below).";
    return "LLM error (see errors below).";
}

// ── PromptPanel ─────────────────────────────────────────────────────────────
PromptPanel::PromptPanel(PluginForgeProcessor& p)
    : processor(p)
{
    // Multi-line prompt. setMultiLine(true, wrap) (juce_TextEditor.h:78);
    // setReturnKeyStartsNewLine(true) (:92) so plain Enter inserts a newline and
    // Cmd/Ctrl+Enter (handled in PromptTextEditor) is the submit gesture;
    // setScrollbarsShown(true) (:156) for prompts taller than the box.
    addAndMakeVisible(promptInput);
    promptInput.setMultiLine(true, true);
    promptInput.setReturnKeyStartsNewLine(true);
    promptInput.setScrollbarsShown(true);
    promptInput.setTextToShowWhenEmpty(
        juce::String(juce::CharPointer_UTF8(
            "Describe your plugin\xe2\x80\xa6  (\xe2\x8c\x98/Ctrl+Enter to generate)")),
        juce::Colours::grey);
    promptInput.onSubmit        = [this] { submitPrompt(); };
    promptInput.onRecallHistory = [this]
    {
        if (! promptHistory.isEmpty())
            restoreFromHistory(promptHistory[0]);
    };

    // Resolution order: PLUGINFORGE_LLM_SCRIPT env override, else walk upward from
    // the binary looking for llm/generate.py. Verified against the real layouts
    // (2026-07-19): dev Standalone binary sits at
    // host/build/PluginForgeHost_artefacts/<config>/Standalone/ (repo root is 5
    // levels up) and the dev VST3 binary at
    // .../VST3/PluginForge Host.vst3/Contents/<arch>/ (repo root is 8 levels up) —
    // the old getSiblingFile("llm") guess resolved inside the build tree and never
    // matched either. An *installed* bundle (e.g. ~/.vst3) has no repo above it;
    // there the env override is the supported mechanism.
    auto envScript = juce::SystemStats::getEnvironmentVariable(
        "PLUGINFORGE_LLM_SCRIPT", "");

    if (envScript.isNotEmpty())
        generateScript = juce::File(envScript);
    else
    {
        auto dir = juce::File::getSpecialLocation(juce::File::currentExecutableFile)
                       .getParentDirectory();
        for (int depth = 0; depth < 10; ++depth)
        {
            auto candidate = dir.getChildFile("llm").getChildFile("generate.py");
            if (candidate.existsAsFile())
            {
                generateScript = candidate;
                break;
            }
            dir = dir.getParentDirectory();
        }
        // Not found: generateScript stays invalid; submitPrompt() surfaces
        // "generate.py not found" with the (empty) path in the UI.
    }

    addAndMakeVisible(generateButton);
    generateButton.onClick = [this] { submitPrompt(); };

    addAndMakeVisible(historyButton);
    historyButton.onClick = [this] { showHistoryMenu(); };

    // ── Kind selector (Instrument / Effect) ────────────────────────────────
    // ADR-011's `kind` field already exists in generate.py:331's request schema
    // and router.py's classify() — this wires it to the UI. Default matches
    // the build target so a synth-build opens with "Instrument" and vice-versa;
    // the user can override per-generation (it is NOT persisted).
    kindSelector.addItem("Effect", 1);
    kindSelector.addItem("Instrument", 2);
    {
        const int idx = PF_IS_SYNTH != 0 ? 2 : 1;
        kindSelector.setSelectedId(idx, juce::dontSendNotification);
    }
    kindSelector.setTooltip("Generation target: routes to the appropriate prompt and compiler rules.");
    addAndMakeVisible(kindSelector);

    // ── Refine selector (New / Add / Redo) ──────────────────────────────────────
    // Replaces the original single ToggleButton with three semantically distinct
    // modes. Both "Add" (surgical) and "Redo" (context) send the prior source,
    // but differ in how the LLM is framed (generate.py's refine_mode field).
    // "New" is the default: fresh generation, no prior source, LoadMode::Fresh.
    refineSelector.addItem("New", 1);
    refineSelector.addItem("Add", 2);
    refineSelector.addItem("Redo", 3);
    refineSelector.setSelectedId(1, juce::dontSendNotification); // default = New
    // Seeded from the processor's source of record, NOT hardcoded false: a DAW
    // restores state (PluginProcessor::setStateInformation) before createEditor
    // ever runs, so a project reopened with a live patch must offer Add/Redo from
    // the first frame, not just after the NEXT same-session generation. See
    // setRefineModesAvailable's header comment for the second call site
    // (onFaustCompileSuccess) this pairs with.
    setRefineModesAvailable(processor.currentSource().isNotEmpty());
    refineSelector.setTooltip(
        "New: fresh plugin - knobs reset to the new patch's own defaults.\n"
        "Add: surgical change to the current patch - preserves structure, changes only the delta.\n"
        "Redo: regenerate with the current patch as context - may restructure freely.");
    addAndMakeVisible(refineSelector);

    addAndMakeVisible(statusLabel);
    statusLabel.setText("Ready.", juce::dontSendNotification);
    statusLabel.setJustificationType(juce::Justification::centredLeft);

    // Progress line: hidden until a run starts. Its alpha is pulsed by
    // timerCallback while a subprocess is in flight.
    addChildComponent(progressLabel);
    progressLabel.setJustificationType(juce::Justification::centredLeft);
    progressLabel.setColour(juce::Label::textColourId, Theme::progress);

    // Error region: read-only (juce_TextEditor.h:130), multi-line + word-wrapped
    // (:78), with scrollbars (:156) and no caret (:140). Starts hidden; shown by
    // setError() and kept populated across a later successful compile.
    addChildComponent(errorBox);
    errorBox.setMultiLine(true, true);
    errorBox.setReadOnly(true);
    errorBox.setScrollbarsShown(true);
    errorBox.setCaretVisible(false);
    errorBox.setColour(juce::TextEditor::backgroundColourId, Theme::surface);
    errorBox.setColour(juce::TextEditor::textColourId,       Theme::danger);
    errorBox.setFont(Theme::Type::mono());
}

PromptPanel::~PromptPanel()
{
    // ORDER MATTERS. Join the generate worker FIRST (PF-006): it holds a raw
    // reference to the processor and calls loadFaustCode() through it. Until this
    // returns, `this` and every member below are still live for the worker.
    shutdownWorker();

    // juce::Timer stops itself on destruction, but be explicit — this panel is
    // owned by the shell and destroyed on the message thread.
    stopTimer();
}

void PromptPanel::shutdownWorker()
{
    {
        std::lock_guard<std::mutex> lock(jobMutex);
        if (stopping)
            return;                     // idempotent
        stopping = true;
        hasJob   = false;               // drop anything queued but not started
    }

    // Bump first, so a run that is mid-flight discards its result rather than
    // touching the processor on the way out.
    generation.fetch_add(1, std::memory_order_acq_rel);

    // Don't wait out a 180s subprocess cap on plugin teardown — the DAW is
    // closing. SIGKILL makes waitForProcessToFinish return promptly.
    {
        std::lock_guard<std::mutex> lock(childMutex);
        if (activeChild != nullptr)
            activeChild->kill();
    }

    jobCv.notify_all();

    // SUBTLE: this join runs on the message thread, and the worker posts UI
    // updates with MessageManager::callAsync — which QUEUES rather than blocks,
    // so it cannot deadlock against us here. A callSync or a MessageManagerLock
    // on the worker would deadlock; do not add one.
    if (worker.joinable())
        worker.join();
}

// ── Submit / worker thread ──────────────────────────────────────────────────
void PromptPanel::submitPrompt()
{
    auto text = promptInput.getText().trim();
    if (text.isEmpty())
        return;

    pushHistory(text);

    // PF-021: clear the previous run's error BEFORE starting this one. setError()
    // deliberately retains text across a later success so the user can scroll
    // back, but nothing ever cleared it on submit — so a prior failure sat on
    // screen through the next generation, including a successful one, and read as
    // the current result. The human reported exactly this on 2026-07-24.
    clearError();

    // Reset the dropped-source and refused-source flags on every submit, message
    // thread, so a prior run's warning never reads as this run's. The worker
    // republishes them on success.
    lastPriorSourceDropped = false;
    lastPriorSourceRefused = false;

    // Prevent double-submit. Re-enabled once the subprocess returns.
    generateButton.setEnabled(false);
    startWorking();
    statusLabel.setText("Generating...", juce::dontSendNotification);

    if (! generateScript.existsAsFile())
    {
        stopWorking();
        setError("generate.py not found at " + generateScript.getFullPathName());
        statusLabel.setText("Error: generate.py not found.", juce::dontSendNotification);
        generateButton.setEnabled(true);
        return;
    }

    // PF-006: hand the prompt to the owned worker instead of detaching a thread.
    // A run already in flight is SUPERSEDED — its subprocess is killed and its
    // result discarded — rather than left to race the new one to loadFaustCode().
    {
        std::lock_guard<std::mutex> lock(jobMutex);
        if (stopping)
            return;                     // tearing down: drop the request

        pendingPrompt = text;
        // The load mode and refine mode are read HERE, on the message thread, from
        // the ComboBox, and published with the job. Reading refineSelector on the
        // worker instead would be a data race on a Component, and would also mean a
        // selection made mid-run retroactively changed a generation the user had
        // already started.
        const int refineSel = refineSelector.getSelectedId();
        if (refineSel == 1) { // New
            pendingMode = PluginForgeProcessor::LoadMode::Fresh;
            pendingPriorSource = juce::String();
        } else { // Add (surgical) or Redo (context)
            pendingMode = PluginForgeProcessor::LoadMode::Iterate;
            pendingPriorSource = processor.currentSource().trim();
        }
        // Kind is a generation-time choice (not persisted), read on the message
        // thread at submit time alongside the other captures.
        pendingKind = kindSelector.getText().trim();
        // Stamp and job published under ONE lock — see the SUBTLE note on
        // `generation` in the header for why re-reading the atomic at pickup was
        // a race that made a run discard itself.
        pendingGeneration = generation.fetch_add(1, std::memory_order_acq_rel) + 1;
        hasJob            = true;

        if (! worker.joinable())        // started lazily, on first use
            worker = std::thread([this] { workerLoop(); });
    }

    {
        std::lock_guard<std::mutex> lock(childMutex);
        if (activeChild != nullptr)
            activeChild->kill();
    }

    jobCv.notify_one();
}

void PromptPanel::submitPromptForTest(const juce::String& text)
{
    promptInput.setText(text, juce::dontSendNotification);
    submitPrompt();
}

// ── Generation worker ───────────────────────────────────────────────────────
void PromptPanel::workerLoop()
{
    for (;;)
    {
        juce::String prompt;
        juce::uint64 myGeneration = 0;
        auto mode = PluginForgeProcessor::LoadMode::Fresh;
        juce::String priorSource;
        juce::String kind;
        {
            std::unique_lock<std::mutex> lock(jobMutex);
            jobCv.wait(lock, [this] { return hasJob || stopping; });
            if (stopping)
                return;
            prompt       = pendingPrompt;
            myGeneration = pendingGeneration;
            mode         = pendingMode;
            priorSource  = pendingPriorSource;
            kind         = pendingKind;
            hasJob       = false;
        }

        runGeneration(prompt, myGeneration, mode, priorSource, kind);
    }
}

void PromptPanel::runGeneration(const juce::String& promptText, juce::uint64 myGeneration,
                                PluginForgeProcessor::LoadMode mode,
                                const juce::String& priorSource,
                                const juce::String& kind)
{
    // Superseded, or shutting down: nothing this run produces is wanted any more.
    // Checked before touching the processor and again before publishing.
    auto stale = [this, myGeneration]
    {
        return generation.load(std::memory_order_acquire) != myGeneration;
    };

    // SUBTLE: SafePointer's underlying WeakReference is NOT atomic on the pointer
    // it guards (verified against juce_WeakReference.h: SharedPointer::owner is a
    // plain pointer, written by clearPointer() / read by get() with no atomic or
    // mutex — only the wrapper's ref-count is atomic). This capture is safe only
    // because safeThis is ever dereferenced (== nullptr, ->) inside callAsync
    // lambdas below, which run on the message thread — the same thread that runs
    // the panel's destructor (clearPointer()). The worker itself only copies
    // safeThis (ref-count-atomic-safe); it never dereferences it.
    //
    // Still required even though the worker is now joined: a callAsync posted
    // just before the join runs AFTER ~PromptPanel returns.
    juce::Component::SafePointer<PromptPanel> safeThis(this);
    auto scriptPath = generateScript.getFullPathName();

    // Interpreter discovery (ADR-011): PLUGINFORGE_PYTHON overrides for
    // installed/venv layouts where the right interpreter isn't `python3` on
    // PATH; the bare-name default relies on the OS PATH search, fine on the
    // Arch dev target.
    auto pythonExe = juce::SystemStats::getEnvironmentVariable(
        "PLUGINFORGE_PYTHON", "python3");

    // Sound because this worker is owned and joined by ~PromptPanel, and
    // juce_AudioProcessor.cpp:51-57 asserts the editor (hence this panel) is
    // destroyed before the processor. The DETACHED thread this replaced could
    // outlive both, which is what made the same expression a use-after-free.
    auto& proc = processor;

    const auto prompt = promptText.toStdString();

    {
        // A4: declared BEFORE `child` so it outlives every exit path through this
        // function — every branch below is a plain `return`, nothing skips a
        // destructor, and ~TemporaryFile deletes the file it wrote
        // (juce_TemporaryFile.h:116-122). std::optional rather than a
        // default-constructed TemporaryFile because it is only needed when
        // there is a prior source to send; the file itself isn't created until
        // written to either way (juce_TemporaryFile.h:78-83).
        std::optional<juce::TemporaryFile> requestFile;
        juce::ChildProcess child;

        // Register for the lifetime of `child` so submit/teardown can kill it.
        // Deregistered by the RAII guard below BEFORE `child` leaves scope —
        // a stale activeChild pointer would be a use-after-free of its own.
        struct ChildRegistration
        {
            PromptPanel& panel;
            ~ChildRegistration()
            {
                std::lock_guard<std::mutex> lock(panel.childMutex);
                panel.activeChild = nullptr;
            }
        } childRegistration { *this };

        {
            std::lock_guard<std::mutex> lock(childMutex);
            activeChild = &child;
        }

        // Registering BEFORE start() is deliberate and safe: ChildProcess::kill()
        // is `activeProcess == nullptr || activeProcess->killProcess()`
        // (juce_ChildProcess.cpp:39), so killing a not-yet-started child is a
        // no-op returning true. Registering after start() would leave a window in
        // which a supersede or a teardown could not reach the process.
        //
        // If a supersede/shutdown already fired, don't even start the subprocess.
        if (stale())
            return;

        // A4: refine carries the prior Faust source via a request file, not argv
        // — juce::JSON::toString gives one clean escaping layer for source that
        // can contain quotes, newlines, anything (see host/tests/FakeGenerator.h's
        // note on why a hand-assembled payload isn't safe here).
        juce::StringArray argv { pythonExe, scriptPath };
        bool usedRequestFile = false;

        // Use --request-file whenever structured fields are needed (prior_source,
        // kind, refine_mode). generate.py:331 already consumes `kind` from the
        // request; this wires it to the UI. The old --prompt path (argv fallback)
        // cannot carry structured fields, so it is now reserved for the trivial
        // case: no kind override AND no prior source AND no refine_mode override.
        const int refineSel = refineSelector.getSelectedId();
        const juce::String refineModeStr = [&] {
            switch (refineSel) {
                case 2: return juce::String("surgical"); // Add
                case 3: return juce::String("context");  // Redo
                default: return juce::String();
            }
        }();
        const bool needsRequestFile = priorSource.isNotEmpty() || kind.isNotEmpty() || refineModeStr.isNotEmpty();
        if (needsRequestFile)
        {
            requestFile.emplace(".json");
            auto* obj = new juce::DynamicObject();
            obj->setProperty("prompt", promptText);
            obj->setProperty("prior_source", priorSource);
            obj->setProperty("kind", kind);
            obj->setProperty("refine_mode", refineModeStr);
            // Forced "\n": juce_File.h:781-784's replaceWithText defaults
            // lineEndings to "\r\n", a trap host/tests/FakeGenerator.h already
            // paid for once (PromptPanelThreadingTest). generate.py's json.loads
            // doesn't care about a stray CR the way a POSIX shell does, but every
            // other write in this codebase already avoids it, for no reason to
            // be the one exception.
            usedRequestFile = requestFile->getFile().replaceWithText(
                juce::JSON::toString(juce::var(obj), /* allOnOneLine */ true),
                false, false, "\n");
        }

        if (usedRequestFile)
        {
            argv.add("--request-file");
            argv.add(requestFile->getFile().getFullPathName());
        }
        else
        {
            // Degrades the PAYLOAD only, never pendingMode (captured at submit,
            // see submitPrompt) — a first generation with Refine ticked (nothing
            // to refine yet) or a temp-file write failure both fall back to a
            // plain, from-scratch request rather than sending an empty or
            // half-written prior_source.
            argv.add("--prompt");
            argv.add(promptText);
        }

        // Verified against juce_ChildProcess.h: start(const StringArray&, int) exists
        // with this exact signature — no shell interpretation of the prompt text.
        bool started = child.start(
            argv, juce::ChildProcess::wantStdOut | juce::ChildProcess::wantStdErr);

        if (! started)
        {
            juce::MessageManager::callAsync([safeThis, pythonExe]
            {
                if (safeThis == nullptr) return;
                safeThis->stopWorking();
                safeThis->setError("Failed to start interpreter: " + pythonExe);
                safeThis->statusLabel.setText("Error: failed to start " + pythonExe + ".",
                                              juce::dontSendNotification);
                safeThis->generateButton.setEnabled(true);
            });
            return;
        }

        // Bound the wait before reading: readAllProcessOutput() alone blocks until
        // EOF, so a hung generate.py (network stall, interactive pdb, ...) would
        // wedge this thread forever with the button stuck disabled. Wait with a
        // cap, kill on expiry. SUBTLE: the child could in principle fill the 64KB
        // pipe buffer and block writing before exiting — but the payload is one
        // JSON line (small), and even then this degrades to a bounded timeout+kill,
        // never an unbounded hang.
        if (! child.waitForProcessToFinish(kSubprocessTimeoutMs))
        {
            child.kill();

            // A supersede or a teardown is the OTHER reason this wait ends early —
            // both kill the child, which makes waitForProcessToFinish return. That
            // is not a timeout and must not be reported as one.
            if (stale())
                return;

            juce::MessageManager::callAsync([safeThis]
            {
                if (safeThis == nullptr) return;
                safeThis->stopWorking();
                safeThis->setError(
                    "The generator did not exit within "
                    + juce::String(kSubprocessTimeoutMs / 1000) + "s and was killed.\n\n"
                    "This is the backstop, not the normal timeout path — generate.py "
                    "budgets itself to finish well inside this window and report why. "
                    "Reaching here means the interpreter itself is wedged: check that "
                    "PLUGINFORGE_PYTHON points at a working python3, and that "
                    "llm/generate.py is not waiting on stdin.");
                safeThis->statusLabel.setText("Error: generator wedged (killed at "
                                              + juce::String(kSubprocessTimeoutMs / 1000)
                                              + "s).", juce::dontSendNotification);
                safeThis->generateButton.setEnabled(true);
            });
            return;
        }

        const int exitCode = child.getExitCode();

        // SUBTLE: a killed child makes waitForProcessToFinish return TRUE, not
        // false — it polls isRunning(), and a SIGKILLed process is not running.
        // So the supersede/teardown path arrives HERE, on the success branch,
        // with a truncated or empty pipe. Without this check that would surface
        // as "generator produced no result", i.e. a superseded run would report a
        // spurious error over the top of the run that replaced it.
        if (stale())
            return;

        // Verified: readAllProcessOutput() blocks until EOF; stdout and stderr are
        // merged into one pipe on Linux when both want flags are set. The process
        // has exited by here, so this returns promptly. generate.py's --prompt path
        // (generate_json()) is confirmed silent on stdout, so the only way stray
        // text mixes in is a startup crash (e.g. missing ANTHROPIC_API_KEY)
        // dumping a traceback — handled below by the "no JSON in output" branch.
        auto raw = child.readAllProcessOutput();

        // generate.py --prompt outputs exactly one JSON line to stdout.
        // Find the last line that starts with '{' in case stderr text is mixed in.
        juce::String jsonLine;
        for (auto& line : juce::StringArray::fromLines(raw))
        {
            auto trimmed = line.trim();
            if (trimmed.startsWith("{"))
                jsonLine = trimmed;
        }

        if (jsonLine.isEmpty())
        {
            juce::MessageManager::callAsync([safeThis, raw, exitCode]
            {
                if (safeThis == nullptr) return;
                safeThis->stopWorking();
                safeThis->setError("No JSON in generator output (exit code " + juce::String(exitCode) + ") — full output:\n\n" + raw);
                safeThis->statusLabel.setText("Error: generator produced no result (exit " + juce::String(exitCode) + ").",
                                              juce::dontSendNotification);
                safeThis->generateButton.setEnabled(true);
            });
            return;
        }

        // Verified against juce_JSON.h / juce_Variant.h: JSON::parse(const String&)
        // returns a var; getProperty(Identifier, default) gives typed access.
        auto parsed = juce::JSON::parse(jsonLine);

        if (! parsed.isObject())
        {
            juce::MessageManager::callAsync([safeThis, raw, exitCode]
            {
                if (safeThis == nullptr) return;
                safeThis->stopWorking();
                safeThis->setError("Malformed JSON object from generator (exit code " + juce::String(exitCode) + ") — raw output:\n\n" + raw);
                safeThis->statusLabel.setText("Error: malformed generator payload.",
                                              juce::dontSendNotification);
                safeThis->generateButton.setEnabled(true);
            });
            return;
        }

        bool success = parsed.getProperty("success", false);
        auto faustCode = parsed.getProperty("faust_code", juce::String()).toString();
        auto errorMsg  = parsed.getProperty("error", juce::String()).toString();
        // ADR-011 `reason` (added with PF-019): ok | invalid_faust | timeout |
        // rate_limited | no_credentials | error. Absent on responses from an older
        // generate.py, which is why the default is empty and statusForReason()
        // falls back to the generic text rather than asserting.
        auto reason = parsed.getProperty("reason", juce::String()).toString();
        // generate.py:381-386: the user asked to carry the prior source (Refine on)
        // but the request overflowed the token budget, so this is a plain regen
        // from scratch. The shell surfaces it as a warning on the status line; the
        // user deserves to know "refine" silently became "start over". Absent on
        // older generate.py — default false, no warning.
        const bool priorSourceDropped = parsed.getProperty("prior_source_dropped", false);

        // generate.py surgical (Add) mode hard-fail (llm/generate.py's
        // _prior_source_refused_response): the prior source was too large for
        // the token budget, and — unlike priorSourceDropped's soft degrade in
        // "Redo"/legacy mode — Add mode refuses rather than silently regenerate
        // from scratch, because that would break the "minimal, surgical change"
        // contract the user picked Add for. It is a HARD ERROR: `success` is
        // false, `reason` is "error" (the ADR-011 enum stays closed; this flag is
        // the discriminator, not a new reason value), so this belongs in the
        // `! success` branch below, not the success path above.
        const bool priorSourceRefused = parsed.getProperty("prior_source_refused", false);

        if (! success || faustCode.isEmpty())
        {
            juce::MessageManager::callAsync([safeThis, errorMsg, reason, exitCode, priorSourceRefused]
            {
                if (safeThis == nullptr) return;
                safeThis->stopWorking();
                juce::String fullErr = errorMsg.isNotEmpty()
                                           ? errorMsg
                                           : juce::String("Generation failed with no error text.");
                if (exitCode != 0)
                    fullErr += "\n\n(Process exited with status code " + juce::String(exitCode) + ")";
                safeThis->setError(fullErr);
                // lastPriorSourceRefused is a message-thread member, same
                // discipline as lastPriorSourceDropped below: written here, read
                // by the shell on the same thread (priorSourceRefusedForTest()).
                safeThis->lastPriorSourceRefused = priorSourceRefused;
                // statusForReason(reason) would say "LLM error (see errors
                // below)" here -- true (reason IS "error") but useless: a
                // refusal has one specific, actionable fix, and
                // statusForReason's own header comment promises "what to DO,
                // not just what happened". reason stays "error" deliberately
                // (see the flag's declaration above), so the discriminator has
                // to be the flag, not a new reason branch inside
                // statusForReason -- that function's whole contract is reading
                // `reason` alone.
                // ASCII only -- juce::String(const char*) asserts on any byte
                // above 127 (juce_String.cpp:315; see this file's promptInput
                // setup for where that bit the project once already). Plain
                // hyphen, not an em dash.
                safeThis->statusLabel.setText(
                    priorSourceRefused
                        ? "Prior patch too large for Add - choose Redo, or simplify the patch."
                        : statusForReason(reason),
                    juce::dontSendNotification);
                safeThis->generateButton.setEnabled(true);
            });
            return;
        }

        // THE check PF-006 exists for. Last gate before touching the processor:
        // if this run has been superseded, or the panel is tearing down, do not
        // publish. A superseded run reaching here would race the run that
        // replaced it into loadFaustCode() and could install the OLDER patch.
        if (stale())
            return;

        // proc outlives this worker: the worker is joined by ~PromptPanel, and
        // juce_AudioProcessor.cpp:51-57 asserts the editor is destroyed before the
        // processor. loadFaustCode() posts the actual JIT compile to FaustEngine's
        // own background thread and returns immediately — it does not block or
        // touch the audio thread. Pass the originating prompt (FLEET req #4) so a
        // DAW-saved session persists what was asked for, not just the code.
        // `mode` is the one captured at submit (see submitPrompt), not a fresh
        // read of the toggle.
        proc.loadFaustCode(faustCode, juce::String(prompt), mode);

        // UI components must only be touched on the message thread. lastPriorSourceDropped
        // is also a message-thread member: it is written HERE, inside the callAsync, and
        // read by the shell on the same thread (priorSourceDroppedForTest()).
        juce::MessageManager::callAsync([safeThis, faustCode, priorSourceDropped]
        {
            if (safeThis == nullptr) return;
            // generateButton re-enables here, when the subprocess returns; the JIT
            // compile is still running on FaustEngine's compile thread. True ready
            // status arrives via onFaustCompileSuccess, routed to setStatus() by the
            // shell. The progress animation belongs to the subprocess phase, so it
            // stops now even though the JIT swap has not landed yet.
            safeThis->stopWorking();
            safeThis->generateButton.setEnabled(true);
            safeThis->lastPriorSourceDropped = priorSourceDropped;
            safeThis->statusLabel.setText("JIT compiling: " +
                faustCode.substring(0, 40) + "...", juce::dontSendNotification);
        });
    }   // `child` (and its registration) go out of scope here
}

// ── Status / error surfaces ─────────────────────────────────────────────────
void PromptPanel::setStatus(const juce::String& text)
{
    statusLabel.setText(text, juce::dontSendNotification);
}

void PromptPanel::setError(const juce::String& fullText)
{
    // Kept verbatim (no truncation) and retained across a later COMPILE success so
    // the user can still scroll back through the last failure. It is cleared on
    // the next SUBMIT — see clearError() and PF-021 for why that distinction is
    // the whole bug.
    errorBox.setText(fullText, juce::dontSendNotification);
    errorBox.setVisible(true);
    errorBox.moveCaretToTop(false);   // show the first line, not the tail
}

void PromptPanel::clearError()
{
    // PF-021. Retaining an error across a later success is a deliberate feature
    // (the user may still want to read it); retaining it across a NEW RUN is the
    // defect, because the stale text then reads as this run's result.
    errorBox.setText({}, juce::dontSendNotification);
    errorBox.setVisible(false);
}

void PromptPanel::setRefineModesAvailable(bool available)
{
    refineSelector.setItemEnabled(2, available);
    refineSelector.setItemEnabled(3, available);
    // The closed box must never keep DISPLAYING a mode the dropdown no longer
    // offers — force back to New. Only matters if the source that made Add/Redo
    // available disappears out from under an open editor (e.g. a source-less
    // blob restored into a live session) — not a path either call site above
    // actually drives today, but cheap enough to make correct rather than
    // merely likely-fine.
    if (! available && refineSelector.getSelectedId() != 1)
        refineSelector.setSelectedId(1, juce::dontSendNotification);
}

// ── Progress animation ──────────────────────────────────────────────────────
void PromptPanel::startWorking()
{
    isWorking   = true;
    workStartMs = juce::Time::currentTimeMillis();
    pulsePhase  = 0.0f;
    progressLabel.setVisible(true);
    startTimerHz(30);   // pulse the "Working…" label
}

void PromptPanel::stopWorking()
{
    isWorking = false;
    stopTimer();
    progressLabel.setVisible(false);
    progressLabel.setAlpha(1.0f);
}

void PromptPanel::timerCallback()
{
    if (! isWorking)
    {
        stopTimer();
        return;
    }

    // Indeterminate pulse: the one-shot subprocess (ADR-011) exposes no live
    // attempt count, so this is deliberately a breathing animation, not a real
    // 1-of-3 counter (overseer FLEET ruling #2a). Elapsed seconds give the user
    // a sense that work is still happening across the up-to-120s window.
    pulsePhase += 0.16f;   // ~1.2s period at 30 Hz
    const float alpha = 0.45f + 0.55f * (0.5f + 0.5f * std::sin(pulsePhase));
    progressLabel.setAlpha(alpha);

    const auto elapsedS = (juce::Time::currentTimeMillis() - workStartMs) / 1000;
    progressLabel.setText(
        juce::String(juce::CharPointer_UTF8("Working\xe2\x80\xa6 "))
            + juce::String(elapsedS) + "s  (auto-retries up to 3x)",
        juce::dontSendNotification);
}

// ── History ─────────────────────────────────────────────────────────────────
void PromptPanel::pushHistory(const juce::String& prompt)
{
    auto p = prompt.trim();
    if (p.isEmpty())
        return;

    // De-duplicate (juce_StringArray.h:352), then push to front (:217) so the
    // menu reads most-recent-first; cap the retained count (:346, :136).
    promptHistory.removeString(p);
    promptHistory.insert(0, p);
    while (promptHistory.size() > kHistoryMax)
        promptHistory.remove(promptHistory.size() - 1);
}

void PromptPanel::showHistoryMenu()
{
    juce::PopupMenu menu;

    if (promptHistory.isEmpty())
    {
        menu.addItem(1, "(no history yet)", false, false);   // disabled
    }
    else
    {
        const int n = juce::jmin(kHistoryShown, promptHistory.size());
        for (int i = 0; i < n; ++i)
        {
            auto entry = promptHistory[i];
            auto label = entry.length() > 60 ? entry.substring(0, 60) + "..." : entry;
            menu.addItem(i + 1, label);   // itemId is 1-based index into promptHistory
        }
    }

    // showMenuAsync callback (juce_PopupMenu.h:725); anchor to the History button
    // via Options::withTargetComponent (:486). SafePointer keeps the async callback
    // safe if the panel is torn down while the menu is open.
    juce::Component::SafePointer<PromptPanel> safeThis(this);
    menu.showMenuAsync(
        juce::PopupMenu::Options().withTargetComponent(historyButton),
        [safeThis](int result)
        {
            if (safeThis == nullptr || result <= 0)
                return;
            const int idx = result - 1;
            if (idx >= 0 && idx < safeThis->promptHistory.size())
                safeThis->restoreFromHistory(safeThis->promptHistory[idx]);
        });
}

void PromptPanel::restoreFromHistory(const juce::String& prompt)
{
    promptInput.setText(prompt, juce::dontSendNotification);
    promptInput.moveCaretToEnd();
    promptInput.grabKeyboardFocus();
}

// ── Layout ──────────────────────────────────────────────────────────────────
void PromptPanel::resized()
{
    // Bounds-robust: the panel lays its children out inside whatever rectangle the
    // shell hands it (Chrome::promptH -- 220px as of 2026-08-12, not the 108px this
    // comment named when it was written pre-split). In a shallow band the error
    // region and progress row simply collapse to zero height rather than overflow.
    // juce::Rectangle::removeFrom* clamps to the space available, so nothing
    // overruns or asserts.
    auto area = getLocalBounds();
    const int gap = 6, buttonH = 28, progressH = 18, statusH = 20;

    // Reserve the fixed control rows from the BOTTOM so the prompt + error region
    // absorb the slack and the controls stay visible even in a shallow band.
    auto statusR = area.removeFromBottom(statusH);
    auto progressR = area.removeFromBottom(progressH);
    area.removeFromBottom(gap);

    // Two rows since 2026-08-12, was one. generateButton(110) + historyButton(90)
    // + kindSelector(90) + refineSelector used to share a single buttonH row, which
    // fit the old 50/50 split's ~432px right column but not 65/35's ~232-302px one
    // (session 010 §3) -- refineSelector was dropping to 0px at the 900px default.
    // Splitting into actions (bottom) + generation options (above) fits comfortably
    // at both the default width and kMinWindowW; see PluginEditor.h's Chrome::promptH.
    auto actionRow = area.removeFromBottom(buttonH);   // generateButton, historyButton
    area.removeFromBottom(gap);
    auto genRow = area.removeFromBottom(buttonH);       // kindSelector, refineSelector
    area.removeFromBottom(gap);

    // Remaining top area splits between the prompt (min 60px per Prompt B, growing
    // to ~half the slack) and the error region (everything left over).
    const int promptH = juce::jlimit(60,
                                     juce::jmax(60, area.getHeight() - 24),
                                     juce::jmax(60, area.getHeight() / 2));
    promptInput.setBounds(area.removeFromTop(promptH));
    area.removeFromTop(gap);
    errorBox.setBounds(area);   // may be zero-height until S3 widens the band

    generateButton.setBounds(actionRow.removeFromLeft(110));
    actionRow.removeFromLeft(gap);
    historyButton.setBounds(actionRow.removeFromLeft(90));

    // Kind selector: generation-time type override (routes to the instrument or
    // effect prompt). Fixed-ish width; if the band is too narrow the ComboBox
    // collapses below its text and the user still has History + Generate.
    kindSelector.setBounds(genRow.removeFromLeft(90));
    genRow.removeFromLeft(gap);
    // Takes whatever is left rather than a fixed width, so the selector simply
    // disappears in a band too narrow for it instead of overlapping kindSelector.
    refineSelector.setBounds(genRow);

    progressLabel.setBounds(progressR);
    statusLabel.setBounds(statusR);
}
