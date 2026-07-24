#pragma once
#include <cmath>
#include <algorithm>

// ── ParamGridLayout ─────────────────────────────────────────────────────────
// The deterministic grid arithmetic from docs/ui_design_plan.md §3, factored out
// of ParamGridPanel as free functions so it carries NO JUCE dependency and can be
// exercised by a plain host test (host/tests/ParamGridLayoutTest.cpp) without a
// live editor, message thread, or compiled DSP. ParamGridPanel is the only
// production caller; keep the two in sync by construction (it includes this).
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
