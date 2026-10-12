---
name: handoff
description: Persist a PluginForge handoff before stopping, changing agents, nearing a context limit, or after a meaningful verified checkpoint.
allowed-tools: Bash(git *), Bash(python3 ${CLAUDE_PROJECT_DIR}/tools/agent_session.py snapshot *)
---

# Handoff

This is the Claude Code adapter for the canonical workflow in
`.agents/skills/handoff/SKILL.md`. Follow that file completely and write the
runtime-neutral `.agent/HANDOFF.md`.

The Claude `SessionStart` injector reads the neutral handoff first and falls back
to `.claude/HANDOFF.md`, so do not maintain duplicate handoff documents.
