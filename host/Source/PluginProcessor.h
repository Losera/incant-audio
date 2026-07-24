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

    // Called from editor when a new Faust string arrives. The optional prompt is
    // the natural-language request that produced the code; it is retained for
    // persistence and future "refine" flows. The default keeps the editor's
    // existing one-arg call compiling while the editor lane adopts the two-arg form
    // (FLEET.md cross-lane request).
    void loadFaustCode(const juce::String& faustCode, const juce::String& prompt = {});

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

    juce::AudioProcessorValueTreeState apvts;

    // Test-only accessors for the retained metadata, so the state round-trip test
    // can assert what setState restored without reaching into private members.
    juce::String currentSourceForTest() const;
    juce::String currentPromptForTest() const;

private:
    FaustEngine faustEngine;
    ParamPool   paramPool;
    OutputGuard outputGuard;

    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    // ── Retained generation metadata ────────────────────────────────────────
    // The Faust source, its originating prompt, and the current slot->label map.
    // Written by loadFaustCode() (whatever thread called it) and the compile
    // callback (compile thread); read by getStateInformation() (message thread).
    // Guarded by metaMutex — none of these are ever touched on the audio thread,
    // so a plain mutex is correct here (unlike the lock-free swap in FaustEngine).
    mutable std::mutex metaMutex;
    juce::String       currentFaustSource;
    juce::String       currentPrompt;
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
