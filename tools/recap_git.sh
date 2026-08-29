#!/usr/bin/env bash
#
# Print what has landed in the repo since the last recorded session boundary,
# for /recap's repo-state section.
#
# "Since last session" resolution order:
#   1. .claude/HANDOFF.md's `head=` meta comment -- the HEAD recorded the last
#      time a session deliberately closed out (see .claude/skills/handoff).
#   2. .claude/handoff-state.json's "head" field -- the PreCompact safety net,
#      used only when no HANDOFF.md exists.
#   3. Neither exists: fall back to the last 10 commits, clearly labeled as a
#      fallback rather than a real boundary -- do not let this print as if it
#      were one.
#
# USAGE
#   tools/recap_git.sh
#   Consumed by .claude/skills/recap/SKILL.md via `!` injection.

set -uo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

HANDOFF="$ROOT/.claude/HANDOFF.md"
STATE="$ROOT/.claude/handoff-state.json"

SINCE=""
SOURCE=""

if [[ -f "$HANDOFF" ]]; then
  SINCE=$(grep -o 'head=[0-9a-f]\{7,40\}' "$HANDOFF" | head -1 | cut -d= -f2)
  [[ -n "$SINCE" ]] && SOURCE="HANDOFF.md"
fi

if [[ -z "$SINCE" && -f "$STATE" ]]; then
  SINCE=$(python3 -c "import json,sys
try:
    print(json.load(open('$STATE')).get('head') or '')
except Exception:
    pass" 2>/dev/null)
  [[ -n "$SINCE" ]] && SOURCE="handoff-state.json"
fi

echo "### Since last session"
echo '```'
if [[ -n "$SINCE" ]] && git cat-file -e "${SINCE}^{commit}" 2>/dev/null; then
  echo "boundary   $SINCE (from $SOURCE)"
  COUNT=$(git rev-list --count "${SINCE}..HEAD" 2>/dev/null || echo 0)
  echo "commits    $COUNT since boundary"
  if [[ "$COUNT" != "0" ]]; then
    echo
    git log "${SINCE}..HEAD" --format='  %h %ad %s' --date=short
    echo
    echo "diffstat"
    git diff --stat "${SINCE}..HEAD" | sed 's/^/  /'
  fi
else
  echo "boundary   none recorded -- showing last 10 commits, NOT a real boundary"
  echo
  git log -10 --format='  %h %ad %s' --date=short 2>/dev/null || echo "  (no commits)"
fi
echo '```'
echo

echo "### Working tree right now"
echo '```'
echo "branch     $(git rev-parse --abbrev-ref HEAD 2>/dev/null || echo '?')"
UNPUSHED=$(git rev-list --count '@{upstream}..HEAD' 2>/dev/null || echo "no-upstream")
echo "unpushed   ${UNPUSHED} commit(s) ahead of origin"
DIRTY=$(git status --short 2>/dev/null | wc -l)
echo "dirty      ${DIRTY} path(s) with uncommitted changes"
if [[ "$DIRTY" != "0" ]]; then
  git status --short 2>/dev/null | sed 's/^/           /'
fi
echo '```'
