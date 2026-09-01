// PromptPanelPathResolutionTest — PF-065 regression.
//
// PromptPanel::resolveGenerateScript() (host/Source/PromptPanel.cpp, just above
// the constructor) picks how the plugin locates llm/generate.py. Before this
// fix it tried only two things: the PLUGINFORGE_LLM_SCRIPT env override, then
// an upward walk from the loaded binary looking for a sibling llm/generate.py.
// That second step works for a dev-tree or in-tree build by layout accident,
// and fails completely for an installed bundle — confirmed in REAPER against
// a real ~/.vst3 install (docs/BUGS.md PF-065): no repo sits above ~/.vst3, so
// every step of the walk misses, and a DAW launched from a desktop icon does
// not inherit the env override either. The on-screen error read "generate.py
// not found at " with an empty path.
//
// This test exercises the four-step resolveGenerateScript() directly —
// forward-declared below rather than exposed via PromptPanel.h, since nothing
// else needs to call it — so each step can be checked in isolation without
// constructing a PromptPanel/PluginForgeProcessor or pumping a message loop.
// Step 3 (ADR-032 v1) is the ~/.config/pluginforge/config.json
// generate_script_path, inserted before the XDG step so a user-set path beats a
// stale install (PF-071); cases 6-8 cover it.
//
// Every case runs against SCRATCH temp directories for both the "binary" and
// "home" locations. That is not incidental: this test binary is itself built
// inside this repository, so passing the real currentExecutableFile/
// userHomeDirectory defaults would let the upward walk find this repo's own
// real llm/generate.py and silently mask the very steps (2 and 3) the test
// exists to check.
//
// NOT COVERED: the dev-inner-loop case named in PromptPanel.cpp's comment — a
// CMake-copied build at ~/.vst3 that was never installed via install.sh has no
// repo above it AND no XDG-installed runtime, so it still requires exporting
// PLUGINFORGE_LLM_SCRIPT before launching a DAW. That is unchanged, documented
// behaviour, not a regression this fix introduces or claims to close.
//
// Build/run:
//   cmake --build host/build --target PromptPanelPathResolutionTest
//   ./host/build/PromptPanelPathResolutionTest_artefacts/Debug/PromptPanelPathResolutionTest

#include "../Source/PluginProcessor.h"
#include "../Source/PluginConfig.h"

#include <cstdio>
#include <cstdlib>

// Defined with external linkage in PromptPanel.cpp exactly so this test can
// call it without pulling in the rest of PromptPanel's construction. The config
// is passed explicitly (ADR-032 v1) so a case can inject a scratch config
// without reading the developer's real ~/.config/pluginforge/config.json.
// `resolvedVia` (ADR-032 item 7): receives "env" | "repo" | "config" | "xdg" | "".
juce::File resolveGenerateScript(const juce::File& binaryDir, const juce::File& homeDir,
                                 const PluginConfig& config,
                                 juce::String* resolvedVia = nullptr);

// Same story for the interpreter that runs generate.py (PF-065): env
// PLUGINFORGE_PYTHON → config.json python_path (when it names a file that
// exists) → "python3". `resolvedVia` receives "env" | "config" | "default".
juce::String resolvePythonExe(const PluginConfig& config,
                              juce::String* resolvedVia = nullptr);

namespace
{
int failures = 0;
int checks   = 0;

void check(bool condition, const char* what)
{
    ++checks;
    std::printf("  [%s] %s\n", condition ? " OK " : "FAIL", what);
    if (! condition)
        ++failures;
}

// All three resolution steps read these; clearing both before every case is
// what makes the cases independent of each other and of whatever the invoking
// shell happens to have set.
void clearEnv()
{
    ::unsetenv("PLUGINFORGE_LLM_SCRIPT");
    ::unsetenv("XDG_DATA_HOME");
}

// The scratch config every case starts from: all fields empty == "not
// configured", which must reproduce the pre-ADR-032 behaviour exactly.
const PluginConfig kNoConfig {};
} // namespace

