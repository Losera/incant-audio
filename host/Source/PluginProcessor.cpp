#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "ParamMap.h"        // mapZoneToSlot, for LoadMode::Fresh (PF-020)
#include "ParamIdentity.h"   // kSchemeVersion, stamped into the state blob
#include <cmath>

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
    preparedSampleRate.store(sampleRate, std::memory_order_relaxed);
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

    juce::String source, prompt, family, familySource;
    {
        std::lock_guard<std::mutex> lock(metaMutex);
        source = pendingRestoreSource;
        prompt = pendingRestorePrompt;
        family = pendingRestoreFamily;
        familySource = pendingRestoreFamilySource;
        pendingRestoreSource.clear();
        pendingRestorePrompt.clear();
        pendingRestoreFamily.clear();
        pendingRestoreFamilySource.clear();
    }
    // Restore, NOT Fresh and NOT Iterate: setStateInformation already wrote the
    // saved macro values via replaceState(). Resetting them to patch defaults
    // here would discard exactly what the restore just recovered (PF-020).
    //
    // Iterate is not strong enough any more. It seeds slots that CHANGED HANDS,
    // which is right for a refine -- but on a restore every slot looks changed
    // whenever the identity map could not be seeded (a v1 blob, or an unknown id
    // scheme), so Iterate would helpfully overwrite the entire restored session
    // with patch defaults. Restore seeds nothing at all, because on this path the
    // APVTS tree is the source of truth by construction.
    if (source.isNotEmpty())
        loadFaustCode(source, prompt, LoadMode::Restore, family, familySource);
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
    auditionPos.store(0, std::memory_order_relaxed);
}

void PluginForgeProcessor::loadAuditionSample(const juce::File& wavFile)
{
    // seq_cst: part of the Dekker handshake with processBlock's seq_cst load of
    // auditionActive. The store-then-drain-then-mutate sequence matches the
    // compile worker's ready-store-then-drain-then-swap (FaustEngine.cpp:900-964).
    // In the total order of seq_cst operations, if a processBlock's load reads
    // true (stale), the load is ordered before this store — so the drain
    // (seq_cst load of audioBusy) sees that processBlock's fetch_add and spins
    // until it completes. If the load reads false, the read path is not taken.
    auditionActive.store(false, std::memory_order_seq_cst);
    auditionPos.store(0, std::memory_order_relaxed);

    if (! wavFile.existsAsFile())
        return;

    juce::AudioFormatManager fm;
    fm.registerBasicFormats();

    std::unique_ptr<juce::AudioFormatReader> reader(fm.createReaderFor(wavFile));
    if (reader == nullptr || reader->lengthInSamples <= 0)
        return;

    const double sourceRate = juce::jmax(1.0, reader->sampleRate);
    const double targetRate = juce::jmax(1.0, preparedSampleRate.load(std::memory_order_relaxed));
    const auto maxLength = static_cast<juce::int64>(sourceRate * 30.0);
    const int sourceLength = static_cast<int>(
        juce::jmin<juce::int64>(reader->lengthInSamples, maxLength));
    juce::AudioBuffer<float> sourceBuffer(2, sourceLength + 8);
    sourceBuffer.clear();
    reader->read(&sourceBuffer, 0, sourceLength, 0, true, true);

    const int targetLength = juce::jmax(1, juce::roundToInt(
        static_cast<double>(sourceLength) * targetRate / sourceRate));
    juce::AudioBuffer<float> loaded(2, targetLength);
    loaded.clear();
    if (std::abs(sourceRate - targetRate) < 0.5)
    {
        for (int channel = 0; channel < 2; ++channel)
            loaded.copyFrom(channel, 0, sourceBuffer, channel, 0, sourceLength);
    }
    else
    {
        const double speedRatio = sourceRate / targetRate;
        for (int channel = 0; channel < 2; ++channel)
        {
            juce::LagrangeInterpolator interpolator;
            interpolator.process(speedRatio, sourceBuffer.getReadPointer(channel),
                                 loaded.getWritePointer(channel), targetLength);
        }
    }

    // Drain: wait for any processBlock that loaded auditionActive==true before
    // our store(false) to complete its audition read and exitAudio. Without this,
    // a mid-memcpy on the audio thread races the std::move below (use-after-free
    // of the old buffer's heap block, per juce_AudioSampleBuffer.h:205-228 /
    // juce_HeapBlock.h:144-148). This is the same drain FaustEngine.cpp:911-912
    // performs before swapping activeDSP.
    faustEngine.drainAudioBusy();

    auditionBuffer = std::move(loaded);
    auditionModeValue.store(auditionBuffer.getNumSamples() > 0
                                ? static_cast<int>(AuditionMode::OneShot)
                                : static_cast<int>(AuditionMode::Stopped),
                            std::memory_order_relaxed);
    auditionActive.store(auditionBuffer.getNumSamples() > 0, std::memory_order_seq_cst);
}

