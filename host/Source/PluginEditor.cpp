#include "PluginEditor.h"
#include "Theme.h"

PluginForgeEditor::PluginForgeEditor(PluginForgeProcessor& p)
    : AudioProcessorEditor(&p), processor(p),
      promptPanel(p), codeEditorPanel(p), paramGridPanel(p), keyboardPanel(p)
{
    // setLookAndFeel(&lnf), NOT setDefaultLookAndFeel: the latter is
    // process-global, and this plugin can share a process with the DAW and
    // other instances of itself (session 002 Part B, item B2). Set before any
    // child is added so nothing paints a frame under the stock JUCE
    // LookAndFeel first.
    setLookAndFeel(&lnf);

    // Two-panel authoring screen (Track 1.1). Wider than the pre-split shell so
    // the left preview/grid column has room to breathe next to the right prompt
    // column. Height is the max(right column, grid) + vertical chrome; 500 fits
    // both an empty grid and the fixed 276px right column comfortably.
    setSize(900, 500);

    // Resizable: prompt band and grid both flex, and the code band appears/disappears
    // under the split. Minimum width keeps both columns wide enough to be usable
    // (~380px each after margins + divider); minimum height keeps the fixed right
    // column visible.
    setResizable(true, true);
    setResizeLimits(800, kMinWindowH, 1600, 1200);

    addAndMakeVisible(promptPanel);
    addAndMakeVisible(paramGridPanel);

    // Always in the layout, unlike codeEditorPanel below -- an effect patch's
    // "you can't play this" is communicated by dimming (KeyboardPanel::
    // setPlayable), not by removing the control from the window, so the user
    // is never left wondering whether playing is possible at all.
    addAndMakeVisible(keyboardPanel);

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

    // ── Audition sample selector ────────────────────────────────────────────
    // Populates with .wav files found relative to the executable (dev layout)
    // or in the standard JUCE audio file search paths. "Off" is always item 0.
    auditionSelector.addItem("Audition: Off", 1);

    // Search for .wav files in artifacts/samples/ relative to the executable
    auto exeDir = juce::File::getSpecialLocation(juce::File::currentExecutableFile)
                      .getParentDirectory();
    // Try multiple relative paths (dev layout vs installed)
    const char* sampleDirs[] = {
        "../../artifacts/samples",   // host/build/.../Standalone/ → artifacts/
        "../artifacts/samples",      // host/build/.../ → artifacts/
        "artifacts/samples",         // running from repo root
    };
    juce::StringArray sampleFiles;
    for (const auto* rel : sampleDirs)
    {
        auto sampleDir = exeDir.getChildFile(rel);
        if (sampleDir.isDirectory())
        {
            for (const auto& f : juce::RangedDirectoryIterator(sampleDir, false, "*.wav"))
            {
                const auto name = f.getFile().getFileNameWithoutExtension();
                if (sampleFiles.indexOf(name) < 0)
                    sampleFiles.add(name);
            }
            break;
        }
    }
    sampleFiles.sortNatural();
    int id = 2;
    for (const auto& name : sampleFiles)
        auditionSelector.addItem(name, id++);

    auditionSelector.setSelectedId(1, juce::dontSendNotification);
    auditionSelector.onChange = [this] { auditionSampleChanged(); };
    addAndMakeVisible(auditionSelector);

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
            auto status = juce::String(juce::CharPointer_UTF8("Ready \xe2\x80\x94 DSP live, "))
                          + juce::String(numParams)
                          + (numParams == 1 ? " param mapped." : " params mapped.");
            // Refine was asked for but generate.py had to drop the prior source
            // (token-budget overflow, generate.py:381-386): this generation is a
            // full regen, not a refinement, and the user deserves to know. The
            // flag is published by PromptPanel's worker on the message thread
            // before the JIT compile starts, so it is settled by the time this
            // compile-success callback hops to the message thread.
            if (safeThis->promptPanel.priorSourceDroppedForTest())
                status += "  (prior source dropped — refine became a fresh generation)";
            safeThis->promptPanel.setStatus(status);
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

void PluginForgeEditor::auditionSampleChanged()
{
    const int selectedId = auditionSelector.getSelectedId();
    if (selectedId <= 1)
    {
        // "Off" or nothing selected — stop audition
        processor.setAuditionActive(false);
        return;
    }

    const auto sampleName = auditionSelector.getText();
    // Search for the .wav file in the same directories as the dropdown population
    auto exeDir = juce::File::getSpecialLocation(juce::File::currentExecutableFile)
                      .getParentDirectory();
    const char* sampleDirs[] = {
        "../../artifacts/samples",
        "../artifacts/samples",
        "artifacts/samples",
    };
    for (const auto* rel : sampleDirs)
    {
        auto wavFile = exeDir.getChildFile(rel).getChildFile(sampleName + ".wav");
        if (wavFile.existsAsFile())
        {
            processor.loadAuditionSample(wavFile);
            return;
        }
    }
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
    // Two-panel window height: the split region is the taller of the left grid
    // column (variable) and the right prompt column (fixed rightColumnHeight()).
    // Grid content is capped to kMaxGridRows so the window stays reasonable — past
    // the cap the grid Viewport scrolls. The code band, when visible, is added as
    // a full-width strip below the split, so revealing code always grows the
    // window (the grow-on-show contract scenario 11 pins).
    const int wanted = paramGridPanel.preferredContentHeight();
    const int capH   = kMaxGridRows * ParamGridPanel::kCellH;
    const int gridH  = juce::jmin(wanted, capH);
    const int splitH = juce::jmax(gridH, rightColumnHeight(chrome));
    const int codeH  = codeEditorPanel.isVisible() ? (chrome.gapCode + chrome.codeH) : 0;
    const int winH   = juce::jmax(kMinWindowH, verticalChrome(chrome) + splitH + codeH);
    setSize(getWidth(), winH);   // synchronously triggers resized() below
}

PluginForgeEditor::~PluginForgeEditor()
{
    // MUST be first: ~LookAndFeel() asserts if any Component still points at
    // it, and this editor's own children still do until this line runs
    // (session 002 Part B, item B2; docs/sessions/002-handoff-README.md).
    setLookAndFeel(nullptr);
}

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

    // Same 30Hz tick, no second timer (fact #7 / PluginProcessor.h's
    // isInstrumentForTest() comment): enable the keyboard only for a patch
    // that actually declares a voice contract. setPlayable() is idempotent, so
    // polling it unconditionally here costs nothing when nothing changed.
    keyboardPanel.setPlayable(processor.isInstrumentForTest());

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

    // Dev-cockpit state export: every third tick (~10Hz), and only while armed —
    // setCockpitStatePath() must have been called with a non-empty path. Default
    // OFF, so tests and headless builds never write /tmp/pluginforge_state.json
    // unless a caller opted in. Written from the message thread, never from
    // audio. Fail-open: a write error does not interrupt the UI tick.
    if (cockpitEnabled && !cockpitStatePath.isEmpty())
    {
        if (++cockpitStateTick >= 3)
        {
            cockpitStateTick = 0;
            writeCockpitState();
        }
    }
}

