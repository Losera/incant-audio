# PluginForge — health schema

**What this file is:** the rubric — dimensions, what counts as evidence for each,
and the GREEN/AMBER/RED rule. It is deliberately the only hand-written document in
this system. Everything it scores is computed by `tools/health_report.py` from
existing artifacts (test output, `docs/BUGS.md`, `docs/decisions.md`, STATUS.md) —
not retyped here, not re-asserted here. A rubric cannot drift from the state it
describes; a second scoreboard can, and `docs/BUGS.md`'s own preamble already
records this project's two worst instances of exactly that drift.

**What this file is not:** a dashboard, a current-status report, or a replacement
for `STATUS.md`. For "what is true right now," run:

```
python tools/health_report.py --lane product      # $0, instant, document-only
python tools/health_report.py --collect            # full report (reads/runs all lanes)
```

which write `artifacts/health/health_<date>.{json,md}`. This file does not get a
date stamp and is not rewritten per session — it changes only when the *rubric*
changes, the way a grading key changes less often than the grades.

## Why a fourth lane, not a second tool

`tools/health_report.py` already measured **harness execution** — did the tests
run, what did they assert, with a strict `absent`-vs-`0` distinction (a harness
that never ran reads `"status": "absent"`, never a passing zero — the project's
"signature defect is a control believed to run that does not"). It has three
lanes, separated by cost and contention:

| Lane | Cost | Needs |
|---|---|---|
| `dsp` | $0 | nothing — parallel-safe |
| `ui` | $0 | a real display (no `xvfb-run` on the dev box) |
| `ai` | spends provider quota | the PF-025 lock, `--i-authorize-spend` |

What was missing was the **document layer**: defect burden, planned-but-undecided
work, and the two recurring drifts this project's own files admit to. That is
`product` — a fourth lane, $0, no network, no display, no build, no lock, so it
can run inside `level_full` unconditionally. It was wired in because it had run
exactly **once**, by hand, on 2026-07-30, and `tools/check.sh` carried only a
comment mentioning it, never an invocation.

## Dimensions

Six. Each is scored **only** from a signal `tools/health_report.py` can derive —
if a dimension's signal cannot be computed, it reads `absent`, never a guessed
color. This preserves the existing script's core discipline rather than replacing
it with a human's judgment call re-made every session.

| Dimension | Derived from | Lane |
|---|---|---|
| Generation efficacy | **render-safety**, not compile rate (see below) — `bench/results/efficacy/*.json`, `bench/classify_failures.py`'s 13-class taxonomy, PF-031's noise floor | `ai` |
| Audible correctness | `bench/render_oracle.py` verdicts over `bench/ladder_corpus.json`; open `critical`/`high` rows tagged audio in `docs/BUGS.md` | `dsp` |
| Host behaviour | `pluginval`/Carla evidence (from STATUS.md "Works"); count of open rows tagged in-host in `docs/BUGS.md` (PF-072..075 at this writing — the count is read from the registry at report time, not hardcoded, so later in-host findings are picked up automatically) | `product` (counts) + human (the behaviour itself — see "What this cannot score," below) |
| State & session | `StatePersistenceTest` + `EditorSessionTest` check/failure counts | `dsp` / `ui` |
| Distribution | clean-checkout rehearsal evidence; `docs/distribution.md` | `product` (presence of evidence, not the rehearsal itself) |
| GUI / face | ADR-038 E/F step completion + ADR-041 ladder position, read from `docs/decisions.md` | `product` |

### Generation efficacy: score render-safety, not compile rate

**PF-079** (2026-09-23, $0, 325 compiled cells across four committed archives) found
that `faust -lang cpp` accept/reject overstates real render-safety by **25–40
percentage points**, and two `dynamics` idioms fail identically across two
independent generator models. ADR-006 named first-try compile rate "the primary
benchmark metric"; PR #88 already reclassified that as one of 8 routine decisions
dressed as ADRs, which is itself a signal it should not anchor a health score.
**This schema scores the render-oracle pass/fail/unsupported verdict, never the
bare compile boolean.** A health tool that inherited the 25–40pp optimistic bias on
day one would be worse than no tool.

### Host behaviour: counts are derivable, the behaviour is not

The `product` lane can count how many PF-rows tagged "in-host" are open — that is a
defect-burden fact. It cannot tell you whether a reopened window actually looks
right in a real host; a defect of that shape is found by a human running the
real DAW, not by any harness. See "What this schema cannot score," below.

## Scoring

Three bands per dimension, computed the same way for all six:

- **GREEN** — the latest evidence is current (see "Freshness," below), shows no
  open `critical`/`high` defect in that dimension, and the relevant harness(es)
  ran (not `absent`).
- **AMBER** — evidence exists but is stale, incomplete (e.g. PF-031's n≥3 bar
  unmet), or shows only `medium`/`low` open defects.
- **RED** — an open `critical`/`high` defect exists in that dimension, the
  relevant harness is `absent`, or no evidence has ever been recorded.

**A dimension with no computable signal is `absent`, not a band.** This mirrors
`tools/health_report.py`'s existing `--strict` contract (any absence is a non-zero
exit) rather than inventing a second meaning for the same word.

