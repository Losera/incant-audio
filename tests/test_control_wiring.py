"""Verify that this project's declared controls are actually wired and can actually fail.

WHY THIS FILE EXISTS
--------------------
On 2026-07-25 all five of this project's enforcement hooks were found to have never
executed. `.claude/settings.json` declared `PreToolUse` at the file root; Claude Code
requires it nested under a top-level "hooks" key. A settings file with the wrong shape
is ignored silently — no warning, no error, every hook simply dead. The scripts
themselves were correct the whole time: `check_bash_denylist.py` returned exit 2 on its
red case when invoked by hand. Nothing ever invoked it.

Meanwhile CLAUDE.md, STATUS.md and a published service review all described the prompt
invariant as "hook-enforced".

That was the THIRD time this project mistook a declared control for a running one:

  1. PAIR mode — "a self-graded rubric that never returns a failure is not a control"
     (COLLABORATION.md §9)
  2. The ADR-009 sync hook — "it verified one sentence and one regex, and the team
     believed it guaranteed the two files could not drift. They had in fact drifted
     substantially" (check_prompt_invariants.py:10-17)
  3. The hooks themselves — this file.

So these tests assert two different things, and the second is the one that matters:

  * SHAPE  — settings.json is nested correctly and every referenced script exists.
  * TEETH  — each hook still exits 2 on a case that must be blocked, and exits 0 on an
             ordinary edit. A gate that cannot fail is not a gate, and a gate that
             fails on everything gets disabled by the first person it annoys.

A test that only checked shape would have the same defect as the sync hook it replaced:
confidence proportional to the proxy rather than to the invariant.

NOT COVERED, deliberately: that Claude Code actually dispatches these hooks at runtime.
Nothing in-process can prove that — it is verified by watching a hook block a real tool
call. Done 2026-07-25; see docs/research/ and the git history for this commit.
"""
import json
import subprocess
import sys
from pathlib import Path

import pytest

ROOT = Path(__file__).parent.parent
SETTINGS = ROOT / ".claude" / "settings.json"
HOOKS_DIR = ROOT / ".claude" / "hooks"

# Hook events that, if present, must carry a matcher-group array.
KNOWN_EVENTS = {
    "PreToolUse", "PostToolUse", "Notification", "Stop", "SubagentStop",
    "UserPromptSubmit", "PreCompact", "SessionStart", "SessionEnd",
}


def _settings() -> dict:
    return json.loads(SETTINGS.read_text())


def _run_hook(script: str, payload: dict) -> int:
    """Invoke a hook the way Claude Code does: JSON on stdin, meaning in the exit code."""
    proc = subprocess.run(
        [sys.executable, str(HOOKS_DIR / script)],
        input=json.dumps(payload), capture_output=True, text=True, timeout=60,
    )
    return proc.returncode


def _registered_commands() -> list[str]:
    out = []
    for groups in _settings()["hooks"].values():
        for group in groups:
            for handler in group.get("hooks", []):
                out.append(handler.get("command", ""))
    return out


# --------------------------------------------------------------------------- shape


class TestSettingsShape:
    """The exact defect found on 2026-07-25. Never again, silently."""

    def test_settings_file_parses(self):
        _settings()

    def test_has_top_level_hooks_key(self):
        s = _settings()
        assert "hooks" in s, (
            "`.claude/settings.json` has no top-level 'hooks' key. Claude Code ignores "
            "hook blocks that are not nested under it — SILENTLY. This is the exact "
            "shape bug that left all five hooks dead until 2026-07-25. Expected:\n"
            '  {"hooks": {"PreToolUse": [{"matcher": ..., "hooks": [...]}]}}'
        )

    def test_no_event_names_stranded_at_root(self):
        stranded = KNOWN_EVENTS & set(_settings())
        assert not stranded, (
            f"Hook event(s) {sorted(stranded)} sit at the root of settings.json. They "
            "must be nested under the top-level 'hooks' key or they will never fire."
        )

    def test_events_are_known(self):
        unknown = set(_settings()["hooks"]) - KNOWN_EVENTS
        assert not unknown, f"Unrecognised hook event(s): {sorted(unknown)}"

    def test_matcher_groups_are_well_formed(self):
        for event, groups in _settings()["hooks"].items():
            assert isinstance(groups, list), f"{event} must map to a list of matcher groups"
            for group in groups:
                assert "matcher" in group, f"{event}: matcher group has no 'matcher'"
                assert group.get("hooks"), f"{event}/{group.get('matcher')}: no handlers"
                for handler in group["hooks"]:
                    assert handler.get("type") == "command", "only 'command' handlers are used here"
                    assert handler.get("command"), "handler has an empty command"


class TestRegisteredScriptsExist:
    def test_every_referenced_script_is_on_disk(self):
        missing = [
            name for cmd in _registered_commands()
            for name in [cmd.split("/")[-1].rstrip('"')]
            if name.endswith(".py") and not (HOOKS_DIR / name).exists()
        ]
        assert not missing, f"settings.json registers missing hook script(s): {missing}"

    def test_no_orphaned_hook_scripts(self):
        """A hook on disk but not registered is dead weight in every session's context."""
        registered = {c.split("/")[-1].rstrip('"') for c in _registered_commands()}
        orphans = {p.name for p in HOOKS_DIR.glob("*.py")} - registered
        assert not orphans, (
            f"Hook script(s) on disk but registered nowhere: {sorted(orphans)}. "
            "Either register them or delete them — a retired hook still costs context."
        )


