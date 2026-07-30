#pragma once
#include "FaustEngine.h"
#include <cmath>
#include <algorithm>

// ---------------------------------------------------------------------------
// ParamMap — the single conversion between a host-facing macro slot (always
// 0..1, per the ADR-settled 64-slot pattern) and a Faust zone's real-world
// value (Hz, dB, ms, a menu index, ...).
//
// WHY THIS EXISTS: before this, ParamPool::pushToFaust pushed the slot's raw
// 0..1 straight into the zone, so a knob at midpoint set a 20..20000 Hz cutoff
// to 0.5 Hz. Meanwhile PluginEditor::refreshParamKnobs normalised patch
// defaults back into slots with its own inline LINEAR formula. Two halves of
// one conversion, implemented once each, disagreeing. They are now the two
// directions of the same pair here, and neither side may reimplement it.
//
// Curve semantics are deliberately taken from Faust's own ValueConverter.h
// (LinearValueConverter / LogValueConverter / ExpValueConverter, as used by
// APIUI with a 0..1 UI range -- exactly our slot range), so a patch author who
// writes [scale:log] gets what every other Faust host would give them:
//   log:  linear interpolation in log space  => zone = min * (max/min)^n
//   exp:  linear interpolation in exp space  => zone = log(e^min + n*(e^max - e^min))
//
// One deliberate divergence, documented: Faust clamps a non-positive log
// minimum to DBL_EPSILON, which for min=0,max=20000 spreads the knob over ~16
// decades and renders it unusable. We fall back to linear instead. A knob that
// works beats bit-fidelity to a degenerate case.
// ---------------------------------------------------------------------------
namespace ParamMap
{

enum class Curve { Linear, Log, Exp };

// Faust reports [style:menu{...}] as a discrete chooser; its values are indices
// and must land exactly on a step, never between two entries.
inline bool isDiscrete(const FaustEngine::ParamInfo& p)
{
    if (p.kind == FaustEngine::Kind::Button || p.kind == FaustEngine::Kind::CheckButton)
        return true;
    if (p.isMenu)
        return true;
    // An integer step of 1 or more expresses "whole units only" (mode indices,
    // voice counts, integer Hz). A fractional step is a resolution hint, not a
    // quantisation demand, so it is left alone.
    return p.step >= 1.0f && std::fabs(p.step - std::floor(p.step)) < 1.0e-6f;
}

inline Curve curveFor(const FaustEngine::ParamInfo& p)
{
    const bool positiveRange = p.min > 0.0f && p.max > p.min;

    // An explicit [scale:...] wins, when the range can support it.
    if (p.scale == FaustEngine::Scale::Log)
        return positiveRange ? Curve::Log : Curve::Linear;
    if (p.scale == FaustEngine::Scale::Exp)
        return Curve::Exp;

    // Default frequency controls to log even without [scale:log]. Pitch is
    // perceived logarithmically, and generated patches rarely declare the tag:
    // a linear 20..20000 Hz knob spends 90% of its travel above 2 kHz, which
    // reads as broken to anyone who has used a filter.
    // dB is deliberately NOT included -- decibels are already a log scale, so
    // mapping them logarithmically again would double-apply it.
    if (p.unit == "Hz" && positiveRange)
        return Curve::Log;

    return Curve::Linear;
}

namespace detail
{
    // Faust clamps exp() to DBL_MAX; for a range like 0..20000 the span is
    // still non-finite in float. Any curve that degenerates falls back to
    // linear rather than emitting inf/NaN into a zone the audio thread reads.
    inline bool finiteSpan(double a, double b)
    {
        return std::isfinite(a) && std::isfinite(b) && std::fabs(b - a) > 1.0e-30;
    }
}

// Slot (0..1, host-facing) -> Faust zone value (real units).
inline float mapSlotToZone(float norm01, const FaustEngine::ParamInfo& p)
{
    const float n = std::clamp(norm01, 0.0f, 1.0f);

    // Momentary/latching controls are on or off; 0.5 is the only sensible
    // threshold for a continuous host automation lane driving a boolean.
    if (p.kind == FaustEngine::Kind::Button || p.kind == FaustEngine::Kind::CheckButton)
        return n >= 0.5f ? 1.0f : 0.0f;

    if (! (p.max > p.min))
        return p.min;

    double value;
    switch (curveFor(p))
    {
        case Curve::Log:
        {
            const double lmin = std::log(static_cast<double>(p.min));
            const double lmax = std::log(static_cast<double>(p.max));
            value = detail::finiteSpan(lmin, lmax)
                        ? std::exp(lmin + n * (lmax - lmin))
                        : p.min + n * (p.max - p.min);
            break;
        }
        case Curve::Exp:
        {
            const double emin = std::exp(static_cast<double>(p.min));
            const double emax = std::exp(static_cast<double>(p.max));
            value = detail::finiteSpan(emin, emax)
                        ? std::log(emin + n * (emax - emin))
                        : p.min + n * (p.max - p.min);
            break;
        }
        case Curve::Linear:
        default:
            value = p.min + n * (p.max - p.min);
            break;
    }

    if (isDiscrete(p) && p.step > 0.0f)
        value = p.min + std::round((value - p.min) / p.step) * p.step;

    // Guard the audio thread: a zone is dereferenced and multiplied every
    // sample, so a non-finite value here becomes silence-or-worse downstream.
    if (! std::isfinite(value))
        value = p.defaultValue;

    return std::clamp(static_cast<float>(value), p.min, p.max);
}

// Faust zone value (real units) -> slot (0..1). Exact inverse of the above,
// used to seed a slot from the patch's declared default.
inline float mapZoneToSlot(float zoneValue, const FaustEngine::ParamInfo& p)
{
    if (p.kind == FaustEngine::Kind::Button || p.kind == FaustEngine::Kind::CheckButton)
        return zoneValue >= 0.5f ? 1.0f : 0.0f;

    if (! (p.max > p.min))
        return 0.0f;

    const double v = std::clamp(static_cast<double>(zoneValue),
                                static_cast<double>(p.min),
                                static_cast<double>(p.max));
    double n;

    switch (curveFor(p))
    {
        case Curve::Log:
        {
            const double lmin = std::log(static_cast<double>(p.min));
            const double lmax = std::log(static_cast<double>(p.max));
            n = detail::finiteSpan(lmin, lmax)
                    ? (std::log(v) - lmin) / (lmax - lmin)
                    : (v - p.min) / (p.max - p.min);
            break;
        }
        case Curve::Exp:
        {
            const double emin = std::exp(static_cast<double>(p.min));
            const double emax = std::exp(static_cast<double>(p.max));
            n = detail::finiteSpan(emin, emax)
                    ? (std::exp(v) - emin) / (emax - emin)
                    : (v - p.min) / (p.max - p.min);
            break;
        }
        case Curve::Linear:
        default:
            n = (v - p.min) / (p.max - p.min);
            break;
    }

    if (! std::isfinite(n))
        n = 0.0f;

    return std::clamp(static_cast<float>(n), 0.0f, 1.0f);
}

// ---------------------------------------------------------------------------
// Presentation — turning a zone value into text a human can read, and back.
//
// PF-037: every knob used to display its raw 0..1 SLOT number, so an 800 Hz
// cutoff read `0.04` and a voice count of 2 read `0.14`. The DSP was always
// correct -- mapSlotToZone above is what feeds it -- and nothing denormalised
// for the eye. The readout was the only part of the parameter path still
// showing the internal representation.
//
// These live here, next to the conversion, because the unit string is already
// load-bearing in this file: curveFor() reads p.unit to decide that "Hz" means
// logarithmic. Precision is the same kind of question about the same metadata,
// and splitting it into the UI layer is how mapSlotToZone and the editor's
// inline formula came to disagree in the first place (see the header comment).
//
// FORMAT: plain, in the unit Faust declared -- `800 Hz`, `-6.0 dB`, `250 ms`.
// Deliberately no kHz auto-scaling: the text box has to parse whatever it
// prints, and rounding `12000 Hz` to `12.0 kHz` makes typing a value and
// reading it back shift it. A slightly clumsy `12000 Hz` round-trips exactly.
// ---------------------------------------------------------------------------
namespace detail
{
    // How many decimals this parameter deserves. The declared step is the
    // author's own statement of resolution, so it wins when it is usable; a
    // step of 0 (or a nonsense one) falls back to the RANGE's span.
    //
    // Span, not the current value: a control whose digit count changes as you
    // turn it (`0.700` at one end, `8.00` at the other) reads as a glitch. The
    // width is a property of the parameter, so it is computed from the
    // parameter, and every position of one knob prints the same shape.
    inline int decimalsFor(const FaustEngine::ParamInfo& p)
    {
        if (isDiscrete(p) && p.step >= 1.0f)
            return 0;

        if (p.step > 0.0f && std::isfinite(p.step))
        {
            // step 1 -> 0dp, 0.1 -> 1dp, 0.01 -> 2dp. Capped: a step of 1e-6 is
            // a resolution hint, not a demand for six digits in a 64-cell grid.
            //
            // The epsilon is not decoration. 0.01f is 0.009999999776 as a
            // double, so -log10 is 2.0000000097 and a bare ceil() returns 3 --
            // which showed up as a 0.01-step control rendering "0.500". Backing
            // off by a hair before rounding up is what makes an exact decimal
            // step give the digit count it looks like it should.
            const double d = -std::log10(static_cast<double>(p.step)) - 1.0e-6;
            return std::clamp(static_cast<int>(std::ceil(d)), 0, 4);
        }

        const double span = static_cast<double>(p.max) - static_cast<double>(p.min);
        if (! std::isfinite(span) || span <= 0.0) return 2;
        if (span >= 100.0) return 0;
        if (span >= 10.0)  return 1;
        if (span >= 1.0)   return 2;
        return 3;
    }
}

// Zone value (real units) -> display text.
inline juce::String formatZone(float zoneValue, const FaustEngine::ParamInfo& p)
{
    // Toggles render as a ToggleButton and never reach a text box, but a total
    // function is cheaper to reason about than a precondition.
    if (p.kind == FaustEngine::Kind::Button || p.kind == FaustEngine::Kind::CheckButton)
        return zoneValue >= 0.5f ? "On" : "Off";

    const double v = static_cast<double>(zoneValue);
    if (! std::isfinite(v))
        return "-";

    juce::String text(v, detail::decimalsFor(p));

    // A [style:menu{'A':0;'B':1}] param can only show its INDEX: ParamCapture
    // records that the style is a menu (FaustEngine.cpp, pendingIsMenu) and
    // discards the entry names, so there is nothing here to print instead.
    // Showing `2` beats showing `0.14`; showing `Sawtooth` needs the capture to
    // keep the names first.
    if (p.unit.empty())
        return text;

    return text + " " + juce::String(p.unit);
}

// Display text -> zone value (real units). Inverse of formatZone for anything
// formatZone can produce, and forgiving of what a user actually types: a bare
// number, a number with the unit appended, or with stray spaces.
inline float parseZone(const juce::String& text, const FaustEngine::ParamInfo& p)
{
    if (p.kind == FaustEngine::Kind::Button || p.kind == FaustEngine::Kind::CheckButton)
    {
        const auto t = text.trim().toLowerCase();
        return (t == "on" || t == "1" || t == "true") ? 1.0f : 0.0f;
    }

    // getDoubleValue stops at the first character it cannot use, so a trailing
    // unit needs no stripping -- "800 Hz" parses as 800. Leading junk does not
    // parse, which is the correct answer for genuinely unintelligible input:
    // fall back to the default rather than jumping the knob to its minimum.
    const juce::String t = text.trim();
    if (t.isEmpty())
        return p.defaultValue;

    const bool looksNumeric = t.containsAnyOf("0123456789");
    if (! looksNumeric)
        return p.defaultValue;

    const double v = t.getDoubleValue();
    if (! std::isfinite(v))
        return p.defaultValue;

    return std::clamp(static_cast<float>(v), p.min, p.max);
}

} // namespace ParamMap
