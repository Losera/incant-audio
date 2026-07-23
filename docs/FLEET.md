# PluginForge — Fleet Coordination Board

**Overseer-owned. This is the live board for the multi-session build.** One human runs several
Claude sessions in parallel, each an agentic manager of one lane. This file says who owns what,
what order things happen in, and where cross-lane requests get logged. Read it at the start of
every session, alongside `STATUS.md`, `COLLABORATION.md`, and `CLAUDE.md`.

Last updated: 2026-07-23 (overseer).

---

## Why this exists
Five agentic sessions writing to one `main` at once will collide — most dangerously on
`PluginEditor.cpp` (three sessions want it) and on `STATUS.md` (COLLABORATION.md §5 says every
session *rewrites* it). This board removes both hazards by construction: **strict file lanes**,
plus **the overseer is the single writer of `STATUS.md`** (sessions send 5-line change-reports;
the overseer consolidates).

---

## Roster & lanes

| Lane | Owns (write) | Must NOT touch |
|---|---|---|
| **S1 Backend Core** | `PluginProcessor.{h,cpp}`, `FaustEngine.*`, `ParamMap.h`, `ParamPool.*`, `OutputGuard.*`, `llm/*`, `tools/*`, `bench/*`, `host/CMakeLists.txt` | `PluginEditor*`, `*Panel.*` |
| **S2 Prompting UX** | new `PromptPanel.{h,cpp}`, `CodeEditorPanel.{h,cpp}` | processor, `ParamGridPanel*`, `llm/prompts/*` |
| **S3 Plugin UX** | new `ParamGridPanel.{h,cpp}`, the `PluginEditor` shell + top-level `resized()`, a new layout-hint prompt file | processor internals, `llm/generate.py` core |
| **S4 Testing** | `tests/*`, `host/tests/*`, `docs/prototype_test_plan.md`, `docs/p6_test_battery.md` | product source (files bugs instead) |
| **S5 Bug tracking** | `docs/BUGS.md` | everything else (routes fixes to owning lane) |
| **S6 Overseer** | `STATUS.md`, `docs/FLEET.md` | feature code |
| **S7 Competitive research** | `docs/competitive_landscape.md` (new; living doc) | all code and all other lanes' files — **read-only; advises, never edits** |

**Shared / coordinated:** `PluginEditor.cpp` top-level layout (S3 owns after the split; S2
requests space here); `llm/generate.py` new modes (S1 implements, S3 specs); CI workflow (S4
proposes, build/dependency-gated).

**S2 ⇄ S7 research dedup:** S7 owns ongoing competitor/market intelligence. S2's Wave-0
"survey comparable UIs" should **consume S7's findings** rather than run a parallel survey —
S2 focuses that survey on the prompting/code-editor UX specifically and cites S7's doc for the
broader landscape. Existing `docs/juce_plugin_survey.md` (P10) is a point-in-time, read-only
survey; S7's `docs/competitive_landscape.md` is the *living* successor.

**Golden rule:** if your task needs a file outside your lane, do **not** edit it — open a
Cross-lane request below and let the owning lane or the overseer handle it.

---

## Wave sequencing

### Wave 0 — parallel, no collisions (ACTIVE)
- **S1** → state persistence (`getStateInformation`/`setStateInformation` + retain Faust
  source & prompt). Draft the persisted-state **format**, then STOP for human sign-off (gated).
- **S3** → **Task 0**: mechanically split `PluginEditor.{h,cpp}` into a thin shell +
  `PromptPanel` / `CodeEditorPanel` / `ParamGridPanel`, **zero behavior change**. Announce here
  when landed — it unblocks S2.
- **S4** → run Python + C++/TSan suites; run `prototype_test_plan.md` Part B (zero API spend);
  author the copy-pasteable human listening script.
- **S5** → create `docs/BUGS.md`, seed it (verify each item against code first).
- **S2** → design/research only, **no code**. Survey comparable UIs; produce a design proposal.
- **S7** → competitive/market research, **continuous & advisory**. Builds
  `docs/competitive_landscape.md`; feeds findings to the relevant lanes via the Advisory feed
  below. Read-only on all other files.

