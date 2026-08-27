#!/usr/bin/env python3
"""Shared scanner for PluginForge's implicit knowledge graph.

WHY THIS EXISTS
---------------
This repository is already an ID-addressed, test-enforced knowledge graph: ~6 ID
namespaces (`PF-NNN`, `ADR-NNN`, `docs/sessions/NNN`, ...), on the order of a
thousand bare-ID cross-references, and ~700 backtick repo-paths that
`tests/test_control_wiring.py` already dead-reference-checks. What it lacked was
any check that a cited `ADR-NNN` or `PF-NNN` actually *resolves* to a definition,
and any way to view the reference graph without adopting a GUI wiki. ADR-031
records that decision; this module is the shared engine for both halves:

  * `tests/test_control_wiring.py` imports it to ASSERT every live `ADR-NNN` /
    `PF-NNN` reference resolves (with red cases).
  * `tools/kg.py` imports it to EMIT the graph (Mermaid / DOT / JSON) and flag
    dangling references and orphan documents.

One implementation, so the test and the viewer can never disagree about what a
reference is.

Dependency-free by design: stdlib `re` / `pathlib` / `subprocess` only. No
`networkx`, no graphviz binding — `tools/kg.py` emits DOT as text.

NOT COVERED: `D`-series (design decisions) and `P`-series (phases) have no
registry file, so they are collected as reference targets but never validated.
Session references are validated against the files actually in `docs/sessions/`.
"""
from __future__ import annotations

import re
import subprocess
from dataclasses import dataclass, field
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent

# --- reference patterns (what a document CITES) ---------------------------------

ADR_REF_RE = re.compile(r"\bADR-(\d{3})\b")
PF_REF_RE = re.compile(r"\bPF-(\d{3,})\b")
SESSION_REF_RE = re.compile(r"\bdocs/sessions/(\d{3})\b")

# Same shape as tests/test_control_wiring.py::_PATH_RE — kept in sync deliberately.
PATH_REF_RE = re.compile(
    r"`((?:docs|tools|llm|bench|host|tests|examples|\.claude)/[A-Za-z0-9_./*{}-]+)`"
)

# --- definition patterns (what makes an ID VALID) -----------------------------

# A heading in docs/decisions.md: "## ADR-029 — ..."  (ADR-011 uses a single #).
_ADR_HEADING_RE = re.compile(r"^#+\s+ADR-(\d{3})\b", re.M)
# A registry-table row in docs/BUGS.md: "| PF-041 | title | ... |"
_PF_ROW_RE = re.compile(r"^\|\s*PF-(\d{3,})\s*\|", re.M)

# Directories never walked for markdown (build output, sibling worktrees, vcs).
_SKIP_DIR_PARTS = {"build", "worktrees", ".git", "node_modules", "JUCE"}

# Untracked (not-yet-committed) markdown is scanned only under these top-level
# trees -- the ones this project manages as documentation. A new session doc,
# ADR, skill or rule therefore has its cross-references checked before it is
# committed (id_graph's reason to exist), while an unrelated scratch file like a
# root-level notes.md cannot turn tests/test_control_wiring.py red.
_UNTRACKED_DOC_ROOTS = {"docs", ".claude"}


@dataclass
class DocRefs:
    """Every ID a single document references."""

    adr: set[str] = field(default_factory=set)
    pf: set[str] = field(default_factory=set)
    session: set[str] = field(default_factory=set)
    paths: set[str] = field(default_factory=set)


def tracked_markdown() -> list[Path]:
    """Every git-tracked `*.md`, plus untracked-not-ignored `*.md` **under
    `_UNTRACKED_DOC_ROOTS`**, minus build/worktree/vendor trees.

    Untracked doc-tree files are included deliberately: a new session doc or ADR
    should have its cross-references checked before it is committed, not after.
    Scoping that to `docs/` and `.claude/` keeps an unrelated untracked scratch
    file (e.g. a root `notes.md` with an example `PF-123`) from failing the
    resolution tests. `.gitignore`d paths (`.claude/plans/`, `docs/_graph.md`,
    ...) are excluded by `--exclude-standard`.
    """
    tracked = subprocess.run(
        ["git", "ls-files", "--cached", "*.md"],
        cwd=str(ROOT), capture_output=True, text=True, timeout=30,
    ).stdout.splitlines()
    untracked = subprocess.run(
        ["git", "ls-files", "--others", "--exclude-standard", "*.md"],
        cwd=str(ROOT), capture_output=True, text=True, timeout=30,
    ).stdout.splitlines()

    lines = list(tracked) + [
        u for u in untracked
        if u.split("/", 1)[0] in _UNTRACKED_DOC_ROOTS
    ]
    files: list[Path] = []
    seen: set[str] = set()
    for line in lines:
        if not line or line in seen:
            continue
        seen.add(line)
        parts = line.split("/")
        if _SKIP_DIR_PARTS.intersection(parts):
            continue
        p = ROOT / line
        if p.exists():
            files.append(p)
    return files


