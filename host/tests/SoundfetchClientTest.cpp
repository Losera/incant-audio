// ---------------------------------------------------------------------------
// SoundfetchClientTest — host/Source/SoundfetchClient.cpp had ZERO tests
// before this file, on a subprocess path that turned out to have three
// independent, fatal, user-visible defects (all confirmed live this session,
// against the real soundfetch CLI):
//
//   A. `executable()` returned the bare name "soundfetch", never on PATH on
//      this machine (only ever installed inside venvs). juce::ChildProcess::
//      start() returns TRUE even when execvp() fails in the forked child
//      (juce_SharedCode_posix.h's ActiveProcess ctor), so the "Soundfetch is
//      unavailable" branch was dead code -- every query instead reported
//      "Soundfetch returned no JSON."
//   B. process.start(command) used JUCE's default wantStdOut|wantStdErr.
//      soundfetch's own INFO logging and Internet-Archive progress lines
//      ("archive metadata: n/50") go to stderr unconditionally (no --quiet
//      flag exists), corrupting the merged JSON for the DEFAULT provider.
//   C. A structural {"ok": false, "error": {...}} payload's message needs to
//      actually reach the caller, not just "parses without crashing".
//
// This file exercises the fixed run()/search() end to end through a REAL
// juce::ChildProcess, via SOUNDFETCH_BIN pointed at disposable shell scripts
// -- no network, no soundfetch install required to run this test. The
// technique mirrors FakeGenerator.h's seam for PromptPanel's subprocess (same
// author, same reasoning): point the resolvable escape hatch at a script and
// let the REAL argv assembly, stream capture, and JSON parsing all run
// exactly as they do in production.
//
// Run: ./SoundfetchClientTest  -- exits 0 on success, 1 with a failure list.
// ---------------------------------------------------------------------------
#include "../Source/SoundfetchClient.h"

#include <cstdlib>
#include <iostream>
#include <string>

