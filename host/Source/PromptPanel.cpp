#include "PromptPanel.h"
#include <thread>
#include <cmath>

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

    addAndMakeVisible(statusLabel);
    statusLabel.setText("Ready.", juce::dontSendNotification);
    statusLabel.setJustificationType(juce::Justification::centredLeft);

    // Progress line: hidden until a run starts. Its alpha is pulsed by
    // timerCallback while a subprocess is in flight.
    addChildComponent(progressLabel);
    progressLabel.setJustificationType(juce::Justification::centredLeft);
    progressLabel.setColour(juce::Label::textColourId, juce::Colour(0xfff9e2af)); // catppuccin yellow

    // Error region: read-only (juce_TextEditor.h:130), multi-line + word-wrapped
    // (:78), with scrollbars (:156) and no caret (:140). Starts hidden; shown by
    // setError() and kept populated across a later successful compile.
    addChildComponent(errorBox);
    errorBox.setMultiLine(true, true);
    errorBox.setReadOnly(true);
    errorBox.setScrollbarsShown(true);
    errorBox.setCaretVisible(false);
    errorBox.setColour(juce::TextEditor::backgroundColourId, juce::Colour(0xff181825));
    errorBox.setColour(juce::TextEditor::textColourId,       juce::Colour(0xfff2c9d3));
    errorBox.setFont(juce::Font(juce::Font::getDefaultMonospacedFontName(), 12.0f,
                                juce::Font::plain));
}

PromptPanel::~PromptPanel()
{
    // juce::Timer stops itself on destruction, but be explicit — this panel is
    // owned by the shell and destroyed on the message thread.
    stopTimer();
}

// ── Submit / worker thread ──────────────────────────────────────────────────
void PromptPanel::submitPrompt()
{
    auto text = promptInput.getText().trim();
    if (text.isEmpty())
        return;

    pushHistory(text);

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

    // SUBTLE: SafePointer's underlying WeakReference is NOT atomic on the pointer
    // it guards (verified against juce_WeakReference.h: SharedPointer::owner is a
    // plain pointer, written by clearPointer() / read by get() with no atomic or
    // mutex — only the wrapper's ref-count is atomic). This capture is safe only
    // because safeThis is ever dereferenced (== nullptr, ->) inside callAsync
    // lambdas below, which run on the message thread — the same thread that runs
    // the panel's destructor (clearPointer()). The background std::thread itself
    // only copies safeThis (ref-count-atomic-safe); it never dereferences it.
    juce::Component::SafePointer<PromptPanel> safeThis(this);
    auto scriptPath = generateScript.getFullPathName();

    // Interpreter discovery (ADR-011): PLUGINFORGE_PYTHON overrides for
    // installed/venv layouts where the right interpreter isn't `python3` on
    // PATH; the bare-name default relies on the OS PATH search, fine on the
    // Arch dev target.
    auto pythonExe = juce::SystemStats::getEnvironmentVariable(
        "PLUGINFORGE_PYTHON", "python3");

    // TODO: VERIFY: PF-006 (docs/BUGS.md, FLEET req #10) — this &proc capture is
    // NOT safe as written. The thread is detached and can sit up to 120s in
    // waitForProcessToFinish; if the processor is destroyed in that window, the
    // proc.loadFaustCode() call below is a use-after-free. The "processor outlives
    // the editor" host contract does not cover a detached thread that outlives
    // BOTH. Fix (routing: FLEET req #16): make this an owned, joinable worker with
    // an atomic abort + child.kill() on teardown, mirroring FaustEngine's PF-003
    // worker (commit d10f59e). Deliberately NOT restructured in this UI change so
    // the threading rework gets its own Tier-2 report; check: PF-006 in docs/BUGS.md.
    auto& proc = processor;

    std::thread([safeThis, scriptPath, pythonExe, prompt = text.toStdString(), &proc]() mutable
    {
        juce::ChildProcess child;

        // Verified against juce_ChildProcess.h: start(const StringArray&, int) exists
        // with this exact signature — no shell interpretation of the prompt text.
        bool started = child.start(
            juce::StringArray { pythonExe, scriptPath,
                                "--prompt", juce::String(prompt) },
            juce::ChildProcess::wantStdOut | juce::ChildProcess::wantStdErr);

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
            juce::MessageManager::callAsync([safeThis, raw]
            {
                if (safeThis == nullptr) return;
                safeThis->stopWorking();
                safeThis->setError("No JSON in generator output — full output:\n\n" + raw);
                safeThis->statusLabel.setText("Error: generator produced no result (see errors).",
                                              juce::dontSendNotification);
                safeThis->generateButton.setEnabled(true);
            });
            return;
        }

        // Verified against juce_JSON.h / juce_Variant.h: JSON::parse(const String&)
        // returns a var; getProperty(Identifier, default) gives typed access.
        auto parsed = juce::JSON::parse(jsonLine);

        bool success = parsed.getProperty("success", false);
        auto faustCode = parsed.getProperty("faust_code", juce::String()).toString();
        auto errorMsg  = parsed.getProperty("error", juce::String()).toString();
        // ADR-011 `reason` (added with PF-019): ok | invalid_faust | timeout |
        // rate_limited | no_credentials | error. Absent on responses from an older
        // generate.py, which is why the default is empty and statusForReason()
        // falls back to the generic text rather than asserting.
        auto reason = parsed.getProperty("reason", juce::String()).toString();

        if (! success || faustCode.isEmpty())
        {
            juce::MessageManager::callAsync([safeThis, errorMsg, reason]
            {
                if (safeThis == nullptr) return;
                safeThis->stopWorking();
                safeThis->setError(errorMsg.isNotEmpty()
                                       ? errorMsg
                                       : juce::String("Generation failed with no error text."));
                safeThis->statusLabel.setText(statusForReason(reason),
                                              juce::dontSendNotification);
                safeThis->generateButton.setEnabled(true);
            });
            return;
        }

        // proc outlives this thread (see PF-006 marker above). loadFaustCode() posts
        // the actual JIT compile to FaustEngine's own background thread and returns
        // immediately — this call does not block or touch the audio thread. Pass the
        // originating prompt (FLEET req #4, S1) so a DAW-saved session persists what
        // was asked for, not just the generated code.
        proc.loadFaustCode(faustCode, juce::String(prompt));

        // UI components must only be touched on the message thread.
        juce::MessageManager::callAsync([safeThis, faustCode]
        {
            if (safeThis == nullptr) return;
            // generateButton re-enables here, when the subprocess returns; the JIT
            // compile is still running on FaustEngine's compile thread. True ready
            // status arrives via onFaustCompileSuccess, routed to setStatus() by the
            // shell. The progress animation belongs to the subprocess phase, so it
            // stops now even though the JIT swap has not landed yet.
            safeThis->stopWorking();
            safeThis->generateButton.setEnabled(true);
            safeThis->statusLabel.setText("JIT compiling: " +
                faustCode.substring(0, 40) + "...", juce::dontSendNotification);
        });
    }).detach();
}

