// ---------------------------------------------------------------------------
// GenerationProfilesAutoTest — GenerationProfiles::resolveAuto()
// (host/Source/GenerationProfiles.generated.h), the C++ mirror of
// llm/generation_profiles.py's resolve() that drives PromptPanel's live
// "Auto -> <Display Name>" preview text (updateAutoFamilyLabel(), called on
// every keystroke). EditorSessionTest cannot exercise the instrument-side
// branch: it never sets -DPF_IS_SYNTH=1 (see OfflineSynthRenderTest's own
// header comment for why that needed a dedicated target), so resolveAuto's
// synthHost==true path -- exactly the branch this session's fix touches --
// is unreachable through it. This target links only juce_core (no gui, no
// libfaust, no plugin sources) and calls resolveAuto() directly, the same
// "pure and cheap" shape as ParamIdentityTest/NoteRingTest.
//
// THE BUG THIS PINS: a prompt naming BOTH generator-family language (drone,
// generative, ...) AND a synth ("a generative synth") used to resolve to
// "generator" -- kind instrument, zero MIDI voice contract required by that
// family's own brief -- silently producing an unplayable "synth" with
// nothing telling the user their prompt got rerouted. The fix is DATA-driven
// (llm/generation_profiles.json's "overridden_by_synth_terms" /
// "synth_override_terms", tools/gen_generation_profiles.py renders the same
// override into this generated header) so the Python resolver and this C++
// preview cannot say different things -- confirmed by
// tests/test_generation_profile_codegen.py's byte-identity check, which is
// what makes running this ONE generated header a reasonable proxy for both
// sides rather than needing to duplicate every case here.
//
// Run: ./GenerationProfilesAutoTest  -- exits 0 on success, 1 with a failure list.
// ---------------------------------------------------------------------------
#include "../Source/GenerationProfiles.generated.h"

#include <iostream>
#include <string>

namespace
{
int failures = 0;
int checks   = 0;

void expectId(const juce::String& prompt, bool synthHost, const char* expectedId,
             const std::string& what)
{
    ++checks;
    const auto& resolved = GenerationProfiles::resolveAuto(prompt, synthHost);
    if (juce::String(resolved.id) != juce::String(expectedId))
    {
        ++failures;
        std::cerr << "FAIL  " << what << "\n"
                  << "      prompt:   \"" << prompt << "\"\n"
                  << "      expected: " << expectedId << "\n"
                  << "      actual:   " << resolved.id << "\n";
    }
}

} // namespace

int main()
{
    // Baseline: unchanged behavior for prompts with no synth-override term.
    expectId("a self-playing ambient drone", true, "generator",
             "generator term alone still resolves to generator");
    expectId("a punchy synthesized kick drum", true, "drum_synth",
             "drum_synth still wins over generator when both could apply");
    expectId("a warm polyphonic pad", true, "synth",
             "no auto_terms match at all still falls back to the instrument default");

    // The fix: a generator term ALSO naming a synth defers to the default.
    expectId("a generative synth", true, "synth",
             "generator+synth defers to the instrument default, not generator");
    expectId("a drone synth pad", true, "synth",
             "drone+synth defers to the instrument default, not generator");
    expectId("a self-playing synth soundscape", true, "synth",
             "multiple generator terms alongside synth still defer");

    // Effect side is untouched -- the override only applies to generator,
    // an instrument-kind profile with overridden_by_synth_terms set.
    expectId("a live granular grain cloud", false, "granular_effect",
             "effect-side auto resolution is unaffected by the synth override");

    std::cerr << checks << " checks, " << failures << " failures\n";
    return failures == 0 ? 0 : 1;
}
