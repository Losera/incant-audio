# Fleet Restart Runbook

How to bring the 7-session PluginForge fleet back up after a reset — a context wipe, a closed
terminal, or a full machine restart. Overseer-owned. Keep it accurate; it's only useful if it
matches how the fleet actually runs.

---

## What survives a reset — and what doesn't

**Survives (it's on disk / in git):**
- The board and state docs: `docs/FLEET.md`, `STATUS.md`, `docs/BUGS.md`, `docs/.fleet/*`.
- All source, all commits, all git history, and any `git worktree`s.
- Research/plan docs: `docs/competitive_landscape.md`, `docs/byo_llm_plan.md`, etc.
- Signal files already written (e.g. `docs/.fleet/S2_UNBLOCKED`).

**Does NOT survive — must be recreated:**
- **Every session's in-memory context.** A reset session knows nothing until it re-reads the board.
- **Every background watcher, Monitor, and `/loop`.** All the auto-resume relays are detached
  processes; they die on reset. The *signal file* they wrote survives, but the watcher *polling*
  for it is gone and must be re-armed.

**The one rule that makes restart trivial:** the durable files are the fleet's memory. A session
resumes by reading its roll-call row in `FLEET.md` (its "Current task" cell is the resume
pointer), plus `STATUS.md` and `docs/BUGS.md`. So keep those precise — see "Graceful shutdown."

---

## Before a planned reset — graceful shutdown (60 seconds, big payoff)

If the reset is planned, have each session do this first so restart is a no-op:
1. **Each worker** updates its `FLEET.md` roll-call row so "Current task" reads as a *resume
   instruction*, not a status ("Resume: adopt `loadFaustCode(prompt)` at `PluginEditor.cpp:191`,
   then error-line highlight"), and lists any open request numbers it owns.
2. **Each worker** either commits its work or notes "uncommitted WIP in <files>" in its row.
3. **Overseer** commits the coordination scaffolding so nothing rides on an unsaved working tree:
   `git add docs/FLEET.md STATUS.md docs/.fleet/ docs/BUGS.md && git commit`.
4. **Overseer** notes in `FLEET.md` "Gate state" which relays are armed, so they get re-armed.

If the reset was unplanned, skip this and reconstruct from the board + `git status` + `git log`.

---

## Restart order

1. **Overseer (S6) first.** It owns the board and re-arms the relays the workers depend on.
2. **Then workers** S1–S5, S7 — any order; they're lane-isolated.

---

## Step 1 — relaunch the Overseer (S6)

Paste into a fresh session:

> You are the Overseer (S6) of the PluginForge multi-session fleet, resuming after a reset. Read
> `CLAUDE.md`, `COLLABORATION.md`, `STATUS.md`, `docs/FLEET.md` (roster, roll call, gate state,
> request log, overseer rulings), `docs/BUGS.md`, and `docs/.fleet/README.md`. You are the single
> writer of `STATUS.md` and own `docs/FLEET.md`; you write no feature code. Reconstruct current
> state from the board and `git log --oneline -15` + `git status`. Then: (a) re-arm any pending
> auto-resume relay whose gate is still open (see `docs/.fleet/README.md`); (b) resume
> consolidating each session's 5-line reports into `STATUS.md`; (c) keep `BUGS.md` and STATUS
> "Broken" in sync; (d) route open cross-lane requests and the human-gated items. Update your own
> roll-call row to ACTIVE.

## Step 2 — re-arm the watchers (critical; they don't survive a reset)

The overseer runs these. Only arm a relay whose gate is **still open** — check `FLEET.md` "Gate
state" first. A signal file that already exists means that relay is *done*; don't re-arm it.

**Generic gate-watcher pattern** (Bash, `run_in_background: true`) — exits when the condition is
true, which re-invokes the overseer to validate and flip the signal:
```
until <gate-condition>; do sleep 120; done
echo "GATE-DETECTED: <name>"
```
Then, on wake: validate the work is real, and `touch docs/.fleet/<SIGNAL>`.

The blocked worker arms the mirror half and resumes when the signal appears:
```
until [ -f docs/.fleet/<SIGNAL> ]; do sleep 120; done
```

See `docs/.fleet/README.md` for the worked S2 example (Gate A = panel files exist; Gate B =
`getStateInformation` no longer an empty stub → signal `S2_UNBLOCKED`).

## Step 3 — relaunch the workers

Paste the matching prompt into each fresh session. Each is deliberately short: the durable board
carries the detail, and the roll-call "Current task" cell is the resume pointer.

> **S1 Backend Core** — You are S1 (Backend Core) resuming after a reset. Read `CLAUDE.md`,
> `COLLABORATION.md`, `STATUS.md`, `docs/FLEET.md`, `docs/BUGS.md`. Your lane and the "must not
> touch" list are in the FLEET roster. Find your roll-call row — its "Current task" is where you
> left off — and check the request log for rows addressed to you. Re-read `git log` for what you
> already committed. Update your row to ACTIVE and continue; emit a 5-line change-report per
> landed change; don't rewrite `STATUS.md`.

> **S2 Prompting UX** — You are S2 resuming after a reset. Read `CLAUDE.md`, `COLLABORATION.md`,
> `STATUS.md`, `docs/FLEET.md`, `docs/ui_design_plan.md`, `docs/ux_roadmap.md`. Your lane:
> `PromptPanel.{h,cpp}`, `CodeEditorPanel.{h,cpp}`. Check whether `docs/.fleet/S2_UNBLOCKED`
> exists: if yes, you're cleared for Wave 1 — flesh out PromptPanel (indeterminate progress
> indicator, not stderr — overseer ruling #2 option (a)) then CodeEditorPanel per ux_roadmap
> Phase 3, and adopt `loadFaustCode(prompt)` at the editor call site (request #4). If the signal
> is absent, re-arm your Stage-2 watcher (above) and end your turn. Update your row; report per change.

> **S3 Plugin UX** — You are S3 resuming after a reset. Read `CLAUDE.md`, `COLLABORATION.md`,
> `STATUS.md`, `docs/FLEET.md`, `docs/ui_design_plan.md` (§3). Your lane: `ParamGridPanel.{h,cpp}`,
> the `PluginEditor` shell + top-level `resized()`. FIRST run `git status` — the Task-0 panel
> split may be uncommitted; if so, verify it builds and **commit it** (that clears a STATUS "Next
> three" item). Then continue with the auto-layout / 8-knob-cap work per your row. Update your row;
> report per change.

> **S4 Testing** — You are S4 resuming after a reset. Read `CLAUDE.md`, `COLLABORATION.md`,
> `STATUS.md`, `docs/FLEET.md`, `docs/prototype_test_plan.md`, `docs/p6_test_battery.md`. Your
> lane: `tests/*`, `host/tests/*`, and those two docs. Priority per the board: **build-verify the
> uncommitted PluginEditor split** and re-run the Python + C++/TSan suites, reporting any
> regression. Finish the copy-pasteable human listening script. File bugs with S5, not fixes.
> Update your row; report per change.

> **S5 Bug tracking** — You are S5 resuming after a reset. Read `COLLABORATION.md`, `STATUS.md`,
> `docs/FLEET.md`, `docs/BUGS.md`. Your lane: `docs/BUGS.md` only — record/triage/route, don't
> fix. Reconcile the registry against the board's request log (new bugs routed to you) and against
> STATUS "Broken". Update your row; report per change.

> **S7 Competitive research** — You are S7 resuming after a reset. Read `COLLABORATION.md`,
> `STATUS.md`, `docs/FLEET.md`, `docs/competitive_landscape.md`, `docs/byo_llm_plan.md`. You are
> read-only on all other lanes' files; you advise via the Advisory feed. Run `git worktree list`
> — your BYO-LLM Phase-0 work may live in a worktree; reconnect to it. BYO-LLM is human-authorized
> (request #6); continue only the phases the board shows authorized, and route new phases as
> requests. Update your row; report per change.

---

## Step 4 — reconnect worktrees

Some lanes work in isolated worktrees (e.g. S7's BYO-LLM Phase 0). After a reset:
```
git worktree list          # enumerate them
git worktree prune         # drop stale entries whose dirs are gone
```
Point the owning session at its worktree path; don't delete a worktree with uncommitted work
without the owner confirming.

---

## Step 5 — verification checklist

The fleet is healthy again when:
- [ ] All seven roll-call rows read **ACTIVE** with a current task.
- [ ] `FLEET.md` "Gate state" matches reality (`git status` / files on disk), no stale ⬜/✅.
- [ ] Every still-open gate has its watcher re-armed (no gate is silently un-watched).
- [ ] No orphaned watchers from before the reset are double-firing (they died with the reset — but
      if the machine survived, check `jobs` / background task list).
- [ ] `git worktree list` accounts for every in-flight isolated branch.
- [ ] `STATUS.md` has exactly one writer (overseer) and no merge markers.
- [ ] Human-gated items still open are visible in STATUS "Waiting on you" (format sign-off,
      benchmark authorization, and whatever else is live).
