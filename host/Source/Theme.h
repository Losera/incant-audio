#pragma once
#include <juce_graphics/juce_graphics.h>
#include <array>

// ── Theme ────────────────────────────────────────────────────────────────────
// Named colour and type tokens, replacing the 15 scattered inline-hex/bare-size
// call sites in PluginEditor.cpp, PromptPanel.cpp and CodeEditorPanel.cpp (B1).
// Header-only, matching the existing ParamGridLayout.h/ParamIdentity.h
// header-only convention in this directory.
//
// Colour names describe UI roles rather than a palette family, so a future
// palette change remains one file instead of a grep-and-replace across panels.
//
// `juce::Colour(uint32)` is `explicit` and not `constexpr` (verified against
// juce_Colour.h:57), but wraps nothing but a uint32 -- a plain `inline const`
// (an inline variable, C++17) is exactly what juce::Colours itself does for
// every named colour (juce_Colours.h). `juce::Font` is NOT safe the same way:
// it holds a `ReferenceCountedObjectPtr<SharedFontInternal>` (juce_Font.h:483)
// that caches a `Typeface`, and a global-scope Font's destructor runs at
// static-deinit time -- AFTER `ScopedJuceInitialiser_GUI`'s own destructor
// (a `main()`-local, torn down when `main()` returns, before file-scope
// statics) has already torn down JUCE's typeface cache. Reproduced directly:
// the first version of this file used `inline const juce::Font` tokens and
// EditorSessionTest immediately failed its leak check (Typeface/OwnedArray/
// CustomTypeface/Path all reported leaked). Every Type:: token below is
// therefore a zero-arg function returning a freshly-constructed Font, the
// same shape LookAndFeel's own getLabelFont()-style overrides use.
namespace Theme
{

// ── Colour ──────────────────────────────────────────────────────────────────
// Palette: EMBER CONSOLE (adopted 2026-08-24, per the "Five Faces of
// PluginForge" design review -- a hybrid of that deck's concept 04 Console
// Brutalism and concept 05 Cybersigilism: this file and ForgeLookAndFeel.h
// take Cybersigilism's colour/type; PluginEditor's paint()/resized() and
// ForgeLookAndFeel's drawXxx overrides take Brutalism's flat, hairline-ruled
// geometry. Replaced Tokyo Night (2026-08-11). The structural ROLE of each
// token below is unchanged -- only the hex values moved -- so this commit is
// a pure repaint and must produce an empty semantic layout diff.
//
// Why: pure black + a single warm ember accent is a deliberate departure from
// "conventional pro-audio blue" toward something more distinctive -- the
// stated goal of the design review that chose it. Ember also reads as one
// family with `progress`/`meterHot` (both warm), rather than pairing a cool
// accent against warm status colours the way Tokyo Night did.
//
// Contrast against `background`, measured with the WCAG relative-luminance
// formula (tools/*, not assumed) -- so nobody reaches for a token that cannot
// carry the text they are about to put in it:
//   textPrimary   #f5f0e6 on background  17.94:1  AAA
//   textSecondary #8a8378 on background   5.43:1  AA
//   accent        #ff4b1f on background   6.09:1  AA
//   progress      #ffb03d on background  11.19:1  AAA
//   danger        #ec3b52 on background   5.16:1  AA
//   meterHot      #ff8f4d on background   9.01:1  AAA
//   outline       #383838 on background   1.74:1  FAILS AA for body text. It
//                 is for disabled states and hairline outlines, never
//                 required text -- same role and same failing status the
//                 Tokyo Night outline token carried.
//
// ONE DELIBERATE DEVIATION from the design review's concept 05 table: it
// specifies danger as #e0263f, which computes to 4.39:1 -- below the 4.5
// line for body text, and PromptPanel.cpp's errorBox renders danger as real
// 12px body copy carrying compiler stderr, not decoration. The deck flags
// this itself ("worth a real re-check before shipping error copy at this
// weight"). Nudged to #ec3b52 -- same crimson, clears AA, still visibly
// distinct from `accent`.
inline const juce::Colour background    { 0xff050505 };  // window background
inline const juce::Colour surface       { 0xff0c0c0c };  // panel/code-editor background
inline const juce::Colour surfaceSunken { 0xff000000 };  // meter track and line-number wells
inline const juce::Colour surfaceRaised { 0xff131313 };  // cards and raised controls
inline const juce::Colour outline       { 0xff383838 };  // hairlines and disabled states
inline const juce::Colour textPrimary   { 0xfff5f0e6 };  // primary text
inline const juce::Colour textSecondary { 0xff8a8378 };  // panel headers and supporting text
inline const juce::Colour accent        { 0xffff4b1f };  // values, focus, and meter cool end
inline const juce::Colour progress      { 0xffffb03d };  // in-progress / working state
inline const juce::Colour danger        { 0xffec3b52 };  // error-region text
inline const juce::Colour meterHot      { 0xffff8f4d };  // clipping-proximity signal only

// ── Generated-plugin accent swatches (ADR-022 §3, T7) ──────────────────────
// A small, fixed set of alternative accents ParamGridPanel::derivePalette
// picks from, one per generated patch -- NOT new hues: the same Ember family
// as `accent` above, so a generated plugin's identity always reads as native
// to the shell around it rather than a mismatched sticker slapped on.
//
// ForgeLookAndFeel's highlightedText pairs Theme::background directly on TOP
// of whichever of these is picked (dark text on the fill), so contrast
// against background is what has to hold. Measured the same way as the
// ratios in the Colour section above (WCAG relative-luminance formula,
// background = #050505):
//   #ff4b1f ember   (== accent)   6.09:1
//   #ffb03d amber                11.19:1
//   #d9542b rust                  5.10:1
//   #ff7a45 coral                 7.88:1
// All four clear AA (3:1 for UI components, 4.5:1 for text) with margin.
//
// RESOLVED RISK: the design deck's fourth swatch was bone #f5f0e6, byte-
// identical to `textPrimary`. Rendered via tools/ui_iterate.sh
// (04_generator_grouped__rotary.png): on a bone-accented patch every knob
// fill and every label shared one colour -- white-on-black with no
// value/label separation at all, erasing the distinction ForgeLookAndFeel.h's
// "ONE ACCENT" reasoning exists to protect. Swapped for coral -- still
// heat-family, still visibly distinct from the other three and from
// `textPrimary`.
namespace GeneratedAccent
{
    inline const std::array<juce::Colour, 4> swatches
    {
        juce::Colour{ 0xffff4b1f },   // ember
        juce::Colour{ 0xffffb03d },   // amber
        juce::Colour{ 0xffd9542b },   // rust
        juce::Colour{ 0xffff7a45 },   // coral
    };
} // namespace GeneratedAccent

// ── Geometry ────────────────────────────────────────────────────────────────
// A deliberately small scale. New layout values should compose these tokens;
// one-off dimensions remain local when they express content rather than rhythm.
namespace Space
{
    inline constexpr int xs = 4;
    inline constexpr int sm = 8;
    inline constexpr int md = 12;
    inline constexpr int lg = 16;
    inline constexpr int xl = 24;
} // namespace Space

namespace Radius
{
    inline constexpr float xs = 3.0f;
    inline constexpr float sm = 6.0f;
    inline constexpr float md = 10.0f;
    inline constexpr float lg = 14.0f;
} // namespace Radius

namespace Stroke
{
    inline constexpr float hairline = 1.0f;
    inline constexpr float focus    = 1.5f;
    inline constexpr float track    = 3.5f;
    inline constexpr float rail     = 4.0f;
} // namespace Stroke

// ── Type scale ──────────────────────────────────────────────────────────────
// The actual visual problem today isn't the typeface -- it's that four of the
// five existing font call sites are the identical bare 12.0f (CodeEditorPanel.
// cpp:9,27, ParamGridPanel.cpp:132, PromptPanel.cpp:154) with no hierarchy at
// all; the fifth (PluginEditor.cpp:263, the "PluginForge" title) is 16.0f with
// nothing between it and the rest. A real scale, even a small one, is most of
// the fix.
namespace Type
{
    inline juce::Font caption()      { return juce::Font(11.0f); }                    // fine print
    inline juce::Font body()         { return juce::Font(12.0f); }                    // default UI text
    inline juce::Font label()        { return juce::Font(12.0f, juce::Font::bold); }  // knob captions
    inline juce::Font sectionTitle() { return juce::Font(11.0f, juce::Font::bold); }  // tracked in paint
    inline juce::Font heading()      { return juce::Font(14.0f, juce::Font::bold); }  // panel headers
    inline juce::Font title()        { return juce::Font(18.0f, juce::Font::bold); }  // shell title

    // Both existing monospaced call sites (CodeEditorPanel.cpp:27, PromptPanel.
    // cpp:154) already use exactly 12.0f -- one token, not a parameterised
    // factory, until a second size is ever actually needed.
    inline juce::Font mono()
    {
        return juce::Font(juce::Font::getDefaultMonospacedFontName(), 12.0f, juce::Font::plain);
    }
} // namespace Type

} // namespace Theme