### Freshness

Evidence is current if it postdates the most recent change to the code path it
measures — not a fixed day count. A dimension whose evidence predates a relevant
commit (checked via `git log --since` against the files the dimension's harness
covers) degrades one band rather than being silently trusted. This is deliberately
conservative: PF-031's own finding is a measured 4.0% noise floor between two
*identical* back-to-back runs, so "it passed once, a while ago" is a weak claim on
this codebase's own evidence.

## What this schema cannot score

Carried forward verbatim from `tools/health_report.py`'s own `no_instrument` list,
because a rubric that silently drops a known gap is worse than one that states it:

- Whether a generated plugin **sounds** like what was asked (COLLABORATION.md §1).
  Not delegable to a hook or a model.
- Whether the parameter grid **looks** right, as opposed to reporting right
  strings — including whether a REOPENED window's GUI looks right at all
  (a class of defect no harness in this repo watches for independently).
- The DAW's own automation lane, which still shows raw 0–1 slots.
- Semantic fidelity (PF-013) — needs a judge model independent of the generator;
  none is installed free.
- Whether a generated face reads as a **professional product** (the GUI-generation
  research track, ADR-044) — the `UiDesignGallery` harness proves a face rendered,
  not that it looks designed.

## The "planned work" roll-up

One `planned` view, assembled from the four places plans actually live today, each
tagged with its source so a stale entry is attributable to the document that owns
it rather than to the health tool:

1. **`STATUS.md` "Next three things"** — the live priority. One slot is reserved
   for an `*(evidence)*`-tagged item (enforced by `tests/test_control_wiring.py`).
2. **`docs/decisions.md` ADRs with `**Status** Proposed`** — a Proposed ADR *is*
   planned-but-undecided work (COLLABORATION.md §2 trigger 2: the decision needs a
   human). Nothing aggregated these before this schema.
3. **`docs/BUGS.md` rows with status `open` / `in-progress`** — the durable defect
   backlog, ranked by severity.
4. **`PLUGIN_HEALTH_PLAN.md` unticked boxes** — flagged `stale` in the output,
   since that file disclaims its own freshness ("treat STATUS.md as authoritative
   for current state; the checkboxes below have not all been re-ticked").

## Known drifts this schema surfaces, not fixes

Two drifts are already on record in this project's own files. The health tool
**reports** them; it must never silently correct the document to make its own
counter agree, because the drift *is* the finding.

1. **The `assumed` metric undercounts its own section.** `tools/check.sh`'s
   `level_assumed()` counts `- **bolded**` list bullets under STATUS.md's
   "Assumed, never checked" heading. That section has at least once been rewritten
   to lead with a bolded *sentence* in plain prose rather than a bulleted one — the
   counter read 0 against a section that, read by a human, named a live claim
   (PF-079's render-safety finding). `lane_product()`'s `_check_assumed_drift()`
   re-runs the counter's exact regex and separately counts bolded prose in the same
   section; a mismatch is reported, not patched.
2. **`docs/BUGS.md` and `STATUS.md` have drifted on a row's status before** (PF-011,
   resolved 2026-09-24 after 16 days of disagreement). `docs/BUGS.md`'s own preamble
   instructs a reader to "believe neither, read the code at HEAD" when they
   disagree. `lane_product()` reports `docs/BUGS.md`'s registry as the durable
   source — consistent with that file's own stated authority — and does not attempt
   a second cross-file reconciliation pass; `tests/test_bugs_registry_integrity.py`
   already owns BUGS.md's internal (registry-vs-detail-section) consistency.

## Reuse, don't reimplement

Every parser in `lane_product()` reuses an existing source of truth rather than
adding a second implementation of the same rule:

- `tests/test_bugs_registry_integrity.py`'s `parse_registry()` and
  `_REGISTRY_ROW_RE` — the exact function `test_bugs_registry_integrity.py` already
  enforces BUGS.md against, imported directly so the two can never disagree about
  what a row says.
- `bench/classify_failures.py`'s `summarise()` — the 13-class failure taxonomy,
  shared with the production retry loop via `llm/error_classes.py`.
- `tools/health_report.py`'s own `score_runs()` — already emits `noise_floor` and
  `stability`; `stability` is "the number that matters more" (a prompt failing
  *every* run with the *same* class is a defect; anything else is sampling noise).
- `tools/check.sh`'s `level_assumed()` section-extraction shape (find the `##`
  heading, stop at the next one) — mirrored, not re-derived, for
  `_scan_status_next_three()` and `_check_assumed_drift()`.

`tools/kg.py` / `tools/id_graph.py` (ADR-031's ID-resolution graph over
PF-/ADR-/session references) is the right tool for validating that a reference
*exists* and is spelled consistently; it is a viewing tool, not a gate
(`tests/test_control_wiring.py` owns the gates), and this schema does not
duplicate it.

## Change control

This file changes only when a dimension, scoring rule, or evidence requirement
changes — not when a number changes. A change here is a rubric change, which is
visible, deliberate, and (per COLLABORATION.md §3) carries the same evidence bar as
any other consequential change: a primary source, a test, and a stated remainder of
what was not verified.
