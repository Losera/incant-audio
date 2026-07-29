#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "ParamMap.h"   // mapZoneToSlot, for LoadMode::Fresh (PF-020)

PluginForgeProcessor::PluginForgeProcessor()
    : AudioProcessor(BusesProperties()
          .withInput ("Input",  juce::AudioChannelSet::stereo(), true)
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
        params.push_back(std::make_unique<juce::AudioParameterFloat>(
            ParamPool::slotId(i),               // paramID (stable — DAW automation uses this)
            "Macro " + juce::String(i + 1),     // display name
            0.0f, 1.0f, 0.0f
        ));
    }

    return { params.begin(), params.end() };
}

void PluginForgeProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    faustEngine.prepare(sampleRate, samplesPerBlock);
    outputGuard.prepare(sampleRate);

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

void PluginForgeProcessor::processBlock(juce::AudioBuffer<float>& buffer,
                                         juce::MidiBuffer&)
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
        outputLevel.store(buffer.getMagnitude(0, buffer.getNumSamples()),
                          std::memory_order_relaxed);
        return;
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
    // Captured by value into the callback: juce::String's copy is atomically
    // ref-counted, so handing it to the compile thread is safe.
    faustEngine.compile(faustCode, [this, faustCode, prompt, mode]
                                   (const FaustEngine::ParamList& params,
                                    const std::string& error) {
        if (error.empty())
        {
            paramPool.remap(params);

            // PF-020. Fresh must reset in the PROCESSOR, not the editor — doing it
            // in ParamGridPanel is what made "fresh" conditional on the editor
            // being open. Ordered after remap() so the ParamInfo the conversion
            // needs is published, and before the labels/source commit below.
            if (mode == LoadMode::Fresh)
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
            // runawayBlocks) from the COMPILE thread, which would be a data race
            // against the audio thread anywhere else. It is safe at exactly this
            // point and nowhere else: this callback runs at FaustEngine::compile
            // step 5 -- after the audioBusy drain (step 2) proved no audio-thread
            // section is in flight, and before ready=true (step 6) lets a new one
            // start. The same drain that makes the activeDSP/activeUI swap safe
            // covers this. Do not move this call outside the compile callback.
            outputGuard.reset();
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

void PluginForgeProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    juce::ValueTree root(kStateRootTag);
    root.setProperty(kSchemaVersionId, kStateSchemaVersion, nullptr);

    {
        std::lock_guard<std::mutex> lock(metaMutex);
        root.setProperty(kFaustSourceId, currentFaustSource, nullptr);
        root.setProperty(kPromptId,      currentPrompt,      nullptr);
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

    {
        std::lock_guard<std::mutex> lock(metaMutex);
        currentFaustSource = source;
        currentPrompt      = prompt;
    }

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
