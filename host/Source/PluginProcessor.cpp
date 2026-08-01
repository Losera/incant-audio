#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "ParamMap.h"   // mapZoneToSlot, for LoadMode::Fresh (PF-020)

// SUBTLE: the input bus is REQUIRED for the effect target and OPTIONAL for the
// instrument target, and that single boolean is what decides whether a DAW will
// instantiate this on an instrument track at all. A synth has nothing to
// process; demanding a stereo input means the host must find something to feed
// it, and most will simply refuse to load the plugin there.
//
// The bus stays *declared* on the instrument rather than being removed, so a
// generated patch that does take input (a 1-in/2-out voice, which the arity
// gate permits) still works when the host happens to supply one. FaustEngine's
// arity routing already handles the 0-in case by computing into scratch, so
// nothing downstream cares whether the input is connected.
PluginForgeProcessor::PluginForgeProcessor()
    : AudioProcessor(BusesProperties()
          .withInput ("Input",  juce::AudioChannelSet::stereo(), PF_IS_SYNTH == 0)
          .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "STATE", createParameterLayout()),
      paramPool(apvts)
{
}

PluginForgeProcessor::~PluginForgeProcessor()
{
    // Stop and join the compile worker FIRST, while paramPool and the
    // onFaustCompile* handlers it calls into are still alive. Members are
    // destroyed in reverse declaration order and faustEngine is declared before
    // paramPool, so ~FaustEngine would otherwise run after ~ParamPool — with a
    // compile potentially still in flight calling remap() on a destroyed pool.
    faustEngine.shutdown();
}

juce::AudioProcessorValueTreeState::ParameterLayout
PluginForgeProcessor::createParameterLayout()
{
    // Params are created here (not in ParamPool) because the apvts constructor
    // needs the full ParameterLayout up front -- see ParamPool.cpp's constructor
    // comment for what went wrong when ParamPool tried to add them afterward via
    // the legacy createAndAddParameter(). ParamPool::slotId() is the single shared
    // ID scheme both sides use.
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;
    params.reserve(static_cast<size_t>(ParamPool::POOL_SIZE));

    for (int i = 0; i < ParamPool::POOL_SIZE; ++i)
    {
        // ⚠️ The NormalisableRange is EXPLICIT, and must stay explicit (PF-040).
        // The convenience overload that takes bare min/max/default is
        //     AudioParameterFloat (pid, nm, { minValue, maxValue, 0.01f }, def)
        // -- juce_AudioParameterFloat.cpp:76, which hardcodes an interval of
        // 0.01. Every slot therefore had exactly 100 positions, so a patch's
        // declared default usually could not be represented: an 800 Hz cutoff
        // on a 20..20000 control needs slot 0.039039 and got 0.04, i.e. 819 Hz.
        // The two-argument NormalisableRange leaves interval at 0
        // (juce_NormalisableRange.h:63-66), which is continuous.
        //
        // This was invisible for the project's whole history because the knobs
        // displayed the raw slot; it surfaced the moment the readout started
        // showing real units (PF-037) and a test asserted the default.
        params.push_back(std::make_unique<juce::AudioParameterFloat>(
            ParamPool::slotId(i),               // paramID (stable — DAW automation uses this)
            "Macro " + juce::String(i + 1),     // display name
            juce::NormalisableRange<float>(0.0f, 1.0f),
            0.0f
        ));
    }

    return { params.begin(), params.end() };
}

void PluginForgeProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    faustEngine.prepare(sampleRate, samplesPerBlock);
    outputGuard.prepare(sampleRate);

    // A note held across a re-prepare would otherwise stay held forever: the gate
    // zone keeps its 1 and only MIDI can clear it, but the events that would have
    // done so are long gone. Hosts call prepareToPlay on sample-rate and
    // block-size changes, which can happen mid-performance.
    faustEngine.silenceVoice();

    // If a session was restored before the host told us the sample rate (setState
    // can precede prepareToPlay), the restore recompile was deferred to here so the
    // DSP JITs at the real rate rather than the 44100 default. Fire it now.
    prepared.store(true, std::memory_order_release);

    juce::String source, prompt;
    {
        std::lock_guard<std::mutex> lock(metaMutex);
        source = pendingRestoreSource;
        prompt = pendingRestorePrompt;
        pendingRestoreSource.clear();
        pendingRestorePrompt.clear();
    }
    // Iterate, NOT Fresh: setStateInformation already wrote the saved macro
    // values via replaceState(). Resetting them to patch defaults here would
    // discard exactly what the restore just recovered (PF-020).
    if (source.isNotEmpty())
        loadFaustCode(source, prompt, LoadMode::Iterate);
}

