#pragma once
#include <atomic>
#include <cstddef>
#include <cstdint>

// ---------------------------------------------------------------------------
// NoteRing — a single-producer/single-consumer lock-free queue carrying note
// events from the message thread to the audio thread.
//
// WHY THIS EXISTS. The on-screen keyboard is a juce::MidiKeyboardComponent, and
// the idiomatic way to drive a plugin from one is
// MidiKeyboardState::processNextMidiBuffer() called inside processBlock. That is
// FORBIDDEN here: MidiKeyboardState holds a juce::CriticalSection
// (juce_MidiKeyboardState.h:182) and processNextMidiBuffer LOCKS IT
// (juce_MidiKeyboardState.cpp:140). processBlock takes no locks, ever
// (CLAUDE.md; .claude/rules/tier2-evidence.md). So the keyboard's callbacks --
// which arrive on the message thread -- push here, and processBlock drains here,
// and the two threads never share a lock.
//
// PRODUCER is the message thread (MidiKeyboardState::Listener callbacks).
// CONSUMER is the audio thread (PluginForgeProcessor::processBlock).
// Exactly one of each. This is NOT safe for two producers, and the keyboard
// component plus computer-keyboard input are the same producer because both
// arrive as MidiKeyboardState callbacks on the message thread.
//
// No JUCE and no Faust types on purpose, exactly like ParamGridLayout.h and
// ParamIdentity.h: the queue is pure index arithmetic over a fixed array, and
// must be exercisable without a compiled DSP, a message thread or a live editor.
// host/tests/NoteRingTest.cpp is that exercise.
// ---------------------------------------------------------------------------
namespace pf
{

struct NoteEvent
{
    unsigned char note     = 0;   // 0..127
    unsigned char velocity = 0;   // 0..127; meaningless when `on` is false
    bool          on       = false;
};

// Capacity is a power of two so the wrap is a mask rather than a modulo. 256 is
// far more than a human can generate inside one audio block (~10.7 ms at 48 kHz
// with 512 samples): overflow means the audio thread has stopped draining
// entirely, not that someone played fast.
template <std::size_t Capacity = 256>
class NoteRingT
{
    static_assert(Capacity >= 2, "capacity must leave room for the empty/full distinction");
    static_assert((Capacity & (Capacity - 1)) == 0, "capacity must be a power of two");

public:
    // ── Producer side (message thread) ──────────────────────────────────────
    //
    // Returns false when the queue is full, having stored nothing.
    //
    // ⚠️ OVERFLOW DROPS THE NEWEST EVENT, NOT THE OLDEST -- a deliberate
    // departure from the plan, which asked for drop-oldest. Drop-oldest is not
    // implementable safely in an SPSC ring: freeing the oldest slot means
    // advancing `readIdx`, and `readIdx` is the CONSUMER's cursor. A producer
    // that writes it makes both indices multi-writer and the queue stops being
    // lock-free-correct -- it would be a race, not a policy. Refusing the write
    // touches only the producer's own cursor and is correct by construction.
    //
    // The musical consequence, stated rather than discovered: a dropped note-off
    // under overflow leaves a note sounding. Three things already bound that --
    // the drain runs every block, the voice is monophonic so the next note-on
    // steals it (FaustEngine.cpp, noteOn), and allNotesOff()/silenceVoice()
    // remain reachable. It is recorded in `dropped` so the condition is
    // observable rather than silent, which is the thing this project keeps
    // paying for when it is missing.
    bool push(const NoteEvent& e) noexcept
    {
        // Only this thread writes writeIdx, so a relaxed load of our own cursor
        // is enough. readIdx is written by the consumer, so it needs acquire --
        // it is what tells us a slot has been freed, and it must not be
        // reordered against the slot write below.
        const std::size_t w = writeIdx.load(std::memory_order_relaxed);
        const std::size_t r = readIdx.load(std::memory_order_acquire);

        // One slot is left permanently unused. That is what distinguishes full
        // from empty without a separate count -- a count would be a third shared
        // variable and would have to be atomic in both directions.
        if (((w + 1) & kMask) == (r & kMask))
        {
            dropped.fetch_add(1, std::memory_order_relaxed);
            return false;
        }

        slots[w & kMask] = e;

        // Release: the slot write above must be visible to the consumer before
        // the index that publishes it. This is the pairing half of the
        // consumer's acquire load.
        writeIdx.store(w + 1, std::memory_order_release);
        return true;
    }

    // ── Consumer side (audio thread) ────────────────────────────────────────
    //
    // No allocation, no locks, no logging -- see the invariant list in
    // .claude/rules/tier2-evidence.md. Returns false when empty.
    bool pop(NoteEvent& out) noexcept
    {
        const std::size_t r = readIdx.load(std::memory_order_relaxed);
        const std::size_t w = writeIdx.load(std::memory_order_acquire);

        if ((r & kMask) == (w & kMask))
            return false;

        out = slots[r & kMask];
        readIdx.store(r + 1, std::memory_order_release);
        return true;
    }

    // ── Diagnostics (either thread; relaxed, advisory only) ─────────────────
    std::uint32_t droppedCount() const noexcept
    {
        return dropped.load(std::memory_order_relaxed);
    }

    // Test/reset support ONLY. Not safe to call while either thread is running;
    // the processor calls it from prepareToPlay, where the audio thread is not
    // yet live for this instance.
    void reset() noexcept
    {
        writeIdx.store(0, std::memory_order_relaxed);
        readIdx.store(0, std::memory_order_relaxed);
        dropped.store(0, std::memory_order_relaxed);
    }

    static constexpr std::size_t capacity() noexcept { return Capacity; }

private:
    static constexpr std::size_t kMask = Capacity - 1;

    // Indices are free-running and masked at use. They wrap at SIZE_MAX rather
    // than at Capacity, which keeps `(w + 1) == r` unambiguous: comparing masked
    // values only, as above, is what makes the wrap harmless.
    std::atomic<std::size_t> writeIdx { 0 };
    std::atomic<std::size_t> readIdx  { 0 };
    std::atomic<std::uint32_t> dropped { 0 };

    NoteEvent slots[Capacity] {};
};

using NoteRing = NoteRingT<256>;

} // namespace pf
