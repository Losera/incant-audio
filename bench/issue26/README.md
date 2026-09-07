# Does faust-rs's better error shorten the repair loop? — issue #26

Reproduction package for **[Losera/incant-audio#26](https://github.com/Losera/incant-audio/issues/26)**.

**Licence:** the corpus (`../corpora/repair_corpus_20260830.json` + `SCHEMA.md`)
is CC-BY-4.0; this reproduction harness is MIT and self-contained (no proprietary
file is needed to run any step). See [`LICENSE`](LICENSE). Everything else in the
repository is proprietary.

## Quickstart — verify the published numbers from a cold clone (~1 s)

```bash
git clone https://github.com/Losera/incant-audio
cd incant-audio
git checkout <SHA>                     # the commit linked from issue #26
python3 -m venv .venv && . .venv/bin/activate
pip install -r bench/issue26/requirements.txt   # scipy (pinned < 2); that's all
python3 bench/issue26/verify.py        # exit 0 == every committed number reproduced
```

Python 3.10+ (CI runs 3.12). No compiler, no model, no network. `verify.py`
prints how many checks it ran and refuses to pass if that count doesn't match
`expected.json` (so a stale checkout can't silently verify a subset). Everything
past this — replaying the A/B on your own model, rebuilding the pinned
container — is under **Reproduce**.

---

**Question (Stéphane Letz, GRAME):** faust-rs returns a more precise compile
error than the C++ Faust compiler — does that make the
*generate → compile → correct* loop converge faster? Can the LLM better
understand what the corrected DSP has to be, so fewer retries are needed?

**Answer, so far:** for a small local repair model, **no — it makes the loop
worse.** Feeding faust-rs's diagnostic text back to the model verbatim lowered
*repaired-within-2-attempts* from **74% → 43%** on `qwen2.5-coder:3b` and
**73% → 49%** on `qwen2.5-coder:7b` (Q3), McNemar exact *p* < 1e-8 on every 3B
cell; arm A also needed fewer attempts. Trimming faust-rs to code + message +
caret (arm C) did not change this, so it is not verbosity. **Untested: a
frontier model** — the gap already narrowed from the 3B to the 7B.

---

## The finding in one table

Paired: each program repaired from the **identical** starting point (same
program, same compiler verdict), fixed repair model at temperature 0, up to 2
corrective attempts. The one thing that differs between arms is the error text
the model is shown — but the two texts are **not** produced symmetrically, and
that is deliberate:

- **arm A is the product's real feedback:** `faust -lang cpp` stderr,
  `.strip()[:500]` — byte-for-byte what PluginForge's shipping repair loop sends
  (`bench/repair_ab_core.py`, mirroring `bench/run_benchmark.py`). The 500-char
  cap is part of the product, so it is part of arm A.
- **arms B/C are faust-rs's full rendered `--check` output, uncapped**
  (`bench/frs_check.py`).

| repair model | n | **A** — capped C++ stderr | **B** — faust-rs full | **C** — faust-rs core |
|---|--:|--:|--:|--:|
| `qwen2.5-coder:3b` (Q4) | 192 | **143 (74%)** | 82 (43%) | 80 (42%) |
| `qwen2.5-coder:7b` (Q3_K_S) | 115 | **84 (73%)** | 56 (49%) | 53 (46%) |

*(n is post-**program screen** — see below. Raw: 3B 202 → A 151 (75%) / B 88 /
C 86; 7B 120 → A 87 / B 60 / C 57. Same finding.)*

Every faust-rs arm vs arm A: McNemar exact *p* < 1e-3 (3B: < 1e-8); arm A fewer
attempts-to-green (paired Wilcoxon *p* < 1e-3 — but on a 3-valued censored score
that test is driven by the same discordant pairs McNemar uses, so it is reported
for completeness, not as independent evidence).

### Does the 500-char cap cause the effect? No — it handicaps arm A.

The cap binds on **34 of the 192** screened programs (39 of the raw 202; mostly
`routing_arity`, where it cuts the tail of Faust's box-expression dump).
Stratified (`verify.py` checks both):

| 3B | n | A repaired | B repaired | McNemar p |
|---|--:|--:|--:|--:|
| arm A stderr **never truncated** | 158 | 118 (75%) | 71 (45%) | ≈ 3e-7 |
| arm A stderr **truncated** | 34 | 25 (74%) | 11 (32%) | ≈ 5e-4 |

Same direction, same size, in the stratum where arm A got the *full* error. A
byte-matched A/B (capping faust-rs too, or uncapping arm A) needs a model re-run
and is WP3 in [`METHODOLOGY.md`](METHODOLOGY.md).

### Why (mechanism, from reading the repair trajectories)

faust-rs pins the failure to an exact token and **quotes the offending source
line back** under a caret. A small model shown that line tends to treat it as
fixed and edit *around* it; the terse, unlocalised C++ stderr instead makes the
model **discard and rewrite** — often "simplify / delete the broken
sub-expression" — which compiles more often. Four signatures, all from the
committed 3B run, all checked by `verify.py`:

- **Caret-line preservation.** Of the 161 programs where the faust-rs feedback
  quoted a source line, that exact line survived **verbatim** into attempt 1 in
  **94 / 161 (58%)** of arm-B rewrites — against **7 / 161 (4%)** of arm A's
  (McNemar exact *p* ≈ 2e-22; discordant 91 vs 4). It replicates on the 7B
  (53 / 99 vs 4 / 99, *p* ≈ 2e-12). Arm **C** — faust-rs's caret with *no* prose
  notes — behaves like arm B (91 / 161, 57%), so it is the quoted line, not the
  verbosity, that anchors the model.
- **Same-class recidivism.** Of arm A's 49 failed repairs, 21 (**54%** of the 39
  that got a corrective attempt) ended on the same error class they started on;
  arm B 83 / 106 (**78%**), arm C 81 / 107 (**76%**).
- **Second-attempt rescue.** Arm A leads from the first corrective attempt
  (94 / 192 wins vs arm B 62) and the gap **widens** on the second: of the
  programs still broken after attempt 1, arm A repairs **49 / 88 (56%)** on
  attempt 2, arm B **20 / 126 (16%)**, arm C **14 / 121 (12%)**.
- **Where it lives.** Significant exactly where the caret is most informative —
  `routing_arity` (McNemar *p* ≈ 5e-6), `syntax` (≈ 6e-6), `hallucinated_symbol`
  (≈ 2e-3) — and the one class where faust-rs is nominally *better* is
  `unclassified` (n = 17, *p* = 0.29), where C++ stderr carries no location.

**Caveats on the preservation reading.** Both arms mostly re-emit the whole
program — the preserved line sits inside an otherwise-rewritten patch — so this
is a *tendency*, not a clean patch-vs-rewrite dichotomy. And the two arms'
correction templates differ in wording (arm A appends "…fix it:"; arms B/C end
"Fix this and re-emit the complete program."), an uncontrolled difference that
bears directly on a *preservation* claim; closing it needs a model re-run
(**WP3** in [`METHODOLOGY.md`](METHODOLOGY.md)).

### What this does *not* claim

- **Compilability only.** The harness scores whether the repaired patch
  compiles, not whether it still does what the prompt asked. On the **62
  programs both arm A and arm B repaired** (`bench/fidelity_gate.py`, non-blank
  line ratio `< 0.60`), arm A shrank **17 / 62** and arm B **21 / 62**; a named
  `expected_primitive` was lost by arm A on **10 / 58** and by arm B on
  **6 / 58**. So arm A does **not** buy its extra compiles with fidelity. *(An
  earlier note here claimed the opposite; it compared each arm to its own,
  different, set of wins and reversed the sign. Retracted.)*
- **Not a frontier model.** `qwen2.5-coder:3b`/`7b` local. The shipping product
  runs a much larger model; that regime is untested here.
- **Quantization confound on the 7B:** 7B is Q3_K_S (fits a 4 GB GPU), 3B is Q4.
- **faust-rs's diagnostics are a human win** — on the 15 never-compiled cells,
  faust-rs gave a source location 15/15 (C++ **8**/15) and a stable code 15/15
  (C++ 0/15). *(The C++ figure was posted on the issue as 9/15; re-derived
  2026-08-30 as 8/15 — a stderr-scoring heuristic change plus Faust
  2.85.5→2.85.9, not material.)* This result is about *how to use it in an
  automated loop*, not a defect in faust-rs. The stable code as a *branch
  signal* (one targeted hint per code, rather than pasting the prose) still
  looks promising and is untested.

---

## Reproduce

### 1. Verify the published numbers — no model, no faust-rs, ~1 s

```bash
python3 bench/issue26/verify.py
```

Re-derives, from the committed data: the corpus shape + class mix; that every
stored `cpp_error_class` still matches the classifier; the program screen (10
excluded, 192 pass); every headline cell (counts, exact McNemar, mean attempts,
per-class McNemar, the rescue split, the second-error identity, caret-line
preservation, the stderr-cap strata); and the fidelity
figures, recomputed from the `*_fidelity.json` `cells` dict under the screen.
Diffs against `expected.json`; asserts it ran exactly `checks_expected` checks.
Only needs `scipy` (`requirements.txt`, pinned `< 2`); p-value checks carry a
`1e-8` floor so a minor scipy change can't fail a byte-identical re-run.
`python3 bench/issue26/verify.py --freeze` re-emits `expected.json` (for when
the committed data is deliberately changed).

### 2. Re-run the diagnostic-quality half — needs `faust` + `faust-rs`

```bash
PLUGINFORGE_FAUST_RS_BIN=$(command -v faust-rs) \
  python3 bench/frs_rederive_issue26.py
```

Runs both compilers over the 15 never-compiled cells (vendored in
`frs_rederive_cells.json`) and prints the location / stable-code / remedy
comparison.

### 3. Replay the A/B with your own model

Self-contained driver, stdlib only, any OpenAI-compatible endpoint or a local
ollama:

```bash
# local ollama, the 3B the published run used
#   (macOS: OLLAMA_HOST=0.0.0.0 ollama serve — it binds 127.0.0.1 by default)
# (add --limit 20 for a quick smoke test before the full corpus)
python3 bench/issue26/repair_ab_standalone.py \
  --corpus bench/corpora/repair_corpus_20260830.json \
  --arms A,B,C --backend ollama --model qwen2.5-coder:3b \
  --out run_local.json
python3 bench/score_repair_ab.py run_local.json \
  --screen bench/corpora/repair_corpus_20260830.json

# a hosted model (Groq shown; any OpenAI-compatible endpoint works)
GROQ_API_KEY=... python3 bench/issue26/repair_ab_standalone.py \
  --corpus bench/corpora/repair_corpus_20260830.json \
  --arms A,B --backend openai \
  --endpoint https://api.groq.com/openai/v1 \
  --model llama-3.3-70b-versatile --api-key-env GROQ_API_KEY \
  --samples 5 --out run_hosted.json
```

The full 192-program corpus is roughly 1–3 h on a CPU 3B. Transport failures
(429 / 5xx / timeout / truncated response) are retried with backoff and, if they
persist, recorded with `terminal_reason` and **excluded** from the arm
comparison — never scored as a repair failure. A run that aborts ≥ 25% of its
records exits non-zero and tells you the endpoint is wrong.

**Determinism:** the published runs used n=1 per (program, arm). Whether the
local repair step is byte-stable run-to-run at temp 0 is **not** audited — an
earlier claim that it "is deterministic when warm" is retracted; a proper audit
is WP5 in [`METHODOLOGY.md`](METHODOLOGY.md). A hosted model is definitely not
bit-deterministic at temp 0 — use `--samples K` (K ≥ 5). `score_repair_ab.py`
collapses a K-sample cell by majority-green / upper-median attempts before
scoring (`load_pairs` → `_aggregate_cell`, WP1); K=1 is a strict no-op.

### 4. Everything pinned, in a container

The one hard part is building `faust` (C++) and `faust-rs` (Rust). The image
does both — `faust` from the Arch package **asserted** at 2.85.9, `faust-rs`
pinned to tag 0.8.0 — and the build fails loudly if either drifts. The base
(`archlinux:base-devel`) is a rolling tag and is deliberately **not**
digest-pinned: a pinned old Arch base running `pacman -Syu` against today's
mirrors is the classic partial-upgrade breakage, so the image will eventually
stop building rather than silently produce a different environment. No
proprietary file is in the image.

```bash
make -C bench/issue26 docker-verify        # builds if needed, then verifies
# 20-program smoke run against a host ollama, then scored:
make -C bench/issue26 docker-replay BACKEND=ollama \
  ENDPOINT=http://host.docker.internal:11434 MODEL=qwen2.5-coder:3b ARMS=A,B,C LIMIT=20
```

The image is tagged `incant-issue26:<short-sha>` so `docker-verify` can't run a
stale build by accident.

**Apple Silicon / arm64:** `archlinux` publishes no arm64 image. Build with
`DOCKER_DEFAULT_PLATFORM=linux/amd64 make -C bench/issue26 docker`. It runs
emulated — fine for `verify`, and the Rust build of faust-rs under emulation is
slow (budget an hour, and there is a small chance of a qemu/rustc failure).

`docker run --rm incant-issue26:<sha> {verify | rederive | fidelity FILE |
replay ARGS | score FILE | shell}`.

---

## What's in here

| file | what |
|---|---|
| `verify.py` | re-derive + check every published number; `--freeze` re-emits expectations |
| `expected.json` | the frozen expectations (schema 2; `checks_expected` guards completeness) |
| `requirements.txt` | the one pinned Python dep for `verify.py` (`scipy < 2`) + the optional chart dep |
| `repair_ab_standalone.py` | the A/B driver, no dependency on `llm/providers.py` (step 3) |
| `system_prompt.txt` | **vendored** — the prompt the corpus was built with (snapshot at `c1e9370`) |
| `frs_rederive_cells.json` | **vendored** — the 15 never-compiled cells for step 2 |
| `expected_primitives.json` | **vendored** — effect_id → expected_primitives, for `fidelity_gate.py` |
| `SCHEMA.md` | field-by-field data dictionary for every committed JSON |
| `METHODOLOGY.md` | the limitations list (L1–L10) + the planned follow-up (WP1–WP6) |
| `LICENSE` | MIT (harness) + pointer to the corpus's CC-BY-4.0 |
| `Dockerfile`, `docker-entrypoint.sh` | the pinned repro environment (step 4) |
| `Makefile` | `verify` / `rederive` / `replay` / `test` / `docker*` |

Shared with the in-repo harness (not duplicated), all MIT:

| file | what |
|---|---|
| `../repair_ab_core.py` | the paired corrective loop itself — imported by both `../run_repair_ab.py` and `repair_ab_standalone.py` |
| `../corpus_screen.py` | the mechanical "is this a Faust program?" screen |
| `../frs_check.py` | `faust-rs --check --error-format json` wrapper |
| `../score_repair_ab.py` | McNemar + per-class + rescue + cap strata + chart |
| `../fidelity_gate.py` | the compile-only-vs-fidelity check (shrink + primitives) |
| `../build_repair_corpus.py` | how the corpus was assembled (needs the full repo to run) |
| `../corpora/repair_corpus_20260830.json` | 513 records → **202 distinct C++-rejected programs**, 192 after the screen (CC-BY-4.0) |
| `../corpora/repair_corpus_20260830_excluded.json` | the 10 non-program rows + reasons |
| `../results/repair_ab/repair_ab_20260830{,_7b}.json` | the raw per-(program,arm) trajectories |
| `../results/repair_ab/repair_ab_20260830{,_7b}_fidelity.json` | fidelity sidecars (`summary` + `summary_screened` + per-cell) |
| `../../llm/error_classes.py` | the shared C++-error taxonomy |

## Provenance

- Corpus + result JSONs were produced in PluginForge commit **`c1e9370`**
  (PR #41). The system prompt as used is vendored as `system_prompt.txt`
  (sha256[:16] `a2d909565e3c2fd2`, unchanged in `llm/prompts/` since).
- Compilers: **Faust 2.85.9**, **faust-rs 0.8.0**.
- Repair models: `qwen2.5-coder:3b` (Q4_K_M, ollama) and
  `qwen2.5-coder:7b-instruct-q3_K_S`. The 7B run is a 120-program (115 screened)
  first-error-class-stratified subset.
- Corpus generators: `qwen2.5-coder:{3b@0.0, 3b@0.4, 3b@0.8, 7b@0.0}` + 15
  archived failures. How a program was first generated does not bias the paired
  A/B — both arms repair the identical program.

## Caveats a re-runner should know

Full list, with the planned follow-up for each, is in
[`METHODOLOGY.md`](METHODOLOGY.md). The load-bearing ones:

1. **Corpus selection.** These are *weak-model* first-generation failures. Run
   through a strong model and most repair under either arm → expect the verdict
   to move toward "no measurable difference". The clean frontier test builds a
   fresh corpus from that model's own failures.
2. **The program screen** (`bench/corpus_screen.py`, METHODOLOGY L10) drops 10
   rows that are not Faust programs. Everything published is post-screen;
   `--no-screen` / omitting `--screen` reproduces the raw 202.
3. **Arm A is capped at 500 chars, arms B/C are not** (METHODOLOGY L6). The
   stratified check above shows the cap isn't the cause; a byte-matched re-run
   is WP3.
4. **`--samples K` is aggregated per cell** by `score_repair_ab.load_pairs` →
   `_aggregate_cell` (majority-green, upper-median attempts, WP1). K=1 — every
   committed cell — is a strict no-op. A tie among K samples counts as
   not-repaired (conservative).
5. **faust-rs is advisory only.** `frs_codes` / `frs_feedback` in the corpus are
   recomputed live during the A/B, so a newer faust-rs changes arms B/C. If the
   binary is missing or crashes mid-run, arms B/C silently fall back to arm A's
   text (METHODOLOGY L7) — fired once in the committed run.
6. **The A vs faust-rs prompt wrappers are not byte-matched** — arm A carries
   "…fix it:", arms B/C carry faust-rs's own "The Faust compiler rejected your
   program." + "…re-emit the complete program." Arm C behaving like B rules out
   *verbosity*, but not the wrapper wording — and the "re-emit the complete
   program" instruction bears directly on the caret-line-preservation reading.
   Median feedback length is 97 / 637 / 262 chars (A/B/C). WP3.
7. **n=1 per cell, determinism unaudited** (WP5).