void PluginForgeProcessor::releaseResources()
{
    faustEngine.release();
}

bool PluginForgeProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    const auto in  = layouts.getMainInputChannelSet();

    // Output is mandatory and capped at FaustEngine::kMaxChannels. A patch may
    // declare 1 or 2 outputs (FaustEngine.cpp's arity gate), and process() fans a
    // mono patch out to whatever the host gave us -- but it cannot serve more
    // channels than `scratch` is sized for.
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;

    // A disabled input is the instrument case, and ONLY the instrument case. The
    // Fx target declares its input required; a host disabling it would leave
    // every effect patch processing silence.
    if (in.isDisabled())
        return PF_IS_SYNTH != 0;

    return in == juce::AudioChannelSet::mono() || in == juce::AudioChannelSet::stereo();
}

void PluginForgeProcessor::reset()
{
    // Only the note is cleared. The DSP's internal state (filter memory, delay
    // lines) is deliberately left alone: FaustEngine has no per-patch reset that
    // does not also re-init the instance, and re-initialising here would discard
    // the tail the host is asking us to stop, not stop it cleanly.
    faustEngine.silenceVoice();
}

void PluginForgeProcessor::processBlock(juce::AudioBuffer<float>& buffer,
                                         juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;

    // SUBTLE: this bracket is what lets FaustEngine::compile() safely mutate
    // activeDSP/activeUI — it drains audioBusy to zero after ready=false. Every
    // engine touch on the audio thread must sit between enterAudio()/exitAudio();
    // adding an engine call outside the bracket reintroduces the swap race.
    if (!faustEngine.enterAudio())
    {
        // No DSP live: input passes through untouched; still feed the meter so
        // the editor shows the dry signal (audio-path-alive indicator).
        //
        // NOTE: MIDI arriving in this block is DROPPED, deliberately. The voice
        // zones belong to a DSP that is mid-swap, so writing them is a
        // use-after-free; buffering the events until the new DSP lands would
        // sound the note against an envelope it was not meant for. A compile
        // swap is already an audible discontinuity — losing a note inside one is
        // the smaller of the two wrongs, and it is bounded by one compile.
        outputLevel.store(buffer.getMagnitude(0, buffer.getNumSamples()),
                          std::memory_order_relaxed);
        return;
    }

    // ── MIDI → voice ────────────────────────────────────────────────────────
    // Inside the bracket, because these writes touch DSP zones exactly as
    // pushToFaust does.
    //
    // Gated on isInstrument() -- a property of the LOADED PATCH -- and
    // deliberately NOT on PF_IS_SYNTH, which is a property of the build. The
    // offline harnesses are console apps: they compile with PF_IS_SYNTH == 0 and
    // drive processBlock directly with no plugin wrapper, so a build-time gate
    // would make every test exercise a path the product does not have. An
    // effect patch never satisfies the voice contract, so it never enters here
    // regardless of which target it is running in.
    //
    // LIMITATION, stated rather than discovered: events are applied at BLOCK
    // granularity, not at their sample offsets — up to ~10.7 ms of timing jitter
    // at 48 kHz with a 512-sample block. Sample-accurate timing means splitting
    // the compute() call at each event offset, which is worth doing once the
    // pitch and polyphony gates exist to prove the split changed nothing else.
    if (faustEngine.isInstrument())
    {
        for (const auto meta : midi)
        {
            // getMessage() constructs a juce::MidiMessage, which OWNS its bytes
            // -- so on the audio thread the question is whether that allocates.
            // It does not for anything we handle here: MidiMessage stores up to
            // sizeof(uint8*) == 8 bytes inline in a union and heap-allocates
            // only above that (juce_MidiMessage.h:981-992,
            // `isHeapAllocated() { return size > (int) sizeof (packedData); }`).
            // Note-on, note-off and CC are 3 bytes. SysEx would allocate, and is
            // not touched here.
            //
            // Checked rather than assumed, and pinned rather than hand-rolling a
            // byte decoder: the velocity-0 convention below is easy to get
            // wrong, and JUCE already gets it right.
            const auto msg = meta.getMessage();

            // SUBTLE: the default arguments carry the running-status convention
            // and must not be "simplified" away. isNoteOn() defaults to
            // returnTrueForVelocity0=false and isNoteOff() to
            // returnTrueForNoteOnVelocity0=true (juce_MidiMessage.h:239, :266),
            // so a note-on with velocity 0 -- which is how many keyboards and
            // DAWs spell note-off -- falls through to the release branch. Pass
            // either flag explicitly the other way and held notes never stop.
            if (msg.isNoteOn())
                faustEngine.noteOn(msg.getNoteNumber(), msg.getVelocity());
            else if (msg.isNoteOff())
                faustEngine.noteOff(msg.getNoteNumber());
            else if (msg.isAllNotesOff() || msg.isAllSoundOff())
                faustEngine.allNotesOff();
        }
    }

    paramPool.pushToFaust(faustEngine);
    faustEngine.process(buffer);

    // SUBTLE: the guard runs ONLY on the generated-DSP path, deliberately. The
    // early-return above passes the host's own input through untouched, and
    // limiting or DC-blocking a user's dry signal would be a bug, not a safety
    // measure. Everything this guards against originates in machine-generated
    // Faust. Kept inside the enterAudio bracket so its cost is included in the
    // drain the compile thread waits on.
    outputGuard.process(buffer);

    faustEngine.exitAudio();

    outputLevel.store(buffer.getMagnitude(0, buffer.getNumSamples()),
                      std::memory_order_relaxed);
}

