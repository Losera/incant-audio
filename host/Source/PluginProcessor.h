#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "FaustEngine.h"
#include "ParamPool.h"
#include "OutputGuard.h"
#include "NoteRing.h"
#include <atomic>
#include <mutex>

// Is this build the instrument target?
//
// host/CMakeLists.txt declares the SAME sources twice: PluginForgeHost
// (IS_SYNTH FALSE, VST3 category Fx) and PluginForgeSynth (IS_SYNTH TRUE,
// NEEDS_MIDI_INPUT TRUE, category Instrument). JUCE turns IS_SYNTH into the
// JucePlugin_IsSynth macro -- but ONLY for plugin targets. The console-app
// harnesses (OfflineRenderTest, EditorSessionTest, ParamPoolTsanTest, ...)
// compile this same file through juce_add_console_app, which emits no
// JucePlugin_* definitions at all: verified, OfflineRenderTest's generated
// Defs.txt contains zero JucePlugin_ symbols.
//
// So testing JucePlugin_IsSynth directly would be a bare undefined identifier
// in eight targets. This derives from JUCE where JUCE provides it, defaults to
// "effect" everywhere else, and stays overridable from the command line so a
// future harness can compile an instrument-configured processor without a
// plugin wrapper.
#ifndef PF_IS_SYNTH
  #if defined(JucePlugin_IsSynth)
    #define PF_IS_SYNTH JucePlugin_IsSynth
  #else
    #define PF_IS_SYNTH 0
  #endif
#endif

class PluginForgeProcessor : public juce::AudioProcessor
{
public:
    PluginForgeProcessor();
    ~PluginForgeProcessor() override;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    // Hosts call reset() to drop any sounding tail — transport stop, loop jump,
    // bypass. For an instrument that means releasing a held note; without it the
    // gate stays 1 and the patch drones with no MIDI able to stop it.
    void reset() override;

