#pragma once
#include "UiIr.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

// ── ThemeValidate — contrast validation for a generated-plugin face ──────────
// ADR-035 Step 2 (docs/design/incant-ui/GENERATION_PLAN.md "Gap 1", session
// 018 §A1). A UiIr::Theme may be hand-authored, produced by the post-compile
// `ui_face` LLM call, or restored from a stale cached state blob. Before a
// LookAndFeel is built from it (Step 3, GeneratedFaceLookAndFeel.h) its colours
// must be checked for legibility, and any field that fails is replaced with the
// Ember Console token for THAT ONE FIELD — never the whole Theme.
//
// Rejecting the whole face on one bad token is the mistake the GeneratedAccent
// bone `#f5f0e6` swatch already taught this codebase once (Theme.h's "RESOLVED
// RISK" note): a single unusable colour must degrade to a default, not discard
// a valid layout. `UiIr::parse()` already degrades per-token for enum fields;
// this is the colour half of the same policy, done in C++ so the host is safe
// against inputs `llm/ui_face.py` never produced.
//
// ── Reference colour: `surface`, not `background` ────────────────────────────
// Contrast is measured against the theme's own `surface` token. This is a
// deliberate choice (B1, decided 2026-09-03) between two candidates:
//
//   * Theme::background `#050505` — the host window ground. Theme.h:47-53's
//     contrast table measures the Ember SHELL tokens against this, because the
//     title band and prompt copy sit directly on the window.
//   * Theme::surface `#0c0c0c` — the panel fill. This is what `ParamGridPanel`
//     paints, and the generated-face LookAndFeel is scoped to that panel only
//     (GENERATION_PLAN.md "Gaps 2 + 3"). Its knobs and labels never touch the
//     window ground.
//
// `surface` is correct because WCAG contrast is defined against the actual
// paint background, and a generated face's actual background is its own
// `theme.surface` (Velvet Drift `#0e0f13`, Dustfield `#08080b`, …), which is
// exactly the token `UiIr::Theme` carries and `#050505` is not. It is also the
// stricter choice: `#0c0c0c` is lighter than `#050505`, so ratios come out
// slightly lower — a face that passes here passes against the window too, never
// the reverse, and the LLM-chosen path is the one that should carry the tighter
// bound. Consequently Theme.h:47-53's published ratios (measured vs background)
// are NOT the fixture values here; ThemeValidateTest.cpp recomputes the Ember
// defaults against `#0c0c0c` and pins those.
//
// ── Thresholds (GENERATION_PLAN.md "Gap 1", README §"Step 2") ────────────────
//   text     ≥ 7.0 : 1   on surface
//   textDim  ≥ 4.5 : 1   on surface
//   accent   ≥ 3.0 : 1   on surface
//   accent, accentAlt and text mutually separated by a colour distance
//     (crude ΔE proxy — see kMinSeparation).
// `panel` and `line` carry no threshold: nothing in scope renders required
// text on them (hairlines, card fills, disabled states). `accentAlt` gets the
// separation check ONLY when it was set to something other than the Ember
// default — a face that provides no distinct second signal (the "—" column in
// README §"Design tokens": Velvet Drift, Echo Plate) inherits the default
// `#ffb03d`, which is inert and must not drag a same-family `accent` (Echo
// Plate's `#f0a63c` sits 0.07 from it) into a substitution. A non-default alt
// is a deliberate second signal (Iron Strip peak zone, Dustfield playhead) and
// must be distinct. `accentAlt` has no on-surface threshold either way — the
// spec lists none and a dim second signal is allowed.
//
// ── Alpha ───────────────────────────────────────────────────────────────────
// rgba() colours are measured on their RGB channels; alpha is dropped, not
// composited over `surface`. LIMITATION, deliberate: compositing would fail
// Velvet Drift's `rgba(238,242,238,.45)` dim ink (~4.2:1 composited vs ~17:1
// as authored), and session 018 §A1's "Done when" requires all four bundle
// faces to pass. A face that sets a low alpha is trusted the way the design
// deck's own authored values are. A follow-up may revisit this if a producer
// abuses it (e.g. `text: "rgba(255,255,255,0.05)"` reads as pure white here).
//
// Header-only, matching Theme.h / ParamGridLayout.h / UiIr.h in this directory.
// No `juce::Component`, no `juce::Colour` — pure arithmetic on parsed RGB, so
// ThemeValidateTest links nothing a UiIr round-trip test does not.

