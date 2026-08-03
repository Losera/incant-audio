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

void ParamPool::clearIdentityMap()
{
    idToSlot.clear();
}

void ParamPool::seedIdentityMap(const std::vector<std::string>& slotIds)
{
    idToSlot.clear();
    for (size_t slot = 0; slot < slotIds.size() && slot < static_cast<size_t>(POOL_SIZE); ++slot)
        if (! slotIds[slot].empty())
            idToSlot[slotIds[slot]] = static_cast<int>(slot);
}

const FaustEngine::ParamList& ParamPool::publishedSlots() const
{
    return infoBuffers[static_cast<size_t>(activeBufferIndex.load(std::memory_order_acquire))];
}

ParamPool::RemapResult ParamPool::remap(const FaustEngine::ParamList& params)
{
    // SUBTLE: single-writer — two remap() calls can never overlap. Both run
    // inside FaustEngine::compile()'s compileMutex lock_guard scope (the lock is
    // held for the whole compile lambda, including the cb() call that triggers
    // remap() — verified against FaustEngine.cpp). So a relaxed load here is
    // safe: this thread is the only writer and already knows the last index it
    // published itself.
    int writeIndex = 1 - activeBufferIndex.load(std::memory_order_relaxed);
    auto& buffer = infoBuffers[static_cast<size_t>(writeIndex)];

    // ── Slot assignment, by identity ────────────────────────────────────────
    // Two passes, and the order between them is the whole point. Reclaimers go
    // first so that a param which held slot 7 last time gets slot 7 back even if
    // an earlier-listed newcomer would otherwise have taken it. Interleaving the
    // passes would make retention depend on Faust's alphabetical emission order,
    // which is exactly the ordinal coupling this replaces.
    std::array<int, POOL_SIZE> slotToParam;
    slotToParam.fill(-1);

    std::vector<int> paramToSlot(params.size(), -1);

    // Who held each slot BEFORE this remap. Needed to answer "did this slot
    // change hands", which is what tells the caller which slots must be seeded
    // (see RemapResult::newlyAssignedSlots). Built from idToSlot before it is
    // rebuilt below, because after that the previous assignment is gone.
    std::array<std::string, POOL_SIZE> previousSlotId;
    for (const auto& entry : idToSlot)
        if (entry.second >= 0 && entry.second < POOL_SIZE)
            previousSlotId[static_cast<size_t>(entry.second)] = entry.first;

    // Meters (Faust hbargraph/vbargraph) are OUTPUTS and take no slot. Marked
    // ineligible up front so neither pass can assign one, and so they cost nothing
    // against the 64-slot budget -- a patch with eight meters must not lose eight
    // knobs. pushToFaust consequently never sees them, which is the guarantee that
    // matters: writing a meter's zone would overwrite the DSP's measurement with a
    // knob position every block. See FaustEngine::Kind::Meter.
    std::vector<bool> eligible(params.size(), true);
    for (size_t i = 0; i < params.size(); ++i)
        eligible[i] = FaustEngine::isWritable(params[i].kind);

    // Pass 1 — reclaim.
    for (size_t i = 0; i < params.size(); ++i)
    {
        if (! eligible[i])
            continue;

        const auto it = idToSlot.find(params[i].id);
        if (it == idToSlot.end())
            continue;

        const int slot = it->second;
        if (slot < 0 || slot >= POOL_SIZE)
            continue;                        // corrupt or out-of-range seed
        if (slotToParam[static_cast<size_t>(slot)] != -1)
            continue;                        // two ids claim one slot: first wins,
                                             // the loser falls through to pass 2

        slotToParam[static_cast<size_t>(slot)] = static_cast<int>(i);
        paramToSlot[i] = slot;
    }

    // Pass 2 — everything unclaimed takes the lowest free slot.
    RemapResult result;
    result.slotIds.assign(static_cast<size_t>(POOL_SIZE), std::string {});

    int nextFree = 0;
    for (size_t i = 0; i < params.size(); ++i)
    {
        if (paramToSlot[i] != -1 || ! eligible[i])
            continue;

        while (nextFree < POOL_SIZE && slotToParam[static_cast<size_t>(nextFree)] != -1)
            ++nextFree;

        if (nextFree >= POOL_SIZE)
        {
            // PF-051: no slot left. Recorded rather than dropped -- this is the
            // case that used to vanish without a trace.
            result.overflowed.push_back(params[i].id);
            continue;
        }

        slotToParam[static_cast<size_t>(nextFree)] = static_cast<int>(i);
        paramToSlot[i] = nextFree;
    }

    // ── Publish ─────────────────────────────────────────────────────────────
    // Rebuild the id->slot map from THIS pass only. Rebuilt rather than updated
    // in place so an id that no longer exists cannot keep reserving a slot
    // forever -- a pool that never forgets fills up after a few regenerations
    // and every newcomer overflows.
    idToSlot.clear();

    buffer.clear();
    buffer.reserve(static_cast<size_t>(POOL_SIZE));
    for (int slot = 0; slot < POOL_SIZE; ++slot)
    {
        const int p = slotToParam[static_cast<size_t>(slot)];
        if (p >= 0)
        {
            // Copied whole -- min/max/step/scale/unit drive denormalisation and
            // the zone pointer is the audio thread's write target. Discarding
            // everything but the label here was half of the denormalisation bug.
            // Note: JUCE does not officially support renaming parameters after init.
            // DAW automation lanes display stable IDs (macro_0..macro_63); the
            // semantic name lives in ParamInfo::id and never reaches the host.
            const auto& info = params[static_cast<size_t>(p)];
            buffer.push_back(info);
            idToSlot[info.id] = slot;
            result.slotIds[static_cast<size_t>(slot)] = info.id;
            ++result.assignedCount;

            // Changed hands? Then its stored value belongs to a parameter that is
            // no longer there and the caller must seed it. Comparing ids rather
            // than tracking a "was reclaimed" flag keeps this true for the case
            // where two params swap slots: both changed hands, both get seeded.
            if (previousSlotId[static_cast<size_t>(slot)] != info.id)
                result.newlyAssignedSlots.push_back(slot);
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

    return result;
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
