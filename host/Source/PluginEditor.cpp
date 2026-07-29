#include "PluginEditor.h"

PluginForgeEditor::PluginForgeEditor(PluginForgeProcessor& p)
    : AudioProcessorEditor(&p), processor(p),
      promptPanel(p), codeEditorPanel(p), paramGridPanel(p)
{
    // Taller default than the pre-split 480×410 so S2's widened prompt/error band
    // (kPromptBandH, FLEET req #17) fits with a grid row visible below the meter.
    setSize(480, 460);

    // Resizable shell (FLEET req #1/#17): the prompt+error band and the auto-layout
    // grid both need to flex, and the code editor won't fit a fixed window. Each
    // child panel gets its own bounds in resized(); min height keeps the prompt
    // band + meter visible, max stays within a sane on-screen size.
    setResizable(true, true);
    setResizeLimits(480, kMinWindowH, 1400, 1200);

    addAndMakeVisible(promptPanel);
    addAndMakeVisible(paramGridPanel);

    // The read-only Faust view (ux_roadmap Phase 3a). Still an invisible child
    // with no layout space UNTIL the user asks for it: this is a no-code tool and
    // must not open on a wall of DSL. The band it takes when shown (kCodeBandH)
    // has been reserved in resized() and updateWindowSizeForParams() since the
    // Task-0 split — the plumbing was finished long before the panel was.
    addChildComponent(codeEditorPanel);

    addAndMakeVisible(codeToggle);
    codeToggle.onClick = [this] { setCodeViewVisible(! codeEditorPanel.isVisible()); };

    startTimerHz(30);   // level-meter repaint tick (see timerCallback)

    // ── Route the processor's compile callbacks to the child panels ──────────
    // Both fire on FaustEngine's compile thread (see PluginProcessor.h), so each
    // hops to the message thread via SafePointer + callAsync before touching any
    // child component — the same contract PromptPanel's own callbacks follow.
    // safeThis is only ever dereferenced inside the callAsync lambda, which runs
    // on the message thread (the thread that also runs this editor's destructor).
    juce::Component::SafePointer<PluginForgeEditor> safeThis(this);

    // Surface a Faust compile failure (as opposed to an LLM-generation failure,
    // handled inside PromptPanel) in the status line. Uses the canonical
    // onFaustCompileFailure name (FLEET req #7); the deprecated onFaustCompileError
    // alias in PluginProcessor.h can now be removed by S1.
    processor.onFaustCompileFailure = [safeThis](const juce::String& error)
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
            // The source of record is committed in the same success branch that
            // fires this callback (PluginProcessor.cpp:180-181), so by the time
            // this message-thread hop runs, currentSource() is the patch that
            // just went live. Pushed unconditionally, even while the view is
            // hidden, so revealing it is instant and never shows a stale patch.
            safeThis->codeEditorPanel.showSource(safeThis->processor.currentSource());
            safeThis->updateWindowSizeForParams();
        });
    };
}

void PluginForgeEditor::setCodeViewVisible(bool shouldBeVisible)
{
    if (codeEditorPanel.isVisible() == shouldBeVisible)
        return;

    if (shouldBeVisible)
        codeEditorPanel.showSource(processor.currentSource());

    codeEditorPanel.setVisible(shouldBeVisible);
    codeToggle.setButtonText(shouldBeVisible ? "Hide code" : "Show code");

    // updateWindowSizeForParams already adds kCodeBandH when the panel is
    // visible, so the window grows and shrinks with the disclosure rather than
    // stealing the band from the param grid.
    updateWindowSizeForParams();
    resized();
}

void PluginForgeEditor::updateWindowSizeForParams()
{
    // Grid content height the panel wants, capped to kMaxGridRows so the window
    // stays reasonable (the Viewport scrolls beyond the cap), and floored so the
    // window never drops below the setResizeLimits minimum (480×360). The result
    // is clamped again by the ComponentBoundsConstrainer setResizeLimits installed.
    const int wanted = paramGridPanel.preferredContentHeight();
    const int capH   = kMaxGridRows * ParamGridPanel::kCellH;
    const int gridH  = juce::jmin(wanted, capH);
    const int codeH  = codeEditorPanel.isVisible() ? kCodeBandH : 0;
    const int winH   = juce::jmax(kMinWindowH, kChromeHeight + gridH + codeH);
    setSize(getWidth(), winH);   // synchronously triggers resized() below
}

PluginForgeEditor::~PluginForgeEditor() {}

// ── Test-only forwarders ────────────────────────────────────────────────────
// See the header for why these are forwarders rather than panel accessors.
void PluginForgeEditor::submitPromptForTest(const juce::String& text)
{
    promptPanel.submitPromptForTest(text);
}

juce::String PluginForgeEditor::statusTextForTest() const
{
    return promptPanel.statusTextForTest();
}

juce::String PluginForgeEditor::errorTextForTest() const
{
    return promptPanel.errorTextForTest();
}

int PluginForgeEditor::gridControlCountForTest() const
{
    return paramGridPanel.controlCountForTest();
}

ParamGridPanel::WidgetKind PluginForgeEditor::gridControlKindForTest(int i) const
{
    return paramGridPanel.controlKindForTest(i);
}

juce::String PluginForgeEditor::gridControlLabelForTest(int i) const
{
    return paramGridPanel.controlLabelForTest(i);
}

double PluginForgeEditor::gridControlValueForTest(int i) const
{
    return paramGridPanel.controlValueForTest(i);
}

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
    // Vertical bands. Each panel lays its own widgets out relative to the bounds
    // it is handed here (S2 owns PromptPanel's internal split; S3 owns the grid).
    // The band heights here MUST match kChromeHeight in the header.
    auto area = getLocalBounds().reduced(16);
    area.removeFromTop(36);                       // title spacer (title painted full-width)

    promptPanel.setBounds(area.removeFromTop(kPromptBandH));   // FLEET req #17
    area.removeFromTop(8);
    meterBounds = area.removeFromTop(14);
    area.removeFromTop(10);

    // Disclosure sits directly above whichever region is below it, so it reads as
    // the control for the code band rather than as part of the prompt block.
    {
        auto row = area.removeFromTop(24);
        codeToggle.setBounds(row.removeFromRight(110));
        area.removeFromTop(6);
    }

    // Code/Errors region (S2): reserved at the bottom only while the panel is
    // visible. It starts hidden, so the grid keeps the whole remainder today.
    if (codeEditorPanel.isVisible())
        codeEditorPanel.setBounds(area.removeFromBottom(kCodeBandH));

    // Remaining space is the auto-layout grid.
    paramGridPanel.setBounds(area);
}