namespace
{
int failures = 0;
int checks   = 0;

void expectTrue(bool cond, const std::string& what)
{
    ++checks;
    if (! cond)
    {
        ++failures;
        std::cerr << "FAIL  " << what << "\n";
    }
}

void expectContains(const juce::String& haystack, const juce::String& needle,
                    const std::string& what)
{
    ++checks;
    if (! haystack.contains(needle))
    {
        ++failures;
        std::cerr << "FAIL  " << what << "\n"
                  << "      expected to contain: " << needle << "\n"
                  << "      actual:               " << haystack << "\n";
    }
}

// Writes a disposable shell script and points SOUNDFETCH_BIN at it. Every
// scenario below calls this before constructing its SoundfetchClient --
// commandPrefix() re-reads the environment on every run(), so no caching
// hazard exists (unlike FakeGenerator's PromptPanel seam, which IS resolved
// once at construction -- see FakeGenerator.h).
juce::File installStub(const juce::File& dir, const juce::String& name, const juce::String& body)
{
    auto script = dir.getChildFile(name);
    script.replaceWithText(juce::String("#!/bin/sh\n") + body, false, false, "\n");
    script.setExecutePermission(true);
    ::setenv("SOUNDFETCH_BIN", script.getFullPathName().toRawUTF8(), 1);
    return script;
}

// ── Case 1: clean stdout JSON parses and populates results ─────────────────
void testCleanJsonParses(const juce::File& tmp)
{
    installStub(tmp, "sf_clean.sh",
        "echo '{\"ok\":true,\"manifest\":\"m.jsonl\",\"results\":"
        "[{\"provider_id\":\"abc123\",\"name\":\"Rain\",\"provider\":\"archive\","
        "\"metadata\":{\"license\":\"CC0\",\"duration\":12.5}}]}'\n");

    SoundfetchClient client(tmp.getChildFile("cache1"));
    auto response = client.search("archive", "rain");

    expectTrue(response.ok, "clean JSON: response.ok is true");
    expectTrue(response.results.size() == 1, "clean JSON: one result parsed");
    if (response.results.size() == 1)
    {
        expectTrue(response.results[0].providerId == "abc123", "clean JSON: provider_id survives");
        expectTrue(response.results[0].title == "Rain", "clean JSON: name survives");
        expectTrue(response.results[0].license == "CC0", "clean JSON: metadata.license survives");
    }
}

// ── Case 2 (cause B): stderr noise ahead of the JSON must not break parsing.
// Before the wantStdOut-only fix, JUCE's default start() flags merged this
// stub's stderr line into the same pipe as stdout, so JSON::parse saw
// "INFO ...\n{...}" and failed -- exactly what a real Internet Archive search
// produced (soundfetch cli.py's unconditional logging.basicConfig(INFO) and
// its "archive metadata: n/50" progress writes, both to stderr). Confirmed
// red against the pre-fix `process.start(command)` (default merge flags) by
// temporarily reverting SoundfetchClient.cpp's wantStdOut-only change during
// this session and re-running this exact test.
void testStderrNoiseIgnored(const juce::File& tmp)
{
    installStub(tmp, "sf_noisy.sh",
        "echo 'INFO soundfetch.providers.archive.provider: skip some-item: no audio file' 1>&2\n"
        "echo 'archive metadata: 1/50' 1>&2\n"
        "echo '{\"ok\":true,\"manifest\":\"m.jsonl\",\"results\":[]}'\n");

    SoundfetchClient client(tmp.getChildFile("cache2"));
    auto response = client.search("archive", "rain");

    expectTrue(response.ok, "stderr-noise case: response.ok is true despite stderr chatter");
}

// ── Case 3 (cause A): a missing interpreter/binary must produce the friendly
// "Soundfetch is unavailable" message, not "Soundfetch returned no JSON." --
// which is what the pre-fix code produced, because juce::ChildProcess::
// start() returns true even for a nonexistent executable (fork() succeeds;
// execvp() fails only in the forked child). This test proves the NEW
// exit-255-and-empty-output detection (SoundfetchClient.cpp) actually fires.
void testMissingInterpreterReportsUnavailable(const juce::File& tmp)
{
    auto missing = tmp.getChildFile("definitely-does-not-exist-soundfetch-stub");
    if (missing.existsAsFile())
        missing.deleteFile();
    ::setenv("SOUNDFETCH_BIN", missing.getFullPathName().toRawUTF8(), 1);

    SoundfetchClient client(tmp.getChildFile("cache3"));
    auto response = client.search("archive", "rain");

    expectTrue(! response.ok, "missing interpreter: response.ok is false");
    expectContains(response.error, "unavailable",
                   "missing interpreter: error names it as unavailable, not \"no JSON\"");
}

// ── Case 4 (cause C): a structural {"ok": false, "error": {...}} payload's
// message must reach the caller -- this is what a real Freesound 403 looks
// like (soundfetch's own JSON error shape, confirmed live this session).
void testStructuredErrorSurfaces(const juce::File& tmp)
{
    installStub(tmp, "sf_403.sh",
        "echo '{\"ok\":false,\"error\":{\"type\":\"HttpError\","
        "\"message\":\"HTTP 403 Forbidden\"}}'\n"
        "exit 1\n");

    SoundfetchClient client(tmp.getChildFile("cache4"));
    auto response = client.search("freesound", "piano");

    expectTrue(! response.ok, "structured error: response.ok is false");
    expectContains(response.error, "403 Forbidden",
                   "structured error: the nested error.message reaches response.error");
}

// ── Case 5 (cause A, the production path): with SOUNDFETCH_BIN unset, the
// resolved command is `<python> -m soundfetch`, not a bare "soundfetch" that
// depended on PATH -- the actual defect this session's fix targets.
void testDefaultCommandUsesPythonModule()
{
    ::unsetenv("SOUNDFETCH_BIN");
    ::unsetenv("PLUGINFORGE_SOUNDFETCH_PYTHON");
    ::unsetenv("PLUGINFORGE_PYTHON");

    SoundfetchClient client;
    auto argv = client.commandPrefixForTest();

    expectTrue(argv.size() == 3, "default resolution: three-token command");
    if (argv.size() == 3)
    {
        expectTrue(argv[0] == "python3", "default resolution: falls back to python3");
        expectTrue(argv[1] == "-m", "default resolution: uses -m");
        expectTrue(argv[2] == "soundfetch", "default resolution: module is soundfetch");
    }

    ::setenv("PLUGINFORGE_SOUNDFETCH_PYTHON", "/opt/venvs/soundfetch/bin/python", 1);
    auto overridden = client.commandPrefixForTest();
    expectTrue(overridden.size() == 3 && overridden[0] == "/opt/venvs/soundfetch/bin/python",
              "PLUGINFORGE_SOUNDFETCH_PYTHON overrides the interpreter");
    ::unsetenv("PLUGINFORGE_SOUNDFETCH_PYTHON");

    ::setenv("PLUGINFORGE_PYTHON", "/opt/shared/bin/python", 1);
    auto shared = client.commandPrefixForTest();
    expectTrue(shared.size() == 3 && shared[0] == "/opt/shared/bin/python",
              "PLUGINFORGE_PYTHON is the secondary interpreter fallback");
    ::unsetenv("PLUGINFORGE_PYTHON");
}

// ── ADR-032 v1: config.json's soundfetch_interpreter_path fills the slot when
// neither env override is set -- the launcher-started-DAW case that inherits no
// PLUGINFORGE_* at all. An env override still wins over it.
void testConfigInterpreterFallback(const juce::File& tmp)
{
    ::unsetenv("SOUNDFETCH_BIN");
    ::unsetenv("PLUGINFORGE_SOUNDFETCH_PYTHON");
    ::unsetenv("PLUGINFORGE_PYTHON");

    auto cfgHome = tmp.getChildFile("sf_cfg_home");
    cfgHome.deleteRecursively();
    ::setenv("XDG_CONFIG_HOME", cfgHome.getFullPathName().toRawUTF8(), 1);

    // No config file yet -> unchanged "python3" default.
    {
        SoundfetchClient client(tmp.getChildFile("cache_cfg_none"));
        auto argv = client.commandPrefixForTest();
        expectTrue(argv.size() == 3 && argv[0] == "python3",
                  "no config file: interpreter still defaults to python3");
    }

    // config file with soundfetch_interpreter_path -> that path is used.
    {
        auto cfgFile = cfgHome.getChildFile("pluginforge/config.json");
        cfgFile.getParentDirectory().createDirectory();
        cfgFile.replaceWithText(
            "{ \"schema\": 1, \"soundfetch_interpreter_path\": \"/opt/incant/bin/python\" }\n",
            false, false, "\n");

        SoundfetchClient client(tmp.getChildFile("cache_cfg_set"));
        auto argv = client.commandPrefixForTest();
        expectTrue(argv.size() == 3 && argv[0] == "/opt/incant/bin/python",
                  "config.json's soundfetch_interpreter_path is used when no env override is set");

        // An env override still beats the config file.
        ::setenv("PLUGINFORGE_SOUNDFETCH_PYTHON", "/opt/venv/bin/python", 1);
        auto overridden = client.commandPrefixForTest();
        expectTrue(overridden.size() == 3 && overridden[0] == "/opt/venv/bin/python",
                  "PLUGINFORGE_SOUNDFETCH_PYTHON still overrides config.json");
        ::unsetenv("PLUGINFORGE_SOUNDFETCH_PYTHON");
    }
    // XDG_CONFIG_HOME left pointed at this scratch dir; main() clears it.
}

// A present interpreter with no soundfetch module is the default installation
// failure, and differs from execvp's missing-executable exit 255 signature.
void testMissingModuleReportsUnavailable(const juce::File& tmp)
{
    auto fakePython = installStub(tmp, "python_without_soundfetch.sh",
        "[ \"$1\" = '-m' ] && [ \"$2\" = 'soundfetch' ] || exit 77\n"
        "echo 'No module named soundfetch' 1>&2\n"
        "exit 1\n");
    ::unsetenv("SOUNDFETCH_BIN");
    ::setenv("PLUGINFORGE_SOUNDFETCH_PYTHON", fakePython.getFullPathName().toRawUTF8(), 1);

    SoundfetchClient client(tmp.getChildFile("cache5"));
    auto response = client.search("archive", "rain");

    expectTrue(! response.ok, "missing module: response.ok is false");
    expectContains(response.error, "unavailable",
                   "missing module: error provides installation guidance, not no JSON");
    ::unsetenv("PLUGINFORGE_SOUNDFETCH_PYTHON");
}

void testSearchAndDownloadArguments(const juce::File& tmp)
{
    installStub(tmp, "sf_argv.sh",
        "if [ \"$2\" = 'search' ]; then\n"
        "  [ \"$1\" = 'archive' ] && [ \"$3\" = 'rain storm' ] &&\n"
        "  [ \"$4\" = '--outdir' ] && [ \"$6\" = '--max-results' ] &&\n"
        "  [ \"$7\" = '10' ] && [ \"$8\" = '--json' ] || exit 78\n"
        "  echo '{\"ok\":true,\"manifest\":\"manifest.jsonl\",\"results\":[]}'\n"
        "elif [ \"$2\" = 'download' ]; then\n"
        "  [ \"$1\" = 'archive' ] && [ \"$3\" = '--manifest' ] &&\n"
        "  [ \"$4\" = 'manifest.jsonl' ] && [ \"$5\" = '--provider-id' ] &&\n"
        "  [ \"$6\" = 'item-42' ] && [ \"$7\" = '--outdir' ] &&\n"
        "  [ \"$9\" = '--json' ] || exit 79\n"
        "  echo '{\"ok\":true,\"items\":[{\"local_path\":\"/tmp/item.wav\"}]}'\n"
        "else\n"
        "  exit 80\n"
        "fi\n");

    SoundfetchClient client(tmp.getChildFile("cache6"));
    auto search = client.search("archive", "rain storm");
    expectTrue(search.ok, "search argv: provider, query, outdir, limit and JSON flag are ordered");

    auto download = client.download("archive", "item-42", "manifest.jsonl");
    expectTrue(download.ok && download.localPath == "/tmp/item.wav",
              "download argv: manifest, provider id, outdir and JSON flag are ordered");
}

} // namespace

