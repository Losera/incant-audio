#!/usr/bin/env bash
# bench/repair_ab_repro/docker-entrypoint.sh — dispatch for the repair-ab repro image.
#
#   verify              re-derive the published numbers from committed data
#                       (no model, no faust-rs, ~1s). This is the default.
#   rederive            re-run the diagnostic-quality half (faust vs faust-rs on
#                       the 15 never-compiled cells)
#   fidelity <file>     re-run the compile-vs-fidelity gate on a result JSON
#   replay [args...]    run the paired A/B with your own model — every arg after
#                       `replay` is passed straight to repair_ab_standalone.py
#                       (--backend / --endpoint / --model / --arms / --samples /
#                        --out / ...). --corpus defaults to the bundled one.
#                       macOS: start ollama with OLLAMA_HOST=0.0.0.0 or the
#                       container can't reach it.
#   score <file>        run score_repair_ab.py --screen on a result file
#   shell               drop into bash
set -euo pipefail
ROOT="${PLUGINFORGE_ROOT:-/work}"
cd "$ROOT"
cmd="${1:-verify}"; shift || true

CORPUS=bench/corpora/repair_corpus_20260830.json

case "$cmd" in
  verify)
    exec python3 bench/repair_ab_repro/verify.py ;;
  rederive)
    exec python3 bench/frs_rederive.py ;;
  fidelity)
    exec python3 bench/fidelity_gate.py "$@" --screen "$CORPUS" ;;
  replay)
    corpus_given=0
    for a in "$@"; do case "$a" in --corpus|--corpus=*) corpus_given=1 ;; esac; done
    if [ "$corpus_given" -eq 0 ]; then
      set -- --corpus "$CORPUS" "$@"
    fi
    exec python3 bench/repair_ab_repro/repair_ab_standalone.py "$@" ;;
  score)
    exec python3 bench/score_repair_ab.py "$@" --screen "$CORPUS" ;;
  shell)
    exec bash ;;
  -h|--help|help)
    sed -n '2,19p' "$0" ; exit 0 ;;
  *)
    echo "usage: $0 {verify|rederive|fidelity <file>|replay [args]|score <file>|shell}" >&2
    exit 2 ;;
esac
