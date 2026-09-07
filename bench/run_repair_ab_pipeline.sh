#!/usr/bin/env bash
# bench/run_repair_ab_pipeline.sh — the whole faust-rs repair-loop A/B, end to end.
#
#   stage 1  build the failing-program corpus     (bench/build_repair_corpus.py)
#   stage 2  run the paired A/B                    (bench/run_repair_ab.py)
#   stage 3  score it + chart + verdict            (bench/score_repair_ab.py)
#
# Every stage is --resume-safe and writes incrementally, so this is fine to kill
# and re-run: it picks up where it left off. Detached from any Claude session —
# `nohup bench/run_repair_ab_pipeline.sh &`. All model calls are local ollama, $0.
set -u
cd "$(dirname "$0")/.."

# Roomy per-generation budget. The default 140 s (per-attempt cap ~42 s) is
# fine on a full-clock GPU (~3.5 s/gen) but trips as BudgetExhausted the moment
# the machine drops to battery / power-save and generations stretch to ~30 s.
# The budget is a ceiling, not a target — a (program, arm) repair only ever runs
# ~2 generations — so a large value has no cost and just makes the run robust.
export PLUGINFORGE_GENERATION_BUDGET="${PLUGINFORGE_GENERATION_BUDGET:-900}"

DATE="${REPAIR_AB_DATE:-$(date +%Y%m%d)}"
CORPUS="bench/corpora/repair_corpus_${DATE}.json"
AB="bench/results/repair_ab/repair_ab_${DATE}.json"
SUMMARY="bench/results/repair_ab/repair_ab_${DATE}_summary.json"
REPORT="bench/results/repair_ab/repair_ab_${DATE}_report.txt"

say() { echo "[pipeline $(date -Is)] $*"; }

# ollama's default VRAM estimate on this 4 GB laptop GPU under-commits and runs
# qwen2.5-coder:3b (1.9 GB) almost entirely on CPU (~10 s/gen). Forcing a full
# GPU load once takes it to ~3.5 s/gen; the continuous harness then keeps it
# resident. Harmless if it is already loaded or if there is no GPU.
warm_gpu() {
    curl -s http://localhost:11434/api/generate \
        -d '{"model":"qwen2.5-coder:3b","prompt":"ok","stream":false,"keep_alive":"2h","options":{"num_gpu":99}}' \
        >/dev/null 2>&1 || true
}

warm_gpu
say "stage 1 — corpus build"
python3 bench/build_repair_corpus.py --out "$CORPUS" --resume || {
    say "corpus build exited $? — a partial corpus may still be usable, continuing"; }

DISTINCT=$(python3 -c "
import json,sys
try:
    r=json.load(open('$CORPUS'))
    print(len({x['code_sha'] for x in r if not x['compiles']}))
except Exception:
    print(0)
")
say "corpus has $DISTINCT distinct failing programs"
if [ "$DISTINCT" -lt 20 ]; then
    say "too few (<20) to run a meaningful A/B — stopping"
    exit 1
fi

warm_gpu
say "stage 2 — paired A/B (arm A = C++ stderr, arm B = faust-rs)"
python3 bench/run_repair_ab.py --corpus "$CORPUS" --out "$AB" --resume || {
    say "A/B exited $? — stopping"; exit 1; }

say "stage 3 — score (--screen: the committed reports are the program-screened view)"
python3 bench/score_repair_ab.py "$AB" --screen "$CORPUS" --json-out "$SUMMARY" | tee "$REPORT"

say "DONE — verdict in $REPORT, machine-readable in $SUMMARY"
