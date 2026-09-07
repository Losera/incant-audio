#pragma once
#include <juce_data_structures/juce_data_structures.h>
#include <vector>
#include <string>

// ── UiIr — renderer-agnostic UI intermediate representation ──────────────────
// A minimal, versioned layout description that the LLM may emit alongside Faust
// code. The native ParamGridPanel renders it; a future WebView can read the same
// JSON. No JUCE types, no pixel coordinates — archetype names, section spans,
// and per-control style tokens.
//
// Renderer-agnostic by design (ADR-024). The schema uses string keys and
// integer spans; nothing here depends on JUCE's Component hierarchy. When a
// future WebView backend arrives, it reads the same JSON produced here.
//
// Invariants enforced by the renderer (ParamGridPanel::applyUiIr):
//   - A Button/CheckButton param never renders as continuous (PF-005)
//   - A Kind::Meter param is never writable
//   - Any compiled param not in the IR is appended to a trailing grid section
//
// Phase 1a: hand-authored IRs only. The LLM does NOT emit this yet.
// Phase 1b (gated on headroom): the prompt teaches the LLM to produce this.
namespace UiIr
{

struct ControlRef
{
    std::string paramLabel;   // FaustEngine::ParamInfo::label, the lookup key
    std::string style;        // "arc-knob", "slider", "toggle", "inc-dec", "" (auto)
    std::string size;         // "sm", "md", "lg", "" (default md)
};

struct Section
{
    std::string id;           // unique within this IR, e.g. "osc", "filter"
    std::string title;        // display heading, e.g. "OSCILLATOR"
    int span = 1;             // grid-column span (1 = normal, 2 = double width)
    std::vector<ControlRef> controls;
};

// UiIr schema 3: the palette and type this generated face is dressed in, so a
// grouped patch reads as its own product rather than one more Ember Console
// grid. Renderer-agnostic like the rest of this file -- colours are CSS hex or
// rgba() STRINGS, not juce::Colour; parsing them to a Colour and enforcing the
// WCAG contrast ratios Theme.h documents is a separate host-side pass
// (ThemeValidate.h, Step 2), not this struct's job.
//
// Every field defaults to the Ember Console token for its role, so a
// default-constructed Theme -- and therefore a schema 0/1/2 Layout, which has
// no `theme` block -- already carries the shell palette. `parse()` never
// produces an empty Theme: a missing `theme` object, a missing key, or an
// unrecognised enum value all degrade to the default token here, never to ""
// and never by rejecting the whole Layout (the mistake the GeneratedAccent
// bone swatch taught this codebase once -- see Theme.h's RESOLVED RISK note).
//
// The four enum fields are closed sets:
//   display : condensed-sans | geometric-sans | grotesk | slab | engraved
//   readout : mono | condensed-sans
//   knob    : arc | filled | pointer | chicken-head
//   density : roomy | standard | tight
struct Theme
{
    std::string surface   = "#0c0c0c";   // outermost panel fill  (Theme::surface)
    std::string panel     = "#131313";   // raised card fill      (Theme::surfaceRaised)
    std::string line      = "#383838";   // hairline / rule       (Theme::outline)
    std::string text      = "#f5f0e6";   // primary ink           (Theme::textPrimary)
    std::string textDim   = "#8a8378";   // section headings, read-only ink (Theme::textSecondary)
    std::string accent    = "#ff4b1f";   // value arcs, focus     (Theme::accent)
    std::string accentAlt = "#ffb03d";   // one distinct second signal only (Theme::progress)
    std::string display   = "engraved";  // heading typeface family
    std::string readout   = "mono";      // numeric-readout typeface family
    std::string knob      = "arc";       // rotary-control style
    std::string density   = "standard";  // layout spacing scale

