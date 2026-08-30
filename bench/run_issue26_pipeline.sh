#!/usr/bin/env bash
# bench/run_issue26_pipeline.sh — the whole issue-#26 repair-loop A/B, end to end.
#
#   stage 1  build the failing-program corpus     (bench/build_repair_corpus.py)
#   stage 2  run the paired A/B                    (bench/run_repair_ab.py)
#   stage 3  score it + chart + verdict            (bench/score_repair_ab.py)
#
# Every stage is --resume-safe and writes incrementally, so this is fine to kill
# and re-run: it picks up where it left off. Detached from any Claude session —
# `nohup bench/run_issue26_pipeline.sh &`. All model calls are local ollama, $0.
set -u
cd "$(dirname "$0")/.."

DATE="${ISSUE26_DATE:-$(date +%Y%m%d)}"
CORPUS="bench/corpora/repair_corpus_${DATE}.json"
AB="bench/results/repair_ab/repair_ab_${DATE}.json"
SUMMARY="bench/results/repair_ab/repair_ab_${DATE}_summary.json"
REPORT="bench/results/repair_ab/repair_ab_${DATE}_report.txt"

say() { echo "[pipeline $(date -Is)] $*"; }

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

say "stage 2 — paired A/B (arm A = C++ stderr, arm B = faust-rs)"
python3 bench/run_repair_ab.py --corpus "$CORPUS" --out "$AB" --resume || {
    say "A/B exited $? — stopping"; exit 1; }

say "stage 3 — score"
python3 bench/score_repair_ab.py "$AB" --json-out "$SUMMARY" | tee "$REPORT"

say "DONE — verdict in $REPORT, machine-readable in $SUMMARY"
