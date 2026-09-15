#pragma once
#include <cmath>

// ── GKnobGeometry ────────────────────────────────────────────────────────────
// ADR-038 F1 (docs/sessions/020-generated-faces-v2.md "F1 — knob geometry
// parity + bipolar rendering"). The bipolar-knob arc-start math, factored out
// of GeneratedFaceLookAndFeel::drawRotarySlider as free functions so it
// carries NO JUCE dependency and CAN be exercised without a live editor, a
// message thread, or a compiled DSP -- the same reason ParamGridLayout.h and
// ArchetypeLayout.h are header-only free functions rather than methods on a
// JUCE-dependent class (that header's own comment: "factored out ... so it
// carries NO JUCE dependency"). GeneratedFaceLookAndFeel.h itself cannot be
// this: it subclasses juce::LookAndFeel_V4 and pulls in juce_gui_basics for
// every other method, so anything left INSIDE that class is only reachable
// through a full JUCE build, never the two-second no-JUCE rung
// ParamGridLayoutTest already runs at.
//
// GeneratedFaceLookAndFeel is the only production caller; keep the two in
// sync by construction (it includes this).
namespace GKnobGeometry
{

// A normal control's value arc fills from the rotary's own start angle
// (rotaryStartAngle) toward the current value. A BIPOLAR control -- one
// whose real Faust range straddles zero, e.g. Detune or Pan -- should fill
// from a CENTRE detent instead: the angle where the control's actual musical
// zero sits, NOT the geometric midpoint of the arc. A perfectly symmetric
// range is not guaranteed (session 020's own trap: "Iron Strip EQ is ±12 dB,
// but a drive-offset might be −0.5..+1.0"), so `detent` is the caller-supplied
// normalised position of real zero -- `-min/(max-min)` on the control's real
// Faust range -- never hard-coded 0.5. `hasDetent` false (a unipolar control)
// returns rotaryStartAngle unchanged.
inline float arcStartAngle(bool hasDetent, float detent,
                           float rotaryStartAngle, float rotaryEndAngle)
{
    return hasDetent ? rotaryStartAngle + detent * (rotaryEndAngle - rotaryStartAngle)
                      : rotaryStartAngle;
}

// Whether a value at sliderPos (0..1) is far enough from this control's own
// "zero" to draw any fill at all. GKnob.dc.html's own guard ("v > 0.004"): a
// near-zero arc renders as visible noise at the rounded cap, not as "no
// value yet" -- generalised here from "distance from 0" to "distance from
// wherever this control's centre actually sits": 0 for a unipolar control,
// `detent` (sliderPos units) for a bipolar one, so a bipolar control exactly
// at its detent renders no fill, the same way a unipolar control at its
// minimum does.
inline bool arcHasFill(bool hasDetent, float detent, float sliderPos)
{
    return std::abs(sliderPos - (hasDetent ? detent : 0.0f)) > 0.004f;
}

} // namespace GKnobGeometry
