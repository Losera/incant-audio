---
name: recap
description: Summarize progress on demand -- what happened in this session so far, plus what landed in the repo since the last recorded session boundary. Spoken/printed only, nothing written to disk (contrast with /handoff, which persists context for the next agent). Trigger on "recap", "recap me", "recap this session", "what have we done so far", "summarize progress", "/recap".
allowed-tools: Bash(${CLAUDE_PROJECT_DIR}/tools/recap_git.sh)
---

# Recap

One read-only, ephemeral summary of progress. Nothing here gets written to disk --
if the user wants this preserved for a future session, that's `/handoff`, not this.

## Repo state since the last session boundary

!`${CLAUDE_PROJECT_DIR}/tools/recap_git.sh`

That's computed, not recalled -- read it, don't guess at commit history from memory.
"boundary none recorded" means there's no `.claude/HANDOFF.md` and no PreCompact
snapshot to anchor to, so the commit list under it is a fallback (last 10), not a real
session boundary -- say that plainly rather than presenting it as one.

## What to actually say

**If this conversation has done real work** -- files read for a reason, edits made,
commands run, decisions reached -- lead with *that*, in your own words, not the git
log: what was asked, what's actually been done, decisions made and why, what's still
open, anything blocked on the user. The conversation knows the *why* a commit message
won't carry. Use the repo-state section above only to flag anything landed but not
yet reflected in what you're about to say (e.g. a background agent that finished
while you were talking), not as the primary content.

**If this is genuinely the first substantive turn** -- nothing to recap from the
conversation yet -- the repo-state section above *is* the recap: turn it into a short
prose summary of what landed since the boundary, and whether anything's uncommitted
or unpushed right now.

Either way: a few short paragraphs or a tight list, whatever reads faster. Not a
fixed template, not a document, not a wall of bullet points restating the raw
command output.

## Relationship to the other session-boundary skills

- **`/orient`** answers "what needs my attention" -- STATUS.md's open items (Broken,
  Assumed, Next three) plus CI state. `/recap` answers "what just happened" and has
  no opinion on what's broken or what's next.
- **`/handoff`** writes a structured document to `.claude/HANDOFF.md` for the *next*
  agent to read automatically. `/recap` is read-only and ephemeral, for the human,
  right now -- it doesn't compete with `/handoff` and doesn't substitute for it.
- **`/change-report`** is the five-line CHANGED/WHY/VERIFIED/RISK/YOUR MOVE format for
  one specific landed change. `/recap` can cover several changes, or none, and isn't
  bound to that format.
