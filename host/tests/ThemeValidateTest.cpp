// ---------------------------------------------------------------------------
// ThemeValidateTest — ThemeValidate::validate() (host/Source/ThemeValidate.h),
// the contrast gate for a generated-plugin face (ADR-035 Step 2,
// docs/design/incant-ui/GENERATION_PLAN.md "Gap 1", session 018 §A1).
//
// A UiIr::Theme reaches the renderer from three untrusted places — a
// hand-authored IR, the post-compile `ui_face` LLM call, and a stale cached
// state blob — so the host validates it in C++ before building a LookAndFeel.
// A failing colour field is replaced by its Ember Console default; the rest of
// the Theme is kept (the per-token policy the GeneratedAccent bone `#f5f0e6`
// incident argued for — see Theme.h's "RESOLVED RISK" note).
//
// Pure arithmetic on parsed RGB — ThemeValidate.h pulls in only UiIr.h. Same
// "pure and cheap" shape as UiIrTest; links juce_data_structures for the
// UiIr::Theme type alone.
//
// Covers:
//   - the WCAG formula: relativeLuminance / contrastRatio pinned to known
//     values (black/white 21:1, mid-grey on white 4.48:1)
//   - parseColour: #rgb, #rgba, #rrggbb, #rrggbbaa, rgb(), rgba(), and reject
//   - Ember defaults pass untouched; their ratios vs `surface` #0c0c0c (NOT
//     vs #050505 — B1, see ThemeValidate.h header)
//   - the four bundle faces (Velvet Drift / Iron Strip / Echo Plate / Dustfield)
//     all pass
//   - the historical bone `#f5f0e6` accent fails and is replaced
//   - per-token isolation: one bad field substituted, the others kept verbatim
//   - reference substitution: an unparseable `surface` falls back and the rest
//     is measured against Ember's surface
//   - alpha is dropped, not composited (rgba dim ink passes)
//   - separation: a non-default accentAlt equal to accent is replaced; a
//     defaulted accentAlt next to a same-family accent is left alone
//   - idempotence: validate(validate(x).theme) substitutes nothing
//
// Run: ./ThemeValidateTest  — exits 0 on success, 1 with a failure list.
// ---------------------------------------------------------------------------
#include "../Source/ThemeValidate.h"

#include <cmath>
#include <iostream>
#include <string>

namespace
{
int failures = 0;
int checks   = 0;

void check(bool ok, const std::string& what)
{
    ++checks;
    if (! ok)
    {
        ++failures;
        std::cerr << "FAIL  " << what << "\n";
    }
}

void checkEq(const std::string& actual, const std::string& expected, const std::string& what)
{
    ++checks;
    if (actual != expected)
    {
        ++failures;
        std::cerr << "FAIL  " << what << "\n"
                  << "      expected: \"" << expected << "\"\n"
                  << "      actual:   \"" << actual << "\"\n";
    }
}

void checkNear(double actual, double expected, double tol, const std::string& what)
{
    ++checks;
    if (std::fabs(actual - expected) > tol)
    {
        ++failures;
        std::cerr << "FAIL  " << what << "\n"
                  << "      expected: " << expected << " +/- " << tol << "\n"
                  << "      actual:   " << actual << "\n";
    }
}

using ThemeValidate::Rgb;

// A Theme whose every field is a valid, passing, NON-Ember colour — a clean
// base to perturb one field at a time. Echo Plate's palette (all fields clear
// their thresholds against its own surface).
UiIr::Theme cleanTheme()
{
    UiIr::Theme t;
    t.surface   = "#17140f";
    t.panel     = "#221d16";
    t.line      = "#2c2620";
    t.text      = "#efe6d4";
    t.textDim   = "#b9ae97";
    t.accent    = "#f0a63c";
    t.accentAlt = "#8fe3c1";   // non-default, distinct from accent + text
    return t;
}

} // namespace