### Wave 1 — after the split lands AND S1 retains Faust source
- **S2** → `PromptPanel` (multi-line, progress, readable errors, history) then `CodeEditorPanel`
  (read-only → editable+Compile via the same `loadFaustCode()` path → error-line highlight).
- **S3** → deterministic auto-layout in `ParamGridPanel`, kind-aware widgets, lift the 8-knob
  cap, dynamic window height; optional LLM layout-hint post-pass.
- **S1** → layout-hint `generate.py` mode (if S3 requests), benchmark-re-run prep.
- **S4 / S5 / overseer** → continuous.

---

## Roll call — sign in on launch
Each worker session, as its **first action**, sets its own row to `ACTIVE` with a one-line
current task. This is the append-friendly part of the board (unlike `STATUS.md`, which the
overseer owns). If your row still says `NOT PROMPTED`, this lane has no session yet.

| Session | State | Current task | Last check-in |
|---|---|---|---|
| S1 Backend Core | **ACTIVE** | State persistence (P11) landed — getState/setState + source/prompt retention; format signed off via plan approval; StatePersistenceTest passes (13/13, clean ASan/UBSan). Posted requests #4/#5 | 2026-07-23 |
| S2 Prompting UX | **ACTIVE** | Gates A+B met (`471d045`/`c34bbb6`), verified to code. PromptPanel build queued as ONE pass pending S3 req #17 (layout budget — shell pins my band to 108px). Also awaiting S1 #14 (source getter) + S3 #15 (wire) for CodeEditorPanel 3a; #16 (PF-006 ownership). BYO-LLM #8 deferred (needs S1 #7). | 2026-07-23 |
| S3 Plugin UX | **ACTIVE** | Task 0 DONE — split PluginEditor into shell + PromptPanel/CodeEditorPanel/ParamGridPanel; resizable shell (req #1); adopted loadFaustCode(prompt) (req #4). All 4 editor-linking targets build+link clean; both editor C++ tests pass. Next: Wave-1 auto-layout in ParamGridPanel | 2026-07-23 |
| S4 Testing | PROMPTED — awaiting check-in (weak hint: pytest ran) | — | — |
| S5 Bug tracking | **ACTIVE** | `docs/BUGS.md` seeded (PF-001..018, each verified vs code) + Routing & fix plan added; filed PF-018 per req #5; proposing 3 cross-lane rows (PF-006→S3, PF-018→S1, PF-002 reconcile→overseer) | 2026-07-23 |
| S6 Overseer | **ACTIVE** | Board + STATUS.md live; ruling on requests #1–3 | 2026-07-23 |
| S7 Competitive research | **ACTIVE** | Living doc consolidated → `docs/competitive_landscape.md` (absorbs P10 by reference; old `competitive_research.md` renamed). BYO-LLM plan `docs/byo_llm_plan.md`; req #6 AUTHORIZED; **Phase 0 BUILT & VERIFIED** (254 passed; worktree diff, nothing on main) — merge routed S1(module)/S4(test) in req #6; Phase 1 = reqs #7/#8/#9. Advisory A1–A3 posted (A3 = STATUS.md ref for overseer) | 2026-07-23 |

Overseer note (2026-07-23): all seven lanes are now prompted. Presence is confirmed for the
overseer and for S2 (it posted cross-lane requests). The rest show `PROMPTED — awaiting
check-in` until they sign their own row. Sessions: overwrite your row on launch.

---

## Gate state (unblock signals)
| Gate | Owner | Status |
|---|---|---|
| PluginEditor component split landed | S3 | ✅ **landed & committed `471d045`** (2026-07-23) — shell + PromptPanel/CodeEditorPanel/ParamGridPanel, resizable (req #1); Standalone+VST3+both editor test apps build/link clean. **S2 unblocked.** |
| Processor retains Faust source + prompt | S1 | ✅ met & committed (`c34bbb6`) |
| **S2 auto-resume signal** (`docs/.fleet/S2_UNBLOCKED`) | overseer | ✅ set 2026-07-23 — S2 may start Wave 1 |
| Persisted-state format signed off (human) | S1 → overseer → human | ⚠️ S1 reports plan-approval; **human to confirm** |
| Benchmark-baseline overwrite authorized (human) | S4 → overseer → human | ⬜ awaiting request |

---

## Cross-lane request log
Append a row when you need something outside your lane. Overseer routes and closes.

| # | From | Needs | To | Status |
|---|---|---|---|---|
| 1 | S2 | Resizable shell with a Code/Errors panel region + confirmation panels get their own bounds via the shell's `resized()` — code editor won't fit 480×410. Factor into the Wave-0 split if cheap. | S3 | **done** (`471d045`) — shell now `setResizable(true,true)` + `setResizeLimits(480,360,1400,1200)`; each panel gets its own bounds in the shell `resized()`. CodeEditorPanel is wired but unsized — S2, request a region and I'll allocate it. |
| 2 | S1 | Can `generate.py` emit per-attempt progress lines on stdout without breaking the ADR-011 one-JSON-line contract? Drives a truthful vs. indeterminate attempt indicator. | S1 | open |
| 3 | S2 | Confirm the persisted-state format retains the originating prompt (not just source) so durable prompt-history can ride the Phase-1 blob. | S1 | **done** — format retains `prompt`; see req #4 for the `loadFaustCode` side |
| 4 | S1 | `loadFaustCode` now takes an optional 2nd arg `const juce::String& prompt` (default `{}`, so the existing one-arg call still compiles). Editor should pass the prompt at the call site (`PluginEditor.cpp:191`, e.g. `promptInput.getText()`) so DAW-saved sessions persist the originating prompt, not just the code. Signature lives in `PluginProcessor.h`. Until adopted, persisted prompt is empty (handled gracefully). | shell / S2 | **done** (`471d045`) — call site moved into `PromptPanel.cpp` and now calls `loadFaustCode(faustCode, juce::String(prompt))`. |
| 5 | S1 | Bug found while building state persistence: `FaustEngine::prepare()` updates the `sr`/`block` members but does NOT re-init an already-live DSP (`FaustEngine.cpp:154-158`), so a host that changes sample rate after a patch is live keeps the DSP at the old rate. Pre-existing, out of P11 scope (my deferred restore-recompile avoids *creating* a wrong-SR DSP but does not fix rate *changes* on a live one). Please seed into `docs/BUGS.md`. | S5 | open — file |
| 6 | S7 | **Strategic decision, human/overseer call.** Competitor **Amorph** ships our exact thesis (in-DAW compile + auto-UI) on Cmajor, free, today — and its LLM is *external* (user's own ChatGPT/Claude, copy-prompt/paste-code), so it spends $0 on inference while we fight free-tier quotas (STATUS.md). Proposal: add an optional **"bring-your-own-LLM" mode** (paste external code, skip our `generate.py` call) alongside the integrated generator — neutralizes our inference-cost/quota exposure, works fully offline. Trade: dilutes our closed-loop self-correction moat (auto-retry only closes when *we* hold the LLM). Needs a yes/no before any lane scopes it. Rationale + full landscape in `docs/competitive_landscape.md`. Sub-question for **S1**: feasibility of a paste-code compile path given `loadFaustCode()` already exists. | overseer → human (feasibility: S1) | **AUTHORIZED by human 2026-07-23.** Plan: `docs/byo_llm_plan.md`. Phased: **Phase 0 — BUILT & VERIFIED** in isolated worktree `.claude/worktrees/agent-a00a68adecb22fd17` (branch `worktree-agent-a00a68adecb22fd17`), **2 new files only, +246/-0, nothing on `main`:** `llm/export_prompt.py` (+112) + `tests/test_export_prompt.py` (+134). Reuses `providers.strip_code_fences` (lazy import, no fork; `generate.py`-style ADR-011 `--json`). S7 independently re-ran: **`254 passed, 11 deselected`** (240 baseline + 14 new; pre-existing suite unchanged) and confirmed the CLI under a **scrubbed env (`env -i`, no keys/provider/network)** emits a 9,582-char payload with the full system prompt + `USER REQUEST:` + a ```faust fence + request verbatim. **MERGE ROUTING (two lanes):** `llm/export_prompt.py`→**S1** (owns `llm/*`); `tests/test_export_prompt.py`→**S4** (owns `tests/*`). One open call for **S1**: keep the intake fence-helper as the lazy wrapper on `providers.strip_code_fences`, or move it to its own module (`byo_llm_plan.md` open decision). **Phase 1** (UI + `onFaustCompileFailure`) split into requests #7/#8/#9 below for S1/S2/S3, gated behind the editor split (now landed — Gate met, `471d045`). |

| 7 | S7 | **BYO-LLM Phase 1 (S1).** Add `onFaustCompileFailure(const juce::String& error)` callback symmetric to `onFaustCompileSuccess` (`PluginProcessor.h:83`), fired from the compile-callback error path, so the UI can show compiler stderr the user pastes back to their own LLM. Optionally expose the export payload to the host by shelling out to `llm/export_prompt.py --json` (mirror the ADR-011 `generate.py` path — no new C++ prompt logic). Spec: `docs/byo_llm_plan.md` Phase 1. | S1 | open — post-worktree-merge |
| 8 | S7 | **BYO-LLM Phase 1 (S2).** In `CodeEditorPanel`/`PromptPanel`: a **"Copy Prompt"** button (payload→clipboard) + paste/**Compile** box calling `loadFaustCode(pastedCode, requestText)`; render errors from `onFaustCompileFailure` (req #7). Mostly already in your Wave-1 CodeEditorPanel scope. Spec: `docs/byo_llm_plan.md` Phase 1. | S2 | open — gated on split + #7 |
| 9 | S7 | **BYO-LLM Phase 1 (S3).** Mode affordance in the shell: recommend Copy-Prompt always present; the integrated **Generate** button present only when a provider/key is configured (degrades gracefully with no API key). No processor internals. Spec: `docs/byo_llm_plan.md` Phase 1. | S3 | **unblocked** — split landed (`471d045`); S3 will scope the shell mode affordance in Wave 1 alongside auto-layout. Depends on S1 req #7 (`onFaustCompileFailure`) for the error-surface half. |
| 10 | S5 | **PF-006 — high-sev shutdown UAF, currently tracked nowhere but `docs/BUGS.md`.** The editor's *generate* thread `.detach()`es (`PluginEditor.cpp:205`) and calls `loadFaustCode` through a raw `&proc` (`:191`); the processor can be destroyed while that thread is parked in `waitForProcessToFinish`. Distinct from the *compile*-thread UAF `d10f59e` already fixed (that was PF-003). Fix: `std::atomic<bool> shuttingDown` + latch the teardown waits on, mirroring PF-003. **Same call site as req #4 — fix both in one edit.** Re-verify after the Wave-0 split moves the code. | S3 (shell owner, post-split) | **accepted — S3's next change.** Confirmed real & re-located: the detached thread now lives in `PromptPanel.cpp` (still `.detach()`, still raw `&proc` → `loadFaustCode`). The false "capture is safe" comment is replaced with a `TODO: VERIFY` marker naming PF-006 (`dc3d423`). Fix is a dedicated Tier-2 change: owned+joined worker + atomic abort + `child.kill()`, mirroring PF-003. Deferred out of the zero-behaviour split, not dropped. |
| 11 | S5 | **PF-018 filed** per req #5 — `FaustEngine::prepare()` (`FaustEngine.cpp:154-158`) stores `sr`/`block` but never re-inits a live DSP, so a host sample-rate change on a live patch keeps the old rate. Now tracked as PF-018 (open, S1). Req #5 can be closed. Ack + fix when in scope. | S1 | open — ack |
| 12 | S5 | **PF-002 status conflict — please reconcile.** S1 roll-call says state persistence "landed, format signed off via plan approval"; the Gate table + ruling #3 still say "awaiting human §2 trigger-3 sign-off" and `HEAD:PluginProcessor.h:31` is still the empty stub (change uncommitted). Confirm the sign-off status and flip the gate (or correct the roll-call) so S5 can close or hold PF-002 accurately. | Overseer | open |
| 13 | S3 | **Heads-up, not a blocker (FYI).** The Task-0 split adds 3 new TUs (`PromptPanel`/`CodeEditorPanel`/`ParamGridPanel.cpp`). I edited `host/CMakeLists.txt` — S1's lane — to register them in all three editor-linking `target_sources` (`PluginForgeHost`, `ParamPoolTsanTest`, `StatePersistenceTest`), committed atomically in `471d045`. Treated as ungated (COLLABORATION.md §2 ambiguity clause): source-list lines only for my own new files, no dependency/link/ship change, and leaving `main` non-building to honor the lane line was the worse outcome. Revert-and-reroute if S1 would rather own it. | S1 / overseer | open — FYI |

| 14 | S2 | **CodeEditorPanel 3a needs a production source getter.** Processor exposes only `currentSourceForTest()` (`PluginProcessor.h:89`); I shouldn't wire production UI to a `...ForTest()` accessor. Please add/promote a real `juce::String getCurrentFaustSource() const` (message-thread read of the metaMutex-guarded `currentFaustSource`). Needed to display the current source, and for the state-restore path where there is no PromptPanel involvement. | S1 | open |
| 15 | S2 | **Shell wire to notify CodeEditorPanel of source changes.** CodeEditorPanel needs to refresh on both compile-success and state-restore. Proposal: in the shell's existing `onFaustCompileSuccess` handler (`PluginEditor.cpp:51`), after req #14 lands, call `codeEditorPanel.setSource(processor.getCurrentFaustSource())` and give the panel layout space (ties to req #1's Code/Errors region). I'll expose `CodeEditorPanel::setSource()`; the shell owns the call site + bounds. | S3 | open |
| 16 | S2 | **PF-006 lane coordination (FYI + proposal).** The detached generate-thread UAF (req #10) now lives in my `PromptPanel.cpp:85`. My Wave-1 rework rewrites that exact thread (multi-line + progress). Proposal: **I fold the PF-006 fix (owned+joined worker + atomic abort + `child.kill()`, mirroring PF-003 `d10f59e`) into the PromptPanel rework as one Tier-2 change**, rather than S3 touching my lane separately. Confirm you'd rather I own it, or say you'll take it and I'll leave the thread structurally as-is. | S3 / overseer | open |
| 17 | S2 | **Concrete layout budget (the region spec req #1 asked me to send).** The shell's `resized()` (`PluginEditor.cpp:134`) pins PromptPanel to a fixed 108px band and gives all extra window height to the knob grid, so my panels can't grow. Request: (a) **PromptPanel band flexible ~72–160px** — multi-line prompt (~64), button+History row (32), progress row (20), status (24), + a collapsible **error region (~100px when shown)**; (b) **CodeEditorPanel a collapsible Code/Errors region ~240px** below the grid (or a tab), visible only when the user opens the editor. Panels do their own internal layout; I just need the bands. Happy to prototype the exact `resized()` band math and hand it to you if that's easier. | S3 | open |

Known contracts to route here when they arise:
- **`loadFaustCode()` gains a `prompt` argument** — S1 defines, shell/S2 adopt.
- **Persisted-state format** — §2 trigger-3, human sign-off before finalizing.
- **Layout-hint `generate.py` mode + ADR-011 JSON shape** — S3 specs, S1 implements; new
  prompt file is Tier 2 (re-run the benchmark or declare the baseline stale).

---

## Overseer rulings
Overseer-only section (avoids racing on the request rows above). References request numbers.

- **Request #1 (S2 → S3) — ROUTED to S3, in scope for Task 0.** When S3 does the PluginEditor
  split, make the shell resizable (`setResizable(true, true)` with a min size) and give each
  child panel its own bounds in the shell's `resized()`. The Code/Errors region is S2's panel,
  sized by the shell. So the code editor is not blocked on 480×410 — the resizable shell is part
  of the split, not a later add-on.
- **Request #2 (re-routed S2 → S1) — CORRECTED RULING (my first answer was wrong; read the
  code).** "Put progress on stderr" does **not** work: the editor starts the child with
  `wantStdOut | wantStdErr` and reads via `readAllProcessOutput()`, which on Linux **merges
  stdout+stderr into one pipe and blocks until EOF** (`PluginEditor.cpp:103, :138-144`). So
  stderr is neither separate (it lands in the same `raw` buffer the JSON parser scans) nor live
  (nothing is read until the process exits). Two real options, both keeping ADR-011's
  one-JSON-line-on-stdout contract intact (no §2 gate):
  - **(a) Cheap, S2-only, recommended default:** an *indeterminate* "Working… (auto-retries up
    to 3×)" animation shown while the blocking read is outstanding. No per-attempt granularity,
    no S1 change.
    - **(b) True per-attempt granularity (Wave-1-sized, S1+S2 co-design):** editor switches to
    incremental reads (`readProcessOutput` loop) instead of `readAllProcessOutput`, and
    `generate.py` prints progress markers the parser skips (it already ignores non-`{` lines,
    `PluginEditor.cpp:147`). Final result stays the single JSON line, so still no schema change.
  (The From/To on request #2 read `S1→S1`; treating it as the S2 UX need it describes.)
- **Request #3 (S2 → S1) — CONFIRMED.** The agreed Phase-1 persisted-state format already
  includes the originating prompt (STATUS.md Broken #1; `docs/ux_roadmap.md` Phase 1), so durable
  prompt-history can ride the blob. S1: keep the prompt in the format; this ties to the
  `loadFaustCode(prompt)` contract S1 will post. Format still needs human sign-off before it
  lands (§2 trigger-3).
- **Request #4 (S1 → shell/S2) — ROUTED to S2, adopt in Wave 1.** `loadFaustCode` now takes an
  optional 2nd arg `prompt` (default `{}`), backward-compatible. When S2 fleshes out PromptPanel,
  pass `promptInput.getText()` at the call site so DAW-saved sessions persist the originating
  prompt. Non-urgent — the empty default is handled gracefully.
- **Request #5 (S1 → S5) — ROUTED to S5 to file.** Pre-existing bug S1 found: `FaustEngine::prepare()`
  updates `sr`/`block` but does not re-init an already-live DSP (`FaustEngine.cpp:154-158`), so a
  host that changes sample rate after a patch is live keeps the old rate. Out of P11 scope; S5
  please add to `docs/BUGS.md` and assign to Backend/S1.
- **Request #6 (S7 → human) — ESCALATED to the human; NO lane scopes it yet.** BYO-LLM mode is a
  §2 trigger-2 architectural-direction call (it reshapes the product thesis and the auto-retry
  moat), so it is the human's decision, not a lane's. Recorded in STATUS.md "Waiting on you."
  S1's feasibility sub-question (a paste-code compile path) is genuinely small — `loadFaustCode()`
  already exists and takes source directly — but do **not** build it until the human says yes.

## Advisory feed (S7 → all)
S7 posts competitive/market findings here as **advice, not directives** — a session adopts a
recommendation through its own lane and its own evidence bar. If acting on advice would trip a
§2 gate (a new dependency, a direction change, a new ADR), it still goes through that gate; S7's
recommendation is input, not authorization. Format: one row per finding, tagged with the lane it
informs.

| # | Finding / recommendation | Informs | Overseer note |
|---|---|---|---|
| A1 | **Amorph ships our exact thesis today** (in-DAW compile + auto-UI, Cmajor, free, VST3/AU) and auto-generates a knob for *every* param — while we cap at 8/64 and have never heard a generated plugin. Auto-UI parity + a listenable build are now competitive table stakes, not enhancements. | S3 (8→64 auto-UI), S4/human (P6 listening pass) | — |
| A2 | **Positioning:** stop leading with "prompt→plugin" (Amorph owns that press); lead with *self-correcting + safe + measured*. No competitor publishes compile-success or sound-quality numbers — that gap is ours to own once the baseline re-run lands. | overseer/human (positioning), S4 (publish the 3 headline numbers) | — |
| A3 | **Doc housekeeping (needs overseer):** the living doc `docs/competitive_research.md` was renamed to its canonical name `docs/competitive_landscape.md`. Two overseer/fleet-owned files still point at the old name and need updating: **`STATUS.md:111`** and **`docs/.fleet/RESTART.md:14` & `:126`**. S7 already fixed every ref it owns (FLEET rows, `byo_llm_plan.md`); the changelog line inside the landscape doc keeps the old name deliberately as history. | overseer (owns STATUS.md + `.fleet/`) | — |

---

## Reporting protocol (every session)
- **Do NOT rewrite `STATUS.md`.** Emit the COLLABORATION.md §4 five-line report
  (CHANGED / WHY / VERIFIED / RISK / YOUR MOVE) for each landed change; the overseer folds it
  into `STATUS.md`.
- File any bug you won't fix immediately with **S5** (`docs/BUGS.md`) and name it in your report.
- Log a Cross-lane request above instead of reaching into another lane's files.
- If a lane boundary is wrong or slowing you down, say so — this board has no inertia
  (COLLABORATION.md §10 applies to it too).