void PluginForgeProcessor::resetMappedSlotsToDefaults(const FaustEngine::ParamList& params)
{
    // SUBTLE: this runs inside the compile callback, which FaustEngine invokes at
    // step 5 of its swap protocol — AFTER the audioBusy drain proved no
    // audio-thread section is in flight, and BEFORE ready=true lets a new one
    // start. That is the one window in which the slot values can be rewritten
    // without pushToFaust concurrently reading them, so it is the right place and
    // the only right place. Same reasoning as outputGuard.reset() below.
    for (int i = 0; i < ParamPool::POOL_SIZE; ++i)
    {
        auto* param = apvts.getParameter(ParamPool::slotId(i));
        if (param == nullptr)
            continue;

        // Mapped slots take the patch's declared default, converted through the
        // SAME ParamMap pair pushToFaust uses in the other direction — a default
        // normalised by any other formula is the PF-001 bug all over again.
        // Unmapped slots go to 0 so a value left by a previous patch cannot
        // reappear if a later patch happens to map that index.
        const float norm =
            (static_cast<size_t>(i) < params.size())
                ? ParamMap::mapZoneToSlot(params[static_cast<size_t>(i)].defaultValue,
                                          params[static_cast<size_t>(i)])
                : 0.0f;

        // setValueNotifyingHost so the DAW's automation lane and any open editor
        // both follow the reset, rather than showing the previous patch's
        // position over a value that has actually changed underneath.
        param->setValueNotifyingHost(norm);
    }
}