int main()
{
    using namespace ThemeValidate;

    // ── WCAG formula pins ─────────────────────────────────────────────────────
    {
        const Rgb white = parseColour("#ffffff");
        const Rgb black = parseColour("#000000");
        const Rgb grey  = parseColour("#777777");

        check(white.valid && black.valid && grey.valid, "formula pins: colours parse");
        checkNear(relativeLuminance(white), 1.0, 1e-9, "luminance(white) == 1");
        checkNear(relativeLuminance(black), 0.0, 1e-9, "luminance(black) == 0");
        checkNear(contrastRatio(black, white), 21.0, 1e-6, "contrast(black, white) == 21:1");
        checkNear(contrastRatio(white, black), 21.0, 1e-6, "contrast is symmetric");
        checkNear(contrastRatio(grey, white), 4.478, 0.01, "contrast(#777, white) ~ 4.48:1");
        checkNear(contrastRatio(grey, black), 4.689, 0.01, "contrast(#777, black) ~ 4.69:1");
    }

    // ── parseColour ──────────────────────────────────────────────────────────
    {
        const Rgb a = parseColour("#ff4b1f");
        check(a.valid, "#ff4b1f valid");
        checkNear(a.r, 1.0, 1e-9, "#ff4b1f r");
        checkNear(a.g, 75.0 / 255.0, 1e-9, "#ff4b1f g");
        checkNear(a.b, 31.0 / 255.0, 1e-9, "#ff4b1f b");
        checkNear(a.a, 1.0, 1e-9, "#ff4b1f opaque");

        const Rgb shortHex = parseColour("#f80");
        check(shortHex.valid, "#f80 valid");
        checkNear(shortHex.r, 1.0, 1e-9, "#f80 r doubled to 0xff");
        checkNear(shortHex.g, 0x88 / 255.0, 1e-9, "#f80 g doubled to 0x88");

        const Rgb hex8 = parseColour("#0c0c0c80");
        check(hex8.valid, "#rrggbbaa valid");
        checkNear(hex8.a, 128.0 / 255.0, 1e-9, "#rrggbbaa alpha");

        const Rgb hex4 = parseColour("#abcd");
        check(hex4.valid && std::fabs(hex4.a - 0xdd / 255.0) < 1e-9, "#rgba valid, alpha nibble doubled");

        const Rgb fn  = parseColour("rgb(255, 75, 31)");
        check(fn.valid, "rgb() valid");
        checkNear(fn.r, 1.0, 1e-9, "rgb() r");
        checkNear(fn.g, 75.0 / 255.0, 1e-9, "rgb() g");

        const Rgb fna = parseColour("rgba(238, 242, 238, 0.45)");
        check(fna.valid, "rgba() valid");
        checkNear(fna.a, 0.45, 1e-9, "rgba() alpha preserved on the struct");

        const Rgb upper = parseColour("  #FF4B1F  ");
        check(upper.valid && std::fabs(upper.r - 1.0) < 1e-9, "case + whitespace tolerated");

        check(! parseColour("").valid,          "empty string rejected");
        check(! parseColour("nope").valid,      "bare word rejected");
        check(! parseColour("#12").valid,       "#12 (2 hex) rejected");
        check(! parseColour("#1234567").valid,  "#1234567 (7 hex) rejected");
        check(! parseColour("rgb(1, 2)").valid, "rgb() with 2 components rejected");
        check(! parseColour("#gggggg").valid,   "non-hex digits rejected");
    }

    // ── Ember defaults pass untouched ────────────────────────────────────────
    {
        const auto rep = validate(UiIr::Theme {});
        check(! rep.anySubstituted(), "Ember default Theme substitutes nothing");
        checkEq(rep.theme.text,      "#f5f0e6", "Ember text unchanged");
        checkEq(rep.theme.accent,    "#ff4b1f", "Ember accent unchanged");
        checkEq(rep.theme.accentAlt, "#ffb03d", "Ember accentAlt unchanged");

        // ratios measured against surface #0c0c0c (see ThemeValidate.h header:
        // Theme.h publishes these against #050505 as 17.94 / 5.43 / 6.09).
        const Rgb surf = parseColour("#0c0c0c");
        checkNear(contrastRatio(parseColour("#f5f0e6"), surf), 17.22, 0.05, "Ember text vs surface");
        checkNear(contrastRatio(parseColour("#8a8378"), surf),  5.21, 0.05, "Ember textDim vs surface");
        checkNear(contrastRatio(parseColour("#ff4b1f"), surf),  5.85, 0.05, "Ember accent vs surface");
        check(contrastRatio(parseColour("#f5f0e6"), surf) >= kTextMinRatio,    "Ember text clears 7:1");
        check(contrastRatio(parseColour("#8a8378"), surf) >= kTextDimMinRatio, "Ember textDim clears 4.5:1");
        check(contrastRatio(parseColour("#ff4b1f"), surf) >= kAccentMinRatio,  "Ember accent clears 3:1");
    }

    // ── the four bundle faces all pass ──────────────────────────────────────
    // docs/design/incant-ui/README.md §"Design tokens". textDim carries the
    // authored rgba() where the bundle specifies one — exercises alpha-drop.
    {
        auto face = [](const char* name, UiIr::Theme t)
        {
            const auto rep = validate(t);
            check(! rep.anySubstituted(),
                  std::string("face passes untouched: ") + name);
            if (rep.anySubstituted())
                for (const auto& f : rep.substituted)
                    std::cerr << "        substituted: " << f << "\n";
        };

        UiIr::Theme velvet;
        velvet.surface = "#0e0f13"; velvet.text = "#eef2ee";
        velvet.textDim = "rgba(238,242,238,0.45)"; velvet.accent = "#8fe3c1";
        // accentAlt left at the Ember default — Velvet Drift declares no alt.
        face("Velvet Drift", velvet);

        UiIr::Theme iron;
        iron.surface = "#d7d3c9"; iron.text = "#1c1b18";
        iron.textDim = "rgba(28,27,24,0.6)"; iron.accent = "#b4402f";
        iron.accentAlt = "#4f7a48";
        face("Iron Strip", iron);

        UiIr::Theme echo;
        echo.surface = "#17140f"; echo.text = "#efe6d4";
        echo.textDim = "rgba(239,230,212,0.4)"; echo.accent = "#f0a63c";
        // accentAlt left at the Ember default #ffb03d — 0.07 from `accent`, must
        // NOT be policed (Echo Plate declares no alt).
        face("Echo Plate", echo);

        UiIr::Theme dust;
        dust.surface = "#08080b"; dust.text = "#e9e7f2";
        dust.textDim = "rgba(233,231,242,0.5)"; dust.accent = "#a78bfa";
        dust.accentAlt = "#67e8f9";
        face("Dustfield", dust);
    }

    // ── the historical bone accent fails ───────────────────────────────────
    {
        UiIr::Theme t = cleanTheme();
        t.accent = "#f5f0e6";   // byte-identical to a light `text`
        t.text   = "#f5f0e6";
        const auto rep = validate(t);
        check(rep.didSubstitute("accent"), "bone accent (== text) is substituted");
        checkEq(rep.theme.accent, "#ff4b1f", "bone accent replaced with Ember accent");
        check(! rep.didSubstitute("surface"), "bone case: surface untouched");
        check(! rep.didSubstitute("panel"),   "bone case: panel untouched");
    }

    // ── per-token isolation ────────────────────────────────────────────────
    {
        UiIr::Theme t = cleanTheme();
        t.text = "#151210";   // near-black text on a dark surface: ~1.1:1
        const auto rep = validate(t);
        check(rep.didSubstitute("text"), "unreadable text is substituted");
        checkEq(rep.theme.text, "#f5f0e6", "text replaced with Ember token");
        check(rep.substituted.size() == 1, "exactly one field substituted");
        checkEq(rep.theme.surface,   "#17140f", "isolation: surface kept verbatim");
        checkEq(rep.theme.accent,    "#f0a63c", "isolation: accent kept verbatim");
        checkEq(rep.theme.accentAlt, "#8fe3c1", "isolation: accentAlt kept verbatim");
        checkEq(rep.theme.panel,     "#221d16", "isolation: panel kept verbatim");
    }

    // ── unparseable colour degrades per-token ──────────────────────────────
    {
        UiIr::Theme t = cleanTheme();
        t.textDim = "not-a-colour";
        const auto rep = validate(t);
        check(rep.didSubstitute("textDim"), "garbage textDim substituted");
        checkEq(rep.theme.textDim, "#8a8378", "garbage textDim -> Ember token");
        check(rep.substituted.size() == 1, "garbage textDim: nothing else touched");
    }

    // ── an unparseable surface falls back, rest measured against Ember ──────
    {
        UiIr::Theme t = cleanTheme();
        t.surface = "rgb(oops)";
        t.text    = "#f5f0e6";   // passes against Ember surface #0c0c0c
        t.accent  = "#ff4b1f";
        const auto rep = validate(t);
        check(rep.didSubstitute("surface"), "bad surface substituted");
        checkEq(rep.theme.surface, "#0c0c0c", "bad surface -> Ember surface");
        check(! rep.didSubstitute("text"),   "text still passes against Ember surface");
        check(! rep.didSubstitute("accent"), "accent still passes against Ember surface");
    }

    // ── alpha is dropped, not composited ──────────────────────────────────
    {
        // rgba(238,242,238,.45) composited over a dark surface is ~4.2:1 and
        // would fail 4.5:1; as authored (alpha dropped) it is ~17:1.
        UiIr::Theme t = cleanTheme();
        t.surface = "#0e0f13";
        t.textDim = "rgba(238,242,238,0.45)";
        const auto rep = validate(t);
        check(! rep.didSubstitute("textDim"), "low-alpha dim ink passes (not composited)");
    }

    // ── separation: a deliberate accentAlt == accent is replaced ───────────
    {
        UiIr::Theme t = cleanTheme();
        t.accent    = "#8fe3c1";
        t.accentAlt = "#8fe3c1";   // non-default, identical to accent
        const auto rep = validate(t);
        check(rep.didSubstitute("accentAlt"), "accentAlt == accent is substituted");
        checkEq(rep.theme.accentAlt, "#ffb03d", "colliding accentAlt -> Ember token");
        check(! rep.didSubstitute("accent"), "the accent itself is kept");
    }

    // ── separation: a DEFAULTED accentAlt beside a same-family accent is left ─
    {
        UiIr::Theme t = cleanTheme();
        t.accent    = "#f0a63c";   // amber, 0.07 from the default #ffb03d
        t.accentAlt = "#ffb03d";   // the Ember default -> inert, not policed
        const auto rep = validate(t);
        check(! rep.anySubstituted(), "defaulted accentAlt near accent: nothing substituted");
        checkEq(rep.theme.accent, "#f0a63c", "same-family accent survives a defaulted alt");
    }

    // ── idempotence ───────────────────────────────────────────────────────
    {
        auto idempotent = [](UiIr::Theme t, const char* name)
        {
            const auto once  = validate(t);
            const auto twice = validate(once.theme);
            check(! twice.anySubstituted(),
                  std::string("idempotent: ") + name);
        };
        UiIr::Theme allBad;
        allBad.surface = "???"; allBad.text = "#000"; allBad.textDim = "#010101";
        allBad.accent = "#f5f0e6"; allBad.accentAlt = "#f5f0e6";
        idempotent(allBad, "all fields bad");

        UiIr::Theme bone = cleanTheme();
        bone.accent = "#efe6d4"; bone.text = "#efe6d4";
        idempotent(bone, "bone accent");

        idempotent(UiIr::Theme {}, "Ember default");
    }

    std::cerr << checks << " checks, " << failures << " failures\n";
    return failures == 0 ? 0 : 1;
}
