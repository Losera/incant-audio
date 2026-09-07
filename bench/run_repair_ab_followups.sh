#!/usr/bin/env bash
# bench/run_repair_ab_followups.sh — the two follow-ups to the 3B repair A/B:
#
#   1. arm C on the 3B  — faust-rs trimmed to code+message+caret (no notes/help).
#      Tests whether it was faust-rs's CONTENT or its human-programmer VERBOSITY
#      that hurt the 3B in arm B. Appends to the existing 3B result file.
#
#   2. arms A/B/C on qwen2.5-coder:7b at Q3_K_S (~3.5 GB — the Q4 7B is 4.7 GB
#      and does NOT fit this 4 GB laptop GPU; Q3_K_S does, ~4x faster) over a
#      class-proportional 120-program sample — does a bigger model use the
#      richer feedback? ~1.5 h. --resume-safe.
#
# Then re-score everything. Detached, $0, kill-and-rerun safe.
# The 7B-Q3 caveat (quantization confound) goes in the issue-#26 reply.
set -u
cd "$(dirname "$0")/.."

export PLUGINFORGE_GENERATION_BUDGET="${PLUGINFORGE_GENERATION_BUDGET:-900}"
DATE="${REPAIR_AB_DATE:-20260830}"
CORPUS="bench/corpora/repair_corpus_${DATE}.json"
AB3B="bench/results/repair_ab/repair_ab_${DATE}.json"
AB7B="bench/results/repair_ab/repair_ab_${DATE}_7b.json"

say() { echo "[followups $(date -Is)] $*"; }
warm() {  # force a full GPU load for whatever model is about to run
    curl -s http://localhost:11434/api/generate \
        -d "{\"model\":\"$1\",\"prompt\":\"ok\",\"stream\":false,\"keep_alive\":\"3h\",\"options\":{\"num_gpu\":99}}" \
        >/dev/null 2>&1 || true
}

say "1/3 — arm C on qwen2.5-coder:3b (all 202)"
warm qwen2.5-coder:3b
python3 bench/run_repair_ab.py --corpus "$CORPUS" --out "$AB3B" --resume \
    --arms C --repair-model qwen2.5-coder:3b || say "arm C run exited $?, continuing"

say "2/3 — arms A,B,C on qwen2.5-coder:7b-instruct-q3_K_S (120-program class-proportional sample)"
warm qwen2.5-coder:7b-instruct-q3_K_S
python3 bench/run_repair_ab.py --corpus "$CORPUS" --out "$AB7B" --resume \
    --arms A,B,C --repair-model qwen2.5-coder:7b-instruct-q3_K_S --sample 120 \
    || { say "7B run exited $? — stopping"; exit 1; }

say "3/3 — score (--screen: the committed reports are the program-screened view)"
python3 bench/score_repair_ab.py "$AB3B" --screen "$CORPUS" \
    --json-out "bench/results/repair_ab/repair_ab_${DATE}_summary.json" \
    | tee "bench/results/repair_ab/repair_ab_${DATE}_report.txt"
python3 bench/score_repair_ab.py "$AB7B" --screen "$CORPUS" \
    --json-out "bench/results/repair_ab/repair_ab_${DATE}_7b_summary.json" \
    | tee "bench/results/repair_ab/repair_ab_${DATE}_7b_report.txt"

say "DONE — reports in bench/results/repair_ab/"
