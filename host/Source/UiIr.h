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
    int schema = 0;           // 0 = no IR; 1 = sections only; 2 = + components (ADR-029 §4)
    std::string archetype;    // "synth-panel", "channel-strip", "pedal", ""
    std::string tokens;       // palette token set name, e.g. "midnight-brass", ""
    std::vector<Section> sections;
    Components components;    // meaningful only when schema >= 2; default (all
                              // false) under schema 0/1, which never actually
                              // shipped a components block (Phase 1b, the only
                              // producer schema 1 was designed for, never landed).
};

// Parse a UI IR from a juce::var (the JSON representation). Returns a Layout
// with schema == 0 if the input is missing, malformed, or outside [1, 2]
// (ADR-029 §4 raised the ceiling from 1 to 2 when components was added).
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

inline Layout parse(const juce::var& v)
{
    Layout out;
    auto* obj = v.getDynamicObject();
    if (obj == nullptr)
        return out;

    out.schema = asInt(obj->getProperty("schema"), 0);
    // ADR-029 §4: this version understands schema 1 (sections only) and 2
    // (+ components) -- anything else (0, or a future schema this build
    // predates) is "no IR", the same defensive ceiling the original 1-only
    // check enforced, just moved up by one.
    if (out.schema < 1 || out.schema > 2)
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

    return juce::var(root.release());
}

} // namespace UiIr
