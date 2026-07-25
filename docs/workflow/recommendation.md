# Workflow: one pilot

2026-07-25, session 2 lane W. Capped at one page by instruction, because the finding this
responds to is that this project writes more than it ships.

---

## The pilot: make `llm/generate.py` AXI-conformant

**Adopt [axi.md](https://axi.md/)'s 10 principles as the spec for `generate.py`'s
agent-facing contract, and fix the failures.** `generate.py` is not *like* an
agent-invoked CLI — it *is* one. The C++ host calls it via `juce::ChildProcess`
(`host/Source/PromptPanel.cpp`), and three bench harnesses call it too. Four agent callers,
no human ones except in debugging.

**What it replaces:** `llm/README.md` (5.6 kB of hand-written usage prose) and the
narrative half of the ADR-011 wire-contract documentation — superseded by `--help` and a
machine-readable `--schema`. Net-negative in documentation bytes by construction: the
contract stops being prose that drifts and becomes output that cannot.

**Why this one and not a process change:** it is the only candidate that adds zero
process. Every finding in the audit points the same way — the apparatus outgrew the
product — so a pilot that installs more apparatus is disqualified by the evidence it
claims to answer.

### Scored against the 10 principles

| # | Principle | `generate.py` |
|---|---|---|
| 1 | Token-efficient output | **Pass** — JSON mode returns code + 4 scalars, no padding |
| 2 | Minimal default schema | **Pass** — 5 fields (`success`, `faust_code`, `attempts`, `error`, `reason`) |
| 3 | Truncation with size hints | **Fail** — no size hints anywhere, and it silently forwards *provider* truncation as a Faust syntax error ([[../research/truncation-confound-HANDOFF-S1]]) |
| 4 | Pre-computed aggregates | Partial — `attempts` only |
| 5 | Definitive empty states | **Pass** — `success:false` + typed `reason`, since `4bea5f3` |
| 6 | Structured errors, exit codes | Partial — ADR-011 JSON is good; subprocess mode deliberately always `exit 0` (`llm/generate.py:293`), so shell callers cannot branch on status |
| 7 | Ambient context | n/a — one-shot tool |
| 8 | Content first (no args) | **Fail, expensively** — see below |
| 9 | Contextual disclosure | **Fail** — no next-step hint on failure |
| 10 | Consistent help | **Fail** — no `argparse`, no `--help` |

### The finding that justifies the pilot on its own

`generate.py` parses `sys.argv` by hand and drops anything starting with `-`
(`llm/generate.py:308`), falling back to a hardcoded default prompt. Therefore:

```
python llm/generate.py --help      # generates a chorus, spends up to 3 provider requests
python llm/generate.py             # same
python llm/generate.py --typo      # same
```

Free-tier quota is this project's binding constraint, and gemini's measured ceiling is
**20 requests/day** (`llm/providers.py:93`). A single `--help` can burn 15% of a day's
budget. Principles 8 and 10 exist precisely to prevent this, and the fix is `argparse`
plus a no-args path that prints help instead of calling a provider.

**Size:** hours, not days. **Gate:** `--help` and `--dry-run` make zero network calls;
`llm/README.md` shrinks to a pointer.

---

## Rejected, with reasons

**[no-mistakes](https://github.com/kunchenguid/no-mistakes)** (7,075★, Go, MIT — verified
live) — a git proxy running an AI validation pipeline in an isolated worktree before
forwarding a push and opening a PR. Genuinely aimed at two audit findings. Rejected
because it treats the wrong bottleneck: the audit's complaint was that work *did not get
committed*, and a proxy adds friction to pushing. It also presumes a review step that, for
a solo developer, is the same person on both sides — the exact structure `COLLABORATION.md`
§9 already diagnosed as fictional in PAIR mode ("a self-graded rubric that never returns a
failure is not a control"). Installing it would repeat that mistake in Go. *If it is ever
adopted: build from source. Its quick install is `curl | sh`.*

**[lavish-axi](https://github.com/kunchenguid/lavish-axi)** (2,155★, JS, MIT — verified
live) — opens agent-generated HTML in a browser for annotation. Addresses the real finding
that the human decision queue is the bottleneck. Rejected on arithmetic: that queue holds
**three** items (confirm the state format, 15 minutes of listening, authorise a $0
benchmark), all pending for days. A three-item queue is not a tooling problem, and buying
a tool to manage it is the substitution the audit named.

---

## What the evidence says about the rest

The multi-agent question is settled and needs no pilot. All 28 configurations in the 2026
literature degraded against single-agent baselines (−4.4% to −35.3%); independent
multi-agent systems amplified errors 17.2× across 180 controlled configurations. This
project's own `docs/.fleet/RETROSPECTIVE.md:75` independently measured ~6–7× token burn and
concluded the fleet is "a burst tool, not a default." Agreement between an internal
retrospective and external literature is as strong as evidence gets here. **Act on it: one
lane by default. No pilot required — just stop.**

On git strategy for AI-heavy repos, the honest answer is that the marketed practices
(stacked branches, worktree-per-agent, elaborate commit taxonomies) have no controlled
evidence behind them, and this repo's actual defect was simpler: 43% of commits touch no
code and the two most consequential days produced none. The intervention is "commit at the
end of the session", which is already a stopping rule and needs no tool.

The one genuinely reusable idea from axi.md beyond the pilot is that **interface design
matters more than transport** — its benchmarks show a well-designed CLI beating MCP on
success rate *and* cost (100% at $0.050/task vs 82–87% at $0.101–0.148). That is worth
remembering before anyone proposes wrapping this project's tools in an MCP server.

---

**One line:** fix `generate.py`'s agent contract, delete the prose it replaces, keep one
lane, and drain the three-item human queue. Nothing else is a workflow problem.
