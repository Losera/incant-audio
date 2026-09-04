#include "PluginEditor.h"
#include "Theme.h"

namespace
{
// Faust's own diagnostic format, confirmed against a live compile error
// (FaustEngine.cpp:799 passes the literal filename "dsp" to
// createDSPFactoryFromString): "dsp<optional space>:<optional space><line>"
// appears once, near the start of the message. The spacing is NOT fixed
// across Faust versions -- this machine's Faust 2.85.9 emits "dsp:2 : ERROR
// : syntax error, unexpected IDENT" (no space before the first colon), while
// CI's installed Faust emits "dsp : 2 : ERROR : syntax error" (a space
// before it), discovered when this exact parse silently returned 0 in CI
// (PR #4's build-host run) despite passing locally -- so both spacings are
// tolerated rather than the one this machine happens to produce. Not every
// failure carries a line at all -- e.g. "the Faust program has no output"
// (FaustEngine.cpp:393) -- so this returns 0 (no line) rather than guessing,
// and CodeEditorPanel::highlightErrorLine() treats <= 0 as a no-op.
int parseFaustErrorLine(const juce::String& error)
{
    int i = error.indexOf("dsp");
    if (i < 0)
        return 0;
    i += 3;

    while (i < error.length() && error[i] == ' ')
        ++i;
    if (i >= error.length() || error[i] != ':')
        return 0;
    ++i;
    while (i < error.length() && error[i] == ' ')
        ++i;

    int line = 0;
    bool sawDigit = false;
    while (i < error.length() && juce::CharacterFunctions::isDigit(error[i]))
    {
        line = line * 10 + (error[i] - '0');
        sawDigit = true;
        ++i;
    }
    return sawDigit ? line : 0;
}
}

