#include "PluginEditor.h"

PluginForgeEditor::PluginForgeEditor(PluginForgeProcessor& p)
    : AudioProcessorEditor(&p), processor(p),
      promptPanel(p), codeEditorPanel(p), paramGridPanel(p)
{
    setSize(480, 410);

    // Resizable shell (FLEET req #1, overseer-routed into Task 0): the Code/Errors
    // panel Session 2 will add cannot fit the old fixed 480×410, so the shell now
    // resizes and hands each child panel its own bounds in resized(). Default size
    // is unchanged, so the pre-split layout is reproduced pixel-for-pixel at launch;
    // min size keeps the prompt band + meter + one knob row visible. The banded
    // resized() below already tolerates arbitrary sizes (grid takes the remainder).
    setResizable(true, true);
    setResizeLimits(480, 360, 1400, 1200);

    addAndMakeVisible(promptPanel);
    addAndMakeVisible(paramGridPanel);

    // CodeEditorPanel is a Task-0 placeholder with no behaviour yet: add it as an
    // invisible child and give it no layout space, so the split stays
    // zero-behaviour. Session 2 makes it visible and requests space via FLEET.md.
    addChildComponent(codeEditorPanel);

    startTimerHz(30);   // level-meter repaint tick (see timerCallback)

    // ── Route the processor's compile callbacks to the child panels ──────────
    // Both fire on FaustEngine's compile thread (see PluginProcessor.h), so each
    // hops to the message thread via SafePointer + callAsync before touching any
    // child component — the same contract PromptPanel's own callbacks follow.
    // safeThis is only ever dereferenced inside the callAsync lambda, which runs
    // on the message thread (the thread that also runs this editor's destructor).
    juce::Component::SafePointer<PluginForgeEditor> safeThis(this);

    // Surface a Faust compile failure (as opposed to an LLM-generation failure,
    // handled inside PromptPanel) in the status line.
    processor.onFaustCompileError = [safeThis](const juce::String& error)
    {
        juce::MessageManager::callAsync([safeThis, error]
        {
            if (safeThis == nullptr) return;
            safeThis->promptPanel.setStatus(
                "Faust compile error: " + error.substring(0, 200));
        });
    };

    // True ready status (ADR-011 point E): fires on the compile thread once the
    // JIT DSP and remapped params are about to go live. Status text goes to the
    // PromptPanel; the param list drives ParamGridPanel's knobs.
    processor.onFaustCompileSuccess =
        [safeThis](const FaustEngine::ParamList& params)
    {
        // Copy the list into the callAsync capture — the reference is only valid
        // for the duration of this compile-thread call.
        juce::MessageManager::callAsync([safeThis, params]
        {
            if (safeThis == nullptr) return;
            const auto numParams = static_cast<int>(params.size());
            safeThis->promptPanel.setStatus(
                juce::String(juce::CharPointer_UTF8("Ready \xe2\x80\x94 DSP live, "))
                + juce::String(numParams)
                + (numParams == 1 ? " param mapped." : " params mapped."));
            safeThis->paramGridPanel.refreshParamKnobs(params);
        });
    };
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
            promptPanel.setStatus(juce::String("DSP MUTED - output ") + why
                                      + ". Generate again to reset.");
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
    // Vertical bands, identical geometry to the pre-split monolith. Each panel
    // lays its own widgets out relative to the bounds it is handed here, so the
    // absolute positions are unchanged.
    auto area = getLocalBounds().reduced(16);
    area.removeFromTop(36);                       // title spacer (title painted full-width)

    // PromptPanel band = prompt(36) + gap(8) + button(32) + gap(8) + status(24).
    promptPanel.setBounds(area.removeFromTop(36 + 8 + 32 + 8 + 24));
    area.removeFromTop(8);
    meterBounds = area.removeFromTop(14);
    area.removeFromTop(10);

    // Remaining space is the knob grid.
    paramGridPanel.setBounds(area);
}
