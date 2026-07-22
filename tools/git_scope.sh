#!/usr/bin/env bash
# git_scope.sh — who is touching what, right now.
#
# PluginForge is edited by the human and by Claude concurrently, sharing one
# working tree, one index, and one HEAD. Neither party can see the other's
# in-flight work without asking git. On 2026-07-21 Claude discovered a
# substantial parallel rewrite (the system prompt, COLLABORATION.md, two hooks,
# a staged file deletion) only by accident, ~40 minutes into a task whose plan
# that rewrite had already invalidated.
#
# Run this:
#   * at the start of a session, before planning anything
#   * before any commit
#   * after a long build/test cycle, before assuming the tree is as you left it
#
# Usage: tools/git_scope.sh
set -uo pipefail

cd "$(git rev-parse --show-toplevel 2>/dev/null)" || {
    echo "not inside a git repository" >&2; exit 1; }

bold=$'\033[1m'; dim=$'\033[2m'; red=$'\033[31m'; yellow=$'\033[33m'
green=$'\033[32m'; reset=$'\033[0m'

echo "${bold}PluginForge — working tree scope${reset}"
echo "${dim}branch $(git rev-parse --abbrev-ref HEAD)  @  $(git log -1 --format='%h %s' | cut -c1-60)${reset}"
echo

# ── 1. The trap: anything already staged ────────────────────────────────────
# A bare `git commit` sweeps the index, whoever filled it. This is the single
# check that would have caught the 2026-07-21 near-miss.
staged="$(git diff --cached --name-status)"
if [ -n "$staged" ]; then
    count=$(printf '%s\n' "$staged" | wc -l)
    echo "${red}${bold}!! INDEX NOT EMPTY — ${count} path(s) already staged${reset}"
    printf '%s\n' "$staged" | sed 's/^/     /'
    echo
    echo "   ${yellow}A bare \`git commit\` would include these, whoever staged them.${reset}"
    echo "   Commit only your own paths:"
    echo "     ${dim}git commit -F <msgfile> --only <path> [<path>...]${reset}"
    echo
else
    echo "${green}index clean${reset} — nothing pre-staged"
    echo
fi

# ── 2. Uncommitted work, grouped by area ────────────────────────────────────
echo "${bold}Uncommitted work by area${reset}"
git status --porcelain | awk '
{
    path = substr($0, 4)
    # strip rename arrows: "old -> new"
    if (match(path, / -> /)) path = substr(path, RSTART + 4)
    n = split(path, parts, "/")
    area = (n > 1) ? parts[1] "/" : "(root)"
    untracked = (substr($0, 1, 2) == "??")
    if (untracked) u[area]++; else m[area]++
    seen[area] = 1
}
END {
    for (a in seen)
        printf "  %-14s %2d modified  %2d untracked\n", a, m[a] + 0, u[a] + 0
}' | sort

total=$(git status --porcelain | wc -l)
if [ "$total" -eq 0 ]; then
    echo "  ${green}clean — nothing uncommitted${reset}"
fi
echo

# ── 3. What changed since you last looked ───────────────────────────────────
# Files modified in the last 30 minutes are very likely someone else's active
# work, not yours from earlier in the session.
echo "${bold}Touched in the last 30 minutes${reset} ${dim}(likely someone's live edits)${reset}"
recent=$(git status --porcelain | while read -r _ p; do
    p="${p#\"}"; p="${p%\"}"
    [ -f "$p" ] && [ -n "$(find "$p" -mmin -30 2>/dev/null)" ] && echo "  $p"
done)
if [ -n "$recent" ]; then
    printf '%s\n' "$recent"
    echo
    echo "  ${yellow}Do not edit these without checking — they may be open in an editor.${reset}"
else
    echo "  ${dim}(none)${reset}"
fi
echo

# ── 4. Long-lived uncommitted work is the real blast radius ─────────────────
if [ "$total" -gt 12 ]; then
    echo "${yellow}${bold}Note:${reset} ${total} uncommitted paths. The larger this gets, the more"
    echo "expensive any mistake becomes, and the harder it is to tell whose work is whose."
    echo "Landing a coherent slice now costs less than untangling it later."
fi
