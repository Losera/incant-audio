# issue-#26 data schema

Two JSON files, both a flat top-level list of records.

---

## Corpus — `bench/corpora/repair_corpus_20260830.json`

One record per first-attempt generation. The **corpus** is the subset with
`compiles == false`; `compiles == true` records are kept only so the per-config
first-try rate is auditable. The A/B keeps the **first occurrence of each
`code_sha`** among the failing records (deduplication) → 202 distinct programs.

| field | type | meaning |
|---|---|---|
| `source` | str | `"tiered"`, `"benchmark25"`, or `"archive:<name>"` for seeded failures |
| `prompt_id` | str | prompt identity, e.g. `"trivial-01/L1"` |
| `category` | str | prompt category (`trivial`, `distortion`, …) |
| `tier` | str \| null | difficulty tier for tiered prompts (`L1`…`L4`), else null |
| `prompt` | str | the natural-language request sent to the model |
| `config` | str | `"<model>@<temp>"` that generated it, or `"archive"` |
| `model` | str | generating model |
| `temperature` | float \| null | generating temperature |
| `compiles` | bool | did `faust -lang cpp` accept it |
| `code` | str | the generated Faust program |
| `code_sha` | str | `sha256(code)[:16]` — the program's identity key across both files |
| `cpp_stderr` | str | raw `faust` stderr (empty if `compiles`) — **arm A's feedback** |
| `cpp_error_class` | str \| null | `llm/error_classes.classify_error(cpp_stderr)` |
| `frs_codes` | list[str] \| null | faust-rs stable codes (advisory metadata; recomputed live by the A/B) |
| `frs_feedback` | str \| null | `frs_check.render(...)` output at corpus-build time (advisory) |
| `timestamp` | str | ISO-8601 UTC |

---

## A/B result — `bench/results/repair_ab/repair_ab_20260830{,_7b}.json`

One record per **(program, arm)** — so a full 3-arm run over 202 programs is 606
records. `bench/score_repair_ab.py` pairs them by `code_sha` within a
`repair_model`.

| field | type | meaning |
|---|---|---|
| `prompt_id`, `category`, `tier` | | copied from the corpus entry |
| `corpus_config` | str | the `config` that first produced this program |
| `code_sha` | str | **pairing key** — same program, compared across arms |
| `first_error_class` | str | class of the *starting* C++ error |
| `arm` | str | `"A"` (C++ stderr) / `"B"` (faust-rs full) / `"C"` (faust-rs core) |
| `repair_model` | str | the fixed model doing the repair (not the generating model) |
| `repaired` | bool | did it compile within `CORRECTIVE_ATTEMPTS` (2) |
| `attempts_to_green` | int \| null | 1 or 2; null if never |
| `attempts_used` | int | attempts actually run (< 2 only if green early or the generator threw) |
| `second_error_class` | str \| null | class of the error after corrective attempt 1 |
| `second_error_same_as_first` | bool \| null | did attempt 1 re-break it the same way |
| `attempt_log` | list | per-attempt detail, below |
| `timestamp` | str | ISO-8601 UTC |
| `sample_index` | int | *(standalone `--samples>1` only)* which sample this is |
| `run_meta` | obj | *(standalone only)* `{system_prompt_sha, backend}` |

### `attempt_log[i]`

| field | type | meaning |
|---|---|---|
| `n` | int | attempt number (1-based) |
| `feedback_arm` | str | which arm's feedback was shown |
| `feedback_code` | str \| null | faust-rs primary code for this attempt (arms B/C) |
| `feedback_text` | str | the exact error text put in the prompt (truncated to 1200 chars) |
| `code` | str | the model's new program |
| `code_sha` | str | its hash |
| `cpp_ok` | bool | did `faust -lang cpp` accept the new program |
| `cpp_stderr` | str | its stderr (empty if `cpp_ok`) |
| `cpp_error_class` | str \| null | class of that stderr |
| `wall_s` | float | generation wall time |
| `error` | str | *(only if the generator raised)* `"<Type>: <msg>"`; the loop then stops |

### Scoring conventions (`bench/score_repair_ab.py`)

- **repaired-within-2** = `repaired`.
- **attempts-to-green** for a never-green pair is censored at `CORRECTIVE_ATTEMPTS + 1 = 3`.
- **McNemar** is exact (binomial on the discordant pairs: `b_only` vs `a_only`).
- **Wilcoxon** signed-rank on the paired attempts-to-green scores.
- A pair is dropped if either arm is missing for that `code_sha`/`repair_model`.
