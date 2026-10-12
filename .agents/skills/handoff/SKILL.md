---
name: handoff
description: Persist a runtime-neutral PluginForge work handoff before stopping, changing agents, nearing a context limit, or after a meaningful verified checkpoint. Use for unfinished work that another session must resume safely.
---

# Handoff

Write `.agent/HANDOFF.md`, replacing the prior file in this worktree. Begin with:

```markdown
# Handoff

<!-- handoff-meta: head=<full HEAD> written=<UTC ISO-8601> branch=<branch> -->
```

Include objective, branch, current state, completed work, changed files,
verification performed, verification still needed, failed approaches or
important discoveries, unresolved risks, and next recommended action. Preserve
reasoning a machine snapshot cannot reconstruct.

Run `python3 tools/agent_session.py snapshot --trigger handoff` afterward. Do not
commit `.agent/HANDOFF.md` or `.agent/handoff-state.json`. During migration,
read legacy `.claude/` state if no neutral handoff exists; do not overwrite a
different session's legacy handoff.
