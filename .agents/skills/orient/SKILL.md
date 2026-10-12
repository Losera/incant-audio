---
name: orient
description: Orient a PluginForge session from live repository, worktree, CI, and STATUS.md state. Use before substantive work, after context loss, or when asked where the project stands or what needs attention.
---

# Orient

1. Run `python3 tools/agent_session.py check` from the intended checkout.
2. Run `tools/status_digest.sh`.
3. Fetch `origin` when current remote state matters. If the checkout is behind,
   read `STATUS.md` and `docs/BUGS.md` from `origin/main`, not the stale tree.
4. Read `AGENTS.md`, `CLAUDE.md`, and `COLLABORATION.md`; despite its legacy
   filename, `CLAUDE.md` contains product invariants for every runtime.
5. Report project, branch, dirty state, recent work, verification state,
   uncertainty, and next action.

Treat status prose as reported state. Verify important claims against code or a
named check, or label them unverified. Never edit the shared primary checkout
when another session may be active.
