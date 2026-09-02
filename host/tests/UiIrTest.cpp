// ---------------------------------------------------------------------------
// UiIrTest — UiIr::parse() / UiIr::toVar() (host/Source/UiIr.h), the
// renderer-agnostic layout IR. Before this target neither function had a
// single direct test: the IR was only ever built in-process by
// ParamGridPanel::deriveLayoutFromGroups() (returns a Layout, never
// round-trips through JSON) and consumed by applyUiIr(). UiIr schema 3 makes
// parse()/toVar() a real production path — the state blob persists the IR as
// JSON (PluginProcessor::get/setStateInformation) — so it needs pinning.
//
// Pure juce_data_structures (var, DynamicObject, JSON). No plugin sources, no
// libfaust, no gui — same "pure and cheap" shape as GenerationProfilesAutoTest.
//
// Covers:
//   - schema-3 round-trip: sections + components + a non-default theme survive
//     Layout -> toVar -> JSON string -> JSON::parse -> parse
//   - the ceiling: schema 4 (and 0, and "no object") -> UiIr::empty()
//   - per-token degradation: a garbage theme enum keeps the Layout and its
//     sections, and that one field falls back to its Ember default
//   - back-compat: a schema-2 blob still parses; its theme reads as defaults
//   - toVar() always emits a theme block, even for a default Layout
//
// Run: ./UiIrTest  — exits 0 on success, 1 with a failure list.
// ---------------------------------------------------------------------------
#include "../Source/UiIr.h"

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

// The full production round-trip: exactly what PluginProcessor does across a
// save/reload — toVar, serialise to a JSON string, parse the string, re-parse.
UiIr::Layout roundTrip(const UiIr::Layout& in)
{
    const juce::String json = juce::JSON::toString(UiIr::toVar(in), true);
    return UiIr::parse(juce::JSON::parse(json));
}

} // namespace