void PluginForgeProcessor::loadFaustCode(const juce::String& faustCode,
                                         const juce::String& prompt,
                                         LoadMode mode)
{
    // PF-022: currentFaustSource/currentPrompt are NOT set here. They used to be
    // written unconditionally before the compile was even queued, so a failed
    // generate overwrote the source-of-record with non-compiling code while
    // activeDSP, currentLabels and the APVTS values still belonged to the
    // PREVIOUS successful patch. A DAW save in that window persisted a
    // broken-source / old-labels / old-values triple, and the restore recompile
    // then failed with no DSP live. The source of record is now whatever last
    // COMPILED, which is the only version of it that can actually be restored.
    //
    // Whether the OUTGOING patch was an instrument, read here on the message
    // thread while it is still the live one. Inside the callback this is already
    // the incoming patch's answer — voiceValid is published at swap step 3a,
    // before the callback fires at step 5 — so the comparison has to straddle
    // the compile. See the Iterate note in the callback.
    const bool wasInstrument = faustEngine.isInstrument();

    // Captured by value into the callback: juce::String's copy is atomically
    // ref-counted, so handing it to the compile thread is safe.
    faustEngine.compile(faustCode, [this, faustCode, prompt, mode, wasInstrument]
                                   (const FaustEngine::ParamList& params,
                                    const std::string& error) {
        if (error.empty())
        {
            paramPool.remap(params);

            // Iterate retains slot values BY INDEX (ParamPool::remap is
            // positional, ParamPool.cpp:33-53). Crossing the instrument boundary
            // shifts every index, because an instrument withholds gate/freq/gain
            // from the published list and an effect does not — so refining a
            // synth into an effect would land the old slot 0 (say Cutoff) on the
            // new slot 0, which for an instrument is a voice control. A wrong
            // pitch is far more audible than a wrong cutoff, and the user did not
            // touch the knob that appears to have moved.
            //
            // Forcing Fresh across that transition is a MITIGATION, not the fix.
            // The fix is a label-keyed remap, which makes retention meaningful for
            // every patch change rather than just this one; it is recorded in
            // STATUS.md's "Assumed, never checked" list as unbuilt.
            const bool crossedInstrumentBoundary =
                wasInstrument != faustEngine.isInstrument();

            // PF-020. Fresh must reset in the PROCESSOR, not the editor — doing it
            // in ParamGridPanel is what made "fresh" conditional on the editor
            // being open. Ordered after remap() so the ParamInfo the conversion
            // needs is published, and before the labels/source commit below.
            if (mode == LoadMode::Fresh || crossedInstrumentBoundary)
                resetMappedSlotsToDefaults(params);

            // Capture the slot->label map for persistence, and commit the source
            // of record (PF-022) — both only now that the compile has SUCCEEDED.
            // Runs on the compile thread, same as remap(); metaMutex (never taken
            // on the audio thread) guards it against a concurrent
            // getStateInformation() on the message thread.
            {
                std::lock_guard<std::mutex> lock(metaMutex);
                currentFaustSource = faustCode;
                currentPrompt      = prompt;
                currentLabels.clearQuick();
                for (const auto& p : params)
                    currentLabels.add(juce::String(p.label));
            }

            // A new patch gets a clean verdict: clear any latched mute from the
            // one it replaces.
            //
            // SUBTLE: reset() writes non-atomic filter state (dcX1/dcY1/
            // runawayRun) from the COMPILE thread, which would be a data race
            // against the audio thread anywhere else. It is safe at exactly this
            // point and nowhere else: this callback runs at FaustEngine::compile
            // step 5 -- after the audioBusy drain (step 2) proved no audio-thread
            // section is in flight, and before ready=true (step 6) lets a new one
            // start. The same drain that makes the activeDSP/activeUI swap safe
            // covers this. Do not move this call outside the compile callback.
            outputGuard.reset();

            // ADR-020: what a sustained over-scale run should DO depends on what
            // the patch IS. An effect running at 0 dBFS without ever dipping
            // below is a diverging filter and gets muted; an instrument doing it
            // is a loud oscillator -- os.square at gain 1.0 sits at |y| == 1.0
            // forever by construction -- and muting it until the next recompile
            // is a worse outcome than the loudness. Set here for the same reason
            // reset() is: it is the one window where writing this is not a race.
            outputGuard.setRunawayPolicy(faustEngine.isInstrument()
                                             ? OutputGuard::RunawayPolicy::Report
                                             : OutputGuard::RunawayPolicy::Latch);

            if (onFaustCompileSuccess)
                onFaustCompileSuccess(params);
        }
        else
        {
            juce::Logger::writeToLog("FaustEngine compile error: " + juce::String(error));

            // Fire the canonical failure callback plus the deprecated alias
            // (FLEET req #7). Both are fired only until S3 migrates the editor's
            // call site from onFaustCompileError to onFaustCompileFailure; the
            // alias is then removed. In practice the editor sets exactly one, so
            // this is a single delivery today, not a double-notify.
            const juce::String errStr(error);
            if (onFaustCompileFailure)
                onFaustCompileFailure(errStr);
            if (onFaustCompileError)
                onFaustCompileError(errStr);
        }
    });
}

