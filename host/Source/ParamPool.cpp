#include "ParamPool.h"

ParamPool::ParamPool(juce::AudioProcessorValueTreeState& apvts_)
    : apvts(apvts_)
{
    // The TODO this replaced was confirmed live: PluginProcessor constructs apvts
    // via the ParameterLayout-taking constructor, which already makes the internal
    // ValueTree `state` valid -- calling the legacy createAndAddParameter() here
    // afterward tripped jassert(!state.isValid()) in
    // juce_AudioProcessorValueTreeState.cpp on every one of the 64 slots. All
    // POOL_SIZE params are now created declaratively in
    // PluginProcessor::createParameterLayout() (using the same slotId() scheme)
    // and passed to the apvts constructor; ParamPool just looks its slots up here.
    for (int i = 0; i < POOL_SIZE; ++i)
        slots.push_back(apvts.getParameter(slotId(i)));
}

void ParamPool::remap(const FaustEngine::ParamList& params)
{
    // SUBTLE: single-writer — two remap() calls can never overlap. Both run
    // inside FaustEngine::compile()'s compileMutex lock_guard scope (the lock is
    // held for the whole compile lambda, including the cb() call that triggers
    // remap() — verified against FaustEngine.cpp). So a relaxed load here is
    // safe: this thread is the only writer and already knows the last index it
    // published itself.
    int writeIndex = 1 - activeBufferIndex.load(std::memory_order_relaxed);
    auto& buffer = labelBuffers[static_cast<size_t>(writeIndex)];

    buffer.clear();
    for (int i = 0; i < POOL_SIZE; ++i)
    {
        if (static_cast<size_t>(i) < params.size())
        {
            const auto& p = params[static_cast<size_t>(i)];
            buffer.push_back(p.label);
            // Note: JUCE does not officially support renaming parameters after init.
            // DAW automation lanes display stable IDs (macro_0..macro_63).
            // Full label rename requires a custom AudioProcessorParameter subclass.
            juce::ignoreUnused(p);
        }
        else
        {
            buffer.push_back("(unused)");
        }
    }

    // SUBTLE: release pairs with the acquire load in pushToFaust(). Publishes
    // every write to `buffer` above before the audio thread can observe the new
    // index — the audio thread never sees a partially-rebuilt buffer.
    activeBufferIndex.store(writeIndex, std::memory_order_release);
}

// Called from processBlock() on the audio thread. Pushes current APVTS parameter
// values into the Faust DSP via FaustEngine::setParamValue().
//
// RT-safe: one atomic acquire-load, then reads of a vector guaranteed not to be
// concurrently mutated (remap() only ever writes the *other* index — see the
// SUBTLE comment there). No allocation, no lock, no mutation on this path.
void ParamPool::pushToFaust(FaustEngine& engine)
{
    if (!engine.isReady())
        return;

    const auto& labels =
        labelBuffers[static_cast<size_t>(activeBufferIndex.load(std::memory_order_acquire))];

    for (size_t i = 0; i < labels.size(); ++i)
    {
        if (labels[i] == "(unused)")
            continue;

        auto* fp = dynamic_cast<juce::AudioParameterFloat*>(slots[i]);
        if (fp)
            engine.setParamValue(labels[i], fp->get());
    }
}
