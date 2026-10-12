# PluginForge agent guide

This file is the runtime-neutral entry point for work on PluginForge. Product and
audio-system context remains in `CLAUDE.md` during the compatibility period; its
instructions apply to every agent even where the filename says Claude.

## Start every substantive session

1. Run `python3 tools/agent_session.py check`.
2. Run `tools/status_digest.sh`.
3. Inspect the current branch, `git status`, relevant history, and all applicable
   repository instructions before editing.
4. When other sessions are active, create a dedicated worktree from the intended
   base. Never edit the shared primary checkout.
5. Treat `STATUS.md` as a report. Verify load-bearing claims against code or a
   named check before repeating them as fact.

The portable skills in `.agents/skills/` define `$orient`, `$brief`, `$recap`, and
`$handoff`. A runtime that does not discover repository skills must follow those
files manually. Runtime-specific adapters may automate these procedures, but may
not redefine them.

## Engineering and verification

- Read `COLLABORATION.md` before non-trivial work. Its consult gates and evidence
  rules are runtime-independent.
- Use the cost-ordered verification ladder in `tools/check.sh`.
- Run `tools/check.sh fast` after every code or workflow-control change. Run the
  broader level required by `CLAUDE.md` and the changed surface.
- Claude edit hooks provide early feedback only. Tests and `tools/check.sh` are
  the portable enforcement path; never claim a rule is enforced solely because
  one runtime registered a hook.
- Inspect the semantic diff and intended file set after editing. Substantive work
  waits for human review before commit or merge.

## Session continuity

- Write the current handoff to `.agent/HANDOFF.md` using `$handoff`.
- Before planned context loss, run
  `python3 tools/agent_session.py snapshot --trigger <reason>`.
- Readers prefer `.agent/HANDOFF.md` and `.agent/handoff-state.json`, then fall
  back to legacy `.claude/` state during migration.
- Handoffs are per-worktree, overwritten in place, and never committed.

## Compatibility

`.claude/` contains Claude Code adapters and hooks. Keep them operational while
Claude remains in use, but put new reusable workflow logic in `.agents/` or
`tools/`. Do not duplicate substantive procedures between runtime adapters.
