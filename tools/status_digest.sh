#!/usr/bin/env bash
#
# Print the session-start digest: the actionable half of STATUS.md, plus live
# repo state, in about a fifth of the tokens of reading the file.
#
# WHY THIS EXISTS
#   CLAUDE.md told every session to read STATUS.md in full. That file is ~3,400
#   tokens and 57% of it is the "Works — and how we know" evidence archive, which
#   answers questions nobody asks at session start. This prints what is open:
#   Broken, Assumed, Next three, Waiting on you — and computes the rest instead
#   of recalling it.
#
#   It replaces `attention-report`, deleted 2026-07-27. That skill read
#   CLAUDE.md's "Current status" section (deleted 2026-07-25) and
#   docs/collaboration_log.md (retired in COLLABORATION.md §5, deleted) and said
#   nothing when both came back empty. Silence on a missing input is the exact
#   defect this project keeps rediscovering.
#
# SO: THIS SCRIPT FAILS LOUD
#   Every section it cannot find is reported as a MISSING line and the script
#   exits 1. A digest that quietly shrinks when STATUS.md is restructured would
#   be worse than no digest, because a short report reads like good news.
#
# WHAT IT DOES NOT DO
#   It does not verify a single claim in STATUS.md. Everything under "Broken" and
#   "Assumed" is what the last writer asserted, not what is true today. The
#   staleness banner is the only cross-check, and it compares dates, not facts.
#
# USAGE
#   tools/status_digest.sh          # the digest
#   Consumed by .claude/skills/orient/SKILL.md via !`...` injection.
set -uo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

echo "### Repo state"
echo '```'
echo "HEAD      $(git log -1 --format='%h %ad %s' --date=short 2>/dev/null || echo 'not a git repo')"
echo "branch    $(git rev-parse --abbrev-ref HEAD 2>/dev/null || echo '?')"
UNPUSHED=$(git rev-list --count '@{upstream}..HEAD' 2>/dev/null || echo "no-upstream")
echo "unpushed  ${UNPUSHED} commit(s) ahead of origin"
DIRTY=$(git status --short 2>/dev/null | wc -l)
echo "dirty     ${DIRTY} path(s) with uncommitted changes"
if [[ "$DIRTY" != "0" ]]; then
  git status --short 2>/dev/null | head -15 | sed 's/^/          /'
fi
echo '```'
echo

python3 - <<'PY'
import datetime, pathlib, re, subprocess, sys

status = pathlib.Path("STATUS.md")
if not status.exists():
    print("**MISSING: STATUS.md does not exist.** The digest has no source.")
    sys.exit(1)

txt = status.read_text()
missing = []

# --- staleness: STATUS.md's own date vs the newest commit it could describe ---
m = re.search(r"^#\s*PluginForge\s*—\s*Status\s*\((\d{4}-\d{2}-\d{2})\)", txt, re.M)
if not m:
    missing.append("the dated `# PluginForge — Status (YYYY-MM-DD)` title")
else:
    stamped = datetime.date.fromisoformat(m.group(1))
    try:
        head_day = subprocess.run(
            ["git", "log", "-1", "--format=%ad", "--date=short"],
            capture_output=True, text=True, timeout=10,
        ).stdout.strip()
        head = datetime.date.fromisoformat(head_day)
    except Exception:
        head = None
    print(f"### STATUS.md — stamped {stamped}")
    if head and head > stamped:
        print(
            f"> **{(head - stamped).days} day(s) of commits have landed since this file was "
            f"rewritten** (HEAD is {head}). COLLABORATION.md §5 says it is rewritten at the end "
            f"of any session that changed something. Treat every claim below as that stale."
        )
    print()


def section(pattern: str, label: str) -> str | None:
    """Return the body under the first heading matching `pattern`, or None."""
    m = re.search(rf"^##+\s*{pattern}.*?$(.*?)(?=^##\s|\Z)", txt, re.S | re.M)
    if not m:
        missing.append(label)
        return None
    # Trailing `---` belongs to the *next* section's header rule, not this body.
    return re.sub(r"\n-{3,}\s*$", "", m.group(1).strip()).strip()


# --- Broken: the bold headline of each numbered item, not the full prose ---
broken = section(r"Broken", "the `## Broken` section")
if broken is not None:
    items = re.findall(r"^\*\*(\d+\..+?)\*\*\s*(\*\(.+?\)\*)?", broken, re.M | re.S)
    print(f"### Broken — {len(items)} open")
    if not items:
        print("_(section present but no `**N. ...**` items parsed — check STATUS.md's format)_")
    for head, tag in items:
        head = " ".join(head.split())
        tag = " ".join(tag.split()) if tag else ""
        print(f"- **{head}** {tag}".rstrip())
    print()

# --- Assumed: titles only. The one number this project steers by. ---
assumed = section(r"Assumed", "the `## Assumed, never checked` section")
if assumed is not None:
    claims = re.findall(r"^\s*[-*]\s+\*\*(.+?)\*\*", assumed, re.M | re.S)
    print(f"### Assumed, never checked — {len(claims)} claims  ← the one number")
    for c in claims:
        print(f"- {' '.join(c.split())}")
    print()

# --- Next three + Waiting on you: verbatim. Short, and entirely actionable. ---
for pat, label in ((r"Next three", "the `## Next three things` section"),
                   (r"Waiting on you", "the `## Waiting on you` section")):
    body = section(pat, label)
    if body is not None:
        title = re.search(rf"^(##+\s*{pat}.*?)$", txt, re.M).group(1).lstrip("# ").strip()
        print(f"### {title}")
        print(body)
        print()

if missing:
    print("---")
    print("**DIGEST INCOMPLETE — STATUS.md no longer has the shape this script reads.**")
    for label in missing:
        print(f"- MISSING: {label}")
    print()
    print("Read STATUS.md directly this session, then fix `tools/status_digest.sh` or the")
    print("file's headings. Do not proceed on the assumption that a short digest means")
    print("little is open.")
    sys.exit(1)
PY
