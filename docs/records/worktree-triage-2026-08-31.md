# Worktree triage — 2026-08-31

Point-in-time record. Four stale git worktrees existed at the start of the
2026-08-31 dev block; this is what happened to each.

## Removed

### `.claude/worktrees/ui-palette` → branch `fix/ember-console-palette`
Landed as **PR #29** (`ce22ee6` on `main`, squash-merged 2026-08-27). Worktree
removed; branch deleted (`was a0664d2`). Nothing lost — the squash is on `main`.

### `.claude/worktrees/keyboard-band-fix` → branch `fix/workflow-audit-hygiene`
Worktree removed. **Branch kept** (has one unmerged commit worth not discarding
silently). The branch is a stale line — most of its commits (`0b66001` default
groq provider, `87e6bb1` PF-065 resolve, ADR-028 handoff, the doc purge) landed
on `main` independently through other PRs. Its top two:

- **`89268ec` "feat: instrument-conditional keyboard band"** — **already on
  `main`**, implemented independently. `PluginEditor.cpp` has
  `addChildComponent(keyboardPanel)` (starts invisible), `resized()` gates
  `keyboardPanel.setVisible(processor.isInstrumentForTest())`, and
  `verticalChrome`/window sizing is instrument-conditional — with the same
  "another compile could leave keyboardPanel invisible (stale)" comment this
  commit's message describes. Obsolete.
- **`885d4cf` "fix: keyboard band vanishes on a fresh, never-compiled editor"**
  — the one genuinely-unmerged change. It makes a brand-new editor show a dimmed
  "load an instrument to play" band (keyed on `processor.currentSource().isEmpty()`),
  arguing a fresh editor is indistinguishable from a confirmed effect. This is a
  **debatable UX tweak**: `main` deliberately makes effect windows 80px shorter
  with no band, and treating a fresh editor as not-yet-instrument (no band until
  a compile proves otherwise) is a defensible choice. Not cherry-picked. Revisit
  only if the fresh-editor onboarding state is reported as a real problem.

## Left for the human (COLLABORATION.md §2 trigger 1)

### `bench/.worktrees/provider-resilience` → branch `feat/provider-resilience`
Abandoned uncommitted WIP: **+553 / −54 across 16 files**, 18 commits behind
`main`. A deterministic provider-fallback feature (touches `llm/providers.py`,
`llm/generate.py`, `PromptPanel.{cpp,h}`), plus a `docs/decisions.md` entry it
drafted as "ADR-028" — **that number is now the Context-clear handoff protocol**
(`docs/decisions.md:1166`), so this ADR text was orphaned when the number was
reassigned. Its direction (explicit deterministic fallback) is *arguably*
distinct from ADR-032 alternative 4's rejected *automatic* failover. The diff
was bundled for review; the human decides revive-as-a-real-ADR vs discard.