namespace ThemeValidate
{

// ── Parsed colour ───────────────────────────────────────────────────────────
// Channels normalised to [0, 1]. `valid` is false when the source string was
// not a recognised CSS colour; callers substitute the Ember token in that case.
struct Rgb
{
    double r = 0.0;
    double g = 0.0;
    double b = 0.0;
    double a = 1.0;
    bool   valid = false;
};

namespace detail
{
    inline std::string lowerTrim(const std::string& in)
    {
        std::size_t begin = 0, end = in.size();
        while (begin < end && std::isspace(static_cast<unsigned char>(in[begin]))) ++begin;
        while (end > begin && std::isspace(static_cast<unsigned char>(in[end - 1]))) --end;
        std::string out = in.substr(begin, end - begin);
        std::transform(out.begin(), out.end(), out.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return out;
    }

    inline bool hexNibble(char c, int& out)
    {
        if (c >= '0' && c <= '9') { out = c - '0';      return true; }
        if (c >= 'a' && c <= 'f') { out = c - 'a' + 10; return true; }
        return false;
    }

    // Parse `count` hex characters starting at `s[pos]` into a byte-per-pair.
    inline bool parseHexBytes(const std::string& s, std::size_t pos, int count,
                              std::array<int, 4>& bytes, int& nBytes)
    {
        nBytes = count / 2;
        for (int i = 0; i < nBytes; ++i)
        {
            int hi = 0, lo = 0;
            if (! hexNibble(s[pos + static_cast<std::size_t>(i * 2)], hi)) return false;
            if (! hexNibble(s[pos + static_cast<std::size_t>(i * 2 + 1)], lo)) return false;
            bytes[static_cast<std::size_t>(i)] = hi * 16 + lo;
        }
        return true;
    }

    // Split "a,b,c" / "a b c" / "a,b,c,d" inside a "(...)" into up to 4 tokens.
    inline int splitComponents(const std::string& body, std::array<std::string, 4>& out)
    {
        int n = 0;
        std::string cur;
        for (char c : body)
        {
            if (c == ',' || c == ' ' || c == '\t' || c == '/')
            {
                if (! cur.empty() && n < 4) { out[static_cast<std::size_t>(n++)] = cur; cur.clear(); }
            }
            else
            {
                cur.push_back(c);
            }
        }
        if (! cur.empty() && n < 4) out[static_cast<std::size_t>(n++)] = cur;
        return n;
    }

    inline bool toDouble(const std::string& tok, double& out)
    {
        try
        {
            std::size_t used = 0;
            out = std::stod(tok, &used);
            return used == tok.size();
        }
        catch (...) { return false; }
    }
}

// Parse a CSS colour string: `#rgb`, `#rgba`, `#rrggbb`, `#rrggbbaa`,
// `rgb(r,g,b)` and `rgba(r,g,b,a)` with 0–255 channels and a 0–1 alpha.
// Anything else → `{ valid = false }`.
inline Rgb parseColour(const std::string& raw)
{
    const std::string s = detail::lowerTrim(raw);
    Rgb out;
    if (s.empty()) return out;

    if (s[0] == '#')
    {
        const std::size_t hexLen = s.size() - 1;
        std::array<int, 4> bytes { 0, 0, 0, 255 };
        int nBytes = 0;

        if (hexLen == 3 || hexLen == 4)
        {
            // #rgb / #rgba — each nibble doubled (0xF -> 0xFF).
            for (std::size_t i = 0; i < hexLen; ++i)
            {
                int nib = 0;
                if (! detail::hexNibble(s[1 + i], nib)) return out;
                bytes[i] = nib * 16 + nib;
            }
            nBytes = static_cast<int>(hexLen);
        }
        else if (hexLen == 6 || hexLen == 8)
        {
            if (! detail::parseHexBytes(s, 1, static_cast<int>(hexLen), bytes, nBytes))
                return out;
        }
        else
        {
            return out;
        }

        out.r = bytes[0] / 255.0;
        out.g = bytes[1] / 255.0;
        out.b = bytes[2] / 255.0;
        out.a = (nBytes == 4) ? bytes[3] / 255.0 : 1.0;
        out.valid = true;
        return out;
    }

    if (s.rfind("rgb", 0) == 0)
    {
        const auto open  = s.find('(');
        const auto close = s.rfind(')');
        if (open == std::string::npos || close == std::string::npos || close <= open)
            return out;

        std::array<std::string, 4> tok;
        const int n = detail::splitComponents(s.substr(open + 1, close - open - 1), tok);
        if (n != 3 && n != 4) return out;

        double ch[4] = { 0, 0, 0, 1.0 };
        for (int i = 0; i < n; ++i)
        {
            std::string t = tok[static_cast<std::size_t>(i)];
            const bool pct = (! t.empty() && t.back() == '%');
            if (pct) t.pop_back();
            double v = 0.0;
            if (! detail::toDouble(t, v)) return out;
            if (i < 3)
                ch[i] = pct ? v / 100.0 : v / 255.0;
            else
                ch[i] = pct ? v / 100.0 : v;   // alpha: 0–1 (or 0–100%)
        }

        out.r = std::clamp(ch[0], 0.0, 1.0);
        out.g = std::clamp(ch[1], 0.0, 1.0);
        out.b = std::clamp(ch[2], 0.0, 1.0);
        out.a = std::clamp(ch[3], 0.0, 1.0);
        out.valid = true;
        return out;
    }

    return out;
}

// WCAG 2.x relative luminance — https://www.w3.org/TR/WCAG21/#dfn-relative-luminance
// Alpha is ignored (see the header note); the caller passes an opaque colour.
inline double relativeLuminance(const Rgb& c)
{
    const auto lin = [](double v)
    {
        return (v <= 0.04045) ? v / 12.92
                              : std::pow((v + 0.055) / 1.055, 2.4);
    };
    return 0.2126 * lin(c.r) + 0.7152 * lin(c.g) + 0.0722 * lin(c.b);
}

// WCAG 2.x contrast ratio — https://www.w3.org/TR/WCAG21/#dfn-contrast-ratio
// Range [1, 21]; symmetric in its arguments.
inline double contrastRatio(const Rgb& x, const Rgb& y)
{
    const double lx = relativeLuminance(x);
    const double ly = relativeLuminance(y);
    const double hi = std::max(lx, ly);
    const double lo = std::min(lx, ly);
    return (hi + 0.05) / (lo + 0.05);
}

// A crude perceptual-distance proxy: Euclidean distance in sRGB space, range
// [0, sqrt(3)]. Not a real ΔE — no Lab conversion — but enough to catch the
// one failure mode the separation rule guards: an accent that is the same as,
// or barely distinct from, the text colour (the historical bone `#f5f0e6`
// accent, byte-identical to `textPrimary`). A follow-up wanting rigour would
// swap this for CIEDE2000 on Lab.
inline double colourDistance(const Rgb& x, const Rgb& y)
{
    const double dr = x.r - y.r;
    const double dg = x.g - y.g;
    const double db = x.b - y.b;
    return std::sqrt(dr * dr + dg * dg + db * db);
}

inline constexpr double kTextMinRatio    = 7.0;
inline constexpr double kTextDimMinRatio = 4.5;
inline constexpr double kAccentMinRatio  = 3.0;
// sRGB-distance floor for accent vs text and accent vs accentAlt. Ember's own
// defaults clear it comfortably (accent/text 1.01, accent/accentAlt 0.41,
// accentAlt/text 0.71), so substituting a colliding field to its Ember default
// always terminates. bone `#f5f0e6` vs textPrimary `#f5f0e6` scores 0.0.
inline constexpr double kMinSeparation   = 0.15;

// ── Result ──────────────────────────────────────────────────────────────────
struct Report
{
    UiIr::Theme theme;                       // every colour field now passes
    std::vector<std::string> substituted;    // field names that were replaced

