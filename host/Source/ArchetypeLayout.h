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
// CORRECTED (fix/pedal-layout-packing): this header used to cover only
// "synth-panel"/"channel-strip"/"tape-unit"/"texture-field", with "pedal",
// "utility", "" and any unrecognised name left on ParamGridPanel.cpp's own
// grid arithmetic (every control at x=0, one per row -- a per-CONTROL width
// decision, never a per-SECTION one). That fallback is GONE: it produced, on
// a real 4-param "pedal"-archetype generation, one section per parameter,
// half the panel dead by construction, and a `size:"lg"` control's centre
// landing at a different x than its siblings (double-width box, same x=0 --
// JUCE centres a rotary in whatever bounds it is handed). `layoutFor()` is
// now TOTAL: every archetype string, including one this build predates,
// resolves to a real layout via `row()`, the new default. There is no second
// grid implementation left anywhere for callers to fall through to.
//
// Packing (new: `perRowFor()`/`stackColumn()`) places more than one control
// per row inside a section/column -- a PURE function of `controlCount` and
// `span`, deliberately never of pixel width. `contentHeightForSections()`
// (ParamGridPanel.cpp) calls `layoutFor()` with a width that CAN be 0 before
// the shell has granted the panel a size yet, and depends on the resulting
// `contentHeight` being the same answer regardless -- true only if row COUNT
// never depends on width. This is also why packing is hand-rolled here
// rather than routed through `juce::FlexBox`: FlexBox wrapping is measured
// against the box's actual width, which would make content height a function
// of width and reintroduce the exact "two answers for one Layout" shape the
// kChromeHeight defect was.
//
// `size:"lg"` (`llm/ui_face.py`'s `MAX_LG_CONTROLS = 2` per FACE, not per
// section) is `SectionInput::large[i]` here: that control renders alone on
// its own full-width row rather than packed alongside its section's other
// controls, replacing the old width-doubling that caused the misalignment
// above.
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

