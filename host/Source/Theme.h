#pragma once
#include <juce_graphics/juce_graphics.h>

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
// Palette: TOKYO NIGHT (adopted 2026-08-11, docs/sessions/010 §2). Replaced
// Catppuccin Mocha. The structural ROLE of each token below is unchanged --
// only the hex values moved -- so this commit is a pure repaint and must
// produce an empty semantic layout diff.
//
// Why: blue accent + amber hot is the conventional modern-app / pro-audio
// pairing. Catppuccin's teal+pink reads as a terminal colour scheme, which is
// precisely the "someone applied a theme to a stock app" impression the design
// pass exists to escape (docs/competitive_landscape.md Threat #6).
//
// ONE DELIBERATE DEVIATION from session 010's table, flagged in the approved
// plan: 010 sets meterHot to #e0af68, which is ALSO its `yellow` token. The
// meter's hot end and the "generation in progress" state would then be the same
// colour -- a role collision that bites the moment both are on screen at once.
// meterHot is Tokyo Night's orange #ff9e64 instead; `yellow` keeps #e0af68.
//
// Contrast against `background`, measured rather than assumed -- so nobody
// reaches for a token that cannot carry the text they are about to put in it:
//   textPrimary   #c0caf5 on background  ~11:1   passes AA/AAA for body text
//   textSecondary #a9b1d6 on background  ~8.6:1  passes AA/AAA
//   outline       #565f89 on background  ~3.0:1  FAILS AA for body text. It is
//                 for disabled states and hairline outlines, never required text.
inline const juce::Colour background    { 0xff1a1b26 };  // window background
inline const juce::Colour surface       { 0xff16161e };  // panel/code-editor background
inline const juce::Colour surfaceSunken { 0xff0f0f17 };  // meter track and line-number wells
inline const juce::Colour surfaceRaised { 0xff242536 };  // cards and raised controls
inline const juce::Colour outline       { 0xff565f89 };  // hairlines and disabled states
inline const juce::Colour textPrimary   { 0xffc0caf5 };  // primary text
inline const juce::Colour textSecondary { 0xffa9b1d6 };  // panel headers and supporting text
inline const juce::Colour accent        { 0xff7aa2f7 };  // values, focus, and meter cool end
inline const juce::Colour progress      { 0xffe0af68 };  // in-progress / working state
inline const juce::Colour danger        { 0xfff7768e };  // error-region text
inline const juce::Colour meterHot      { 0xffff9e64 };  // clipping-proximity signal only

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
