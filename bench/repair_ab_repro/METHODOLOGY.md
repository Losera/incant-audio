# Repair-loop A/B — methodology, limitations, and planned follow-up

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
  *p* = 0.29), where C++ stderr carries no location. The associated
  trajectory-level statistics still hold: the quoted line survives verbatim
  into attempt 1 in 58% of arm-B rewrites vs 4% of arm A's (McNemar
  *p* ≈ 2e-22, replicated on the 7B; `verify.py` checks it); rescue-after-
  attempt-1-failed is 49/88 (56%) for arm A vs 20/126 (16%) for arm B;
  same-class recidivism 78% under faust-rs vs 54% under C++ stderr.
  **⚠ Interpretive correction (2026-09-24, L12):** the sentence that used to
  stand here — *"faust-rs quotes the offending source line back, and the model
  then treats it as fixed and edits around it"* — is **withdrawn**. Neither
  arm's `user_message` (`repair_ab_core.py:107`) contains the failing program;
  it is `prompt + feedback`, where `prompt` is the original natural-language
  request and `code` is used only to compute the diagnostic (`:105`), never
  shown. The model is not "editing around" a program it can see — it is
  regenerating from the spec each attempt. `feedback_for` (`:84`) *does* splice
  the offending line into arm B/C's own feedback text via `render()`'s
  `source` argument (`frs_check.py:222`) — so the 58%/4% split measures
  whether that supplied line is copied into the next attempt, not whether the
  model anchors on a visible caret and edits defensively around it. The
  statistics above are correct measurements of a different mechanism than the
  one originally claimed. See L12.
- **Does not:** say anything about a frontier repair model; say faust-rs's
  diagnostics are worse *for a human* (they are unambiguously better — 15/15
  source locations vs C++ 8/15, 15/15 stable codes vs 0/15); rule out that the
  effect is partly an artifact of how each arm's feedback is *wrapped* rather
  than its content, or of source-line visibility differing by arm (L3, L12);
  say the repair loop as tested resembles a human's or a stronger model's
  repair workflow, where the failing program IS normally visible.

## Known limitations → work packages

