# PAIR Draft — Editor → LLM Bridge

**Mode:** PAIR. This is a research artifact. Read it, verify the JUCE API calls against
installed headers, then write the version that gets committed. Do not commit this file's
code verbatim.

**Scope:** `host/Source/PluginEditor.h` (three new private members) and
`host/Source/PluginEditor.cpp` (full `generateButton.onClick` implementation).
`generate.py --prompt` flag is already in place (DELEGATE, done).

---

## What this draft demonstrates

1. Spawn `generate.py --prompt "..."` as a child process from the JUCE message thread
   using `juce::ChildProcess` — no shell, no stdin pipe, no temp files.
2. Do the blocking read on a detached `std::thread` so the UI stays responsive.
3. Use `juce::Component::SafePointer` so the callback is a no-op if the editor is
   closed while the background thread is running.
4. Hop back to the message thread via `juce::MessageManager::callAsync` before
   touching any UI components or calling `loadFaustCode`.

---

## Required change to `PluginEditor.h`

Add three private members below `statusLabel`:

```cpp
// ── LLM bridge ───────────────────────────────────────────────────────────
// generateScript: resolved once in the constructor; empty if not found.
juce::File generateScript;

// TODO: VERIFY API: SafePointer is available in juce_gui_basics — confirm
// PluginEditor.h already includes juce_audio_processors which pulls it in,
// or add: #include <juce_gui_basics/juce_gui_basics.h>
```

No additional data members are needed — `SafePointer` is constructed inline in the lambda.

---

## Constructor addition

Resolve `generate.py` once at startup rather than on every click. The path strategy
below works in the build tree; you will need a different strategy for distribution
(bundle the script next to the plugin binary and resolve from `currentExecutableFile`).

```cpp
// In PluginForgeEditor constructor, after addAndMakeVisible(statusLabel):

// TODO: VERIFY: adjust this path to match your actual build layout.
// During development the plugin binary sits in build/PluginForgeHost_artefacts/...
// and the source tree is two levels above host/. The env-var fallback lets you
// override at runtime without rebuilding.
auto envScript = juce::SystemStats::getEnvironmentVariable(
    "PLUGINFORGE_LLM_SCRIPT", "");

if (envScript.isNotEmpty())
    generateScript = juce::File(envScript);
else
    generateScript = juce::File(
        juce::File::getSpecialLocation(juce::File::currentExecutableFile)
            .getParentDirectory()            // binary dir
            .getSiblingFile("llm")           // TODO: VERIFY path relative to binary
            .getChildFile("generate.py"));

if (!generateScript.existsAsFile())
    statusLabel.setText("WARN: generate.py not found at " +
                        generateScript.getFullPathName(),
                        juce::dontSendNotification);
```

---

## Full `generateButton.onClick` replacement

Replace the current stub body with this:

