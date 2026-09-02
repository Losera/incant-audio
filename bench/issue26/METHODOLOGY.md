# issue #26 — methodology, limitations, and planned follow-up

The headline result (feeding faust-rs `--check` diagnostics into a small-model
repair loop *lowers* fix-within-2-attempts: 75%→44% on `qwen2.5-coder:3b`,
72%→50% on `7b`) is a real, paired, significant effect on this corpus. It is
**not** the last word, and this file is the honest list of what would have to
be tightened to make it one. Each item names the concrete work package that
addresses it.

## What the result does and does not say

- **Does:** on 202 distinct C++-rejected programs from a weak generator, a weak
  repair model recovers *more often* from raw C++ stderr than from faust-rs's
  localised diagnostic, and the difference is concentrated in the error classes
  where the caret is most precise (`routing_arity`, `syntax`,
  `hallucinated_symbol`). Mechanism, from the trajectories: precise localisation
  → the model edits at the caret and re-breaks the same spot; vague stderr →
  broader rewrite that compiles.
- **Does not:** say anything about a frontier repair model; say faust-rs's
  diagnostics are worse *for a human* (they are unambiguously better — 15/15
  source locations vs C++ 8/15, 15/15 stable codes vs 0/15); rule out that the
  effect is partly an artifact of how each arm's feedback is *wrapped* rather
  than its content.

## Known limitations → work packages

| # | limitation | status | WP |
|---|---|---|---|
| L1 | `score_repair_ab.py`'s `load_pairs` overwrites samples (`by_sha[sha][arm] = r`), so `--samples K` silently keeps 1 of K. Latent on the committed data (0 duplicate triples). | open | **WP1** |
| L2 | A generator exception (`RateLimited`, output-token cap) is scored as a failed repair and marked "done" in the resume set. On the 3B run this hit arm A 13×, arms B/C 5× each — so the 151/202 headline is if anything *conservative* for arm A. | open | **WP2** |
| L3 | Arm A vs arms B/C differ in three ways, not one: error payload, a `"The Faust compiler rejected your program."` header, and a closing `"Fix this and re-emit the complete program."` directive (both from `frs_check.render`). Median feedback length 97 / 637 / 262 chars (A/B/C) — though arm C behaving like B is evidence against verbosity as the cause. | open | **WP3** |
| L4 | "Fidelity" is checked with two cheap tiers only (non-blank-line shrink, expected-primitive retention). No render-level check (silence / NaN / DC / spectral match). | partial (`fidelity_gate.py` ships the two cheap tiers) | **WP4** |
| L5 | n = 1 per (program, arm). Run-to-run determinism at temperature 0 for the local stack is **not** audited; `docs/BUGS.md` records ~20% output flips on a related measurement. | open | **WP5** |
| L6 | Only 2 corrective attempts (the product loop's budget). Can't see whether faust-rs converges slower-but-better. | accepted (product constraint) | — |
| L7 | 3B ran 202 programs, 7B only 120, and the 7B is Q3 vs the 3B's Q4. | accepted for now | WP6 (removes the quant confound) |

## Work packages

- **WP1 — real per-cell aggregation.** Replace the `load_pairs` overwrite with a
  `(code_sha, arm)` → K-samples grouping; majority-green / median-attempts per
  cell; K=1 a strict no-op (the committed numbers must not move).
- **WP2 — honest transport failures.** Classify generator exceptions
  (`terminal_reason ∈ {compiled, compile_failed, rate_limited, timeout,
  transport_error}`); a transport failure is dropped from the cell, not scored
  as a failed repair; the resume set skips it rather than marking it done.
- **WP3 — matched wrapper.** A `framing=False` kwarg on
  `frs_check.render{,_minimal}` that drops the header and the closing directive;
  new arms A2/B2/C2 that share one wrapper so the *only* difference is the error
  payload. Re-run; if A2 ≈ B2 the published effect was mostly wrapper, and the
  reply says so.
- **WP4 — full fidelity gate.** Extend `fidelity_gate.py` with a render tier
  (`render_oracle.analyse` → silent / NaN / DC / never-decays, plus a spectral
  compliance score). Report the A/B on "repaired **and** still matches the
  prompt", not just "repaired".
- **WP5 — determinism audit.** 30 programs, arms A + B, K = 5, `qwen2.5-coder:3b`
  temp 0, warm GPU. Report byte-identity of attempt-1 outputs, unanimity of the
  green/not-green verdict, and a bootstrap CI on the arm-A rate. Pre-registered
  decision rule: unanimity ≥ 0.95 → n = 1 was warranted; < 0.80 → all claims
  move to majority-of-K.

## Frontier follow-up

A Stage-1 run on `openai/gpt-oss-120b` (reusing this corpus, matched-wrapper
arms A2/B2) is planned. Expectation is a **null** — a large model repairs most
weak-model failures under either arm — which would be reported as "no measurable
difference on weak-model failures", not "faust-rs is fine". The clean test is a
fresh corpus built from the frontier model's *own* failures.
