#!/usr/bin/env python3
"""Emit PluginForge's implicit knowledge graph — a headless viewing tool.

WHY THIS EXISTS
---------------
ADR-031. The repo's cross-references are bare IDs (`PF-024`, `ADR-011`) and
backtick repo-paths, not markdown hyperlinks — so Obsidian's graph view, or any
wiki's, sees almost nothing. This script builds that graph from the IDs directly
and emits it as Mermaid (renders in a fenced block and in an Obsidian note), DOT,
or JSON. It also names the two things the 2026-08-19 doc purge was made of:
**dangling** references (an ID cited nowhere-defined) and **orphan** documents
(nothing links to them).

WHAT THIS IS NOT
----------------
Not a CI gate. It makes no assertions and never exits non-zero on graph content
(only on its own errors). Wiring a zero-assertion lane into tools/check.sh would
report it as healthy — the UiDesignGallery precedent (host/CMakeLists.txt). The
resolution *checks* live in tests/test_control_wiring.py (TestIdReferencesResolve),
which shares this script's scanner (tools/id_graph.py) so the two cannot drift.

USAGE
-----
    python tools/kg.py                      # Mermaid, whole graph, to stdout
    python tools/kg.py --format dot | dot -Tsvg -o graph.svg
    python tools/kg.py --format json | python -m json.tool
    python tools/kg.py --focus PF-024       # just that node's neighbourhood
    python tools/kg.py > docs/_graph.md     # then open in Obsidian (gitignored)

Dependency-free: stdlib only. DOT is emitted as text, not via a binding.
"""
from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import id_graph  # noqa: E402


def build_graph() -> dict:
    """Nodes, edges, and the two flagged categories."""
    scanned = id_graph.scan()
    valid_adr = id_graph.valid_adr_ids()
    valid_pf = id_graph.valid_pf_ids()
    valid_session = id_graph.valid_session_ids()

    doc_nodes = sorted(scanned)
    referenced_adr: set[str] = set()
    referenced_pf: set[str] = set()
    referenced_session: set[str] = set()
    edges: list[tuple[str, str, str]] = []  # (src, dst, kind)

    # Reverse map: which docs are pointed AT by a backtick path.
    inbound_paths: dict[str, int] = {d: 0 for d in doc_nodes}

    for doc, refs in scanned.items():
        for adr in sorted(refs.adr):
            referenced_adr.add(adr)
            edges.append((doc, adr, "cites"))
        for pf in sorted(refs.pf):
            referenced_pf.add(pf)
            edges.append((doc, pf, "cites"))
        for s in sorted(refs.session):
            referenced_session.add(s)
            edges.append((doc, f"session-{s}", "cites"))
        for path in sorted(refs.paths):
            if path in inbound_paths and path != doc:
                inbound_paths[path] += 1
                edges.append((doc, path, "links"))

    # "ID defined in doc" edges, for the IDs that resolve.
    decisions = "docs/decisions.md"
    bugs = "docs/BUGS.md"
    adr_src = id_graph.adr_sources()
    for adr in sorted(referenced_adr | valid_adr):
        for path in adr_src.get(adr, []):
            edges.append((adr, path, "defined-in"))
    for pf in sorted(referenced_pf | valid_pf):
        if pf in valid_pf:
            edges.append((pf, bugs, "defined-in"))
    for s in sorted(referenced_session | valid_session):
        if s in valid_session:
            match = next(
                (d for d in doc_nodes if d.startswith(f"docs/sessions/{s}-")), None
            )
            if match:
                edges.append((f"session-{s}", match, "defined-in"))

    dangling = {
        "adr": sorted(referenced_adr - valid_adr),
        "pf": sorted(referenced_pf - valid_pf),
        "session": sorted(referenced_session - valid_session),
    }

    # Orphan docs: nothing links to them by backtick path. Three categories are
    # excluded because a zero inbound-path count is expected and not a defect:
    #   - anchor docs invoked/read by convention, not by link (CLAUDE.md, the
    #     README family, everything under .claude/ — skills and agents are
    #     entrypoints the harness dispatches by name);
    #   - point-in-time / append-only trees (docs/sessions, docs/research,
    #     docs/records, the ADR log) — COLLABORATION.md §8.
    ANCHORS = {"CLAUDE.md", "COLLABORATION.md", "STATUS.md", "README.md",
               "OPEN_QUESTIONS.md", "PLUGIN_HEALTH_PLAN.md", "INTERFACE.md"}

    def excused(d: str) -> bool:
        return (
            d in ANCHORS
            or d.startswith(".claude/")
            or Path(d).name == "README.md"
            or d.startswith(("docs/sessions/", "docs/research/", "docs/records/",
                             "docs/architectural_decisions/"))
            or d == decisions
        )

    orphans = sorted(
        d for d, n in inbound_paths.items() if n == 0 and not excused(d)
    )
    pointintime_unlinked = sorted(
        d for d, n in inbound_paths.items()
        if n == 0 and excused(d) and d not in ANCHORS and not d.startswith(".claude/")
    )

    return {
        "docs": doc_nodes,
        "adr": sorted(referenced_adr | valid_adr),
        "pf": sorted(referenced_pf | valid_pf),
        "sessions": sorted(referenced_session | valid_session),
        "edges": edges,
        "dangling": dangling,
        "orphans": orphans,
        "pointintime_unlinked": pointintime_unlinked,
        "valid": {"adr": sorted(valid_adr), "pf": sorted(valid_pf)},
    }


