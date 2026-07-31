#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "FaustEngine.h"
#include "ParamPool.h"
#include "OutputGuard.h"
#include <atomic>
#include <mutex>

class PluginForgeProcessor : public juce::AudioProcessor
{
public:
    PluginForgeProcessor();
    ~PluginForgeProcessor() override;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "PluginForge Host"; }
    bool acceptsMidi() const override  { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

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
    static constexpr int kStateSchemaVersion = 1;

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
        Iterate    // keep current slot values (refine, and state restore)
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
                       LoadMode mode = LoadMode::Fresh);

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

    // Test-only alias retained so the existing harnesses keep reading the way
    // they were written. Prefer currentSource() in new code.
    juce::String currentSourceForTest() const { return currentSource(); }

    // Test-only. The live slot->label map. This used to be observable only by
    // serialising a blob and counting <SlotLabels> children; that node was
    // dropped from the persisted format (see getStateInformation), so the tests
    // that care about the mapping read it here instead — from the in-memory
    // record, which is where it always actually lived.
    juce::StringArray currentLabelsForTest() const;

    // Test-only. The rate the LIVE DSP is actually running at, per Faust's own
    // getSampleRate(); 0 when none is live. PF-018's spec assertion is precisely
    // "this follows prepareToPlay", and before the fix it did not.
    int liveDspSampleRateForTest() const { return faustEngine.liveDspSampleRateForTest(); }

private:
    FaustEngine faustEngine;
    ParamPool   paramPool;
    OutputGuard outputGuard;

    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    // PF-020, LoadMode::Fresh. Seeds every mapped slot from the new patch's own
    // declared default, converting Faust zone units -> 0..1 with the same
    // ParamMap pair pushToFaust uses in the other direction. Unmapped slots are
    // zeroed so a stale value cannot reappear if a later patch maps that index.
    void resetMappedSlotsToDefaults(const FaustEngine::ParamList& params);

    // ── Retained generation metadata ────────────────────────────────────────
    // The Faust source, its originating prompt, and the current slot->label map.
    // Written by loadFaustCode() (whatever thread called it) and the compile
    // callback (compile thread); read by getStateInformation() (message thread).
    // Guarded by metaMutex — none of these are ever touched on the audio thread,
    // so a plain mutex is correct here (unlike the lock-free swap in FaustEngine).
    mutable std::mutex metaMutex;
    juce::String       currentFaustSource;
    juce::String       currentPrompt;
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

    // A restore blob can arrive (setStateInformation) before the host has called
    // prepareToPlay, at which point FaustEngine still holds the default 44100 Hz
    // sample rate (FaustEngine.cpp:154-158) and would JIT the DSP at the wrong
    // rate. So the restore recompile is deferred: setState stashes the source here
    // and prepareToPlay kicks it once the real sample rate is known. Guarded by
    // metaMutex; `prepared` gates which side fires the compile.
    std::atomic<bool> prepared { false };
    juce::String      pendingRestoreSource;
    juce::String      pendingRestorePrompt;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginForgeProcessor)
};
