#pragma once
#include <algorithm>
#include <string>
#include <vector>

// ── ArchetypeLayout ──────────────────────────────────────────────────────────
// Free-function section/control geometry for a generated plugin's archetype
// (ADR-035 gap 4 / GENERATION_PLAN.md "Gap 4", session 018 track A4). NO JUCE
// dependency -- mirrors ParamGridLayout.h's own convention -- so the placement
// arithmetic is exercisable from host/tests/ParamGridLayoutTest.cpp without a
// live editor, a message thread or a compiled DSP.
//
// What this covers, and what it deliberately does NOT touch:
//
// `ParamGridPanel::layoutSectioned()`'s existing geometry (used for the
// "pedal"/"utility" archetypes, "", and any archetype name this build
// predates -- deriveLayoutFromGroups() never sets `archetype` at all,
// ParamGridPanel.cpp:619-638, so every heuristically-derived Layout takes
// this path today) places every control at x = 0, one per row, with WIDTH
// cellW * (2 if that control's own `size == "lg"`, else 1) -- a per-CONTROL
// decision, not a per-SECTION one; `section.span` there only widens `cols`,
// the shared column-count clamp, never a control's x-position. That is real,
// load-bearing arithmetic already covered indirectly (EditorSessionTest
// scenario 3 pins the 6-column clamp via window height). Reimplementing it
// here as a second copy, however carefully, is exactly the
// two-copies-of-one-layout-arithmetic shape that produced the kChromeHeight
// defect PluginEditor.h's own header warns about -- so this header does not
// reimplement it. `ParamGridPanel.cpp` keeps that code, unchanged, as the
// fallback for anything `isSupported()` below returns false for.
//
// What this header adds: the THREE archetypes that read as a half-width list
// today because nothing ever placed a section beside another one --
// "synth-panel"/"channel-strip" (columns), "tape-unit" (a two-region split),
// "texture-field" (a reserved display region plus one narrow rail column).
// Vocabulary matches UiIr::Layout::archetype, which matches
// llm/ui_face.py:43's ARCHETYPES set and llm/prompts/ui_face_prompt.md's
// table.
namespace ArchetypeLayout
{

// A plain rectangle -- no juce::Rectangle, per the no-JUCE-dependency rule
// above. ParamGridPanel converts these to juce::Rectangle<int> at the one
// point it actually places a widget.
struct Rect
{
    int x = 0, y = 0, w = 0, h = 0;

