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
// dependent); only that the <SlotLabels> node is emitted. A trivial passthrough is
// used as the source so the test does not depend on any particular stdlib function.
//
// Build/run:
//   cmake --build build --target StatePersistenceTest
//   ./build/StatePersistenceTest_artefacts/Debug/StatePersistenceTest

#include "../Source/PluginProcessor.h"
#include <cstdio>
#include <cmath>

namespace
{
int failures = 0;

void check(bool condition, const char* what)
{
    std::printf("  [%s] %s\n", condition ? " OK " : "FAIL", what);
    if (! condition)
        ++failures;
}

void checkNear(float actual, float expected, const char* what)
{
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

        // Retains source + prompt (and queues a compile we do not wait on).
        a.loadFaustCode(kSource, kPrompt);

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
            check(root.getChildWithName("SlotLabels").isValid(), "SlotLabels node emitted");
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

    std::printf(failures == 0 ? "\nAll StatePersistence checks passed.\n"
                              : "\n%d StatePersistence check(s) FAILED.\n", failures);
    return failures == 0 ? 0 : 1;
}
