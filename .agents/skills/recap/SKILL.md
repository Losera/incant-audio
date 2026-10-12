---
name: recap
description: Recap PluginForge work in the current conversation and commits since the last portable or legacy handoff. Use when asked what happened, what has been completed, or what remains in the current session.
---

# Recap

Run `tools/recap_git.sh`. Lead with work actually performed in the current
conversation: decisions, edits, verification, blockers, and next action. Use the
script output to add repository work not already covered.

If no boundary exists, say that the displayed commits are only a fallback. This
skill is read-only and does not replace `$handoff`.
