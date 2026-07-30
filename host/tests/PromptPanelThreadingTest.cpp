// PromptPanelThreadingTest — the PF-006 regression harness.
//
// WHAT BROKE. PromptPanel::submitPrompt() used to launch a `std::thread(...)
// .detach()` that captured a raw PluginForgeProcessor& and could sit up to 120s
// in waitForProcessToFinish. Two consequences, both observed:
//
//   1. Shutdown UAF. A detached thread outlives BOTH the panel and the processor,
//      so proc.loadFaustCode() ran against freed memory if the DAW tore the
//      plugin down inside that window.
//   2. No supersede. N rapid Generate clicks meant N live detached threads racing
//      each other into loadFaustCode(). This produced a "Segmentation fault (core
//      dumped)" during the 2026-07-24 P6 listening battery, prompt #7.
//
// WHAT THIS ASSERTS. Not "the code has a worker" — the *properties* that make the
// crash impossible:
//
//   1. destroying the panel with a subprocess in flight does not crash, and
//      returns PROMPTLY (kills the child rather than waiting out its 180s cap)
//   2. rapid-fire submits do not accumulate threads, and only the LAST one
//      publishes to the processor
//   3. destroying immediately after submit — the tightest race — is safe
//   4. a panel that never generated never spawned a thread, and its destructor
//      does not block
//   5. PF-021: submitting clears the previous run's error text
//
// HOW IT AVOIDS THE NETWORK. PromptPanel resolves its generator through
// PLUGINFORGE_LLM_SCRIPT and its interpreter through PLUGINFORGE_PYTHON. This test
// points them at /bin/sh and a generated shell script, so "generate.py" is a fake
// that sleeps for a controlled time and then prints one ADR-011 JSON line. No
// Python, no API key, no tokens spent, fully deterministic.
//
// NOT COVERED, stated explicitly (COLLABORATION.md §3):
//   * The real generate.py is never invoked. This is a threading test, not an
//     end-to-end one.
//   * DAW-driven teardown ordering is approximated by destroying the panel on the
//     message thread. A host that tears down from another thread is not exercised
//     and cannot be, without a host.
//   * ASan/TSan cannot prove the absence of a race, only its absence in the
//     interleavings this run happened to take. The loop counts below are chosen
//     to make the window wide, not to be exhaustive.
//
// Build/run (ASan/UBSan are wired in host/CMakeLists.txt):
//   cmake --build build --target PromptPanelThreadingTest
//   ./build/PromptPanelThreadingTest_artefacts/Debug/PromptPanelThreadingTest

#include "../Source/PluginProcessor.h"
#include "../Source/PromptPanel.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>

// LeakSanitizer suppression, scoped as narrowly as the mechanism allows.
//
// libfaust leaks its parser buffers on EVERY compile — FAUST_scan_buffer /
// FAUST_scan_bytes reached from SourceReader::parseString, inside
// /usr/lib/libfaust.so.2. It is third-party, it is not reachable from anything
// this repo can free, and it fires on valid and invalid patches alike. Without
// this the harness exits non-zero on a leak that is not ours, which would train
// everyone to ignore the leak report — the opposite of what ASan is for.
//
// Deliberately matched on the LIBRARY, not switched off wholesale: a leak in
// PluginForge's own code still fails this test.
extern "C" const char* __lsan_default_suppressions()
{
    return "leak:libfaust\n";
}