    // Which bus layouts a host may negotiate.
    //
    // There was no override at all until now, so JUCE's default applied —
    // juce_AudioProcessor.h:1329, `return true` for ANY layout. That is not a
    // safe default here: FaustEngine::kMaxChannels is 2 and its `scratch` buffer
    // is sized to it, so a host offering 5.1 would be accepted and then hit the
    // defensive bail in FaustEngine::process, which silently passes the DRY input
    // through. A plugin that accepts a layout and then does nothing is worse than
    // one that declines it, because the host has no way to know.
    //
    // The instrument target additionally accepts a DISABLED input bus — a synth
    // has nothing to process, and refusing that is what keeps a plugin off an
    // instrument track.
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "PluginForge Host"; }
    // The instrument target must advertise MIDI input or no host will route
    // notes to it. The Fx target deliberately still says false: claiming MIDI
    // input on an effect changes how DAWs present it for no gain.
    //
    // This flag does NOT gate processBlock's MIDI walk. That walk is gated on
    // FaustEngine::isInstrument() -- a property of the loaded patch, not of the
    // build -- so the console-app harnesses, which compile with PF_IS_SYNTH == 0
    // and get no plugin wrapper, still exercise the real note path.
    bool acceptsMidi() const override  { return PF_IS_SYNTH != 0; }
    bool producesMidi() const override { return false; }

    // An instrument's sound outlives the note that started it -- a release tail
    // is the whole point of an envelope. Reporting 0 here lets a host stop
    // calling processBlock the moment the last note ends, truncating the
    // release. Effects keep 0: nothing in the Fx path rings on after silence.
    // TODO(phase-1): derive this from the patch's declared release time once
    // envelopes are captured; 2 s is a placeholder that covers typical pads.
    double getTailLengthSeconds() const override { return PF_IS_SYNTH ? 2.0 : 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    // State persistence (P11 / docs/ux_roadmap.md Phase 1). Serialises the Faust
    // source + originating prompt + the 64 APVTS macro values + the slot-label map
    // as a versioned ValueTree->XML blob. The DSP itself is never serialised: the
    // Faust source is the artifact of record and setState recompiles it. Format is
    // the COLLABORATION.md §2 trigger-3 contract documented above getStateInformation
    // in the .cpp.
    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    // Persisted-state schema version. Bump when the blob layout changes; setState
    // rejects a blob whose version it does not understand rather than misreading it.
    // 3 as of 2026-08-11: the blob also carries the resolved generation family
    // and whether it was explicit, inferred, defaulted, or migrated. Version 2
    // introduced the slot -> ParamIdentity map and
    // the id-derivation scheme that produced it. A REAL bump, not an amendment
    // like <SlotLabels> or `uiStyle` were: those were fields nothing depended on,
    // whereas a v1 blob genuinely lacks information a v2 restore uses, and the two
    // take different code paths (see setStateInformation).
    static constexpr int kStateSchemaVersion = 3;

    // How a newly loaded patch treats the macro values already in the APVTS.
    //
    // PF-020. Before this existed, there was no fresh-vs-iterate concept anywhere,
    // and "fresh" was an accident of whether the editor happened to be open:
    // macro values were reset only by UI-layer seeding (ParamGridPanel), so with
    // the editor closed the previous patch's values survived and pushToFaust drove
    // the NEW patch's zones with the OLD values BY SLOT INDEX
    // (ParamPool.cpp:94-96). A knob labelled "Cutoff" in patch A silently drove
    // "Feedback" in patch B. The human observed this as generated plugins
    // "reiterating on each other rather than starting fresh", and it is the
    // likeliest cause of the flaky P6 results (#4/#5/#7 failed under a
    // contaminated session and passed clean).
    enum class LoadMode
    {
        Fresh,     // reset every mapped slot to the new patch's declared default
        Iterate,   // keep the value of every param that RECLAIMED its slot; seed
                   // only slots that changed hands (refine)
        Restore    // keep every slot exactly as it is -- seed nothing at all
    };

    // Called from the editor when a new Faust string arrives. The optional prompt
    // is the natural-language request that produced the code; it is retained for
    // persistence and future "refine" flows.
    //
    // Defaults to Fresh: a newly generated patch is a NEW plugin, and inheriting
    // the previous patch's knob positions by slot index is the PF-020 defect.
    // State restore must pass Iterate — it has just written the saved values and
    // resetting them would discard exactly what it restored.
    void loadFaustCode(const juce::String& faustCode, const juce::String& prompt = {},
                       LoadMode mode = LoadMode::Fresh,
                       const juce::String& family = {},
                       const juce::String& familySource = {});

    // Set by the editor to surface a Faust compile failure (as opposed to an
    // LLM-generation failure, which the editor already handles from its own
    // subprocess result) in the UI — including the BYO-LLM paste-back flow, where
    // the user feeds this compiler stderr to their own LLM (FLEET req #7). Named
    // to pair with onFaustCompileSuccess below. Fires on FaustEngine's detached
    // compile thread, not the message thread. Whoever assigns this must hop to the
    // message thread themselves before touching any UI component, the same way
    // PluginEditor's existing callbacks do via juce::MessageManager::callAsync.
    std::function<void(const juce::String& error)> onFaustCompileFailure;

    // DEPRECATED transitional alias for onFaustCompileFailure. The rename (FLEET
    // req #7) pairs Success/Failure; the old name lived in the editor's call site
    // (PluginEditor.cpp:38, S3's shell lane), which this session cannot edit, so
    // both names are fired from the compile error path to keep main building while
    // S3 migrates the assignment to onFaustCompileFailure. REMOVE this member once
    // S3 has adopted the new name (tracked in FLEET req #7).
    std::function<void(const juce::String& error)> onFaustCompileError;

    // Latched output-guard state, polled by the editor's 30Hz timer. True means
    // the generated DSP produced NaN/Inf or sat at/over 0 dBFS for half a second
    // and has been muted; it stays true until the next successful compile.
    bool isOutputMuted() const { return outputGuard.isMuted(); }
    OutputGuard::Trip outputTrip() const { return outputGuard.trippedBy(); }

    // Per-block output peak (post-DSP), published for the editor's level meter.
    // Written with a relaxed store in processBlock (RT-safe: one atomic store,
    // no allocation); read by the editor's 30Hz repaint timer. Relaxed is enough —
    // a meter tolerates seeing a stale block, there is no ordering dependency.
    std::atomic<float> outputLevel { 0.0f };

    // ── On-screen / computer keyboard notes ─────────────────────────────────
    // Called from the MESSAGE THREAD only — the editor's MidiKeyboardState
    // listener callbacks. Lock-free; returns false if the queue is full, having
    // stored nothing (NoteRing.h documents why overflow drops the newest).
    //
    // The ring lives HERE and not in the editor because the editor is destroyed
    // and recreated every time the user closes and reopens the plugin window,
    // while the audio thread keeps running throughout. A queue owned by the
    // editor would be freed underneath processBlock.
    //
    // Events queued while a compile is mid-swap are NOT lost: processBlock's
    // early-return path leaves them in the ring and the next block drains them
    // in order. That is deliberately different from the MidiBuffer path, which
    // drops (see processBlock). A hardware note-off in the swap window is gone
    // for good because the buffer is gone; a keyboard note-off is still queued,
    // and honouring it costs nothing and prevents a stuck key.
    bool pushKeyboardNote(int note, int velocity, bool on) noexcept
    {
        return noteRing.push({ static_cast<unsigned char>(note),
                               static_cast<unsigned char>(velocity),
                               on });
    }

    // Advisory, for tests and diagnostics. Non-zero means the audio thread
    // stopped draining long enough to fill 255 events.
    std::uint32_t droppedKeyboardNotes() const noexcept { return noteRing.droppedCount(); }

    // ── Sample audition ─────────────────────────────────────────────────────
    // Loads a reference WAV and feeds it through the live DSP, replacing the
    // input bus when active. For auditioning generated effects with known
    // source material.
    //
    // Thread-safety handshake (matches the compile-worker idiom):
    //   1. auditionActive = false (seq_cst) — gates new entrants
    //   2. FaustEngine::drainAudioBusy()   — waits for in-flight processBlock
    //   3. auditionBuffer = std::move(...)  — safe: no reader can be inside
    //   4. auditionActive = true (seq_cst)  — re-enables reads of the new buffer
    //
    // The seq_cst load of auditionActive in processBlock, paired with the seq_cst
    // store(false) here, forms a Dekker handshake on audioBusy: if the load reads
    // true (stale), the load is ordered before the store in the total order — so
    // the drain sees that processBlock's fetch_add and spins until it exits.
    // (Rationale: FaustEngine.h:255-270.)
    //
    // loadAuditionSample: NON-AUDIO THREAD ONLY. Reads audio into a local buffer,
    // drains audioBusy, then moves it into auditionBuffer. The sample browser calls
    // it from its worker so file decoding never blocks the message thread.
    //
    // auditionPos is atomic, relaxed, only written by the audio thread; reset to
    // 0 on every load/set. auditionActive is atomic and written only on the
    // message thread.
    void loadAuditionSample(const juce::File& wavFile);
    enum class AuditionMode { Stopped = 0, OneShot = 1, Loop = 2 };
    void setAuditionMode(AuditionMode mode);
    AuditionMode auditionMode() const noexcept
    {
        return static_cast<AuditionMode>(auditionModeValue.load(std::memory_order_relaxed));
    }
    void retriggerAudition() noexcept { auditionPos.store(0, std::memory_order_relaxed); }
    void setAuditionActive(bool active);
    bool isAuditionActive() const noexcept { return auditionMode() != AuditionMode::Stopped; }

    // Set by the editor to surface true JIT-ready status (ADR-011 "point E":
    // the Generate button re-enables when the subprocess returns, but the DSP
    // only goes live when this fires). Same threading contract as
    // onFaustCompileFailure: compile thread, hop via callAsync before touching UI.
    // Fires after ParamPool::remap() has published the new labels, just before
    // FaustEngine flips ready=true — "success" here means audio is about to
    // switch over, not that it already has. Receives the full captured param
    // list so the editor can label its knobs; receivers must copy what they
    // keep (the vector lives on the compile thread's stack).
    std::function<void(const FaustEngine::ParamList& params)> onFaustCompileSuccess;

    // ── UI control style — a VIEW property, not a parameter ─────────────────
    // Deliberately NOT an AudioParameterChoice: that would add an entry to the
    // host's automation lane and to a parameter count that is already dynamic
    // per generated patch. It is also deliberately not reachable from anything
    // that can recompile — setUiStyle() has no handle on FaustEngine, so a style
    // change is structurally incapable of touching the DSP.
    //
    // Message-thread only, like the rest of the meta block. Fires
    // onUiStyleChanged so a second open editor (or one reopened after a project
    // load) picks up the stored choice instead of snapping back to the default.
    juce::String uiStyle() const;
    void         setUiStyle(const juce::String& styleName);
    std::function<void(const juce::String& styleName)> onUiStyleChanged;

    juce::AudioProcessorValueTreeState apvts;

    // The Faust source currently live, or empty if nothing has compiled. Product
    // API, not a test hook: CodeEditorPanel reads it to show the user what was
    // generated (docs/ux_roadmap.md Phase 3a). metaMutex-guarded and never taken
    // on the audio thread, so any non-audio thread may call it.
    //
    // It began as currentSourceForTest() — the source of record was observable
    // only to tests for five days while the panel meant to display it stayed an
    // empty stub. The name is now honest about who reads it.
    juce::String currentSource() const;

    // Test-only accessor for the retained prompt, so the state round-trip test
    // can assert what setState restored without reaching into private members.
    juce::String currentPromptForTest() const;
    juce::String currentFamily() const;
    juce::String currentFamilySource() const;

    // Test-only alias retained so the existing harnesses keep reading the way
    // they were written. Prefer currentSource() in new code.
    juce::String currentSourceForTest() const { return currentSource(); }

    // Test-only. The live slot->label map. This used to be observable only by
    // serialising a blob and counting <SlotLabels> children; that node was
    // dropped from the persisted format (see getStateInformation), so the tests
    // that care about the mapping read it here instead — from the in-memory
    // record, which is where it always actually lived.
    juce::StringArray currentLabelsForTest() const;

    // Test-only. The live slot -> ParamIdentity map, POOL_SIZE entries, empty
    // string for a free slot. The direct observable for identity-keyed
    // assignment: which slot a parameter landed in, and whether it kept it across
    // a regeneration, are both statements about this vector and nothing else.
    std::vector<std::string> slotIdsForTest() const;

    // Test-only. How many slots the live patch actually occupies. NOT the same as
    // currentLabelsForTest().size(): a Faust bargraph is published as a parameter
    // (so it has a label) but is an OUTPUT and takes no slot, so a patch with two
    // knobs and a meter reports 3 labels and 2 slots. That difference is the
    // assertion the meter fixture exists to make.
    int mappedSlotCountForTest() const;

    // Test-only. The rate the LIVE DSP is actually running at, per Faust's own
    // getSampleRate(); 0 when none is live. PF-018's spec assertion is precisely
    // "this follows prepareToPlay", and before the fix it did not.
    int liveDspSampleRateForTest() const { return faustEngine.liveDspSampleRateForTest(); }

    // Test-only. Does the LIVE DSP declare Faust's full voice contract (gate +
    // freq|key + gain|vel)? faustEngine stays private; this is the same
    // forwarding idiom as liveDspSampleRateForTest above.
    //
    // WHY THIS EXISTS. The instrument corpus can hardcode the answer
    // (OfflineRenderTest.cpp's Patch::isInstrument) because it owns its sources.
    // --capture loads an ARBITRARY file and cannot, and a capture that guesses
    // wrong renders a generated synth as silence.
    //
    // Ordering: FaustEngine::isInstrument() is published at DSP-swap time,
    // strictly before the compile callback that loadAndAwait() waits on, so any
    // caller that has observed a successful load also observes this correctly.
    bool isInstrumentForTest() const { return faustEngine.isInstrument(); }

private:
    FaustEngine faustEngine;
    ParamPool   paramPool;
    OutputGuard outputGuard;

    // Notes from the on-screen / computer keyboard, message thread -> audio
    // thread. Written by pushKeyboardNote(), drained inline in processBlock.
    // Declared AFTER faustEngine deliberately: ~PluginForgeProcessor calls
    // faustEngine.shutdown() first (see the destructor), and members are
    // destroyed in reverse declaration order, so the ring outlives the engine
    // teardown rather than the other way round.
    pf::NoteRing noteRing;

    // ── Sample audition buffer ──────────────────────────────────────────────
    // Loaded on the message thread by loadAuditionSample(), read on the audio
    // thread when auditionActive is true. The buffer is swapped via std::move
    // under the drain guard: auditionActive is set false (seq_cst), audioBusy
    // is drained to zero, then the buffer is moved, then auditionActive is set
    // true (seq_cst). processBlock's seq_cst load of auditionActive pairs with
    // the seq_cst store to form the handshake. (See loadAuditionSample header.)
    //
    // auditionPos is atomic, relaxed, only written by the audio thread; reset to
    // 0 on every load/set call. auditionActive is atomic, seq_cst on the write
    // side, seq_cst on the read side in processBlock, relaxed in isAuditionActive
    // (a UI snapshot, not part of the handshake).
    std::atomic<bool> auditionActive { false };
    std::atomic<int> auditionModeValue { static_cast<int>(AuditionMode::Stopped) };
    std::atomic<size_t> auditionPos { 0 };
    juce::AudioBuffer<float> auditionBuffer;  // 2-channel, pre-loaded from WAV
    std::atomic<double> preparedSampleRate { 44100.0 };

    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    // PF-020, LoadMode::Fresh. Seeds every mapped slot from the new patch's own
    // declared default, converting Faust zone units -> 0..1 with the same
    // ParamMap pair pushToFaust uses in the other direction. Unmapped slots are
    // zeroed so a stale value cannot reappear if a later patch maps that index.
    // Takes no ParamList: with identity-keyed assignment the "which param holds
    // slot i" question has exactly one answer and ParamPool owns it, so this
    // reads paramPool.publishedSlots() instead of indexing a list by slot number.
    // See the body for why passing the list would reintroduce the PF-001 shape.
    void resetMappedSlotsToDefaults();

    // Seed ONLY the listed slots to their (new) param's declared default. Used by
    // LoadMode::Iterate for the slots ParamPool reports as having changed hands --
    // see RemapResult::newlyAssignedSlots for why retaining a value there would
    // hand a newcomer a deleted parameter's position.
    void seedSlotsToDefaults(const std::vector<int>& slotsToSeed);

    // ── Retained generation metadata ────────────────────────────────────────
    // The Faust source, its originating prompt, and the current slot->label map.
    // Written by loadFaustCode() (whatever thread called it) and the compile
    // callback (compile thread); read by getStateInformation() (message thread).
    // Guarded by metaMutex — none of these are ever touched on the audio thread,
    // so a plain mutex is correct here (unlike the lock-free swap in FaustEngine).
    mutable std::mutex metaMutex;
    juce::String       currentFaustSource;
    juce::String       currentPrompt;
    juce::String       currentGenerationFamily { PF_IS_SYNTH ? "synth" : "effect" };
    juce::String       currentGenerationFamilySource { "legacy_default" };
    // The user's chosen control style, by name (see ParamGridPanel::ControlStyleName).
    // Stored as a string rather than an int so the state blob stays readable and a
    // future style can be added without renumbering an enum that is already on disk.
    //
    // NOT on apvts.state, which would have been the obvious home: replaceState()
    // is `state = newState` (juce_AudioProcessorValueTreeState.cpp:391), a plain
    // ValueTree assignment that rebinds the object — so any ValueTree::Listener
    // registered on apvts.state is silently dropped the first time a project is
    // reloaded. A style stored there would persist but stop notifying, which is
    // the failure mode this project keeps re-learning. This member plus an
    // explicit callback cannot rot that way.
    juce::String       currentUiStyle { "faithful" };
    juce::StringArray  currentLabels;

    // Slot -> ParamIdentity id for the live patch, POOL_SIZE entries, empty
    // string for a free slot. Written in the compile callback beside
    // currentLabels and under the same metaMutex, for the same reason: it is
    // produced on the compile thread and read by getStateInformation() on the
    // message thread.
    //
    // This is the half of the parameter model that has to survive a project
    // reload. Without it a restored session knows every knob's VALUE (the APVTS
    // tree) and nothing about which parameter each value belonged to, so the
    // recompile would repack slots by identity order and hand every knob its
    // neighbour's value. It is the reason schemaVersion moved to 2.
    std::vector<std::string> currentSlotIds;

    // A restore blob can arrive (setStateInformation) before the host has called
    // prepareToPlay, at which point FaustEngine still holds the default 44100 Hz
    // sample rate (FaustEngine.cpp:154-158) and would JIT the DSP at the wrong
    // rate. So the restore recompile is deferred: setState stashes the source here
    // and prepareToPlay kicks it once the real sample rate is known. Guarded by
    // metaMutex; `prepared` gates which side fires the compile.
    std::atomic<bool> prepared { false };
    juce::String      pendingRestoreSource;
    juce::String      pendingRestorePrompt;
    juce::String      pendingRestoreFamily;
    juce::String      pendingRestoreFamilySource;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginForgeProcessor)
};
