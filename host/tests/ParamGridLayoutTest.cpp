// ---------------------------------------------------------------------------
// ParamGridLayoutTest — ArchetypeLayout.h's section/control placement
// (host/Source/ArchetypeLayout.h), ADR-035 gap 4 / session 018 track A4.
//
// host/Source/ParamGridLayout.h has carried this exact warning since it was
// written: "This comment used to name host/tests/ParamGridLayoutTest.cpp as
// the test that did it; that file has never existed... Naming a test that
// does not exist is how this project has repeatedly mistaken a declared
// control for a running one." This file is what makes that sentence false.
//
// It tests ArchetypeLayout.h, not ParamGridLayout.h's own columnsFor/rowsFor
// (those remain covered only indirectly, per that header's comment, which
// this file does not change). ArchetypeLayout.h is the header this session's
// work actually adds, and it carries the same "no JUCE dependency" property
// ParamGridLayout.h does, for the same reason: exercisable without a live
// editor, a message thread or a compiled DSP.
//
// Header-only under test: one translation unit, no JUCE, no libfaust, no
// plugin sources.
//
// Run: ./ParamGridLayoutTest — exits 0 on success, 1 with a failure list.
// ---------------------------------------------------------------------------
#include "../Source/ArchetypeLayout.h"

#include <iostream>
#include <set>
#include <string>
#include <vector>

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

void expectEq(int actual, int expected, const std::string& what)
{
    ++checks;
    if (actual != expected)
    {
        ++failures;
        std::cerr << "FAIL  " << what << "\n"
                  << "      expected: " << expected << "\n"
                  << "      actual:   " << actual << "\n";
    }
}

using ArchetypeLayout::Rect;
using ArchetypeLayout::Result;
using ArchetypeLayout::SectionInput;

// Every rect this Result produced -- headings AND controls -- pairwise
// non-overlapping. A section heading overlapping its own or another
// section's controls, or two columns bleeding into each other, would both
// show up here.
void expectNoOverlaps(const Result& r, const std::string& label)
{
    std::vector<Rect> all = r.headings;
    for (const auto& c : r.controls)
        all.push_back(c.bounds);

    for (size_t i = 0; i < all.size(); ++i)
        for (size_t j = i + 1; j < all.size(); ++j)
        {
            ++checks;
            if (all[i].overlaps(all[j]))
            {
                ++failures;
                std::cerr << "FAIL  " << label << ": rect " << i << " overlaps rect " << j
                          << "  [" << all[i].x << "," << all[i].y << "," << all[i].w << "," << all[i].h
                          << "] vs ["
                          << all[j].x << "," << all[j].y << "," << all[j].w << "," << all[j].h << "]\n";
            }
        }
}

// Every control named by `sections` appears in r.controls EXACTLY once,
// under its own (section, index) — no control silently dropped, and none
// duplicated across two placements (which the overlap check alone would not
// catch if a duplicate landed at a different y).
void expectFullCoverage(const Result& r, const std::vector<SectionInput>& sections,
                         const std::string& label)
{
    int expectedTotal = 0;
    for (const auto& s : sections)
        expectedTotal += s.controlCount;
    expectEq(static_cast<int>(r.controls.size()), expectedTotal, label + ": total control count");

    std::set<std::pair<int, int>> seen;
    for (const auto& c : r.controls)
    {
        ++checks;
        const auto key = std::make_pair(c.section, c.index);
        if (seen.count(key) != 0)
        {
            ++failures;
            std::cerr << "FAIL  " << label << ": (section " << c.section << ", index " << c.index
                      << ") placed more than once\n";
        }
        seen.insert(key);

        if (c.section < 0 || c.section >= static_cast<int>(sections.size())
            || c.index < 0 || c.index >= sections[static_cast<size_t>(c.section)].controlCount)
        {
            ++failures;
            std::cerr << "FAIL  " << label << ": (section " << c.section << ", index " << c.index
                      << ") is out of range for its section\n";
        }
    }

    expectEq(static_cast<int>(seen.size()), expectedTotal, label + ": every control placed exactly once");
}

