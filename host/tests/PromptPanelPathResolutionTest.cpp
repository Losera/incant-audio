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
// This test exercises the fixed three-step resolveGenerateScript() directly —
// forward-declared below rather than exposed via PromptPanel.h, since nothing
// else needs to call it — so each step can be checked in isolation without
// constructing a PromptPanel/PluginForgeProcessor or pumping a message loop.
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

#include <cstdio>
#include <cstdlib>

// Defined with external linkage in PromptPanel.cpp exactly so this test can
// call it without pulling in the rest of PromptPanel's construction.
juce::File resolveGenerateScript(const juce::File& binaryDir, const juce::File& homeDir);

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

        auto resolved = resolveGenerateScript(emptyBinaryDir, emptyHomeDir);
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

        auto resolved = resolveGenerateScript(binDir, emptyHomeDir);
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
        auto resolved = resolveGenerateScript(emptyBinaryDir, emptyHomeDir);
        check(resolved == installed,
              "PF-065: an installed-but-unexported runtime is found via XDG_DATA_HOME");
    }

    // ── 4. PF-065: same, via the ~/.local/share default (XDG_DATA_HOME unset) ─
    {
        clearEnv();
        auto installed = emptyHomeDir.getChildFile(".local/share/pluginforge/llm/generate.py");
        installed.getParentDirectory().createDirectory();
        installed.replaceWithText("# installed default\n");

        auto resolved = resolveGenerateScript(emptyBinaryDir, emptyHomeDir);
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

        auto resolved = resolveGenerateScript(freshBinaryDir, freshHomeDir);
        check(! resolved.existsAsFile(),
              "with nothing found anywhere, the result stays an invalid/nonexistent File");
    }

    scratch.deleteRecursively();

    std::printf(failures == 0 ? "\nAll PromptPanelPathResolution checks passed.\n"
                              : "\n%d PromptPanelPathResolution check(s) FAILED.\n", failures);
    std::printf("PF_SUMMARY harness=%s checks=%d failures=%d\n",
                "PromptPanelPathResolutionTest", checks, failures);
    return failures == 0 ? 0 : 1;
}
