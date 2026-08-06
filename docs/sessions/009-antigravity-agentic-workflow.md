# Session 009 — Antigravity Agentic Workflow & Architecture Protocol

## 1. Context & Purpose

This document records the protocol and agentic workflow for co-existence between **Antigravity** (and its subagents) and concurrent developer/Claude Code sessions operating inside `PluginForge`.

---

## 2. Multi-Session Safety Rules (Mechanism A Implementation)

To eliminate git index conflicts, code stomping, and unverified prompt changes across sessions, all Antigravity agent operations strictly enforce the following protocol:

### A. Boundary Declaration (`touches` / `depends` / `provides`)
Before executing file edits or creating subagents, declare exact file sets:
- **`touches`**: Explicit list of files modified or created.
- **`depends`**: Existing component interfaces, headers, or schemas relied upon.
- **`provides`**: Deliverables, exported methods, or updated behaviors.

### B. Git Branch & Index Isolation
1. **Zero Global Staging:** Never execute `git add .` or `git commit -a`. Stage only explicitly modified paths (`git add <file>`).
2. **Dedicated Working Branches:** Feature implementations must execute in isolated feature branches (`feat/antigravity-*`).
3. **PreToolUse Hook Compliance:** All file edits must satisfy repository hooks:
   - `check_rt_safety.py` (No RT-thread allocations)
   - `check_prompt_invariants.py` (Faust stdlib signature resolution)
   - `check_bash_denylist.py` (No forbidden commands or global staging)
   - `check_doc_naming.py` (Session NNN document naming)

---

## 3. Standard Agentic Prompt Series (P0 – P5)

Every session operating on `PluginForge` executes through the following cost-ordered, cumulative prompt sequence:

```
P0 (/orient)  ──>  P1 (Consult Gate)  ──>  P2 (Prompt Headroom)
                                                   │
P5 (DAW Audition) <──  P4 (Render Oracle) <──  P3 (RT-Safety Audit)
```

| Stage | Command / Prompt | Purpose | Evidence Artifact |
|---|---|---|---|
| **P0** | `/orient` | Session start sync: repo state, CI status, open STATUS.md | Live banner output |
| **P1** | Architectural Consult | Evaluate changes against COLLABORATION.md §2 triggers | ADR draft if triggered |
| **P2** | Invariant & Headroom | `pytest tests/test_prompt_headroom.py` & hook checks | Token slack > 300 |
| **P3** | RT-Safety Audit | `python3 .claude/hooks/check_rt_safety.py` | Zero RT allocations |
| **P4** | Acoustic Oracle | `python3 bench/render_oracle.py` & `spectral_judge.py` | Clean audio & spectral score |
| **P5** | DAW / Host Audition | `tools/check.sh full` + Standalone host launch | UI screenshot & listening pass |

---

## 4. Change Report Format (COLLABORATION.md §4)

Every session deliverables update concludes with the 5-line summary:

```text
CHANGED    <files modified/added>
WHY        <defect or architectural need addressed>
VERIFIED   <file:line citations, tests run, green results>
RISK       <unverified edge cases or remaining limitations>
YOUR MOVE  <human action item or listening pass request>
```