int main()
{
    // ── schema-3 round-trip ────────────────────────────────────────────────
    {
        UiIr::Layout l;
        l.schema = 3;
        l.archetype = "tape-unit";
        l.tokens = "echo-plate";
        l.components.keyboard = false;
        l.components.sampleBrowser = true;
        l.components.meter = true;
        l.theme.surface   = "#17140f";
        l.theme.panel     = "#221d16";
        l.theme.line      = "rgba(239,230,212,0.22)";
        l.theme.text      = "#efe6d4";
        l.theme.textDim   = "rgba(239,230,212,0.4)";
        l.theme.accent    = "#f0a63c";
        l.theme.accentAlt = "#67e8f9";
        l.theme.display   = "slab";
        l.theme.readout   = "mono";
        l.theme.knob      = "pointer";
        l.theme.density   = "roomy";

        UiIr::Section transport;
        transport.id = "transport";
        transport.title = "TRANSPORT";
        transport.span = 2;
        transport.controls.push_back({ "Time", "arc-knob", "lg" });
        transport.controls.push_back({ "Sync", "toggle", "" });
        l.sections.push_back(transport);

        UiIr::Section mix;
        mix.id = "mix";
        mix.title = "MIX";
        mix.span = 1;
        mix.controls.push_back({ "Mix", "slider", "md" });
        l.sections.push_back(mix);

        const auto out = roundTrip(l);

        check(out.schema == 3, "schema 3 survives");
        checkEq(out.archetype, "tape-unit", "archetype survives");
        checkEq(out.tokens, "echo-plate", "tokens survives");
        check(out.components.sampleBrowser && out.components.meter && ! out.components.keyboard,
              "components survive");

        checkEq(out.theme.surface,   "#17140f",                    "theme.surface survives");
        checkEq(out.theme.panel,     "#221d16",                    "theme.panel survives");
        checkEq(out.theme.line,      "rgba(239,230,212,0.22)",     "theme.line (rgba) survives");
        checkEq(out.theme.text,      "#efe6d4",                    "theme.text survives");
        checkEq(out.theme.textDim,   "rgba(239,230,212,0.4)",      "theme.textDim survives");
        checkEq(out.theme.accent,    "#f0a63c",                    "theme.accent survives");
        checkEq(out.theme.accentAlt, "#67e8f9",                    "theme.accentAlt survives");
        checkEq(out.theme.display,   "slab",                       "theme.display survives");
        checkEq(out.theme.readout,   "mono",                       "theme.readout survives");
        checkEq(out.theme.knob,      "pointer",                    "theme.knob survives");
        checkEq(out.theme.density,   "roomy",                      "theme.density survives");

        check(out.sections.size() == 2, "both sections survive");
        if (out.sections.size() == 2)
        {
            checkEq(out.sections[0].id, "transport", "section 0 id");
            check(out.sections[0].span == 2, "section 0 span");
            check(out.sections[0].controls.size() == 2, "section 0 control count");
            checkEq(out.sections[0].controls[0].paramLabel, "Time", "section 0 control 0 label");
            checkEq(out.sections[0].controls[0].size, "lg", "section 0 control 0 size");
            checkEq(out.sections[1].controls[0].paramLabel, "Mix", "section 1 control 0 label");
        }
    }

    // ── the ceiling: schema 4 -> empty() ───────────────────────────────────
    {
        auto obj = std::make_unique<juce::DynamicObject>();
        obj->setProperty("schema", 4);
        obj->setProperty("archetype", "synth-panel");
        juce::Array<juce::var> sections;
        auto sec = std::make_unique<juce::DynamicObject>();
        sec->setProperty("id", "osc");
        juce::Array<juce::var> ctrls;
        auto c = std::make_unique<juce::DynamicObject>();
        c->setProperty("param", "Detune");
        ctrls.add(juce::var(c.release()));
        sec->setProperty("controls", ctrls);
        sections.add(juce::var(sec.release()));
        obj->setProperty("sections", sections);

        const auto out = UiIr::parse(juce::var(obj.release()));
        check(out.schema == 0, "schema 4 parses as schema 0");
        check(out.sections.empty(), "schema 4 discards its sections");
        checkEq(out.archetype, "", "schema 4 discards its archetype");
    }

    // schema 0 and a non-object both yield empty()
    check(UiIr::parse(juce::var()).schema == 0, "void var -> schema 0");
    {
        auto z = std::make_unique<juce::DynamicObject>();
        z->setProperty("schema", 0);
        check(UiIr::parse(juce::var(z.release())).schema == 0, "explicit schema 0 stays 0");
    }

    // ── per-token degradation: garbage theme enum ──────────────────────────
    {
        auto theme = std::make_unique<juce::DynamicObject>();
        theme->setProperty("display", "comic-sans-3000");   // not in the closed set
        theme->setProperty("knob", "wobble");               // not in the closed set
        theme->setProperty("accent", "#abcdef");            // a valid colour string
        theme->setProperty("surface", "");                  // blank -> default

        auto sec = std::make_unique<juce::DynamicObject>();
        sec->setProperty("id", "flt");
        sec->setProperty("title", "FILTER");
        juce::Array<juce::var> ctrls;
        auto c = std::make_unique<juce::DynamicObject>();
        c->setProperty("param", "Cutoff");
        ctrls.add(juce::var(c.release()));
        sec->setProperty("controls", ctrls);
        juce::Array<juce::var> sections;
        sections.add(juce::var(sec.release()));

        auto obj = std::make_unique<juce::DynamicObject>();
        obj->setProperty("schema", 3);
        obj->setProperty("theme", juce::var(theme.release()));
        obj->setProperty("sections", sections);

        const auto out = UiIr::parse(juce::var(obj.release()));

        check(out.schema == 3, "garbage enum: Layout still schema 3");
        check(out.sections.size() == 1, "garbage enum: sections kept");
        if (! out.sections.empty())
            checkEq(out.sections[0].controls.at(0).paramLabel, "Cutoff",
                    "garbage enum: control kept");
        checkEq(out.theme.display, "engraved", "garbage display -> default token");
        checkEq(out.theme.knob, "arc", "garbage knob -> default token");
        checkEq(out.theme.accent, "#abcdef", "valid accent kept verbatim");
        checkEq(out.theme.surface, "#0c0c0c", "blank surface -> default token");
        checkEq(out.theme.text, "#f5f0e6", "missing text key -> default token");
    }

    // ── back-compat: a schema-2 blob still parses, theme reads as defaults ──
    {
        UiIr::Layout l;
        l.schema = 2;
        l.archetype = "pedal";
        l.components.meter = false;
        UiIr::Section s;
        s.id = "drive";
        s.title = "DRIVE";
        s.controls.push_back({ "Gain", "", "" });
        l.sections.push_back(s);

        const auto out = roundTrip(l);
        check(out.schema == 2, "schema 2 preserved");
        check(out.sections.size() == 1, "schema 2 sections preserved");
        checkEq(out.theme.accent, "#ff4b1f", "schema 2: theme is Ember default accent");
        checkEq(out.theme.knob, "arc", "schema 2: theme is Ember default knob");
    }

    // ── toVar() always emits a theme block, even for a default Layout ───────
    {
        const auto v = UiIr::toVar(UiIr::empty());
        auto* obj = v.getDynamicObject();
        check(obj != nullptr && obj->hasProperty("theme"), "toVar(empty) has a theme block");
        if (obj != nullptr)
        {
            auto* t = obj->getProperty("theme").getDynamicObject();
            check(t != nullptr && t->getProperty("accent").toString() == "#ff4b1f",
                  "toVar(empty) theme carries the Ember default accent");
        }
    }

    std::cerr << checks << " checks, " << failures << " failures\n";
    return failures == 0 ? 0 : 1;
}
