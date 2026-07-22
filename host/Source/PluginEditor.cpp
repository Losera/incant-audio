#include "PluginEditor.h"
#include "ParamMap.h"
#include <thread>

PluginForgeEditor::PluginForgeEditor(PluginForgeProcessor& p)
    : AudioProcessorEditor(&p), processor(p)
{
    setSize(480, 410);

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
        // the editor's destructor (clearPointer()). The background std::thread itself
        // only copies safeThis (ref-count-atomic-safe); it never dereferences it.
        juce::Component::SafePointer<PluginForgeEditor> safeThis(this);
        auto scriptPath = generateScript.getFullPathName();

        // Interpreter discovery (ADR-011): PLUGINFORGE_PYTHON overrides for
        // installed/venv layouts where the right interpreter isn't `python3` on
        // PATH; the bare-name default relies on the OS PATH search, fine on the
        // Arch dev target.
        auto pythonExe = juce::SystemStats::getEnvironmentVariable(
            "PLUGINFORGE_PYTHON", "python3");

        // SUBTLE: processor is a reference to PluginForgeProcessor, which creates this
        // editor via createEditor() and is always destroyed after it by the JUCE/DAW
        // host contract — capturing &processor is safe even if the editor is destroyed
        // before this thread finishes.
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
            proc.loadFaustCode(faustCode);

            // UI components must only be touched on the message thread.
            juce::MessageManager::callAsync([safeThis, faustCode]
            {
                if (safeThis == nullptr) return;
                // generateButton re-enables here, when the subprocess returns; the
                // JIT compile is still running on FaustEngine's compile thread. True
                // ready status arrives via onFaustCompileSuccess below (point E of
                // docs/pair_draft_editor_llm_bridge.md — implemented 2026-07-19).
                safeThis->generateButton.setEnabled(true);
                safeThis->statusLabel.setText("JIT compiling: " +
                    faustCode.substring(0, 40) + "...", juce::dontSendNotification);
            });
        }).detach();
    };

    addAndMakeVisible(statusLabel);
    statusLabel.setText("Ready.", juce::dontSendNotification);
    statusLabel.setJustificationType(juce::Justification::centredLeft);

    for (int i = 0; i < MAX_KNOBS; ++i)
    {
        auto& s = paramSliders[static_cast<size_t>(i)];
        s.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
        s.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 64, 16);
        addChildComponent(s);   // invisible until a compile maps this slot

        // Attachment binds to the slot's full 0-1 range; what that 0-1 value
        // means to the live DSP is pushToFaust()'s contract, not the UI's.
        paramAttachments[static_cast<size_t>(i)] =
            std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
                processor.apvts, ParamPool::slotId(i), s);

        auto& l = paramNameLabels[static_cast<size_t>(i)];
        l.setJustificationType(juce::Justification::centred);
        l.setFont(juce::Font(12.0f));
        addChildComponent(l);
    }

    startTimerHz(30);   // level-meter repaint tick (see timerCallback)

    // Surface a Faust compile failure (as opposed to an LLM-generation failure,
    // already handled above) in the UI instead of only juce::Logger. Fires on
    // FaustEngine's detached compile thread (see the comment on
    // PluginForgeProcessor::onFaustCompileError), so this hops to the message
    // thread via the same SafePointer + callAsync pattern as generateButton's
    // callbacks above -- same reasoning: safeThis is only ever dereferenced
    // inside the callAsync lambda, which runs on the message thread.
    juce::Component::SafePointer<PluginForgeEditor> safeThisForErrors(this);
    processor.onFaustCompileError = [safeThisForErrors](const juce::String& error)
    {
        juce::MessageManager::callAsync([safeThisForErrors, error]
        {
            if (safeThisForErrors == nullptr) return;
            safeThisForErrors->statusLabel.setText(
                "Faust compile error: " + error.substring(0, 200),
                juce::dontSendNotification);
        });
    };

    // True ready status (ADR-011 point E): fires on the compile thread once the
    // JIT DSP and remapped params are about to go live — same SafePointer +
    // callAsync hop as the error path above.
    processor.onFaustCompileSuccess =
        [safeThisForErrors](const FaustEngine::ParamList& params)
    {
        // Copy the list into the callAsync capture — the reference is only
        // valid for the duration of this compile-thread call.
        juce::MessageManager::callAsync([safeThisForErrors, params]
        {
            if (safeThisForErrors == nullptr) return;
            const auto numParams = static_cast<int>(params.size());
            safeThisForErrors->statusLabel.setText(
                juce::String(juce::CharPointer_UTF8("Ready \xe2\x80\x94 DSP live, "))
                + juce::String(numParams)
                + (numParams == 1 ? " param mapped." : " params mapped."),
                juce::dontSendNotification);
            safeThisForErrors->refreshParamKnobs(params);
        });
    };
}