// A 6-param effect: two sections, matching a small pedal-shaped patch (e.g.
// Drive + Tone, three controls each).
std::vector<SectionInput> sixParamEffect()
{
    return { SectionInput{ 3, 1 }, SectionInput{ 3, 1 } };
}

// An 18-param synth: four sections of uneven size and span, modelled on the
// Velvet Drift / Coral Instrument mockups (Osc/Filter/Env/Fx, one section
// wider than the rest).
std::vector<SectionInput> eighteenParamSynth()
{
    return {
        SectionInput{ 4, 1 },   // Osc
        SectionInput{ 4, 2 },   // Filter -- the wide "lg" cutoff column
        SectionInput{ 5, 1 },   // Env
        SectionInput{ 5, 1 },   // Fx
    };
}

constexpr int kRowH = 95;    // ParamGridPanel::kCellH
constexpr int kHeadingH = 20; // ParamGridPanel::kHeadingH
constexpr int kGapH = 4;     // ParamGridPanel::kSectionGapH
constexpr int kWidth = 900;

void testIsSupported()
{
    using ArchetypeLayout::isSupported;
    expectTrue(isSupported("synth-panel"),   "synth-panel is a supported archetype");
    expectTrue(isSupported("channel-strip"), "channel-strip is a supported archetype");
    expectTrue(isSupported("tape-unit"),     "tape-unit is a supported archetype");
    expectTrue(isSupported("texture-field"), "texture-field is a supported archetype");
    expectTrue(! isSupported("pedal"),       "pedal falls through to the existing grid");
    expectTrue(! isSupported("utility"),     "utility falls through to the existing grid");
    expectTrue(! isSupported(""),            "the empty archetype falls through to the existing grid");
    expectTrue(! isSupported("some-future-archetype"),
               "an unrecognised name falls through, not crashes");
}

void testColumnsNoOverlapAndFullCoverage()
{
    for (const auto& [label, sections] :
         { std::pair{ std::string("6-param effect"), sixParamEffect() },
           std::pair{ std::string("18-param synth"), eighteenParamSynth() } })
    {
        const auto r = ArchetypeLayout::columns(sections, kWidth, kRowH, kHeadingH, kGapH);
        expectNoOverlaps(r, "columns/" + label);
        expectFullCoverage(r, sections, "columns/" + label);
        expectEq(static_cast<int>(r.headings.size()), static_cast<int>(sections.size()),
                  "columns/" + label + ": one heading per section");
    }
}

void testSplitNoOverlapAndFullCoverage()
{
    for (const auto& [label, sections] :
         { std::pair{ std::string("6-param effect"), sixParamEffect() },
           std::pair{ std::string("18-param synth"), eighteenParamSynth() } })
    {
        const auto r = ArchetypeLayout::split(sections, kWidth, kRowH, kHeadingH, kGapH);
        expectNoOverlaps(r, "split/" + label);
        expectFullCoverage(r, sections, "split/" + label);
    }
}

void testRailNoOverlapAndFullCoverage()
{
    for (const auto& [label, sections] :
         { std::pair{ std::string("6-param effect"), sixParamEffect() },
           std::pair{ std::string("18-param synth"), eighteenParamSynth() } })
    {
        const auto r = ArchetypeLayout::rail(sections, kWidth, kRowH, kHeadingH, kGapH);
        expectNoOverlaps(r, "rail/" + label);
        expectFullCoverage(r, sections, "rail/" + label);

        // The reserved display region is everything left of the rail -- every
        // rect this function produces must start at or to the right of it,
        // never inside it (rail() computes x = width - railW; nothing to its
        // left is ever a valid origin here).
        int minX = kWidth;
        for (const auto& c : r.controls)
            minX = std::min(minX, c.bounds.x);
        for (const auto& h : r.headings)
            minX = std::min(minX, h.x);
        expectTrue(minX > 0, "rail: a display region is reserved left of the rail column");
    }
}

