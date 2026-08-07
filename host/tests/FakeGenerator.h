#pragma once
#include <juce_core/juce_core.h>

// ── FakeGenerator ───────────────────────────────────────────────────────────
// A stand-in for llm/generate.py, so a test can drive the REAL PromptPanel
// subprocess bridge without a network, an API key, or a token of quota.
//
// HOW THE SEAM WORKS. PromptPanel resolves its generator through
// PLUGINFORGE_LLM_SCRIPT and its interpreter through PLUGINFORGE_PYTHON
// (PromptPanel.cpp:80-108, :294-299). Point the second at /bin/sh and the first
// at a shell script, and the panel's ChildProcess path — argv assembly, the
// 180s cap, readAllProcessOutput, the "last line starting with {" scan, the
// JSON::parse, the ADR-011 reason handling — all run exactly as they do in
// production. Only the thing on the far end is fake.
//
// THE PAYLOAD IS BUILT WITH juce::JSON AND cat'd, NOT printf'd. The first
// version of this header assembled the JSON inside a shell printf format string,
// which needs THREE layers of escaping to agree at once (C++ literal, printf
// format, JSON string). It did not agree. A patch containing \" arrived at the
// panel as broken JSON, the panel reported "generation failed", and the harness
// read that as a product bug — the fake failing in a way that looks exactly like
// the code under test. juce::JSON::toString on a juce::var cannot emit invalid
// JSON, and `cat` cannot reinterpret it. There is now exactly one escaping
// layer, and a library owns it.
//
// (The same class of trap, already paid for once in PromptPanelThreadingTest:
// juce::File::replaceWithText's `lineEndings` parameter defaults to "\r\n"
// (juce_File.h:781-784), which silently rewrites every \n into CRLF. A POSIX
// shell then reads `sleep 1\r` as an invalid interval. Every write below passes
// "\n" explicitly. Do not remove it.)
namespace FakeGenerator
{

// One valid, trivially compiling stereo patch, as RAW Faust — no escaping. Used
// wherever a scenario needs "generation succeeded" and does not care what the
// DSP is.
inline const char* trivialPatch()
{
    return "import(\"stdfaust.lib\");\nprocess = _*0.5, _*0.5;";
}

namespace detail
{
// Writes `payload` to a file and returns a /bin/sh script that cat's it.
inline juce::File writeScript(const juce::File& dir, const juce::String& name,
                              const juce::String& payload, int sleepSeconds)
{
    auto jsonFile = dir.getChildFile(name + ".json");
    jsonFile.replaceWithText(payload + "\n", false, false, "\n");

    auto script = dir.getChildFile(name);
    script.replaceWithText(
        juce::String("#!/bin/sh\n")
        + (sleepSeconds > 0 ? "sleep " + juce::String(sleepSeconds) + "\n" : "")
        // Quoted so a path containing spaces still works. The prompt ($2) is
        // deliberately ignored: PromptPanel does not read the prompt back out of
        // the JSON — it passes its OWN copy to loadFaustCode (PromptPanel.cpp,
        // runGeneration) — so a fake that echoed it would be testing nothing.
        + "cat '" + jsonFile.getFullPathName() + "'\n",
        false, false, "\n");
    script.setExecutePermission(true);
    return script;
}
} // namespace detail

// success:true with the given RAW Faust source (no pre-escaping — that is this
// function's job). `priorSourceDropped` (default false) lets a test assert the
// token-budget-overflow warning path (generate.py:381-386): the request asked
// to carry prior_source, the LLM layer could not, and the response must say so.
inline juce::File writeSuccess(const juce::File& dir, const juce::String& name,
                               const juce::String& faustCode, int sleepSeconds = 0,
                               bool priorSourceDropped = false)
{
    auto* obj = new juce::DynamicObject();
    obj->setProperty("success", true);
    obj->setProperty("faust_code", faustCode);
    obj->setProperty("attempts", 1);
    obj->setProperty("error", juce::var());
    obj->setProperty("reason", "ok");
    // Present only when true, matching generate.py:381-386 (production emits the
    // field only in the dropped case — an always-present `false` would not be
    // the shape of a real response).
    if (priorSourceDropped)
        obj->setProperty("prior_source_dropped", true);
    // allOnOneLine=true is REQUIRED, not cosmetic. juce::JSON::toString defaults
    // to pretty-printing (juce_JSON.h: `bool allOnOneLine = false`), and
    // PromptPanel scans the output for "the last line that starts with {"
    // (PromptPanel.cpp:415-423) because real stderr can be interleaved. Against
    // pretty-printed JSON that scan matches the opening brace ALONE, parses "{"
    // into an empty var, reads success=false, and reports "Generation failed with
    // no error text" — a fake failing in the shape of a product bug. generate.py
    // emits exactly one line, so this matches production.
    return detail::writeScript(dir, name,
                               juce::JSON::toString(juce::var(obj), /* allOnOneLine */ true),
                               sleepSeconds);
}

// success:false — the LLM-side failure the panel surfaces into the error region.
// `reason` is an ADR-011 reason: invalid_faust | truncated | timeout |
// rate_limited | no_credentials | error. `errorText` is raw, newlines and all.
//
// `priorSourceRefused` mirrors generate.py's _prior_source_refused_response
// (llm/generate.py): surgical (Add) mode's preflight hard-fail. When true,
// `attempts` is forced to 0 regardless of the default below — that response is a
// short-circuit BEFORE generate_faust is ever called, not a failed attempt, and
// the two are coupled deliberately rather than exposed as a separate `attempts`
// parameter (more signature for less fidelity to what production actually sends).
inline juce::File writeFailure(const juce::File& dir, const juce::String& name,
                               const juce::String& errorText,
                               const juce::String& reason = "error",
                               int sleepSeconds = 0,
                               bool priorSourceRefused = false)
{
    auto* obj = new juce::DynamicObject();
    obj->setProperty("success", false);
    obj->setProperty("faust_code", juce::var());
    obj->setProperty("attempts", priorSourceRefused ? 0 : 3);
    obj->setProperty("error", errorText);
    obj->setProperty("reason", reason);
    // Present only when true, matching generate.py's additive-field precedent
    // (writeSuccess's priorSourceDropped just above) -- an always-present
    // `false` would not be the shape of a real response.
    if (priorSourceRefused)
        obj->setProperty("prior_source_refused", true);
    // allOnOneLine=true is REQUIRED, not cosmetic. juce::JSON::toString defaults
    // to pretty-printing (juce_JSON.h: `bool allOnOneLine = false`), and
    // PromptPanel scans the output for "the last line that starts with {"
    // (PromptPanel.cpp:415-423) because real stderr can be interleaved. Against
    // pretty-printed JSON that scan matches the opening brace ALONE, parses "{"
    // into an empty var, reads success=false, and reports "Generation failed with
    // no error text" — a fake failing in the shape of a product bug. generate.py
    // emits exactly one line, so this matches production.
    return detail::writeScript(dir, name,
                               juce::JSON::toString(juce::var(obj), /* allOnOneLine */ true),
                               sleepSeconds);
}

// Output with no JSON line at all — a wedged interpreter or a startup traceback.
// Exercises PromptPanel's "no JSON in output" branch (PromptPanel.cpp:424-437).
inline juce::File writeGarbage(const juce::File& dir, const juce::String& name)
{
    auto script = dir.getChildFile(name);
    script.replaceWithText(
        "#!/bin/sh\n"
        "echo 'Traceback (most recent call last):' >&2\n"
        "echo '  File \"llm/generate.py\", line 1, in <module>' >&2\n"
        "echo 'ModuleNotFoundError: no module named anthropic' >&2\n",
        false, false, "\n");
    script.setExecutePermission(true);
    return script;
}

// A5: a variant that RECORDS what PromptPanel actually invoked, instead of
// ignoring argv the way detail::writeScript's cat does. Every run overwrites
// `<name>_argv.txt` (one arg per line) with this invocation's argv, and
// removes/rewrites `<name>_request.json` -- present with a copy of the
// --request-file payload when that flag was used, ABSENT otherwise (the `rm -f`
// up front matters: without it a --prompt run following a --request-file run on
// the same script would leave the PREVIOUS run's request.json on disk, and a
// test reading "request.json exists" would be checking history, not this run).
// Otherwise behaves like writeSuccess(): always returns success:true with the
// given RAW Faust source.
inline juce::File writeSuccessCapturing(const juce::File& dir, const juce::String& name,
                                        const juce::String& faustCode)
{
    auto argvFile     = dir.getChildFile(name + "_argv.txt");
    auto requestFile  = dir.getChildFile(name + "_request.json");
    auto responseFile = dir.getChildFile(name + "_response.json");

    auto* obj = new juce::DynamicObject();
    obj->setProperty("success", true);
    obj->setProperty("faust_code", faustCode);
    obj->setProperty("attempts", 1);
    obj->setProperty("error", juce::var());
    obj->setProperty("reason", "ok");
    // allOnOneLine=true -- see writeSuccess() above for why that's required, not
    // cosmetic (PromptPanel scans for "the last line starting with {").
    responseFile.replaceWithText(
        juce::JSON::toString(juce::var(obj), /* allOnOneLine */ true),
        false, false, "\n");

    auto script = dir.getChildFile(name);
    script.replaceWithText(
        juce::String("#!/bin/sh\n")
        + "rm -f '" + requestFile.getFullPathName() + "'\n"
        + "for a in \"$@\"; do printf '%s\\n' \"$a\"; done > '"
        + argvFile.getFullPathName() + "'\n"
        + "if [ \"$1\" = '--request-file' ]; then cp \"$2\" '"
        + requestFile.getFullPathName() + "'; fi\n"
        + "cat '" + responseFile.getFullPathName() + "'\n",
        false, false, "\n");
    script.setExecutePermission(true);
    return script;
}

// Test-only readback for writeSuccessCapturing()'s argv.txt -- one juce::String
// per invoked argument, in order, or an empty array if the file doesn't exist
// yet (generation hasn't run).
inline juce::StringArray capturedArgv(const juce::File& dir, const juce::String& name)
{
    auto f = dir.getChildFile(name + "_argv.txt");
    if (! f.existsAsFile())
        return {};
    return juce::StringArray::fromLines(f.loadFileAsString());
}

// Test-only readback for writeSuccessCapturing()'s request.json -- the raw text
// of the --request-file payload PromptPanel wrote, or an empty string if this
// invocation didn't use --request-file at all.
inline juce::String capturedRequestJson(const juce::File& dir, const juce::String& name)
{
    auto f = dir.getChildFile(name + "_request.json");
    return f.existsAsFile() ? f.loadFileAsString() : juce::String();
}

// Point PromptPanel at a given script. Call BEFORE constructing the panel or the
// editor — the path is resolved once, in the constructor (PromptPanel.cpp:80-108).
inline void install(const juce::File& script)
{
    ::setenv("PLUGINFORGE_PYTHON", "/bin/sh", 1);
    ::setenv("PLUGINFORGE_LLM_SCRIPT", script.getFullPathName().toRawUTF8(), 1);
}

} // namespace FakeGenerator