namespace
{
int failures = 0;
int checks   = 0;

void check(bool condition, const char* what)
{
    ++checks;
    std::printf("  [%s] %s\n", condition ? " OK " : "FAIL", what);
    if (! condition)
        ++failures;
}

using Clock = std::chrono::steady_clock;

int elapsedMs(Clock::time_point since)
{
    return (int) std::chrono::duration_cast<std::chrono::milliseconds>(
        Clock::now() - since).count();
}

// Writes a fake generator: sleeps `sleepSeconds`, then emits one ADR-011 JSON
// line naming the prompt it was given, so a test can tell WHICH run published.
juce::File writeFakeGenerator(const juce::File& dir, const char* name, int sleepSeconds)
{
    auto script = dir.getChildFile(name);
    script.replaceWithText(
        juce::String("#!/bin/sh\n")
        + "sleep " + juce::String(sleepSeconds) + "\n"
        // $2 is the prompt: argv is `sh <script> --prompt <text>`, so after the
        // script name the shell sees $1=--prompt $2=<text>.
        // faust_code is a fixed VALID patch, not the prompt text. Which run
        // published is identified through the prompt (loadFaustCode stores it
        // separately and currentPromptForTest reads it back), so the code itself
        // does not need to vary — and making it valid keeps real Faust compile
        // errors out of the output, where they would look like test failures.
        + "printf '{\"success\": true, \"faust_code\": "
          "\"import(\\\\\"stdfaust.lib\\\\\");process = _*0.5;\", "
          "\"attempts\": 1, \"error\": null, \"reason\": \"ok\"}\\n' \"$2\"\n",
        // NOT a default argument worth taking: File::replaceWithText's
        // `lineEndings` parameter defaults to "\r\n" (juce_File.h:781-784), which
        // silently rewrites every \n above into CRLF. A POSIX shell then reads
        // `sleep 1\r` as an invalid interval and the emitted JSON carries a stray
        // CR, so the panel parsed garbage and reported "Generation failed with no
        // error text" — with the fake generator, not the code under test, at
        // fault. Cost an hour; pass "\n" explicitly.
        /* asUnicode */ false, /* writeUnicodeHeaderBytes */ false, "\n");
    return script;
}

// Pumps the JUCE message loop so callAsync lambdas actually run.
void pump(int ms)
{
    juce::MessageManager::getInstance()->runDispatchLoopUntil(ms);
}

struct Fixture
{
    PluginForgeProcessor processor;
    PromptPanel          panel { processor };
};
} // namespace

