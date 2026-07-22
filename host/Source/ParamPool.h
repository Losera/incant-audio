#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "FaustEngine.h"
#include <array>
#include <atomic>
#include <vector>
#include <string>

// Pre-allocates POOL_SIZE generic float parameters.
// When Faust compiles, remap() re-labels them to match the new patch.
class ParamPool
{
public:
    static constexpr int POOL_SIZE = 64;

    explicit ParamPool(juce::AudioProcessorValueTreeState& apvts);

    // Called after FaustEngine::compile() succeeds
    void remap(const FaustEngine::ParamList& params);

    // Push current APVTS values into the Faust DSP object each block
    void pushToFaust(FaustEngine& engine);

    // Shared ID scheme between ParamPool (which looks slots up) and
    // PluginProcessor::createParameterLayout() (which creates them) -- static so
    // both sides use the exact same one definition instead of duplicating the
    // "macro_N" string in two files.
    static juce::String slotId(int i) { return "macro_" + juce::String(i); }

private:
    juce::AudioProcessorValueTreeState& apvts;

    // SUBTLE: double-buffered to avoid a data race between remap() (compile
    // thread, writer) and pushToFaust() (audio thread, reader every block).
    // remap() writes the inactive buffer then publishes it via a release-store
    // to activeBufferIndex; pushToFaust() acquire-loads the index once and reads
    // only that buffer for the block. Neither buffer is ever freed while live,
    // so there's no delete to synchronize (unlike FaustEngine's DSP pointer swap).
    //
    // These hold the FULL ParamInfo, not just labels: pushToFaust needs each
    // param's min/max/step/scale to denormalise the slot, and its zone pointer
    // to write the result without a string lookup. The zone pointers are why
    // the buffer that is NOT active must never be read -- see the LIFETIME note
    // on ParamInfo::zone in FaustEngine.h.
    std::array<FaustEngine::ParamList, 2> infoBuffers;
    std::atomic<int>                      activeBufferIndex { 0 };

    std::vector<juce::RangedAudioParameter*> slots;
};