def adr_sources() -> dict[str, list[str]]:
    """Every valid ADR id mapped to the repo-relative path(s) that DEFINE it: a
    heading in docs/decisions.md, a file in docs/architectural_decisions/, or
    both (ADR-007/008/009/011 currently have both). Order: decisions.md first
    when present, then the standalone file. This is the single source of truth
    for `valid_adr_ids()` and for the `defined-in` edges tools/kg.py draws."""
    out: dict[str, list[str]] = {}
    decisions = ROOT / "docs" / "decisions.md"
    if decisions.exists():
        for n in _ADR_HEADING_RE.findall(decisions.read_text()):
            out.setdefault(f"ADR-{n}", [])
            if "docs/decisions.md" not in out[f"ADR-{n}"]:
                out[f"ADR-{n}"].append("docs/decisions.md")
    adr_dir = ROOT / "docs" / "architectural_decisions"
    if adr_dir.exists():
        for f in sorted(adr_dir.glob("ADR-*.md")):
            m = re.match(r"ADR-(\d{3})", f.name)
            if m:
                out.setdefault(f"ADR-{m.group(1)}", []).append(
                    f"docs/architectural_decisions/{f.name}"
                )
    return out


def valid_adr_ids() -> set[str]:
    """ADR IDs that resolve: a heading in docs/decisions.md OR a file in
    docs/architectural_decisions/ADR-NNN-*.md."""
    return set(adr_sources())


def valid_pf_ids() -> set[str]:
    """PF IDs that resolve to a row of docs/BUGS.md's registry table."""
    bugs = ROOT / "docs" / "BUGS.md"
    if not bugs.exists():
        return set()
    return {f"PF-{n}" for n in _PF_ROW_RE.findall(bugs.read_text())}


def valid_session_ids() -> set[str]:
    """Session IDs that resolve to a docs/sessions/NNN-*.md file."""
    sdir = ROOT / "docs" / "sessions"
    if not sdir.exists():
        return set()
    ids: set[str] = set()
    for f in sdir.glob("*"):
        m = re.match(r"(\d{3})-", f.name)
        if m:
            ids.add(m.group(1))
    return ids


def scan(files: list[Path] | None = None) -> dict[str, DocRefs]:
    """Map every document's relative path to the IDs and paths it references."""
    if files is None:
        files = tracked_markdown()
    result: dict[str, DocRefs] = {}
    for f in files:
        if not f.exists():
            continue
        text = f.read_text()
        rel = f.relative_to(ROOT).as_posix()
        refs = DocRefs(
            adr={f"ADR-{n}" for n in ADR_REF_RE.findall(text)},
            pf={f"PF-{n}" for n in PF_REF_RE.findall(text)},
            session=set(SESSION_REF_RE.findall(text)),
            paths={
                r for r in PATH_REF_RE.findall(text)
                if not any(ch in r for ch in "*{")
            },
        )
        result[rel] = refs
    return result


def scan_text(text: str) -> DocRefs:
    """Reference scan of a single in-memory string (used by the red-case test)."""
    return DocRefs(
        adr={f"ADR-{n}" for n in ADR_REF_RE.findall(text)},
        pf={f"PF-{n}" for n in PF_REF_RE.findall(text)},
        session=set(SESSION_REF_RE.findall(text)),
        paths={
            r for r in PATH_REF_RE.findall(text)
            if not any(ch in r for ch in "*{")
        },
    )


if __name__ == "__main__":  # pragma: no cover - quick manual sanity check
    adr, pf = valid_adr_ids(), valid_pf_ids()
    print(f"valid ADR ids : {len(adr)}  ({min(adr, default='-')}..{max(adr, default='-')})")
    print(f"valid PF ids  : {len(pf)}")
    scanned = scan()
    print(f"scanned docs  : {len(scanned)}")
    dangling_adr = sorted(
        {a for r in scanned.values() for a in r.adr} - adr
    )
    dangling_pf = sorted({p for r in scanned.values() for p in r.pf} - pf)
    print(f"unresolved ADR refs (whole tree): {dangling_adr or 'none'}")
    print(f"unresolved PF refs  (whole tree): {dangling_pf or 'none'}")
