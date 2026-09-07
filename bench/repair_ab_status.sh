#!/usr/bin/env bash
# bench/repair_ab_status.sh — one-shot status of the repair-loop A/B run.
# Safe to run anytime; reads files only, starts nothing.
set -u
cd "$(dirname "$0")/.."

DATE="${REPAIR_AB_DATE:-20260830}"
LOG="${REPAIR_AB_LOG:-/tmp/claude-1000/-home-losera-PluginForge/3e50c350-5d82-4f84-948c-badfe3206708/scratchpad/pipeline.log}"
CORPUS="bench/corpora/repair_corpus_${DATE}.json"
AB="bench/results/repair_ab/repair_ab_${DATE}.json"
REPORT="bench/results/repair_ab/repair_ab_${DATE}_report.txt"

echo "── process ──"
if pgrep -af run_repair_ab_pipeline.sh; then :; else echo "pipeline NOT running"; fi
echo
echo "── last 8 log lines ($LOG) ──"
tail -8 "$LOG" 2>/dev/null || echo "no log yet"
echo
echo "── counts ──"
python3 - "$CORPUS" "$AB" <<'PY'
import json, os, sys
from collections import Counter
corpus, ab = sys.argv[1], sys.argv[2]
if os.path.exists(corpus):
    r = json.load(open(corpus))
    f = [x for x in r if not x["compiles"]]
    d = len({x["code_sha"] for x in f})
    print(f"corpus : {len(r):4d} records | {len(f):3d} failing | {d:3d} distinct")
    print(f"         classes: {dict(Counter(x['cpp_error_class'] for x in f))}")
else:
    print("corpus : not started")
if os.path.exists(ab):
    r = json.load(open(ab))
    by = Counter(x["arm"] for x in r)
    pairs = len({x["code_sha"] for x in r if x["arm"] == "A"} &
                {x["code_sha"] for x in r if x["arm"] == "B"})
    ag = sum(1 for x in r if x["arm"] == "A" and x["repaired"])
    bg = sum(1 for x in r if x["arm"] == "B" and x["repaired"])
    print(f"A/B    : {len(r):4d} records | arm A {by['A']}, arm B {by['B']} | "
          f"{pairs} complete pairs | repaired A {ag} / B {bg}")
else:
    print("A/B    : not started")
PY
echo
if [ -f "$REPORT" ]; then
    echo "── FINAL REPORT ($REPORT) ──"
    cat "$REPORT"
else
    echo "final report not written yet → $REPORT"
fi
