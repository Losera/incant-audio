# issue-#26 data schema

Committed JSON under `bench/corpora/` and `bench/results/repair_ab/`:

| file | shape |
|---|---|
| `corpora/repair_corpus_20260830.json` | list of generation records |
| `corpora/repair_corpus_20260830_excluded.json` | list — the 10 rows the program screen drops |
| `results/repair_ab/repair_ab_20260830{,_7b}.json` | list — one record per (program, arm[, sample]) |
| `results/repair_ab/repair_ab_20260830{,_7b}_fidelity.json` | **object** — `meta` / `summary` / `summary_screened` / `cells` |
| `results/repair_ab/repair_ab_20260830{,_7b}_summary.json` | list of `score_repair_ab.report()` dicts |
| `results/repair_ab/repair_ab_20260830{,_7b}_report.txt` | the human-readable score run |
| `results/repair_ab/*_chart.png` | the grouped-bar charts |
| `results/repair_ab/frs_rederive_20260830.txt` | the diagnostic-quality comparison table |
| `issue26/expected.json` | **object** — the frozen expectations `verify.py` checks (schema 2) |

---

## Corpus — `repair_corpus_20260830.json`

One record per first-attempt generation. The **corpus** is the subset with
`compiles == false`; `compiles == true` records are kept only so the per-config
first-try rate is auditable. The A/B keeps the **first occurrence of each
`code_sha`** among the failing records (deduplication) → 202 distinct programs,
**192 after `bench/corpus_screen.py`** removes rows that are not Faust programs
(see `_excluded.json`).

| field | type | meaning |
|---|---|---|
| `source` | str | `"tiered"`, `"benchmark25"`, or `"archive:<name>"` for seeded failures |
| `prompt_id` | str | prompt identity, e.g. `"trivial-01/L1"` |
| `category` | str | prompt category (`trivial`, `distortion`, …) |
| `tier` | str \| null | difficulty tier (`L0`…`L4`), else null |
| `prompt` | str | the natural-language request sent to the model |
| `config` | str | `"<model>@<temp>"` that generated it, or `"archive"` |
| `model` | str | generating model |
| `temperature` | float \| null | generating temperature |
| `compiles` | bool | did `faust -lang cpp` accept it |
| `code` | str | the generated Faust source (raw model output, fences stripped) |
| `code_sha` | str | `sha256(code)[:16]` — the identity key across every file |
| `cpp_stderr` | str | `faust -lang cpp` stderr, **`.strip()[:500]`** — this is **arm A's feedback**, capped exactly as PluginForge's shipping repair loop caps it (`bench/run_benchmark.py`). Empty if `compiles`. 39 of the 202 distinct failing rows are exactly 500 chars, i.e. truncated (35 of them `routing_arity`). Arms B/C's feedback is NOT capped at 500 — see METHODOLOGY L6. |
| `cpp_error_class` | str \| null | `llm/error_classes.classify_error(cpp_stderr)`. `verify.py` re-checks that every stored value still matches the classifier. |
| `frs_codes` | list[str] \| null | faust-rs stable codes at corpus-build time (advisory; the A/B recomputes live) |
| `frs_feedback` | str \| null | `frs_check.render(...)` output at corpus-build time (advisory) |
| `timestamp` | str | ISO-8601 UTC |

### `repair_corpus_20260830_excluded.json`

`{code_sha, prompt_id, tier, cpp_error_class, screen_reason, code_head}` for each
of the 10 excluded rows. `screen_reason` ∈ `{no_process_definition,
truncated_ellipsis}`.

---

## A/B result — `repair_ab_20260830{,_7b}.json`

One record per **(program, arm)** — a full 3-arm run over 202 programs is 606
records; the committed 7B file is a 120-program stratified subset (360 records).
`bench/score_repair_ab.py --screen CORPUS` pairs them by `code_sha` within a
`repair_model`, restricted to the screened programs.

