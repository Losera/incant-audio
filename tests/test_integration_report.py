"""Tests for tools/integration_report.py.

Two layers, matching the module's own split between pure parsing and git
plumbing:

  * Pure-parser tests (TestParseWorktreePorcelain, TestParseNumstat, TestAgeStr)
    exercise text -> structure with no git process at all.

  * TestThreeDotVsTwoDot and TestCollectBranchState construct a REAL throwaway
    git repo + worktree under tmp_path and run the actual module functions
    against it. This is deliberate, not incidental: the module exists because a
    two-dot diff against a moved `main` was misread as a 391-line regression
    (STATUS.md "Waiting on you" #6, 2026-09-18 -> found 2026-10-20) and froze a
    branch for a month. A fixture-text test could assert the module calls
    `git diff --numstat` with three dots in it, but it could not catch a
    regression where the three-dot *behavior* silently reverted to two-dot's
    numbers while the call still looked right. Only a constructed repo with a
    real divergent history proves the fix actually holds.
"""
from __future__ import annotations

import subprocess
import sys
from datetime import datetime, timezone
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))

import integration_report as ir  # noqa: E402


def _git(cwd: Path, *args: str) -> str:
    p = subprocess.run(["git", "-C", str(cwd), *args], capture_output=True, text=True)
    assert p.returncode == 0, f"git {args} failed: {p.stderr}"
    return p.stdout


def _init_repo(path: Path) -> None:
    path.mkdir(parents=True, exist_ok=True)
    _git(path, "init", "--quiet", "--initial-branch=main")
    _git(path, "config", "user.email", "test@example.com")
    _git(path, "config", "user.name", "Test")


# ── Pure parsers ────────────────────────────────────────────────────────────


class TestParseWorktreePorcelain:
    def test_single_worktree_with_branch(self):
        text = (
            "worktree /repo\n"
            "HEAD abc123\n"
            "branch refs/heads/main\n"
        )
        out = ir.parse_worktree_porcelain(text)
        assert out == [{"path": "/repo", "head": "abc123", "branch": "main"}]

    def test_multiple_worktrees(self):
        text = (
            "worktree /repo\n"
            "HEAD abc123\n"
            "branch refs/heads/main\n"
            "\n"
            "worktree /repo/.worktrees/feat\n"
            "HEAD def456\n"
            "branch refs/heads/feat/x\n"
        )
        out = ir.parse_worktree_porcelain(text)
        assert len(out) == 2
        assert out[1]["path"] == "/repo/.worktrees/feat"
        assert out[1]["branch"] == "feat/x"

    def test_detached_head_has_no_branch(self):
        text = "worktree /repo/.worktrees/detached\nHEAD abc123\ndetached\n"
        out = ir.parse_worktree_porcelain(text)
        assert out[0]["branch"] is None

    def test_empty_text_is_empty_list(self):
        assert ir.parse_worktree_porcelain("") == []


class TestParseNumstat:
    def test_basic_counts(self):
        text = "3\t1\tfoo.py\n10\t0\tbar.py\n"
        out = ir.parse_numstat(text)
        assert out == {"files": 2, "insertions": 13, "deletions": 1}

    def test_binary_file_counts_as_touched_not_lines(self):
        text = "-\t-\timage.png\n2\t2\ttext.py\n"
        out = ir.parse_numstat(text)
        assert out == {"files": 2, "insertions": 2, "deletions": 2}

    def test_empty_text_is_zero(self):
        assert ir.parse_numstat("") == {"files": 0, "insertions": 0, "deletions": 0}


class TestAgeStr:
    def test_today(self):
        now = datetime(2026, 10, 20, tzinfo=timezone.utc)
        epoch = int(datetime(2026, 10, 20, tzinfo=timezone.utc).timestamp())
        assert ir._age_str(epoch, now) == "today"

    def test_one_day(self):
        now = datetime(2026, 10, 20, tzinfo=timezone.utc)
        epoch = int(datetime(2026, 10, 19, tzinfo=timezone.utc).timestamp())
        assert ir._age_str(epoch, now) == "1d"

    def test_many_days(self):
        now = datetime(2026, 10, 20, tzinfo=timezone.utc)
        epoch = int(datetime(2026, 9, 7, tzinfo=timezone.utc).timestamp())
        assert ir._age_str(epoch, now) == "43d"

    def test_none_is_unknown(self):
        assert ir._age_str(None) == "?"


# ── Real constructed repos ──────────────────────────────────────────────────


