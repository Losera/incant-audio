#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "Theme.h"

// ── ForgeLookAndFeel ─────────────────────────────────────────────────────────
// LookAndFeel_V4 subclass giving generated plugins a styled, non-default look
// (session 002, Part B, item B2: docs/sessions/002-refine-loop-and-ui-redesign.md
// lines 355-380). Header-only, matching Theme.h/ParamGridLayout.h's convention in
// this directory -- PluginEditor.cpp already sits in seven target_sources lists
// in host/CMakeLists.txt, so a .cpp companion here means seven edit sites and
// link-error risk for no behavioural gain.
//
// APPROACH TAKEN: colour-scheme-first, per docs/sessions/002-handoff-README.md's
// stated (and until this file, unverified) hypothesis that
// LookAndFeel_V4::drawRotarySlider et al. are already flat, findColour()-driven
// implementations, so a Theme-built ColourScheme may solve most of "looks
// unstyled" before any bespoke drawXxx is written. Verified by reading every
// candidate virtual's real JUCE 7.0.9 implementation (not recalled), not by
// assuming the hypothesis holds:
//   - drawRotarySlider / drawLinearSlider / getSliderThumbRadius
//     (juce_LookAndFeel_V4.cpp:1064-1112, 963-1055, 957-961): flat arcs/tracks,
//     every colour from findColour(), no bevel or gradient. ColourScheme alone
//     covers these.
//   - drawButtonBackground / drawToggleButton / drawTickBox
//     (juce_LookAndFeel_V4.cpp:281-372): flat rounded-rect fill + rounded tick
//     box, colour-driven throughout. ColourScheme alone covers these too.
//   - fillTextEditorBackground / drawTextEditorOutline
//     (juce_LookAndFeel_V4.cpp:676-707, delegating to
//     juce_LookAndFeel_V2.cpp:1164-1167 for the non-AlertWindow case): a flat
//     fill and a 1-2px flat-colour rect outline. ColourScheme alone covers
//     these. (This is also the fix for PromptPanel's promptInput TextEditor,
//     which sets no colours of its own -- see PromptPanel.h/.cpp -- and was
//     rendering in stock JUCE white-on-black before this file existed; errorBox
//     already sets its own colours explicitly and is unaffected either way.)
//   - drawScrollbar / getDefaultScrollbarWidth (juce_LookAndFeel_V4.cpp:629-646):
//     already a flat 8px rounded thumb with no track paint (ScrollBar::
//     backgroundColourId defaults to transparent, juce_LookAndFeel_V4.cpp:1360).
//     ColourScheme alone covers this.
//   - createSliderButton (juce_LookAndFeel_V2.cpp:1589-1592): returns a plain
//     TextButton("+"/"-", {}), added as a child of the Slider
//     (juce_Slider.cpp:611-612) with no LookAndFeel of its own -- it inherits
//     PluginForgeEditor's via the normal Component::getLookAndFeel()
//     parent-chain lookup the moment setLookAndFeel(&lnf) is called on the
//     editor. Already covered, and already picks up the getTextButtonFont
//     override below.
//
// Two virtuals ARE overridden below, both purely typographic, because
// ColourScheme has no font opinion and each has a real, cited gap:
//   - getTextButtonFont: LookAndFeel_V4's own default
//     (juce_LookAndFeel_V4.cpp:276-279) is jmin(16, buttonHeight*0.6) in
//     whatever the platform default typeface is -- nowhere on Theme::Type's
//     scale.
//   - createSliderTextBox: LookAndFeel_V2's default (juce_LookAndFeel_V2.cpp:
//     1607-1629) wires every colour through findColour() (already correct via
//     ColourScheme) but never touches the Label's font, and none of
//     ParamGridPanel.cpp's setTextBoxStyle() call sites (:228,246,257,261,265,
//     271) set one either -- it would render in the JUCE default Label font
//     otherwise, the one remaining "obviously a stock JUCE app" tell.
//
// Two virtuals from the session-002 candidate list are DELIBERATELY left
// un-overridden -- not just untried, but a considered decision that overriding
// them would be a regression:
//   - getLabelFont: LookAndFeel_V2's default (juce_LookAndFeel_V2.cpp:
//     1300-1303) returns label.getFont() -- i.e. whatever the label's own
//     setFont() call already chose. Every Label in this app already calls
//     Theme::Type::*() explicitly at construction, B1's own type-scale work
//     (CodeEditorPanel.cpp:10,24, KeyboardPanel.cpp:47, ParamGridPanel.cpp:133).
//     A LookAndFeel override returning one fixed font would stomp that
//     per-label differentiation (heading vs. label vs. caption vs. mono),
//     undoing B1 rather than extending it.
//   - drawLabel: LookAndFeel_V2's default (juce_LookAndFeel_V2.cpp:1305-1331)
//     already reads getLabelFont() (untouched, see above) and
//     findColour(Label::textColourId/backgroundColourId), which per-label
//     setColour() calls already override where B1 wanted something different
//     (CodeEditorPanel.cpp:11, KeyboardPanel.cpp:48, PromptPanel.cpp:143) and
//     which ColourScheme now supplies a sane default for everywhere else
//     (Label::backgroundColourId is forced transparent in initialiseColours(),
//     juce_LookAndFeel_V4.cpp:1355). Nothing left to fix.
//
// Lifetime contract (both directions load-bearing, satisfied in
// PluginEditor.h/.cpp, not here): this object must outlive every Component
// that points at it (declared before the child panels in PluginEditor.h), and
// setLookAndFeel(nullptr) must be called on whatever owns it before this
// object is destroyed -- ~LookAndFeel() asserts if anything still points at it
// (juce_LookAndFeel.cpp).
//
// No juce::Font is ever stored here -- only Theme::Type::*() calls made at
// paint/construction time inside function bodies below -- so the Font-global
// static-deinit-order trap (Theme.h's own header comment; a real leak this
// project already hit once) does not apply: nothing here is static-duration.
class ForgeLookAndFeel : public juce::LookAndFeel_V4
{
public:
    ForgeLookAndFeel()
    {
        // UIColour order is windowBackground, widgetBackground, menuBackground,
        // outline, defaultText, defaultFill, highlightedText, highlightedFill,
        // menuText (juce_LookAndFeel_V4.h:46-56) -- position, not name, decides
        // which colour is which; the ColourScheme constructor is a bare
        // variadic pack with only a static_assert on the count
        // (juce_LookAndFeel_V4.h:58-65).
        //
        // ONE ACCENT. Both fill slots are Theme::accent (the blue), and that
        // is a design decision rather than an oversight.
        //
        // These two slots used to be accent -> meterHot, echoing the level
        // meter's gradient. Under Catppuccin that read as teal thumb + pink
        // track and merely looked busy; under Tokyo Night it was actively wrong,
        // and rendering the palette change is what exposed it. V4 wires
        // Slider::thumbColourId from defaultFill and BOTH
        // Slider::trackColourId and Slider::rotarySliderFillColourId from
        // highlightedFill (juce_LookAndFeel_V4.cpp:1399-1401), so every slider
        // in the product was drawing its VALUE in the same orange the meter uses
        // to mean "you are about to clip". A colour that means "hot" everywhere
        // means nothing anywhere.
        //
        // So: the accent marks value and focus, and nothing else. The level
        // meter keeps its cool->hot gradient because PluginEditor::paint()
        // draws it from the Theme tokens directly rather than through the
        // scheme (PluginEditor.cpp:290-295) -- which leaves meterHot's orange
        // the ONLY orange in the UI, and therefore still a signal.
        //
        // highlightedText stays Theme::background (dark) wherever text
        // sits on top of highlightedFill (TextButton::textColourOnId, PopupMenu
        // highlighted text -- juce_LookAndFeel_V4.cpp:1338,1374); dark-on-#7aa2f7
        // is a stronger pairing than it was on the old pink.
        setColourScheme ({
            Theme::background,  // windowBackground
            Theme::surface,     // widgetBackground
            Theme::surface,     // menuBackground
            Theme::outline,     // outline
            Theme::textPrimary, // defaultText
            Theme::accent,      // defaultFill      -- slider thumb
            Theme::background,  // highlightedText
            Theme::accent,      // highlightedFill  -- slider track + rotary fill
            Theme::textPrimary  // menuText
        });
    }

    // Routes every TextButton (codeToggle, styleToggle, and the Faust NumEntry
    // +/- buttons createSliderButton hands back unmodified) onto Theme's type
    // scale instead of LookAndFeel_V4's un-themed default
    // (juce_LookAndFeel_V4.cpp:276-279).
    juce::Font getTextButtonFont (juce::TextButton&, int) override
    {
        return Theme::Type::label();
    }

    // Base implementation (LookAndFeel_V2::createSliderTextBox,
    // juce_LookAndFeel_V2.cpp:1607-1629) already wires every colour through
    // findColour(), which the ColourScheme above now supplies correctly --
    // only the font was missing.
    juce::Label* createSliderTextBox (juce::Slider& slider) override
    {
        auto* l = juce::LookAndFeel_V2::createSliderTextBox (slider);
        l->setFont (Theme::Type::caption());
        return l;
    }
};
