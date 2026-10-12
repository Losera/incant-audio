---
name: orient
description: Session-start orientation for PluginForge. Run before substantive work, after context loss, or when asked where the project stands or what needs attention.
allowed-tools: Bash(python3 ${CLAUDE_PROJECT_DIR}/tools/agent_session.py check), Bash(${CLAUDE_PROJECT_DIR}/tools/status_digest.sh)
---

# Orient

This is the Claude Code adapter for the canonical workflow in
`.agents/skills/orient/SKILL.md`. Follow that file completely.

The adapter injects the two live reports so Claude Code preserves its existing
session-start behavior:

!`python3 ${CLAUDE_PROJECT_DIR}/tools/agent_session.py check`

!`${CLAUDE_PROJECT_DIR}/tools/status_digest.sh`

Reusable workflow changes belong in `.agents/skills/orient/` or `tools/`, not
in this adapter.