void PluginForgeEditor::writeCockpitState()
{
    // Self-protecting: the timer gates on cockpitEnabled + a non-empty path, but
    // a direct call must not write to an empty path — juce::File("") resolves to
    // the current directory and replaceWithText would fail into it.
    if (!cockpitEnabled || cockpitStatePath.isEmpty())
        return;

    auto root = std::make_unique<juce::DynamicObject>();
    root->setProperty("status", statusTextForTest());
    root->setProperty("error", errorTextForTest());
    root->setProperty("controlStyle", controlStyleForTest());
    root->setProperty("styleButton", styleButtonTextForTest());
    root->setProperty("codeVisible", codeVisibleForTest());
    root->setProperty("codeReadOnly", codeIsReadOnlyForTest());
    root->setProperty("keyboardPlayable", keyboardPlayableForTest());
    root->setProperty("outputLevel", displayLevel);
    root->setProperty("outputMuted", processor.isOutputMuted());
    root->setProperty("isInstrument", processor.isInstrumentForTest());
    root->setProperty("sourceChars", processor.currentSource().length());
    root->setProperty("windowWidth", getWidth());
    root->setProperty("windowHeight", getHeight());

    juce::Array<juce::var> controls;
    const int n = gridControlCountForTest();
    for (int i = 0; i < n; ++i)
    {
        auto control = std::make_unique<juce::DynamicObject>();
        control->setProperty("index", i);
        control->setProperty("label", gridControlLabelForTest(i));
        control->setProperty("group", gridControlGroupForTest(i));
        control->setProperty("value", gridControlValueForTest(i));
        control->setProperty("text", gridControlTextForTest(i));
        control->setProperty("kind", static_cast<int>(gridControlKindForTest(i)));
        controls.add(juce::var(control.release()));
    }
    root->setProperty("controls", controls);

    const auto json = juce::JSON::toString(juce::var(root.release()), true);
    juce::File(cockpitStatePath).replaceWithText(json, false, false, "\n");
}

