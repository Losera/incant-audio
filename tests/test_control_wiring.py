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
                    if not (ROOT / ref).exists():
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

    HARNESSES = ["p6_capture.py", "run_efficacy_study.py"]

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