int main()
{
    auto tmp = juce::File::createTempFile("soundfetch_client_test");
    tmp.deleteFile();
    tmp.createDirectory();

    // Isolate from the developer's real ~/.config/pluginforge/config.json:
    // commandPrefix() now consults it (ADR-032 v1) when no env override is set,
    // so every case that asserts the "python3" default needs a config-free HOME.
    auto isolatedConfigHome = tmp.getChildFile("xdg_config_home_empty");
    isolatedConfigHome.createDirectory();
    ::setenv("XDG_CONFIG_HOME", isolatedConfigHome.getFullPathName().toRawUTF8(), 1);

    testCleanJsonParses(tmp);
    testStderrNoiseIgnored(tmp);
    testMissingInterpreterReportsUnavailable(tmp);
    testStructuredErrorSurfaces(tmp);
    testDefaultCommandUsesPythonModule();
    testMissingModuleReportsUnavailable(tmp);
    testSearchAndDownloadArguments(tmp);
    testConfigInterpreterFallback(tmp);

    ::unsetenv("SOUNDFETCH_BIN");
    ::unsetenv("XDG_CONFIG_HOME");
    tmp.deleteRecursively();

    std::cerr << checks << " checks, " << failures << " failures\n";
    return failures == 0 ? 0 : 1;
}