int main()
{
    std::printf("PromptPanelPathResolutionTest (PF-065)\n\n");

    auto scratch = juce::File::getSpecialLocation(juce::File::tempDirectory)
                       .getChildFile("pluginforge_pf065_test");
    scratch.deleteRecursively();
    scratch.createDirectory();

    auto emptyBinaryDir = scratch.getChildFile("binary_dir");
    emptyBinaryDir.createDirectory();
    auto emptyHomeDir = scratch.getChildFile("home_dir");
    emptyHomeDir.createDirectory();

    // ── 1. The env override wins outright, even with a real XDG install present ─
    {
        clearEnv();
        auto xdgHome = scratch.getChildFile("case1_xdg");
        auto installed = xdgHome.getChildFile("pluginforge/llm/generate.py");
        installed.getParentDirectory().createDirectory();
        installed.replaceWithText("# installed\n");
        ::setenv("XDG_DATA_HOME", xdgHome.getFullPathName().toRawUTF8(), 1);

        auto envScript = scratch.getChildFile("case1_env_override.py");
        envScript.replaceWithText("# env override\n");
        ::setenv("PLUGINFORGE_LLM_SCRIPT", envScript.getFullPathName().toRawUTF8(), 1);

        auto resolved = resolveGenerateScript(emptyBinaryDir, emptyHomeDir, kNoConfig);
        check(resolved == envScript,
              "PLUGINFORGE_LLM_SCRIPT is used even when an XDG install also exists");
    }

    // ── 2. Pre-existing behaviour: the upward walk finds a dev-tree sibling ────
    {
        clearEnv();
        auto repoRoot = scratch.getChildFile("case2_repo");
        auto binDir = repoRoot.getChildFile("host/build/x/y/z");
        binDir.createDirectory();
        auto script = repoRoot.getChildFile("llm/generate.py");
        script.getParentDirectory().createDirectory();
        script.replaceWithText("# dev tree\n");

        auto resolved = resolveGenerateScript(binDir, emptyHomeDir, kNoConfig);
        check(resolved == script,
              "the upward walk still finds llm/generate.py in a dev-tree-style layout");
    }

    // ── 3. PF-065: no env var, no repo above the binary — XDG_DATA_HOME wins ──
    {
        clearEnv();
        auto xdgHome = scratch.getChildFile("case3_xdg");
        auto installed = xdgHome.getChildFile("pluginforge/llm/generate.py");
        installed.getParentDirectory().createDirectory();
        installed.replaceWithText("# installed\n");
        ::setenv("XDG_DATA_HOME", xdgHome.getFullPathName().toRawUTF8(), 1);

        // emptyBinaryDir has nothing above it in this scratch tree — the exact
        // shape of the confirmed-in-REAPER repro (~/.vst3 with no repo above it).
        auto resolved = resolveGenerateScript(emptyBinaryDir, emptyHomeDir, kNoConfig);
        check(resolved == installed,
              "PF-065: an installed-but-unexported runtime is found via XDG_DATA_HOME");
    }

    // ── 4. PF-065: same, via the ~/.local/share default (XDG_DATA_HOME unset) ─
    {
        clearEnv();
        auto installed = emptyHomeDir.getChildFile(".local/share/pluginforge/llm/generate.py");
        installed.getParentDirectory().createDirectory();
        installed.replaceWithText("# installed default\n");

        auto resolved = resolveGenerateScript(emptyBinaryDir, emptyHomeDir, kNoConfig);
        check(resolved == installed,
              "PF-065: falls back to ~/.local/share/pluginforge when XDG_DATA_HOME is unset");
    }

    // ── 5. Nothing anywhere — stays invalid, exactly as before this fix ───────
    {
        clearEnv();
        auto freshBinaryDir = scratch.getChildFile("case5_binary");
        freshBinaryDir.createDirectory();
        auto freshHomeDir = scratch.getChildFile("case5_home");
        freshHomeDir.createDirectory();

        auto resolved = resolveGenerateScript(freshBinaryDir, freshHomeDir, kNoConfig);
        check(! resolved.existsAsFile(),
              "with nothing found anywhere, the result stays an invalid/nonexistent File");
    }

    // ── 6. ADR-032 / PF-071: config generate_script_path beats a stale XDG ────
    {
        clearEnv();
        auto xdgHome = scratch.getChildFile("case6_xdg");
        auto staleInstall = xdgHome.getChildFile("pluginforge/llm/generate.py");
        staleInstall.getParentDirectory().createDirectory();
        staleInstall.replaceWithText("# stale 2026-08-15 install\n");
        ::setenv("XDG_DATA_HOME", xdgHome.getFullPathName().toRawUTF8(), 1);

        auto configured = scratch.getChildFile("case6_runtime/llm/generate.py");
        configured.getParentDirectory().createDirectory();
        configured.replaceWithText("# the runtime the user pointed config.json at\n");

        PluginConfig cfg;
        cfg.generateScriptPath = configured.getFullPathName();

        auto resolved = resolveGenerateScript(emptyBinaryDir, emptyHomeDir, cfg);
        check(resolved == configured,
              "PF-071: config.json's generate_script_path wins over a stale XDG install");
    }

    // ── 7. A configured path that does not exist falls through to XDG ─────────
    {
        clearEnv();
        auto xdgHome = scratch.getChildFile("case7_xdg");
        auto install = xdgHome.getChildFile("pluginforge/llm/generate.py");
        install.getParentDirectory().createDirectory();
        install.replaceWithText("# installed\n");
        ::setenv("XDG_DATA_HOME", xdgHome.getFullPathName().toRawUTF8(), 1);

        PluginConfig cfg;
        cfg.generateScriptPath = scratch.getChildFile("case7_missing/generate.py").getFullPathName();

        auto resolved = resolveGenerateScript(emptyBinaryDir, emptyHomeDir, cfg);
        check(resolved == install,
              "a configured generate_script_path that does not exist does not block the XDG fallback");
    }

    // ── 8. The upward walk still wins over a configured path (dev inner loop) ──
    {
        clearEnv();
        auto repoRoot = scratch.getChildFile("case8_repo");
        auto binDir = repoRoot.getChildFile("host/build/x/y");
        binDir.createDirectory();
        auto devScript = repoRoot.getChildFile("llm/generate.py");
        devScript.getParentDirectory().createDirectory();
        devScript.replaceWithText("# dev tree\n");

        auto configured = scratch.getChildFile("case8_runtime/llm/generate.py");
        configured.getParentDirectory().createDirectory();
        configured.replaceWithText("# configured\n");

        PluginConfig cfg;
        cfg.generateScriptPath = configured.getFullPathName();

        auto resolved = resolveGenerateScript(binDir, emptyHomeDir, cfg);
        check(resolved == devScript,
              "the binary-relative dev-tree walk still takes priority over a configured path");
    }

    // ── 9. ADR-032 item 7: resolvedVia names which step won ──────────────────
    {
        clearEnv();
        juce::String via;

        auto envScript = scratch.getChildFile("case9_env.py");
        envScript.replaceWithText("# env\n");
        ::setenv("PLUGINFORGE_LLM_SCRIPT", envScript.getFullPathName().toRawUTF8(), 1);
        resolveGenerateScript(emptyBinaryDir, emptyHomeDir, kNoConfig, &via);
        check(via == "env", "resolvedVia == 'env' for the PLUGINFORGE_LLM_SCRIPT override");

        clearEnv();
        auto repoRoot = scratch.getChildFile("case9_repo");
        auto binDir = repoRoot.getChildFile("host/build/x");
        binDir.createDirectory();
        auto devScript = repoRoot.getChildFile("llm/generate.py");
        devScript.getParentDirectory().createDirectory();
        devScript.replaceWithText("# dev\n");
        resolveGenerateScript(binDir, emptyHomeDir, kNoConfig, &via);
        check(via == "repo", "resolvedVia == 'repo' for the upward walk");

        clearEnv();
        auto configured = scratch.getChildFile("case9_cfg/llm/generate.py");
        configured.getParentDirectory().createDirectory();
        configured.replaceWithText("# cfg\n");
        PluginConfig cfg;
        cfg.generateScriptPath = configured.getFullPathName();
        resolveGenerateScript(emptyBinaryDir, emptyHomeDir, cfg, &via);
        check(via == "config", "resolvedVia == 'config' for config.json's generate_script_path");

        clearEnv();
        auto xdgHome = scratch.getChildFile("case9_xdg");
        auto install = xdgHome.getChildFile("pluginforge/llm/generate.py");
        install.getParentDirectory().createDirectory();
        install.replaceWithText("# xdg\n");
        ::setenv("XDG_DATA_HOME", xdgHome.getFullPathName().toRawUTF8(), 1);
        resolveGenerateScript(emptyBinaryDir, emptyHomeDir, kNoConfig, &via);
        check(via == "xdg", "resolvedVia == 'xdg' for the XDG install");

        clearEnv();
        auto b = scratch.getChildFile("case9_none_bin"); b.createDirectory();
        auto h = scratch.getChildFile("case9_none_home"); h.createDirectory();
        resolveGenerateScript(b, h, kNoConfig, &via);
        check(via.isEmpty(), "resolvedVia == '' when nothing resolves");
    }

    // ── 10. PF-065: which interpreter runs generate.py (resolvePythonExe) ─────
    {
        ::unsetenv("PLUGINFORGE_PYTHON");
        juce::String via;

        // env override wins, even with a config python_path present
        auto envPy = scratch.getChildFile("case10_env_python");
        envPy.replaceWithText("#!/bin/sh\n");
        ::setenv("PLUGINFORGE_PYTHON", envPy.getFullPathName().toRawUTF8(), 1);
        PluginConfig cfgWithPy;
        cfgWithPy.pythonPath = scratch.getChildFile("case10_cfg_python").getFullPathName();
        juce::File(cfgWithPy.pythonPath).replaceWithText("#!/bin/sh\n");
        check(resolvePythonExe(cfgWithPy, &via) == envPy.getFullPathName() && via == "env",
              "PLUGINFORGE_PYTHON wins over a configured python_path");

        // no env: an existing config python_path is used
        ::unsetenv("PLUGINFORGE_PYTHON");
        check(resolvePythonExe(cfgWithPy, &via) == cfgWithPy.pythonPath && via == "config",
              "config.json python_path is used when it names a file that exists");

        // no env, config path does not exist: fall through to "python3"
        PluginConfig cfgMissingPy;
        cfgMissingPy.pythonPath = scratch.getChildFile("case10_missing_python").getFullPathName();
        check(resolvePythonExe(cfgMissingPy, &via) == "python3" && via == "default",
              "a python_path that does not exist falls through to python3, not a hard failure");

        // nothing configured: "python3"
        check(resolvePythonExe(kNoConfig, &via) == "python3" && via == "default",
              "with neither env nor config, the interpreter is python3 (pre-ADR-032 default)");
    }

    scratch.deleteRecursively();

    std::printf(failures == 0 ? "\nAll PromptPanelPathResolution checks passed.\n"
                              : "\n%d PromptPanelPathResolution check(s) FAILED.\n", failures);
    std::printf("PF_SUMMARY harness=%s checks=%d failures=%d\n",
                "PromptPanelPathResolutionTest", checks, failures);
    return failures == 0 ? 0 : 1;
}