def _focus(graph: dict, node: str) -> dict:
    """Restrict the graph to `node` and its immediate neighbours."""
    keep = {node}
    for src, dst, _ in graph["edges"]:
        if src == node:
            keep.add(dst)
        elif dst == node:
            keep.add(src)
    g = dict(graph)
    g["edges"] = [e for e in graph["edges"] if e[0] in keep and e[1] in keep]
    g["docs"] = [d for d in graph["docs"] if d in keep]
    g["adr"] = [a for a in graph["adr"] if a in keep]
    g["pf"] = [p for p in graph["pf"] if p in keep]
    g["sessions"] = [s for s in graph["sessions"] if f"session-{s}" in keep]
    return g


def _slug(name: str) -> str:
    return "".join(c if c.isalnum() else "_" for c in name)


def _mm_label(text: str) -> str:
    """Escape a label for a Mermaid quoted node string. Node ids reach here raw
    (repo paths with `/` and `.`, `ADR-NNN`, em-dashes); `/` `.` `-` are fine
    inside quotes but a bare `"` or `#` renders a broken diagram with no error.
    Mermaid reads `#nnn;` HTML-entity codes inside quotes, so map to those. `#`
    is escaped first so the entities this introduces are not double-escaped."""
    return text.replace("#", "#35;").replace('"', "#quot;")


def emit_mermaid(graph: dict) -> str:
    lines = ["```mermaid", "flowchart LR"]
    dangling_all = {
        *graph["dangling"]["adr"],
        *graph["dangling"]["pf"],
        *(f"session-{s}" for s in graph["dangling"]["session"]),
    }
    orphan_set = set(graph["orphans"])

    def node_line(node_id: str, label: str, shape: str = "round") -> str:
        sid = _slug(node_id)
        lbl = _mm_label(label)
        if shape == "round":
            return f'    {sid}(["{lbl}"])'
        return f'    {sid}["{lbl}"]'

    seen: set[str] = set()

    def ensure(node_id: str, label: str, shape: str = "round"):
        if node_id in seen:
            return
        seen.add(node_id)
        lines.append(node_line(node_id, label, shape))

    for src, dst, kind in graph["edges"]:
        arrow = {"cites": "-->", "defined-in": "==>", "links": "-.->"}[kind]
        ensure(src, src)
        ensure(dst, dst)
        lines.append(f"    {_slug(src)} {arrow} {_slug(dst)}")

    for node_id in dangling_all:
        if node_id in seen:
            lines.append(f"    class {_slug(node_id)} dangling")
    for node_id in orphan_set:
        if node_id in seen:
            lines.append(f"    class {_slug(node_id)} orphan")

    lines.append("    classDef dangling fill:#fdd,stroke:#c00,stroke-width:2px")
    lines.append("    classDef orphan fill:#fe8,stroke:#c80")
    lines.append("```")
    return "\n".join(lines)


def emit_dot(graph: dict) -> str:
    lines = ["digraph knowledge {", "  rankdir=LR;", '  node [shape=box];']
    dangling_nodes = set(
        graph["dangling"]["adr"] + graph["dangling"]["pf"]
        + [f"session-{s}" for s in graph["dangling"]["session"]]
    )
    orphan_set = set(graph["orphans"])
    for src, dst, kind in graph["edges"]:
        style = {"cites": "solid", "defined-in": "bold", "links": "dashed"}[kind]
        lines.append(f'  "{src}" -> "{dst}" [style={style}];')
    for n in dangling_nodes:
        lines.append(f'  "{n}" [color=red, penwidth=2];')
    for n in orphan_set:
        lines.append(f'  "{n}" [fillcolor="#ffee88", style=filled];')
    lines.append("}")
    return "\n".join(lines)


def emit_json(graph: dict) -> str:
    return json.dumps(graph, indent=2, sort_keys=True)


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--format", choices=("mermaid", "dot", "json"), default="mermaid")
    ap.add_argument("--focus", metavar="ID",
                    help="restrict to one node and its neighbours (e.g. PF-024, ADR-011)")
    ap.add_argument("--summary", action="store_true",
                    help="print only the dangling/orphan summary to stderr, no graph")
    args = ap.parse_args(argv)

    graph = build_graph()

    d = graph["dangling"]
    summary = (
        f"dangling ADR refs : {d['adr'] or 'none'}"
        + ("  (none are cited from a LIVE doc — they sit in the dated 2026-07-21 "
           "review and in ADR-031's own prose; the test scopes to live docs)"
           if d['adr'] else "") + "\n"
        f"dangling PF refs  : {d['pf'] or 'none'}\n"
        f"dangling sessions : {d['session'] or 'none'}"
        + ("  (early session docs deleted in the 2026-08-19 purge — usually "
           "legitimate; git log is their record)" if d['session'] else "") + "\n"
        f"orphan docs       : {len(graph['orphans'])}"
        + (f" ({', '.join(graph['orphans'])})" if graph['orphans'] else "")
    )
    print(summary, file=sys.stderr)

    if args.summary:
        return 0

    if args.focus:
        graph = _focus(graph, args.focus)

    out = {"mermaid": emit_mermaid, "dot": emit_dot, "json": emit_json}[args.format](graph)
    print(out)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
