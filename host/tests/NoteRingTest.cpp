// ---------------------------------------------------------------------------
// NoteRingTest — the SPSC queue behind host/Source/NoteRing.h.
//
// WHY THIS TEST IS NOT OPTIONAL. This queue is the ONLY thing standing between
// the message thread and the audio thread on the note path. If it is wrong, the
// symptom is not a failing assertion -- it is an intermittent stuck note or a
// torn read under load, discovered on stage. The ordering argument in the header
// is only as good as an exercise of it, and this project's own rule is that a
// control counts once it has been seen failing (CLAUDE.md).
//
// Header-only under test, so this compiles ONE translation unit and links no
// JUCE, no libfaust and no plugin -- the same reasoning as ParamIdentityTest and
// ParamGridLayout.h: index arithmetic over a fixed array must be exercisable
// without a compiled DSP, a message thread or a live editor.
//
// The concurrency section runs a real producer thread against a real consumer
// thread; the build compiles this target under TSan as well as ASan/UBSan (see
// host/CMakeLists.txt), because a single-threaded exercise of a memory-ordering
// argument proves nothing about the ordering.
//
// Run: ./NoteRingTest  -- exits 0 on success, 1 with a failure list.
// ---------------------------------------------------------------------------
#include "../Source/NoteRing.h"

#include <atomic>
#include <iostream>
#include <string>
#include <thread>

