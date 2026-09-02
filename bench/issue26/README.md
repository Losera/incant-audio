# Does faust-rs's better error shorten the repair loop? — issue #26

Reproduction package for **[Losera/incant-audio#26](https://github.com/Losera/incant-audio/issues/26)**.

**Question (Stéphane Letz, GRAME):** faust-rs returns a more precise compile
error than the C++ Faust compiler — does that make the
*generate → compile → correct* loop converge faster? Can the LLM better
understand what the corrected DSP has to be, so fewer retries are needed?

**Answer, so far:** for a small local repair model, **no — it makes the loop
worse.** Feeding faust-rs's diagnostic text back to the model verbatim lowered
*repaired-within-2-attempts* from **75% → 44%** on `qwen2.5-coder:3b` and
**72% → 50%** on `qwen2.5-coder:7b` (Q3), McNemar *p* < 1e-3 in every cell; arm A
also needed fewer attempts. Trimming faust-rs to code + message + caret (arm C)
did not change this, so it is not verbosity. **Untested: a frontier model** — the
gap already narrowed from the 3B to the 7B.

---

## The finding in one table

Paired: each program repaired from the **identical** starting point (same
program, same compiler verdict), fixed repair model at temperature 0, up to 2
corrective attempts. **Only the error text the model sees differs.**

| repair model | n | **A** — C++ stderr | **B** — faust-rs full | **C** — faust-rs core |
|---|--:|--:|--:|--:|
| `qwen2.5-coder:3b` (Q4) | 202 | **151 (75%)** | 88 (44%) | 86 (43%) |
| `qwen2.5-coder:7b` (Q3_K_S) | 120 | **87 (72%)** | 60 (50%) | 57 (48%) |

Every faust-rs arm vs arm A: McNemar exact *p* < 1e-3; arm A fewer
attempts-to-green (Wilcoxon *p* < 1e-3).

### Why (mechanism, from reading the repair trajectories)

faust-rs pins the failure to an exact token with a caret. The small model edits
**at that spot** and re-breaks it the same way — of its *failed* repairs,
~75–80% hit the same error class again (arm A: ~40–55%). Terse, unlocalised C++
stderr provokes a **broader rewrite** — often "simplify / delete the broken
sub-expression" — that compiles more often.

### What this does *not* claim

- **Compilability only.** The harness scores whether the repaired patch
  compiles, not whether it still does what the prompt asked. Recomputed paired
  from the committed data (`bench/fidelity_gate.py`, non-blank line ratio
  `< 0.60`): **both** arms shrink a sizeable share of their wins — 3b: arm A
  35/151 (23%), arm B 35/88 (40%), arm C 27/86 (31%). Shrinkage is a
  small-model-repair property, **not** an arm-A tell; an earlier note here
  saying "~24% of arm A's wins shrink … arm A partly buys the compile with
  fidelity" reversed the comparison and is retracted. Arm A does lose a named
  `expected_primitive` slightly more often (14% of determinable wins vs
  9% / 6%), consistent with the broader-rewrite mechanism above.
- **Not a frontier model.** `qwen2.5-coder:3b`/`7b` local. The shipping product
  runs a much larger model; that regime is untested here.
- **Quantization confound on the 7B:** 7B is Q3_K_S (fits a 4 GB GPU), 3B is Q4.
- **faust-rs's diagnostics are a human win** — on the 15 never-compiled cells,
  faust-rs gave a source location 15/15 (C++ 8/15) and a stable code 15/15
  (C++ 0/15). This result is about *how to use it in an automated loop*, not a
  defect in faust-rs. The stable code as a *branch signal* (pick one targeted
  hint per code, rather than pasting the prose) still looks promising and is
  untested.

---

## Reproduce

### 1. Verify the published numbers — no model, no faust-rs, ~1 s

```bash
python bench/issue26/verify.py
```

Re-derives the corpus shape and every headline cell from the committed data in
`bench/corpora/` and `bench/results/repair_ab/`, and diffs against
`expected.json`. Only needs `scipy`.

### 2. Re-run the diagnostic-quality half — needs `faust` + `faust-rs`

```bash
PLUGINFORGE_FAUST_RS_BIN=$(command -v faust-rs) \
  python bench/frs_rederive_issue26.py
```

Runs both compilers over the 15 archived never-compiled cells and prints the
location / stable-code / remedy comparison.

### 3. Replay the A/B with your own model

Self-contained driver, stdlib only, any OpenAI-compatible endpoint or a local
ollama:

```bash
# local ollama, the 3B the published run used
# (add --limit 20 for a quick smoke test before the full ~1h corpus)
python bench/issue26/repair_ab_standalone.py \
  --corpus bench/corpora/repair_corpus_20260830.json \
  --arms A,B,C --backend ollama --model qwen2.5-coder:3b \
  --out run_local.json
python bench/score_repair_ab.py run_local.json

# a hosted model (Groq shown; any OpenAI-compatible endpoint works)
GROQ_API_KEY=... python bench/issue26/repair_ab_standalone.py \
  --corpus bench/corpora/repair_corpus_20260830.json \
  --arms A,B --backend openai \
  --endpoint https://api.groq.com/openai/v1 \
  --model llama-3.3-70b-versatile --api-key-env GROQ_API_KEY \
  --samples 5 --out run_hosted.json
```

**Determinism:** the published runs used n=1 per (program, arm). Whether the
local repair step is byte-stable run-to-run at temp 0 is **not** audited — an
earlier claim that it "is deterministic when warm" had no supporting artifact
and is retracted; `docs/BUGS.md` records ~20% run-to-run output flips for ollama
at temp 0 on a related measurement, and a proper audit is pre-registered as WP5
of the issue-#26 methodology plan. A hosted model is definitely **not** bit
deterministic at temp 0 — use `--samples K` (K ≥ 5); `score_repair_ab.py` treats
samples as independent records, so aggregate per cell (majority-green,
median-attempts) before reading the verdict.

### 4. Everything pinned, in a container

The one hard part is building `faust` (C++) and `faust-rs` (Rust). The image
does both, pinned; the LLM stays outside it.

```bash
make -C bench/issue26 docker
make -C bench/issue26 docker-verify
# 20-program smoke run against a host ollama, then scored:
make -C bench/issue26 docker-replay BACKEND=ollama \
  ENDPOINT=http://host.docker.internal:11434 MODEL=qwen2.5-coder:3b ARMS=A,B,C LIMIT=20
# drop LIMIT for the full 202-program corpus (a few hours on a CPU 3B)
```

`docker-replay` runs the A/B and then `score_repair_ab.py` on the result
(written to `bench/issue26/out/`). See `Makefile` for all knobs.

---

## What's in here

| file | what |
|---|---|
| `verify.py` | re-derive the published numbers from committed data (step 1) |
| `expected.json` | frozen expectations `verify.py` checks against |
| `repair_ab_standalone.py` | the A/B driver, decoupled from `llm/providers.py` (step 3) |
| `SCHEMA.md` | field-by-field data dictionary for both JSON files |
| `Dockerfile`, `docker-entrypoint.sh` | the pinned repro environment (step 4) |
| `Makefile` | `verify` / `rederive` / `replay` / `test` / `docker*` |

Shared with the in-repo harness (not duplicated):

| file | what |
|---|---|
| `../repair_ab_core.py` | the paired corrective loop itself — imported by both `../run_repair_ab.py` and `repair_ab_standalone.py` |
| `../frs_check.py` | `faust-rs --check --error-format json` wrapper: `check()` / `render()` / `render_minimal()` |
| `../score_repair_ab.py` | McNemar + Wilcoxon + per-class breakdown + chart |
| `../build_repair_corpus.py` | how the corpus was assembled (needs the full repo) |
| `../corpora/repair_corpus_20260830.json` | 513 records → **202 distinct C++-rejected programs** |
| `../results/repair_ab/repair_ab_20260830{,_7b}.json` | the raw per-(program,arm) trajectories |
| `../../llm/error_classes.py` | the shared C++-error taxonomy |
| `../../llm/prompts/system_prompt.txt` | the system prompt (see provenance below) |

## Provenance

- Corpus + result JSONs were produced in PluginForge commit **`c1e9370`**
  (PR #41). The system prompt as used is
  `git show c1e9370:llm/prompts/system_prompt.txt`.
- Compilers: **Faust 2.85.9**, **faust-rs 0.8.0**.
- Repair models: `qwen2.5-coder:3b` (Q4_K_M, ollama) and
  `qwen2.5-coder:7b-instruct-q3_K_S`.
- Corpus generators: `qwen2.5-coder:{3b@0.0, 3b@0.4, 3b@0.8, 7b@0.0}` + 15
  archived failures. How a program was first generated does not bias the paired
  A/B — both arms repair the identical program.

## Caveats a re-runner should know

1. **Corpus selection.** These are *weak-model* first-generation failures. Run
   through a strong model, most repair under either arm → expect the verdict to
   move toward "no measurable difference" rather than "faust-rs wins". The clean
   frontier test builds a fresh corpus from that model's own failures.
2. **`--samples` aggregation is on you.** `score_repair_ab.py` does not dedupe
   samples.
3. **faust-rs is advisory only.** `frs_codes` / `frs_feedback` in the corpus are
   recomputed live during the A/B (`repair_ab_core.feedback_for`), so a newer
   faust-rs changes arms B/C without rebuilding the corpus.
4. **The arm A vs faust-rs prompt wrappers are not byte-matched.** Arm A wraps
   the error with *"Your previous output had this compiler error — fix it:"*
   (`repair_ab_core.ARM_A_TEMPLATE`); arms B/C carry faust-rs's own framing —
   a *"The Faust compiler rejected your program."* header **and** a closing
   *"Fix this and re-emit the complete program."* directive
   (`frs_check.render`). So the A-vs-B contrast mixes three things: the error
   payload, the framing, and the presence of an explicit "re-emit the complete
   program" instruction — the last of which is itself a plausible cause of the
   "arm A provokes a broader rewrite" mechanism. A matched-wrapper re-run
   (arms A2/B2/C2, identical wrapper, payload the only difference) is the next
   step and is WP3 of the issue-#26 methodology plan.
5. **n=1 per cell, determinism unaudited.** See the Determinism note above.
