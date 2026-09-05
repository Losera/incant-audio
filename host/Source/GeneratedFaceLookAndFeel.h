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
// for A3a. A3c ADDS five more typefaces of its own (the theme.display/readout
// embedded face set below) rather than factoring the shared ones out --
// the "so this stops duplicating" refactor this comment used to promise A3c
// would do is now itself DEFERRED, not delivered: sharing typeface ownership
// with ForgeLookAndFeel would mean changing how the base class's five fonts
// are constructed (its own header ties them to `ForgeLookAndFeel`'s member
// initialiser list), which is a materially bigger change than "add five
// fonts and a dispatch table" and belongs in its own piece of work. So: ten
// embedded typefaces total now duplicate for as long as a face is attached
// (~2-4 MB), a larger but still bounded and still-accepted cost -- font
// FILES, not per-frame allocation, and nothing on the audio thread.
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
        : spaceGroteskBold        (juce::Typeface::createSystemTypefaceFor (PluginForgeFonts::SpaceGroteskBold_ttf,
                                                                            (size_t) PluginForgeFonts::SpaceGroteskBold_ttfSize)),
          barlowCondensedSemiBold (juce::Typeface::createSystemTypefaceFor (PluginForgeFonts::BarlowCondensedSemiBold_ttf,
                                                                            (size_t) PluginForgeFonts::BarlowCondensedSemiBold_ttfSize)),
          oswaldSemiBold          (juce::Typeface::createSystemTypefaceFor (PluginForgeFonts::OswaldSemiBold_ttf,
                                                                            (size_t) PluginForgeFonts::OswaldSemiBold_ttfSize)),
          archivoBold             (juce::Typeface::createSystemTypefaceFor (PluginForgeFonts::ArchivoBold_ttf,
                                                                            (size_t) PluginForgeFonts::ArchivoBold_ttfSize)),
          ibmPlexMonoRegular      (juce::Typeface::createSystemTypefaceFor (PluginForgeFonts::IBMPlexMonoRegular_ttf,
                                                                            (size_t) PluginForgeFonts::IBMPlexMonoRegular_ttfSize))
    {
        const UiIr::Theme t = ThemeValidate::validate(raw).theme;
        // A3c: this class's OWN copy of the two typography enums, used by
        // createSliderTextBox() below and by displayFont()/readoutFont().
        // Deliberately NOT taken as a parameter at call time from
        // ParamGridPanel's `activeTheme` -- that field is a SEPARATE copy of
        // the same fact, set independently by applyUiIr(), and the two can
        // desync: a test (EditorSessionTest scenario 50) that calls
        // applyGeneratedFace() directly, the way the production compile-
        // success callback calls it (PluginEditor.cpp) rather than only
        // through the full applyUiIr()-then-applyGeneratedFace() sequence,
        // reproduced exactly that desync live -- activeTheme.display stayed
        // "engraved" from the PREVIOUS layout while this constructor built a
        // face from "grotesk", and every label kept rendering Pirata One.
        // Storing the enum HERE, on the object that is actually the single
        // source of truth for "what face is attached," makes that class of
        // bug structurally impossible: there is no second copy left to
        // disagree with it.
        themeDisplayEnum = t.display;
        themeReadoutEnum = t.readout;

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

    // ── A3c: theme.display / theme.readout typography ───────────────────────
    // The five embedded families new to A3c, dispatched by the exact literal
    // name their own name-table carries (verified per-file with fontTools --
    // each was renamed after instancing/download so the family FIELD bakes in
    // the weight and the subfamily reads "Regular", the same convention
    // BigShouldersDisplaySemiBold/WorkSansSemiBold already use above, which is
    // what makes dispatch-by-getTypefaceName() unambiguous). Falls through to
    // ForgeLookAndFeel::getTypefaceForFont for the five Ember faces (a face
    // whose theme.display resolves to "engraved" asks for "Pirata One" by
    // name, same as the shell) and, beneath that, LookAndFeel_V4's platform
    // default.
    juce::Typeface::Ptr getTypefaceForFont(const juce::Font& font) override
    {
        const auto& name = font.getTypefaceName();

        if (name == "Space Grotesk Bold")         return spaceGroteskBold;
        if (name == "Barlow Condensed SemiBold")  return barlowCondensedSemiBold;
        if (name == "Oswald SemiBold")             return oswaldSemiBold;
        if (name == "Archivo Bold")                return archivoBold;
        if (name == "IBM Plex Mono")               return ibmPlexMonoRegular;

        return ForgeLookAndFeel::getTypefaceForFont(font);
    }

    // theme.readout drives the SLIDER'S OWN value textbox -- ForgeLookAndFeel's
    // own override of this (inherited otherwise) hardcodes Theme::Type::
    // caption(), which names "Work Sans" at 11px; every generated face wants
    // its readout in `readoutFamilyFor(theme.readout)` instead (IBM Plex Mono
    // for every worked mockup; Barlow Condensed SemiBold only for a `pedal`
    // archetype that chose the condensed-sans readout option per
    // llm/prompts/ui_face_prompt.md:85). Base implementation unchanged
    // (LookAndFeel_V2::createSliderTextBox, juce_LookAndFeel_V2.cpp:1607-1629)
    // -- only the font differs from ForgeLookAndFeel's own override.
    juce::Label* createSliderTextBox(juce::Slider& slider) override
    {
        auto* l = juce::LookAndFeel_V2::createSliderTextBox(slider);
        l->setFont(readoutFont(Theme::Type::caption().getHeight()));
        return l;
    }

    // Called by ParamGridPanel (control name labels, applyPresentation) and
    // ContentArea (section headings, paint()) once they already know a
    // GeneratedFaceLookAndFeel is attached (via dynamic_cast on their own
    // getLookAndFeel()). Deliberately take no enum argument -- see
    // themeDisplayEnum's own declaration for why the caller must not supply
    // a second copy of a fact this object already owns. `height` is whatever
    // Theme::Type::label()/sectionTitle() already fixed for that role -- only
    // the FAMILY changes, the size scale this codebase already tuned per
    // role does not.
    juce::Font displayFont(float height)
    {
        return namedFont(displayFamilyFor(themeDisplayEnum), height);
    }

    juce::Font readoutFont(float height)
    {
        return namedFont(readoutFamilyFor(themeReadoutEnum), height);
    }

    // The enum-to-family mapping is public (not just used internally) so a
    // test can assert it directly without constructing a Font/Graphics
    // context. "slab" is a deliberately imperfect label: Oswald is a
    // condensed grotesque, not a true slab serif, but it is the fourth worked
    // mockup face (Echo Plate) and the only remaining UiIr enum bucket once
    // grotesk/condensed-sans/geometric-sans take the other three faces' own
    // literal family names -- recorded here so a future reader is not misled
    // into expecting an actual slab serif is embedded.
    static juce::String displayFamilyFor(const std::string& display)
    {
        if (display == "grotesk")         return "Space Grotesk Bold";
        if (display == "condensed-sans")  return "Barlow Condensed SemiBold";
        if (display == "geometric-sans")  return "Archivo Bold";
        if (display == "slab")            return "Oswald SemiBold";
        return "Pirata One";   // "engraved", and any value this build predates
    }

    static juce::String readoutFamilyFor(const std::string& readout)
    {
        if (readout == "condensed-sans")  return "Barlow Condensed SemiBold";
        return "IBM Plex Mono";   // "mono", and any value this build predates
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

    // A3c: the five typefaces new to this class, loaded once per attach --
    // see the COST note at the top of this file's header comment. Same
    // lifetime shape ForgeLookAndFeel's own five already establish: members
    // of an object that is a by-value/unique_ptr member up the ownership
    // chain (PluginEditor's `faceLnf`), destroyed well before static deinit.
    juce::Typeface::Ptr spaceGroteskBold, barlowCondensedSemiBold, oswaldSemiBold,
                        archivoBold, ibmPlexMonoRegular;

    // A3c: this face's validated theme.readout, for createSliderTextBox()
    // above. Set once in the constructor; never mutated afterward (a face is
    // replaced wholesale on the next compile, never edited in place).
    std::string themeDisplayEnum = "engraved";
    std::string themeReadoutEnum = "mono";

    // Builds a Font from a literal family NAME. ForgeLookAndFeel's own private
    // resolveFont() takes the opposite input (an existing Theme::Type::*()
    // Font, re-resolving only its typeface) and is not reachable from here
    // (private to that class) -- this is the shape displayFont()/
    // readoutFont() need instead, since their caller supplies an ENUM string,
    // not a pre-built Font.
    juce::Font namedFont(const juce::String& family, float height)
    {
        return juce::Font(getTypefaceForFont(juce::Font(family, height, juce::Font::plain)))
                   .withHeight(height);
    }

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
