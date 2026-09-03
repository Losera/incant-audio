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
                         const juce::String& family = {},
                         const juce::String& familySource = {},
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

    p.loadFaustCode(source, prompt, mode, family, familySource);

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
                                  PluginForgeProcessor::LoadMode::Iterate,
                                  "granular_effect", "explicit"),
              "compile completed before serialising");

        // C6: prompt history is now part of the persisted state.
        a.setPromptHistory({ "a warm lowpass", "add a chorus" });

        // UiIr schema 3 (generated-plugin faces, Step 1): the per-plugin layout
        // + theme rides the blob too. Editor-pushed in the real app; set here
        // directly, the same way setPromptHistory() is.
        {
            UiIr::Layout ir;
            ir.schema = 3;
            ir.archetype = "tape-unit";
            ir.tokens = "echo-plate";
            ir.components.sampleBrowser = true;
            ir.theme.accent = "#f0a63c";
            ir.theme.knob = "pointer";
            UiIr::Section s;
            s.id = "mix";
            s.title = "MIX";
            s.controls.push_back({ "Mix", "slider", "md" });
            ir.sections.push_back(s);
            a.setUiIr(ir);
        }

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
            check(root.getProperty("generationFamily").toString() == "granular_effect",
                  "generation family is persisted");
            check(root.getProperty("familySource").toString() == "explicit",
                  "family resolution source is persisted");
            check(! root.getChildWithName("SlotLabels").isValid(),
                  "no SlotLabels node — dropped from v1 2026-07-27");
            check(root.hasProperty("uiIr"),
                  "uiIr attribute present (v3 amendment, 2026-09-01)");

            // v2 (2026-08-02) added the slot -> ParamIdentity map; v3 (2026-08-12,
            // C6) adds prompt history. The count assertion is kept rather than
            // relaxed to ">= 1" -- its job is to catch a node nobody meant to add,
            // and a bound that grows whenever anything is added would stop doing
            // that job. Bump it deliberately, with the new node named, or not at
            // all.
            check(root.getChildWithName("ParamMap").isValid(),
                  "ParamMap child present (schemaVersion 2)");
            check(root.getChildWithName("PromptHistory").isValid(),
                  "PromptHistory child present (schemaVersion 3)");
            check(root.getNumChildren() == 3,
                  "STATE, ParamMap and PromptHistory are the blob's only children");

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

        const auto restoredHistory = b.promptHistoryForTest();
        check(restoredHistory.size() == 2, "prompt history: both entries restored");
        check(restoredHistory[0] == "a warm lowpass" && restoredHistory[1] == "add a chorus",
              "prompt history: order preserved");

        check(b.currentFamily() == "granular_effect", "generation family restored");
        check(b.currentFamilySource() == "explicit", "family resolution source restored");

        const auto ir = b.uiIrForTest();
        check(ir.schema == 3, "uiIr: schema 3 restored");
        check(ir.archetype == "tape-unit", "uiIr: archetype restored");
        check(ir.tokens == "echo-plate", "uiIr: tokens restored");
        check(ir.components.sampleBrowser, "uiIr: components restored");
        check(ir.theme.accent == "#f0a63c", "uiIr: theme.accent restored");
        check(ir.theme.knob == "pointer", "uiIr: theme.knob restored");
        check(ir.theme.surface == "#0c0c0c", "uiIr: unset theme token is the Ember default");
        check(ir.sections.size() == 1 && ir.sections[0].id == "mix",
              "uiIr: section restored");
    }

    // ── v2 BACK-COMPAT: a blob saved before prompt history still restores ────
    // Same insurance as the v1-ParamMap case above, one schema step later: a
    // project saved under schemaVersion 2 (2026-08-02..2026-08-12) genuinely
    // has no PromptHistory node, and the correct response is an EMPTY history,
    // not an error or a crash -- there is nothing to migrate a v2 blob's
    // history into, because it never had one.
    {
        std::printf("\nv2 back-compat — a pre-history blob still restores with empty history\n");

        juce::MemoryBlock v2Blob;
        {
            PluginForgeProcessor donor;
            check(loadAndAwaitCompile(donor, kSource, kPrompt,
                                      PluginForgeProcessor::LoadMode::Iterate),
                  "v2 donor compile completed before serialising");
            // No setPromptHistory() call: this donor's blob would ALREADY lack a
            // PromptHistory node (the loop that builds it just runs zero times
            // over an empty promptHistory) -- stamping schemaVersion 2 below is
            // what makes it an honest v2 fixture rather than merely a v3 blob
            // with an empty history.
            juce::MemoryBlock current;
            donor.getStateInformation(current);
            auto xml = juce::AudioProcessor::getXmlFromBinary(
                           current.getData(), static_cast<int>(current.getSize()));
            check(xml != nullptr, "v2 donor blob parsed");
            if (xml != nullptr)
            {
                auto root = juce::ValueTree::fromXml(*xml);
                root.setProperty("schemaVersion", 2, nullptr);
                root.removeChild(root.getChildWithName("PromptHistory"), nullptr);

                check(! root.getChildWithName("PromptHistory").isValid(),
                      "the synthesised v2 blob genuinely has no PromptHistory");
                check((int) root.getProperty("schemaVersion", 0) == 2,
                      "the synthesised blob declares schemaVersion 2");

                if (auto v2Xml = root.createXml())
                    juce::AudioProcessor::copyXmlToBinary(*v2Xml, v2Blob);
            }
        }

        PluginForgeProcessor v2;
        v2.setStateInformation(v2Blob.getData(), static_cast<int>(v2Blob.getSize()));

        check(v2.currentSourceForTest() == kSource, "v2: Faust source restored");
        check(v2.promptHistoryForTest().isEmpty(),
              "v2: prompt history restores empty, not an error");
    }

    // ── v1 MIGRATION: a blob saved before the identity map still restores ────
    // THE ONE-WAY-DOOR INSURANCE. schemaVersion 2 added the slot -> ParamIdentity
    // map, and every project saved before 2026-08-02 lacks it. Those blobs are in
    // users' sessions already and cannot be re-saved retroactively, so "we will
    // handle it later" is not available -- if a v1 blob does not restore, the data
    // is gone.
    //
    // The fallback is deliberately the DO-NOTHING one: no map means an empty
    // identity seed, which makes remap() pack params in capture order -- exactly
    // the positional layout v1 values were written under. So the old arrangement
    // and the fallback are the same arrangement, and no migration code exists to
    // rot. This test is what proves that claim rather than asserting it.
    //
    // A v1 blob is synthesised here rather than checked in as a fixture, because a
    // fixture would silently keep passing if getStateInformation stopped being
    // able to produce something a reader can still parse. Built from the same
    // pieces v1 actually wrote: root tag, schemaVersion 1, faustSource, prompt,
    // and STATE -- and no ParamMap, no idScheme.
    {
        std::printf("\nv1 migration — a pre-identity blob still restores\n");

        juce::MemoryBlock v1Blob;
        {
            // Borrow a real APVTS tree by round-tripping a live processor, then
            // strip the v2 additions back off. This keeps the STATE child's exact
            // shape honest instead of hand-rolling 64 PARAM elements.
            PluginForgeProcessor donor;
            setSlot(donor, 0,  0.25f);
            setSlot(donor, 3,  0.42f);
            setSlot(donor, 63, 1.0f);

            juce::MemoryBlock current;
            donor.getStateInformation(current);
            auto xml = juce::AudioProcessor::getXmlFromBinary(
                           current.getData(), static_cast<int>(current.getSize()));
            check(xml != nullptr, "donor blob parsed");
            if (xml != nullptr)
            {
                auto root = juce::ValueTree::fromXml(*xml);
                root.setProperty("schemaVersion", 1, nullptr);
                root.setProperty("faustSource",   kSource, nullptr);
                root.setProperty("prompt",        kPrompt, nullptr);
                root.removeChild(root.getChildWithName("ParamMap"), nullptr);

                check(! root.getChildWithName("ParamMap").isValid(),
                      "the synthesised v1 blob genuinely has no ParamMap");
                check((int) root.getProperty("schemaVersion", 0) == 1,
                      "the synthesised blob declares schemaVersion 1");

                if (auto v1Xml = root.createXml())
                    juce::AudioProcessor::copyXmlToBinary(*v1Xml, v1Blob);
            }
        }

        PluginForgeProcessor v1;
        v1.setStateInformation(v1Blob.getData(), static_cast<int>(v1Blob.getSize()));

        checkNear(getSlot(v1, 0),  0.25f, "v1: macro_0 restored");
        checkNear(getSlot(v1, 3),  0.42f, "v1: macro_3 restored");
        checkNear(getSlot(v1, 63), 1.0f,  "v1: macro_63 restored");
        check(v1.currentSourceForTest() == kSource, "v1: Faust source restored");
        check(v1.currentPromptForTest() == kPrompt, "v1: prompt restored");
        check(v1.currentFamily() == "effect", "v1: family migrates to the effect default");
        check(v1.currentFamilySource() == "legacy_default", "v1: family source records migration");
    }

    // ── An UNKNOWN id scheme falls back rather than mis-mapping ──────────────
    // A future build may change the derivation rule. A blob written by it carries
    // ids this build cannot interpret, and honouring them would map saved values
    // onto whatever those strings happen to collide with under the current rule --
    // silently, and onto the wrong controls. Falling back to positional loses the
    // improvement for that one blob and cannot corrupt it.
    {
        juce::MemoryBlock futureBlob;
        {
            PluginForgeProcessor donor;
            setSlot(donor, 0, 0.25f);
            setSlot(donor, 3, 0.42f);

            juce::MemoryBlock current;
            donor.getStateInformation(current);
            auto xml = juce::AudioProcessor::getXmlFromBinary(
                           current.getData(), static_cast<int>(current.getSize()));
            if (xml != nullptr)
            {
                auto root = juce::ValueTree::fromXml(*xml);
                root.setProperty("faustSource", kSource, nullptr);
                auto mapNode = root.getChildWithName("ParamMap");
                check(mapNode.isValid(), "donor blob carried a ParamMap to corrupt");
                mapNode.setProperty("idScheme", "v99-from-the-future", nullptr);

                if (auto futureXml = root.createXml())
                    juce::AudioProcessor::copyXmlToBinary(*futureXml, futureBlob);
            }
        }

        PluginForgeProcessor f;
        f.setStateInformation(futureBlob.getData(), static_cast<int>(futureBlob.getSize()));
        checkNear(getSlot(f, 0), 0.25f, "unknown idScheme: values still restored");
        checkNear(getSlot(f, 3), 0.42f, "unknown idScheme: second value still restored");
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

        // ── Iterate onto an EMPTY pool now seeds, and that is the fix ──────────
        // This block used to assert the opposite: that Iterate left the 0.1 alone.
        // The expectation changed on 2026-08-02 with identity-keyed assignment,
        // deliberately, and the old one was the weaker contract.
        //
        // Read what the setup actually says. A brand-new processor has no live
        // patch and an empty identity map; the 0.1 was poked directly into slot 0
        // and was never a value any user set on any control. "Gain" is therefore a
        // NEWCOMER, and retaining an unrelated leftover to drive it is precisely
        // the PF-020 hazard -- a knob coming up at a position that belongs to
        // nothing. Seeding it to Gain's own declared 0.75 is the correct answer.
        //
        // Iterate has NOT stopped preserving values. It preserves the value of any
        // param that RECLAIMS its slot, which is the real refine case and is
        // asserted two blocks down; what it no longer does is hand a newcomer a
        // dead value. EditorSessionTest scenario 15 covers the same split through
        // the UI, with a survivor and a newcomer in one patch.
        {
            PluginForgeProcessor p;
            setSlot(p, 0, 0.1f);
            check(loadAndAwaitCompile(p, withDefault, "iterate",
                                      PluginForgeProcessor::LoadMode::Iterate),
                  "Iterate patch compiled");
            checkNear(getSlot(p, 0), 0.75f,
                      "Iterate SEEDED a newcomer rather than inheriting an unrelated leftover");
        }

        // ── The real refine case: a survivor keeps its value ──────────────────
        // Same patch loaded twice. The second load's "Gain" has the same identity
        // as the first's, so it reclaims its slot and the user's value stands.
        // This is the half of Iterate that must NOT change, and without it the
        // block above could be satisfied by an implementation that simply always
        // seeds -- i.e. by deleting Iterate altogether.
        {
            PluginForgeProcessor p;
            check(loadAndAwaitCompile(p, withDefault, "iterate-first",
                                      PluginForgeProcessor::LoadMode::Fresh),
                  "the patch compiled once");
            setSlot(p, 0, 0.1f);   // now a value a user really did set on Gain
            check(loadAndAwaitCompile(p, withDefault, "iterate-second",
                                      PluginForgeProcessor::LoadMode::Iterate),
                  "the same patch recompiled in Iterate mode");
            checkNear(getSlot(p, 0), 0.1f,
                      "Iterate PRESERVED the value of a param that reclaimed its slot");
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
            check(root.getNumChildren() == 3,
                  "STATE, ParamMap and PromptHistory remain the blob's only children");

            // Deliberately NOT "the blob does not contain 'Gain'" — it does, and
            // must: faustSource carries hslider("Gain",...) verbatim, which is the
            // artifact of record. What must be absent is the LABEL markup.
            //
            // <Slot> is now expected -- it is how v2 records the identity map --
            // so this no longer asserts its absence. `label=` still must not
            // appear: the dropped <SlotLabels> node wrote human labels, whereas a
            // v2 Slot carries `id=`, a derived key. The distinction is the point.
            // A slot's DISPLAY name is still rebuilt by remap() on restore and is
            // still not persisted.
            const juce::String doc = xml->toString();
            check(! doc.contains("label="),
                  "no label attribute anywhere in the blob");
            check(doc.contains("<Slot") && doc.contains("id="),
                  "the identity map IS persisted, as Slot/id");
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