// One entry per UiIr::Section. `large` marks, by in-section control index
// (matching PlacedControl::index), which controls render solo on their own
// row instead of being packed -- see the header comment above. Empty (or
// shorter than `controlCount`, or all-false) is the common case: every
// control packs through `perRowFor()`. A `large` entry past `controlCount`
// is simply never consulted.
struct SectionInput
{
    int controlCount = 0;
    int span = 1;              // column-width unit (columns()) / packing width hint
    std::vector<bool> large;   // size 0..controlCount; large[i] => control i is solo
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

// How many (non-solo) controls share one row, for a section of this `span`
// carrying `packedCount` packable controls. A pure function of count/span --
// see the header comment on why this must never read a pixel width.
//
// `span*2` centres the packing density on the reference mockups (a span-1
// column like Velvet Drift's OSC packs 2 across; a span-2 EQ row like Iron
// Strip's four knobs packs 4 across), clamped to [1,4] so a lone control
// never "packs" into a wider row than it has content for and a huge section
// never produces an absurdly wide single row. The second pass rebalances so
// the LAST row is never a lone orphan (e.g. 5 controls at perRow=4 would
// otherwise leave a widowed 5th row of one) -- `rows = ceil(n/perRow)`,
// then `perRow = ceil(n/rows)` redistributes evenly across those rows.
inline int perRowFor(int packedCount, int span)
{
    if (packedCount <= 0)
        return 1;
    int perRow = std::clamp(span * 2, 1, 4);
    perRow = std::min(perRow, packedCount);
    const int rows = (packedCount + perRow - 1) / perRow;
    return (packedCount + rows - 1) / rows;
}

// ── one column of stacked sections ──────────────────────────────────────────
// Places `which` sections (indices into `sections`) top-to-bottom inside one
// column of the given x/width: a heading row, then that section's controls
// packed `perRowFor()`-wide per row (a `large` control instead gets a solo
// full-width row of its own), then a gap, repeated per section. Shared by
// columns(), split(), rail() and row() below so a column's own arithmetic
// exists exactly once -- the same "one height function" principle
// ParamGridPanel.h's own header states for
// contentHeightForSections()/layoutSectioned(), applied one level deeper.
inline void stackColumn(const std::vector<SectionInput>& sections,
                         const std::vector<int>& which,
                         int x, int width, int rowH, int headingH, int sectionGapH,
                         Result& out, int& yInOut)
{
    for (int s : which)
    {
        const auto& sec = sections[static_cast<size_t>(s)];
        out.headings[static_cast<size_t>(s)] = { x, yInOut, width, headingH };
        yInOut += headingH;

        const auto isLarge = [&sec](int i)
        {
            return i < static_cast<int>(sec.large.size()) && sec.large[static_cast<size_t>(i)];
        };

        int packedCount = 0;
        for (int i = 0; i < sec.controlCount; ++i)
            if (! isLarge(i))
                ++packedCount;
        const int perRow = perRowFor(packedCount, sec.span);

        std::vector<int> rowIdx;   // packed indices queued for the row being built
        const auto flushRow = [&]()
        {
            if (rowIdx.empty())
                return;
            const int cellW = width / static_cast<int>(rowIdx.size());
            for (size_t k = 0; k < rowIdx.size(); ++k)
                out.controls.push_back({ s, rowIdx[k],
                    Rect{ x + static_cast<int>(k) * cellW, yInOut, cellW, rowH } });
            yInOut += rowH;
            rowIdx.clear();
        };

        for (int i = 0; i < sec.controlCount; ++i)
        {
            if (isLarge(i))
            {
                flushRow();   // a solo control starts its own row, packed or not
                out.controls.push_back({ s, i, Rect{ x, yInOut, width, rowH } });
                yInOut += rowH;
            }
            else
            {
                rowIdx.push_back(i);
                if (static_cast<int>(rowIdx.size()) == perRow)
                    flushRow();
            }
        }
        flushRow();

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

// ── row: pedal / utility / "" / any unrecognised archetype ──────────────────
// The new default. Every section stacked full-width, top to bottom, each
// packing its own controls via stackColumn -- replacing ParamGridPanel.cpp's
// old one-per-row grid fallback entirely. With Phase 2's controls-per-section
// minimum (a separate change, `deriveLayoutFromGroups()`/`llm/ui_face.py`), a
// typical 3-7 param pedal collapses to one section, which this turns into a
// single packed row or two of knobs across the panel -- an Iron-Strip-style
// strip, not a stack of one-knob islands.
inline Result row(const std::vector<SectionInput>& sections,
                   int width, int rowH, int headingH, int sectionGapH)
{
    Result out;
    out.headings.resize(sections.size());
    if (sections.empty())
        return out;

    std::vector<int> all(sections.size());
    for (size_t i = 0; i < sections.size(); ++i)
        all[i] = static_cast<int>(i);

    int y = 0;
    stackColumn(sections, all, 0, width, rowH, headingH, sectionGapH, out, y);
    out.contentHeight = y;
    return out;
}

// Single dispatch point across EVERY archetype -- both layoutSectioned()
// (placement) and contentHeightForSections() (the shell's window-size
// request) must call this SAME function for the SAME archetype, never one
// calling columns()/row() directly while the other re-derives the same case
// by hand. Total: `row()` is the default for "pedal", "utility", "", and any
// archetype name this build predates, so there is no unsupported case left
// for a caller to special-case.
inline Result layoutFor(const std::string& archetype,
                         const std::vector<SectionInput>& sections,
                         int width, int rowH, int headingH, int sectionGapH)
{
    if (archetype == "synth-panel" || archetype == "channel-strip")
        return columns(sections, width, rowH, headingH, sectionGapH);
    if (archetype == "tape-unit")
        return split(sections, width, rowH, headingH, sectionGapH);
    if (archetype == "texture-field")
        return rail(sections, width, rowH, headingH, sectionGapH);
    return row(sections, width, rowH, headingH, sectionGapH);
}

} // namespace ArchetypeLayout
