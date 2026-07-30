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
int checks   = 0;

void check(bool condition, const char* what)
{
    ++checks;
    std::printf("  [%s] %s\n", condition ? " OK " : "FAIL", what);
    if (! condition)
        ++failures;
}

void checkNear(float actual, float expected, float tolerance, const char* what)
{
    ++checks;
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

    // ── PF-037: the readout ─────────────────────────────────────────────────
    // Every knob used to display its raw 0..1 SLOT. The headline case is the
    // one from the defect report: an 800 Hz cutoff read `0.04`.
    {
        std::printf("\nPF-037 — the readout shows real units\n");

        // THE DEFECT REPORT'S OWN NUMBER IS A CLUE, and it took a failing test
        // to notice. STATUS.md recorded "a cutoff of 800 Hz reads 0.04". On the
        // log curve 800 Hz sits at slot 0.534, not 0.04 -- 0.04 is the LINEAR
        // slot, (800-20)/(20000-20). So the patch the harness photographed
        // declared neither [unit:Hz] nor [scale:log], which is the common
        // generated case and the one curveFor() cannot help. Both are pinned
        // below, because the unitless one is what users will actually meet.
        PI bare = slider(800.0f, 20.0f, 20000.0f, 1.0f);   // no unit, no scale
        checkNear(ParamMap::mapZoneToSlot(800.0f, bare), 0.039f, 0.001f,
                  "an unitless 20..20k control really does put 800 at slot 0.04");
        check(ParamMap::formatZone(
                  ParamMap::mapSlotToZone(ParamMap::mapZoneToSlot(800.0f, bare), bare), bare)
                  == "800",
              "and it now reads \"800\" rather than \"0.04\"");

        PI cutoff = slider(800.0f, 20.0f, 20000.0f, 1.0f);
        cutoff.unit = "Hz";

        // With the unit declared, curveFor() picks Log, so the same 800 Hz sits
        // at slot 0.53 instead -- a different wrong number, equally unreadable.
        const float slot = ParamMap::mapZoneToSlot(800.0f, cutoff);
        checkNear(slot, 0.534f, 0.002f, "declaring Hz moves 800 to the log slot 0.53");

        const juce::String shown =
            ParamMap::formatZone(ParamMap::mapSlotToZone(slot, cutoff), cutoff);
        check(shown == "800 Hz", "800 Hz reads as \"800 Hz\", not a slot number");

        // A step of 1 means whole Hz; no decimals should survive.
        check(! shown.containsChar('.'), "an integer-step Hz control shows no decimals");

        PI gain = slider(0.0f, -24.0f, 6.0f, 0.1f);
        gain.unit = "dB";
        check(ParamMap::formatZone(-6.0f, gain) == "-6.0 dB",
              "a 0.1-step dB control keeps one decimal and its sign");

        // Every exact decimal step, because this is where float bites: 0.01f is
        // 0.009999999776 as a double, so -log10 is 2.0000000097 and a bare
        // ceil() yields THREE decimals. Caught end-to-end first ("0.500" for a
        // 0.01-step Level), pinned here so the epsilon cannot be tuned away.
        struct StepCase { float step; const char* shown; };
        const StepCase steps[] = {
            { 1.0f,    "0"     },
            { 0.1f,    "0.0"   },
            { 0.01f,   "0.00"  },
            { 0.001f,  "0.000" },
        };
        for (const auto& sc : steps)
        {
            PI p = slider(0.0f, 0.0f, 1.0f, sc.step);
            const auto got = ParamMap::formatZone(0.0f, p);
            char msg[128];
            std::snprintf(msg, sizeof msg, "step %.4f -> \"%s\" (got \"%s\")",
                          sc.step, sc.shown, got.toRawUTF8());
            check(got == sc.shown, msg);
        }

        // The common generated-patch case: no declared unit at all. A bare
        // number is still an enormous improvement over a slot index.
        // Precision comes from the RANGE, not the current value, so one knob
        // prints the same shape at both ends of its travel.
        PI q = slider(0.7f, 0.7f, 8.0f);
        check(ParamMap::formatZone(0.7f, q) == "0.70",
              "an unitless control shows a bare number, no suffix");
        check(ParamMap::formatZone(8.0f, q) == "8.00",
              "and the same control keeps that width at the other end");

        PI voices = slider(2.0f, 1.0f, 8.0f, 1.0f);
        check(ParamMap::formatZone(2.0f, voices) == "2",
              "a discrete count reads \"2\", not \"0.14\"");

        // Round trip: what the box prints must parse back to the same slot,
        // or typing nothing and pressing enter moves the knob.
        {
            bool roundTrips = true;
            for (float n = 0.0f; n <= 1.0f; n += 0.05f)
            {
                const float zone = ParamMap::mapSlotToZone(n, cutoff);
                const juce::String txt = ParamMap::formatZone(zone, cutoff);
                const float back = ParamMap::mapZoneToSlot(
                    ParamMap::parseZone(txt, cutoff), cutoff);
                // Tolerance is one integer Hz expressed in slot terms near the
                // bottom of a log curve, which is the coarsest point.
                if (std::fabs(back - n) > 0.02f)
                    roundTrips = false;
            }
            check(roundTrips, "formatZone -> parseZone returns the same slot across the sweep");
        }

        check(ParamMap::parseZone("800 Hz", cutoff) == 800.0f,
              "parseZone accepts the unit suffix it printed");
        check(ParamMap::parseZone("800", cutoff) == 800.0f,
              "parseZone accepts a bare number");
        checkNear(ParamMap::parseZone("", cutoff), 800.0f, 0.001f,
                  "empty text falls back to the default, not the minimum");
        checkNear(ParamMap::parseZone("banana", cutoff), 800.0f, 0.001f,
                  "unparseable text falls back to the default, not the minimum");
        check(ParamMap::parseZone("999999", cutoff) == 20000.0f,
              "an out-of-range entry clamps to max");

        PI toggle = slider(0.0f, 0.0f, 1.0f);
        toggle.kind = FaustEngine::Kind::CheckButton;
        check(ParamMap::formatZone(1.0f, toggle) == "On", "a checkbox reads On");
        check(ParamMap::formatZone(0.0f, toggle) == "Off", "a checkbox reads Off");
    }

    std::printf("\n%s (%d failure%s)\n", failures == 0 ? "PASSED" : "FAILED",
                failures, failures == 1 ? "" : "s");
    // One machine-readable line for tools/health_report.py. The `checks` total is
    // the point: every harness here already carried an exact `failures` int and
    // threw the denominator away, so "0 failures" was indistinguishable from
    // "ran nothing". Format is fixed and shared by all seven harnesses.
    std::printf("PF_SUMMARY harness=%s checks=%d failures=%d\n", "ParamMapTest", checks, failures);
    return failures == 0 ? 0 : 1;
}
