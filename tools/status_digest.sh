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
# THE CI SECTION (added 2026-07-28)
#   Every control in this project watched the code; none watched whether the
#   remote gate had reported. On 2026-07-28 CI had been red on four consecutive
#   pushes — since 2026-07-26 — while STATUS.md's Broken list held one item that
#   was not that, and this digest said nothing at all. Local `check.sh full`
#   passes on the same tests because a dev session supplies what a headless
#   runner does not, so the ladder said green and only the unread half said red.
#
#   That is the same failure class as the dead hooks and the 17-commits-behind
#   CI, one level up: not a control that was wrong, a control that reported to
#   nobody. So this section never prints nothing. Red prints a banner, green-on-
#   an-older-commit prints a banner, and "could not tell" prints a banner naming
#   why and what to run by hand.
#
# WHAT IT DOES NOT DO
#   It does not verify a single claim in STATUS.md. Everything under "Broken" and
#   "Assumed" is what the last writer asserted, not what is true today. The
#   staleness banner is the only cross-check, and it compares dates, not facts.
#
#   It does not re-run CI, and it trusts `gh` about what CI concluded. A green
#   run means the runner was happy with that commit, not that HEAD is good — read
#   the `commit` line, which says how far behind HEAD the tested commit is.
#
#   An unknown CI status does NOT exit non-zero. A non-zero exit here means
#   "STATUS.md no longer has the shape this script reads, go read it directly",
#   and overloading it with "you aren't logged into gh" would make the real
#   signal ignorable on any machine without the CLI. Unknown is loud, not fatal.
#
# USAGE
#   tools/status_digest.sh          # the digest
#   Consumed by .claude/skills/orient/SKILL.md via !`...` injection.
#
#   PLUGINFORGE_CI_RUNS_JSON  — test seam. When set, it is used verbatim in place
#   of invoking `gh run list`, so tests/test_control_wiring.py can drive the red,
#   stale-green and malformed cases without a network. Production never sets it.
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

# ------------------------------------------------------------------------- CI
# Deliberately a separate interpreter from the STATUS.md block below: a failure
# to reach GitHub must not be able to swallow the sections that come after it,
# and neither block's exit code should depend on the other's inputs.
python3 - <<'PY'
import json, os, shutil, subprocess, sys

GOOD = {"success", "skipped", "neutral"}
FIELDS = "databaseId,status,conclusion,headSha,createdAt,workflowName,event,url"


def git(*args: str) -> str:
    try:
        p = subprocess.run(["git", *args], capture_output=True, text=True, timeout=10)
    except Exception:
        return ""
    return p.stdout.strip() if p.returncode == 0 else ""


BRANCH = git("rev-parse", "--abbrev-ref", "HEAD") or "?"
HEAD = git("rev-parse", "HEAD")


def unknown(why: str, how: str) -> None:
    """The one thing this section must never do is stay quiet. Say why, and how to check."""
    print(f"### CI — UNKNOWN on `{BRANCH}`")
    print()
    print("> **CI STATUS UNKNOWN.** Nothing else in this digest looks at the remote gate,")
    print("> so treat the build as unverified this session rather than as passing.")
    print(f"> Reason: {why}.")
    print(f"> Check by hand: {how}")
    print()


def load_runs() -> list[dict] | None:
    seam = os.environ.get("PLUGINFORGE_CI_RUNS_JSON")
    manual = f"`gh run list --branch {BRANCH}`"
    if seam is not None:
        raw = seam
    elif shutil.which("gh") is None:
        unknown("`gh` is not on PATH", "open the repository's Actions tab in a browser")
        return None
    else:
        try:
            p = subprocess.run(
                ["gh", "run", "list", "--branch", BRANCH, "--limit", "15", "--json", FIELDS],
                capture_output=True, text=True, timeout=20,
            )
        except subprocess.TimeoutExpired:
            unknown("`gh run list` did not answer within 20s", manual)
            return None
        except Exception as exc:
            unknown(f"`gh run list` could not be invoked ({type(exc).__name__})", manual)
            return None
        if p.returncode != 0:
            first = next((ln for ln in p.stderr.splitlines() if ln.strip()), "no stderr")
            unknown(f"`gh run list` exited {p.returncode}: {first.strip()[:110]}", manual)
            return None
        raw = p.stdout

    try:
        runs = json.loads(raw)
        if not isinstance(runs, list):
            raise ValueError(f"expected a list of runs, got {type(runs).__name__}")
    except Exception as exc:
        unknown(f"could not parse the run list ({exc})", manual)
        return None
    if not runs:
        unknown(f"no workflow runs found for branch `{BRANCH}`", manual)
        return None
    return runs


def commit_line(sha: str) -> tuple[str, int | None]:
    """Describe the tested commit relative to HEAD. Returns (text, commits_behind)."""
    short = (sha or "?")[:7]
    if sha and HEAD and sha == HEAD:
        return f"{short} = HEAD", 0
    if sha:
        n = git("rev-list", "--count", f"{sha}..HEAD")
        if n.isdigit():
            return f"{short} — HEAD is {n} commit(s) ahead of it", int(n)
    return f"{short} — not present locally, distance from HEAD unknown", None


runs = load_runs()
if runs is None:
    sys.exit(0)

latest = next((r for r in runs if r.get("status") == "completed"), None)
in_flight = [r for r in runs if r.get("status") != "completed"]

# Consecutive non-success, newest first. In-flight runs are skipped, not counted:
# a queued run is not yet evidence of anything.
streak = 0
for r in runs:
    if r.get("status") != "completed":
        continue
    if (r.get("conclusion") or "") in GOOD:
        break
    streak += 1

workflow = (latest or runs[0]).get("workflowName") or "workflow"
print(f"### CI — {workflow} on `{BRANCH}`")
print("```")

if latest is None:
    print(f"status    IN PROGRESS — {len(in_flight)} run(s) queued, none completed yet")
    print("```")
    print()
    print(f"> **CI has not concluded on `{BRANCH}` yet.** The most recent push is still")
    print("> being tested, so no run in this window is evidence either way.")
    print()
    sys.exit(0)

conclusion = (latest.get("conclusion") or "unknown").upper()
when = (latest.get("createdAt") or "?").replace("T", " ").replace("Z", "Z")
text, behind = commit_line(latest.get("headSha") or "")

print(f"status    {conclusion}" + (f" — {streak} consecutive" if streak > 1 else ""))
print(f"run       {latest.get('databaseId', '?')}  {when}")
print(f"commit    {text}")
if in_flight:
    print(f"also      {len(in_flight)} run(s) still in progress")
print("```")
print()

if (latest.get("conclusion") or "") not in GOOD:
    nth = f"The last {streak} runs on" if streak > 1 else "The last run on"
    where = "at HEAD" if behind == 0 else "on an earlier commit"
    print(f"> **CI IS RED.** {nth} `{BRANCH}` concluded *{conclusion.lower()}*, most recently")
    print(f"> {where}. A green `tools/check.sh` does not contradict this: the ladder runs a")
    print("> SMALLER set than CI does (PF-029). Read the failure before trusting the Broken")
    print("> list below to be complete:")
    print(f"> `gh run view {latest.get('databaseId', '?')} --log-failed`")
    print()
elif behind is None or behind > 0:
    n = "an unknown number of" if behind is None else str(behind)
    print(f"> **CI is green on a commit that is not HEAD.** {n} commit(s) have landed since")
    print("> the last tested one, and nothing has run against them. Green here is evidence")
    print("> about that commit, not about the tree you are working in.")
    print()
PY

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