// ── Status / error surfaces ─────────────────────────────────────────────────
void PromptPanel::setStatus(const juce::String& text)
{
    statusLabel.setText(text, juce::dontSendNotification);
}

void PromptPanel::setError(const juce::String& fullText)
{
    // Kept verbatim (no truncation) and retained across a later success so the
    // user can still scroll back through the last failure.
    errorBox.setText(fullText, juce::dontSendNotification);
    errorBox.setVisible(true);
    errorBox.moveCaretToTop(false);   // show the first line, not the tail
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
    // shell hands it. It fills out fully once S3 widens the band (FLEET req #17);
    // in the current fixed 108px band the error region and progress row simply
    // collapse to zero height rather than overflow. juce::Rectangle::removeFrom*
    // clamps to the space available, so nothing overruns or asserts.
    auto area = getLocalBounds();
    const int gap = 6, buttonH = 28, progressH = 18, statusH = 20;

    // Reserve the fixed control rows from the BOTTOM so the prompt + error region
    // absorb the slack and the controls stay visible even in a shallow band.
    auto statusR = area.removeFromBottom(statusH);
    auto progressR = area.removeFromBottom(progressH);
    area.removeFromBottom(gap);
    auto buttonR = area.removeFromBottom(buttonH);
    area.removeFromBottom(gap);

    // Remaining top area splits between the prompt (min 60px per Prompt B, growing
    // to ~half the slack) and the error region (everything left over).
    const int promptH = juce::jlimit(60,
                                     juce::jmax(60, area.getHeight() - 24),
                                     juce::jmax(60, area.getHeight() / 2));
    promptInput.setBounds(area.removeFromTop(promptH));
    area.removeFromTop(gap);
    errorBox.setBounds(area);   // may be zero-height until S3 widens the band

    generateButton.setBounds(buttonR.removeFromLeft(110));
    buttonR.removeFromLeft(gap);
    historyButton.setBounds(buttonR.removeFromLeft(90));

    progressLabel.setBounds(progressR);
    statusLabel.setBounds(statusR);
}
