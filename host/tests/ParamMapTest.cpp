// ParamMapTest — the slot<->zone conversion that PluginForge got wrong for its
// entire history: a 0-1 macro slot was pushed raw into a Faust zone, so a knob
// at midpoint set a 20..20000 Hz cutoff to 0.5 Hz.
//
// Tests are phrased as observable value contracts (what does the zone read when
// the knob is here) rather than as internal state, and the headline case is the
// plan's acceptance criterion: a 20..20000 Hz cutoff at knob midpoint must land
// on the LOG midpoint, ~632 Hz.
//
// Build/run:
//   cmake --build build --target ParamMapTest
//   ./build/ParamMapTest_artefacts/Debug/ParamMapTest

#include "../Source/ParamMap.h"
#include <cmath>
#include <cstdio>

namespace
{
int failures = 0;

void check(bool condition, const char* what)
{
    std::printf("  [%s] %s\n", condition ? " OK " : "FAIL", what);
    if (! condition)
        ++failures;
}

void checkNear(float actual, float expected, float tolerance, const char* what)
{
    const bool ok = std::fabs(actual - expected) <= tolerance;
    std::printf("  [%s] %s (got %.4f, expected %.4f +/- %.4f)\n",
                ok ? " OK " : "FAIL", what, actual, expected, tolerance);
    if (! ok)
        ++failures;
}

using PI = FaustEngine::ParamInfo;

PI slider(float init, float lo, float hi, float step = 0.0f)
{
    PI p {};
    p.label = "test";
    p.defaultValue = init;
    p.min = lo;
    p.max = hi;
    p.step = step;
    p.kind = FaustEngine::Kind::HSlider;
    return p;
}
} // namespace