```cpp
generateButton.onClick = [this]
{
    auto text = promptInput.getText().trim();
    if (text.isEmpty())
        return;

    // Prevent double-submit. Re-enabled inside the callAsync callback.
    generateButton.setEnabled(false);
    statusLabel.setText("Generating...", juce::dontSendNotification);

    if (!generateScript.existsAsFile())
    {
        statusLabel.setText("Error: generate.py not found.", juce::dontSendNotification);
        generateButton.setEnabled(true);
        return;
    }

    // SUBTLE: SafePointer becomes nullptr the moment the editor's destructor runs.
    // The background thread checks it before touching any UI or processor state.
    // Do NOT capture `this` directly — the editor can be destroyed while the
    // thread is running (DAW closes the plugin window mid-generation).
    juce::Component::SafePointer<PluginForgeEditor> safeThis(this);
    auto scriptPath = generateScript.getFullPathName();

    // SUBTLE: processor is a reference to the PluginForgeProcessor, which is owned
    // by the host and always outlives any editor. Capturing &processor is safe even
    // if the editor is destroyed before the thread finishes.
    auto& proc = processor;

    std::thread([safeThis, scriptPath, prompt = text.toStdString(), &proc]() mutable
    {
        // ── 1. Spawn the Python subprocess ───────────────────────────────────
        juce::ChildProcess child;

        // TODO: VERIFY API: juce::ChildProcess::start(StringArray) is available in
        // juce_core. Confirm the StringArray form (not the String/command-line form)
        // is available in your JUCE 7 install — it avoids shell interpretation.
        // If only the String form exists, use:
        //   child.start("python3 " + scriptPath + " --prompt " + escapedPrompt)
        // and ensure the prompt string is shell-safe (no unescaped quotes/spaces).
        bool started = child.start(
            juce::StringArray { "python3", scriptPath,
                                "--prompt", juce::String(prompt) },
            juce::ChildProcess::wantStdOut | juce::ChildProcess::wantStdErr);

        if (!started)
        {
            juce::MessageManager::callAsync([safeThis]
            {
                if (safeThis == nullptr) return;
                safeThis->statusLabel.setText("Error: failed to start python3.",
                                              juce::dontSendNotification);
                safeThis->generateButton.setEnabled(true);
            });
            return;
        }

        // ── 2. Read all output (blocks until process exits) ──────────────────
        // TODO: VERIFY API: readAllProcessOutput() reads both stdout and stderr
        // when wantStdErr is set. Confirm whether it interleaves them or separates
        // them — we want only stdout (the JSON line) for parsing.
        // If they are interleaved, switch to wantStdOut only and accept that
        // Python tracebacks will be invisible unless you add separate stderr capture.
        auto raw = child.readAllProcessOutput();

        // TODO: VERIFY API: waitForProcessToFinish — call after readAllProcessOutput
        // to reap the process and get its exit code. The timeout here is a safety net;
        // readAllProcessOutput() already blocks until EOF.
        child.waitForProcessToFinish(500);

        // ── 3. Parse the JSON response ───────────────────────────────────────
        // generate.py --prompt outputs exactly one JSON line to stdout.
        // Find the last line that starts with '{' in case stderr is mixed in.
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
                safeThis->statusLabel.setText("Error: no JSON in output:\n" + raw,
                                              juce::dontSendNotification);
                safeThis->generateButton.setEnabled(true);
            });
            return;
        }

        // TODO: VERIFY: JUCE 7 has no built-in JSON parser for arbitrary objects.
        // juce::JSON::parse returns a juce::var. Accessing fields uses
        // var["key"] which returns var (not string). Confirm the API below against
        // juce_data_structures or juce_core headers.
        auto parsed = juce::JSON::parse(jsonLine);

        bool success = static_cast<bool>(parsed["success"]);
        auto faustCode = parsed["faust_code"].toString();
        auto errorMsg  = parsed["error"].toString();

        if (!success || faustCode.isEmpty())
        {
            juce::MessageManager::callAsync([safeThis, errorMsg]
            {
                if (safeThis == nullptr) return;
                safeThis->statusLabel.setText("LLM error: " + errorMsg,
                                              juce::dontSendNotification);
                safeThis->generateButton.setEnabled(true);
            });
            return;
        }

        // ── 4. Load the Faust code (processor outlives editor — safe to call) ─
        // loadFaustCode posts the compile to its own background thread internally;
        // this call returns immediately.
        proc.loadFaustCode(faustCode);

        // ── 5. Hop back to the message thread to update UI ───────────────────
        // SUBTLE: statusLabel and generateButton are UI components — they must only
        // be touched on the message thread. callAsync posts a lambda to the message
        // thread's event queue; it runs asynchronously after this thread returns.
        juce::MessageManager::callAsync([safeThis, faustCode]
        {
            if (safeThis == nullptr) return;
            safeThis->statusLabel.setText("Compiling...", juce::dontSendNotification);
            // generateButton stays disabled until FaustEngine fires its callback.
            // Wire that callback in PluginProcessor::loadFaustCode to re-enable it.
            // For now, re-enable immediately so the UI isn't permanently locked.
            // TODO: replace with a callback from loadFaustCode once compile is wired.
            safeThis->generateButton.setEnabled(true);
            safeThis->statusLabel.setText("Loaded: " +
                faustCode.substring(0, 40) + "...", juce::dontSendNotification);
        });

    }).detach();
};
```

---

## What this draft does NOT handle (verify and decide before committing)

### A. `SafePointer` thread-safety

`juce::Component::SafePointer<T>` wraps `juce::WeakReference<T>`. Copying the
`SafePointer` into the lambda is safe (the copy happens on the message thread before
`detach()`). Inside the thread, calling `if (safeThis == nullptr)` is safe because
`WeakReference` null-check is implemented with an atomic flag.

**What to verify:** Confirm in `juce_core/memory/juce_WeakReference.h` that the
`masterReference` pointer is accessed with `std::atomic` semantics. If it uses a
plain pointer with a mutex, the pattern is still safe (the mutex is uncontended in
the happy path), but you should know which it is.

### B. `juce::ChildProcess` stdin support

This draft uses `start(StringArray)` which does not write to the child's stdin.
`generate.py --prompt` reads from argv, not stdin, so no stdin write is needed.
The `--json` stdin mode is still available for other callers.

**What to verify:** Open `juce_core/native/juce_ChildProcess.cpp` (Linux implementation)
and confirm that `start(StringArray)` sets up the pipe for stdout correctly and that
`readAllProcessOutput()` does not hang if the child writes nothing to stdout.

