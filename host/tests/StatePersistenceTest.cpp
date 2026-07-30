// StatePersistenceTest — round-trips the P11 persisted-state blob through a real
// PluginForgeProcessor: set param values + source + prompt on processor A, serialise
// with getStateInformation(), deserialise into a fresh processor B with
// setStateInformation(), and assert the macro values, Faust source, and prompt all
// survived. This is the runnable check for the COLLABORATION.md §2 trigger-3
// persisted-state contract (see the big comment above getStateInformation() in
// PluginProcessor.cpp).
//
// Scope: this asserts the *serialisation contract*, not the restore recompile.
// Label CONTENT is not asserted (it is produced by the async compile and is timing
// dependent); the blob is asserted to carry no label node at all. A trivial passthrough is
// used as the source so the test does not depend on any particular stdlib function.
//
// Build/run:
//   cmake --build build --target StatePersistenceTest
//   ./build/StatePersistenceTest_artefacts/Debug/StatePersistenceTest

#include "../Source/PluginProcessor.h"
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <mutex>

// libfaust leaks its parser buffers on every compile (FAUST_scan_buffer, inside
// libfaust.so). Third-party and not reachable from anything this repo can free.
// Matched on the library so a leak in OUR code still fails the test.
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

void checkNear(float actual, float expected, const char* what)
{
    ++checks;
    const bool ok = std::fabs(actual - expected) <= 1.0e-4f;
    std::printf("  [%s] %s (got %.5f, expected %.5f)\n",
                ok ? " OK " : "FAIL", what, actual, expected);
    if (! ok)
        ++failures;
}

// Slots are declared with a 0..1 range (PluginProcessor::createParameterLayout), so
// the raw parameter value written via getParameterAsValue equals the normalised value
// read back via getParameter()->getValue() — no range conversion to reason about here.
void setSlot(PluginForgeProcessor& p, int slot, float value)
{
    p.apvts.getParameterAsValue(ParamPool::slotId(slot)).setValue(value);
}

float getSlot(PluginForgeProcessor& p, int slot)
{
    return p.apvts.getParameter(ParamPool::slotId(slot))->getValue();
}

// Loads a patch and BLOCKS until the JIT compile has succeeded.
//
// PF-022 made this necessary and the necessity is the point: loadFaustCode no
// longer commits currentFaustSource/currentPrompt synchronously. It used to write
// them before the compile was even queued, so a FAILED generate overwrote the
// source-of-record with non-compiling code and a DAW save in that window
// persisted a broken patch. The source of record is now whatever last COMPILED —
// so a test that wants to serialise it has to wait for that, exactly as a real
// save-after-generate does.
//
// Returns false on timeout rather than hanging the suite.
bool loadAndAwaitCompile(PluginForgeProcessor& p,
                         const juce::String& source,
                         const juce::String& prompt,
                         PluginForgeProcessor::LoadMode mode
                             = PluginForgeProcessor::LoadMode::Fresh,
                         int timeoutMs = 15000)
{
    std::mutex m;
    std::condition_variable cv;
    bool done = false;

    p.onFaustCompileSuccess = [&](const FaustEngine::ParamList&)
    {
        std::lock_guard<std::mutex> lock(m);
        done = true;
        cv.notify_all();
    };

    p.loadFaustCode(source, prompt, mode);

    std::unique_lock<std::mutex> lock(m);
    const bool ok = cv.wait_for(lock, std::chrono::milliseconds(timeoutMs),
                                [&] { return done; });
    lock.unlock();
    p.onFaustCompileSuccess = nullptr;
    return ok;
}
} // namespace