void testLayoutForDispatchesConsistently()
{
    const auto sections = eighteenParamSynth();
    for (const std::string& archetype :
         { std::string("synth-panel"), std::string("channel-strip"),
           std::string("tape-unit"), std::string("texture-field") })
    {
        const auto viaDispatch = ArchetypeLayout::layoutFor(archetype, sections, kWidth, kRowH, kHeadingH, kGapH);
        expectNoOverlaps(viaDispatch, "layoutFor/" + archetype);
        expectFullCoverage(viaDispatch, sections, "layoutFor/" + archetype);
    }

    // synth-panel and channel-strip are the SAME geometry (both are
    // "sections become columns") -- layoutFor must route both to columns(),
    // not silently diverge for one of the two names ADR-035's README groups
    // together.
    const auto a = ArchetypeLayout::layoutFor("synth-panel", sections, kWidth, kRowH, kHeadingH, kGapH);
    const auto b = ArchetypeLayout::layoutFor("channel-strip", sections, kWidth, kRowH, kHeadingH, kGapH);
    expectEq(a.contentHeight, b.contentHeight, "synth-panel and channel-strip share one geometry");
}

void testSpanWidensColumn()
{
    // Two sections, one span 1 and one span 2, three controls each. The
    // span-2 section's column must be wider -- "a section's span widens its
    // column" (GENERATION_PLAN.md Gap 4) is the one property that
    // distinguishes columns() from a plain equal-width split.
    std::vector<SectionInput> sections = { SectionInput{ 3, 1 }, SectionInput{ 3, 2 } };
    const auto r = ArchetypeLayout::columns(sections, 900, kRowH, kHeadingH, kGapH);
    expectTrue(r.headings[1].w > r.headings[0].w,
               "columns: a span-2 section's column is wider than a span-1 section's");
    expectEq(r.headings[1].w, r.headings[0].w * 2,
              "columns: a span-2 column is exactly twice a span-1 column at equal base width");
}

void testEmptySectionsDoesNotCrash()
{
    std::vector<SectionInput> empty;
    for (const auto& archetype :
         { std::string("synth-panel"), std::string("tape-unit"), std::string("texture-field") })
    {
        const auto r = ArchetypeLayout::layoutFor(archetype, empty, kWidth, kRowH, kHeadingH, kGapH);
        expectEq(static_cast<int>(r.headings.size()), 0, archetype + ": no sections, no headings");
        expectEq(static_cast<int>(r.controls.size()), 0, archetype + ": no sections, no controls");
        expectEq(r.contentHeight, 0, archetype + ": no sections, zero content height");
    }
}

void testNarrowWidthDegradesRailGracefully()
{
    // Below the [160,320] clamp floor, rail() must not produce a
    // negative-width display region or an empty rail -- it takes the whole
    // width instead (the same "degrade, never crash or go negative" posture
    // UiIr::parse() takes for a malformed field).
    std::vector<SectionInput> sections = { SectionInput{ 2, 1 } };
    const auto r = ArchetypeLayout::rail(sections, 100, kRowH, kHeadingH, kGapH);
    expectNoOverlaps(r, "rail/narrow-width");
    expectFullCoverage(r, sections, "rail/narrow-width");
    expectTrue(r.headings[0].x >= 0, "rail: narrow width never produces a negative x");
    expectTrue(r.headings[0].w <= 100, "rail: narrow width never widens past the available width");
}

} // namespace

int main()
{
    std::cout << "ParamGridLayoutTest — ArchetypeLayout.h (ADR-035 gap 4)\n";

    testIsSupported();
    testColumnsNoOverlapAndFullCoverage();
    testSplitNoOverlapAndFullCoverage();
    testRailNoOverlapAndFullCoverage();
    testLayoutForDispatchesConsistently();
    testSpanWidensColumn();
    testEmptySectionsDoesNotCrash();
    testNarrowWidthDegradesRailGracefully();

    std::cout << checks - failures << "/" << checks << " checks passed\n";

    if (failures != 0)
    {
        std::cerr << "\n" << failures << " FAILED\n";
        return 1;
    }

    std::cout << "PASS\n";
    return 0;
}