    // Field-wise equality. Its one caller (PluginForgeEditor::applyGeneratedFace,
    // ADR-035 Step 3) asks a single question: "is this the untouched Ember
    // default?" -- `theme == UiIr::Theme{}` -- to decide between attaching a
    // GeneratedFaceLookAndFeel and leaving the shell's ForgeLookAndFeel in
    // place. Written out rather than defaulted: this file is C++17.
    friend bool operator== (const Theme& a, const Theme& b)
    {
        return a.surface == b.surface && a.panel == b.panel && a.line == b.line
            && a.text == b.text && a.textDim == b.textDim && a.accent == b.accent
            && a.accentAlt == b.accentAlt && a.display == b.display
            && a.readout == b.readout && a.knob == b.knob && a.density == b.density;
    }
    friend bool operator!= (const Theme& a, const Theme& b) { return ! (a == b); }
};

// ADR-029 §4: which fixed UI bands this generated plugin has, decided by the
// same deterministic post-compile pass that already produces
// ParamGridPanel::deriveLayoutFromGroups() -- from the voice contract and
// (in principle) captured Kind::Meter zones, not a new LLM output.
// Generalizes 89268ec's single keyboard-conditional bool into one descriptor
// rather than a bespoke flag per band. sampleBrowser is currently always true
// (89268ec's explicit "unconditional by design" call, unchanged by this ADR)
// -- carried here as a descriptive fact, not a new policy decision.
//
// `meter` is real as a FIELD, not yet as a live signal: ParamGridPanel::
// deriveComponents() computes it correctly from whatever list it is given,
// but the list that actually reaches it (ParamPool's per-slot view) cannot
// carry a meter at all -- PF-052, still open, discards them one layer
// upstream. See deriveComponents()'s own comment for the exact call chain.
struct Components
{
    bool keyboard = false;
    bool sampleBrowser = false;
    bool meter = false;
};

struct Layout
{
    int schema = 0;           // 0 = no IR; 1 = sections only; 2 = + components
                              // (ADR-029 §4); 3 = + theme (UiIr schema 3)
    std::string archetype;    // "synth-panel", "channel-strip", "pedal", ""
    std::string tokens;       // palette token set name, e.g. "midnight-brass", ""
    std::vector<Section> sections;
    Components components;    // meaningful only when schema >= 2; default (all
                              // false) under schema 0/1, which never actually
                              // shipped a components block (Phase 1b, the only
                              // producer schema 1 was designed for, never landed).
    Theme theme;             // meaningful only when schema >= 3; defaults to the
                              // Ember Console tokens for every earlier schema,
                              // which carry no `theme` block. The renderer
                              // (ParamGridPanel::applyUiIr) does not read this
                              // yet -- a per-face LookAndFeel does (Step 3).
};

// Parse a UI IR from a juce::var (the JSON representation). Returns a Layout
// with schema == 0 if the input is missing, malformed, or outside [1, 3]
// (ADR-029 §4 raised the ceiling from 1 to 2 for components; UiIr schema 3
// raised it to 3 for the theme block).
Layout parse(const juce::var& v);

// Serialize a Layout to a juce::var suitable for JSON output.
juce::var toVar(const Layout& layout);

// A "no IR" sentinel — schema 0, empty sections.
inline Layout empty() { return {}; }

inline std::string asString(const juce::var& v)
{
    return v.isString() ? v.toString().toStdString() : std::string {};
}

inline int asInt(const juce::var& v, int fallback)
{
    return v.isInt() ? static_cast<int>(v) : fallback;
}

// A colour string as authored (CSS hex or rgba()), or `fallback` when the key
// is absent or empty. Validity of the string itself is Step 2's concern, not
// this parser's -- an unparseable colour must still degrade per-token, never
// discard the Layout.
inline std::string colourOr(const juce::var& v, const std::string& fallback)
{
    const auto s = asString(v);
    return s.empty() ? fallback : s;
}

// `v` if it is one of `allowed`, otherwise `fallback`. The theme enums are
// closed sets; an unknown value (a newer producer, a typo) collapses to the
// default token rather than rejecting the face.
inline std::string oneOf(const juce::var& v,
                         std::initializer_list<const char*> allowed,
                         const std::string& fallback)
{
    const auto s = asString(v);
    for (const auto* a : allowed)
        if (s == a)
            return s;
    return fallback;
}

inline Layout parse(const juce::var& v)
{
    Layout out;
    auto* obj = v.getDynamicObject();
    if (obj == nullptr)
        return out;

    out.schema = asInt(obj->getProperty("schema"), 0);
    // This version understands schema 1 (sections only), 2 (+ components,
    // ADR-029 §4) and 3 (+ theme, UiIr schema 3) -- anything else (0, or a
    // future schema this build predates) is "no IR", the same defensive
    // ceiling the original 1-only check enforced, moved up as each block
    // landed.
    if (out.schema < 1 || out.schema > 3)
        return empty();

    out.archetype = asString(obj->getProperty("archetype"));
    out.tokens = asString(obj->getProperty("tokens"));

    if (out.schema >= 2)
    {
        if (auto* compObj = obj->getProperty("components").getDynamicObject())
        {
            out.components.keyboard = static_cast<bool>(compObj->getProperty("keyboard"));
            out.components.sampleBrowser = static_cast<bool>(compObj->getProperty("sampleBrowser"));
            out.components.meter = static_cast<bool>(compObj->getProperty("meter"));
        }
    }

    if (out.schema >= 3)
    {
        // out.theme starts at the Ember defaults (Theme's member initialisers),
        // so a missing `theme` object or any missing/blank key just keeps the
        // default token for that field. Enum fields collapse to their default
        // on an unrecognised value -- never rejecting the Layout.
        if (auto* themeObj = obj->getProperty("theme").getDynamicObject())
        {
            Theme t;
            t.surface   = colourOr(themeObj->getProperty("surface"),   t.surface);
            t.panel     = colourOr(themeObj->getProperty("panel"),     t.panel);
            t.line      = colourOr(themeObj->getProperty("line"),      t.line);
            t.text      = colourOr(themeObj->getProperty("text"),      t.text);
            t.textDim   = colourOr(themeObj->getProperty("textDim"),   t.textDim);
            t.accent    = colourOr(themeObj->getProperty("accent"),    t.accent);
            t.accentAlt = colourOr(themeObj->getProperty("accentAlt"), t.accentAlt);
            t.display = oneOf(themeObj->getProperty("display"),
                              { "condensed-sans", "geometric-sans", "grotesk", "slab", "engraved" },
                              t.display);
            t.readout = oneOf(themeObj->getProperty("readout"),
                              { "mono", "condensed-sans" }, t.readout);
            t.knob = oneOf(themeObj->getProperty("knob"),
                           { "arc", "filled", "pointer", "chicken-head" }, t.knob);
            t.density = oneOf(themeObj->getProperty("density"),
                              { "roomy", "standard", "tight" }, t.density);
            out.theme = std::move(t);
        }
    }

    const auto sectionsVar = obj->getProperty("sections");
    if (! sectionsVar.isArray())
        return out;

    for (const auto& sectionVar : *sectionsVar.getArray())
    {
        auto* sectionObj = sectionVar.getDynamicObject();
        if (sectionObj == nullptr)
            continue;

        Section section;
        section.id = asString(sectionObj->getProperty("id"));
        section.title = asString(sectionObj->getProperty("title"));
        section.span = asInt(sectionObj->getProperty("span"), 1);
        if (section.span < 1) section.span = 1;
        if (section.span > 3) section.span = 3;

        const auto controlsVar = sectionObj->getProperty("controls");
        if (controlsVar.isArray())
        {
            for (const auto& controlVar : *controlsVar.getArray())
            {
                auto* controlObj = controlVar.getDynamicObject();
                if (controlObj == nullptr)
                    continue;
                ControlRef ref;
                ref.paramLabel = asString(controlObj->getProperty("param"));
                ref.style = asString(controlObj->getProperty("style"));
                ref.size = asString(controlObj->getProperty("size"));
                if (! ref.paramLabel.empty())
                    section.controls.push_back(ref);
            }
        }

        if (! section.controls.empty())
            out.sections.push_back(std::move(section));
    }

    return out;
}

inline juce::var toVar(const Layout& layout)
{
    auto root = std::make_unique<juce::DynamicObject>();
    root->setProperty("schema", layout.schema);
    root->setProperty("archetype", juce::String(layout.archetype));
    root->setProperty("tokens", juce::String(layout.tokens));

    juce::Array<juce::var> sections;
    for (const auto& section : layout.sections)
    {
        auto sec = std::make_unique<juce::DynamicObject>();
        sec->setProperty("id", juce::String(section.id));
        sec->setProperty("title", juce::String(section.title));
        sec->setProperty("span", section.span);

        juce::Array<juce::var> controls;
        for (const auto& ref : section.controls)
        {
            auto ctrl = std::make_unique<juce::DynamicObject>();
            ctrl->setProperty("param", juce::String(ref.paramLabel));
            ctrl->setProperty("style", juce::String(ref.style));
            ctrl->setProperty("size", juce::String(ref.size));
            controls.add(juce::var(ctrl.release()));
        }
        sec->setProperty("controls", controls);
        sections.add(juce::var(sec.release()));
    }
    root->setProperty("sections", sections);

    // ADR-029 §4: always written, regardless of layout.schema -- harmless for
    // a schema-1 producer's output (an old parse() ignores unknown keys) and
    // is what lets a schema-2 reader recover components from output this
    // version itself wrote.
    auto comp = std::make_unique<juce::DynamicObject>();
    comp->setProperty("keyboard", layout.components.keyboard);
    comp->setProperty("sampleBrowser", layout.components.sampleBrowser);
    comp->setProperty("meter", layout.components.meter);
    root->setProperty("components", juce::var(comp.release()));

    // UiIr schema 3: same "always written" policy as components above. A
    // schema < 3 reader ignores the key; a schema-3 reader round-trips the
    // face. layout.theme is always fully populated (Ember defaults if it was
    // never set), so this never emits a partial block.
    auto th = std::make_unique<juce::DynamicObject>();
    th->setProperty("surface",   juce::String(layout.theme.surface));
    th->setProperty("panel",     juce::String(layout.theme.panel));
    th->setProperty("line",      juce::String(layout.theme.line));
    th->setProperty("text",      juce::String(layout.theme.text));
    th->setProperty("textDim",   juce::String(layout.theme.textDim));
    th->setProperty("accent",    juce::String(layout.theme.accent));
    th->setProperty("accentAlt", juce::String(layout.theme.accentAlt));
    th->setProperty("display",   juce::String(layout.theme.display));
    th->setProperty("readout",   juce::String(layout.theme.readout));
    th->setProperty("knob",      juce::String(layout.theme.knob));
    th->setProperty("density",   juce::String(layout.theme.density));
    root->setProperty("theme", juce::var(th.release()));

    return juce::var(root.release());
}

} // namespace UiIr
