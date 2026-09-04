#pragma once
#include "ForgeLookAndFeel.h"
#include "ThemeValidate.h"
#include "UiIr.h"

// ── GeneratedFaceLookAndFeel ─────────────────────────────────────────────────
// ADR-035 Step 3 (docs/design/incant-ui/GENERATION_PLAN.md "Gaps 2 + 3",
// docs/sessions/018-incant-ui-faces-and-shell.md §A3). A LookAndFeel built from
// a generated patch's UiIr::Theme, attached to `paramGridPanel` ONLY so that
// panel reads as its own product while the shell around it (title band, prompt
// column, keyboard, sample browser) stays Ember Console.
//
// ── Why it subclasses ForgeLookAndFeel ──────────────────────────────────────
// ForgeLookAndFeel already proved (by reading every candidate virtual's real
// JUCE 7.0.9 body — see its header) that LookAndFeel_V4's draw* methods are
// flat and entirely findColour()-driven, so a themed `ColourScheme` is the
// whole job for colour: no bespoke drawRotarySlider/drawLinearSlider is needed
// to change a face's palette (the arc-knob GEOMETRY is a separate step, A3d).
// Subclassing inherits that ColourScheme-first result plus the three real
// overrides ForgeLookAndFeel carries (getTextButtonFont, createSliderTextBox,
// getTypefaceForFont) and its five embedded typefaces, and this class re-runs
// exactly one thing in its constructor: setColourScheme(), with the nine slots
// filled from the theme instead of the Theme:: constants.
//
// COST: the base constructor reloads the five typefaces, so an active face
// holds a second copy of them (~1-2 MB) for as long as it is attached. Accepted
// for A3a; A3c (the `theme.display`/`theme.readout` embedded face set) is where
// the font ownership gets factored so this stops duplicating.
//
// ── Lifetime (identical contract to ForgeLookAndFeel, one level down) ────────
// juce::Component holds a `WeakReference<LookAndFeel> lookAndFeel`
// (juce_Component.h:2571); ~Component releases it when that member destructs
// (juce_Component.cpp:177 — no explicit LnF handling), and ~LookAndFeel asserts
// the weak-ref count is zero (juce_LookAndFeel.cpp:71). So whatever owns a
// GeneratedFaceLookAndFeel must outlive `paramGridPanel`, AND the panel must be
// detached (`setLookAndFeel(nullptr)`) before this object is replaced during a
// live recompile. Both are handled in PluginEditor: `faceLnf` is a
// std::unique_ptr member declared right after `lnf` and therefore before
// `paramGridPanel` (PluginEditor.h), PluginForgeEditor::applyGeneratedFace()
// detaches before it resets the pointer, and ~PluginForgeEditor detaches the
// panel before anything is torn down.
//
// No juce::Font or Typeface::Ptr is stored HERE beyond what the base already
// owns as members — nothing at namespace or static scope — so the
// static-deinit-order trap Theme.h documents does not apply.
class GeneratedFaceLookAndFeel : public ForgeLookAndFeel
{
public:
    // `raw` is a UiIr::Theme straight off the IR — possibly hand-authored,
    // LLM-produced or restored from a stale state blob. It is run through
    // ThemeValidate::validate() here, so every colour used below is guaranteed
    // parseable and to clear the WCAG thresholds (a failing field is already
    // the Ember token by the time this reads it).
    explicit GeneratedFaceLookAndFeel(const UiIr::Theme& raw)
    {
        const UiIr::Theme t = ThemeValidate::validate(raw).theme;

        const juce::Colour surface = toColour(t.surface, Theme::surface);
        const juce::Colour line    = toColour(t.line,    Theme::outline);
        const juce::Colour text    = toColour(t.text,    Theme::textPrimary);
        const juce::Colour accent  = toColour(t.accent,  Theme::accent);

        // Same nine-slot order and same role assignments ForgeLookAndFeel
        // documents (UIColour: windowBackground, widgetBackground,
        // menuBackground, outline, defaultText, defaultFill, highlightedText,
        // highlightedFill, menuText — juce_LookAndFeel_V4.h:46-56; position,
        // not name, decides which is which).
        //
        // ONE ACCENT, exactly as ForgeLookAndFeel argues: both fill slots are
        // the theme accent, so a control's VALUE and FOCUS are the only things
        // wearing it.
        //
        // highlightedText sits ON TOP of highlightedFill (the accent) — button
        // text-on, popup highlight. ForgeLookAndFeel hard-codes Theme::background
        // there because its surface is always near-black; a generated face can
        // be light (Iron Strip), so pick whichever of surface / text reads
        // better against the accent rather than assuming a dark ground.
        const juce::Colour onAccent =
            ThemeValidate::contrastRatio(ThemeValidate::parseColour(t.surface),
                                         ThemeValidate::parseColour(t.accent))
          >= ThemeValidate::contrastRatio(ThemeValidate::parseColour(t.text),
                                          ThemeValidate::parseColour(t.accent))
                ? surface : text;

        setColourScheme ({
            surface,   // windowBackground
            surface,   // widgetBackground
            surface,   // menuBackground
            line,      // outline
            text,      // defaultText
            accent,    // defaultFill      -- slider thumb
            onAccent,  // highlightedText
            accent,    // highlightedFill  -- slider track + rotary fill
            text       // menuText
        });
    }

private:
    // ThemeValidate returns colour strings and no juce type by design (its test
    // links nothing a UiIr round-trip does not). This is the one place a
    // validated string becomes a juce::Colour.
    static juce::Colour toColour(const std::string& s, juce::Colour fallback)
    {
        const auto c = ThemeValidate::parseColour(s);
        if (! c.valid)          // unreachable after validate(); defensive only
            return fallback;
        return juce::Colour::fromFloatRGBA(static_cast<float>(c.r),
                                           static_cast<float>(c.g),
                                           static_cast<float>(c.b),
                                           static_cast<float>(c.a));
    }
};