| # | limitation | status | WP |
|---|---|---|---|
| L1 | `score_repair_ab.py`'s `load_pairs` used to overwrite samples (`by_sha[sha][arm] = r`), so `--samples K` silently kept 1 of K. **Fixed (WP1):** `load_pairs` now groups each `(code_sha, arm)` cell and `_aggregate_cell` collapses it by majority-green / upper-median attempts-to-green before pairing (a tie is not-repaired); `tests/test_score_repair_ab.py` covers it. K=1 — every cell in the committed data — is a strict no-op, so the published numbers are unchanged. | fixed | **WP1** |
| L2 | When the generator raises before producing a program, the loop aborts. On the committed **local ollama** runs all 23 such aborts were `OutputTruncated` (the model hit the 4096-token cap): 3B arm A 12, B 4, C 5; 7B 0/0/0 — so the 3B headline is if anything *conservative* for arm A. A hosted run adds `RateLimited` to this class, and arm B's ~6× longer prompt makes it more exposed. **Fixed in `repair_ab_standalone.py` (P5/WP2):** such records carry `terminal_reason` and `score_repair_ab.py` excludes them from the arm comparison; `--resume` retries them; a run that aborts ≥25% exits non-zero. `bench/run_repair_ab.py` (the in-repo harness) still needs the same treatment. | fixed for the standalone; open for `run_repair_ab.py` | **WP2** |
| L3 | **The arms' correction wrappers are not matched.** Arm A wraps the payload with `"Your previous output had this compiler error — fix it:"` (`repair_ab_core.py:44`); arms B/C use only a bare lead-in (`repair_ab_core.py:47`) because `frs_check.render` already supplies a `"The Faust compiler rejected your program. …"` header and a closing `"Fix this and re-emit the complete program."` directive (`frs_check.py:222,244`). This bears directly on the **caret-line-preservation** signature (§"What the result does") — a "re-emit the complete program" instruction plausibly nudges toward verbatim reproduction of the shown line, independently of the caret. Arm C behaving like arm B is evidence against *verbosity* as the driver, but not against the wrapper wording. Median feedback length 97 / 637 / 262 chars (A/B/C). **WP3 now also needs to vary source-line visibility (L12), not just the wrapper** — an A2/B2/C2 with matched wrappers AND matched source visibility is the real fix. | open | **WP3** |
| L4 | "Fidelity" is checked with two cheap tiers only (non-blank-line shrink, expected-primitive retention). No render-level check (silence / NaN / DC / spectral match). | partial (`fidelity_gate.py` ships the two cheap tiers) | **WP4** |
| L5 | n = 1 per (program, arm). Run-to-run determinism at temperature 0 for the local stack is **not** audited; `docs/BUGS.md` records ~20% output flips on a related measurement. | open | **WP5** |
| L6 | **Arm A's C++ stderr is truncated at 500 chars** (`bench/run_benchmark.py:246`, mirroring the product loop); arms B/C's faust-rs output is uncapped. The cap fires on 34 of the 192 screened programs (39 of the raw 202), mostly `routing_arity`, where it cuts the tail of Faust's box-expression dump. It **handicaps** arm A, and it is not driving the result: on the 158 programs where arm A's stderr was never truncated, arm A repairs 118/158 (75%) vs arm B 71/158 (45%), McNemar *p* ≈ 3e-7 (`verify.py` checks both strata). Making arm B symmetric would need a model re-run. | disclosed + stratified | **WP3** (matched wrappers) |
| L7 | Arms B/C silently fall back to arm A's text if `frs_check.check()` returns `None` or `ok` mid-run — a missing binary, a crash, a timeout, or faust-rs *accepting* a program C++ rejects (a real divergence). The harnesses guard binary presence at t=0 only; after that the fallback is silent and the record's `feedback_code` is `None`, indistinguishable from "faust-rs rejected it but gave no code". Fired **once** in the committed 3B run (1/327). | open | — |
| L8 | Only 2 corrective attempts (the product loop's budget). Can't see whether faust-rs converges slower-but-better. | accepted (product constraint) | — |
| L9 | 3B ran 202 programs (192 screened), 7B only 120 (115 screened), and the 7B is Q3 vs the 3B's Q4. | accepted for now | WP6 (removes the quant confound) |
| L10 | 10 of the 202 distinct C++-rejected rows are not Faust programs (9 prose, 1 truncated). `bench/corpus_screen.py` drops them mechanically (no top-level `process`, or a literal `...`); everything published is the screened view, and `--no-screen` reproduces the raw 202 (75/44/43 — same finding). | fixed | — |
| L11 | **The faust-rs binary that produced these numbers was never verified to be the pinned tag.** 2026-09-23 audit (`docs/records/faust-rs-delta-2026-09-23.md`): `faust-rs --version` reports `0.8.0` identically for the true `0.8.0` tag and for every build up to and including current `main` (216 commits later) — the Cargo package version was never bumped in that span, so the old Dockerfile's `--version` guard could not have caught drift even in principle. The 2026-08-30 binary this repo actually measured PF-076/#26 with reports `0.8.0` but carries `--dump-sig-dag`, a flag absent at the tag — so it was never really the tag, and its exact commit cannot be recovered (source checkout gone). Re-derived directly (`bench/frs_build_compare.py`, `bench/frs_rederive.py`) against the true tag, that same binary, and current `main`: all three agree 191/191 on the full screened corpus, and the 15-cell diagnostic-quality figures above reproduce bit-for-bit on all three. A provenance gap, not (as far as re-derivation on this corpus can tell) a correctness gap. | fixed (Dockerfile now pins `--rev` to a commit, guarded by flag-presence instead of version string) | — |
| L12 | **Source visibility is not a controlled factor — it differs BY ARM, and arm A's "no visibility" side is itself not clean (see L14).** `repair_ab_core.repair_loop`'s `user_message` (`:107`) is `prompt + feedback`; the failing program is used only to compute the diagnostic (`:105`), never placed in the message, in either arm. But `feedback_for` (`:84`) passes `code` as `render()`'s `source` argument, which **splices the offending source line into arm B/C's feedback only** (`frs_check.py:222` `_source_caret`). The published "caret-line-preservation" mechanism (58%/4%, §"What the result does") is confounded by this: it cannot distinguish "the model anchors on a visible caret and edits around it" from "the model copies text it was handed, because it has nothing else to go on." Discovered 2026-09-24, prompted by an external adversarial review, verified directly against source before accepting the review's claim. | open, interpretive correction landed (§"What the result does") | **WP3** |
| L13 | **`frs_check._clean_message` strips faust-rs's own repair-suggestion list from every arm that uses it** (`frs_check.py:185-193`), truncating at `"Repair sequences found"`. Per this module's own header, the 5 of 15 diagnostic-quality cells with no `help` remedy are `FRS-PARSE-0001`, "which instead carry LR-parser repair sequences" — for that subset, the stripped content was the *only* actionable remedy faust-rs offered. Arms B and C were therefore never tested with faust-rs's full repair-oriented output; the measured effect is faust-rs's diagnostics **minus its own suggested fixes**, vs raw C++ stderr. Not tested this session (would need a new arm). | open | — |
| L14 | **Arm A is not a clean "zero program visibility" baseline — the C++ box-expression dump leaks real program content in a class-dependent way, and L12's "arm A never sees any part of the program" was itself an overclaim.** Measured directly against `bench/corpora/repair_corpus_20260830.json` (207 failing-stderr records): **76/207 (37%)** contain a literal widget-constructor string (`hslider("Gain", 1.0f, 0.01f, 1e+01f, 0.01f)` and similar — label text, numeric ranges, the model's actual written arguments). This is not uniform across error classes, and the shape matters more than the aggregate: **routing_arity 61/84 (73%)** — the class where the A-vs-B/C effect is strongest (McNemar *p* ≈ 5e-6) — leaks widget fragments, but *embedded in an unreadable, heavily desugared box-expression dump* (`\(x13).(\(x14).(((1.0f,(1,...`), not a readable source line; a model would struggle to use it as "the code." **`duplicate_symbol` is the sharp counter-example: 14/15 (93%) show TWO full, syntactically valid Faust definition lines reprinted verbatim** (`buttonPress = hslider(...) : si.smoo : ...; buttonPress = si.smoo(buttonPress);`) — strictly *more* raw source than arm B/C's single spliced line, in the arm the correction record calls "zero visibility." Some `unclassified` (range-validation) errors similarly quote a full, clean source expression. Net effect on the WP3 design: a matched-visibility experiment cannot simply assume arm A = 0 lines / arm B,C = 1 line as a controlled binary — visibility has to be *measured per record* (e.g. via the same widget-constructor regex, or a stronger AST-aware check) and used as a covariate or stratifying variable, not asserted. Found 2026-09-24 by an independent audit re-checking the L12 correction itself, then verified directly against the corpus before being accepted. | open, discovered while re-auditing L12 | **WP3** |

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