# --------------------------------------------------------------------------- teeth

CWD = str(ROOT)
PROMPT = str(ROOT / "llm" / "prompts" / "system_prompt.txt")
UNRELATED = str(ROOT / "README.md")

# Assembled at runtime so the literal trigger string never appears in this source file.
# It would otherwise be caught by check_bash_denylist itself when an agent greps or
# edits this file through a Bash command — which is exactly what happened while this
# test was being written.
WHOLE_TREE_ADD = "git " + "add " + "-A"


class TestHooksStillHaveTeeth:
    """Each hook must exit 2 on the case it exists to block."""

    def test_denylist_blocks_whole_tree_git_add(self):
        assert _run_hook("check_bash_denylist.py",
                         {"tool_name": "Bash", "tool_input": {"command": WHOLE_TREE_ADD}}) == 2

    def test_rt_safety_blocks_allocation_in_audio_path(self):
        code = ("void FaustEngine::process(float** x, int n) {\n"
                "  void* p = malloc(64);\n}\n")
        assert _run_hook("check_rt_safety.py", {
            "tool_name": "Write", "cwd": CWD,
            "tool_input": {"file_path": str(ROOT / "host/Source/FaustEngine.cpp"), "content": code},
        }) == 2

    def test_prompt_invariants_blocks_fabricated_stdlib_function(self):
        assert _run_hook("check_prompt_invariants.py", {
            "tool_name": "Write", "cwd": CWD,
            "tool_input": {"file_path": PROMPT,
                           "content": "Use ef.totally_fake_function for chorus.\n"},
        }) == 2

    # PF-015. The hook scoped FaustEngine::process and processBlock only, while the
    # audio thread actually runs four functions. `pushToFaust` MOVED onto the audio
    # thread with the PF-004 fix (efbb5a5) and was never added; OutputGuard::process
    # has been there since 91a5a89 and was never added either. So for weeks the
    # hook's coverage and the real audio path disagreed, and everything else in the
    # repo described that path as "hook-guarded".
    #
    # One red case per newly-scoped function. These are the assertions that would
    # have caught the gap.
    @pytest.mark.parametrize("source_file,code", [
        ("host/Source/ParamPool.cpp",
         "void ParamPool::pushToFaust(FaustEngine& e) {\n"
         "  float* p = new float[4];\n}\n"),
        ("host/Source/OutputGuard.cpp",
         "void OutputGuard::process(juce::AudioBuffer<float>& b) {\n"
         "  std::lock_guard<std::mutex> lock(m);\n}\n"),
    ])
    def test_rt_safety_covers_everything_reachable_from_process_block(self, source_file, code):
        assert _run_hook("check_rt_safety.py", {
            "tool_name": "Write", "cwd": CWD,
            "tool_input": {"file_path": str(ROOT / source_file), "content": code},
        }) == 2, (
            f"{source_file} runs on the audio thread (reachable from processBlock) "
            "but the RT-safety hook did not block an allocation/lock in it. See "
            "PF-015 and the scoped-function list in check_rt_safety.py's docstring."
        )


class TestHooksDoNotOverreach:
    """A gate that blocks ordinary work gets switched off, which is the same as dead."""

    @pytest.mark.parametrize("script", ["check_rt_safety.py", "check_prompt_invariants.py"])
    def test_unrelated_edit_passes(self, script):
        assert _run_hook(script, {
            "tool_name": "Edit", "cwd": CWD,
            "tool_input": {"file_path": UNRELATED, "new_string": "hello"},
        }) == 0

    def test_ordinary_git_command_passes(self):
        assert _run_hook("check_bash_denylist.py",
                         {"tool_name": "Bash", "tool_input": {"command": "git status --short"}}) == 0

    def test_scoped_git_add_passes(self):
        """The denylist must permit the explicit-path form it tells you to use instead."""
        assert _run_hook("check_bash_denylist.py", {
            "tool_name": "Bash", "tool_input": {"command": "git add docs/one.md docs/two.md"},
        }) == 0

    @pytest.mark.parametrize("source_file", [
        "host/Source/FaustEngine.cpp", "host/Source/PluginProcessor.cpp",
        "host/Source/ParamPool.cpp", "host/Source/OutputGuard.cpp",
    ])
    def test_the_real_audio_path_passes_its_own_hook(self, source_file):
        """If the committed audio path cannot satisfy the hook guarding it, the hook
        is unusable — and widening its scope (PF-015) is exactly when that breaks.

        This also pins the ANCHOR_RE's exclusions: ParamPool::remap and
        FaustEngine::runCompile legitimately allocate and lock on background
        threads, in the same files, a few lines away.
        """
        path = ROOT / source_file
        assert _run_hook("check_rt_safety.py", {
            "tool_name": "Write", "cwd": CWD,
            "tool_input": {"file_path": str(path), "content": path.read_text()},
        }) == 0

    def test_the_real_system_prompt_passes_its_own_invariant(self):
        """If the committed prompt cannot satisfy the hook guarding it, the hook is unusable."""
        assert _run_hook("check_prompt_invariants.py", {
            "tool_name": "Write", "cwd": CWD,
            "tool_input": {"file_path": PROMPT, "content": Path(PROMPT).read_text()},
        }) == 0
