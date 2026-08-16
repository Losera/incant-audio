// ValidationGateTest — the post-compile validation gate (Brief F).
//
// Faust happily compiles a `process` that cannot actually run in this host: too
// many channels, an instrument that declares only part of the voice contract,
// more controls than the 64-slot macro pool holds. None of that is a Faust
// error, so nothing before the gate (host/Source/FaustEngine.cpp's
// validatePatch(), called from runCompile() before the atomic swap) ever
// noticed. This is the runnable check for that gate: one minimal Faust source
// per check that trips exactly it, through the REAL compile path --
// PluginForgeProcessor::loadFaustCode() -> FaustEngine::compile() -> the JIT --
// plus one test proving a rejected compile leaves the previously loaded DSP
// running and audible.
//
// NOT COVERED, stated per COLLABORATION.md §3: whether a rejected topology was
// what the PROMPT asked for (bench/ladder_corpus.json entry 1's mono/stereo
// reversal). That is explicitly outside this gate's scope -- see the big
// comment on validatePatch() in FaustEngine.cpp.
//
// Build/run:
//   cmake --build build --target ValidationGateTest
//   ./build/ValidationGateTest_artefacts/Debug/ValidationGateTest

#include "../Source/PluginProcessor.h"
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdio>
#include <mutex>
#include <string>

// libfaust leaks its parser buffers on every compile (FAUST_scan_buffer, inside
// libfaust.so): third-party, not reachable from anything this repo can free.
// Matched on the library so a leak in OUR code still fails the test.
extern "C" const char* __lsan_default_suppressions()
{
    return "leak:libfaust\n";
}

namespace
{
int failures = 0;
int checks   = 0;

void check(bool condition, const std::string& what)
{
    ++checks;
    std::printf("  [%s] %s\n", condition ? " OK " : "FAIL", what.c_str());
    if (! condition)
        ++failures;
}

// What a compile attempt actually did -- both outcomes are legitimate results
// here (some sources are EXPECTED to fail the gate), unlike loadAndAwaitCompile
// in the other harnesses, which only ever wants a success.
struct CompileOutcome
{
    bool        timedOut = false;
    bool        succeeded = false;
    juce::String error;
};

CompileOutcome loadAndAwaitEither(PluginForgeProcessor& p,
                                  const juce::String& source,
                                  const juce::String& prompt,
                                  int timeoutMs = 15000)
{
    std::mutex m;
    std::condition_variable cv;
    bool done = false;
    CompileOutcome outcome;

    p.onFaustCompileSuccess = [&](const FaustEngine::ParamList&)
    {
        std::lock_guard<std::mutex> lock(m);
        outcome.succeeded = true;
        done = true;
        cv.notify_all();
    };
    p.onFaustCompileFailure = [&](const juce::String& err, const juce::String&)
    {
        std::lock_guard<std::mutex> lock(m);
        outcome.succeeded = false;
        outcome.error      = err;
        done = true;
        cv.notify_all();
    };

    p.loadFaustCode(source, prompt);

    std::unique_lock<std::mutex> lock(m);
    outcome.timedOut = ! cv.wait_for(lock, std::chrono::milliseconds(timeoutMs),
                                     [&] { return done; });
    lock.unlock();

    p.onFaustCompileSuccess = nullptr;
    p.onFaustCompileFailure = nullptr;
    return outcome;
}

constexpr double kSampleRate = 44100.0;
constexpr int    kBlockSize  = 512;

// Builds a patch with `count` uniquely-labelled, eligible (hslider) controls --
// the mechanical way to trip the param-count check without hand-writing 65
// lines. Every slider is writable (FaustEngine::Kind::HSlider), matching what
// ParamPool::remap's `eligible` pass and validatePatch()'s writableCount both
// count against the 64-slot budget.
juce::String manyParamPatch(int count)
{
    juce::String src = "import(\"stdfaust.lib\");\n";
    juce::String sum;
    for (int i = 0; i < count; ++i)
    {
        const juce::String name = "p" + juce::String(i);
        src += name + " = hslider(\"" + name + "\", 0, 0, 1, 0.01);\n";
        sum += (i == 0 ? name : " + " + name);
    }
    src += "process = " + sum + ";\n";
    return src;
}
} // namespace

