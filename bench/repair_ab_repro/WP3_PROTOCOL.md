# WP3 — matched-wrapper, matched-visibility repair A/B: pre-registered protocol

Written and committed **before** any of the arms below are run, per this project's own
precedent (WP5's pre-registered decision rule, `METHODOLOGY.md`). This supersedes the WP3
description in `METHODOLOGY.md` L3 to the extent it conflicts — this file is the operative
plan; L3/L12/L14 remain the record of why it's needed.

## Why this design, specifically

Two confounds are now on record between arm A (raw C++ stderr) and arms B/C (faust-rs) in
the original A/B:

1. **Wrapper wording** (L3, known since before this session) — arm A's correction template
   differs from arm B/C's.
2. **Source-line visibility** (L12/L14, found this session) — arm B/C's feedback always
   splices in one caret line; arm A's raw stderr *sometimes* leaks fragments of the program
   too (37% overall, 73% `routing_arity`, 93% `duplicate_symbol` — see L14), but this is an
   **uncontrolled, class-dependent accident** of Faust's box-expression dump, not something
   that can be toggled the same way for arm A. A clean design therefore does not try to force
   arm A into a matched-visibility cell; it isolates the visibility factor entirely on the
   faust-rs side (where it genuinely is a controlled toggle, via `source=None` vs
   `source=code`) and treats arm A's own leak rate as a **measured covariate**, not a designed
   factor.

## Arms

All arms share **one wrapper** (the `framing=False` addition below), run on the same 192
program-screened corpus (`bench/corpora/repair_corpus_20260830.json`), same repair model
(`qwen2.5-coder:3b`, local ollama, temperature 0, 2 corrective attempts — matches the
original scale and the product's real budget), same validate step.

| Arm | Diagnostic | Source line shown | Role |
|---|---|---|---|
| **A2** | raw C++ stderr | n/a (uncontrolled leak, measured post-hoc) | reference, wrapper-matched |
| **B2** | faust-rs `render()` (full) | yes | replicates original arm B, wrapper-matched |
| **B2n** | faust-rs `render()` (full) | **no** | isolates faust-rs *content* from visibility |
| **C2** | faust-rs `render_minimal()` | yes | replicates original arm C, wrapper-matched |
| **C2n** | faust-rs `render_minimal()` | **no** | isolates minimal-content from visibility |

## Implementation (harness changes, not yet made)

- `frs_check.render(result, source=None, *, max_notes=4, framing=True)` and
  `render_minimal(result, source=None, *, framing=True)`: when `framing=False`, drop the
  leading `"The Faust compiler rejected your program. "` and the trailing
  `"Fix this and re-emit the complete program."` (`frs_check.py:221-222,240`), leaving only
  `[CODE] message`, the optional `at line N` + caret block, and (for `render()`) notes/help.
  `source` continues to control the caret splice exactly as today — unchanged, just now
  independently toggleable from `framing`.
- `repair_ab_core.py`: a shared `WP3_TEMPLATE = "\n\nYour previous output had this compiler
  error — fix it:\n{feedback}"` (byte-identical to today's `ARM_A_TEMPLATE`) applied to
  **every** arm in this run, replacing the per-arm `ARM_A_TEMPLATE`/`ARM_FRS_TEMPLATE` switch
  for this protocol only (the original arms and their scoring are untouched — this is a new,
  additive run, not a rewrite of `bench/run_repair_ab.py`).
- `feedback_for` gains an arm table for A2/B2/B2n/C2/C2n; A2 is unchanged from today's arm A
  (`cpp_stderr`, no splice concept applies).

## Measured covariate (not a designed factor)

For every A2 record, compute `leak_score` from `cpp_stderr` using the same detection already
used to quantify L14:
- `0` — no match for `\b(?:hslider|vslider|nentry|button|checkbox|vgroup|hgroup|tgroup)\s*\(\s*"`
- `1` — matches, but the surrounding text is a desugared box-expression dump (heuristic: the
  stderr contains `\(x` or `.(` box-composition notation within 200 chars of the match)
- `2` — matches, and the stderr contains a `;`-terminated statement of the shape
  `IDENT = ... ;` reproducing a full definition line verbatim (the `duplicate_symbol` pattern)

Report `leak_score` distribution by `cpp_error_class` (replicating L14's table on THIS run's
draw, since temperature-0 local generation should reproduce the same corpus deterministically,
but confirm rather than assume) and carry it into the analysis below.

## Pre-registered analysis plan

**Primary endpoints** (paired exact McNemar, `bench/score_repair_ab.mcnemar_exact`, matching
the project's own established test):

1. **B2n vs A2** — the cleanest available test of "does faust-rs's diagnostic *content* hurt
   repair, independent of both wrapper and visibility." This is the closest thing to a fair
   re-test of the original headline.
2. **B2 vs B2n** (paired, same corpus, same model, only the source splice differs) — direct
   causal test of the visibility factor, holding content and wrapper fixed.
3. **C2 vs C2n** — same as 2, for the minimal renderer.
4. **A (original, product wrapper) vs A2 (matched wrapper)** — sanity check; A2's wrapper is
   already byte-identical to original arm A's, so this should show no effect. A real
   difference here would indicate a run-to-run determinism problem (L5), not a wrapper effect,
   and should stop the analysis for a determinism audit before trusting anything else.

**Exploratory, not confirmatory** (pre-registered as such — `leak_score` is not randomly
assigned, it's a near-deterministic function of `cpp_error_class`, so this cannot separate
"visibility helps/hurts" from "this error class is just easier/harder"):

5. Within A2 alone, `repaired-within-2` by `leak_score`, reported stratified by
   `cpp_error_class` (not pooled across classes) — descriptive only.

**Known limitation carried forward, not solved by this protocol:** the 202 raw / 192 screened
programs draw from ~100 distinct prompts (review finding, not yet re-verified this session) —
paired McNemar treats each program as independent, which is optimistic if two `routing_arity`
failures from the same prompt aren't independent draws. `score_repair_ab.py` gains a
`--cluster-by prompt_id` option that reports both the naive McNemar p and a permutation test
that shuffles within-prompt-id blocks; both are reported, neither is silently substituted for
the other.

## Pre-registered decision rule

Stated before any arm runs, so the interpretation isn't fit to whatever comes back:

- **B2n vs A2 still shows p < 1e-3** (the original headline's threshold) → the original
  finding — faust-rs's diagnostic content, not its wrapper or its visible line, makes this
  weak model's repair loop worse — is **confirmed under fair conditions**. The GRAME reply can
  say so plainly.
- **B2n vs A2 is not significant, but B2 vs B2n shows a real effect** (source visibility
  matters, content alone doesn't) → the original effect was **substantially the visibility
  confound**, not faust-rs's content per se. The GRAME reply needs to say the headline doesn't
  survive a fair comparison as stated, and that visibility — not diagnostic richness — is the
  operative variable.
- **Neither B2n vs A2 nor B2 vs B2n clears significance** → underpowered at this corpus size
  for the fair-comparison design; report a null with the stated MDE, do not claim either
  direction.
- **Any other pattern** (e.g., B2n vs A2 significant in the *opposite* direction) gets written
  up in full rather than forced into one of the above — this list is not exhaustive by
  construction, only the two directions anticipated going in.

## Explicitly not run by this protocol

Frontier-model repeat, the FRS-code-as-branch-signal idea, and L13's unstripped
repair-sequence arm all remain separately open — adding them here would confound this
protocol's own factorial. Each is its own follow-on once this result is in.