namespace
{
int failures = 0;
int checks   = 0;

void expectTrue(bool cond, const std::string& what)
{
    ++checks;
    if (! cond)
    {
        ++failures;
        std::cerr << "FAIL  " << what << "\n";
    }
}

void expectEqInt(long long actual, long long expected, const std::string& what)
{
    ++checks;
    if (actual != expected)
    {
        ++failures;
        std::cerr << "FAIL  " << what << "\n"
                  << "      expected: " << expected << "\n"
                  << "      actual:   " << actual   << "\n";
    }
}

// ── Empty / single round trip ───────────────────────────────────────────────
void testEmptyAndRoundTrip()
{
    pf::NoteRingT<8> ring;
    pf::NoteEvent out;

    expectTrue(! ring.pop(out), "a fresh ring is empty");

    expectTrue(ring.push({ 60, 100, true }), "push into an empty ring succeeds");
    expectTrue(ring.pop(out), "the pushed event pops");
    expectEqInt(out.note, 60, "note survives the round trip");
    expectEqInt(out.velocity, 100, "velocity survives the round trip");
    expectTrue(out.on, "the on flag survives the round trip");

    expectTrue(! ring.pop(out), "the ring is empty again after the single pop");
}

// ── FIFO order, which is the entire point for note-on/note-off pairing ──────
void testFifoOrder()
{
    pf::NoteRingT<8> ring;

    for (int i = 0; i < 5; ++i)
        expectTrue(ring.push({ static_cast<unsigned char>(60 + i), 90, true }),
                   "push " + std::to_string(i) + " within capacity");

    for (int i = 0; i < 5; ++i)
    {
        pf::NoteEvent out;
        expectTrue(ring.pop(out), "pop " + std::to_string(i));
        expectEqInt(out.note, 60 + i, "events pop in the order they were pushed");
    }
}

// ── Overflow: the newest is refused, the queue stays intact ─────────────────
//
// This is the red case for the policy documented in NoteRing.h. Capacity 8
// leaves one slot unused to distinguish full from empty, so 7 pushes fit and the
// 8th must be refused WITHOUT corrupting the 7 already queued.
void testOverflowDropsNewest()
{
    pf::NoteRingT<8> ring;

    for (int i = 0; i < 7; ++i)
        expectTrue(ring.push({ static_cast<unsigned char>(i), 64, true }),
                   "push " + std::to_string(i) + " fits (capacity 8 holds 7)");

    expectEqInt(ring.droppedCount(), 0, "nothing dropped while there was room");

    expectTrue(! ring.push({ 99, 64, true }), "the 8th push is refused");
    expectEqInt(ring.droppedCount(), 1, "the refusal is counted");

    expectTrue(! ring.push({ 98, 64, true }), "a second overflow is also refused");
    expectEqInt(ring.droppedCount(), 2, "and also counted");

    // The queued events must be untouched -- a refused push that corrupted the
    // buffer would be far worse than a dropped note.
    for (int i = 0; i < 7; ++i)
    {
        pf::NoteEvent out;
        expectTrue(ring.pop(out), "queued event " + std::to_string(i) + " survives overflow");
        expectEqInt(out.note, i, "overflow did not overwrite queued event " + std::to_string(i));
    }

    pf::NoteEvent out;
    expectTrue(! ring.pop(out), "the refused events were never stored");
}

// ── Space is reclaimed as the consumer drains ───────────────────────────────
void testWrapAround()
{
    pf::NoteRingT<4> ring;
    pf::NoteEvent out;

    // Push/pop far more than capacity through the same ring, which forces the
    // free-running indices to wrap the mask many times.
    for (int i = 0; i < 100; ++i)
    {
        expectTrue(ring.push({ static_cast<unsigned char>(i % 128), 64, (i % 2) == 0 }),
                   "wrap push " + std::to_string(i));
        expectTrue(ring.pop(out), "wrap pop " + std::to_string(i));
        expectEqInt(out.note, i % 128, "wrap round trip " + std::to_string(i));
    }

    expectEqInt(ring.droppedCount(), 0, "a drained ring never overflows");
}

// ── Two real threads ────────────────────────────────────────────────────────
//
// The ordering argument in NoteRing.h is about release/acquire pairing. Exercise
// it: one producer thread, one consumer thread, and assert that everything the
// consumer sees is intact and in order. Under TSan this is also the race check.
void testConcurrentProducerConsumer()
{
    constexpr int kEvents = 200000;
    pf::NoteRingT<256> ring;

    std::atomic<bool> producerDone { false };
    std::atomic<int>  consumed     { 0 };
    std::atomic<int>  outOfOrder   { 0 };
    std::atomic<int>  corrupt      { 0 };

    std::thread producer([&]
    {
        for (int i = 0; i < kEvents; ++i)
        {
            // Encode the sequence number in the payload so the consumer can
            // check both integrity and order. note carries i % 128 and velocity
            // carries (i / 128) % 128 -- together a 14-bit counter.
            pf::NoteEvent e { static_cast<unsigned char>(i % 128),
                              static_cast<unsigned char>((i / 128) % 128),
                              (i % 2) == 0 };
            // Spin until it fits: this test is about correctness under
            // contention, not about the drop policy (testOverflowDropsNewest
            // covers that).
            while (! ring.push(e))
                std::this_thread::yield();
        }
        producerDone.store(true, std::memory_order_release);
    });

    std::thread consumer([&]
    {
        int expected = 0;
        pf::NoteEvent out;
        while (true)
        {
            if (ring.pop(out))
            {
                const int gotNote = out.note;
                const int gotVel  = out.velocity;
                const bool gotOn  = out.on;

                if (gotNote != expected % 128 || gotVel != (expected / 128) % 128)
                    outOfOrder.fetch_add(1, std::memory_order_relaxed);
                if (gotOn != ((expected % 2) == 0))
                    corrupt.fetch_add(1, std::memory_order_relaxed);

                ++expected;
                consumed.fetch_add(1, std::memory_order_relaxed);
            }
            else if (producerDone.load(std::memory_order_acquire))
            {
                // One last drain: the producer may have published between our
                // failed pop and this load.
                if (! ring.pop(out))
                    break;
                ++expected;
                consumed.fetch_add(1, std::memory_order_relaxed);
            }
            else
            {
                std::this_thread::yield();
            }
        }
    });

    producer.join();
    consumer.join();

    expectEqInt(consumed.load(), kEvents, "every produced event was consumed exactly once");
    expectEqInt(outOfOrder.load(), 0, "no event arrived out of order");
    expectEqInt(corrupt.load(), 0, "no event payload was torn");
}
} // namespace

int main()
{
    std::cout << "NoteRingTest — the message->audio note queue (NoteRing.h)\n";

    testEmptyAndRoundTrip();
    testFifoOrder();
    testOverflowDropsNewest();
    testWrapAround();
    testConcurrentProducerConsumer();

    std::cout << checks - failures << "/" << checks << " checks passed\n";

    if (failures != 0)
    {
        std::cerr << "\n" << failures << " FAILED\n";
        return 1;
    }

    std::cout << "PASS\n";
    return 0;
}
