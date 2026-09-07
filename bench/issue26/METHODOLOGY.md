# issue #26 — methodology, limitations, and planned follow-up

The headline result (feeding faust-rs `--check` diagnostics into a small-model
repair loop *lowers* fix-within-2-attempts: on the program-screened corpus,
**74%→43%** on `qwen2.5-coder:3b`, **73%→49%** on `7b`) is a real, paired,
significant effect on this corpus (McNemar exact *p* < 1e-8 on every 3B headline
cell). It is **not** the last word, and this file is the honest list of what
would have to be tightened to make it one. Each item names the concrete work
package that addresses it. `verify.py` checks every number here that is
derivable from the committed data.

## What the result does and does not say

- **Does:** on 192 program-screened C++-rejected programs from a weak generator,
  a weak repair model recovers *more often* from raw (capped) C++ stderr than
  from faust-rs's localised diagnostic, and the difference is concentrated in
  the error classes where the caret is most precise — `routing_arity`
  (McNemar *p* ≈ 5e-6), `syntax` (≈ 6e-6), `hallucinated_symbol` (≈ 2e-3). The
  one class where faust-rs is nominally *better* is `unclassified` (n=17,
  *p* = 0.29), where C++ stderr carries no location. Mechanism, from the
  trajectories: faust-rs quotes the offending source line back, and the model
  then treats it as fixed and edits *around* it — the quoted line survives
  verbatim into attempt 1 in 58% of arm-B rewrites vs 4% of arm A's (McNemar
  *p* ≈ 2e-22, replicated on the 7B; `verify.py` checks it). Vague C++ stderr
  instead makes the model discard and rewrite, which compiles more often. The
  split is on the *second* corrective attempt: rescue-after-attempt-1-failed is
  49/88 (56%) for arm A vs 20/126 (16%) for arm B; same-class recidivism 78%
  under faust-rs vs 54% under C++ stderr.
- **Does not:** say anything about a frontier repair model; say faust-rs's
  diagnostics are worse *for a human* (they are unambiguously better — 15/15
  source locations vs C++ 8/15, 15/15 stable codes vs 0/15); rule out that the
  effect is partly an artifact of how each arm's feedback is *wrapped* rather
  than its content.

## Known limitations → work packages

