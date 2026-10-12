"""Portable agent workflow regression tests (ADR-038)."""

from __future__ import annotations

import os
import subprocess
import sys
from pathlib import Path


ROOT = Path(__file__).parent.parent
SESSION = ROOT / "tools" / "agent_session.py"
RECAP = ROOT / "tools" / "recap_git.sh"


def run(*args: str, cwd: Path, env: dict[str, str] | None = None):
    return subprocess.run(
        args,
        cwd=cwd,
        env={**os.environ, **(env or {})},
        capture_output=True,
        text=True,
        timeout=30,
        check=True,
    )


def make_repo(tmp_path: Path) -> tuple[Path, str]:
    repo = tmp_path / "repo"
    repo.mkdir()
    run("git", "init", "-q", cwd=repo)
    run("git", "config", "user.name", "Agent Workflow Test", cwd=repo)
    run("git", "config", "user.email", "agent-workflow@example.invalid", cwd=repo)
    (repo / "tracked.txt").write_text("first\n")
    run("git", "add", "tracked.txt", cwd=repo)
    run("git", "commit", "-qm", "first", cwd=repo)
    first = run("git", "rev-parse", "HEAD", cwd=repo).stdout.strip()
    (repo / "tracked.txt").write_text("second\n")
    run("git", "commit", "-qam", "second", cwd=repo)
    return repo, first


def test_check_and_snapshot_are_checkout_relative(tmp_path):
    repo, _ = make_repo(tmp_path)

    checked = run(sys.executable, str(SESSION), "check", cwd=repo).stdout
    assert f"root      {repo}" in checked
    assert "WARNING   primary checkout" in checked
    assert "handoff   none on disk" in checked

    snapshot = run(
        sys.executable,
        str(SESSION),
        "snapshot",
        "--trigger",
        "test",
        cwd=repo,
    )
    state = repo / ".agent" / "handoff-state.json"
    assert snapshot.stdout.strip() == str(state)
    text = state.read_text()
    assert '"trigger": "test"' in text
    assert '"head":' in text


def test_recap_prefers_portable_handoff(tmp_path):
    repo, first = make_repo(tmp_path)
    (repo / ".agent").mkdir()
    (repo / ".claude").mkdir()
    (repo / ".agent" / "HANDOFF.md").write_text(
        f"# Handoff\n<!-- handoff-meta: head={first} written=now branch=main -->\n"
    )
    current = run("git", "rev-parse", "HEAD", cwd=repo).stdout.strip()
    (repo / ".claude" / "HANDOFF.md").write_text(
        f"# Handoff\n<!-- handoff-meta: head={current} written=now branch=main -->\n"
    )

    output = run(
        "bash",
        str(RECAP),
        cwd=repo,
        env={"PLUGINFORGE_RECAP_ROOT": str(repo)},
    ).stdout
    assert "(from .agent/HANDOFF.md)" in output
    assert "commits    1 since boundary" in output


def test_runtime_specific_skills_are_thin_adapters():
    for name in ("orient", "recap", "handoff"):
        canonical = ROOT / ".agents" / "skills" / name / "SKILL.md"
        adapter = ROOT / ".claude" / "skills" / name / "SKILL.md"
        assert canonical.exists()
        assert f".agents/skills/{name}/SKILL.md" in adapter.read_text()

    assert (ROOT / ".agents" / "skills" / "brief" / "SKILL.md").exists()


def test_claude_session_hooks_prefer_portable_state():
    injector = (ROOT / ".claude" / "hooks" / "handoff_injector.py").read_text()
    collision = (ROOT / ".claude" / "hooks" / "session_collision_guard.py").read_text()
    for body in (injector, collision):
        assert 'ROOT / ".agent" / "HANDOFF.md"' in body
        assert "PORTABLE_HANDOFF if PORTABLE_HANDOFF.exists()" in body
