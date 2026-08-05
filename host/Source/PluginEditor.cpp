#include "PluginEditor.h"
#include "Theme.h"

PluginForgeEditor::PluginForgeEditor(PluginForgeProcessor& p)
    : AudioProcessorEditor(&p), processor(p),
      promptPanel(p), codeEditorPanel(p), paramGridPanel(p)
{
    // Taller default than the pre-split 480×410 so the widened prompt/error band
    // (Chrome::promptH) fits with a grid row visible below the meter.
    setSize(480, 460);

    // Resizable shell: the prompt+error band and the auto-layout
    // grid both need to flex, and the code editor won't fit a fixed window. Each
    // child panel gets its own bounds in resized(); min height keeps the prompt
    // band + meter visible, max stays within a sane on-screen size.
    setResizable(true, true);
    setResizeLimits(480, kMinWindowH, 1400, 1200);

    addAndMakeVisible(promptPanel);
    addAndMakeVisible(paramGridPanel);

    // The read-only Faust view (ux_roadmap Phase 3a). Still an invisible child
    // with no layout space UNTIL the user asks for it: this is a no-code tool and
    // must not open on a wall of DSL. The band it takes when shown (Chrome::codeH)
    // has been reserved in resized() and updateWindowSizeForParams() since the
    // Task-0 split — the plumbing was finished long before the panel was.
    addChildComponent(codeEditorPanel);

    addAndMakeVisible(codeToggle);
    codeToggle.onClick = [this] { setCodeViewVisible(! codeEditorPanel.isVisible()); };

    addAndMakeVisible(styleToggle);
    styleToggle.onClick = [this] { cycleControlStyle(); };

    // Come up in whatever style is stored, not the default -- a project reopened
    // with rotaries must not snap back to sliders. Applied before any compile, so
    // the first refreshParamKnobs already styles correctly.
    applyControlStyle(processor.uiStyle());

    // Keep a second open editor, and this one after a project load, in sync.
    // setStateInformation fires this before its restore recompile.
    processor.onUiStyleChanged = [safeThis = juce::Component::SafePointer<PluginForgeEditor>(this)]
        (const juce::String& styleName)
    {
        juce::MessageManager::callAsync([safeThis, styleName]
        {
            if (safeThis == nullptr) return;
            safeThis->applyControlStyle(styleName);
        });
    };

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
    // onFaustCompileFailure name; the deprecated onFaustCompileError alias in
    // PluginProcessor.h is now assigned by nothing and can be removed.
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
            // `params` is the PER-SLOT view: POOL_SIZE entries, most of them the
            // unused-slot sentinel. Counting size() here would report "64 params
            // mapped" for every patch. The occupied slots are the ones with a
            // live zone -- the same discriminator the grid skips on.
            int numParams = 0;
            for (const auto& p : params)
                if (p.zone != nullptr)
                    ++numParams;
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

void PluginForgeEditor::applyControlStyle(const juce::String& styleName)
{
    const auto s = ParamGridPanel::controlStyleFromName(styleName);
    paramGridPanel.setControlStyle(s);

    switch (s)
    {
        case ParamGridPanel::ControlStyle::Rotary:     styleToggle.setButtonText("Knobs: rotary"); break;
        case ParamGridPanel::ControlStyle::Horizontal: styleToggle.setButtonText("Knobs: sliders"); break;
        case ParamGridPanel::ControlStyle::Faithful:   styleToggle.setButtonText("Knobs: auto"); break;
    }

    // The two styles want different content heights, so the window follows.
    updateWindowSizeForParams();
}

void PluginForgeEditor::cycleControlStyle()
{
    using CS = ParamGridPanel::ControlStyle;
    const auto next = [cur = paramGridPanel.controlStyle()]
    {
        switch (cur)
        {
            case CS::Faithful:   return CS::Rotary;
            case CS::Rotary:     return CS::Horizontal;
            case CS::Horizontal: break;
        }
        return CS::Faithful;
    }();

    // Write through the PROCESSOR, not straight to the panel: the processor owns
    // the stored value and its callback is what applies it here. One direction of
    // data flow, so the button, the state blob and a second editor cannot diverge.
    processor.setUiStyle(ParamGridPanel::controlStyleName(next));
}

void PluginForgeEditor::setCodeViewVisible(bool shouldBeVisible)
{
    if (codeEditorPanel.isVisible() == shouldBeVisible)
        return;

    if (shouldBeVisible)
        codeEditorPanel.showSource(processor.currentSource());

    codeEditorPanel.setVisible(shouldBeVisible);
    codeToggle.setButtonText(shouldBeVisible ? "Hide code" : "Show code");

    // updateWindowSizeForParams already adds Chrome::codeH when the panel is
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
    const int codeH  = codeEditorPanel.isVisible() ? chrome.codeH : 0;
    const int winH   = juce::jmax(kMinWindowH, chromeHeight(chrome) + gridH + codeH);
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

int PluginForgeEditor::gridRefreshCountForTest() const
{
    return paramGridPanel.refreshCountForTest();
}

ParamGridPanel::WidgetKind PluginForgeEditor::gridControlKindForTest(int i) const
{
    return paramGridPanel.controlKindForTest(i);
}

juce::String PluginForgeEditor::gridControlLabelForTest(int i) const
{
    return paramGridPanel.controlLabelForTest(i);
}

juce::String PluginForgeEditor::gridControlGroupForTest(int i) const
{
    return paramGridPanel.controlGroupForTest(i);
}

double PluginForgeEditor::gridControlValueForTest(int i) const
{
    return paramGridPanel.controlValueForTest(i);
}

juce::String PluginForgeEditor::gridControlTextForTest(int i) const
{
    return paramGridPanel.controlTextForTest(i);
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
    g.fillAll(Theme::base);
    g.setColour(juce::Colours::white);
    g.setFont(Theme::Type::title());
    g.drawText("PluginForge", getLocalBounds().removeFromTop(36),
               juce::Justification::centred);

    // ── Output level meter (post-DSP peak) ──────────────────────────────────
    auto track = meterBounds.toFloat();
    g.setColour(Theme::crust);
    g.fillRoundedRectangle(track, 3.0f);

    // Map linear peak → meter fraction with a gentle curve so quiet material
    // still registers; clip at 1.0 (levels above 0dBFS just pin the meter).
    auto frac = juce::jlimit(0.0f, 1.0f, std::pow(displayLevel, 0.5f));
    if (frac > 0.001f)
    {
        auto fill = track.withWidth(track.getWidth() * frac);
        g.setGradientFill(juce::ColourGradient(
            Theme::meterCool, track.getX(), 0.0f,
            Theme::meterHot, track.getRight(), 0.0f,
            false));
        g.fillRoundedRectangle(fill, 3.0f);
    }
}

void PluginForgeEditor::resized()
{
    // Vertical bands. Each panel lays its own widgets out relative to the bounds
    // it is handed here (S2 owns PromptPanel's internal split; S3 owns the grid).
    // Every band comes from `chrome`, which chromeHeight() also sums — so the
    // window arithmetic in updateWindowSizeForParams() cannot drift from what is
    // carved here. Do not reintroduce a literal.
    //
    // The de-duplication that produced `Chrome` claimed zero behaviour change, and
    // nothing else pins it: EditorSessionTest reads a window height that has passed
    // through jmax/jmin clamps, so it cannot isolate the sum. This can, at compile
    // time. When it fires, update the height baselines in the same commit as the
    // band change — do not relax it. (Lives here, not at class scope: `Chrome{}`
    // needs default member initializers the enclosing class has not finished
    // declaring yet.)
    static_assert(chromeHeight(Chrome{}) == 350,
                  "Chrome must still sum to 350 — the pre-refactor kChromeHeight.");

    const auto& c = chrome;

    auto area = getLocalBounds().reduced(c.margin);
    area.removeFromTop(c.titleH);                 // title spacer (title painted full-width)

    promptPanel.setBounds(area.removeFromTop(c.promptH));
    area.removeFromTop(c.gapMeter);
    meterBounds = area.removeFromTop(c.meterH);
    area.removeFromTop(c.gapRow);

    // Disclosure sits directly above whichever region is below it, so it reads as
    // the control for the code band rather than as part of the prompt block.
    {
        auto row = area.removeFromTop(c.rowH);
        codeToggle.setBounds(row.removeFromRight(110));
        row.removeFromRight(6);
        styleToggle.setBounds(row.removeFromRight(120));
        area.removeFromTop(c.gapGrid);
    }

    // Code/Errors region (S2): reserved at the bottom only while the panel is
    // visible. It starts hidden, so the grid keeps the whole remainder today.
    if (codeEditorPanel.isVisible())
        codeEditorPanel.setBounds(area.removeFromBottom(c.codeH));

    // Remaining space is the auto-layout grid.
    paramGridPanel.setBounds(area);
}