int main()
{
    std::printf("StatePersistenceTest\n");

    const juce::String kSource = "import(\"stdfaust.lib\");\nprocess = _;";
    const juce::String kPrompt = "a simple passthrough";

    juce::MemoryBlock blob;

    // ── Processor A: set state, serialise ────────────────────────────────────
    {
        PluginForgeProcessor a;
        setSlot(a, 0, 0.25f);
        setSlot(a, 3, 0.42f);
        setSlot(a, 63, 1.0f);

        // Await the compile: PF-022 commits the source of record on SUCCESS only.
        // Iterate so the slot values set above survive — Fresh would (correctly)
        // reset them to the patch's defaults, which is what the PF-020 block below
        // asserts separately.
        check(loadAndAwaitCompile(a, kSource, kPrompt,
                                  PluginForgeProcessor::LoadMode::Iterate),
              "compile completed before serialising");

        a.getStateInformation(blob);
        check(blob.getSize() > 0, "getStateInformation produced a non-empty blob");
    }

    // ── The blob is well-formed and carries the documented shape ─────────────
    {
        auto xml = juce::AudioProcessor::getXmlFromBinary(blob.getData(),
                                                          static_cast<int>(blob.getSize()));
        check(xml != nullptr, "blob parses back to XML");
        if (xml != nullptr)
        {
            auto root = juce::ValueTree::fromXml(*xml);
            check(root.getType().toString() == "PluginForgeState", "root tag is PluginForgeState");
            check((int) root.getProperty("schemaVersion", 0)
                      == PluginForgeProcessor::kStateSchemaVersion,
                  "schemaVersion matches current");
            check(root.getChildWithName("STATE").isValid(), "STATE child present");
            check(! root.getChildWithName("SlotLabels").isValid(),
                  "no SlotLabels node — dropped from v1 2026-07-27");
            check(root.getNumChildren() == 1,
                  "STATE is the blob's only child");

            // The §2 trigger-3 contract is a FORMAT, so it has to be reviewable as
            // one. Set PLUGINFORGE_DUMP_STATE=<path> to write the literal emitted
            // document for a human to read against the doc comment above
            // getStateInformation(). Opt-in: the normal test run writes nothing.
            if (const char* dumpPath = std::getenv("PLUGINFORGE_DUMP_STATE"))
            {
                juce::File(juce::String(dumpPath)).replaceWithText(xml->toString());
                std::printf("  [dump] persisted-state XML written to %s\n", dumpPath);
            }
        }
    }

    // ── Processor B: restore, assert round-trip ──────────────────────────────
    {
        PluginForgeProcessor b;
        b.setStateInformation(blob.getData(), static_cast<int>(blob.getSize()));

        checkNear(getSlot(b, 0),  0.25f, "macro_0 restored");
        checkNear(getSlot(b, 3),  0.42f, "macro_3 restored");
        checkNear(getSlot(b, 63), 1.0f,  "macro_63 restored");

        check(b.currentSourceForTest() == kSource, "Faust source restored");
        check(b.currentPromptForTest() == kPrompt, "prompt restored");
    }

    // ── Corrupt / foreign blobs leave state untouched ────────────────────────
    {
        PluginForgeProcessor c;
        setSlot(c, 5, 0.7f);
        const char garbage[] = "not a valid blob at all";
        c.setStateInformation(garbage, (int) sizeof(garbage));
        checkNear(getSlot(c, 5), 0.7f, "garbage blob does not disturb existing params");
        check(c.currentSourceForTest().isEmpty(), "garbage blob does not set a source");
    }

    // ── PF-022: a FAILED compile must not poison the source of record ────────
    // This is the defect in one assertion. Load a good patch, then a broken one.
    // The retained source must still be the good one, because that is the only
    // version a restore could actually bring back — persisting the broken source
    // alongside the previous patch's labels and values was the data-loss bug.
    {
        std::printf("\nPF-022 — failed compile must not overwrite the source of record\n");
        PluginForgeProcessor p;
        check(loadAndAwaitCompile(p, kSource, kPrompt), "good patch compiled");
        check(p.currentSourceForTest() == kSource, "good source committed");

        // Deliberately uncompilable. No await: this compile never succeeds, so
        // there is no success callback to wait for — poll until the engine has
        // certainly processed it.
        const juce::String broken = "process = ;;; not faust at all";
        p.loadFaustCode(broken, "a prompt that failed");
        for (int i = 0; i < 100 && p.currentSourceForTest() != broken; ++i)
            juce::Thread::sleep(50);

        check(p.currentSourceForTest() == kSource,
              "source of record is STILL the last patch that compiled");
        check(p.currentPromptForTest() == kPrompt,
              "prompt of record is STILL the last patch that compiled");

        // And the blob written in that state must restore to something that works.
        juce::MemoryBlock afterFailure;
        p.getStateInformation(afterFailure);
        PluginForgeProcessor restored;
        restored.setStateInformation(afterFailure.getData(),
                                     static_cast<int>(afterFailure.getSize()));
        check(restored.currentSourceForTest() == kSource,
              "a save taken after a failed generate still restores the working patch");
    }

    // ── PF-020: Fresh resets mapped slots; Iterate keeps them ────────────────
    // The contamination bug: with no fresh/iterate concept, patch B's zones were
    // driven by patch A's values BY SLOT INDEX, so a knob labelled "Cutoff" in A
    // silently drove "Feedback" in B. Crucially this must hold with NO EDITOR
    // OPEN — the old reset lived in ParamGridPanel, which made "fresh" an
    // accident of whether the window happened to be showing.
    {
        std::printf("\nPF-020 — LoadMode::Fresh resets, LoadMode::Iterate preserves\n");

        // A patch with a declared default well away from both 0 and 1, so a reset
        // is distinguishable from "left alone" and from "zeroed".
        const juce::String withDefault =
            "import(\"stdfaust.lib\");\n"
            "process = _ * hslider(\"Gain\", 0.75, 0.0, 1.0, 0.01);";

        {
            PluginForgeProcessor p;
            setSlot(p, 0, 0.1f);   // stand-in for the previous patch's value
            check(loadAndAwaitCompile(p, withDefault, "fresh",
                                      PluginForgeProcessor::LoadMode::Fresh),
                  "Fresh patch compiled");
            checkNear(getSlot(p, 0), 0.75f,
                      "Fresh reset slot 0 to the patch's declared default");
        }

        {
            PluginForgeProcessor p;
            setSlot(p, 0, 0.1f);
            check(loadAndAwaitCompile(p, withDefault, "iterate",
                                      PluginForgeProcessor::LoadMode::Iterate),
                  "Iterate patch compiled");
            checkNear(getSlot(p, 0), 0.1f,
                      "Iterate preserved the existing slot value");
        }

        // Slots the new patch does NOT map must be zeroed, so a value left by a
        // longer previous patch cannot reappear if a later patch maps that index.
        {
            PluginForgeProcessor p;
            setSlot(p, 7, 0.9f);
            check(loadAndAwaitCompile(p, withDefault, "fresh",
                                      PluginForgeProcessor::LoadMode::Fresh),
                  "Fresh patch compiled (unmapped-slot case)");
            checkNear(getSlot(p, 7), 0.0f,
                      "Fresh zeroed a slot the new patch does not map");
        }
    }

    // ── The slot->label map lives in memory, NOT in the blob ─────────────────
    // <SlotLabels> was dropped from v1 on 2026-07-27: it was written on every save
    // and read by nothing. The mapping itself is not gone — it is still built by
    // the compile callback — so this asserts both halves of that split, with a
    // param-bearing patch rather than the `process = _;` used above (which
    // declares no params and so could not tell an empty map from a missing one).
    //
    // Label ORDER is libfaust's UI-traversal order, not a contract this project
    // states, so the label SET is pinned rather than a specific index→label pair.
    {
        std::printf("\nslot->label map: in memory, absent from the blob\n");

        const juce::String twoParams =
            "import(\"stdfaust.lib\");\n"
            "process = _ * hslider(\"Gain\", 0.5, 0.0, 1.0, 0.01)\n"
            "            * hslider(\"Depth\", 0.25, 0.0, 1.0, 0.01);";

        PluginForgeProcessor p;
        check(loadAndAwaitCompile(p, twoParams, "two named params",
                                  PluginForgeProcessor::LoadMode::Fresh),
              "param-bearing patch compiled");

        auto labels = p.currentLabelsForTest();
        check(labels.size() == 2, "the compile published one label per declared param");
        labels.sort(false);
        check(labels.joinIntoString(",") == "Depth,Gain",
              "the declared param labels reach the in-memory map");

        // ...and none of that leaks into the persisted format. This is the check
        // that fails if anyone re-adds the node.
        juce::MemoryBlock labelled;
        p.getStateInformation(labelled);
        auto xml = juce::AudioProcessor::getXmlFromBinary(labelled.getData(),
                                                          static_cast<int>(labelled.getSize()));
        check(xml != nullptr, "labelled blob parses back to XML");
        if (xml != nullptr)
        {
            auto root = juce::ValueTree::fromXml(*xml);
            check(! root.getChildWithName("SlotLabels").isValid(),
                  "a patch WITH params still serialises no SlotLabels node");
            check(root.getNumChildren() == 1, "STATE remains the blob's only child");

            // Deliberately NOT "the blob does not contain 'Gain'" — it does, and
            // must: faustSource carries hslider("Gain",...) verbatim, which is the
            // artifact of record. What must be absent is the label MARKUP, so that
            // is what is matched.
            const juce::String doc = xml->toString();
            check(! doc.contains("<Slot") && ! doc.contains("label="),
                  "no Slot element or label attribute anywhere in the blob");
        }
    }

    std::printf(failures == 0 ? "\nAll StatePersistence checks passed.\n"
                              : "\n%d StatePersistence check(s) FAILED.\n", failures);
    // One machine-readable line for tools/health_report.py. The `checks` total is
    // the point: every harness here already carried an exact `failures` int and
    // threw the denominator away, so "0 failures" was indistinguishable from
    // "ran nothing". Format is fixed and shared by all seven harnesses.
    std::printf("PF_SUMMARY harness=%s checks=%d failures=%d\n", "StatePersistenceTest", checks, failures);
    return failures == 0 ? 0 : 1;
}
