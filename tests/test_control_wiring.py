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
import os
import re
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


def _run_hook_capture(script: str, payload: dict) -> subprocess.CompletedProcess:
    """Invoke a hook the way Claude Code does: JSON on stdin. Returns the full
    process so a caller can inspect stdout (e.g. additionalContext JSON), not just
    the exit code."""
    return subprocess.run(
        [sys.executable, str(HOOKS_DIR / script)],
        input=json.dumps(payload), capture_output=True, text=True, timeout=60,
    )


def _run_hook(script: str, payload: dict) -> int:
    """Invoke a hook the way Claude Code does: JSON on stdin, meaning in the exit code."""
    return _run_hook_capture(script, payload).returncode


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

    def test_no_literal_wildcard_matcher(self):
        """`"matcher": "*"` was written for handoff_injector.py / handoff_precompact.py
        (ADR-028) and would have silently never fired: the harness's own settings
        validator (confirmed 2026-08-21 by inspecting the installed 2.1.239 binary's
        Zod schema description strings, since the public docs claimed "*" works and
        were wrong) documents the matcher as "a tool name, pipe-separated list, or
        empty to match all" -- "*" is not a case it names, so it is compared literally
        against the event's matched field (a tool name, or a SessionStart source /
        PreCompact trigger) and matches nothing. Caught before commit, but exactly
        the "declared control that never ran" shape this file exists to prevent, so
        it gets a permanent case rather than trusting it was fixed once.
        """
        for event, groups in _settings()["hooks"].items():
            for group in groups:
                assert group.get("matcher") != "*", (
                    f"{event} hook uses matcher \"*\", which is not a wildcard in "
                    "Claude Code's own matcher syntax. Use \"\" (empty string) to "
                    "match every case, or omit the field."
                )
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


# ------------------------------------------------------------------ extension rot
#
# The FOURTH instance of the pattern in this file's docstring, found 2026-07-27:
# `.claude/skills/attention-report/SKILL.md` instructed every run to read
# CLAUDE.md's "Current status" section (deleted 2026-07-25) and
# docs/collaboration_log.md (retired in COLLABORATION.md §5, then deleted). Neither
# existed. Nothing said so — the skill just gathered less and reported confidently,
# which is worse than erroring, because a short report reads like good news. It also
# tagged every finding with a DELEGATE/PAIR/HUMAN-OWNED mode six days after §9
# retired that taxonomy.
#
# Hooks had `test_every_referenced_script_is_on_disk`. Skills, rules and agents had
# no equivalent, so their references rotted silently for weeks. This is that check.
#
# NOT COVERED: whether a referenced file still *says* what the extension assumes. A
# path can exist and its contents be wrong; only reading it catches that.

EXTENSION_DIRS = [ROOT / ".claude" / "skills", ROOT / ".claude" / "agents",
                  ROOT / ".claude" / "rules"]

# Repo-relative paths in backticks: `docs/foo.md`, `tools/bar.sh`, `host/Source/X.cpp`.
_PATH_RE = __import__("re").compile(
    r"`((?:docs|tools|llm|bench|host|tests|examples|\.claude)/[A-Za-z0-9_./*{}-]+)`"
)

# Retired by COLLABORATION.md revision 2 (2026-07-21). Historical citations are fine;
# these are the phrasings that read as live instructions.
RETIRED_MODE_RE = __import__("re").compile(
    r"\b(?:is|are|was|remains?|classified|treat(?:ed)? as)\s+(?:a\s+)?"
    r"(DELEGATE|PAIR|HUMAN-OWNED)\b"
)


# A missing path is only a defect if the extension tells someone to OPEN it. Two
# kinds of reference legitimately name a file that is not there: a historical note
# ("retired and deleted on ...") and an output the extension itself creates. Both
# are recognised by wording on the same line, so a genuine dead read cannot hide
# behind them by accident.
_EXEMPT_RE = __import__("re").compile(
    r"\b(?:deleted|retired|superseded|removed|no longer exists|used to|"
    r"create with|if absent|will be created|cautionary)\b",
    __import__("re").I,
)

_gitignore_cache: dict[str, bool] = {}


def _is_gitignored(ref: str) -> bool:
    """A third legitimate reason a repo-relative path can be absent: it's a real,
    documented build/runtime artifact (`host/build`, `host/JUCE`, a lock file) that
    .gitignore keeps out of every checkout, including CI's. Found 2026-08-19 when
    the widened live-doc check below passed on a dev machine that happened to have
    built the host and run a benchmark -- so the artifacts existed locally -- and
    then failed in CI's clean checkout, where they legitimately do not. A path this
    project chose to gitignore is not a stale reference; requiring it to exist
    would just move the false positive from "doc rot" to "not built yet."

    Second bug, same day, caught by reproducing against a fresh clone before re-
    pushing rather than trusting the first fix: `git check-ignore -q host/build`
    alone returns "not ignored" when `host/build` does not exist on disk, because
    the matching .gitignore pattern (`build/`) is directory-only and git will not
    treat a nonexistent path as a directory. It matched instantly on the machine
    that had actually built the host (a real `host/build/` on disk) and failed
    everywhere else -- the same "works on my machine" shape as the first bug, one
    layer deeper. `bench/results/.run.lock` has no trailing slash in its pattern,
    so it was never affected. Retrying with a trailing slash forces git to
    evaluate the path as a directory regardless of whether it currently exists.
    """
    if ref not in _gitignore_cache:
        ignored = any(
            subprocess.run(
                ["git", "check-ignore", "-q", candidate],
                cwd=str(ROOT), timeout=10,
            ).returncode == 0
            for candidate in (ref, ref + "/")
        )
        _gitignore_cache[ref] = ignored
    return _gitignore_cache[ref]


def _extension_files():
    for d in EXTENSION_DIRS:
        if d.exists():
            yield from sorted(d.rglob("*.md"))


class TestExtensionsDoNotReferenceDeletedFiles:
    """Skills, agents and rules must not instruct a reader to open something gone."""

    def test_referenced_repo_paths_exist(self):
        broken = []
        for f in _extension_files():
            for line in f.read_text().splitlines():
                if _EXEMPT_RE.search(line):
                    continue  # historical note, or a file this extension creates
                for ref in set(_PATH_RE.findall(line)):
                    if any(ch in ref for ch in "*{"):
                        continue  # a glob pattern, not a file reference
                    if not (ROOT / ref).exists() and not _is_gitignored(ref):
                        broken.append(f"{f.relative_to(ROOT)} -> {ref}")
        assert not broken, (
            "Extension(s) reference repo paths that do not exist:\n  "
            + "\n  ".join(sorted(broken))
            + "\nThis is how attention-report rotted: it kept reading deleted files and "
              "reported the shortfall as nothing to report."
        )

    def test_no_live_retired_mode_instructions(self):
        stale = []
        for f in _extension_files():
            for m in RETIRED_MODE_RE.finditer(f.read_text()):
                stale.append(f"{f.relative_to(ROOT)}: ...{m.group(0)}...")
        assert not stale, (
            "Extension(s) still instruct using the DELEGATE/PAIR/HUMAN-OWNED protocol "
            "retired by COLLABORATION.md §9 on 2026-07-21:\n  " + "\n  ".join(sorted(stale))
            + "\nGate on §2 (consequence) and §3 (evidence tier) instead. Citing the old "
              "modes as history is fine; instructing with them is not."
        )


