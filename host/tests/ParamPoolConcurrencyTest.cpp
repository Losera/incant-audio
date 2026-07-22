// ThreadSanitizer stress harness for ParamPool's double-buffer scheme
// (see the SUBTLE comments in Source/ParamPool.h/.cpp).
//
// This is a PAIR draft (per COLLABORATION.md -- ParamPool parameter handling is
// explicitly called out there as needing human verification) written to close a
// real gap: the activeLabels data race was fixed 2026-07-17 with zero
// execution-level verification since. This drives the REAL production object
// graph and REAL threading model rather than a synthetic mock:
//
//   - PluginForgeProcessor::loadFaustCode() -> FaustEngine::compile(), which
//     runs on its own detached std::thread and calls back into
//     ParamPool::remap() directly on THAT thread (confirmed by reading
//     FaustEngine.cpp -- the ".h" comment saying the callback runs "on the
//     message thread" is not what the implementation actually does; cb() is
//     called straight from the detached compile thread, right after the
//     atomic DSP-pointer swap). That's the real write side.
//   - A busy-loop thread here calls PluginForgeProcessor::processBlock()
//     repeatedly, which calls ParamPool::pushToFaust() every time -- the real
//     audio-thread read side.
//
// Build:  cmake --build build --target ParamPoolTsanTest
// Run:    TSAN_OPTIONS="halt_on_error=1" ./build/ParamPoolTsanTest_artefacts/.../ParamPoolTsanTest
// Pass:   prints "PASS" and exits 0.
// Fail:   ThreadSanitizer prints a "DATA RACE" report to stderr and the
//         process exits non-zero (halt_on_error=1 makes this deterministic).
//
// KNOWN LIMITATION (flagged, not solved here -- see the review note this harness
// ships with): FaustEngine::compile()'s background thread is detached, and
// nothing in production code exposes a way to wait for an in-flight compile to
// finish. This harness works around that with a fixed sleep after firing all
// compiles, sized generously for these deliberately tiny Faust programs. On a
// very slow machine a compile could still be mid-flight when main() returns and
// `processor` is destroyed, which would be a harness-induced race, not a
// production one -- please read this note before trusting a report from a run
// that finishes suspiciously fast.
//
// SEPARATE FINDING, NOT WHAT THIS HARNESS TARGETS (reported to the human rather
// than fixed here -- FaustEngine.cpp's audio-thread/atomic-swap code is
// HUMAN-OWNED per COLLABORATION.md SS1): running this harness prints a large
// number of "ERROR : setParamValue 'x' not found" lines to stderr from Faust's
// own MapUI (faust/gui/MapUI.h). That is NOT this harness failing, and it is NOT
// a ParamPool bug -- ParamPool's own double-buffer (the thing this file is
// actually testing) never falls over, and ThreadSanitizer reports zero races on
// it. Isolating the symptom down to raw libfaust + MapUI with no JUCE/ParamPool
// involved at all (see the scratch probes referenced in the 2026-07-18
// collaboration_log.md entry) narrowed it to FaustEngine::activeUI: the
// `activeUI = std::move(newUI)` swap (FaustEngine.cpp, the "Step 3" comment) is
// guarded only by the `ready` flag, and pushToFaust()/setParamValue() only check
// `isReady()` once before iterating all 64 slots -- there is a real TOCTOU
// window between that check and the swap where a concurrent read can land on a
// `MapUI` mid-move-assignment. This harness's aggressive back-to-back compiles
// make that window common instead of rare; it doesn't reproduce under a single
// isolated compile. Left as a reported finding, not a fix, pending human review.

#include "../Source/PluginProcessor.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <thread>

namespace
{
    constexpr int kCompileIterations = 20;
    constexpr int kAudioBlockSize    = 512;
    constexpr double kSampleRate     = 44100.0;

    // Two different tiny Faust programs, alternated, so remap() actually
    // changes the published param list/labels each time rather than
    // re-publishing an identical one -- a no-op remap wouldn't exercise the
    // double-buffer's write path meaningfully.
    const juce::String kProgramA =
        "import(\"stdfaust.lib\");"
        "g = hslider(\"gain\",0,-60,12,0.1) : ba.db2linear;"
        "process = _*g, _*g;";

    const juce::String kProgramB =
        "import(\"stdfaust.lib\");"
        "f = hslider(\"cutoff\",1000,20,20000,1);"
        "process = fi.lowpass(2,f), fi.lowpass(2,f);";
}

int main()
{
    // Verified live 2026-07-18 (first run of this harness): constructing/using a
    // juce::AudioProcessor from a plain console main() with no
    // juce::MessageManager/dispatch loop works — the run completes. Expected
    // side effects observed then and every run since: Timer/AsyncUpdater
    // assertion messages at startup and LeakedObjectDetector reports at
    // shutdown (no dispatch loop ever services or tears these down). Both are
    // harness noise, not product findings.
    PluginForgeProcessor processor;
    processor.prepareToPlay(kSampleRate, kAudioBlockSize);

    std::atomic<bool> stopAudioThread { false };

    // Audio-thread stand-in: hammer processBlock() (-> ParamPool::pushToFaust())
    // as fast as possible for the whole run.
    std::thread audioThread([&]
    {
        juce::AudioBuffer<float> buffer(2, kAudioBlockSize);
        juce::MidiBuffer midi;

        while (!stopAudioThread.load(std::memory_order_relaxed))
        {
            buffer.clear();
            processor.processBlock(buffer, midi);
        }
    });

    // Compile-thread stand-in: drive remap() repeatedly while the audio thread
    // hammers pushToFaust(), which is the concurrency the double-buffer exists
    // to survive.
    //
    // PACING IS LOAD-BEARING, and it was not always. Until 2026-07-22 every
    // loadFaustCode() spawned its own detached thread, so firing 20 back-to-back
    // produced 20 overlapping compiles. FaustEngine now runs ONE worker with a
    // single pending slot (newest wins), so an unpaced burst would be collapsed
    // into roughly two compiles -- the first, and whichever was queued last --
    // and this test would quietly stop testing anything. The gap is chosen to
    // exceed a single libfaust compile of these tiny programs so each iteration
    // is actually picked up.
    for (int i = 0; i < kCompileIterations; ++i)
    {
        processor.loadFaustCode(i % 2 == 0 ? kProgramA : kProgramB);
        std::this_thread::sleep_for(std::chrono::milliseconds(120));
    }

    // Settle time for the final compile. No longer load-bearing for correctness:
    // ~PluginForgeProcessor calls FaustEngine::shutdown(), which joins the
    // worker, so no compile can outlive main() the way the detached threads
    // could -- that outliving is precisely what ThreadSanitizer caught in CI run
    // 29883556305 (race against JUCE's static-destructor teardown at exit).
    std::this_thread::sleep_for(std::chrono::seconds(2));

    stopAudioThread.store(true, std::memory_order_relaxed);
    audioThread.join();

    std::puts("PASS: ran to completion with no ThreadSanitizer report above this line.");
    return 0;
}
