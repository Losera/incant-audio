#include "PromptPanel.h"
#include <thread>

PromptPanel::PromptPanel(PluginForgeProcessor& p)
    : processor(p)
{
    addAndMakeVisible(promptInput);
    promptInput.setMultiLine(false);
    promptInput.setReturnKeyStartsNewLine(false);
    promptInput.setTextToShowWhenEmpty("Describe your plugin...",
                                       juce::Colours::grey);

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
        // Not found: generateScript stays invalid; the onClick handler already
        // surfaces "generate.py not found" with the (empty) path in the UI.
    }

    addAndMakeVisible(generateButton);
    generateButton.onClick = [this]
    {
        auto text = promptInput.getText().trim();
        if (text.isEmpty())
            return;

        // Prevent double-submit. Re-enabled once the subprocess returns (see point E
        // in docs/pair_draft_editor_llm_bridge.md: JIT compile itself finishes later,
        // on the compile thread — wiring "Ready" back to the UI is a follow-on task).
        generateButton.setEnabled(false);
        statusLabel.setText("Generating...", juce::dontSendNotification);

        if (!generateScript.existsAsFile())
        {
            statusLabel.setText("Error: generate.py not found at " +
                                generateScript.getFullPathName(),
                                juce::dontSendNotification);
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

        // SUBTLE: processor is a reference to PluginForgeProcessor, which creates the
        // editor (and therefore this panel) via createEditor() and is always destroyed
        // after it by the JUCE/DAW host contract — capturing &processor is safe even
        // if the panel is destroyed before this thread finishes.
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

            if (!started)
            {
                juce::MessageManager::callAsync([safeThis, pythonExe]
                {
                    if (safeThis == nullptr) return;
                    safeThis->statusLabel.setText("Error: failed to start " + pythonExe + ".",
                                                  juce::dontSendNotification);
                    safeThis->generateButton.setEnabled(true);
                });
                return;
            }

            // Bound the wait before reading: readAllProcessOutput() alone blocks until
            // EOF, so a hung generate.py (network stall, interactive pdb, ...) would
            // wedge this thread forever with the button stuck disabled. Wait with a
            // cap, kill on expiry. 120s covers ADR-005's worst case (3 LLM attempts +
            // faust validation per attempt). SUBTLE: the child could in principle fill
            // the 64KB pipe buffer and block writing before exiting — but the payload
            // is one JSON line (small), and even then this degrades to a bounded
            // timeout+kill, never an unbounded hang.
            if (!child.waitForProcessToFinish(120 * 1000))
            {
                child.kill();
                juce::MessageManager::callAsync([safeThis]
                {
                    if (safeThis == nullptr) return;
                    safeThis->statusLabel.setText("Error: LLM subprocess timed out after 120s.",
                                                  juce::dontSendNotification);
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
                    safeThis->statusLabel.setText("Error: no JSON in output:\n" + raw,
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

            // proc outlives this thread (see SUBTLE above). loadFaustCode() posts the
            // actual JIT compile to FaustEngine's own background thread and returns
            // immediately — this call does not block or touch the audio thread.
            // Pass the originating prompt (FLEET req #4, S1) so a DAW-saved session
            // persists what was asked for, not just the generated code.
            proc.loadFaustCode(faustCode, juce::String(prompt));

            // UI components must only be touched on the message thread.
            juce::MessageManager::callAsync([safeThis, faustCode]
            {
                if (safeThis == nullptr) return;
                // generateButton re-enables here, when the subprocess returns; the
                // JIT compile is still running on FaustEngine's compile thread. True
                // ready status arrives via onFaustCompileSuccess (point E of
                // docs/pair_draft_editor_llm_bridge.md), routed to setStatus() by the
                // shell.
                safeThis->generateButton.setEnabled(true);
                safeThis->statusLabel.setText("JIT compiling: " +
                    faustCode.substring(0, 40) + "...", juce::dontSendNotification);
            });
        }).detach();
    };

    addAndMakeVisible(statusLabel);
    statusLabel.setText("Ready.", juce::dontSendNotification);
    statusLabel.setJustificationType(juce::Justification::centredLeft);
}

void PromptPanel::setStatus(const juce::String& text)
{
    statusLabel.setText(text, juce::dontSendNotification);
}

void PromptPanel::resized()
{
    // Panel-local layout. The shell sizes this panel so these three bands land
    // exactly where prompt/button/status sat before the split.
    auto area = getLocalBounds();
    promptInput.setBounds(area.removeFromTop(36));
    area.removeFromTop(8);
    generateButton.setBounds(area.removeFromTop(32).removeFromLeft(120));
    area.removeFromTop(8);
    statusLabel.setBounds(area.removeFromTop(24));
}