void PluginForgeProcessor::setAuditionMode(AuditionMode mode)
{
    if (mode != AuditionMode::Stopped && auditionBuffer.getNumSamples() == 0)
        return;
    auditionPos.store(0, std::memory_order_relaxed);
    auditionModeValue.store(static_cast<int>(mode), std::memory_order_relaxed);
    auditionActive.store(mode != AuditionMode::Stopped, std::memory_order_seq_cst);
}

void PluginForgeProcessor::setAuditionActive(bool active)
{
    if (active && auditionBuffer.getNumSamples() == 0)
        return;
    auditionPos.store(0, std::memory_order_relaxed);
    // seq_cst: consistent with loadAuditionSample's store sequence. No drain
    // needed — this function does not mutate auditionBuffer, only the flag.
    // An in-flight processBlock reading the unmodified buffer is harmless.
    auditionModeValue.store(static_cast<int>(active ? AuditionMode::Loop : AuditionMode::Stopped),
                            std::memory_order_relaxed);
    auditionActive.store(active, std::memory_order_seq_cst);
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
    const bool playable = faustEngine.isInstrument();

    if (playable)
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

    // ── The on-screen / computer keyboard ───────────────────────────────────
    // Same voice writes as the walk above, from a different source: the editor's
    // MidiKeyboardState callbacks run on the message thread and cannot touch a
    // DSP zone themselves, so they queue here and this drains them. NoteRing.h
    // carries the argument for why a queue and not the idiomatic
    // MidiKeyboardState::processNextMidiBuffer -- that call locks, and this
    // thread does not.
    //
    // DRAINED INLINE, not in a helper. check_rt_safety.py scopes exactly four
    // function names and cannot follow a call graph (its own docstring says so),
    // so a drainNoteRing() would be invisible to the one automated check that
    // reads this file for allocations and locks. Keeping the loop in the
    // function the hook already watches is the difference between a guarded and
    // an unguarded audio path.
    //
    // ⚠️ DRAINED UNCONDITIONALLY, APPLIED ONLY TO AN INSTRUMENT -- and the two
    // halves of that sentence are not the same condition, which is the whole
    // reason this sits OUTSIDE the `playable` block above. Draining inside it
    // meant an effect patch never emptied the queue: the ring would fill to 255,
    // every further keypress would count as dropped, and the moment the user
    // generated an instrument the stale backlog would fire as one burst of
    // note-ons. The editor disables the keyboard for an effect, so this should
    // never receive anything then -- "should never" is exactly the assumption
    // worth not building on.
    //
    // AFTER the MidiBuffer walk, so that when both sources deliver in one block
    // the on-screen key is the one that wins the monophonic voice. The two
    // streams have no timestamp relationship -- ring events were produced
    // asynchronously on another thread -- so SOME order had to be chosen; this
    // one favours the control the user is looking at.
    pf::NoteEvent ev;
    while (noteRing.pop(ev))
    {
        if (! playable)
            continue;

        if (ev.on)
            faustEngine.noteOn(ev.note, ev.velocity);
        else
            faustEngine.noteOff(ev.note);
    }

    paramPool.pushToFaust(faustEngine);

    // ── Sample audition: feed pre-loaded WAV through the live DSP ──────────
    // When auditionActive is true, the input buffer is overwritten with the
    // audition sample (looping), so effects process a known signal rather than
    // whatever the DAW host feeds in. This is the audition path — the sample
    // was pre-loaded on the message thread (loadAuditionSample) into a fixed
    // AudioBuffer; we read it with an atomic position (no allocation on audio
    // thread, no lock, no I/O — just a memcpy from a pre-existing buffer).
    //
    // SUBTLE: auditionPos is ONLY written by this audio-thread code (reset to 0
    // by loadAuditionSample / setAuditionActive on the message thread, but the
    // store is relaxed and we tolerate one stale read — worst case, we read a
    // slightly old position and the sample loops sooner or later than intended
    // by one block, which is inaudible at 512 samples / 48 kHz).
    // seq_cst: part of the Dekker handshake with loadAuditionSample's seq_cst
    // store(false). In the seq_cst total order, if this load reads true (stale),
    // it is ordered before that store — which means loadAuditionSample's drain
    // sees our audioBusy and waits. If it reads false, the buffer is not touched.
    // (See FaustEngine.h:255-270 for the enterAudio Dekker argument this mirrors.)
    if (auditionActive.load(std::memory_order_seq_cst))
    {
        const int numAudSamples = auditionBuffer.getNumSamples();
        if (numAudSamples > 0)
        {
            const int numCh = juce::jmin(buffer.getNumChannels(),
                                         auditionBuffer.getNumChannels());
            const int numSamples = buffer.getNumSamples();
            size_t pos = auditionPos.load(std::memory_order_relaxed);

            const bool loop = auditionMode() == AuditionMode::Loop;
            bool reachedEnd = false;
            for (int ch = 0; ch < numCh; ++ch)
            {
                const float* src = auditionBuffer.getReadPointer(ch);
                float* dst = buffer.getWritePointer(ch, 0);
                size_t p = pos;
                for (int s = 0; s < numSamples; ++s)
                {
                    if (p < static_cast<size_t>(numAudSamples))
                        dst[s] = src[p++];
                    else if (loop)
                    {
                        p = 0;
                        dst[s] = src[p++];
                    }
                    else
                    {
                        dst[s] = 0.0f;
                        reachedEnd = true;
                    }
                }
            }
            // Zero any remaining channels (e.g., mono buffer with stereo sample)
            for (int ch = numCh; ch < buffer.getNumChannels(); ++ch)
                buffer.clear(ch, 0, numSamples);

            pos += static_cast<size_t>(numSamples);
            if (loop)
                while (pos >= static_cast<size_t>(numAudSamples)) pos -= static_cast<size_t>(numAudSamples);
            else if (pos >= static_cast<size_t>(numAudSamples))
                reachedEnd = true;
            auditionPos.store(pos, std::memory_order_relaxed);
            if (reachedEnd)
            {
                auditionModeValue.store(static_cast<int>(AuditionMode::Stopped), std::memory_order_relaxed);
                auditionActive.store(false, std::memory_order_seq_cst);
            }
        }
    }

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

void PluginForgeProcessor::seedSlotsToDefaults(const std::vector<int>& slotsToSeed)
{
    // Same thread-safety window as resetMappedSlotsToDefaults below -- both run
    // inside the compile callback, between the audioBusy drain and ready=true.
    const auto& bySlot = paramPool.publishedSlots();

    for (const int i : slotsToSeed)
    {
        if (i < 0 || i >= ParamPool::POOL_SIZE)
            continue;

        auto* param = apvts.getParameter(ParamPool::slotId(i));
        if (param == nullptr)
            continue;

        float norm = 0.0f;
        if (static_cast<size_t>(i) < bySlot.size() && bySlot[static_cast<size_t>(i)].zone != nullptr)
            norm = ParamMap::mapZoneToSlot(bySlot[static_cast<size_t>(i)].defaultValue,
                                           bySlot[static_cast<size_t>(i)]);

        param->setValueNotifyingHost(norm);
    }
}

void PluginForgeProcessor::resetMappedSlotsToDefaults()
{
    // SUBTLE: this runs inside the compile callback, which FaustEngine invokes at
    // step 5 of its swap protocol — AFTER the audioBusy drain proved no
    // audio-thread section is in flight, and BEFORE ready=true lets a new one
    // start. That is the one window in which the slot values can be rewritten
    // without pushToFaust concurrently reading them, so it is the right place and
    // the only right place. Same reasoning as outputGuard.reset() below.
    //
    // ⚠️ It now reads the pool's PER-SLOT view rather than the caller's ParamList.
    // It used to take that list and index it with the slot number, which was only
    // ever correct because remap() was positional -- params[i] WAS slot i. With
    // identity-keyed assignment a param can land in any slot, so indexing the
    // list by slot number would seed slot 3 from the 4th param in Faust's
    // alphabetical order: not merely a wrong value, but the PF-001 shape again
    // (two places computing one mapping and disagreeing). Asking the pool which
    // param actually holds each slot is the only version of this that cannot
    // drift, because there is only one answer and the pool owns it.
    const auto& bySlot = paramPool.publishedSlots();

    for (int i = 0; i < ParamPool::POOL_SIZE; ++i)
    {
        auto* param = apvts.getParameter(ParamPool::slotId(i));
        if (param == nullptr)
            continue;

        // Mapped slots take the patch's declared default, converted through the
        // SAME ParamMap pair pushToFaust uses in the other direction — a default
        // normalised by any other formula is the PF-001 bug all over again.
        // Unmapped slots go to 0 so a value left by a previous patch cannot
        // reappear if a later patch happens to map that slot. A null zone is the
        // unused-slot sentinel remap() writes.
        float norm = 0.0f;
        if (static_cast<size_t>(i) < bySlot.size() && bySlot[static_cast<size_t>(i)].zone != nullptr)
            norm = ParamMap::mapZoneToSlot(bySlot[static_cast<size_t>(i)].defaultValue,
                                           bySlot[static_cast<size_t>(i)]);

        // setValueNotifyingHost so the DAW's automation lane and any open editor
        // both follow the reset, rather than showing the previous patch's
        // position over a value that has actually changed underneath.
        param->setValueNotifyingHost(norm);
    }
}

void PluginForgeProcessor::loadFaustCode(const juce::String& faustCode,
                                         const juce::String& prompt,
                                         LoadMode mode,
                                         const juce::String& family,
                                         const juce::String& familySource)
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
    // A `wasInstrument` snapshot used to be taken here, on the message thread
    // while the outgoing patch was still the live one, so the callback could
    // detect a crossing of the instrument/effect boundary and force Fresh.
    // Identity-keyed assignment retired that mitigation (see the callback), and
    // the snapshot went with it rather than being left captured-but-unread: a
    // variable that still looks load-bearing is how a reader learns to distrust
    // the rest of the file.
    //
    // Captured by value into the callback: juce::String's copy is atomically
    // ref-counted, so handing it to the compile thread is safe.
    const auto resolvedFamily = family.isNotEmpty() ? family : juce::String(PF_IS_SYNTH ? "synth" : "effect");
    const auto resolvedFamilySource = familySource.isNotEmpty() ? familySource : juce::String("legacy_default");
    faustEngine.compile(faustCode, [this, faustCode, prompt, mode, resolvedFamily, resolvedFamilySource]
                                   (const FaustEngine::ParamList& params,
                                    const std::string& error) {
        if (error.empty())
        {
            // Fresh forgets the previous slot assignment so the pool packs from
            // slot 0; its values are about to be reset to patch defaults anyway,
            // and retaining a fragmented assignment whose values are discarded
            // buys nothing. Iterate keeps the map, which is what makes a value
            // follow its parameter across a regeneration.
            //
            // Ordered BEFORE remap(): remap() rebuilds the map from whatever it
            // finds, so clearing afterwards would discard the assignment it just
            // made.
            if (mode == LoadMode::Fresh)
                paramPool.clearIdentityMap();

            const auto remapResult = paramPool.remap(params);

            // THE INSTRUMENT-BOUNDARY MITIGATION IS GONE, and this is the change
            // that retires it. It used to force Fresh whenever wasInstrument
            // differed from isInstrument(), because remap was POSITIONAL: an
            // instrument withholds gate/freq/gain from the published list and an
            // effect does not, so crossing that boundary shifted every index and
            // landed the old slot 0 (say Cutoff) on a voice control. Its own
            // comment called itself a mitigation and named label-keyed remap as
            // the fix.
            //
            // Identity-keyed assignment makes the crossing a non-event: `cutoff`
            // reclaims whatever slot `cutoff` held, whether or not three voice
            // controls appeared or vanished around it. Retention is now correct
            // for EVERY patch change rather than disabled for one of them.

            // PF-020. Fresh must reset in the PROCESSOR, not the editor — doing it
            // in ParamGridPanel is what made "fresh" conditional on the editor
            // being open. Ordered after remap() so the ParamInfo the conversion
            // needs is published, and before the labels/source commit below.
            if (mode == LoadMode::Fresh)
            {
                resetMappedSlotsToDefaults();
            }
            else if (mode == LoadMode::Restore)
            {
                // Nothing. setStateInformation already wrote the saved values and
                // seeded the identity map that makes this compile reclaim the
                // same slots; touching anything here would undo the restore.
            }
            else
            {
                // Iterate keeps the values of params that RECLAIMED their slot --
                // that is the entire feature, and it is what makes "make the
                // resonance stronger" preserve the rest of the patch.
                //
                // But a slot that CHANGED HANDS holds a value belonging to a
                // parameter that no longer exists. Left alone, a newly-introduced
                // "Drive" would appear holding the position the user had dialled
                // into the deleted "Res" -- PF-020's exact hazard, arriving
                // through identity assignment instead of positional. Seeding just
                // those slots is the narrowest correct answer: it fixes the
                // newcomer without touching a single retained value.
                seedSlotsToDefaults(remapResult.newlyAssignedSlots);
            }

            // PF-051. A patch with more than POOL_SIZE controls used to lose the
            // surplus silently -- no error, no log, no count. Logged here rather
            // than failing the compile: the DSP is live and correct, and 64 of its
            // controls do work, so refusing the patch outright would be a worse
            // answer than a partial one the user is told about.
            if (! remapResult.overflowed.empty())
                juce::Logger::writeToLog(
                    "PluginForge: patch declares " + juce::String(params.size())
                    + " controls; only " + juce::String(ParamPool::POOL_SIZE)
                    + " fit the macro pool. Unreachable: "
                    + juce::String(remapResult.overflowed.front())
                    + (remapResult.overflowed.size() > 1
                           ? " (+" + juce::String(remapResult.overflowed.size() - 1) + " more)"
                           : ""));

            // Capture the slot->label map for persistence, and commit the source
            // of record (PF-022) — both only now that the compile has SUCCEEDED.
            // Runs on the compile thread, same as remap(); metaMutex (never taken
            // on the audio thread) guards it against a concurrent
            // getStateInformation() on the message thread.
            {
                std::lock_guard<std::mutex> lock(metaMutex);
                currentFaustSource = faustCode;
                currentPrompt      = prompt;
                currentGenerationFamily = resolvedFamily;
                currentGenerationFamilySource = resolvedFamilySource;
                currentLabels.clearQuick();
                for (const auto& p : params)
                    currentLabels.add(juce::String(p.label));

                // The slot->id map, captured in the same critical section as the
                // labels and the source so a getStateInformation() on the message
                // thread can never observe a blob whose values, labels and
                // assignment came from different compiles.
                currentSlotIds = remapResult.slotIds;
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

            // The PER-SLOT view, not the compact capture list. The editor binds
            // each control to a macro slot, so it has to be told which slot each
            // param landed in -- and after identity-keyed assignment that is no
            // longer "the same order you captured them in". See the header
            // comment on ParamGridPanel::refreshParamKnobs.
            //
            // Safe to hand out by reference: the editor's handler copies it into
            // a callAsync capture before returning (PluginEditor.cpp:81-83), on
            // this thread, while the buffer is live and published.
            if (onFaustCompileSuccess)
                onFaustCompileSuccess(paramPool.publishedSlots());
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
                onFaustCompileFailure(errStr, faustCode);
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
//
// ADDED 2026-08-12 (C6) — <PromptHistory>, schemaVersion 3. A child element,
// one <HistoryEntry text="..."/> per retained prompt, most-recent-first:
//
//   <PromptHistory>
//     <HistoryEntry text="a warm lowpass"/>
//     <HistoryEntry text="add a chorus"/>
//   </PromptHistory>
//
// A v1/v2 blob has none of these children; getChildWithName returns an
// invalid tree and setStateInformation restores an empty history, the same
// do-nothing fallback the v1-ParamMap case already established. This IS a
// real schemaVersion bump, unlike <SlotLabels> or uiStyle: those were fields
// nothing depended on, whereas an old session's actual prompt history is
// present in the wild and genuinely absent from the blob, not merely unread.
//
// ADDED 2026-08-11 (main, "deterministic plugin families") — `generationFamily`
// / `familySource` root attributes, same schemaVersion 3, landing alongside
// PromptHistory above via this merge rather than as a separate bump (see
// kStateSchemaVersion's comment in PluginProcessor.h for why one combined v3
// is safe here). `generationFamily` is the resolved profile id (e.g.
// "granular_effect", "drum_synth"); `familySource` records HOW it was
// resolved -- "explicit" (user picked it), "auto" (deterministically inferred
// from the prompt), or "legacy_default"/"migrated" for a restore that had no
// family information to read. A v1/v2 blob predates both attributes;
// setStateInformation's `version >= 3` gate (below) falls back to the
// build-target default (PF_IS_SYNTH) with source "legacy_default" -- the same
// behaviour those blobs were always saved under.
// ─────────────────────────────────────────────────────────────────────────────

static const juce::Identifier kStateRootTag    ("PluginForgeState");
static const juce::Identifier kSchemaVersionId ("schemaVersion");
static const juce::Identifier kFaustSourceId   ("faustSource");
static const juce::Identifier kPromptId        ("prompt");
static const juce::Identifier kGenerationFamilyId ("generationFamily");
static const juce::Identifier kFamilySourceId  ("familySource");
// Added 2026-07-31. A v1 AMENDMENT, not a schema bump, by the same argument the
// dropped <SlotLabels> node used above: an old blob simply lacks the attribute and
// getProperty's default supplies "faithful", which is the behaviour those blobs
// were saved under anyway. Nothing has to migrate.
static const juce::Identifier kUiStyleId       ("uiStyle");
// schemaVersion 2 (2026-08-02). The slot -> ParamIdentity map, and the derivation
// scheme that produced it. See getStateInformation for the format and
// setStateInformation for what a v1 blob (which has none of this) does instead.
static const juce::Identifier kParamMapTag     ("ParamMap");
static const juce::Identifier kIdSchemeId      ("idScheme");
static const juce::Identifier kSlotTag         ("Slot");
static const juce::Identifier kSlotIndexId     ("index");
static const juce::Identifier kSlotParamId     ("id");
// schemaVersion 3 (2026-08-12, C6). The prompt-history list, most-recent-
// first. Same "only write what exists" shape as ParamMap's Slot children —
// a HistoryEntry per retained prompt, none for an empty history.
static const juce::Identifier kHistoryTag      ("PromptHistory");
static const juce::Identifier kHistoryEntryTag ("HistoryEntry");
static const juce::Identifier kHistoryTextId   ("text");

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
        root.setProperty(kGenerationFamilyId, currentGenerationFamily, nullptr);
        root.setProperty(kFamilySourceId, currentGenerationFamilySource, nullptr);
        root.setProperty(kUiStyleId,     currentUiStyle,     nullptr);

        // ── The slot -> identity map (schemaVersion 2) ──────────────────────
        // Without this, a restore knows every knob's VALUE and nothing about
        // which PARAMETER each value belonged to. The recompile would then pack
        // slots by identity in capture order while the restored values sit at
        // their saved indices, and a patch whose params do not happen to capture
        // in the saved order hands every knob its neighbour's value.
        //
        // `idScheme` records the derivation rule that produced these strings
        // (ParamIdentity::kSchemeVersion). It is not decoration: the rule is a
        // one-way door, and a future change to it must be able to recognise an
        // old blob and migrate rather than silently mismatch every id.
        //
        // Only OCCUPIED slots are written. An empty element per free slot would
        // trade a fixed 64 children for nothing readable.
        juce::ValueTree mapNode(kParamMapTag);
        mapNode.setProperty(kIdSchemeId, juce::String(ParamIdentity::kSchemeVersion), nullptr);
        for (size_t slot = 0; slot < currentSlotIds.size(); ++slot)
        {
            if (currentSlotIds[slot].empty())
                continue;
            juce::ValueTree slotNode(kSlotTag);
            slotNode.setProperty(kSlotIndexId, static_cast<int>(slot), nullptr);
            slotNode.setProperty(kSlotParamId, juce::String(currentSlotIds[slot]), nullptr);
            mapNode.appendChild(slotNode, nullptr);
        }
        root.appendChild(mapNode, nullptr);

        // ── Prompt history (schemaVersion 3) ────────────────────────────────
        juce::ValueTree historyNode(kHistoryTag);
        for (const auto& prompt : promptHistory)
        {
            juce::ValueTree entry(kHistoryEntryTag);
            entry.setProperty(kHistoryTextId, prompt, nullptr);
            historyNode.appendChild(entry, nullptr);
        }
        root.appendChild(historyNode, nullptr);
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

    // ── Seed the slot assignment BEFORE the recompile ───────────────────────
    // The recompile below is what rebuilds the pool, and it reclaims a slot only
    // if the identity map already says that id held it. Seeding here is therefore
    // the difference between "every knob comes back where the user left it" and
    // "every knob comes back holding whatever param happens to capture in that
    // position".
    //
    // A v1 blob has no ParamMap, and the correct response is to seed NOTHING. v1
    // values were saved under positional assignment, and an empty map makes
    // remap() pack params in capture order -- i.e. positionally -- which is
    // exactly the layout those values were written under. The old blob restores
    // bit-identically to how it always did, with no migration step, because the
    // fallback and the thing it is falling back to are the same arrangement.
    //
    // An UNRECOGNISED idScheme is also treated as "no map": the ids in it were
    // produced by a rule this build does not have, so honouring them would map
    // values onto whatever those strings happen to collide with now. Falling back
    // to positional loses the improvement for that one blob and cannot corrupt it.
    {
        std::vector<std::string> restoredSlotIds;
        const auto mapNode = root.getChildWithName(kParamMapTag);
        if (mapNode.isValid())
        {
            const juce::String scheme = mapNode.getProperty(kIdSchemeId, juce::String());
            if (scheme == juce::String(ParamIdentity::kSchemeVersion))
            {
                restoredSlotIds.assign(static_cast<size_t>(ParamPool::POOL_SIZE), std::string {});
                for (int c = 0; c < mapNode.getNumChildren(); ++c)
                {
                    const auto slotNode = mapNode.getChild(c);
                    if (slotNode.getType() != kSlotTag)
                        continue;
                    const int slot = slotNode.getProperty(kSlotIndexId, -1);
                    const juce::String id = slotNode.getProperty(kSlotParamId, juce::String());
                    if (slot >= 0 && slot < ParamPool::POOL_SIZE && id.isNotEmpty())
                        restoredSlotIds[static_cast<size_t>(slot)] = id.toStdString();
                }
            }
            else
            {
                juce::Logger::writeToLog(
                    "PluginForge: saved project uses id scheme '" + scheme
                    + "', this build understands '"
                    + juce::String(ParamIdentity::kSchemeVersion)
                    + "'. Restoring by slot position instead.");
            }
        }

        // Empty vector => cleared map => positional packing, which is the v1
        // behaviour. seedIdentityMap handles both cases with one call.
        paramPool.seedIdentityMap(restoredSlotIds);
    }

    // ── Prompt history (schemaVersion 3) ────────────────────────────────────
    // A v1/v2 blob has no PromptHistory node at all: getChildWithName returns
    // an invalid ValueTree, the loop below runs zero times, and
    // setPromptHistory(empty) is exactly the do-nothing fallback the v1-
    // ParamMap case established above -- no older blob was ever written with
    // history in it, so there is nothing to migrate.
    {
        juce::StringArray restoredHistory;
        const auto historyNode = root.getChildWithName(kHistoryTag);
        if (historyNode.isValid())
        {
            for (int c = 0; c < historyNode.getNumChildren(); ++c)
            {
                const auto entry = historyNode.getChild(c);
                if (entry.getType() != kHistoryEntryTag)
                    continue;
                const juce::String text = entry.getProperty(kHistoryTextId, juce::String());
                if (text.isNotEmpty())
                    restoredHistory.add(text);
            }
        }
        setPromptHistory(restoredHistory);
    }

    const juce::String source = root.getProperty(kFaustSourceId, juce::String());
    const juce::String prompt = root.getProperty(kPromptId,      juce::String());
    const juce::String legacyFamily = PF_IS_SYNTH ? "synth" : "effect";
    const juce::String family = version >= 3
        ? root.getProperty(kGenerationFamilyId, legacyFamily).toString()
        : legacyFamily;
    const juce::String familySource = version >= 3
        ? root.getProperty(kFamilySourceId, "legacy_default").toString()
        : juce::String("legacy_default");
    // Absent in a pre-2026-07-31 blob; the default is the behaviour those blobs
    // were saved under, so an old session reopens looking exactly as it did.
    const juce::String style  = root.getProperty(kUiStyleId,     "faithful");

    {
        std::lock_guard<std::mutex> lock(metaMutex);
        currentFaustSource = source;
        currentPrompt      = prompt;
        currentGenerationFamily = family;
        currentGenerationFamilySource = familySource;
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
        // Restore: the values replaceState() wrote above are the point of the
        // restore, so this recompile must not reset any of them. See the twin
        // call site in prepareToPlay for why Iterate is not sufficient here.
        loadFaustCode(source, prompt, LoadMode::Restore, family, familySource);
    }
    else
    {
        std::lock_guard<std::mutex> lock(metaMutex);
        pendingRestoreSource = source;
        pendingRestorePrompt = prompt;
        pendingRestoreFamily = family;
        pendingRestoreFamilySource = familySource;
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

juce::StringArray PluginForgeProcessor::promptHistorySnapshot() const
{
    std::lock_guard<std::mutex> lock(metaMutex);
    return promptHistory;
}

void PluginForgeProcessor::setPromptHistory(const juce::StringArray& history)
{
    std::lock_guard<std::mutex> lock(metaMutex);
    promptHistory = history;
}

juce::String PluginForgeProcessor::currentFamily() const
{
    std::lock_guard<std::mutex> lock(metaMutex);
    return currentGenerationFamily;
}

juce::String PluginForgeProcessor::currentFamilySource() const
{
    std::lock_guard<std::mutex> lock(metaMutex);
    return currentGenerationFamilySource;
}

juce::StringArray PluginForgeProcessor::currentLabelsForTest() const
{
    std::lock_guard<std::mutex> lock(metaMutex);
    return currentLabels;   // by value, under the lock: the compile thread rewrites this
}

std::vector<std::string> PluginForgeProcessor::slotIdsForTest() const
{
    std::lock_guard<std::mutex> lock(metaMutex);
    return currentSlotIds;   // by value, under the lock, same as the labels above
}

int PluginForgeProcessor::mappedSlotCountForTest() const
{
    std::lock_guard<std::mutex> lock(metaMutex);
    int n = 0;
    for (const auto& id : currentSlotIds)
        if (! id.empty())
            ++n;
    return n;
}

juce::AudioProcessorEditor* PluginForgeProcessor::createEditor()
{
    return new PluginForgeEditor(*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new PluginForgeProcessor();
}