PluginForgeEditor::PluginForgeEditor(PluginForgeProcessor& p)
    : AudioProcessorEditor(&p), processor(p),
      promptPanel(p), codeEditorPanel(p), paramGridPanel(p), keyboardPanel(p),
      sampleBrowserPanel([&p](const juce::File& file) { p.loadAuditionSample(file); },
                         [&p](int mode) { p.setAuditionMode(
                             static_cast<PluginForgeProcessor::AuditionMode>(mode)); })
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
    // at the 65/35 split (432 grid + 232 prompt at kMinWindowW, after margins +
    // divider); minimum height keeps the fixed right column visible.
    setResizable(true, true);
    setResizeLimits(kMinWindowW, kMinWindowH, 1600, 1200);

    addAndMakeVisible(promptPanel);
    addAndMakeVisible(paramGridPanel);

    // Conditional band since 2026-08-24 (item 1 of the generated-plugin-identity
    // work): laid out only for a patch that has actually declared a voice
    // contract, gated on the exact same FaustEngine::isInstrument() boolean
    // that already drives KeyboardPanel::setPlayable()'s dimming, below and in
    // resized()/updateWindowSizeForParams(). This retires the older design --
    // "always show it dimmed so nobody wonders if playing is possible" -- which
    // made sense for one shell hosting every patch, but stops making sense once
    // a generated effect is exported as its own standalone plugin: a dimmed
    // keyboard on a reverb is 80px of dead chrome explaining a synth case that
    // plugin will never hit. addChildComponent (not addAndMakeVisible): starts
    // invisible, matching voiceValid's own default-false until a patch compiles
    // (FaustEngine.h:338); resized() sets the real visibility every layout pass.
    addChildComponent(keyboardPanel);

    // Sample browser stays unconditional -- NOT gated the same way. It feeds
    // the audition input bus for auditioning a generated EFFECT against a real
    // source (PluginProcessor.h's "Sample audition" block, loadAuditionSample),
    // which is the opposite of the instrument case the keyboard boolean tracks.
    // Gating both bands on one boolean would hide the sample browser for
    // exactly the patches it exists to serve.
    addAndMakeVisible(sampleBrowserPanel);

    // The read-only Faust view (ux_roadmap Phase 3a). Still an invisible child
    // with no layout space UNTIL the user asks for it: this is a no-code tool and
    // must not open on a wall of DSL. The band it takes when shown (Chrome::codeH)
    // has been reserved in resized() and updateWindowSizeForParams() since the
    // Task-0 split — the plumbing was finished long before the panel was.
    addChildComponent(codeEditorPanel);
    addChildComponent(recommendationPanel);

    promptPanel.onRecommendationReady = [safeThis = juce::Component::SafePointer<PluginForgeEditor>(this)]
        (const juce::var& plan, const juce::String& provider, const juce::String& model)
    {
        if (safeThis != nullptr)
            safeThis->recommendationPanel.setRecommendation(plan, provider, model);
    };
    promptPanel.onRecommendationFailure = [safeThis = juce::Component::SafePointer<PluginForgeEditor>(this)]
        (const juce::String& message, bool allowBypass, bool targetMismatch)
    {
        if (safeThis != nullptr)
            safeThis->recommendationPanel.setFailure(message, allowBypass, targetMismatch);
    };
    promptPanel.onRecommendationInvalidated = [safeThis = juce::Component::SafePointer<PluginForgeEditor>(this)]
    {
        if (safeThis != nullptr) safeThis->recommendationPanel.markStale();
    };
    recommendationPanel.onGenerate = [safeThis = juce::Component::SafePointer<PluginForgeEditor>(this)]
        (const juce::var& plan, const juce::String& provider, const juce::String& model)
    {
        if (safeThis != nullptr)
            safeThis->promptPanel.generateFromRecommendation(plan, provider, model);
    };
    recommendationPanel.onRetry = [safeThis = juce::Component::SafePointer<PluginForgeEditor>(this)]
    {
        if (safeThis != nullptr) safeThis->promptPanel.retryRecommendation();
    };
    recommendationPanel.onGenerateDirect = [safeThis = juce::Component::SafePointer<PluginForgeEditor>(this)]
    {
        if (safeThis != nullptr) safeThis->promptPanel.generateDirect();
    };
    recommendationPanel.onVisibilityChanged = [safeThis = juce::Component::SafePointer<PluginForgeEditor>(this)](bool)
    {
        if (safeThis == nullptr) return;
        safeThis->updateWindowSizeForParams();
        safeThis->resized();
    };

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

    // Dev-cockpit mirror (session 010 §7), armed 2026-08-12. OFF by default --
    // setCockpitStatePath() previously had no caller anywhere in the repo, so
    // cockpitEnabled was always false and dev-cockpit/server.py's /api/state
    // could only ever 503. Opt in with PLUGINFORGE_COCKPIT_STATE set to the path
    // server.py reads (its STATE_FILE, hardcoded to /tmp/pluginforge_state.json)
    // to enable the read-only mirror the /cockpit skill expects.
    auto cockpitPath = juce::SystemStats::getEnvironmentVariable("PLUGINFORGE_COCKPIT_STATE", {});
    if (cockpitPath.isNotEmpty())
        setCockpitStatePath(cockpitPath);

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
    processor.onFaustCompileFailure = [safeThis](const juce::String& error,
                                                  const juce::String& attemptedSource)
    {
        juce::MessageManager::callAsync([safeThis, error, attemptedSource]
        {
            if (safeThis == nullptr) return;
            safeThis->promptPanel.setStatus(
                "Faust compile error: " + error.substring(0, 200));
            // Full, untruncated stderr (C6) -- PromptPanel.h's setError() has
            // documented "the shell can route Faust-compiler stderr here
            // instead of truncating into the status label" since before this
            // call existed; this is what actually wires it.
            safeThis->promptPanel.setError(error);
            // Show what was actually ATTEMPTED, not the source of record --
            // PF-022 means currentSource() still holds the last SUCCESSFUL
            // compile, so without this the code view would keep showing old,
            // unrelated code while highlightErrorLine() pointed at an
            // arbitrary line inside it. attemptedSource is the same string
            // FaustEngine just failed to compile, threaded through
            // onFaustCompileFailure's second parameter for exactly this.
            safeThis->codeEditorPanel.showSource(attemptedSource);
            safeThis->codeEditorPanel.highlightErrorLine(parseFaustErrorLine(error));
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
            // ADR-022 Track 1.2: derive a sectioned layout purely from Faust
            // group nesting already present in `params` -- no prompt change,
            // no LLM involvement. Called unconditionally; deriveLayoutFromGroups
            // itself leaves `sections` empty when sectioning would not help,
            // and applyUiIr's own "sections.empty()" branch is already the
            // "render the flat grid" path (ADR-029 §4 -- see that function's
            // comment for why this moved off ir.schema != 1), so an ungrouped
            // or sparse patch is unaffected byte-for-byte.
            const auto derivedLayout = ParamGridPanel::deriveLayoutFromGroups(
                params, safeThis->processor.isInstrumentForTest());
            safeThis->paramGridPanel.applyUiIr(derivedLayout);
            // ADR-035 Step 3: dress paramGridPanel in the layout's theme. Today
            // deriveLayoutFromGroups() only ever produces the Ember default, so
            // this is a no-op detach in production until the ui_face producer
            // (#59) feeds a real theme in here; the machinery and its
            // EditorSessionTest scenario are what this step lands.
            safeThis->applyGeneratedFace(derivedLayout.theme);
            // Hand the layout to the processor so it rides the state blob
            // (UiIr schema 3, Step 1). Persistence only -- the restore path
            // does not feed this back into applyUiIr() yet; this callback
            // re-derives from `params` on every compile, restore included.
            safeThis->processor.setUiIr(derivedLayout);
            // The source of record is committed in the same success branch that
            // fires this callback (PluginProcessor.cpp:180-181), so by the time
            // this message-thread hop runs, currentSource() is the patch that
            // just went live. Pushed unconditionally, even while the view is
            // hidden, so revealing it is instant and never shows a stale patch.
            safeThis->codeEditorPanel.showSource(safeThis->processor.currentSource());
            // Same "source of record already committed" reasoning as showSource()
            // above: a patch just went live, so Add/Redo now have something to
            // add to or redo from. The panel's OWN constructor already seeds this
            // from processor.currentSource() (covers a DAW project load, which
            // completes before this callback is even wired up) — this call is
            // what unlocks the modes within a session, after the FIRST generation.
            safeThis->promptPanel.setRefineModesAvailable(
                safeThis->processor.currentSource().isNotEmpty());
            safeThis->updateWindowSizeForParams();
            // Belt-and-suspenders with the line above, same reason
            // setCodeViewVisible() pairs the two calls: JUCE's setSize() only
            // triggers resized() when width or height actually CHANGE, and an
            // instrument <-> effect transition does not guarantee that -- the
            // grid's own height can coincidentally absorb the keyboard band's
            // 80px. Found live: recompiling an instrument straight after
            // another compile could leave keyboardPanel invisible (stale from
            // the previous resized() call) even though this patch IS an
            // instrument, and focusForPlaying() below would then grab focus on
            // a non-showing component -- a real jassert in
            // juce_Component.cpp's grabKeyboardFocus(), not a hypothetical
            // one. An explicit call makes keyboardPanel's visibility and
            // bounds authoritative regardless of whether setSize() no-opped.
            safeThis->resized();

            // Run setPlayable() HERE rather than waiting for the next 30Hz
            // timerCallback tick: focusForPlaying() below needs the widget
            // already enabled, since JUCE does not grant keyboard focus to a
            // disabled component, and the timer could still be a frame away.
            // Idempotent (KeyboardPanel::setPlayable's own guard), so this
            // and the timer's regular poll never fight each other.
            const bool instrument = safeThis->processor.isInstrumentForTest();
            safeThis->keyboardPanel.setPlayable(instrument);
            if (instrument)
                // "Click the piano first" for the single most common case --
                // a synth just finished generating -- rather than only via
                // the general keyStateChanged forwarding below, which still
                // depends on where focus already happens to be (see that
                // override's own comment for the fuller story).
                safeThis->keyboardPanel.focusForPlaying();
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
    const int recommendationH = recommendationPanel.isVisible()
                                  ? (chrome.gapRecommendation + chrome.recommendationH) : 0;
    // Same voice-contract boolean as resized()'s keyboard band and
    // KeyboardPanel::setPlayable() -- an effect patch's window does not
    // reserve gapKeyboard+keyboardH for a band it will not lay out.
    const bool instrument = processor.isInstrumentForTest();
    const int winH   = juce::jmax(kMinWindowH,
                                   verticalChrome(chrome, instrument) + splitH + codeH
                                       + recommendationH);
    setSize(getWidth(), winH);   // synchronously triggers resized() below
}

PluginForgeEditor::~PluginForgeEditor()
{
    // MUST be first: ~LookAndFeel() asserts if any Component still points at
    // it, and this editor's own children still do until this line runs
    // (session 002 Part B, item B2; docs/sessions/002-handoff-README.md).
    //
    // paramGridPanel points at `faceLnf` (not `lnf`) whenever a generated face
    // is active — detach it here too. `faceLnf` is declared before
    // `paramGridPanel` so reverse-order destruction would tear the panel down
    // first regardless, but the panel also gets `setLookAndFeel(&*faceLnf)`
    // mid-life in applyGeneratedFace(), and being explicit at both ends keeps
    // the one contract in one shape (ADR-035 Step 3).
    paramGridPanel.setLookAndFeel(nullptr);
    setLookAndFeel(nullptr);
}

// ADR-035 Step 3. See the header for the contract; the lifetime reasoning is in
// GeneratedFaceLookAndFeel.h. Detach-then-rebuild order matters: the panel must
// never hold a WeakReference to a GeneratedFaceLookAndFeel that is about to be
// freed, so setLookAndFeel(nullptr) precedes faceLnf.reset() every time.
void PluginForgeEditor::applyGeneratedFace(const UiIr::Theme& theme)
{
    paramGridPanel.setLookAndFeel(nullptr);
    faceLnf.reset();

    if (theme != UiIr::Theme {})
    {
        faceLnf = std::make_unique<GeneratedFaceLookAndFeel>(theme);
        paramGridPanel.setLookAndFeel(faceLnf.get());
    }
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

juce::String PluginForgeEditor::gridControlStyleForTest(int i) const
{
    return paramGridPanel.controlStyleForTest(i);
}

juce::String PluginForgeEditor::gridControlOrientationForTest(int i) const
{
    return paramGridPanel.controlOrientationForTest(i);
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

bool PluginForgeEditor::keyStateChanged(bool isKeyDown)
{
    // A TextEditor swallows this walk only for KEY-DOWN transitions
    // (juce_TextEditor.cpp:2189-2205: "if (! isKeyDown) return false;" is the
    // first line of its own override) -- key-UP always propagates past it
    // while it holds focus. But MidiKeyboardComponent::keyStateChanged
    // ignores its own isKeyDown parameter entirely and re-polls EVERY mapped
    // key's live isCurrentlyDown() state on each call
    // (juce_MidiKeyboardComponent.cpp:254-283), firing noteOn/noteOff for
    // whichever ones changed relative to its own bookkeeping. So a key-up
    // from releasing one letter while typing in the prompt box can catch
    // ANOTHER letter the user is still physically holding (ordinary
    // fast-typing rollover) that was never registered as down -- its own
    // key-down was swallowed -- and fire a spurious noteOn for it. Guard
    // explicitly rather than relying on TextEditor's asymmetric return
    // value: skip forwarding entirely while a TextEditor -- the prompt box
    // or the sample-search query box, both juce::TextEditor -- holds focus.
    // A note legitimately held before focus moved to a text box is not left
    // permanently stuck by this: MidiKeyboardComponent's own poll (not this
    // override) is what notices a physical key is no longer down, and that
    // poll still runs on the next keyStateChanged call this editor forwards
    // once focus moves away from the text box.
    if (isTextEditorFocusTarget(juce::Component::getCurrentlyFocusedComponent()))
        return false;
    return keyboardPanel.routeKeyStateChanged(isKeyDown);
}

bool PluginForgeEditor::keyPressed(const juce::KeyPress& key)
{
    // Ctrl+Shift+C toggles the read-only code view (C6). A one-shot
    // press/release chord -- juce::Component::keyPressed, a DIFFERENT virtual
    // from keyStateChanged above (continuous held-state polling, what C4's
    // seam answers). They serve different purposes and are deliberately not
    // unified into one override.
    //
    // Fires even while the prompt box holds focus. Verified against primary
    // source, not assumed: juce::TextEditor overrides keyPressed too
    // (juce_TextEditor.cpp:2141), but for Ctrl+Shift+C it consumes nothing --
    // TextEditorKeyMapper::invokeKeyFunction (juce_TextEditorKeyMapper.h:38-
    // 107) only matches 'c' against ModifierKeys::commandModifier (plain
    // Ctrl+C, for copy) via KeyPress's exact-modifier-set equality, never
    // commandModifier|shiftModifier; and TextEditor::keyPressed's own
    // fallback only inserts a character when getTextCharacter() is printable
    // (juce_TextEditor.cpp:2170), which a Ctrl-held key is not. So
    // TextEditor::keyPressed returns false for this chord, and JUCE's own
    // dispatch walk -- ComponentPeer::handleKeyPress climbing
    // getParentComponent() on a false return, juce_ComponentPeer.cpp:185-189,
    // the same walk C4's comment cites for keyStateChanged -- reaches this
    // override without this editor needing to compete with typing.
    if (key == juce::KeyPress('c', juce::ModifierKeys::ctrlModifier | juce::ModifierKeys::shiftModifier, 0))
    {
        setCodeViewVisible(! codeEditorPanel.isVisible());
        return true;
    }
    return false;
}

void PluginForgeEditor::paint(juce::Graphics& g)
{
    g.fillAll(Theme::background);
    g.setColour(Theme::textPrimary);
    // resolveThemeFont, not Theme::Type::title() directly -- see
    // ForgeLookAndFeel.h's header comment on that function for why a
    // name-based Font alone never actually resolves to the embedded Pirata
    // One. Safe here: paint() only ever runs once this component is fully
    // wired into the editor's tree.
    g.setFont(resolveThemeFont(*this, Theme::Type::title()));
    // Title lives in the top-margin+title band so it reads as a shell header, not
    // as part of either column. Left-aligned (was centred) since 2026-08-12: the
    // disclosure row now shares this band on the right (titleTextBounds, set in
    // resized()), and centring the text would risk it drifting under those
    // controls as the window resizes.
    // ADR-029 §5: a name derived from this compile's (params, isInstrument)
    // hash, the same inputs derivePalette() already hashes for the accent --
    // replaces the literal "PluginForge" so the title agrees with the accent
    // colour rather than naming the engine regardless of what was generated.
    g.drawText(paramGridPanel.getGeneratedTitle(), titleTextBounds,
                juce::Justification::centredLeft);

    // Divider between the left (grid) and right (prompt) columns. Drawn as a thin
    // line down the middle of the dividerW gap. Set in resized().
    //
    // Bottom subtraction must track the SAME instrument conditional resized()
    // and updateWindowSizeForParams() use: when the keyboard band is not laid
    // out, getHeight() no longer includes gapKeyboard+keyboardH at all, so
    // subtracting them unconditionally here would shorten the line by that
    // same amount a second time -- found while wiring the conditional band,
    // 2026-08-24, not previously reachable because the band was unconditional.
    if (dividerX > 0)
    {
        // Theme::outline, not surfaceSunken: this is a hairline meant to be seen
        // (same convention ParamGridPanel.cpp's section underlines and
        // KeyboardPanel's key separators already use -- outline/background is a
        // real 1.74:1, where surfaceSunken/background was 1.03:1, functionally
        // invisible). This was the one structural divider in the shell drawn
        // against the wrong token; found during the 2026-08-25 palette pass.
        g.setColour(Theme::outline);
        const bool instrument = processor.isInstrumentForTest();
        const int bottomChrome = chrome.margin
                                + (instrument ? (chrome.gapKeyboard + chrome.keyboardH) : 0);
        g.fillRect(juce::Rectangle<int>(dividerX, chrome.margin + chrome.titleH,
                                        1, getHeight() - (chrome.margin + chrome.titleH)
                                              - bottomChrome));
    }

    // ── Output level meter (post-DSP peak) ──────────────────────────────────
    auto track = meterBounds.toFloat();
    g.setColour(Theme::surfaceSunken);
    g.fillRoundedRectangle(track, 3.0f);

    // Map linear peak → meter fraction with a gentle curve so quiet material
    // still registers; clip at 1.0 (levels above 0dBFS just pin the meter).
    auto frac = juce::jlimit(0.0f, 1.0f, std::pow(displayLevel, 0.5f));
    if (frac > 0.001f)
    {
        auto fill = track.withWidth(track.getWidth() * frac);
        g.setGradientFill(juce::ColourGradient(
            Theme::accent, track.getX(), 0.0f,
            Theme::meterHot, track.getRight(), 0.0f,
            false));
        g.fillRoundedRectangle(fill, 3.0f);
    }
}

void PluginForgeEditor::resized()
{
    // Two-panel authoring screen. The window is a full-width title bar (title
    // text left, disclosure row right — moved here 2026-08-12, see Chrome::
    // promptH's comment), a split region (left preview/grid column | right
    // prompt column), an optional full-width code band, a full-width sample-
    // browser band, and a full-width keyboard band at the bottom. Every band
    // comes from `chrome`;
    // updateWindowSizeForParams() sums the same numbers via
    // rightColumnHeight()/verticalChrome(), so window arithmetic cannot drift from
    // what is carved here. Do not reintroduce a literal.
    //
    // The two derived sums replace the pre-split single chromeHeight() pin. When
    // one fires, update the height baselines in the same commit as the band change
    // — do not relax it. (Lives here, not at class scope: `Chrome{}` needs default
    // member initializers the enclosing class has not finished declaring yet.)
    static_assert(rightColumnHeight(Chrome{}) == 276,
                  "Right column: promptH(254) + gapMeter(8) + meterH(14) = 276. "
                  "promptH went 220 -> 254 when ADR-032 v1 added the provider/model row.");
    static_assert(verticalChrome(Chrome{}) == 216,
                  "Vertical chrome: margin(16) + titleH(32) + gapSamples(8) "
                  "+ samplesH(64) + gapKeyboard(8) + keyboardH(72) + margin(16) = 216.");

    const auto& c = chrome;

    auto area = getLocalBounds().reduced(c.margin);
    auto titleArea = area.removeFromTop(c.titleH);

    // Disclosure row: claims the right side of the title band, same left-to-
    // right order and widths it had as its own row before the 65/35 split left
    // the right column too narrow for it (see Chrome::promptH's comment).
    // Whatever remains on the left is the title text's bounds, read by paint().
    codeToggle.setBounds(titleArea.removeFromRight(110));
    titleArea.removeFromRight(6);
    styleToggle.setBounds(titleArea.removeFromRight(120));
    titleArea.removeFromRight(6);
    titleTextBounds = titleArea;

    // Keyboard: full-width band at the bottom, laid out only for a patch that
    // has declared a voice contract (see the addChildComponent comment in the
    // constructor) -- the same FaustEngine::isInstrument() boolean
    // updateWindowSizeForParams() already reads for the window-height sum
    // above, so the two cannot disagree about whether the band exists.
    const bool instrument = processor.isInstrumentForTest();
    keyboardPanel.setVisible(instrument);
    if (instrument)
    {
        auto keyboardArea = area.removeFromBottom(c.keyboardH);
        keyboardPanel.setBounds(keyboardArea);
        area.removeFromBottom(c.gapKeyboard);
    }

    sampleBrowserPanel.setBounds(area.removeFromBottom(c.samplesH));
    area.removeFromBottom(c.gapSamples);

    // Code editor: full-width band above the keyboard, only while visible.
    // Placing it here (rather than inside the left column, as the plan diagram
    // suggested) preserves scenario 11's grow-on-show contract even when the right
    // prompt column would otherwise absorb the code band's height.
    if (codeEditorPanel.isVisible())
    {
        codeEditorPanel.setBounds(area.removeFromBottom(c.codeH));
        area.removeFromBottom(c.gapCode);
    }

    if (recommendationPanel.isVisible())
    {
        recommendationPanel.setBounds(area.removeFromBottom(c.recommendationH));
        area.removeFromBottom(c.gapRecommendation);
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

    // Right column, top to bottom: prompt → meter. Any vertical slack (the
    // column is taller than rightColumnHeight when the left grid is tall) falls
    // below the meter.
    promptPanel.setBounds(rightCol.removeFromTop(c.promptH));
    rightCol.removeFromTop(c.gapMeter);
    meterBounds = rightCol.removeFromTop(c.meterH);
}
