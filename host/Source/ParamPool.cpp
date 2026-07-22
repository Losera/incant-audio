#include "ParamPool.h"
#include "ParamMap.h"
#include <algorithm>

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
    auto& buffer = infoBuffers[static_cast<size_t>(writeIndex)];

    buffer.clear();
    buffer.reserve(static_cast<size_t>(POOL_SIZE));
    for (int i = 0; i < POOL_SIZE; ++i)
    {
        if (static_cast<size_t>(i) < params.size())
        {
            // Copied whole -- min/max/step/scale/unit drive denormalisation and
            // the zone pointer is the audio thread's write target. Discarding
            // everything but the label here was half of the denormalisation bug.
            // Note: JUCE does not officially support renaming parameters after init.
            // DAW automation lanes display stable IDs (macro_0..macro_63).
            // Full label rename requires a custom AudioProcessorParameter subclass.
            buffer.push_back(params[static_cast<size_t>(i)]);
        }
        else
        {
            // Unused slot: a null zone is the sentinel pushToFaust skips on.
            FaustEngine::ParamInfo unused {};
            unused.label = "(unused)";
            unused.zone  = nullptr;
            buffer.push_back(unused);
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
//
// This used to route every parameter through FaustEngine::setParamValue ->
// MapUI, which cost a dynamic_cast, a std::string comparison, and up to three
// std::map<std::string,...> probes PER PARAMETER PER BLOCK — and, on a lookup
// miss, an fprintf(stderr) — i.e. an I/O syscall on the audio thread. (That
// fprintf is what produced the ~1,100-error episode recorded in CLAUDE.md.)
// Writing the captured zone pointer directly removes all of it: the whole loop
// is now a bounds check, a normalised read, arithmetic, and a store.
void ParamPool::pushToFaust(FaustEngine& engine)
{
    if (!engine.isReady())
        return;

    const auto& infos =
        infoBuffers[static_cast<size_t>(activeBufferIndex.load(std::memory_order_acquire))];

    const size_t count = std::min(infos.size(), slots.size());
    for (size_t i = 0; i < count; ++i)
    {
        const auto& info = infos[i];
        if (info.zone == nullptr)      // unused slot
            continue;

        // getValue() is the parameter's normalised 0-1 value and is virtual on
        // the base class, so no dynamic_cast is needed to reach it. The slots
        // are declared with a 0-1 range, so this is also exactly what the host
        // automation lane holds.
        const float norm = slots[i]->getValue();

        *info.zone = static_cast<FAUSTFLOAT>(ParamMap::mapSlotToZone(norm, info));
    }
}