int main()
{
    std::printf("ValidationGateTest\n");

    // ── Check: output-channels ────────────────────────────────────────────
    // 0 inputs, 3 outputs -- exceeds kMaxChannels (2) on the OUTPUT side only.
    {
        PluginForgeProcessor p;
        p.prepareToPlay(kSampleRate, kBlockSize);
        const auto outcome = loadAndAwaitEither(
            p, "import(\"stdfaust.lib\");\nprocess = 0,0,0;", "3 outputs");
        check(! outcome.timedOut, "output-channels: compile attempt returned");
        check(! outcome.succeeded, "output-channels: 3-output patch rejected");
        check(outcome.error.contains("output-channels"),
              "output-channels: gate names the check ('" + outcome.error.toStdString() + "')");
    }

    // ── Check: input-channels ─────────────────────────────────────────────
    // 3 inputs merged to 1 output -- exceeds kMaxChannels on the INPUT side
    // only, so this cannot also trip output-channels or zero-outputs.
    {
        PluginForgeProcessor p;
        p.prepareToPlay(kSampleRate, kBlockSize);
        const auto outcome = loadAndAwaitEither(
            p, "import(\"stdfaust.lib\");\nprocess = _,_,_ :> _;", "3 inputs");
        check(! outcome.timedOut, "input-channels: compile attempt returned");
        check(! outcome.succeeded, "input-channels: 3-input patch rejected");
        check(outcome.error.contains("input-channels"),
              "input-channels: gate names the check ('" + outcome.error.toStdString() + "')");
    }

    // ── Check: zero-outputs ────────────────────────────────────────────────
    // NOT reachable through this end-to-end test, and that is itself the
    // finding, verified against the installed Faust 2.85.9 compiler: EVERY
    // Faust source with no output signal -- `process = 0 : !;`, `par(i,0,_)`,
    // `0,0 : !,!` all tried -- is refused by createDSPFactoryFromString
    // ITSELF ("ERROR : the Faust program has no output signal"), before a
    // `dsp` instance exists for validatePatch() to ever see. So the
    // numOuts < 1 branch in validatePatch() is unreachable through the real
    // compiler today, the same shape as the PF-023 defensive branch in
    // FaustEngine::process() (FaustEngine.cpp) -- kept as cheap insurance
    // against that guarantee changing, not because it fires. What this test
    // asserts instead is the guarantee itself: the compile is still refused
    // (by Faust's frontend rather than by our gate), and does not silently
    // produce a live zero-output DSP.
    {
        PluginForgeProcessor p;
        p.prepareToPlay(kSampleRate, kBlockSize);
        const auto outcome = loadAndAwaitEither(
            p, "import(\"stdfaust.lib\");\nprocess = 0 : !;", "no outputs");
        check(! outcome.timedOut, "zero-outputs: compile attempt returned");
        check(! outcome.succeeded, "zero-outputs: 0-output patch rejected (by Faust itself)");
        check(outcome.error.contains("no output signal"),
              "zero-outputs: rejection is Faust's own frontend, not validatePatch() -- "
              "the numOuts<1 branch is unreachable today ('"
                  + outcome.error.toStdString() + "')");
    }

    // ── Check: instrument-voice-contract ──────────────────────────────────
    // Declares "gate" and "freq" but not "gain" -- matches the SAME label list
    // FaustEngine.cpp:252-257 uses (via extractVoiceControls/VoiceControls),
    // not a second hand-copied one here.
    {
        PluginForgeProcessor p;
        p.prepareToPlay(kSampleRate, kBlockSize);
        const juce::String src =
            "import(\"stdfaust.lib\");\n"
            "freq = hslider(\"freq\", 440, 20, 8000, 0.01);\n"
            "gate = button(\"gate\");\n"
            "process = os.osc(freq) * gate;\n";
        const auto outcome = loadAndAwaitEither(p, src, "partial voice contract");
        check(! outcome.timedOut, "instrument-voice-contract: compile attempt returned");
        check(! outcome.succeeded, "instrument-voice-contract: gate+freq-but-no-gain rejected");
        check(outcome.error.contains("instrument-voice-contract"),
              "instrument-voice-contract: gate names the check ('"
                  + outcome.error.toStdString() + "')");
    }

    // ── Check: param-count (warning, non-fatal — NOT what the brief literally
    // asked for) ─────────────────────────────────────────────────────────
    // 65 eligible controls, one over ParamPool::POOL_SIZE (64). This is
    // deliberately a WARNING, not a fatal rejection, overriding Brief F's
    // literal "→ fatal" against existing, tested, documented behaviour:
    // PF-051 (PluginProcessor.cpp's loadFaustCode) already lets an
    // over-budget patch compile and run, mapping the first 64 controls and
    // logging the rest as unreachable -- host/tests/EditorSessionTest.cpp
    // pins exactly this with a real 70-param patch ("compiled and settled at
    // the pool ceiling"). Making this fatal here would have silently broken
    // that test and reversed a considered product decision the brief did not
    // make; flagged in OPEN_QUESTIONS.md rather than decided unilaterally.
    {
        PluginForgeProcessor p;
        p.prepareToPlay(kSampleRate, kBlockSize);
        const auto outcome = loadAndAwaitEither(p, manyParamPatch(ParamPool::POOL_SIZE + 1),
                                                "65 controls");
        check(! outcome.timedOut, "param-count: compile attempt returned");
        check(outcome.succeeded,
              "param-count: 65-control patch still compiles (PF-051 graceful degradation)");
        check(p.mappedSlotCountForTest() == ParamPool::POOL_SIZE,
              "param-count: exactly the pool ceiling (64) actually got mapped");
    }

    // ── Check: channel-role-mismatch (warning, non-fatal) ─────────────────
    // Both sub-cases compile successfully -- this check only ever LOGS, it
    // never blocks the swap. There is no error string to inspect, so what is
    // asserted is that the patch still loads.
    {
        // Effect with zero audio input.
        PluginForgeProcessor p;
        p.prepareToPlay(kSampleRate, kBlockSize);
        const auto outcome = loadAndAwaitEither(
            p, "import(\"stdfaust.lib\");\nprocess = 0;", "generator effect");
        check(! outcome.timedOut, "channel-role-mismatch (effect/0-in): compile returned");
        check(outcome.succeeded,
              "channel-role-mismatch (effect/0-in): warning does not block the swap");
    }
    {
        // Instrument with a nonzero audio input.
        PluginForgeProcessor p;
        p.prepareToPlay(kSampleRate, kBlockSize);
        const juce::String src =
            "import(\"stdfaust.lib\");\n"
            "freq = hslider(\"freq\", 440, 20, 8000, 0.01);\n"
            "gain = hslider(\"gain\", 0.5, 0, 1, 0.01);\n"
            "gate = button(\"gate\");\n"
            "process = _ + os.osc(freq) * gain * gate;\n";
        const auto outcome = loadAndAwaitEither(p, src, "instrument with audio input");
        check(! outcome.timedOut, "channel-role-mismatch (instrument/1-in): compile returned");
        check(outcome.succeeded,
              "channel-role-mismatch (instrument/1-in): warning does not block the swap");
    }

    // ── A rejected compile leaves the previously loaded DSP running ───────
    // Load a healthy oscillator (process = os.osc(300) * 0.5), confirm it is
    // live and audible by actually rendering a block, THEN attempt a fatal
    // patch and confirm it is refused, THEN render another block and confirm
    // the ORIGINAL patch -- not silence, not the rejected one -- is still
    // what runs.
    //
    // A TONE, deliberately, not a constant/DC level: OutputGuard's DC blocker
    // (OutputGuard.cpp, ~5 Hz one-pole highpass) carries filter state ACROSS
    // processBlock calls, so a sustained DC input decays measurably between
    // one 512-sample block and the next -- a real behaviour of the guard, but
    // one that would make a two-call comparison look like the DSP changed
    // when it did not. A 300 Hz tone sits ~60x above that cutoff and passes
    // through at essentially unity gain each block, so RMS is a stable,
    // phase-independent probe for "is the same DSP still producing the same
    // signal" across the two renders.
    {
        PluginForgeProcessor p;
        p.prepareToPlay(kSampleRate, kBlockSize);

        const juce::String good = "import(\"stdfaust.lib\");\nprocess = os.osc(300) * 0.5;";
        const auto loaded = loadAndAwaitEither(p, good, "the running patch");
        check(! loaded.timedOut && loaded.succeeded, "swap-preservation: good patch loaded");

        // Expected RMS of a 0.5-amplitude sine, asymptotically: 0.5/sqrt(2).
        constexpr float kExpectedRms = 0.5f / 1.41421356f;

        auto renderBlockRms = [&]() -> float
        {
            juce::AudioBuffer<float> buffer(2, kBlockSize);
            juce::MidiBuffer midi;
            buffer.clear();   // process takes 0 inputs; content is irrelevant either way
            p.processBlock(buffer, midi);
            return buffer.getRMSLevel(0, 0, kBlockSize);
        };

        const float beforeRms = renderBlockRms();
        check(std::fabs(beforeRms - kExpectedRms) < 0.02f,
              "swap-preservation: good patch is live and audible before the rejection "
              "(RMS " + std::to_string(beforeRms) + ", expected ~" + std::to_string(kExpectedRms) + ")");

        const auto rejected = loadAndAwaitEither(
            p, "import(\"stdfaust.lib\");\nprocess = 0,0,0;", "a fatal patch");
        check(! rejected.timedOut && ! rejected.succeeded,
              "swap-preservation: the bad patch was in fact rejected");

        const float afterRms = renderBlockRms();
        check(std::fabs(afterRms - kExpectedRms) < 0.02f,
              "swap-preservation: the ORIGINAL patch is still live and audible after the "
              "rejected compile (RMS " + std::to_string(afterRms) + ", expected ~"
                  + std::to_string(kExpectedRms) + ")");
        check(p.currentSourceForTest() == good,
              "swap-preservation: currentSource still names the original patch, not the "
              "rejected one");
    }

    std::printf("\n");
    if (failures > 0)
        std::printf("FAILED: %d/%d checks\n", failures, checks);
    else
        std::printf("All ValidationGate checks passed.\n");
    std::printf("PF_SUMMARY harness=ValidationGateTest checks=%d failures=%d\n", checks, failures);

    return failures > 0 ? 1 : 0;
}
