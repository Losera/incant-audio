# Does faust-rs's better error shorten the repair loop? — issue #26

Reproduction package for **[Losera/incant-audio#26](https://github.com/Losera/incant-audio/issues/26)**.

**Licence:** the corpus (`../corpora/repair_corpus_20260830.json` + `SCHEMA.md`)
is CC-BY-4.0; this reproduction harness is MIT. See [`LICENSE`](LICENSE). The
rest of the repository is proprietary.

## Quickstart — verify the published numbers from a cold clone (~1 s)

```bash
git clone https://github.com/Losera/incant-audio
cd incant-audio
python3 -m pip install scipy          # the only dependency verify.py needs
python3 bench/issue26/verify.py        # exit 0 == every committed number reproduced
```

Python 3.10+ (CI runs 3.12). No compiler, no model, no network. Everything past
this point — replaying the A/B on your own model, rebuilding the pinned
container — is in **Reproduce** below.

---

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
**at that spot** and re-breaks it the same way; terse, unlocalised C++ stderr
provokes a **broader rewrite** — often "simplify / delete the broken
sub-expression" — that compiles more often. Three signatures of that, all from
the committed 3B run (`score_repair_ab.py` prints the first two):

- **Second-attempt rescue.** Of the programs still broken after attempt 1, arm A
  fixes **50 / 101 (50%)** on attempt 2; arm B **20 / 134 (15%)**, arm C
  **15 / 131 (11%)**. The arms are close on the first shot (101 / 68 / 71 wins);
  the gap is almost entirely arm B/C *failing to recover*.
- **Same-class recidivism.** Of arm A's **51** failed repairs, **21 (41%)**
  ended on the same error class they started on; arm B **85 / 114 (75%)**, arm C
  **84 / 116 (72%)**.
- **Where it lives.** The effect is significant exactly where the caret is most
  informative — `routing_arity` (McNemar *p* ≈ 3e-6), `syntax` (≈ 8e-6),
  `hallucinated_symbol` (≈ 2e-3) — and the one class where faust-rs is *nominally
  better* is `unclassified` (n = 17, *p* = 0.29), where C++ stderr carries no
  location at all.

### What this does *not* claim

- **Compilability only.** The harness scores whether the repaired patch
  compiles, not whether it still does what the prompt asked. On the **67
  programs both arm A and arm B repaired** (`bench/fidelity_gate.py`, non-blank
  line ratio `< 0.60`), arm A shrank **19 / 67 (28%)** and arm B **24 / 67
  (36%)**; a named `expected_primitive` was lost by arm A on **10 / 63** and by
  arm B on **6 / 63**. So arm A does **not** buy its extra compiles with
  fidelity — an earlier note here claiming "~24% of arm A's wins shrink … arm A
  partly buys the compile with fidelity" compared each arm to its own (different)
  set of wins, reversed the sign, and is retracted.
- **Not a frontier model.** `qwen2.5-coder:3b`/`7b` local. The shipping product
  runs a much larger model; that regime is untested here.
- **Quantization confound on the 7B:** 7B is Q3_K_S (fits a 4 GB GPU), 3B is Q4.
- **faust-rs's diagnostics are a human win** — on the 15 never-compiled cells,
  faust-rs gave a source location 15/15 (C++ **8**/15) and a stable code 15/15
  (C++ 0/15). *(The C++ figure was posted on the issue as 9/15; re-derived
  2026-08-30 as 8/15 — a stderr-scoring heuristic change plus Faust 2.85.5→2.85.9,
  not material.)* This result is about *how to use it in an automated loop*, not
  a defect in faust-rs. The stable code as a *branch signal* (pick one targeted
  hint per code, rather than pasting the prose) still looks promising and is
  untested.

---

## Reproduce

### 1. Verify the published numbers — no model, no faust-rs, ~1 s

```bash
python3 bench/issue26/verify.py
```

Re-derives the corpus shape and every headline A/B cell from the committed data
in `bench/corpora/` and `bench/results/repair_ab/`, and diffs against
`expected.json`. The fidelity numbers (the "not just compiles" block above) are
checked against the committed `*_fidelity.json` sidecars — a checksum of those,
not a re-derivation, since `fidelity_gate.py` pulls in the wider product harness.
Only needs `scipy`.

### 2. Re-run the diagnostic-quality half — needs `faust` + `faust-rs`

```bash
PLUGINFORGE_FAUST_RS_BIN=$(command -v faust-rs) \
  python3 bench/frs_rederive_issue26.py
```

Runs both compilers over the 15 archived never-compiled cells and prints the
location / stable-code / remedy comparison.

### 3. Replay the A/B with your own model

Self-contained driver, stdlib only, any OpenAI-compatible endpoint or a local
ollama:

```bash
# local ollama, the 3B the published run used
# (add --limit 20 for a quick smoke test before the full corpus)
python3 bench/issue26/repair_ab_standalone.py \
  --corpus bench/corpora/repair_corpus_20260830.json \
  --arms A,B,C --backend ollama --model qwen2.5-coder:3b \
  --out run_local.json
python3 bench/score_repair_ab.py run_local.json

# a hosted model (Groq shown; any OpenAI-compatible endpoint works)
GROQ_API_KEY=... python3 bench/issue26/repair_ab_standalone.py \
  --corpus bench/corpora/repair_corpus_20260830.json \
  --arms A,B --backend openai \
  --endpoint https://api.groq.com/openai/v1 \
  --model llama-3.3-70b-versatile --api-key-env GROQ_API_KEY \
  --samples 5 --out run_hosted.json
```

The full 202-program corpus is roughly 1–3 h on a CPU 3B, depending on the box.

**Determinism:** the published runs used n=1 per (program, arm). Whether the
local repair step is byte-stable run-to-run at temp 0 is **not** audited — an
earlier claim that it "is deterministic when warm" had no supporting artifact
and is retracted; `docs/BUGS.md` records ~20% run-to-run output flips for ollama
at temp 0 on a related measurement, and a proper audit is pre-registered as WP5
in [`METHODOLOGY.md`](METHODOLOGY.md). A hosted model is definitely **not** bit
deterministic at temp 0 — use `--samples K` (K ≥ 5); `score_repair_ab.py` treats
samples as independent records, so aggregate per cell (majority-green,
median-attempts) before reading the verdict.

### 4. Everything pinned, in a container

The one hard part is building `faust` (C++) and `faust-rs` (Rust). The image
does both — `faust` from the Arch package **asserted** at 2.85.9, `faust-rs`
pinned to tag 0.8.0 — and the build fails loudly if either drifts. The base
(`archlinux:base-devel`) is a rolling tag and is deliberately **not**
digest-pinned: a pinned old Arch base running `pacman -Syu` against today's
mirrors is the classic partial-upgrade breakage, so the image will eventually
stop building rather than silently produce a different environment. The LLM
stays outside the image.

```bash
make -C bench/issue26 docker
make -C bench/issue26 docker-verify
# 20-program smoke run against a host ollama, then scored:
make -C bench/issue26 docker-replay BACKEND=ollama \
  ENDPOINT=http://host.docker.internal:11434 MODEL=qwen2.5-coder:3b ARMS=A,B,C LIMIT=20
# drop LIMIT for the full 202-program corpus (1–3 h on a CPU 3B)
```

**Apple Silicon / arm64:** `archlinux` publishes no arm64 image. Build with
`DOCKER_DEFAULT_PLATFORM=linux/amd64 make -C bench/issue26 docker` (or add
`--platform linux/amd64` to a raw `docker build`). It runs emulated — fine for
`verify`, slow for a full replay.

`docker-replay` runs the A/B and then `score_repair_ab.py` on the result
(written to `bench/issue26/out/`). See `Makefile` for all knobs.

---

## What's in here

| file | what |
|---|---|
| `verify.py` | re-derive the published numbers from committed data (step 1) |
| `expected.json` | frozen expectations `verify.py` checks against |
| `repair_ab_standalone.py` | the A/B driver, decoupled from `llm/providers.py` (step 3) |
| `SCHEMA.md` | field-by-field data dictionary for the JSON files |
| `METHODOLOGY.md` | the known limitations + the planned follow-up work (WP1–WP5) |
| `LICENSE` | MIT (harness) + pointer to the corpus's CC-BY-4.0 |
| `Dockerfile`, `docker-entrypoint.sh` | the pinned repro environment (step 4) |
| `Makefile` | `verify` / `rederive` / `replay` / `test` / `docker*` |

Shared with the in-repo harness (not duplicated):

| file | what |
|---|---|
| `../repair_ab_core.py` | the paired corrective loop itself — imported by both `../run_repair_ab.py` and `repair_ab_standalone.py` |
| `../frs_check.py` | `faust-rs --check --error-format json` wrapper: `check()` / `render()` / `render_minimal()` |
| `../score_repair_ab.py` | McNemar + Wilcoxon + per-class breakdown + chart |
| `../fidelity_gate.py` | the compile-only-vs-fidelity check (shrink + primitives tiers) |
| `../build_repair_corpus.py` | how the corpus was assembled (needs the full repo) |
| `../corpora/repair_corpus_20260830.json` | 513 records → **202 distinct C++-rejected programs** (CC-BY-4.0) |
| `../results/repair_ab/repair_ab_20260830{,_7b}.json` | the raw per-(program,arm) trajectories |
| `../results/repair_ab/repair_ab_20260830{,_7b}_fidelity.json` | the fidelity sidecars (`summary` + per-cell) |
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

Full list, with the planned follow-up for each, is in
[`METHODOLOGY.md`](METHODOLOGY.md). The load-bearing ones:

1. **Corpus selection.** These are *weak-model* first-generation failures. Run
   through a strong model, most repair under either arm → expect the verdict to
   move toward "no measurable difference" rather than "faust-rs wins". The clean
   frontier test builds a fresh corpus from that model's own failures.
2. **`--samples K` keeps only the last of the K.** `score_repair_ab.py`'s
   `load_pairs` (`by_sha[sha][arm] = r`) overwrites, so for `K > 1` it silently
   drops K−1 samples per cell and lowers *n* without warning. Harmless for the
   committed data (0 duplicate `(model, code_sha, arm)` triples); real for any
   hosted-model run. Fix is WP1 in `METHODOLOGY.md`; until then, aggregate the
   K samples yourself before scoring.
3. **faust-rs is advisory only.** `frs_codes` / `frs_feedback` in the corpus are
   recomputed live during the A/B (`repair_ab_core.feedback_for`), so a newer
   faust-rs changes arms B/C without rebuilding the corpus.
4. **The arm A vs faust-rs prompt wrappers are not byte-matched.** Arm A wraps
   the error with *"Your previous output had this compiler error — fix it:"*
   (`repair_ab_core.ARM_A_TEMPLATE`); arms B/C carry faust-rs's own framing —
   a *"The Faust compiler rejected your program."* header **and** a closing
   *"Fix this and re-emit the complete program."* directive
   (`frs_check.render`). So the A-vs-B contrast mixes the error payload, the
   framing, and a "re-emit the complete program" instruction present only on
   B/C. Median feedback length is 97 / 637 / 262 chars for A / B / C (none
   truncated) — but arm C, at 262 chars, still behaves like B, which is evidence
   *against* verbosity being the driver. A matched-wrapper re-run (A2/B2/C2,
   payload the only difference) is WP3 in `METHODOLOGY.md`.
5. **n=1 per cell, determinism unaudited** (WP5). See the Determinism note in
   step 3 above.