void PluginForgeEditor::paint(juce::Graphics& g)
{
    g.fillAll(Theme::base);
    g.setColour(juce::Colours::white);
    g.setFont(Theme::Type::title());
    // Title lives in the top-margin+title band so it reads as a shell header, not
    // as part of either column. chrome.titleH matches the reservation in resized().
    g.drawText("PluginForge",
               getLocalBounds().removeFromTop(chrome.margin + chrome.titleH),
               juce::Justification::centred);

    // Divider between the left (grid) and right (prompt) columns. Drawn as a thin
    // line down the middle of the dividerW gap. Set in resized().
    if (dividerX > 0)
    {
        g.setColour(Theme::crust);
        g.fillRect(juce::Rectangle<int>(dividerX, chrome.margin + chrome.titleH,
                                        1, getHeight() - (chrome.margin + chrome.titleH)
                                              - chrome.margin - chrome.gapKeyboard
                                              - chrome.keyboardH));
    }

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
    // Two-panel authoring screen. The window is a full-width title bar, a split
    // region (left preview/grid column | right prompt column), an optional full-
    // width code band, and a full-width keyboard band at the bottom. Every band
    // comes from `chrome`; updateWindowSizeForParams() sums the same numbers via
    // rightColumnHeight()/verticalChrome(), so window arithmetic cannot drift from
    // what is carved here. Do not reintroduce a literal.
    //
    // The two derived sums replace the pre-split single chromeHeight() pin. When
    // one fires, update the height baselines in the same commit as the band change
    // — do not relax it. (Lives here, not at class scope: `Chrome{}` needs default
    // member initializers the enclosing class has not finished declaring yet.)
    static_assert(rightColumnHeight(Chrome{}) == 276,
                  "Right column: promptH(220) + gapMeter(8) + meterH(14) "
                  "+ gapRow(10) + rowH(24) = 276.");
    static_assert(verticalChrome(Chrome{}) == 136,
                  "Vertical chrome: margin(16) + titleH(32) + gapKeyboard(8) "
                  "+ keyboardH(64) + margin(16) = 136.");

    const auto& c = chrome;

    auto area = getLocalBounds().reduced(c.margin);
    area.removeFromTop(c.titleH);                 // full-width title spacer

    // Keyboard: full-width band at the bottom, ALWAYS laid out (see the
    // addAndMakeVisible comment in the constructor) so an effect patch's dimmed
    // keyboard is still visible rather than absent.
    auto keyboardArea = area.removeFromBottom(c.keyboardH);
    keyboardPanel.setBounds(keyboardArea);
    area.removeFromBottom(c.gapKeyboard);

    // Code editor: full-width band above the keyboard, only while visible.
    // Placing it here (rather than inside the left column, as the plan diagram
    // suggested) preserves scenario 11's grow-on-show contract even when the right
    // prompt column would otherwise absorb the code band's height.
    if (codeEditorPanel.isVisible())
    {
        codeEditorPanel.setBounds(area.removeFromBottom(c.codeH));
        area.removeFromBottom(c.gapCode);
    }

    // Split what remains into left (grid) and right (prompt) columns.
    const int splitW = area.getWidth();
    const int leftW  = juce::jmax(0, juce::roundToInt((splitW - c.dividerW) * kLeftFraction));
    auto leftCol  = area.removeFromLeft(leftW);
    area.removeFromLeft(c.dividerW);              // divider gap
    auto rightCol = area;                          // remainder

    // Record the divider x (in window coords) for paint() to draw the seam.
    dividerX = leftCol.getRight() + c.dividerW / 2;

    // Left column: the whole thing is the grid. Section titles (Track 1.2) paint
    // inside the panel; no dedicated header band is carved here.
    paramGridPanel.setBounds(leftCol);

    // Right column, top to bottom: prompt → meter → disclosure row. Any vertical
    // slack (the column is taller than rightColumnHeight when the left grid is
    // tall) falls between the disclosure row and the bottom edge.
    promptPanel.setBounds(rightCol.removeFromTop(c.promptH));
    rightCol.removeFromTop(c.gapMeter);
    meterBounds = rightCol.removeFromTop(c.meterH);
    rightCol.removeFromTop(c.gapRow);

    auto row = rightCol.removeFromTop(c.rowH);
    codeToggle.setBounds(row.removeFromRight(110));
    row.removeFromRight(6);
    styleToggle.setBounds(row.removeFromRight(120));
    row.removeFromRight(6);
    auditionSelector.setBounds(row.removeFromLeft(juce::jmin(160, row.getWidth())));
}
