#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "FaustEngine.h"
#include <array>
#include <atomic>
#include <unordered_map>
#include <vector>
#include <string>

// Pre-allocates POOL_SIZE generic float parameters.
// When Faust compiles, remap() re-binds them to match the new patch.
class ParamPool
{
public:
    static constexpr int POOL_SIZE = 64;

    explicit ParamPool(juce::AudioProcessorValueTreeState& apvts);

    // What remap() decided. Returned rather than stored so the caller can copy it
    // under its OWN lock (PluginProcessor's metaMutex, which already guards
    // currentLabels for exactly this compile-thread -> message-thread hop) --
    // this class then needs no lock of its own, and nothing the audio thread
    // touches ever acquires one.
    struct RemapResult
    {
        // Index = slot, value = the ParamIdentity id occupying it, empty for a
        // free slot. Exactly POOL_SIZE entries. This is what gets persisted.
        std::vector<std::string> slotIds;

        // Params that could not be given a slot because all POOL_SIZE were taken
        // (PF-051). Previously these were dropped with no record anywhere: the
        // old remap() looped to POOL_SIZE and never looked at params[64+], and
        // pushToFaust's min(infos.size(), slots.size()) hid it a second time. A
        // patch could publish 70 controls, six of which were unreachable and
        // unmentioned. Named here so a caller can say so.
        std::vector<std::string> overflowed;

        // Slots whose OCCUPANT CHANGED in this remap -- a newcomer, or a
        // different id than the one that held the slot before.
        //
        // ⚠️ This is what makes Iterate safe, and it is not optional. Iterate
        // deliberately does not reset values, which is correct for a param that
        // RECLAIMED its slot -- that is the whole feature. But a NEWCOMER landing
        // on a slot vacated by a deleted param would silently inherit that dead
        // param's value: "Drive" would appear holding the position the user had
        // set on "Res". That is PF-020's hazard arriving through the new
        // assignment path instead of the old positional one. The caller seeds
        // exactly these slots and nothing else.
        std::vector<int> newlyAssignedSlots;

        int assignedCount = 0;
    };

    // Called after FaustEngine::compile() succeeds, on the compile thread.
    //
    // Slots are assigned BY IDENTITY, not by position (ParamIdentity.h):
    //   1. every param whose id held a slot in the previous patch reclaims that
    //      slot, so the user's value follows the parameter it belongs to;
    //   2. everything else takes the lowest free slot;
    //   3. slots vacated by a param that no longer exists are freed, and the
    //      caller zeroes them so a newcomer cannot inherit a stale value (the
    //      PF-020 hazard, generalised);
    //   4. anything that still has no slot lands in RemapResult::overflowed.
    RemapResult remap(const FaustEngine::ParamList& params);

    // Forget the previous patch's slot assignment, so the next remap() packs from
    // slot 0. Called for LoadMode::Fresh, which resets values to patch defaults
    // anyway -- retaining an assignment whose values are about to be discarded
    // would only fragment the pool.
    void clearIdentityMap();

    // Seed the assignment from a restored project (schemaVersion 2 blob) so the
    // recompile that follows setStateInformation reclaims the SAME slots the
    // session was saved with. Without this, restore would pack from slot 0 in
    // identity order while the restored APVTS values sit at their saved indices,
    // and every knob would come back holding some other knob's value.
    void seedIdentityMap(const std::vector<std::string>& slotIds);

    // Push current APVTS values into the Faust DSP object each block
    void pushToFaust(FaustEngine& engine);

    // The per-slot ParamInfo currently published, indexed by SLOT. Unused slots
    // carry a null-zone sentinel. Compile-thread callers only, and only inside
    // the compile callback -- the zone pointers belong to the live DSP (see the
    // LIFETIME note on ParamInfo::zone).
    //
    // Exists because seeding a slot to its patch default is now a per-SLOT
    // question rather than a per-index one: resetMappedSlotsToDefaults used to
    // read params[i] for slot i, which identity-keyed assignment makes wrong the
    // moment a slot is reclaimed out of order.
    const FaustEngine::ParamList& publishedSlots() const;

    // Shared ID scheme between ParamPool (which looks slots up) and
    // PluginProcessor::createParameterLayout() (which creates them) -- static so
    // both sides use the exact same one definition instead of duplicating the
    // "macro_N" string in two files.
    //
    // ⚠️ This is the HOST-facing identity and it is ordinal FOREVER (ADR-004).
    // DAW automation lanes bind to these strings. ParamIdentity's semantic id is
    // a separate, internal layer above it -- the id is the key, the slot is the
    // storage. Do not conflate them.
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
    //
    // Indexed by SLOT. That was already true positionally; it is now true by
    // assignment, which is the whole of this change.
    std::array<FaustEngine::ParamList, 2> infoBuffers;
    std::atomic<int>                      activeBufferIndex { 0 };

    // id -> slot, carried across compiles. Written and read ONLY by remap(),
    // clearIdentityMap() and seedIdentityMap() -- the first two on the compile
    // thread inside FaustEngine::compile()'s compileMutex, the third before any
    // compile is queued. The audio thread never touches it, which is the only
    // reason an unordered_map -- allocating, hashing, emphatically not RT-safe --
    // is allowed to exist in this class. Do not read it from pushToFaust.
    std::unordered_map<std::string, int> idToSlot;

    std::vector<juce::RangedAudioParameter*> slots;
};
