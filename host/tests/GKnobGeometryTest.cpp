// ---------------------------------------------------------------------------
// GKnobGeometryTest — GKnobGeometry.h's bipolar-arc math (ADR-038 F1).
//
// Regression test for the bug ADR-038 / session 020 name directly: a bipolar
// control (Detune, Pan — a real Faust range that straddles zero) filled its
// rotary arc from the arc's own start angle instead of a centre detent, so
// the centre value rendered as a partial fill rather than no fill at all.
// Covers the trap session 020's own plan calls out explicitly: "a perfectly
// symmetric range is not guaranteed ... the detent is -min/(max-min), never
// hard-coded 0.5" — every bipolar case below uses an asymmetric range.
//
// Header-only under test: one translation unit, no JUCE, no libfaust, no
// plugin sources — same convention ParamGridLayoutTest.cpp already
// established for ArchetypeLayout.h, and for the same reason: this header
// cannot live inside GeneratedFaceLookAndFeel.h itself (that class pulls in
// juce_gui_basics for every other method) and testing it only through a full
// JUCE build would mean no fast rung ever covers this arithmetic directly.
//
// Run: ./GKnobGeometryTest — exits 0 on success, 1 with a failure list.
// ---------------------------------------------------------------------------
#include "../Source/GKnobGeometry.h"

#include <cmath>
#include <iostream>
#include <string>

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

void expectNear(float actual, float expected, const std::string& what)
{
    ++checks;
    if (std::abs(actual - expected) > 1.0e-5f)
    {
        ++failures;
        std::cerr << "FAIL  " << what << "\n"
                  << "      expected: " << expected << "\n"
                  << "      actual:   " << actual << "\n";
    }
}

constexpr float kPi = 3.14159265358979323846f;

// The real call site's own rotary limits (ParamGridPanel.cpp's
// setRotaryParameters(pi*1.2f, pi*2.8f) — 7 o'clock to 5 o'clock).
constexpr float kStart = kPi * 1.2f;
constexpr float kEnd   = kPi * 2.8f;

// -min/(max-min) for an asymmetric Faust range, e.g. a drive-offset control
// spanning −0.5..+1.0 (session 020's own worked example): detent = 0.5/1.5.
constexpr float kAsymDetent = 1.0f / 3.0f;   // -(-0.5f) / (1.0f - -0.5f)

// A unipolar control's arc must start exactly where the rotary itself
// starts, for any pair of limits — this is the "every other control renders
// byte-identically to before" regression guard ADR-038 F1 requires.
void testUnipolarUnchanged()
{
    expectNear(GKnobGeometry::arcStartAngle(false, 0.0f, kStart, kEnd), kStart,
               "unipolar: arc starts at rotaryStartAngle (real call-site limits)");
    expectNear(GKnobGeometry::arcStartAngle(false, 0.0f, 0.0f, kPi), 0.0f,
               "unipolar: arc starts at rotaryStartAngle (0..pi)");
    expectNear(GKnobGeometry::arcStartAngle(false, 0.7f, -kPi, kPi), -kPi,
               "unipolar: a nonsense detent argument is ignored when hasDetent is false");
}

// A bipolar control's arc must start exactly at its DETENT angle, which is
// the geometric midpoint only for a symmetric range — an asymmetric range
// (session 020's own trap) must not silently collapse to 0.5.
void testBipolarStartsAtDetentNotMidpoint()
{
    expectNear(GKnobGeometry::arcStartAngle(true, 0.5f, kStart, kEnd), (kStart + kEnd) / 2.0f,
               "bipolar, symmetric range (detent 0.5): arc starts at the geometric midpoint");
    expectNear(GKnobGeometry::arcStartAngle(true, kAsymDetent, 0.0f, kPi),
               kAsymDetent * kPi,
               "bipolar, asymmetric range (-0.5..+1.0, detent 1/3): arc starts a THIRD "
               "of the way through the sweep, not at the midpoint");
    // The regression this guards: hard-coding 0.5 would have placed this at
    // pi/2, not detent*pi — assert the two disagree so a future edit that
    // reverts to a hard-coded midpoint is caught, not just "close enough".
    expectTrue(std::abs(GKnobGeometry::arcStartAngle(true, kAsymDetent, 0.0f, kPi) - (kPi / 2.0f)) > 0.01f,
               "bipolar, asymmetric range: the detent angle is NOT the geometric midpoint");
}

// GKnob's "v > 0.004" guard, unchanged for a unipolar control: no fill at
// sliderPos 0 or just above it; fill once clearly past the threshold.
void testUnipolarFillGuardUnchanged()
{
    expectTrue(! GKnobGeometry::arcHasFill(false, 0.0f, 0.0f), "unipolar: no fill at sliderPos 0");
    expectTrue(! GKnobGeometry::arcHasFill(false, 0.0f, 0.004f), "unipolar: no fill at the guard boundary");
    expectTrue(GKnobGeometry::arcHasFill(false, 0.0f, 0.01f), "unipolar: fill clearly past the guard");
    expectTrue(GKnobGeometry::arcHasFill(false, 0.0f, 1.0f), "unipolar: fill at sliderPos 1");
}

// The bug this fix closes, pinned directly: a bipolar control at its OWN
// detent (not sliderPos 0.5, and not sliderPos 0) must render no fill —
// before this fix, the guard measured distance from 0, so a centred bipolar
// control always drew a large, wrong partial arc.
void testBipolarNoFillAtItsOwnDetent()
{
    expectTrue(! GKnobGeometry::arcHasFill(true, kAsymDetent, kAsymDetent),
               "bipolar, asymmetric range: no fill exactly at its own detent (1/3, not 0.5)");
    expectTrue(! GKnobGeometry::arcHasFill(true, kAsymDetent, kAsymDetent + 0.001f),
               "bipolar: no fill just above its detent");
    expectTrue(! GKnobGeometry::arcHasFill(true, kAsymDetent, kAsymDetent - 0.001f),
               "bipolar: no fill just below its detent");
    expectTrue(GKnobGeometry::arcHasFill(true, kAsymDetent, kAsymDetent + 0.1f),
               "bipolar: fill clearly above its detent");
    expectTrue(GKnobGeometry::arcHasFill(true, kAsymDetent, 0.0f),
               "bipolar: fill at the minimum extreme, which is NOT this control's detent");
    expectTrue(GKnobGeometry::arcHasFill(true, kAsymDetent, 1.0f),
               "bipolar: fill at the maximum extreme");
    // The regression this guards: measuring from sliderPos 0 instead of the
    // detent would have called sliderPos==detent "far from zero" and drawn a
    // fill anyway. Assert the old, wrong criterion and the new one disagree.
    expectTrue(GKnobGeometry::arcHasFill(false, 0.0f, kAsymDetent),
               "sanity: a unipolar reading of the same sliderPos WOULD show fill -- "
               "the bipolar path is what suppresses it");
}

} // namespace

int main()
{
    std::cout << "GKnobGeometryTest — the bipolar-knob arc fix (ADR-038 F1)\n";

    testUnipolarUnchanged();
    testBipolarStartsAtDetentNotMidpoint();
    testUnipolarFillGuardUnchanged();
    testBipolarNoFillAtItsOwnDetent();

    std::cout << checks - failures << "/" << checks << " checks passed\n";

    if (failures != 0)
    {
        std::cerr << "\n" << failures << " FAILED\n";
        return 1;
    }

    std::cout << "PASS\n";
    return 0;
}