// ─────────────────────────────────────────────────────────────────────────────
// State persistence (P11 / docs/ux_roadmap.md Phase 1).
//
// PERSISTED-STATE FORMAT — COLLABORATION.md §2 trigger-3 contract, signed off
// 2026-07-23. Consumers: this processor, and (future) preset / .pforge export /
// schema migration. A ValueTree serialised to XML then to a binary blob:
//
//   <PluginForgeState schemaVersion="1"
//                     faustSource="import(&quot;stdfaust.lib&quot;);&#10;process = _;"
//                     prompt="a warm lowpass">
//     <STATE> ... verbatim apvts.copyState(): 64 macro_* PARAM children ... </STATE>
//   </PluginForgeState>
//
// - faustSource / prompt are root attributes; JUCE escapes newlines and quotes in
//   XML attribute values, so multi-line source survives without CDATA.
// - <STATE> is the APVTS's own tree appended verbatim, so the 64 slot values ride
//   JUCE's standard mechanism.
// - The DSP is never serialised. The Faust source is the artifact of record and
//   setState recompiles it.
//
// DROPPED 2026-07-27 — <SlotLabels>. v1 used to carry a slot->label hint node,
// documented as letting the editor label knobs before the async restore recompile
// finished. Nothing ever read it: setStateInformation below restores STATE,
// faustSource and prompt and never looked the node up, so the hint was written on
// every save and consumed by no one. Removed while v1 is still the only blob in
// the wild, rather than carried as a contract we would have to honour later.
// remap() on the restore recompile is, and always was, the only label source.
//
// Old blobs that still contain the node restore correctly and need no migration:
// setStateInformation looks its children up BY NAME (getChildWithName), so an
// unrecognised extra child is simply never read. That is why this is an amendment
// to v1 and not a schemaVersion bump.
// ─────────────────────────────────────────────────────────────────────────────

static const juce::Identifier kStateRootTag    ("PluginForgeState");
static const juce::Identifier kSchemaVersionId ("schemaVersion");
static const juce::Identifier kFaustSourceId   ("faustSource");
static const juce::Identifier kPromptId        ("prompt");
// Added 2026-07-31. A v1 AMENDMENT, not a schema bump, by the same argument the
// dropped <SlotLabels> node used above: an old blob simply lacks the attribute and
// getProperty's default supplies "faithful", which is the behaviour those blobs
// were saved under anyway. Nothing has to migrate.
static const juce::Identifier kUiStyleId       ("uiStyle");

juce::String PluginForgeProcessor::uiStyle() const
{
    std::lock_guard<std::mutex> lock(metaMutex);
    return currentUiStyle;
}

void PluginForgeProcessor::setUiStyle(const juce::String& styleName)
{
    {
        std::lock_guard<std::mutex> lock(metaMutex);
        if (currentUiStyle == styleName)
            return;                       // idempotent; no spurious notification
        currentUiStyle = styleName;
    }
    // Fired OUTSIDE the lock: a listener will call back into the editor, and
    // holding metaMutex across arbitrary UI work is how a deadlock gets built.
    if (onUiStyleChanged)
        onUiStyleChanged(styleName);
}

void PluginForgeProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    juce::ValueTree root(kStateRootTag);
    root.setProperty(kSchemaVersionId, kStateSchemaVersion, nullptr);

    {
        std::lock_guard<std::mutex> lock(metaMutex);
        root.setProperty(kFaustSourceId, currentFaustSource, nullptr);
        root.setProperty(kPromptId,      currentPrompt,      nullptr);
        root.setProperty(kUiStyleId,     currentUiStyle,     nullptr);
    }

    // copyState() flushes pending parameter updates and returns a thread-safe copy
    // (juce_AudioProcessorValueTreeState.h:375-382). Its tree type is "STATE" (the
    // valueTreeType passed to the apvts constructor in the initialiser list above).
    root.appendChild(apvts.copyState(), nullptr);

    // copyXmlToBinary: XmlElement -> binary blob, reversed by getXmlFromBinary
    // (juce_AudioProcessor.h:1306-1312).
    if (auto xml = root.createXml())
        copyXmlToBinary(*xml, destData);
}

void PluginForgeProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    // getXmlFromBinary may return nullptr on corrupt/unsuitable data
    // (juce_AudioProcessor.h:1309-1312) — treat as "leave current state untouched".
    auto xml = getXmlFromBinary(data, sizeInBytes);
    if (xml == nullptr)
        return;

    auto root = juce::ValueTree::fromXml(*xml);
    if (! root.isValid() || root.getType() != kStateRootTag)
        return;   // not our blob, or a foreign one — do not misread it.

    const int version = root.getProperty(kSchemaVersionId, 0);
    if (version < 1 || version > kStateSchemaVersion)
        return;   // unknown version: refuse rather than guess at the layout.

    // Restore the 64 macro values. replaceState() updates the APVTS tree in a
    // thread-safe way (juce_AudioProcessorValueTreeState.h:384-395); it swaps the
    // backing ValueTree, not the RangedAudioParameter objects, so the pointers
    // ParamPool captured in its constructor (ParamPool.cpp:16-17) stay valid.
    auto stateChild = root.getChildWithName(apvts.state.getType());
    if (stateChild.isValid())
        apvts.replaceState(stateChild);

    const juce::String source = root.getProperty(kFaustSourceId, juce::String());
    const juce::String prompt = root.getProperty(kPromptId,      juce::String());
    // Absent in a pre-2026-07-31 blob; the default is the behaviour those blobs
    // were saved under, so an old session reopens looking exactly as it did.
    const juce::String style  = root.getProperty(kUiStyleId,     "faithful");

    {
        std::lock_guard<std::mutex> lock(metaMutex);
        currentFaustSource = source;
        currentPrompt      = prompt;
        currentUiStyle     = style;
    }

    // Tell any open editor before the restore recompile below: the compile will
    // rebuild every widget, and applyPresentation reads the panel's style, so the
    // panel must already know the restored choice or the first frame after a
    // project load renders in the wrong style and only corrects on the next flip.
    if (onUiStyleChanged)
        onUiStyleChanged(style);

    if (source.isEmpty())
        return;   // param values restored; no source to recompile.

    // Trigger the restore recompile — but only if the host has already told us the
    // sample rate. Before prepareToPlay, FaustEngine still holds the 44100 default
    // (FaustEngine.cpp:154-158) and would JIT at the wrong rate; defer to
    // prepareToPlay, which drains the stash below.
    if (prepared.load(std::memory_order_acquire))
    {
        // Iterate: the values restored by replaceState() above are the point of
        // the restore, so this recompile must not reset them (PF-020).
        loadFaustCode(source, prompt, LoadMode::Iterate);
    }
    else
    {
        std::lock_guard<std::mutex> lock(metaMutex);
        pendingRestoreSource = source;
        pendingRestorePrompt = prompt;
    }
}

juce::String PluginForgeProcessor::currentSource() const
{
    std::lock_guard<std::mutex> lock(metaMutex);
    return currentFaustSource;
}

juce::String PluginForgeProcessor::currentPromptForTest() const
{
    std::lock_guard<std::mutex> lock(metaMutex);
    return currentPrompt;
}

juce::StringArray PluginForgeProcessor::currentLabelsForTest() const
{
    std::lock_guard<std::mutex> lock(metaMutex);
    return currentLabels;   // by value, under the lock: the compile thread rewrites this
}

juce::AudioProcessorEditor* PluginForgeProcessor::createEditor()
{
    return new PluginForgeEditor(*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new PluginForgeProcessor();
}