| field | type | meaning |
|---|---|---|
| `prompt_id`, `category`, `tier` | | copied from the corpus entry |
| `corpus_config` | str | the `config` that first produced this program |
| `code_sha` | str | **pairing key** — same program, compared across arms |
| `first_error_class` | str | class of the *starting* C++ error |
| `arm` | str | `"A"` (capped C++ stderr) / `"B"` (faust-rs full) / `"C"` (faust-rs core) |
| `repair_model` | str | the fixed model doing the repair (not the generating model) |
| `repaired` | bool | did it compile within `CORRECTIVE_ATTEMPTS` (2) |
| `attempts_to_green` | int \| null | 1 or 2; null if never |
| `attempts_used` | int | attempts actually run |
| `second_error_class` | str \| null | class of the error after corrective attempt 1 |
| `second_error_same_as_first` | bool \| null | did attempt 1 re-break it the same way (null when no program was produced) |
| `terminal_reason` | str \| null | `null` once a program is produced; else `rate_limited` / `truncated` / `timeout` / `empty_response` / `transport_error` — an infra abort, **not** a repair failure. `score_repair_ab.py` excludes these from the comparison. Absent on the frozen pre-2026-09 data (reads as `null`). |
| `attempt_log` | list | per-attempt detail, below |
| `timestamp` | str | ISO-8601 UTC |
| `sample_index` | int | *(standalone `--samples>1` only)* which sample this is |
| `run_meta` | obj | *(standalone only)* the full run `meta` (endpoint, model, temperature, faust/faust-rs versions, corpus, …) |

### `attempt_log[i]`

| field | type | meaning |
|---|---|---|
| `n` | int | attempt number (1-based) |
| `feedback_arm` | str | which arm's feedback was shown |
| `feedback_code` | str \| null | faust-rs primary code for this attempt (arms B/C; null if faust-rs gave no code, or if it fell back to arm A — METHODOLOGY L7) |
| `feedback_text` | str | the exact error text put in the prompt (log copy truncated to 1200 chars; arm A's is ≤ 500 by construction, arms B/C up to ~1000) |
| `code` | str | the model's new program |
| `code_sha` | str | its hash |
| `cpp_ok` | bool | did `faust -lang cpp` accept the new program |
| `cpp_stderr` | str | its stderr (empty if `cpp_ok`) |
| `cpp_error_class` | str \| null | class of that stderr |
| `wall_s` | float | generation wall time |
| `error` | str | *(only if the generator raised)* `"<Type>: <msg>"`; the loop then stops and `terminal_reason` is set |

---

## Fidelity sidecar — `repair_ab_20260830{,_7b}_fidelity.json`

`bench/fidelity_gate.py` output. `meta` records the inputs;
`cells` maps `"<code_sha>::<arm>"` → per-cell verdict (`repaired`, `shrank`,
`shrink_ratio`, `primitives_expected`, `pre_has_primitive`, `post_has_primitive`,
`primitive_lost`); `summary` aggregates over **all 202** programs' wins;
`summary_screened` over the **192** screened. `verify.py` recomputes
`summary_screened` from `cells` — it does not trust the stored value.

---

## Scoring conventions (`bench/score_repair_ab.py`)

- **multi-sample cells** (a `--samples K>1` run): `load_pairs` collapses each
  `(code_sha, arm)` cell via `_aggregate_cell` before pairing — majority vote on
  `repaired` (a tie is not-repaired), the upper median of the green samples'
  `attempts_to_green`, and `attempt_log` / second-error fields from the first
  sample matching the majority verdict. The synthetic record carries
  `samples_aggregated` + `samples_green`. K=1 (every committed cell) is a strict
  no-op — the record passes through untouched.
- **repaired-within-2** = `repaired`.
- **attempts-to-green** for a never-green pair is censored at
  `CORRECTIVE_ATTEMPTS + 1 = 3`.
- **McNemar** is exact (binomial on the discordant pairs: `b_only` vs `a_only`),
  reported per headline cell and per first-error class.
- **Wilcoxon** signed-rank on the paired attempts-to-green scores — reported for
  completeness; on a 3-valued censored score it is driven by the same discordant
  pairs McNemar uses and is not independent evidence.
- **rescue**: `won_at_1` / `still_broken` (= n − won_at_1 − no_program) /
  `rescued_at_2`, per arm.
- **second_error**: per arm, of the repairs that FAILED — `failed`,
  `same_class` / `new_class` (attempt-1 error class vs the start), and
  `no_attempt` (never produced a corrective program). `same + new + no_attempt
  == failed`.
- **caret_preservation**: paired. Of the pairs where the treatment arm's
  attempt-1 feedback quoted a source line under its caret AND both arms produced
  an attempt-1 program (`n`) — how often that exact line survived, stripped-equal,
  into each arm's rewrite (`a_preserved` / `b_preserved`), with the discordant
  split (`b_only` / `a_only`) and an exact McNemar `mcnemar_p`. The mechanism
  measurement for issue #26: a precise caret anchors the model to the flagged
  line; a contentless C++ error makes it rewrite.
- **by_arm_a_truncation**: the headline split on whether arm A's attempt-1
  feedback hit the 500-char cap.
- A pair is dropped if either arm is missing, or (with `--drop-transport`) if
  either arm's `terminal_reason` is set.
