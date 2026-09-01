#!/usr/bin/env bash
# bench/issue26/docker-entrypoint.sh — dispatch for the issue-#26 repro image.
#
#   verify              re-derive the published numbers from committed data
#                       (no model, no faust-rs, ~1s). This is the default.
#   rederive            re-run the diagnostic-quality half (faust vs faust-rs on
#                       the 15 archived never-compiled cells)
#   replay [args...]    run the paired A/B with your own model — every arg after
#                       `replay` is passed straight to repair_ab_standalone.py
#                       (--backend / --endpoint / --model / --arms / --samples /
#                        --out / ...). --corpus defaults to the bundled one.
#   score <file>        run score_repair_ab.py on a result file
#   shell               drop into bash
set -euo pipefail
ROOT="${PLUGINFORGE_ROOT:-/work}"
cd "$ROOT"
cmd="${1:-verify}"; shift || true

case "$cmd" in
  verify)
    exec python bench/issue26/verify.py ;;
  rederive)
    exec python bench/frs_rederive_issue26.py ;;
  replay)
    corpus_given=0
    for a in "$@"; do [ "$a" = "--corpus" ] && corpus_given=1; done
    set -- "$@"
    if [ "$corpus_given" -eq 0 ]; then
      set -- --corpus bench/corpora/repair_corpus_20260830.json "$@"
    fi
    exec python bench/issue26/repair_ab_standalone.py "$@" ;;
  score)
    exec python bench/score_repair_ab.py "$@" ;;
  shell)
    exec bash ;;
  *)
    echo "usage: $0 {verify|rederive|replay [args]|score <file>|shell}" >&2
    exit 2 ;;
esac