    bool anySubstituted() const { return ! substituted.empty(); }
    bool didSubstitute(const std::string& field) const
    {
        return std::find(substituted.begin(), substituted.end(), field) != substituted.end();
    }
};

namespace detail
{
    inline void record(Report& r, const std::string& field)
    {
        if (std::find(r.substituted.begin(), r.substituted.end(), field) == r.substituted.end())
            r.substituted.push_back(field);
    }
}

// Validate `in` against the thresholds above. Returns a Report whose `.theme`
// is safe to build a LookAndFeel from: every failing colour field has been
// replaced by its Ember Console default (the member initialisers of a
// default-constructed UiIr::Theme), and `.substituted` names those fields.
inline Report validate(const UiIr::Theme& in)
{
    const UiIr::Theme ember {};   // member initialisers == the Ember Console tokens
    Report r;
    r.theme = in;

    // ── surface: the reference. Must parse; if not, it is itself substituted. ──
    Rgb surface = parseColour(r.theme.surface);
    if (! surface.valid)
    {
        r.theme.surface = ember.surface;
        detail::record(r, "surface");
        surface = parseColour(ember.surface);
    }

    // ── on-surface legibility ────────────────────────────────────────────────
    const auto enforceRatio = [&](std::string& field, const std::string& name,
                                  const std::string& emberValue, double minRatio)
    {
        const Rgb c = parseColour(field);
        if (! c.valid || contrastRatio(c, surface) < minRatio)
        {
            field = emberValue;
            detail::record(r, name);
        }
    };

    enforceRatio(r.theme.text,    "text",    ember.text,    kTextMinRatio);
    enforceRatio(r.theme.textDim, "textDim", ember.textDim, kTextDimMinRatio);
    enforceRatio(r.theme.accent,  "accent",  ember.accent,  kAccentMinRatio);

    // ── mutual separation ───────────────────────────────────────────────────
    // accent must always be distinct from text (the bone-swatch guard).
    // accentAlt is policed only when it is a deliberate, non-default signal.
    // accentAlt must also parse; an unparseable one is substituted here.
    if (! parseColour(r.theme.accentAlt).valid)
    {
        r.theme.accentAlt = ember.accentAlt;
        detail::record(r, "accentAlt");
    }
    const bool policeAlt = (r.theme.accentAlt != ember.accentAlt);

    // Two one-shot checks, each substituting toward the Ember token. Neither can
    // reopen the other: the accent guard only ever moves `accent` to
    // `ember.accent`, and the alt guard reads the post-guard `accent`. Once a
    // field is at its Ember default the result is accepted as-is — `ember.accent`
    // vs `ember.accentAlt` is 0.41 apart, and a `text` legible on `surface` is
    // far from the reddish `ember.accent` in every non-pathological case.

    // 1. accent must be distinct from text — the bone `#f5f0e6` guard.
    if (r.theme.accent != ember.accent
        && colourDistance(parseColour(r.theme.accent), parseColour(r.theme.text)) < kMinSeparation)
    {
        r.theme.accent = ember.accent;
        detail::record(r, "accent");
    }

    // 2. a deliberate (non-default) accentAlt must be distinct from accent and
    //    text. A defaulted accentAlt is inert — Echo Plate's amber `accent`
    //    sits 0.07 from the default `#ffb03d` and must not be disturbed by it.
    if (policeAlt && r.theme.accentAlt != ember.accentAlt)
    {
        const Rgb alt = parseColour(r.theme.accentAlt);
        if (colourDistance(alt, parseColour(r.theme.accent)) < kMinSeparation
            || colourDistance(alt, parseColour(r.theme.text)) < kMinSeparation)
        {
            r.theme.accentAlt = ember.accentAlt;
            detail::record(r, "accentAlt");
        }
    }

    return r;
}

} // namespace ThemeValidate