| # | limitation | status | WP |
|---|---|---|---|
| L1 | `score_repair_ab.py`'s `load_pairs` used to overwrite samples (`by_sha[sha][arm] = r`), so `--samples K` silently kept 1 of K. **Fixed (WP1):** `load_pairs` now groups each `(code_sha, arm)` cell and `_aggregate_cell` collapses it by majority-green / upper-median attempts-to-green before pairing (a tie is not-repaired); `tests/test_score_repair_ab.py` covers it. K=1 — every cell in the committed data — is a strict no-op, so the published numbers are unchanged. | fixed | **WP1** |
| L2 | When the generator raises before producing a program, the loop aborts. On the committed **local ollama** runs all 23 such aborts were `OutputTruncated` (the model hit the 4096-token cap): 3B arm A 12, B 4, C 5; 7B 0/0/0 — so the 3B headline is if anything *conservative* for arm A. A hosted run adds `RateLimited` to this class, and arm B's ~6× longer prompt makes it more exposed. **Fixed in `repair_ab_standalone.py` (P5/WP2):** such records carry `terminal_reason` and `score_repair_ab.py` excludes them from the arm comparison; `--resume` retries them; a run that aborts ≥25% exits non-zero. `bench/run_repair_ab.py` (the in-repo harness) still needs the same treatment. | fixed for the standalone; open for `run_repair_ab.py` | **WP2** |
| L3 | **The arms' correction wrappers are not matched.** Arm A wraps the payload with `"Your previous output had this compiler error — fix it:"` (`repair_ab_core.py:44`); arms B/C use only a bare lead-in (`repair_ab_core.py:47`) because `frs_check.render` already supplies a `"The Faust compiler rejected your program. …"` header and a closing `"Fix this and re-emit the complete program."` directive (`frs_check.py:222,244`). This bears directly on the **caret-line-preservation** signature (§"What the result does") — a "re-emit the complete program" instruction plausibly nudges toward verbatim reproduction of the shown line, independently of the caret. Arm C behaving like arm B is evidence against *verbosity* as the driver, but not against the wrapper wording. Median feedback length 97 / 637 / 262 chars (A/B/C). A byte-matched A2/B2/C2 with identical wrappers is the fix. | open | **WP3** |
| L4 | "Fidelity" is checked with two cheap tiers only (non-blank-line shrink, expected-primitive retention). No render-level check (silence / NaN / DC / spectral match). | partial (`fidelity_gate.py` ships the two cheap tiers) | **WP4** |
| L5 | n = 1 per (program, arm). Run-to-run determinism at temperature 0 for the local stack is **not** audited; `docs/BUGS.md` records ~20% output flips on a related measurement. | open | **WP5** |
| L6 | **Arm A's C++ stderr is truncated at 500 chars** (`bench/run_benchmark.py:246`, mirroring the product loop); arms B/C's faust-rs output is uncapped. The cap fires on 34 of the 192 screened programs (39 of the raw 202), mostly `routing_arity`, where it cuts the tail of Faust's box-expression dump. It **handicaps** arm A, and it is not driving the result: on the 158 programs where arm A's stderr was never truncated, arm A repairs 118/158 (75%) vs arm B 71/158 (45%), McNemar *p* ≈ 3e-7 (`verify.py` checks both strata). Making arm B symmetric would need a model re-run. | disclosed + stratified | **WP3** (matched wrappers) |
| L7 | Arms B/C silently fall back to arm A's text if `frs_check.check()` returns `None` or `ok` mid-run — a missing binary, a crash, a timeout, or faust-rs *accepting* a program C++ rejects (a real divergence). The harnesses guard binary presence at t=0 only; after that the fallback is silent and the record's `feedback_code` is `None`, indistinguishable from "faust-rs rejected it but gave no code". Fired **once** in the committed 3B run (1/327). | open | — |
| L8 | Only 2 corrective attempts (the product loop's budget). Can't see whether faust-rs converges slower-but-better. | accepted (product constraint) | — |
| L9 | 3B ran 202 programs (192 screened), 7B only 120 (115 screened), and the 7B is Q3 vs the 3B's Q4. | accepted for now | WP6 (removes the quant confound) |
| L10 | 10 of the 202 distinct C++-rejected rows are not Faust programs (9 prose, 1 truncated). `bench/corpus_screen.py` drops them mechanically (no top-level `process`, or a literal `...`); everything published is the screened view, and `--no-screen` reproduces the raw 202 (75/44/43 — same finding). | fixed | — |

## Work packages

- **WP1 — real per-cell aggregation.** *(Done — `score_repair_ab._aggregate_cell`.)*
  `load_pairs` groups each `(code_sha, arm)` cell and collapses it before pairing:
  majority vote on `repaired` (a tie is not-repaired), the upper median of the
  green samples' attempts-to-green, and the representative `attempt_log` from the
  first sample matching the majority verdict (so rescue / caret-preservation /
  cap-strata still run on one real trajectory). K=1 is a strict no-op — the
  committed numbers do not move.
- **WP2 — honest transport failures.** *(Done for `repair_ab_standalone.py` +
  `score_repair_ab.py`; `bench/run_repair_ab.py` still to do.)* Generator
  exceptions set `terminal_reason` (`rate_limited` | `truncated` | `timeout` |
  `empty_response` | `transport_error`), classified by exception class name so
  `repair_ab_core.py` keeps its leaf-import guarantee. Such records are excluded
  from the arm comparison, retried on `--resume`, and a run that aborts ≥25%
  exits non-zero instead of printing a clean null.
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
