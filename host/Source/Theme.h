#pragma once
#include <juce_graphics/juce_graphics.h>

// ── Theme ────────────────────────────────────────────────────────────────────
// Named colour and type tokens, replacing the 15 scattered inline-hex/bare-size
// call sites in PluginEditor.cpp, PromptPanel.cpp and CodeEditorPanel.cpp (B1).
// Header-only, matching the existing ParamGridLayout.h/ParamIdentity.h
// header-only convention in this directory.
//
// The palette below is Catppuccin Mocha -- already what every one of those call
// sites was hand-typing as a bare hex literal; this just gives the same colours
// names instead of re-deriving them at each site, so a future palette change is
// one file, not a grep-and-replace across three.
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
inline const juce::Colour base      { 0xff1e1e2e };  // window background
inline const juce::Colour mantle    { 0xff181825 };  // panel/code-editor background
inline const juce::Colour crust     { 0xff11111b };  // recessed wells (meter track, line numbers)
inline const juce::Colour text      { 0xffcdd6f4 };  // primary text
inline const juce::Colour subtext   { 0xff9399b2 };  // secondary text (panel headers)
inline const juce::Colour overlay   { 0xff6c7086 };  // tertiary/dim text (line numbers)
inline const juce::Colour yellow    { 0xfff9e2af };  // in-progress / working state
inline const juce::Colour errorText { 0xfff2c9d3 };  // error region text
inline const juce::Colour meterCool { 0xff94e2d5 };  // teal -- low end of the level meter
inline const juce::Colour meterHot  { 0xfff38ba8 };  // red -- hot end of the level meter

// ── Type scale ──────────────────────────────────────────────────────────────
// The actual visual problem today isn't the typeface -- it's that four of the
// five existing font call sites are the identical bare 12.0f (CodeEditorPanel.
// cpp:9,27, ParamGridPanel.cpp:132, PromptPanel.cpp:154) with no hierarchy at
// all; the fifth (PluginEditor.cpp:263, the "PluginForge" title) is 16.0f with
// nothing between it and the rest. A real scale, even a small one, is most of
// the fix.
namespace Type
{
    inline juce::Font caption() { return juce::Font(11.0f); }                        // fine print
    inline juce::Font body()    { return juce::Font(12.0f); }                        // default UI text
    inline juce::Font label()   { return juce::Font(12.0f, juce::Font::bold); }      // knob captions
    inline juce::Font heading() { return juce::Font(14.0f, juce::Font::bold); }      // panel headers
    inline juce::Font title()   { return juce::Font(18.0f, juce::Font::bold); }      // "PluginForge"

    // Both existing monospaced call sites (CodeEditorPanel.cpp:27, PromptPanel.
    // cpp:154) already use exactly 12.0f -- one token, not a parameterised
    // factory, until a second size is ever actually needed.
    inline juce::Font mono()
    {
        return juce::Font(juce::Font::getDefaultMonospacedFontName(), 12.0f, juce::Font::plain);
    }
} // namespace Type

} // namespace Theme
