# Faust repair corpus — `repair_corpus_20260830.json`

A dataset of **real, LLM-generated Faust programs that the C++ Faust compiler
rejects**, each paired with both compilers' diagnostics and a machine
classification of the failure. Built for the issue-#26 repair-loop A/B
([Losera/incant-audio#26](https://github.com/Losera/incant-audio/issues/26)); it
is also usable on its own as a faust-rs regression / diagnostic-quality corpus.

---

## Contents

| | |
|---|---|
| File | `repair_corpus_20260830.json` — flat JSON list, 513 records |
| Records where `compiles == false` | **207** (kept for auditability: the `true` records let you recompute each config's first-try rate) |
| **Distinct failing programs** (first occurrence of each `code_sha`) | **202** — this is "the corpus" |
| Per record | the NL prompt, the generated Faust source, `faust -lang cpp` stderr, `faust-rs --check` codes + rendered feedback, and an error-class label |

### Failure classes (over the 202 distinct programs)

`cpp_error_class` is `llm/error_classes.classify_error(cpp_stderr)` — a coarse
taxonomy of the C++ stderr:

| class | n | what it is |
|---|--:|---|
| `routing_arity` | 81 | split/merge (`<:` `:>`) or sequential (`:`) channel-count mismatch |
| `syntax` | 61 | parse errors — often a construct the prompt recommends but never shows (`with{}`, unbalanced parens, unexpected token) |
| `hallucinated_symbol` | 25 | invented or mis-namespaced stdlib function |
| `unclassified` | 17 | rejected, but the stderr did not match a known pattern |
| `duplicate_symbol` | 15 | the same definition emitted twice |
| `delay_range` | 2 | `@` / `delay` with a non-constant or unbounded amount |
| `recursion_cycle` | 1 | `~` feedback with no delay in the loop |

All 202 have at least one faust-rs stable code (`frs_codes`).

---

## Schema

Field-by-field data dictionary: **[`../issue26/SCHEMA.md`](../issue26/SCHEMA.md)**
(§ *Corpus*). One example failing record:

```json
{
  "source": "archive:efficacy_ollama_20260828",
  "prompt_id": "trivial-01/L1",
  "category": "trivial",
  "tier": "L1",
  "prompt": "let me push a sound forward or fade it back until it almost disappears",
  "config": "archive",
  "model": "qwen2.5-coder:7b",
  "temperature": null,
  "compiles": false,
  "code": "import(\"stdfaust.lib\");\n…",
  "code_sha": "a09e07c0bc9f5cc2",
  "cpp_stderr": "ERROR : split composition A<:B | The number of outputs [2] of A must be a divisor of the number of inputs [3] of B | …",
  "cpp_error_class": "routing_arity",
  "frs_codes": ["FRS-PROP-0002"],
  "frs_feedback": "…rendered faust-rs --check output…",
  "timestamp": "2026-08-…"
}
```

`code_sha` is `sha256(code)[:16]` and is the identity key that joins the corpus
to the A/B result files in `../results/repair_ab/`.

---

## Provenance

| | |
|---|---|
| Built by | `bench/build_repair_corpus.py` at PluginForge commit **`c1e9370`** (PR #41) |
| Date | 2026-08-30 |
| Faust | **2.85.9** (`faust -lang cpp`, the accept/reject oracle and `cpp_stderr`) |
| faust-rs | **0.8.0** (`faust-rs --check --error-format json`, `frs_codes` / `frs_feedback`) |
| System prompt | `git show c1e9370:llm/prompts/system_prompt.txt` — one prompt, no variation, matching `bench/run_efficacy_study.py`'s confound controls |
| Generating configs | `qwen2.5-coder:3b` @ temp {0.0, 0.4, 0.8}, `qwen2.5-coder:7b` @ 0.0, plus 15 seed failures from an archived efficacy run (`config == "archive"`) |

**How a program was generated does not bias the repair A/B** — both arms repair
the byte-identical program from the byte-identical compiler verdict; the only
variable is which error text the repair model is shown. The multi-config
generation exists only to collect *enough distinct failures* (a single grid pass
fails too rarely to have statistical power).

`frs_codes` / `frs_feedback` in the corpus are **advisory metadata** — the A/B
recomputes faust-rs feedback live (`bench/repair_ab_core.feedback_for`), so a
newer faust-rs changes arms B/C without the corpus being rebuilt.

---

## Using it

- **Reproduce the issue-#26 result:** see [`../issue26/README.md`](../issue26/README.md).
- **faust-rs diagnostic-quality check:** `bench/frs_rederive_issue26.py` runs both
  compilers over the 15 never-compiled cells and prints the
  location / stable-code / remedy comparison.
- **Your own analysis:** the file is a plain JSON list; every field is documented
  in `SCHEMA.md`. No PluginForge code is required to read it.

---

## Licence and attribution

`repair_corpus_20260830.json` — the dataset file — is released under
**[Creative Commons Attribution 4.0 International (CC-BY-4.0)](https://creativecommons.org/licenses/by/4.0/)**.
You may share and adapt it, including commercially, provided you give
attribution.

This licence covers **only** `repair_corpus_20260830.json` and the field
documentation in `../issue26/SCHEMA.md`. The rest of the PluginForge repository
remains proprietary (`LICENSE` — "All rights reserved"); the harness scripts
referenced here are provided for reproduction of issue #26, not relicensed.

The dataset embeds LLM-generated Faust source and natural-language prompts
authored for this project; it contains no third-party copyrighted material.

Cite as: *"Faust repair corpus, PluginForge / Incant Audio,
`repair_corpus_20260830.json`, commit `c1e9370`. CC-BY-4.0."*