### C. stderr interleaving

`wantStdOut | wantStdErr` causes stdout and stderr to be merged into one stream on
Linux (they are both redirected to the same pipe fd). If the LLM Python layer prints
retry messages to stdout (it does in the non-`generate_json` code path, but
`generate_json` is silent), they will appear before the JSON line.

The draft handles this by scanning for the last `{`-prefixed line. **But:** if a retry
error message itself starts with `{`, this breaks. Safer fix: have `generate.py` write
all diagnostic output to `sys.stderr` in the `--prompt` path. That is a one-line
DELEGATE change you can make before integrating this draft.

**RESOLVED 2026-07-19** (DELEGATE, see point F below): `--prompt`/`--json` now wrap
their body in `_run_subprocess_mode()`, which converts any unexpected exception into
the ADR-011 failure JSON on stdout (traceback goes to stderr only) — so a stray `{`
in an error message can no longer come from an uncaught traceback mixing into stdout.
`generate_json()` itself was already silent on stdout; this closes the remaining
startup-crash gap (e.g. the missing-key case in point F).

### D. `juce::JSON::parse` API

`juce::JSON::parse(const String&)` returns a `juce::var`. Field access is:
```cpp
juce::var root = juce::JSON::parse(jsonLine);
bool success = root.getProperty("success", false);
juce::String faust = root.getProperty("faust_code", "").toString();
```

The `operator[]` form (`root["success"]`) also works but returns `var`, not `bool`.
Cast explicitly. Verify the exact API in `juce_core/text/juce_JSON.h`.

### E. The compile callback runs on the compile thread, not the message thread

`FaustEngine::compile()` calls its `CompileCallback` from the `std::thread` it
detaches — not from the message thread. In `PluginProcessor::loadFaustCode()`:

```cpp
faustEngine.compile(faustCode, [this](const FaustEngine::ParamList& params,
                                      const std::string& error) {
    if (error.empty())
        paramPool.remap(params);       // ← runs on compile thread
    else
        juce::Logger::writeToLog(...); // ← runs on compile thread
});
```

`paramPool.remap()` writes `activeLabels` on the compile thread while
`pushToFaust()` reads it on the audio thread. **This is the existing data race
(HUMAN-OWNED).** Do not add UI updates to this callback without wrapping them in
`callAsync`.

If you want the `statusLabel` to update to "Ready" after JIT compilation finishes,
the cleanest pattern is to wrap the `loadFaustCode` callback in `callAsync`:

```cpp
faustEngine.compile(faustCode, [this](const FaustEngine::ParamList& params,
                                      const std::string& error) {
    if (error.empty())
        paramPool.remap(params);
    // UI update must hop to message thread:
    juce::MessageManager::callAsync([this, error] {
        // but `this` is PluginForgeProcessor — it has no statusLabel.
        // You would need to give the processor a callback to invoke,
        // or use a listener pattern. Design decision for you to make.
    });
});
```

The editor draft above does not wire compile-completion back to the UI — that is a
follow-on task. For now the button re-enables when the Python subprocess returns, not
when JIT compilation finishes.

### F. `generate.py --prompt` stderr cleanliness

`generate_json()` is silent — it does not print retry progress. But the `.env` load
and `anthropic.Anthropic()` constructor may print warnings to stderr if the API key
is missing or the env file is malformed. These go to the merged pipe and could
confuse the JSON scanner. Add `ANTHROPIC_API_KEY` existence check in `generate.py`
before running the subprocess if you want clean error messaging in the UI.

**RESOLVED 2026-07-19** (DELEGATE): `--prompt`/`--json` now precheck
`os.environ.get("ANTHROPIC_API_KEY")` before any API call and, if empty/unset, print
the ADR-011 failure JSON — `{"success": false, "faust_code": null, "attempts": 0,
"error": "ANTHROPIC_API_KEY is not set. Add it to PluginForge/.env or the plugin's
environment."}` — and exit 0 (verified: `PluginEditor.cpp`'s `ChildProcess` handling
never inspects the exit code, only `waitForProcessToFinish` + stdout, so exit 0 is
correct — a nonzero exit carries no signal the host reads and would only obscure the
structured error). Also verified `anthropic.Anthropic()` does not raise at
construction with no key (auth is validated lazily at request-send time), so the
module-level `client` did not need to become lazy. Tests:
`tests/test_generate_unit.py::TestSubprocessModeMissingApiKey`,
`::TestSubprocessModeUnexpectedException`, `::TestSubprocessModeNormalPath`.

---

## What to do with this draft (your steps)

See the section "Human steps and what you must understand" in the companion analysis.
