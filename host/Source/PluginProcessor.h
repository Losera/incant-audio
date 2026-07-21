#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "FaustEngine.h"
#include "ParamPool.h"

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

    void getStateInformation(juce::MemoryBlock&) override {}
    void setStateInformation(const void*, int) override {}

    // Called from editor when a new Faust string arrives
    void loadFaustCode(const juce::String& faustCode);

    // Set by the editor to surface a Faust compile failure (as opposed to an
    // LLM-generation failure, which the editor already handles from its own
    // subprocess result) in the UI. Fires on FaustEngine's detached compile
    // thread, not the message thread. Whoever assigns this must hop to the
    // message thread themselves before touching any UI component, the same
    // way PluginEditor's existing generateButton.onClick callbacks do via
    // juce::MessageManager::callAsync.
    std::function<void(const juce::String& error)> onFaustCompileError;

    // Per-block output peak (post-DSP), published for the editor's level meter.
    // Written with a relaxed store in processBlock (RT-safe: one atomic store,
    // no allocation); read by the editor's 30Hz repaint timer. Relaxed is enough —
    // a meter tolerates seeing a stale block, there is no ordering dependency.
    std::atomic<float> outputLevel { 0.0f };

    // Set by the editor to surface true JIT-ready status (ADR-011 "point E":
    // the Generate button re-enables when the subprocess returns, but the DSP
    // only goes live when this fires). Same threading contract as
    // onFaustCompileError: compile thread, hop via callAsync before touching UI.
    // Fires after ParamPool::remap() has published the new labels, just before
    // FaustEngine flips ready=true — "success" here means audio is about to
    // switch over, not that it already has. Receives the full captured param
    // list so the editor can label its knobs; receivers must copy what they
    // keep (the vector lives on the compile thread's stack).
    std::function<void(const FaustEngine::ParamList& params)> onFaustCompileSuccess;

    juce::AudioProcessorValueTreeState apvts;

private:
    FaustEngine faustEngine;
    ParamPool   paramPool;

    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginForgeProcessor)
};