# The extension-only scope above (added when this class was written) left every other
# markdown file free to rot silently -- which is exactly how INTERFACE.md's line
# citations, ADR-011's "Closed" row, and a dozen dead `docs/collaboration_log.md` /
# `bench/prompts/system_faust.txt` pointers went unnoticed until the 2026-08-19 audit
# (docs/records/doc-purge-2026-08-19.md). This widens the same two checks to a curated
# list of documents this project keeps continuously live -- deliberately NOT a blanket
# docs/ rglob: `docs/decisions.md`, `docs/architectural_decisions/ADR-*.md`,
# `docs/sessions/*.md`, and `docs/research/*.md` are append-only or point-in-time by
# design (COLLABORATION.md §8) and are allowed to cite things that later got deleted,
# same as git history. Add a file here only if it is meant to always be current.
LIVE_DOC_FILES = [
    ROOT / "CLAUDE.md",
    ROOT / "COLLABORATION.md",
    ROOT / "STATUS.md",
    ROOT / "docs" / "BUGS.md",
    ROOT / "PLUGIN_HEALTH_PLAN.md",
    ROOT / "INTERFACE.md",
    ROOT / "README.md",
]


class TestLiveDocsDoNotReferenceDeletedFiles:
    """The always-current documents get the same two checks as skills/agents/rules."""

    def test_referenced_repo_paths_exist(self):
        broken = []
        for f in LIVE_DOC_FILES:
            if not f.exists():
                continue
            for line in f.read_text().splitlines():
                if _EXEMPT_RE.search(line):
                    continue
                for ref in set(_PATH_RE.findall(line)):
                    if any(ch in ref for ch in "*{"):
                        continue
                    if ref == str(f.relative_to(ROOT)):
                        continue  # a doc naming its own path (e.g. a purge manifest row)
                    if not (ROOT / ref).exists() and not _is_gitignored(ref):
                        broken.append(f"{f.relative_to(ROOT)} -> {ref}")
        assert not broken, (
            "Live document(s) reference repo paths that do not exist:\n  "
            + "\n  ".join(sorted(broken))
            + "\nThese files are meant to stay continuously accurate, unlike docs/sessions, "
              "docs/research, or the ADR log, which are append-only/point-in-time by design."
        )

    def test_no_live_retired_mode_instructions(self):
        stale = []
        for f in LIVE_DOC_FILES:
            if not f.exists():
                continue
            for m in RETIRED_MODE_RE.finditer(f.read_text()):
                stale.append(f"{f.relative_to(ROOT)}: ...{m.group(0)}...")
        assert not stale, (
            "Live document(s) still instruct using the DELEGATE/PAIR/HUMAN-OWNED protocol "
            "retired by COLLABORATION.md §9 on 2026-07-21:\n  " + "\n  ".join(sorted(stale))
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

    # Every form must block, because a rule that catches one spelling of a date and
    # not another is worse than none: it teaches you the rule is enforced while
    # letting the next variant through. The "today" case is here specifically
    # because it did NOT block when first written -- the day-word pattern anchored
    # on a separator or end-of-string, and ".md" is neither.
    @pytest.mark.parametrize("name", [
        "session_plan_2026-09-09.md",   # full date
        "impl_plan_2026-09.md",         # year and month
        "notes_2026_09_09.md",          # underscore separators
        "plan_20260909.md",             # compact, no separators
        "test_plan_today.md",           # relative day word
        "tonight.md",                   # relative day word, whole stem
    ])
    def test_doc_naming_blocks_a_dated_new_document(self, name):
        assert _run_hook("check_doc_naming.py", {
            "tool_name": "Write", "cwd": CWD,
            "tool_input": {"file_path": str(ROOT / "docs" / name), "content": "x"},
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

    # The exemptions are the reason this hook is usable at all, so they are pinned
    # as tightly as the blocks. A gate that refused to let you write an archived
    # benchmark run, or to edit a dated document that already exists, would be
    # switched off within a day -- and a switched-off gate is a dead one.
    @pytest.mark.parametrize("relpath, why", [
        ("docs/sessions/003-playground-shell.md", "a session-numbered doc is the prescribed form"),
        ("docs/records/review_2026-09-09.md",     "a point-in-time record may be dated"),
        ("bench/results/run_2026-09-09.json",     "an archived run is a record, not a document"),
        ("docs/polyphony_design.md",              "an ordinary new doc is untouched"),
        ("docs/adr_021_v2.md",                    "a version number is not a date"),
    ])
    def test_doc_naming_permits(self, relpath, why):
        assert _run_hook("check_doc_naming.py", {
            "tool_name": "Write", "cwd": CWD,
            "tool_input": {"file_path": str(ROOT / relpath), "content": "x"},
        }) == 0, why

    def test_doc_naming_leaves_existing_dated_files_editable(self):
        """Five dated documents already exist and are staying. Rewriting one must not
        be blocked, or the historical record becomes read-only for no benefit."""
        existing = ROOT / "docs" / "architecture_review_2026-07-21.md"
        assert existing.exists(), "fixture moved; pick another committed dated doc"
        assert _run_hook("check_doc_naming.py", {
            "tool_name": "Write", "cwd": CWD,
            "tool_input": {"file_path": str(existing), "content": "x"},
        }) == 0

    def test_the_real_system_prompt_passes_its_own_invariant(self):
        """If the committed prompt cannot satisfy the hook guarding it, the hook is unusable."""
        assert _run_hook("check_prompt_invariants.py", {
            "tool_name": "Write", "cwd": CWD,
            "tool_input": {"file_path": PROMPT, "content": Path(PROMPT).read_text()},
        }) == 0


# ------------------------------------------------------------- handoff (ADR-028)


HANDOFF_PATH = ROOT / ".claude" / "HANDOFF.md"
HANDOFF_STATE_PATH = ROOT / ".claude" / "handoff-state.json"
HANDOFF_SKILL = ROOT / ".claude" / "skills" / "handoff" / "SKILL.md"


def _current_head() -> str:
    out = subprocess.run(["git", "rev-parse", "HEAD"], cwd=str(ROOT),
                          capture_output=True, text=True, timeout=10)
    assert out.returncode == 0, "test needs a real git HEAD to run against"
    return out.stdout.strip()


def _additional_context(proc: subprocess.CompletedProcess) -> str:
    payload = json.loads(proc.stdout)
    return payload["hookSpecificOutput"]["additionalContext"]


class TestHandoffHasTeeth:
    """The re-injection guard must never be silent, and the compaction snapshot must
    never overwrite a real handoff. Neither hook can BLOCK a session or a compaction
    (no decision control exists for SessionStart/PreCompact) — so "teeth" here means
    the honest-signal cases: a missing handoff says so explicitly, staleness is
    flagged, and the machine-written safety net stays out of the human-written file's
    way.
    """

    @pytest.fixture
    def clean_handoff(self):
        """Yield only if no real handoff/state is on disk; always leave the tree as
        found. Mirrors TestBenchmarkConcurrencyGuard.free_lock — do not clobber
        another session's in-progress state to run a test."""
        if HANDOFF_PATH.exists() or HANDOFF_STATE_PATH.exists():
            pytest.skip("a real .claude/HANDOFF.md or handoff-state.json is on disk")
        yield
        HANDOFF_PATH.unlink(missing_ok=True)
        HANDOFF_STATE_PATH.unlink(missing_ok=True)

    def test_injector_reports_no_handoff_on_disk(self, clean_handoff):
        """The attention-report failure mode this project already knows: a short or
        empty signal must never be read as good news. See orient/SKILL.md's own
        warning about exactly this."""
        proc = _run_hook_capture("handoff_injector.py", {
            "hook_event_name": "SessionStart", "source": "clear", "cwd": CWD,
        })
        assert proc.returncode == 0
        assert "NO HANDOFF ON DISK" in _additional_context(proc)

    def test_injector_flags_a_stale_head(self, clean_handoff):
        HANDOFF_PATH.write_text(
            "# Handoff\n\n"
            "<!-- handoff-meta: head=0000000000000000000000000000000000000000 "
            "written=2020-01-01T00:00:00Z branch=nowhere -->\n"
            "OBJECTIVE test fixture only\n"
        )
        proc = _run_hook_capture("handoff_injector.py", {
            "hook_event_name": "SessionStart", "source": "resume", "cwd": CWD,
        })
        assert proc.returncode == 0
        ctx = _additional_context(proc)
        assert "STALENESS WARNING" in ctx, (
            "A handoff recording a HEAD that no longer matches the repo must be "
            f"flagged, not presented as current. Got:\n{ctx}"
        )

    def test_precompact_leaves_handoff_byte_identical(self, clean_handoff):
        """The one that matters most: this pins the prose/state separation as a
        mechanism, not a comment. A script has no access to the agent's reasoning,
        so it must never overwrite a real handoff with a machine-generated stub."""
        sentinel = "# Handoff\n\nOBJECTIVE sentinel content, must not be touched\n"
        HANDOFF_PATH.write_text(sentinel)
        proc = _run_hook_capture("handoff_precompact.py", {
            "hook_event_name": "PreCompact", "trigger": "auto", "cwd": CWD,
            "session_id": "test-session",
        })
        assert proc.returncode == 0
        assert HANDOFF_PATH.read_text() == sentinel, (
            "handoff_precompact.py modified .claude/HANDOFF.md. It must only ever "
            "write .claude/handoff-state.json — see its module docstring."
        )
        assert HANDOFF_STATE_PATH.exists()
        state = json.loads(HANDOFF_STATE_PATH.read_text())
        assert state["trigger"] == "auto"

    def test_handoff_skill_declares_its_own_name(self):
        """Mirrors test_orient_skill_declares_its_own_name. Discovered 2026-08-19 for
        /orient: a frontmatter-less same-named global skill can silently win the
        slot. A missing/wrong `name:` field here is the cheap half of that failure
        this repo controls."""
        assert HANDOFF_SKILL.exists(), "the handoff skill is gone; nothing writes the doc"
        body = HANDOFF_SKILL.read_text()
        assert body.startswith("---\n"), (
            "handoff/SKILL.md has no YAML frontmatter block -- the exact shape that "
            "let a same-named global skill shadow /orient on 2026-08-19."
        )
        frontmatter = body.split("---\n", 2)[1]
        assert re.search(r"^name:\s*handoff\s*$", frontmatter, re.MULTILINE), (
            "handoff/SKILL.md's frontmatter does not declare `name: handoff`."
        )


class TestHandoffDoesNotOverreach:
    """A gate that blocks ordinary work gets switched off, which is the same as
    dead — and here that would mean blocking a session from starting or a
    compaction from completing, either of which is much worse than a merely
    annoying gate."""

    @pytest.fixture
    def clean_handoff(self):
        if HANDOFF_PATH.exists() or HANDOFF_STATE_PATH.exists():
            pytest.skip("a real .claude/HANDOFF.md or handoff-state.json is on disk")
        yield
        HANDOFF_PATH.unlink(missing_ok=True)
        HANDOFF_STATE_PATH.unlink(missing_ok=True)

    def test_fresh_handoff_raises_no_staleness_banner(self, clean_handoff):
        head = _current_head()
        HANDOFF_PATH.write_text(
            f"# Handoff\n\n<!-- handoff-meta: head={head} "
            "written=2026-01-01T00:00:00Z branch=test -->\n"
            "OBJECTIVE test fixture only\n"
        )
        proc = _run_hook_capture("handoff_injector.py", {
            "hook_event_name": "SessionStart", "source": "clear", "cwd": CWD,
        })
        assert proc.returncode == 0
        ctx = _additional_context(proc)
        assert "STALENESS WARNING" not in ctx, f"A fresh, matching-HEAD handoff was flagged stale:\n{ctx}"
        assert "NO HANDOFF ON DISK" not in ctx

    @pytest.mark.parametrize("source", ["startup", "resume", "clear", "compact"])
    def test_injector_never_blocks_any_session_start_source(self, source, clean_handoff):
        assert _run_hook("handoff_injector.py", {
            "hook_event_name": "SessionStart", "source": source, "cwd": CWD,
        }) == 0

    @pytest.mark.parametrize("trigger", ["auto", "manual"])
    def test_precompact_never_blocks_any_trigger(self, trigger, clean_handoff):
        assert _run_hook("handoff_precompact.py", {
            "hook_event_name": "PreCompact", "trigger": trigger, "cwd": CWD,
            "session_id": "test-session",
        }) == 0


# ------------------------------------------------------- benchmark concurrency (PF-025)


class TestBenchmarkConcurrencyGuard:
    """PF-025. Two benchmark runs at once is a data-loss bug, and it has fired twice.

    2026-07-21: two --dry-run invocations overwrote a committed 25-record run.
    2026-07-27: two full groq runs executed concurrently, launched minutes apart by two
    agents that could not see each other. Both were headed for the same results.json,
    and they shared one free-tier token budget -- the second took HTTP 429 (TPM limit
    8000) on its first prompt after five retries, which classify_failures files as
    `transport`. A measurement corrupted by the collision, not by the model.

    The guard is asymmetric on purpose, and so are these tests: the REFUSE path is
    exercised end-to-end through the CLI (it costs nothing -- the lock is checked
    before any generation), while the ALLOW path is asserted at function level,
    because proving it by running the harness would spend 25 prompts of quota.
    """

    @staticmethod
    def _bench():
        sys.path.insert(0, str(ROOT / "bench"))
        import run_benchmark
        return run_benchmark

    @pytest.fixture
    def free_lock(self):
        """Yield only if no real benchmark holds the lock; always leave it as found."""
        rb = self._bench()
        if rb.LOCK_FILE.exists():
            pytest.skip(f"a real benchmark run holds {rb.LOCK_FILE}")
        yield rb
        rb.LOCK_FILE.unlink(missing_ok=True)

    def test_second_concurrent_run_refuses_with_exit_2(self, free_lock):
        """The red case. A live holder must stop the second run before it generates."""
        rb = free_lock
        rb.acquire_lock()  # this process is the live holder
        proc = subprocess.run(
            [sys.executable, str(ROOT / "bench" / "run_benchmark.py"), "--provider", "groq"],
            capture_output=True, text=True, timeout=120, cwd=str(ROOT),
        )
        assert proc.returncode == 2, (
            "A second concurrent benchmark run was NOT refused. Two runs share one "
            "results.json and one provider rate limit — see PF-025.\n"
            f"stdout: {proc.stdout[-400:]}\nstderr: {proc.stderr[-400:]}"
        )
        assert str(os.getpid()) in proc.stderr, (
            "The refusal must name the holding pid — an unactionable lock message is "
            "how a guard gets deleted by the first person it blocks."
        )

    def test_stale_lock_is_reclaimed_not_latched(self, free_lock):
        """A guard that stays latched after a crash gets switched off, i.e. is dead."""
        rb = free_lock
        dead_pid = 2 ** 22  # far above /proc/sys/kernel/pid_max; cannot be running
        assert not rb._pid_alive(dead_pid)
        rb.LOCK_FILE.write_text(json.dumps({"pid": dead_pid, "started": "whenever"}))
        rb.acquire_lock()
        assert json.loads(rb.LOCK_FILE.read_text())["pid"] == os.getpid()

    def test_lock_is_free_when_no_run_is_active(self, free_lock):
        """The overreach counterpart: an ordinary run must not be blocked."""
        rb = free_lock
        rb.acquire_lock()
        rb.release_lock()
        assert not rb.LOCK_FILE.exists()
        rb.acquire_lock()  # a second sequential run acquires cleanly

    def test_corrupt_lock_does_not_wedge_the_harness(self, free_lock):
        """An unparseable lock is reclaimed, not treated as a permanent holder."""
        rb = free_lock
        rb.LOCK_FILE.write_text("{ this is not json")
        rb.acquire_lock()
        assert json.loads(rb.LOCK_FILE.read_text())["pid"] == os.getpid()

    def test_every_real_run_writes_a_dated_archive(self):
        """results.json alone is not an archive — the next run overwrites it.

        Asserted against the source rather than by running the harness: this is the
        line that made today's `cp results.json results_..._baseline.json` step
        unnecessary, and it must not quietly regress to a single output file.
        """
        src = (ROOT / "bench" / "run_benchmark.py").read_text()
        assert 'f"results_{stamp}_{tag}.json"' in src, (
            "run_benchmark.py no longer writes a per-run dated archive. Without it, "
            "two sequential runs silently overwrite each other's evidence (PF-025)."
        )


# ------------------------------------------------------------- CI reporting (digest)


DIGEST = ROOT / "tools" / "status_digest.sh"
ORIENT = ROOT / ".claude" / "skills" / "orient" / "SKILL.md"


def _head_sha() -> str:
    return subprocess.run(
        ["git", "rev-parse", "HEAD"], capture_output=True, text=True, cwd=str(ROOT), timeout=10
    ).stdout.strip()


def _older_sha(n: int = 3) -> str:
    out = subprocess.run(
        ["git", "rev-parse", f"HEAD~{n}"], capture_output=True, text=True, cwd=str(ROOT), timeout=10
    )
    if out.returncode != 0:
        pytest.skip(f"repository has fewer than {n + 1} commits")
    return out.stdout.strip()


def _ci_run(sha: str, conclusion: str = "failure", status: str = "completed", rid: int = 1) -> dict:
    return {
        "databaseId": rid, "status": status, "conclusion": conclusion, "headSha": sha,
        "createdAt": "2026-07-28T10:00:00Z", "workflowName": "Tests", "event": "push",
        "url": f"https://example.invalid/runs/{rid}",
    }


def _digest(ci_json: str) -> subprocess.CompletedProcess:
    """Run the digest with the CI seam set, so no test ever touches the network."""
    env = {**os.environ, "PLUGINFORGE_CI_RUNS_JSON": ci_json}
    return subprocess.run(
        ["bash", str(DIGEST)], capture_output=True, text=True, timeout=120, cwd=str(ROOT), env=env,
    )


class TestDigestReportsCI:
    """The digest must never open without saying something about the remote gate.

    Found 2026-07-28. CI had been red on four consecutive pushes since 2026-07-26 --
    `host/tests/OfflineRenderTest.cpp:372` constructs the processor, whose APVTS ctor
    calls startTimerHz and trips the JUCE assertion at juce_Timer.cpp:336 on a headless
    runner, exit 132. Local `check.sh full` ran the same tests and passed, because a dev
    session supplies what the runner does not. So the read half of the loop said green,
    the unread half said red, and STATUS.md's Broken list held one item that was neither.

    That is this project's signature defect one level up: not a control that was wrong,
    a control that reported to nobody. Same family as the five hooks that never fired and
    the CI that was green 17 commits behind.

    The invariant under test is NOT "CI is green" -- the digest has no opinion about
    that. It is that **every reachable state prints a section**: red prints a banner,
    green-on-an-older-commit prints a banner, and unreachable prints a banner naming why.
    Silence is the only forbidden output, because a short digest reads like good news --
    the exact inference that made the deleted `attention-report` skill useless.

    NOT COVERED: that `gh` reports GitHub truthfully, and that /orient is actually run at
    session start. The first is trusted; the second is a human habit, not a mechanism.
    """

    # ------------------------------------------------------------------ teeth

    def test_red_ci_is_announced(self):
        """The red case. A failing run at HEAD must produce an unmissable banner."""
        out = _digest(json.dumps([_ci_run(_head_sha(), "failure")])).stdout
        assert "CI IS RED" in out, (
            "The digest did not announce a failing CI run. This is the defect the "
            "section exists for: four red pushes were invisible to every artifact in "
            f"the loop on 2026-07-28.\n---\n{out[:800]}"
        )
        assert "--log-failed" in out, (
            "The red banner must name the command that shows the failure. A banner with "
            "no next step is how a warning becomes wallpaper."
        )

    def test_consecutive_failures_are_counted(self):
        """One red run is a flake; four in a row is a broken branch. Say which."""
        sha = _head_sha()
        runs = [_ci_run(sha, "failure", rid=i) for i in range(4)]
        out = _digest(json.dumps(runs)).stdout
        assert "4 consecutive" in out, f"Streak length was not reported.\n---\n{out[:800]}"

    def test_green_on_an_older_commit_is_not_reported_as_a_pass(self):
        """`CI green but 17 commits behind` is a defect this project has already had.

        A success on a commit that is not HEAD is evidence about that commit only.
        """
        out = _digest(json.dumps([_ci_run(_older_sha(3), "success")])).stdout
        assert "green on a commit that is not HEAD" in out, (
            "A stale-green run was reported without the caveat that HEAD is untested.\n"
            f"---\n{out[:800]}"
        )
        assert "3 commit(s)" in out, "The banner must quantify how far behind the tested commit is."

    @pytest.mark.parametrize(
        "seam, why",
        [
            ("{ this is not json", "malformed payload"),
            ("[]", "no runs for this branch"),
            ('{"runs": []}', "unexpected JSON shape"),
        ],
    )
    def test_unreachable_ci_is_loud_not_silent(self, seam, why):
        """Silence on a missing input is the defect that deleted `attention-report`."""
        out = _digest(seam).stdout
        assert "CI STATUS UNKNOWN" in out, (
            f"The digest went quiet when CI status could not be determined ({why}). "
            f"Unknown must never be indistinguishable from passing.\n---\n{out[:800]}"
        )

    @pytest.mark.parametrize(
        "seam",
        [
            "{ this is not json",
            "[]",
            json.dumps([_ci_run("0" * 40, "failure")]),
            json.dumps([_ci_run("0" * 40, "success")]),
            json.dumps([_ci_run("0" * 40, None, status="in_progress")]),
        ],
    )
    def test_a_ci_section_is_always_present(self, seam):
        """The load-bearing invariant: no reachable state produces no CI section."""
        out = _digest(seam).stdout
        assert "### CI" in out, (
            f"No CI section was printed at all for seam {seam[:60]!r}. Every other "
            "assertion in this class is secondary to this one."
        )

    # ------------------------------------------------------------- no overreach

    def test_green_at_head_raises_no_alarm(self):
        """A gate that cries wolf on the good case gets deleted by the first reader."""
        out = _digest(json.dumps([_ci_run(_head_sha(), "success")])).stdout
        assert "### CI" in out and "SUCCESS" in out
        assert "CI IS RED" not in out, "The digest alarmed on a passing run at HEAD."
        assert "UNKNOWN" not in out, "A clean pass was reported as indeterminate."
        assert "not HEAD" not in out, "A run at HEAD was reported as behind HEAD."

    def test_ci_trouble_does_not_swallow_the_rest_of_the_digest(self):
        """The CI block runs in its own interpreter for exactly this reason."""
        out = _digest("{ this is not json").stdout
        assert "Assumed, never checked" in out, (
            "An unreadable CI status suppressed the STATUS.md sections below it. The "
            "digest's primary job must survive its newest section failing."
        )

    def test_unknown_ci_does_not_claim_the_digest_is_incomplete(self):
        """Exit codes mean one thing here: STATUS.md no longer has the shape we read.

        Overloading them with `you are not logged into gh` would make the real signal
        ignorable on any machine without the CLI.
        """
        out = _digest("[]").stdout
        assert "DIGEST INCOMPLETE" not in out, (
            "An unknown CI status was escalated to DIGEST INCOMPLETE, which instructs "
            "the reader to go read STATUS.md directly — a different and wrong remedy."
        )

    # ----------------------------------------------------------- delivery path

    def test_orient_actually_runs_the_digest(self):
        """A CI line nobody reads is the bug, not the fix.

        `/orient` is the only consumer. If the skill stops injecting the script, the
        section still works and still reaches no one.
        """
        assert ORIENT.exists(), "the orient skill is gone; the digest has no reader"
        body = ORIENT.read_text()
        assert "status_digest.sh" in body, (
            "The orient skill no longer invokes tools/status_digest.sh. Whatever the "
            "digest prints now reaches nobody at session start."
        )
        assert "!`" in body, (
            "The orient skill references the digest but no longer injects its output "
            "with !`...`, so the CI status is described rather than computed."
        )

    def test_orient_skill_declares_its_own_name(self):
        """A frontmatter-less or misnamed skill can be silently shadowed.

        Discovered 2026-08-19: `~/.claude/skills/orient/SKILL.md` (global, a different
        project's skill) had NO YAML frontmatter at all -- no `name:` field -- and won the
        `/orient` slot anyway, because the harness fell back to deriving a name from the
        file's first line. Every session that ran `/orient` under that collision got that
        other project's rules injected instead of this file's digest, and
        `test_orient_actually_runs_the_digest` above could not have caught it: it only
        reads THIS file, which was correct the whole time. This test is still not
        sufficient on its own -- a same-named collision elsewhere would not be caught by
        anything in this repo -- but a missing/wrong `name:` field here is the specific,
        cheap-to-check half of that failure this project controls.
        """
        assert ORIENT.exists(), "the orient skill is gone; the digest has no reader"
        body = ORIENT.read_text()
        assert body.startswith("---\n"), (
            "orient/SKILL.md has no YAML frontmatter block -- this is exactly the shape "
            "that let a same-named global skill shadow it silently on 2026-08-19."
        )
        frontmatter = body.split("---\n", 2)[1]
        assert re.search(r"^name:\s*orient\s*$", frontmatter, re.MULTILINE), (
            "orient/SKILL.md's frontmatter does not declare `name: orient` -- without an "
            "explicit, correct name a same-named skill anywhere else (global or another "
            "project) can win the slot instead."
        )


# ------------------------------------------------ prose about mechanisms (COLLABORATION)


COLLAB = ROOT / "COLLABORATION.md"
STATUS = ROOT / "STATUS.md"


class TestHookTableMatchesReality:
    """COLLABORATION.md §7 tabulates what is enforcing. It must not be able to lie.

    Found 2026-07-28. The table listed `check_adr009_prompt_sync.py` and
    `protect_human_owned.py` -- retired in cf1d8e8, neither on disk -- and omitted
    `check_prompt_invariants.py`, which was registered in settings.json and running. Two
    of three live rows wrong, in the one section whose job is telling a reader what is
    actually enforced, six days after that section was last revised.

    This project's signature defect is a claim that outlives the thing it described. The
    prompt already lives under the right rule -- it cannot name a Faust function that does
    not resolve. This test applies that rule to prose about mechanisms: §7 names exactly
    the hooks settings.json registers, and every hook it calls retired is really gone.

    NOT COVERED: whether each row's *description* is accurate. A hook can be listed
    correctly and described wrongly; only reading the docstring catches that.
    """

    @staticmethod
    def _section_seven() -> str:
        body = COLLAB.read_text()
        m = re.search(r"^##\s*7\.\s.*?$(.*?)(?=^##\s\d)", body, re.S | re.M)
        assert m, "COLLABORATION.md no longer has a `## 7.` section to check"
        return m.group(1)

    @staticmethod
    def _table_scripts(section: str) -> set[str]:
        """Only the table's first column -- prose elsewhere in §7 names retired hooks."""
        found = set()
        for line in section.splitlines():
            if not line.lstrip().startswith("|"):
                continue
            first_cell = line.split("|")[1] if line.count("|") >= 2 else ""
            found.update(re.findall(r"`([\w.]+\.py)`", first_cell))
        return found

    def test_table_names_exactly_the_registered_hooks(self):
        tabled = self._table_scripts(self._section_seven())
        registered = {
            name for cmd in _registered_commands()
            for name in re.findall(r"([\w.]+\.py)", cmd)
        }
        assert tabled == registered, (
            "COLLABORATION.md §7's hook table and .claude/settings.json disagree.\n"
            f"  in the table, not registered: {sorted(tabled - registered) or 'none'}\n"
            f"  registered, not in the table: {sorted(registered - tabled) or 'none'}\n"
            "A reader trusts §7 to say what is enforcing. On 2026-07-28 it was wrong "
            "about two of three rows."
        )

    def test_hooks_called_retired_are_actually_gone(self):
        """The other half: a hook named as retired must not still be on disk."""
        section = self._section_seven()
        m = re.search(r"\*\*Retired.*?\*\*(.*?)(?=\n###|\Z)", section, re.S)
        assert m, "§7 no longer records which hooks were retired and why"
        for script in re.findall(r"`([\w.]+\.py)`", m.group(1)):
            assert not (HOOKS_DIR / script).exists(), (
                f"§7 calls {script} retired, but it is still in .claude/hooks/. Either "
                "it is live and belongs in the table, or it should be deleted."
            )

    def test_every_hook_on_disk_appears_in_the_table(self):
        """Closes the loop: no hook can exist, run, and go undocumented."""
        on_disk = {p.name for p in HOOKS_DIR.glob("*.py")}
        tabled = self._table_scripts(self._section_seven())
        assert not (on_disk - tabled), (
            f"Hook script(s) {sorted(on_disk - tabled)} exist but are absent from "
            "COLLABORATION.md §7's table."
        )


class TestStatusReservesAnEvidenceSlot:
    """`Next three things` must keep one slot that only a measurement can fill.

    Found 2026-07-28. All 18 closed defects were code defects, most closed within one to
    three days. All six *evidence* defects -- PF-009 through PF-014, filed 2026-07-23 --
    were still open five days later, and those six ARE the "Assumed, never checked" list.
    The project's one metric was made entirely of the work that never won a slot, because
    a list ranked by urgency will never schedule work whose defining property is that it
    is not urgent.

    So one of the three carries the literal tag `*(evidence)*` (COLLABORATION.md §5).

    NOT COVERED: that the tagged item is genuinely an evidence item rather than a code
    task wearing the tag. Nothing mechanical can tell the difference -- this test makes
    the slot exist, not honest.
    """

    @staticmethod
    def _next_three() -> str:
        m = re.search(
            r"^##+\s*Next three.*?$(.*?)(?=^##\s|\Z)", STATUS.read_text(), re.S | re.M
        )
        assert m, "STATUS.md has no `## Next three things` section"
        return m.group(1)

    def test_there_are_exactly_three(self):
        # Top-level items only: no leading whitespace, so a numbered sub-list inside an
        # item is not counted. The tag may precede the bold title, so do not require `**`.
        body = self._next_three()
        items = re.findall(r"^(\d+)\.\s+\S", body, re.M)
        assert len(items) == 3, (
            f"`Next three things` lists {len(items)} items, not three. It is not a "
            "backlog (COLLABORATION.md §5)."
        )

    def test_one_slot_is_reserved_for_evidence(self):
        body = self._next_three()
        tagged = [ln for ln in body.splitlines() if "*(evidence)*" in ln]
        assert tagged, (
            "None of the `Next three things` carries the `*(evidence)*` tag. One of the "
            "three is reserved for work that moves a claim out of 'Assumed, never "
            "checked' — the metric closed 0 of 6 such items in the five days before this "
            "rule existed (COLLABORATION.md §5)."
        )


# ---------------------------------------------------------------------------
CHECK_SH = ROOT / "tools" / "check.sh"
WORKFLOW = ROOT / ".github" / "workflows" / "test.yml"


class TestLadderRunsWhatCIRuns:
    """`check.sh full` must not be green about a smaller set of tests than CI runs.

    PF-029, found 2026-07-28. `tools/check.sh` built four targets and ran one. CI
    additionally built and ran OfflineRenderTest and PromptPanelThreadingTest. So the
    ladder and the remote gate were measuring different things, and the ladder was the
    half anyone actually looked at: four consecutive pushes read as green locally while
    CI died at exit 132 inside a harness the ladder had never executed. The read half
    of the loop said green, the unread half said red, and the two never met (PF-026).

    This is the project's signature defect -- CLAUDE.md names the class in check.sh's
    own header, "believing a control runs when it does not." The specific instance here
    is subtler than a dead hook: every check was real and every check passed. The lie
    was in the scope.

    So the invariant is a RELATION, not a list: whatever behavioural harness CI decides
    to run, the ladder runs it too. Hard-coding two names here would go stale the first
    time someone adds a third harness to the workflow and not to the ladder -- which is
    exactly the drift being guarded against.

    NOT COVERED: that the harnesses PASS. That is their own job, and OfflineRenderTest
    is currently expected to fail on the CI runner and pass locally (PF-027). This
    asserts only that the ladder is not structurally blind to them.
    """

    @staticmethod
    def _ci_harnesses() -> set[str]:
        """Targets CI builds in its 'Build behavioural test harnesses' step."""
        m = re.search(
            r"name:\s*Build behavioural test harnesses\s*\n\s*run:\s*(.+)", WORKFLOW.read_text()
        )
        assert m, (
            ".github/workflows/test.yml no longer has a 'Build behavioural test "
            "harnesses' step. If it was renamed, repoint this test; if the harnesses "
            "were dropped from CI, PF-029 needs rethinking, not deleting."
        )
        after_target = m.group(1).split("--target", 1)
        assert len(after_target) == 2, "the harness build step has no --target list"
        return {t for t in after_target[1].split() if t.endswith("Test")}

    def test_ci_actually_builds_some_harnesses(self):
        # Guards the guard: if the regex above ever matches an empty list, every
        # assertion below passes vacuously and this test class becomes decoration.
        assert self._ci_harnesses(), "parsed zero harness targets out of the workflow"

    def test_full_builds_every_harness_ci_builds(self):
        body = CHECK_SH.read_text()
        missing = sorted(t for t in self._ci_harnesses() if t not in body)
        assert not missing, (
            f"CI builds {missing} and tools/check.sh does not mention them. The ladder "
            "would be green about a smaller set of tests than the remote gate runs -- "
            "PF-029, which is how four red pushes read as green locally."
        )

    def test_full_also_runs_them_not_just_builds_them(self):
        # Building a harness and never running it is the same defect one step in:
        # a compile is not a test. Each harness must appear in a `run`/`skip` label
        # too, not only in the --target list.
        body = CHECK_SH.read_text()
        target_line = re.search(r"--target[^\n]*(?:\\\n[^\n]*)*", body)
        assert target_line, "tools/check.sh has no --target line"
        targets_only = target_line.group(0)
        for harness in sorted(self._ci_harnesses()):
            elsewhere = body.replace(targets_only, "")
            assert harness in elsewhere, (
                f"{harness} is built by tools/check.sh but never invoked -- it appears "
                "only in the --target list. A compile is not a test."
            )

    def test_recent_local_harnesses_are_not_omitted_from_ci(self):
        ci = WORKFLOW.read_text()
        for harness in ("AuditionThreadingTest", "ValidationGateTest"):
            assert harness in self._ci_harnesses(), (
                f"{harness} runs in tools/check.sh full but is absent from CI's "
                "behavioural harness build list."
            )
            assert ci.count(harness) >= 2, (
                f"{harness} is named in CI's build list but has no run step."
            )

    def test_a_display_dependent_harness_is_skipped_not_hung(self):
        # PromptPanelThreadingTest constructs juce::Components and pumps the message
        # loop; with no display it HANGS rather than failing, which is why CI wraps it
        # in xvfb-run. A ladder that hangs for 300s teaches people to skip the level.
        body = CHECK_SH.read_text()
        assert "WAYLAND_DISPLAY" in body and "DISPLAY" in body, (
            "tools/check.sh does not check for a display before running the harness "
            "that needs one. Run headless it hangs instead of failing."
        )


# ---------------------------------------------------------------------------
class TestPfLdPreloadIsSetBeforeItIsRead:
    """GitHub step exports must precede every step that consumes them.

    PF-036's preload was attached to the pure-harness command, but its value was
    only written to ``$GITHUB_ENV`` by a later step. GitHub accepts an empty
    ``LD_PRELOAD``, so the workflow looked wired while the shim did no work.
    """

    VARIABLES = ("PF_LD_PRELOAD", "PF_LD_PRELOAD_TSAN")

    @staticmethod
    def _ordered_steps() -> list[tuple[str, str]]:
        parts = re.split(r"\n\s*-\s*name:\s*", WORKFLOW.read_text())
        steps = []
        for part in parts[1:]:
            name, _, body = part.partition("\n")
            steps.append((name.strip(), body))
        assert steps, "parsed no named steps from .github/workflows/test.yml"
        return steps

    @staticmethod
    def _reads(body: str, variable: str) -> bool:
        # The boundary prevents PF_LD_PRELOAD matching inside its _TSAN sibling.
        return re.search(rf"\$(?:\{{{variable}\}}|{variable}\b)", body) is not None

    def test_each_preload_export_precedes_every_consumer(self):
        steps = self._ordered_steps()
        for variable in self.VARIABLES:
            writers = [
                index
                for index, (_, body) in enumerate(steps)
                if re.search(rf"echo\s+[^\n]*\b{variable}=.*GITHUB_ENV", body)
            ]
            assert len(writers) == 1, (
                f"expected exactly one $GITHUB_ENV writer for {variable}, got "
                f"{len(writers)}"
            )
            writer = writers[0]
            early_readers = [
                name
                for index, (name, body) in enumerate(steps)
                if index < writer and self._reads(body, variable)
            ]
            assert not early_readers, (
                f"{variable} is consumed before its $GITHUB_ENV writer by "
                f"{early_readers}. GitHub makes an empty LD_PRELOAD valid, so the "
                "PF-036 shim would be silently inert."
            )


class TestLadderOnlyStepsAreDeliberate:
    """Important full-ladder checks must run in CI or be explicitly exempted."""

    REQUIRED_IN_CI = (
        "tools/gen_voice_contract.py --check",
        "tools/gen_presentation_prompt.py --check",
        "tests/test_prompt_claims.py::TestClaimsHoldAgainstTheCompiler",
        "tests/test_pitch_gate.py -q -m integration",
    )
    LOCAL_ONLY = {
        # Reporting-only quality metric: --report never gates and always exits 0.
        "bench/presentation_checker.py --report",
    }

    def test_important_full_checks_are_classified(self):
        ladder = CHECK_SH.read_text()
        for marker in (*self.REQUIRED_IN_CI, *self.LOCAL_ONLY):
            assert marker in ladder, (
                f"classified full-ladder marker {marker!r} no longer appears in "
                "tools/check.sh; remove or update the classification deliberately"
            )

    def test_non_exempt_full_checks_also_run_in_ci(self):
        ci = WORKFLOW.read_text()
        missing = [marker for marker in self.REQUIRED_IN_CI if marker not in ci]
        assert not missing, (
            f"tools/check.sh full runs {missing}, but CI does not. Add them to CI "
            "or document a concrete local-only reason in LOCAL_ONLY."
        )


# ---------------------------------------------------------------------------
class TestEveryPromptIsGuarded:
    """Every file in llm/prompts/ must be covered by the prompt controls.

    Both guards used to name ONE path. `.claude/hooks/check_prompt_invariants.py`
    matched `llm/prompts/system_prompt.txt` exactly, and
    `tools/gen_stdlib_block.py --verify-prompt` read that same constant. So the
    day a second prompt arrived it would have been born UNGUARDED -- no ADR-009
    duplicate-`process` check, no stdlib resolution, no foreign-construct scan --
    while every gate stayed green and the file looked covered because its sibling
    was.

    Phase 1 adds llm/prompts/instrument_prompt.txt. This asserts the COVERAGE
    RELATION rather than the presence of a path, so the new file is guarded by
    existing to be found, not by someone remembering to add it.

    NOT COVERED: that the guards are correct, only that they look at every file.
    """

    PROMPT_DIR = ROOT / "llm" / "prompts"
    HOOK = ROOT / ".claude" / "hooks" / "check_prompt_invariants.py"

    @classmethod
    def _prompts(cls) -> list:
        return sorted(cls.PROMPT_DIR.glob("*.txt"))

    def test_there_is_at_least_one_prompt(self):
        # Guards the guard: an empty glob would pass everything below vacuously.
        assert self._prompts(), f"no *.txt found in {self.PROMPT_DIR}"

    def test_the_hook_matches_every_prompt_file(self):
        import importlib.util

        spec = importlib.util.spec_from_file_location("_pf_hook", self.HOOK)
        hook = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(hook)

        unmatched = [
            p.name for p in self._prompts()
            if not hook.PROMPT_RE.search(p.relative_to(ROOT).as_posix())
        ]
        assert not unmatched, (
            f"check_prompt_invariants.py does not match {unmatched}. Those prompts "
            "can be edited with no invariant check at all -- the hook would be "
            "decoration for them while passing for their siblings."
        )

    @pytest.mark.skipif(
        not Path("/usr/share/faust/stdfaust.lib").exists(),
        reason="Faust standard library not installed (the CI Python job has no "
               "compiler by design -- see .github/workflows/test.yml)",
    )
    def test_verify_prompt_covers_every_prompt_file(self):
        # The ladder runs `--verify-prompt` with no argument; that invocation must
        # reach every prompt, or check.sh full is green about a subset.
        #
        # ⚠️ NEEDS FAUST, and the skip above is not optional. Written without it,
        # this test passed locally and failed CI run 30687159048: it shells out to
        # gen_stdlib_block.py, which reads the REAL library by design, while the
        # CI Python job installs no compiler ("no API key or faust compiler
        # required"). That is PF-029 in the direction the ladder cannot see -- a
        # green local gate making a claim the remote gate cannot evaluate. The
        # sibling assertions above need no compiler and stay unguarded.
        import subprocess

        out = subprocess.run(
            [sys.executable, str(ROOT / "tools" / "gen_stdlib_block.py"), "--verify-prompt"],
            capture_output=True, text=True, cwd=ROOT,
        )
        assert out.returncode == 0, f"--verify-prompt failed:\n{out.stderr}"

        missing = [p.name for p in self._prompts() if p.name not in out.stdout]
        assert not missing, (
            f"`gen_stdlib_block.py --verify-prompt` never mentions {missing}, so "
            "check.sh's stdlib gate does not read them. A fabricated ns.func in "
            "those files would reach the model unchallenged."
        )


# ---------------------------------------------------------------------------
class TestBothPluginTargetsAreGated:
    """Every juce_add_plugin target must be built by BOTH the ladder and CI.

    TestLadderRunsWhatCIRuns above cannot cover this, for two independent reasons
    -- either alone would make it a false comfort:

      1. Its universe is filtered to names ending in "Test"
         (_ci_harnesses, :770). A plugin target called PluginForgeSynth is
         outside it by construction.
      2. Its relation runs one way, CI -> ladder. Nothing there stops the LADDER
         growing a target CI never builds.

    Both fired at once on 2026-07-31. PluginForgeSynth -- the instrument target,
    a whole second plugin -- shipped in tools/check.sh and in no CI job, with
    every gate green. That is PF-029 inverted: the local ladder making a TRUE
    statement about a LARGER set than the remote gate, which is the same defect
    read from the other end.

    NOT COVERED: that the targets link, load in a DAW, or do anything once built.
    This asserts only that neither gate is structurally blind to them.
    """

    CMAKE = ROOT / "host" / "CMakeLists.txt"

    @classmethod
    def _plugin_targets(cls) -> set[str]:
        return set(re.findall(r"juce_add_plugin\(\s*(\w+)", cls.CMAKE.read_text()))

    def test_there_is_more_than_one_plugin_target(self):
        # Guards the guard. If the regex breaks, every assertion below passes
        # vacuously and this class becomes decoration -- the exact failure mode
        # it was written to prevent.
        targets = self._plugin_targets()
        assert len(targets) >= 2, (
            f"parsed {targets} out of host/CMakeLists.txt. Expected at least the Fx "
            "and instrument targets; if a target was removed, update this test "
            "deliberately rather than letting it pass on an empty set."
        )

    def test_the_ladder_builds_every_plugin_target(self):
        body = CHECK_SH.read_text()
        missing = sorted(t for t in self._plugin_targets() if t not in body)
        assert not missing, (
            f"host/CMakeLists.txt declares {missing} and tools/check.sh never builds "
            "them. A plugin nobody compiles locally breaks on someone else's machine."
        )

    def test_ci_builds_every_plugin_target(self):
        body = WORKFLOW.read_text()
        missing = sorted(t for t in self._plugin_targets() if t not in body)
        assert not missing, (
            f"host/CMakeLists.txt declares {missing} and .github/workflows/test.yml "
            "never builds them. 'Done means pushed and green' is a claim about CI, "
            "and CI would not be looking -- PF-029 inverted."
        )


# ---------------------------------------------------------------------------
class TestCaptureHarnessTakesTheLock:
    """Every harness that spends provider quota must hold the PF-025 lock.

    PF-025 fired twice: two runs of the SAME harness destroying each other's
    evidence and sharing one free-tier rate limit. The lock closed that. PF-030 is
    the residual and it is the more interesting half -- `run_efficacy_study.py`
    takes no lock at all, so it can run CONCURRENTLY with `run_benchmark.py` and
    collide in exactly the same way, between two different harnesses that have
    never heard of each other. A lock only one participant respects is not a lock.

    `bench/p6_capture.py` was written after that was known, so it takes the lock
    from the start, and this test is what stops the next harness being written
    without one. It deliberately checks the IMPORT and both CALLS rather than
    running the harness: acquiring for real would either block on a legitimate run
    or leave a stale lock behind if this test failed mid-way.

    NOT COVERED: that the lock is held for the whole run rather than released
    early. That is a property of control flow (p6_capture uses try/finally) and is
    not mechanically checkable from here.
    """

    # score_efficacy.py joined 2026-07-30. It reads as an offline scorer and is one
    # everywhere except behind --judge, which makes one provider call per compiled
    # record -- up to 125 on a full grid -- and took no lock at all until the judge
    # was run for the first time and the hole became visible. That is PF-030's
    # hazard reappearing in the scorer six days after PF-030 closed it in the study,
    # which is why this list is a list and not two names.
    HARNESSES = ["p6_capture.py", "run_efficacy_study.py", "score_efficacy.py"]

    @pytest.mark.parametrize("harness", HARNESSES)
    def test_harness_acquires_and_releases_the_lock(self, harness):
        src = (ROOT / "bench" / harness).read_text()
        assert "acquire_lock" in src, (
            f"bench/{harness} spends provider quota and never calls acquire_lock(). "
            "Two harnesses sharing one free-tier rate limit is PF-025, and a "
            "measurement corrupted by a collision looks exactly like data (PF-030)."
        )
        assert "release_lock" in src, (
            f"bench/{harness} acquires the PF-025 lock and never releases it. A guard "
            "that stays latched after a run gets deleted by the first person it blocks."
        )
        assert "finally" in src, (
            f"bench/{harness} must release the lock in a finally block, or a crashed "
            "run leaves the lock held and the next person deletes it by hand."
        )

    def test_a_real_run_is_gated_behind_explicit_consent(self):
        # tools/check.sh's `quota` level refuses to spend without
        # --i-authorize-spend. Anything that spends quota follows the same rule:
        # free-tier requests are this project's binding constraint.
        src = (ROOT / "bench" / "p6_capture.py").read_text()
        assert "--i-authorize-spend" in src, (
            "bench/p6_capture.py spends 14 generations of free-tier quota and does "
            "not require explicit authorization."
        )

    def test_the_battery_prompts_match_the_document(self):
        """The transcription is checked, not trusted.

        docs/p6_test_battery.md says: copy these verbatim, and a reworded prompt is
        a different data point. bench/prompts/p6_battery.json is a transcription of
        that table, and a transcription nobody checks is how a benchmark ends up
        measuring a prompt that no longer exists (PF-007, PF-009).
        """
        battery = json.loads((ROOT / "bench" / "prompts" / "p6_battery.json").read_text())
        doc = (ROOT / "docs" / "p6_test_battery.md").read_text()

        assert len(battery["prompts"]) == 14, "the P6 battery is 14 prompts"
        ids = [p["id"] for p in battery["prompts"]]
        assert ids == list(range(1, 15)), f"ids must be 1..14, got {ids}"

        for item in battery["prompts"]:
            # Each prompt must appear in the document, inside backticks, exactly.
            assert f"`{item['prompt']}`" in doc, (
                f"prompt {item['id']} is not in docs/p6_test_battery.md verbatim:\n"
                f"  {item['prompt']!r}\n"
                "Either the transcription drifted or the document changed. Fix the "
                "JSON from the document, never the other way round."
            )


class TestJitTargetIsPinnedInCI:
    """Every CI step that JITs Faust must preload the PF-036 CPU shim.

    PF-036, found 2026-07-30. libfaust's LLVM picks its target CPU by NAME
    (`llvm::sys::getHostCPUName()`, CPUID family/model) and enables that CPU's
    DEFAULT feature set without rechecking whether the guest can execute it. On
    GitHub's AMD EPYC 9V74 runners -- Azure Genoa, which reports as znver4 while
    the hypervisor masks AVX-512 out of the guest -- that emits `kmovd`/EVEX and
    the harness dies with SIGILL inside `computemydsp`.

    GitHub's fleet mixes that CPU with EPYC 7763 (no AVX-512, safe) and Xeon
    8573C (AVX-512 enabled, safe), so the failure is a ~1-in-5 runner draw. That
    is why three readings produced three causes, and why PF-027 was closed on a
    single green run against a hypothesis that was half right.

    The guarded property is WIRING, not behaviour: that the preload is still
    attached to every step that runs a JIT-ing binary. Whether the shim actually
    changes the emitted instruction set is host/tests/JitTargetTest.cpp's job,
    and that binary refuses to pass at all when the preload is missing.

    NOT COVERED: that CI passes on a 9V74. No workflow can request a CPU model,
    so that remains unobservable on demand -- which is exactly why the local
    disassembly test exists and why a green run must never be read as proof.
    """

    #: Steps whose binaries reach FaustEngine::compile() and therefore JIT.
    JIT_STEPS = (
        "Run OfflineRenderTest",
        "Run PromptPanelThreadingTest",
        "Run EditorSessionTest",
        "Run ParamPoolTsanTest",
        # StatePersistenceTest constructs a real processor and compiles Faust
        # through the JIT, so it carries the same AVX-512 exposure. It joined the
        # workflow 2026-07-30 after being found to run nowhere at all.
        "Run the pure harnesses",
    )

    @staticmethod
    def _step_bodies() -> dict[str, str]:
        """Split the workflow into `- name: ...` blocks, keyed by step name."""
        text = WORKFLOW.read_text()
        blocks: dict[str, str] = {}
        parts = re.split(r"\n\s*-\s*name:\s*", text)
        for part in parts[1:]:
            name, _, body = part.partition("\n")
            blocks[name.strip()] = body
        return blocks

    def test_the_shim_is_built(self):
        # Must be in the cmake --target list, not merely mentioned. A comment
        # naming the shim satisfies a substring search and builds nothing --
        # this project's recurring defect is precisely a control that reads as
        # present while doing no work.
        m = re.search(
            r"name:\s*Build behavioural test harnesses\s*\n\s*run:\s*(.+)",
            WORKFLOW.read_text(),
        )
        assert m, "the 'Build behavioural test harnesses' step is gone"
        assert "pf_cpu_shim" in m.group(1).split("--target", 1)[-1], (
            "the workflow no longer BUILDS pf_cpu_shim. Without it every "
            "LD_PRELOAD below resolves to a missing file and the JIT goes back "
            "to trusting getHostCPUName() -- PF-036."
        )

    def test_step_names_still_exist(self):
        # Guards the guard: if the workflow renamed these steps, every assertion
        # below would pass vacuously by matching nothing.
        bodies = self._step_bodies()
        for prefix in self.JIT_STEPS:
            assert any(n.startswith(prefix) for n in bodies), (
                f"no workflow step starts with {prefix!r}. If it was renamed, "
                "repoint this test; if it was deleted, PF-036 needs rethinking, "
                "not this list quietly shrinking."
            )

    def test_every_jit_step_preloads_the_shim(self):
        bodies = self._step_bodies()
        missing = []
        for prefix in self.JIT_STEPS:
            for name, body in bodies.items():
                if name.startswith(prefix) and "LD_PRELOAD" not in body:
                    missing.append(name)
        assert not missing, (
            f"these CI steps JIT Faust without the PF-036 preload: {missing}. "
            "On an EPYC 9V74 runner they will SIGILL in computemydsp, and on the "
            "other ~4 in 5 draws they will pass -- so this cannot be left to be "
            "caught by a red run."
        )

    def test_the_sanitizer_runtime_leads_the_preload(self):
        """libasan/libtsan must come FIRST in LD_PRELOAD.

        Not stylistic. An ASan-instrumented binary aborts at startup with
        "ASan runtime does not come first in initial library list" if anything
        precedes it -- observed locally 2026-07-30 while wiring this. Putting the
        shim first would turn a 1-in-5 SIGILL into a 5-in-5 abort.
        """
        text = WORKFLOW.read_text()

        # The workflow composes these from shell variables rather than literal
        # paths (libasan's location is a compiler question, not a constant), so
        # match the composition, then check what those variables are bound to.
        for var, runtime in (("PF_LD_PRELOAD", "ASAN_LIB"), ("PF_LD_PRELOAD_TSAN", "TSAN_LIB")):
            m = re.search(rf'{var}=(\S+)', text)
            assert m, f"{var} is no longer set in the workflow"
            value = m.group(1)
            first = value.split(":", 1)[0]
            assert runtime in first, (
                f"{var} starts with {first!r}, not ${runtime}; the sanitizer "
                "runtime must lead or the instrumented harnesses abort before "
                "they run."
            )
            assert "$SHIM" in value, f"{var} does not include $SHIM"

        # ...and $SHIM must actually be the shim.
        m = re.search(r'SHIM="([^"]+)"', text)
        assert m and "libpf_cpu_shim.so" in m.group(1), (
            "SHIM is not bound to libpf_cpu_shim.so; the preload would be inert."
        )
        for runtime, lib in (("ASAN_LIB", "libasan.so"), ("TSAN_LIB", "libtsan.so")):
            m = re.search(rf'{runtime}="([^"]+)"', text)
            assert m and lib in m.group(1), f"{runtime} is not bound to {lib}"

    def test_the_ladder_runs_the_shim_test(self):
        assert "JitTargetTest" in CHECK_SH.read_text(), (
            "tools/check.sh does not run JitTargetTest. The shim would be free to "
            "rot until the next unlucky runner draw -- PF-029's lesson applied to "
            "PF-036."
        )