    bool overlaps(const Rect& o) const
    {
        // A zero-area rect can never overlap anything, including itself at
        // the same origin -- deliberate, since an empty section (no
        // controls) legitimately produces no control rects at all, not a
        // degenerate one.
        if (w <= 0 || h <= 0 || o.w <= 0 || o.h <= 0)
            return false;
        return x < o.x + o.w && o.x < x + w && y < o.y + o.h && o.y < y + h;
    }
};

// One entry per UiIr::Section, reduced to the one fact these three
// archetypes place by: how many controls it holds. (Unlike the existing grid
// path, none of the three archetypes here size an individual control by its
// own `size == "lg"` field -- "controls flow inside a column"
// (GENERATION_PLAN.md "Gap 4") is one full-width row per control. `span`
// widens columns() only, per that same source.)
struct SectionInput
{
    int controlCount = 0;
    int span = 1;   // meaningful to columns() only; ignored by split()/rail()
};

// One placed control, addressable back to its input by (section, index) --
// `index` counts only within that section, in the order its own
// `controlCount` implies.
struct PlacedControl
{
    int section = 0;
    int index   = 0;
    Rect bounds;
};

struct Result
{
    std::vector<Rect> headings;            // one per input section, same index
    std::vector<PlacedControl> controls;   // every control, exactly once
    int contentHeight = 0;                 // total scrollable height at this width
};

// The archetype names this header has a real layout for. Everything else --
// "pedal", "utility", "", or a name this build predates -- keeps
// ParamGridPanel's own existing grid code, unchanged. Callers must check this
// before calling layoutFor(); it is not itself a fallback dispatcher, so as
// not to duplicate the grid arithmetic described above.
inline bool isSupported(const std::string& archetype)
{
    return archetype == "synth-panel" || archetype == "channel-strip"
        || archetype == "tape-unit"   || archetype == "texture-field";
}

// ── one column of stacked sections ──────────────────────────────────────────
// Places `which` sections (indices into `sections`) top-to-bottom inside one
// column of the given x/width: heading row, then one control per row at the
// column's full width, then a gap, repeated per section. Shared by columns(),
// split() and rail() below so a column's own arithmetic exists exactly once
// -- the same "one height function" principle ParamGridPanel.h's own header
// states for contentHeightForSections()/layoutSectioned(), applied one level
// deeper.
inline void stackColumn(const std::vector<SectionInput>& sections,
                         const std::vector<int>& which,
                         int x, int width, int rowH, int headingH, int sectionGapH,
                         Result& out, int& yInOut)
{
    for (int s : which)
    {
        out.headings[static_cast<size_t>(s)] = { x, yInOut, width, headingH };
        yInOut += headingH;

        const int n = sections[static_cast<size_t>(s)].controlCount;
        for (int i = 0; i < n; ++i)
        {
            out.controls.push_back({ s, i, Rect{ x, yInOut, width, rowH } });
            yInOut += rowH;
        }
        yInOut += sectionGapH;
    }
}

// ── columns: synth-panel / channel-strip ────────────────────────────────────
// One section = one column, side by side, in input order. A section's `span`
// widens its column: total width is divided into `sum(span)` equal units, and
// a section's column is `span` units wide. Each column stacks its own
// section's controls independently (stackColumn), so columns can end at
// different heights -- content height is the TALLEST column, matching the
// mockups (Velvet Drift's OSC/FILTER/ENV/FX columns are visibly uneven).
inline Result columns(const std::vector<SectionInput>& sections,
                       int width, int rowH, int headingH, int sectionGapH)
{
    Result out;
    out.headings.resize(sections.size());
    if (sections.empty())
        return out;

    int totalUnits = 0;
    for (const auto& s : sections)
        totalUnits += std::max(1, s.span);
    const int unitW = totalUnits > 0 ? width / totalUnits : width;

    int x = 0;
    int tallest = 0;
    for (size_t s = 0; s < sections.size(); ++s)
    {
        const int colW = unitW * std::max(1, sections[s].span);
        int y = 0;
        stackColumn(sections, { static_cast<int>(s) }, x, colW, rowH, headingH, sectionGapH, out, y);
        tallest = std::max(tallest, y);
        x += colW;
    }
    out.contentHeight = tallest;
    return out;
}

// ── split: tape-unit ─────────────────────────────────────────────────────────
// Two regions, transport (first half of the sections, by count) and tone (the
// rest), each its own column at half the width. An odd section count gives
// the extra section to the left (transport) region -- an arbitrary but
// deterministic tie-break; GENERATION_PLAN.md names no rule for this because
// it describes the split by CONTENT (transport vs. tone), a distinction this
// generic, content-blind function cannot make. Content height is the taller
// of the two regions.
inline Result split(const std::vector<SectionInput>& sections,
                     int width, int rowH, int headingH, int sectionGapH)
{
    Result out;
    out.headings.resize(sections.size());
    if (sections.empty())
        return out;

    const int leftCount = static_cast<int>((sections.size() + 1) / 2);
    std::vector<int> left, right;
    for (int i = 0; i < static_cast<int>(sections.size()); ++i)
        (i < leftCount ? left : right).push_back(i);

    const int leftW = width / 2;
    const int rightW = width - leftW;

    int yL = 0, yR = 0;
    stackColumn(sections, left,  0,     leftW,  rowH, headingH, sectionGapH, out, yL);
    stackColumn(sections, right, leftW, rightW, rowH, headingH, sectionGapH, out, yR);

    out.contentHeight = std::max(yL, yR);
    return out;
}

// ── rail: texture-field ──────────────────────────────────────────────────────
// A reserved display region on the left (no rects placed into it -- nothing
// draws there yet; session 019's finding F5 keeps any such display
// non-committal) and every section stacked in one narrow column on the
// right. Rail width is a third of the available width, clamped to [160, 320]
// so it neither vanishes at a small window nor swallows the whole grid at a
// large one; if the available width is narrower than the floor, the rail
// simply takes the entire width (degrades to a single full-width column
// rather than producing a negative-width display region).
inline Result rail(const std::vector<SectionInput>& sections,
                    int width, int rowH, int headingH, int sectionGapH)
{
    Result out;
    out.headings.resize(sections.size());
    if (sections.empty())
        return out;

    const int railW = std::min(width, std::clamp(width / 3, 160, 320));
    const int x = width - railW;

    std::vector<int> all(sections.size());
    for (size_t i = 0; i < sections.size(); ++i)
        all[i] = static_cast<int>(i);

    int y = 0;
    stackColumn(sections, all, x, railW, rowH, headingH, sectionGapH, out, y);
    out.contentHeight = y;
    return out;
}

// Single dispatch point across the three SUPPORTED archetypes -- both
// layoutSectioned() (placement) and contentHeightForSections() (the shell's
// window-size request) must call this SAME function for the SAME archetype,
// never one calling columns() directly while the other re-derives the same
// case by hand. Precondition: isSupported(archetype). Calling this with an
// unsupported name returns grid-shaped garbage (falls through to the
// texture-field branch), which is exactly why callers check isSupported()
// first rather than treating this as a universal fallback dispatcher.
inline Result layoutFor(const std::string& archetype,
                         const std::vector<SectionInput>& sections,
                         int width, int rowH, int headingH, int sectionGapH)
{
    if (archetype == "synth-panel" || archetype == "channel-strip")
        return columns(sections, width, rowH, headingH, sectionGapH);
    if (archetype == "tape-unit")
        return split(sections, width, rowH, headingH, sectionGapH);
    return rail(sections, width, rowH, headingH, sectionGapH);
}

} // namespace ArchetypeLayout
