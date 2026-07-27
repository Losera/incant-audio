---
name: orient
description: Session-start orientation for PluginForge — injects live repo state plus the open half of STATUS.md (Broken, Assumed, Next three, Waiting on you) instead of reading the whole file. Run this at the start of any non-trivial session, before planning work, or when returning after time away. Trigger on "where are we", "what's the status", "what needs my attention", "catch me up", "/orient".
allowed-tools: Bash(${CLAUDE_PROJECT_DIR}/tools/status_digest.sh)
---

# Orient

Authorized 2026-07-27 to **replace** CLAUDE.md's former "read STATUS.md first, every
session" instruction. The digest below is the session-start read. Reading STATUS.md in
full is now the exception, not the default — see "When to read the file anyway".

Everything under the rule is computed at load time, not recalled.

---

!`${CLAUDE_PROJECT_DIR}/tools/status_digest.sh`

---

## How to use what is above

**The digest reports; it does not verify.** Every line under *Broken* and *Assumed* is
what the last writer of STATUS.md asserted. COLLABORATION.md §3 still applies: before
repeating any of it as fact in your own output, check it against the code, or mark it
*(unverified)*. This project's recurring defect is a claim that outlived the thing it
described — three hooks that never ran, a benchmark measuring a deleted prompt, a
"Current status" section that had been removed.

**`Assumed, never checked` is the metric.** Per CLAUDE.md, every piece of work should
move at least one claim out of that list. It cannot be improved by writing
documentation, which is the entire reason it is the number.

**A staleness banner is a warning, not a blocker.** If it says commits have landed since
STATUS.md was rewritten, the gap is the risk: work landed that nobody wrote down. Ask
what happened in those commits before trusting the Broken list to be complete.

## When to read STATUS.md in full anyway

The digest deliberately omits **"Works — and how we know"** — 57% of the file, and an
evidence archive rather than an open question. Go read it when:

- you need to know *how* a capability was proven, not just that it was;
- you are about to claim something works, and want the artifact that showed it;
- the digest printed **DIGEST INCOMPLETE**. That means STATUS.md no longer has the
  headings `tools/status_digest.sh` reads. Read the file directly this session, then fix
  the script or the headings. **Do not read a short digest as good news** — that
  inference is what made the deleted `attention-report` skill useless for weeks.

## Ending a session

COLLABORATION.md §5: STATUS.md is **rewritten**, not appended to, at the end of any
session that changed something, and §4's five-line change report is due in-session for
each landed change (see `/change-report`). If you changed something and STATUS.md still
carries the old date, the next session's digest will open with a staleness banner
pointing at you.
