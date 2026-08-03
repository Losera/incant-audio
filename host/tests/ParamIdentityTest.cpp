// ---------------------------------------------------------------------------
// ParamIdentityTest — the derivation rule behind host/Source/ParamIdentity.h.
//
// WHY THIS TEST IS NOT OPTIONAL. The rule it pins is a one-way door: once these
// strings are written into a saved user project, changing what they produce for
// an input they already accepted orphans every restored value in every saved
// session. There is no way to notice that by eye and no way to recover from it
// afterwards, so the rule is nailed down here, at the cheapest rung, before it
// ever reaches a blob.
//
// Header-only under test, so this compiles ONE translation unit and links no
// JUCE, no libfaust and no plugin -- deliberately, and for the same reason
// ParamGridLayout.h was factored out: a rule that is pure string arithmetic must
// be exercisable without a compiled DSP, a message thread or a live editor.
// (ParamGridLayout.h's header once named a test that had never existed. This one
// exists; that is the whole difference.)
//
// Run: ./ParamIdentityTest  -- exits 0 on success, 1 with a failure list.
// ---------------------------------------------------------------------------
#include "../Source/ParamIdentity.h"

#include <iostream>
#include <string>
#include <vector>

namespace
{
int failures = 0;
int checks   = 0;

void expectEq(const std::string& actual, const std::string& expected,
              const std::string& what)
{
    ++checks;
    if (actual != expected)
    {
        ++failures;
        std::cerr << "FAIL  " << what << "\n"
                  << "      expected: \"" << expected << "\"\n"
                  << "      actual:   \"" << actual   << "\"\n";
    }
}

void expectNe(const std::string& a, const std::string& b, const std::string& what)
{
    ++checks;
    if (a == b)
    {
        ++failures;
        std::cerr << "FAIL  " << what << "\n"
                  << "      both were: \"" << a << "\" (they must differ)\n";
    }
}

// ── slug() ──────────────────────────────────────────────────────────────────
void testSlug()
{
    using ParamIdentity::slug;

    expectEq(slug("Cutoff"),        "cutoff",       "slug lowercases");
    expectEq(slug("Attack Time"),   "attack_time",  "slug replaces a space");
    expectEq(slug("Dry/Wet"),       "dry_wet",      "slug replaces a slash inside a label");
    expectEq(slug("Q"),             "q",            "slug keeps a single character");
    expectEq(slug("Osc 2"),         "osc_2",        "slug keeps digits");

    // Collapsing and trimming -- the three rules that make the output stable
    // against cosmetic label edits.
    expectEq(slug("Low  --  Pass"), "low_pass",     "slug collapses a run of separators");
    expectEq(slug("  Gain  "),      "gain",         "slug trims leading/trailing separators");
    expectEq(slug("!!Drive!!"),     "drive",        "slug trims leading/trailing punctuation");
    expectEq(slug("A_-_B"),         "a_b",          "slug collapses mixed separators");

    // The degenerate input. A label with no alphanumeric character at all would
    // otherwise produce an empty component, which would make "osc/" and a
    // genuinely ungrouped parameter indistinguishable.
    expectEq(slug("---"),           "param",        "slug falls back for an all-separator label");
    expectEq(slug(""),              "param",        "slug falls back for an empty label");

    // Non-ASCII. The point is not the specific output -- it is that the bytes go
    // through the separator branch DETERMINISTICALLY rather than indexing the
    // negative half of glibc's ctype table (ParamIdentity.h's cast comment).
    expectEq(slug("Résonance"),     "r_sonance",    "slug handles a UTF-8 byte deterministically");
}

// ── base() ──────────────────────────────────────────────────────────────────
void testBase()
{
    using ParamIdentity::base;

    expectEq(base("", "Gain"),               "gain",
             "base omits the separator for an ungrouped param");
    expectEq(base("Filter", "Cutoff"),       "filter/cutoff",
             "base joins group and label");
    expectEq(base("Filter/Env 2", "Amount"), "filter/env_2/amount",
             "base slugs each path component separately and keeps the separators");

    // THE CASE THAT MOTIVATES GROUPING AT ALL. Two controls both called "Gain",
    // in different sections, are different parameters. An identity scheme that
    // conflated them would be worse than the ordinal one it replaces, because it
    // would actively move a value onto the wrong control instead of merely
    // failing to move it onto the right one.
    expectNe(base("Osc", "Gain"), base("Amp", "Gain"),
             "two same-named params in DIFFERENT groups must not share an id");
}

// ── disambiguate() ──────────────────────────────────────────────────────────
void testDisambiguate()
{
    using ParamIdentity::disambiguate;

    // First occurrence keeps the bare name.
    expectEq(disambiguate("filter/cutoff", {}), "filter/cutoff",
             "the first occurrence is unsuffixed");

    // THE RED CASE. Faust permits two controls with the same label in the same
    // group. Without a suffix they would share an id, and identity-keyed remap
    // would assign both to one slot -- silently dropping a control, which is
    // exactly the class of failure PF-051 records for the 65th parameter.
    expectEq(disambiguate("amp/gain", {"amp/gain"}), "amp/gain#2",
             "a same-group collision takes #2");
    expectEq(disambiguate("amp/gain", {"amp/gain", "amp/gain#2"}), "amp/gain#3",
             "a third collision takes #3");

    // A suffix must not be handed out twice even when the taken list is not in
    // capture order -- the scan is a membership test, not a count.
    expectEq(disambiguate("amp/gain", {"amp/gain#2", "amp/gain"}), "amp/gain#3",
             "suffixing is order-independent");

    // An unrelated id must not trigger a suffix.
    expectEq(disambiguate("amp/gain", {"amp/level", "osc/gain"}), "amp/gain",
             "a non-colliding id stays unsuffixed");
}

// ── The end-to-end shape ParamCapture produces ──────────────────────────────
// Mirrors FaustEngine.cpp's consume(): derive a base, disambiguate against what
// this pass has already assigned, append. If consume() ever stops doing exactly
// this, this test keeps passing and that is a real gap -- stated here rather
// than left implied. Closing it needs a compiled DSP, which is OfflineRenderTest's
// rung, not this one.
void testCapturePassShape()
{
    struct Widget { const char* group; const char* label; };
    const std::vector<Widget> widgets {
        { "",       "Gain"   },
        { "Filter", "Cutoff" },
        { "Filter", "Q"      },
        { "Amp",    "Gain"   },   // same label as widget 0, different group
        { "Amp",    "Gain"   },   // genuine same-group duplicate
    };

    std::vector<std::string> assigned;
    for (const auto& w : widgets)
        assigned.push_back(
            ParamIdentity::disambiguate(ParamIdentity::base(w.group, w.label), assigned));

    expectEq(assigned[0], "gain",       "pass: ungrouped Gain");
    expectEq(assigned[1], "filter/cutoff", "pass: grouped Cutoff");
    expectEq(assigned[2], "filter/q",   "pass: grouped Q");
    expectEq(assigned[3], "amp/gain",   "pass: Amp/Gain does not collide with bare gain");
    expectEq(assigned[4], "amp/gain#2", "pass: the true duplicate is suffixed");

    // Every id in one pass must be unique. This is the invariant the whole
    // scheme rests on: remap() uses the id as a map key, so a duplicate is a
    // silently dropped control.
    for (size_t i = 0; i < assigned.size(); ++i)
        for (size_t j = i + 1; j < assigned.size(); ++j)
            expectNe(assigned[i], assigned[j],
                     "pass: ids " + std::to_string(i) + " and " + std::to_string(j)
                         + " must be distinct");
}
} // namespace

int main()
{
    std::cout << "ParamIdentityTest — the derivation rule (ParamIdentity.h)\n";

    testSlug();
    testBase();
    testDisambiguate();
    testCapturePassShape();

    std::cout << checks - failures << "/" << checks << " checks passed\n";

    if (failures != 0)
    {
        std::cerr << "\n" << failures << " FAILED\n";
        return 1;
    }

    std::cout << "PASS\n";
    return 0;
}
