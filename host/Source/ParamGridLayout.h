#pragma once
#include <cmath>
#include <algorithm>

// ── ParamGridLayout ─────────────────────────────────────────────────────────
// The deterministic grid arithmetic from docs/ui_design_plan.md §3, factored out
// of ParamGridPanel as free functions so it carries NO JUCE dependency and COULD
// be exercised without a live editor, message thread or compiled DSP.
//
// It is not, today. This comment used to name host/tests/ParamGridLayoutTest.cpp
// as the test that did it; that file has never existed. The arithmetic is covered
// only indirectly, through EditorSessionTest scenario 3 (a 40-param patch, which
// pins the 6-column clamp and the row count via the window height it prints).
// Naming a test that does not exist is how this project has repeatedly mistaken a
// declared control for a running one — so either write it or leave this honest.
//
// ParamGridPanel is the only production caller; keep the two in sync by
// construction (it includes this).
namespace ParamGridLayout
{

// Columns for N controls: cols = clamp(ceil(sqrt(N)), 2, 6).
// N <= 0 returns the floor (2) so an empty grid still has a sane width divisor.
inline int columnsFor(int n)
{
    if (n <= 0)
        return 2;
    const int c = static_cast<int>(std::ceil(std::sqrt(static_cast<double>(n))));
    return std::clamp(c, 2, 6);
}

// Rows needed to hold N controls at columnsFor(N) columns.
inline int rowsFor(int n)
{
    if (n <= 0)
        return 0;
    const int cols = columnsFor(n);
    return (n + cols - 1) / cols;   // ceil(n / cols)
}

} // namespace ParamGridLayout