int main()
{
    std::printf("ParamMapTest\n");

    // ── 1. THE headline bug: Hz defaults to a log curve ─────────────────────
    {
        PI cutoff = slider(1000.0f, 20.0f, 20000.0f, 1.0f);
        cutoff.unit = "Hz";

        // Log midpoint of 20..20000 == 20 * sqrt(1000) == 632.46 Hz.
        checkNear(ParamMap::mapSlotToZone(0.5f, cutoff), 632.46f, 32.0f,
                  "20-20kHz cutoff at knob midpoint -> ~632 Hz (log midpoint)");

        check(ParamMap::mapSlotToZone(0.5f, cutoff) > 100.0f,
              "midpoint is emphatically NOT 0.5 Hz (the original bug)");

        checkNear(ParamMap::mapSlotToZone(0.0f, cutoff), 20.0f, 0.51f,
                  "knob at 0 -> range minimum");
        checkNear(ParamMap::mapSlotToZone(1.0f, cutoff), 20000.0f, 1.0f,
                  "knob at 1 -> range maximum");
    }

    // ── 2. Round-trip: the two directions are exact inverses ────────────────
    {
        PI hz = slider(1000.0f, 20.0f, 20000.0f);
        hz.unit = "Hz";
        PI lin = slider(0.5f, -60.0f, 12.0f);
        PI logExplicit = slider(1.0f, 0.1f, 100.0f);
        logExplicit.scale = FaustEngine::Scale::Log;

        bool allRoundTrip = true;
        for (const PI* p : { &hz, &lin, &logExplicit })
            for (float n = 0.0f; n <= 1.0001f; n += 0.05f)
            {
                const float back =
                    ParamMap::mapZoneToSlot(ParamMap::mapSlotToZone(n, *p), *p);
                if (std::fabs(back - n) > 1.0e-3f)
                    allRoundTrip = false;
            }
        check(allRoundTrip, "mapZoneToSlot(mapSlotToZone(n)) == n across curves");
    }

    // ── 3. A patch default seeds the slot to that exact value ───────────────
    {
        PI cutoff = slider(1000.0f, 20.0f, 20000.0f);
        cutoff.unit = "Hz";
        const float slot = ParamMap::mapZoneToSlot(cutoff.defaultValue, cutoff);
        checkNear(ParamMap::mapSlotToZone(slot, cutoff), 1000.0f, 5.0f,
                  "declared default of 1000 Hz round-trips to 1000 Hz");
    }

    // ── 4. dB stays linear (decibels are already logarithmic) ───────────────
    {
        PI gain = slider(0.0f, -60.0f, 12.0f);
        gain.unit = "dB";
        check(ParamMap::curveFor(gain) == ParamMap::Curve::Linear,
              "dB unit stays linear (no double-log)");
        checkNear(ParamMap::mapSlotToZone(0.5f, gain), -24.0f, 0.01f,
                  "dB midpoint is the arithmetic midpoint");
    }

    // ── 5. Explicit [scale:log] wins; degenerate ranges fall back ───────────
    {
        PI p = slider(1.0f, 0.1f, 100.0f);
        p.scale = FaustEngine::Scale::Log;
        checkNear(ParamMap::mapSlotToZone(0.5f, p), 3.162f, 0.05f,
                  "explicit log scale: midpoint of 0.1..100 is ~3.162");

        // min <= 0 cannot be log-mapped; Faust clamps to DBL_EPSILON (16 useless
        // decades), we deliberately fall back to linear instead.
        PI bad = slider(0.0f, 0.0f, 20000.0f);
        bad.scale = FaustEngine::Scale::Log;
        check(ParamMap::curveFor(bad) == ParamMap::Curve::Linear,
              "log with min<=0 falls back to linear, does not assert");
        checkNear(ParamMap::mapSlotToZone(0.5f, bad), 10000.0f, 1.0f,
                  "fallback behaves as a plain linear range");

        PI hzZeroMin = slider(0.0f, 0.0f, 20000.0f);
        hzZeroMin.unit = "Hz";
        check(ParamMap::curveFor(hzZeroMin) == ParamMap::Curve::Linear,
              "Hz with min==0 stays linear (log needs a positive minimum)");
    }

    // ── 6. Integer steps quantise; fractional steps do not ──────────────────
    {
        PI mode = slider(0.0f, 0.0f, 4.0f, 1.0f);
        const float v = ParamMap::mapSlotToZone(0.45f, mode);
        checkNear(v, std::round(v), 1.0e-5f, "integer step lands exactly on a step");
        check(v >= 0.0f && v <= 4.0f, "quantised value stays in range");

        PI fine = slider(0.0f, 0.0f, 1.0f, 0.01f);
        check(! ParamMap::isDiscrete(fine),
              "fractional step is a resolution hint, not a quantisation demand");
    }

    // ── 7. Menu style is discrete regardless of step ────────────────────────
    {
        PI menu = slider(0.0f, 0.0f, 2.0f, 1.0f);
        menu.isMenu = true;
        check(ParamMap::isDiscrete(menu), "[style:menu] is discrete");
        const float v = ParamMap::mapSlotToZone(0.7f, menu);
        checkNear(v, std::round(v), 1.0e-5f, "menu value is an exact index");
    }

    // ── 8. Buttons and checkboxes threshold at 0.5 ──────────────────────────
    {
        PI btn {};
        btn.min = 0.0f; btn.max = 1.0f; btn.step = 1.0f;
        btn.kind = FaustEngine::Kind::Button;

        checkNear(ParamMap::mapSlotToZone(0.499f, btn), 0.0f, 1.0e-6f, "0.499 -> off");
        checkNear(ParamMap::mapSlotToZone(0.501f, btn), 1.0f, 1.0e-6f, "0.501 -> on");
        checkNear(ParamMap::mapSlotToZone(0.5f,   btn), 1.0f, 1.0e-6f, "exactly 0.5 -> on");

        PI chk = btn;
        chk.kind = FaustEngine::Kind::CheckButton;
        checkNear(ParamMap::mapZoneToSlot(1.0f, chk), 1.0f, 1.0e-6f, "checkbox on round-trips");
        checkNear(ParamMap::mapZoneToSlot(0.0f, chk), 0.0f, 1.0e-6f, "checkbox off round-trips");
    }

    // ── 9. Nothing escapes the declared range, ever ─────────────────────────
    {
        PI hz = slider(1000.0f, 20.0f, 20000.0f);
        hz.unit = "Hz";
        bool inRange = true;
        for (float n = -0.5f; n <= 1.5f; n += 0.017f)
        {
            const float v = ParamMap::mapSlotToZone(n, hz);
            if (! std::isfinite(v) || v < hz.min - 1.0e-3f || v > hz.max + 1.0e-3f)
                inRange = false;
        }
        check(inRange, "out-of-range slot inputs are clamped, never NaN/Inf");

        // Degenerate: min == max must not divide by zero.
        PI flat = slider(5.0f, 5.0f, 5.0f);
        check(std::isfinite(ParamMap::mapSlotToZone(0.5f, flat))
                  && std::isfinite(ParamMap::mapZoneToSlot(5.0f, flat)),
              "zero-width range does not produce NaN");
    }

    // ── 10. Monotonicity: a knob sweep must never reverse direction ─────────
    {
        PI hz = slider(1000.0f, 20.0f, 20000.0f);
        hz.unit = "Hz";
        bool monotonic = true;
        float prev = -1.0f;
        for (float n = 0.0f; n <= 1.0f; n += 0.01f)
        {
            const float v = ParamMap::mapSlotToZone(n, hz);
            if (v < prev)
                monotonic = false;
            prev = v;
        }
        check(monotonic, "cutoff increases monotonically across the full sweep");
    }

    std::printf("\n%s (%d failure%s)\n", failures == 0 ? "PASSED" : "FAILED",
                failures, failures == 1 ? "" : "s");
    return failures == 0 ? 0 : 1;
}