int main()
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    auto tempDir = juce::File::getSpecialLocation(juce::File::tempDirectory)
                       .getChildFile("pluginforge_pf006_test");
    tempDir.createDirectory();

    // /bin/sh stands in for the Python interpreter; the "script" is a shell fake.
    // setenv rather than putenv: the panel reads these in its constructor.
    ::setenv("PLUGINFORGE_PYTHON", "/bin/sh", 1);

    std::printf("PromptPanelThreadingTest (PF-006)\n\n");

    // ── 0. Baseline: a single uncontended submit must publish ────────────────
    // Not a PF-006 property, but the control. Every assertion below about which
    // run publishes is meaningless if the happy path does not publish at all.
    {
        std::printf("0. Baseline — one submit publishes to the processor\n");
        auto quick = writeFakeGenerator(tempDir, "quick_generate.py", 1);
        ::setenv("PLUGINFORGE_LLM_SCRIPT", quick.getFullPathName().toRawUTF8(), 1);

        Fixture fixture;
        fixture.panel.submitPromptForTest("baseline prompt");
        pump(4000);
        const auto persisted = fixture.processor.currentPromptForTest();
        std::printf("     processor holds prompt: \"%s\"\n", persisted.toRawUTF8());
        std::printf("     panel error text: \"%s\"\n",
                    fixture.panel.errorTextForTest().toRawUTF8());
        check(persisted == "baseline prompt", "a single submit reaches loadFaustCode");
    }


    // ── 1. Teardown with a subprocess in flight ──────────────────────────────
    // THE use-after-free. The fake sleeps 30s; destroying the panel must kill it
    // and return in well under that, without touching freed memory.
    {
        std::printf("1. Destroy panel with a subprocess in flight\n");
        auto slow = writeFakeGenerator(tempDir, "slow_generate.py", 30);
        ::setenv("PLUGINFORGE_LLM_SCRIPT", slow.getFullPathName().toRawUTF8(), 1);

        auto started = Clock::now();
        {
            Fixture fixture;
            fixture.panel.submitPromptForTest("a warm lowpass filter");
            pump(400);           // let the subprocess actually get going
            check(fixture.panel.hasWorkerForTest(), "worker thread was started");
        }                        // ~PromptPanel runs here — must join, not hang
        const int tookMs = elapsedMs(started);

        std::printf("     teardown took %d ms (fake generator sleeps 30000 ms)\n", tookMs);
        check(tookMs < 5000,
              "teardown killed the child instead of waiting it out");
    }

    // ── 2. Rapid-fire submits: supersede, don't accumulate ───────────────────
    // The P6 #7 shape. Ten clicks in a row used to mean ten detached threads.
    {
        std::printf("\n2. Rapid-fire submits supersede rather than accumulate\n");
        auto quick = writeFakeGenerator(tempDir, "quick_generate.py", 1);
        ::setenv("PLUGINFORGE_LLM_SCRIPT", quick.getFullPathName().toRawUTF8(), 1);

        Fixture fixture;
        for (int i = 0; i < 10; ++i)
        {
            fixture.panel.submitPromptForTest("prompt " + juce::String(i));
            pump(30);
        }
        pump(3000);   // let the surviving run finish and publish

        // Exactly the last prompt must reach the processor — not an earlier one
        // (a superseded run overwrote it) and NOT nothing (the surviving run
        // discarded itself). The first version of this assertion allowed an empty
        // result and passed vacuously while the supersede logic was in fact
        // dropping every run; keep it strict.
        const auto persisted = fixture.processor.currentPromptForTest();
        std::printf("     processor holds prompt: \"%s\"\n", persisted.toRawUTF8());
        std::printf("     panel error text: \"%s\"\n",
                    fixture.panel.errorTextForTest().toRawUTF8());
        check(persisted == "prompt 9",
              "exactly the newest submit published (not an older one, and not none)");
    }

    // ── 3. Destroy immediately after submit — the tightest race ──────────────
    {
        std::printf("\n3. Destroy immediately after submit (tight race), 25x\n");
        auto slow = writeFakeGenerator(tempDir, "slow_generate.py", 30);
        ::setenv("PLUGINFORGE_LLM_SCRIPT", slow.getFullPathName().toRawUTF8(), 1);

        auto started = Clock::now();
        for (int i = 0; i < 25; ++i)
        {
            Fixture fixture;
            fixture.panel.submitPromptForTest("race " + juce::String(i));
            // No pump at all: destroy while the worker may not even have picked
            // the job up yet. Exercises the stale() check before start().
        }
        std::printf("     25 create/submit/destroy cycles in %d ms\n", elapsedMs(started));
        check(true, "25 tight create/submit/destroy cycles completed without crashing");
    }

    // ── 4. A panel that never generates spawns no thread ─────────────────────
    {
        std::printf("\n4. No generation => no thread, and no blocking destructor\n");
        auto started = Clock::now();
        {
            Fixture fixture;
            check(! fixture.panel.hasWorkerForTest(), "no worker before first submit");
        }
        check(elapsedMs(started) < 1000, "destructor of an idle panel returns immediately");
    }

    // ── 5. PF-021: submitting clears the previous run's error ────────────────
    {
        std::printf("\n5. PF-021 — a new submit clears the stale error\n");
        auto quick = writeFakeGenerator(tempDir, "quick_generate.py", 1);
        ::setenv("PLUGINFORGE_LLM_SCRIPT", quick.getFullPathName().toRawUTF8(), 1);

        Fixture fixture;
        fixture.panel.setError("a previous failure the user has already read");
        check(fixture.panel.errorTextForTest().isNotEmpty(), "error text is set");

        fixture.panel.submitPromptForTest("a fresh attempt");
        check(fixture.panel.errorTextForTest().isEmpty(),
              "submit cleared the stale error before the new run started");
        pump(2500);
    }

    tempDir.deleteRecursively();

    std::printf("\n%s (%d failure%s)\n",
                failures == 0 ? "PASS" : "FAIL", failures, failures == 1 ? "" : "s");
    // One machine-readable line for tools/health_report.py. The `checks` total is
    // the point: every harness here already carried an exact `failures` int and
    // threw the denominator away, so "0 failures" was indistinguishable from
    // "ran nothing". Format is fixed and shared by all seven harnesses.
    std::printf("PF_SUMMARY harness=%s checks=%d failures=%d\n", "PromptPanelThreadingTest", checks, failures);
    return failures == 0 ? 0 : 1;
}