void PluginForgeEditor::refreshParamKnobs(const FaustEngine::ParamList& params)
{
    const int numMapped =
        juce::jmin(static_cast<int>(params.size()), ParamPool::POOL_SIZE);

    // ── Seed EVERY mapped slot, not just the ones with a visible knob ───────
    // This loop used to stop at MAX_KNOBS (8) while the pool holds 64. Slots
    // 8..63 therefore kept whatever they last held (0.0 on a fresh instance).
    // Under the old raw push that was merely wrong; now that pushToFaust
    // denormalises, 0.0 maps to each zone's MINIMUM -- a 12-param patch would
    // come up with its 9th..12th controls pinned to the bottom of their range
    // (a cutoff at 20 Hz: silence). Knob *visibility* is still capped at 8
    // below; slot *seeding* must cover the whole pool.
    for (int i = 0; i < numMapped; ++i)
    {
        const auto& p = params[static_cast<size_t>(i)];
        if (auto* rp = processor.apvts.getParameter(ParamPool::slotId(i)))
        {
            // Shared with pushToFaust's forward direction -- the two must be
            // exact inverses or the knob lies about the value it is sending.
            rp->setValueNotifyingHost(
                juce::jlimit(0.0f, 1.0f, ParamMap::mapZoneToSlot(p.defaultValue, p)));
        }
    }

    // ── Knob visibility and labels ─────────────────────────────────────────
    numVisibleKnobs = juce::jmin(numMapped, MAX_KNOBS);

    for (int i = 0; i < MAX_KNOBS; ++i)
    {
        const bool on = i < numVisibleKnobs;
        paramSliders[static_cast<size_t>(i)].setVisible(on);
        paramNameLabels[static_cast<size_t>(i)].setVisible(on);

        if (!on)
            continue;

        paramNameLabels[static_cast<size_t>(i)].setText(
            juce::String(params[static_cast<size_t>(i)].label),
            juce::dontSendNotification);
    }
}

PluginForgeEditor::~PluginForgeEditor() {}

void PluginForgeEditor::timerCallback()
{
    // Instant attack, exponential release — the standard meter ballistics that
    // read well at 30fps. Relaxed load matches the relaxed store in processBlock;
    // a one-block-stale value is invisible at meter timescales.
    auto peak = processor.outputLevel.load(std::memory_order_relaxed);
    displayLevel = (peak > displayLevel) ? peak : displayLevel * 0.85f;
    repaint(meterBounds);

    // Output-guard edge detection. The guard latches on the audio thread; this
    // is the only place the UI learns about it. Written on transition only (see
    // wasOutputMuted) so a compile error message isn't overwritten 30x/second.
    const bool nowMuted = processor.isOutputMuted();
    if (nowMuted != wasOutputMuted)
    {
        wasOutputMuted = nowMuted;
        if (nowMuted)
        {
            const auto why = processor.outputTrip() == OutputGuard::Trip::NonFinite
                                 ? "produced NaN/Inf"
                                 : "ran away to full scale";
            statusLabel.setText(juce::String("DSP MUTED - output ") + why
                                    + ". Generate again to reset.",
                                juce::dontSendNotification);
        }
    }
}

void PluginForgeEditor::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff1e1e2e));
    g.setColour(juce::Colours::white);
    g.setFont(16.0f);
    g.drawText("PluginForge", getLocalBounds().removeFromTop(36),
               juce::Justification::centred);

    // ── Output level meter (post-DSP peak) ──────────────────────────────────
    auto track = meterBounds.toFloat();
    g.setColour(juce::Colour(0xff11111b));
    g.fillRoundedRectangle(track, 3.0f);

    // Map linear peak → meter fraction with a gentle curve so quiet material
    // still registers; clip at 1.0 (levels above 0dBFS just pin the meter).
    auto frac = juce::jlimit(0.0f, 1.0f, std::pow(displayLevel, 0.5f));
    if (frac > 0.001f)
    {
        auto fill = track.withWidth(track.getWidth() * frac);
        g.setGradientFill(juce::ColourGradient(
            juce::Colour(0xff94e2d5), track.getX(), 0.0f,      // teal
            juce::Colour(0xfff38ba8), track.getRight(), 0.0f,  // red at hot end
            false));
        g.fillRoundedRectangle(fill, 3.0f);
    }
}

void PluginForgeEditor::resized()
{
    auto area = getLocalBounds().reduced(16);
    area.removeFromTop(36);
    promptInput.setBounds(area.removeFromTop(36));
    area.removeFromTop(8);
    generateButton.setBounds(area.removeFromTop(32).removeFromLeft(120));
    area.removeFromTop(8);
    statusLabel.setBounds(area.removeFromTop(24));
    area.removeFromTop(8);
    meterBounds = area.removeFromTop(14);

    // ── Knob grid: 4 columns × 2 rows below the meter ───────────────────────
    area.removeFromTop(10);
    const int cols  = 4;
    const int cellW = area.getWidth() / cols;
    const int cellH = 95;
    for (int i = 0; i < MAX_KNOBS; ++i)
    {
        auto cell = juce::Rectangle<int>(area.getX() + (i % cols) * cellW,
                                         area.getY() + (i / cols) * cellH,
                                         cellW, cellH);
        paramNameLabels[static_cast<size_t>(i)].setBounds(cell.removeFromTop(16));
        paramSliders[static_cast<size_t>(i)].setBounds(cell.reduced(4));
    }
}