class TestThreeDotVsTwoDot:
    """Re-proves the actual finding: a two-dot diff against a `main` that has
    since moved reports the branch's own additions as deletions; three-dot
    (merge-base) does not. This is the exact shape STATUS.md's frozen
    worktree-triage note misread (see module docstring)."""

    def test_two_dot_misreads_a_small_branch_as_a_large_regression(self, tmp_path):
        repo = tmp_path / "repo"
        _init_repo(repo)
        (repo / "a.txt").write_text("line\n" * 50)
        _git(repo, "add", "a.txt")
        _git(repo, "commit", "--quiet", "-m", "base")
        base_sha = _git(repo, "rev-parse", "HEAD").strip()

        _git(repo, "checkout", "--quiet", "-b", "feature")
        (repo / "b.txt").write_text("new small feature\n")
        _git(repo, "add", "b.txt")
        _git(repo, "commit", "--quiet", "-m", "small feature commit")

        _git(repo, "checkout", "--quiet", "main")
        (repo / "a.txt").write_text("line\n" * 50 + "lots more main work\n" * 40)
        _git(repo, "add", "a.txt")
        _git(repo, "commit", "--quiet", "-m", "main grew a lot after the branch split")

        two_dot = ir.parse_numstat(_git(repo, "diff", "--numstat", "main..feature"))
        three_dot = ir.parse_numstat(_git(repo, "diff", "--numstat", "main...feature"))

        # Two-dot: feature's tree vs main's CURRENT tree -- main's 40 later
        # lines of growth on a.txt show up as something feature would have to
        # delete to match, i.e. as feature's own "regression".
        assert two_dot["deletions"] >= 40
        # Three-dot: feature's tree vs the common ancestor -- feature only
        # ever touched b.txt, so it has no deletions at all.
        assert three_dot["deletions"] == 0
        assert three_dot["insertions"] == 1
        assert three_dot["files"] == 1

    def test_diffstat_helper_uses_three_dot_and_agrees(self, tmp_path):
        repo = tmp_path / "repo"
        _init_repo(repo)
        (repo / "a.txt").write_text("line\n" * 50)
        _git(repo, "add", "a.txt")
        _git(repo, "commit", "--quiet", "-m", "base")

        _git(repo, "checkout", "--quiet", "-b", "feature")
        (repo / "b.txt").write_text("new small feature\n")
        _git(repo, "add", "b.txt")
        _git(repo, "commit", "--quiet", "-m", "small feature commit")

        _git(repo, "checkout", "--quiet", "main")
        (repo / "a.txt").write_text("line\n" * 50 + "lots more main work\n" * 40)
        _git(repo, "add", "a.txt")
        _git(repo, "commit", "--quiet", "-m", "main grew")

        out = ir._diffstat(repo, "main", "feature")
        assert out == {"files": 1, "insertions": 1, "deletions": 0}


class TestCollectBranchState:
    """collect_branch_state against a real repo with an actual worktree and an
    actual branch-without-a-worktree, since that split is the part a
    fixture-text test cannot exercise."""

    @pytest.fixture
    def repo_with_worktree(self, tmp_path):
        repo = tmp_path / "repo"
        _init_repo(repo)
        (repo / "a.txt").write_text("base\n")
        _git(repo, "add", "a.txt")
        _git(repo, "commit", "--quiet", "-m", "base")

        _git(repo, "branch", "no-worktree-branch")
        _git(repo, "branch", "with-worktree")
        wt = tmp_path / "wt"
        _git(repo, "worktree", "add", "--quiet", str(wt), "with-worktree")
        (wt / "b.txt").write_text("feature\n")
        _git(wt, "add", "b.txt")
        _git(wt, "commit", "--quiet", "-m", "feature commit")
        return repo, wt

    def test_branch_with_worktree_reports_dirty_count(self, repo_with_worktree):
        repo, wt = repo_with_worktree
        (wt / "untracked.txt").write_text("dirty\n")
        row = ir.collect_branch_state(str(wt), "with-worktree", root=repo, base="main")
        assert row["contribution"] == {"files": 1, "insertions": 1, "deletions": 0}
        assert row["behind_main"] == 0
        assert row["dirty_paths"] == 1
        assert row["worktree"] == str(wt)

    def test_branch_without_worktree_has_no_dirty_count(self, repo_with_worktree):
        repo, _wt = repo_with_worktree
        row = ir.collect_branch_state(None, "no-worktree-branch", root=repo, base="main")
        assert row["dirty_paths"] is None
        assert row["worktree"] is None
        assert row["behind_main"] == 0

    def test_list_local_branches_without_worktree_excludes_checked_out_ones(
        self, repo_with_worktree
    ):
        repo, _wt = repo_with_worktree
        worktrees = ir.list_worktrees(repo)
        without = ir.list_local_branches_without_worktree(repo, worktrees)
        assert "no-worktree-branch" in without
        assert "with-worktree" not in without
        assert "main" not in without  # repo's own primary checkout IS main's worktree


class TestPrList:
    def test_seam_overrides_real_gh_call(self, tmp_path, monkeypatch):
        monkeypatch.setenv(
            "PLUGINFORGE_PR_LIST_JSON",
            '[{"number": 1, "headRefName": "x", "state": "OPEN", "url": "u"}]',
        )
        out = ir.pr_list(tmp_path)
        assert out == [{"number": 1, "headRefName": "x", "state": "OPEN", "url": "u"}]

    def test_malformed_seam_is_none_not_a_crash(self, monkeypatch, tmp_path):
        monkeypatch.setenv("PLUGINFORGE_PR_LIST_JSON", "not json")
        assert ir.pr_list(tmp_path) is None

    def test_non_list_seam_is_none(self, monkeypatch, tmp_path):
        monkeypatch.setenv("PLUGINFORGE_PR_LIST_JSON", '{"not": "a list"}')
        assert ir.pr_list(tmp_path) is None


class TestFormatReportNeverClaimsNoPrOnUnknown:
    """When gh is unavailable, every row's PR state must read as unknown, never
    silently as 'none' -- the same 'unknown is loud' rule
    tools/status_digest.sh's CI section already follows."""

    def test_pr_unavailable_is_visible_in_output(self):
        data = {
            "primary_branch_is_main": False,
            "pr_lookup_available": False,
            "rows": [{
                "branch": "feat/x", "worktree": "/repo/.worktrees/x",
                "head": "abc", "contribution": {"files": 1, "insertions": 2, "deletions": 0},
                "behind_main": 0, "dirty_paths": 0, "age": "1d",
                "has_remote": True, "pr": None,
            }],
        }
        out = ir.format_report(data)
        assert "unknown (gh unavailable)" in out
        assert "none" not in out.split("unknown (gh unavailable)")[0].split("\n")[-1]

    def test_empty_rows_says_so_plainly(self):
        out = ir.format_report({"primary_branch_is_main": True, "pr_lookup_available": True, "rows": []})
        assert "nothing stranded" in out
