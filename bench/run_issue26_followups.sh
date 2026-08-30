#!/usr/bin/env bash
# bench/run_issue26_followups.sh — the two follow-ups to the 3B repair A/B:
#
#   1. arm C on the 3B  — faust-rs trimmed to code+message+caret (no notes/help).
#      Tests whether it was faust-rs's CONTENT or its human-programmer VERBOSITY
#      that hurt the 3B in arm B. Appends to the existing 3B result file.
#
#   2. arms A/B/C on qwen2.5-coder:7b over a class-proportional 120-program
#      sample of the same corpus — does a bigger model use the richer feedback?
#      The 7B does not fit this 4 GB GPU (~26 s/gen), so ~4-5 h. --resume-safe.
#
# Then re-score everything. Detached, $0, kill-and-rerun safe.
set -u
cd "$(dirname "$0")/.."

export PLUGINFORGE_GENERATION_BUDGET="${PLUGINFORGE_GENERATION_BUDGET:-900}"
DATE="${ISSUE26_DATE:-20260830}"
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

say "2/3 — arms A,B,C on qwen2.5-coder:7b (120-program class-proportional sample)"
warm qwen2.5-coder:7b
python3 bench/run_repair_ab.py --corpus "$CORPUS" --out "$AB7B" --resume \
    --arms A,B,C --repair-model qwen2.5-coder:7b --sample 120 \
    || { say "7B run exited $? — stopping"; exit 1; }

say "3/3 — score"
python3 bench/score_repair_ab.py "$AB3B" \
    --json-out "bench/results/repair_ab/repair_ab_${DATE}_summary.json" \
    | tee "bench/results/repair_ab/repair_ab_${DATE}_report.txt"
python3 bench/score_repair_ab.py "$AB7B" \
    --json-out "bench/results/repair_ab/repair_ab_${DATE}_7b_summary.json" \
    | tee "bench/results/repair_ab/repair_ab_${DATE}_7b_report.txt"

say "DONE — reports in bench/results/repair_ab/"
