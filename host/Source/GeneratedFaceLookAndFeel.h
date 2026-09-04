#pragma once
#include "ForgeLookAndFeel.h"
#include "ThemeValidate.h"
#include "UiIr.h"
#include <cmath>

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

        // A3d: the rotary TRACK cannot be a ColourScheme slot -- see
        // drawRotarySlider's own comment below for why -- so it is computed
        // and stored here instead, from the same validated `text` ink this
        // constructor already resolved. README.md:253's "hairlines at 7-10%
        // ink alpha" is the rule; 10% is GKnob.dc.html's own default
        // (`rgba(255,255,255,.10)` on its dark reference, `text` there being
        // near-white) and is used uniformly for both dark and light themes
        // rather than the two distinct literals the mockups hand-tuned per
        // face (Iron Strip: `rgba(28,27,24,.18)`) -- an 18% light-theme value
        // is not derivable from "text at some alpha" without a second
        // per-theme constant, so this is a deliberate simplification, not an
        // oversight; a light face's track will read slightly fainter than its
        // own mockup until that is revisited.
        trackColour = text.withAlpha(0.10f);
    }

    // The SAME validated accent this constructor just built its ColourScheme
    // from, exposed statically so a caller that does not (yet) have a
    // GeneratedFaceLookAndFeel instance can still know what accent one WOULD
    // resolve to. Exists for ParamGridPanel::setFaceAccent(), called from
    // PluginEditor::applyGeneratedFace() to re-colour already-built slider
    // widgets to match -- see that function's own comment for why a second,
    // separate colour source is needed at all. `theme == UiIr::Theme{}`
    // (the detach sentinel) is a caller concern, not this function's; it
    // still returns a real (Ember) colour for that input, same as the
    // constructor would.
    static juce::Colour resolvedAccent(const UiIr::Theme& raw)
    {
        return toColour(ThemeValidate::validate(raw).theme.accent, Theme::accent);
    }

    // A3d (ADR-035 Step 3 cont'd): the arc-knob GEOMETRY, per
    // docs/design/incant-ui/GKnob.dc.html -- the exact SVG this class's own
    // header comment named as "a separate step" when A3a shipped colour only.
    // Ported formula-for-formula from that file's renderVals(): angle 0 is
    // top-centre, increasing clockwise (GKnob's own
    // `pt(a,r) = (cx + r*sin(a), cy - r*cos(a))`), which is the SAME
    // convention juce::Path::addCentredArc documents (JUCE 7.0.9,
    // juce_Path.h:462-464: "0 is the top-centre of the ellipse... clockwise"),
    // so `rotaryStartAngle`/`rotaryEndAngle` need no rotation offset to reuse
    // directly -- they already arrive in this convention from
    // ParamGridPanel.cpp's `setRotaryParameters(pi*1.2f, pi*2.8f)` call,
    // confirmed by reading that call site rather than assumed.
    //
    // WHY THIS CANNOT BE setColourScheme() ALONE, unlike every other widget
    // this file touches only through colour: LookAndFeel_V4's own
    // initialiseColours() (juce_LookAndFeel_V4.cpp:1401,
    // `Slider::rotarySliderOutlineColourId` <- `ColourScheme::widgetBackground`)
    // wires the TRACK to the same slot this class fills with `surface` -- the
    // panel's own fill. The default drawRotarySlider (juce_LookAndFeel_V4.cpp
    // :1064-1112) would therefore paint every rotary's track in the exact
    // colour of the panel behind it: invisible by construction, on both
    // ForgeLookAndFeel and this class, until this override exists. There is
    // no UIColour slot for "ink at reduced alpha" (trackColour above), so
    // recolouring alone cannot reach the mockups' faint-but-visible hairline
    // track (README.md:253).
    //
    // The default implementation also draws the thumb as a filled ELLIPSE at
    // the value angle (`g.fillEllipse`, juce_LookAndFeel_V4.cpp:1110-1111);
    // GKnob draws a 2px LINE from 34% radius to just inside the arc instead.
    // That geometry difference, not only the missing track colour, is why
    // this overrides the whole method rather than layering a recolour on top
    // of the inherited one.
    void drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height,
                          float sliderPos, float rotaryStartAngle, float rotaryEndAngle,
                          juce::Slider& slider) override
    {
        // Same 10px inset the default implementation uses
        // (juce_LookAndFeel_V4.cpp:1070) -- kept so a face knob occupies the
        // same footprint inside its cell as an un-faced one did.
        const auto bounds = juce::Rectangle<int>(x, y, width, height).toFloat().reduced(10.0f);
        const float diameter = juce::jmin(bounds.getWidth(), bounds.getHeight());
        const float cx = bounds.getCentreX();
        const float cy = bounds.getCentreY();

        // GKnob.dc.html's own words: "stroke 4px at 52px diameter, scale with
        // size" -- thickness is a proportion of diameter (4/52), not a fixed
        // 4px regardless of the control's own sm/md/lg size, clamped to a
        // sane range so a very small or very large knob never gets a
        // hairline-thin or track-swallowing stroke.
        const float strokeW = juce::jlimit(2.0f, 6.0f, diameter * (4.0f / 52.0f));
        const float r = diameter / 2.0f - strokeW / 2.0f - 1.0f;

        const float toAngle = rotaryStartAngle + sliderPos * (rotaryEndAngle - rotaryStartAngle);

        juce::Path track;
        track.addCentredArc(cx, cy, r, r, 0.0f, rotaryStartAngle, rotaryEndAngle, true);
        g.setColour(trackColour);
        g.strokePath(track, juce::PathStrokeType(strokeW, juce::PathStrokeType::curved,
                                                  juce::PathStrokeType::rounded));

        if (slider.isEnabled())
        {
            const auto fill = slider.findColour(juce::Slider::rotarySliderFillColourId);

            // GKnob's own guard ("v > 0.004"): a near-zero arc renders as
            // visible noise at the rounded cap, not as "no value yet".
            if (sliderPos > 0.004f)
            {
                juce::Path value;
                value.addCentredArc(cx, cy, r, r, 0.0f, rotaryStartAngle, toAngle, true);
                g.setColour(fill);
                g.strokePath(value, juce::PathStrokeType(strokeW, juce::PathStrokeType::curved,
                                                          juce::PathStrokeType::rounded));
            }

            // Pointer: a 2px line from 34% radius to just inside the arc
            // (GKnob.dc.html: `pt(a, r*0.34)` to `pt(a, r - w - 1)`).
            const auto atRadius = [&](float radius)
            {
                return juce::Point<float>(cx + radius * std::sin(toAngle),
                                          cy - radius * std::cos(toAngle));
            };
            g.setColour(fill);
            g.drawLine(juce::Line<float>(atRadius(r * 0.34f), atRadius(r - strokeW - 1.0f)), 2.0f);
        }
    }

private:
    // A3d: see drawRotarySlider() above. Not a ColourScheme UIColour --
    // computed once in the constructor, reused on every paint of every
    // rotary this LookAndFeel draws.
    juce::Colour trackColour = Theme::outline;

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
