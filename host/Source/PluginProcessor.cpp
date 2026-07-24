#include "PluginProcessor.h"
#include "PluginEditor.h"

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
    if (source.isNotEmpty())
        loadFaustCode(source, prompt);
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

void PluginForgeProcessor::loadFaustCode(const juce::String& faustCode,
                                         const juce::String& prompt)
{
    // Retain the source and prompt so a DAW save can serialise them. Set before
    // the compile is queued: the source is the artifact of record regardless of
    // whether this particular compile ends up succeeding.
    {
        std::lock_guard<std::mutex> lock(metaMutex);
        currentFaustSource = faustCode;
        currentPrompt      = prompt;
    }

    faustEngine.compile(faustCode, [this](const FaustEngine::ParamList& params,
                                          const std::string& error) {
        if (error.empty())
        {
            paramPool.remap(params);

            // Capture the slot->label map for persistence. Runs on the compile
            // thread, same as remap(); metaMutex (never taken on the audio thread)
            // guards it against a concurrent getStateInformation() on the message
            // thread. Only the FaustEngine swap protocol touches the audio thread.
            {
                std::lock_guard<std::mutex> lock(metaMutex);
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
//     <SlotLabels>
//       <Slot index="0" label="Cutoff"/> ...
//     </SlotLabels>
//   </PluginForgeState>
//
// - faustSource / prompt are root attributes; JUCE escapes newlines and quotes in
//   XML attribute values, so multi-line source survives without CDATA.
// - <STATE> is the APVTS's own tree appended verbatim, so the 64 slot values ride
//   JUCE's standard mechanism.
// - <SlotLabels> is a HINT for the editor to label knobs before the async restore
//   recompile finishes; remap() regenerates the authoritative map on recompile, so
//   on any disagreement the recompile wins.
// - The DSP is never serialised. The Faust source is the artifact of record and
//   setState recompiles it.
// ─────────────────────────────────────────────────────────────────────────────

static const juce::Identifier kStateRootTag  ("PluginForgeState");
static const juce::Identifier kSlotLabelsTag  ("SlotLabels");
static const juce::Identifier kSlotTag        ("Slot");
static const juce::Identifier kSchemaVersionId ("schemaVersion");
static const juce::Identifier kFaustSourceId   ("faustSource");
static const juce::Identifier kPromptId        ("prompt");
static const juce::Identifier kIndexId         ("index");
static const juce::Identifier kLabelId         ("label");

void PluginForgeProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    juce::ValueTree root(kStateRootTag);
    root.setProperty(kSchemaVersionId, kStateSchemaVersion, nullptr);

    {
        std::lock_guard<std::mutex> lock(metaMutex);
        root.setProperty(kFaustSourceId, currentFaustSource, nullptr);
        root.setProperty(kPromptId,      currentPrompt,      nullptr);

        juce::ValueTree labels(kSlotLabelsTag);
        for (int i = 0; i < currentLabels.size(); ++i)
        {
            juce::ValueTree slot(kSlotTag);
            slot.setProperty(kIndexId, i, nullptr);
            slot.setProperty(kLabelId, currentLabels[i], nullptr);
            labels.appendChild(slot, nullptr);
        }
        root.appendChild(labels, nullptr);
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
        loadFaustCode(source, prompt);
    }
    else
    {
        std::lock_guard<std::mutex> lock(metaMutex);
        pendingRestoreSource = source;
        pendingRestorePrompt = prompt;
    }
}

juce::String PluginForgeProcessor::currentSourceForTest() const
{
    std::lock_guard<std::mutex> lock(metaMutex);
    return currentFaustSource;
}

juce::String PluginForgeProcessor::currentPromptForTest() const
{
    std::lock_guard<std::mutex> lock(metaMutex);
    return currentPrompt;
}

juce::AudioProcessorEditor* PluginForgeProcessor::createEditor()
{
    return new PluginForgeEditor(*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new PluginForgeProcessor();
}
