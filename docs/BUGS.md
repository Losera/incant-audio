# PluginForge — Bug Registry

**Owned by S5 (Bug tracking). The durable, IDed source of truth for defects.**
Seeded 2026-07-23. Read alongside `STATUS.md`, `docs/FLEET.md`, `COLLABORATION.md`, `CLAUDE.md`.

## Why this file exists
Before this, defects lived only as prose in `STATUS.md`'s "Broken — ranked" and "Assumed,
never checked" sections — which COLLABORATION.md §5 *rewrites* every session. Once a bug dropped
off the top-N list it survived only in git history: no stable ID, no cross-session record, no
way to say "PF-003 is the one we fixed in `d10f59e`." This registry is that record.

## How it relates to STATUS.md
- **BUGS.md is the durable, IDed source.** Every defect gets a permanent `PF-NNN` here and stays
  (as `fixed`/`wontfix`), it is never deleted.
- **STATUS.md "Broken — ranked" is the live top-N view.** The overseer maintains it and should
  reference IDs (e.g. "Broken #1 → PF-002"). S5 does **not** edit STATUS.md — sync happens by
  proposing Broken-section changes to the overseer (FLEET.md reporting protocol).
- IDs are assigned in discovery order and never reused.

## Conventions
- **Severity:** `critical` (product doesn't work / data loss) · `high` · `medium` · `low`.
  Original arch-review P0/P1/P2 grades noted in the detail where they apply.
- **Status:** `open` · `in-progress` (fix in flight) · `fixed` · `wontfix`.
- **Lane:** owning FLEET.md lane responsible for the fix (S5 records; S5 never fixes).
- **File:line** is given at HEAD where meaningful; where a working-tree draft has moved lines,
  both are noted in the detail.

---

## Registry

| ID | Title | Sev | Status | Lane | File:line | Discovered | Closed |
|---|---|---|---|---|---|---|---|
| PF-001 | Parameter values never denormalized — 0–1 slot pushed raw into Faust zones | critical | fixed | S1 Backend | `ParamPool.cpp:75`, `ParamMap.h` | 2026-07-21 | `efbb5a5` |
| PF-002 | No state persistence — saving a DAW session discards the plugin | high | in-progress | S1 Backend | `PluginProcessor.h:31` (HEAD stub) | 2026-07-19 | — |
| PF-003 | Shutdown use-after-free on the detached compile thread | high | fixed | S1 Backend | `FaustEngine.cpp` | 2026-07-21 | `d10f59e` |
| PF-004 | Param path not RT-safe — `fprintf`/`std::map` lookups reachable on audio thread | high | fixed | S1 Backend | `ParamPool.cpp:75` | 2026-07-21 | `efbb5a5` |
| PF-005 | Editor exposes only 8 of 64 params; toggles render as rotaries | medium | open | S3 Plugin UX | `PluginEditor.h:41` | 2026-07-21 | — |
| PF-006 | Shutdown UAF on the editor's detached *generate* thread (raw `&proc`) | high | open | S3 Plugin UX | `PluginEditor.cpp:191,205` | 2026-07-21 | — |
| PF-007 | Benchmark measured a prompt that diverged from production | high | fixed | S1 Backend | `bench/prompts/system_faust.txt` (deleted) | 2026-07-21 | prompt-unify (2026-07-21) |
| PF-008 | No generated plugin has ever been listened to (P6 audible battery unrun) | high | open | S4 Testing | `docs/p6_test_battery.md` | 2026-07-23 | — |
| PF-009 | Every benchmark number on record is void (measured on the deleted prompt) | medium | open | S4 Testing | `bench/results/.prompt_baseline.json` | 2026-07-23 | — |
| PF-010 | Prompt rewrite is unmeasured — verified *correct*, not *better* | medium | open | S4 Testing | `llm/prompts/system_prompt.txt` | 2026-07-23 | — |
| PF-011 | Efficacy pilot generalizes to nothing (N=50, 1 model, 2/5 categories) | medium | open | S4 Testing | `bench/run_efficacy_study.py` | 2026-07-23 | — |
| PF-012 | No cross-model comparison exists (ADR-008 "Under evaluation") | low | open | S4 Testing | `docs/architectural_decisions/` (ADR-008) | 2026-07-23 | — |
| PF-013 | Semantic fidelity unmeasured — `--judge` rubric off by default, never run | medium | open | S4 Testing | `bench/score_efficacy.py` | 2026-07-23 | — |
| PF-014 | No real user prompt has ever been recorded (`generate.py` logs nothing) | low | open | S1 Backend | `llm/generate.py` | 2026-07-23 | — |
| PF-015 | `check_rt_safety.py` scopes only 2 functions; `pushToFaust` (now RT) uncovered | medium | open | S1 Backend | `.claude/hooks/check_rt_safety.py:22` | 2026-07-23 | — |
| PF-016 | CI has never run green with the new prompt steps (5 unchecked Ubuntu-Faust TODOs) | medium | open | S4 Testing | `.github/workflows/test.yml` | 2026-07-23 | — |
| PF-017 | Stray `ParamPool::pushToFaust()` definition in `FaustEngine.cpp` | medium | fixed | S1 Backend | `FaustEngine.cpp` (removed) | 2026-07-16 | pre-history (see detail) |
| PF-018 | `FaustEngine::prepare()` does not re-init a live DSP on sample-rate change | medium | open | S1 Backend | `FaustEngine.cpp:154` | 2026-07-23 | — |

---

## Routing & fix plan (2026-07-23)

**Purpose.** Get every open defect in front of the lane that owns the code, with a concrete fix
shape and a coordination path, so no bug sits unseen between sessions. S5 records and routes; S5
does **not** fix. Routing to another lane happens through the **FLEET.md Cross-lane request log**
(overseer routes and closes) — the rows below are what S5 proposes the overseer append. Awareness
also rides each lane's own change-report loop.

### Coordination hotspot — `PluginEditor.cpp:191`
Two independent items land on the **same call site**, so they must be fixed together, not in two
passes that each re-touch the line:
- **Cross-lane req #4** (S1 → shell/S2): adopt `loadFaustCode(faustCode, promptInput.getText())`
  so DAW-saved sessions persist the originating prompt.
- **PF-006** (this registry): that same call passes a raw `&proc` from a `.detach()`ed thread
  (`:205`) — a shutdown UAF.

**Plan:** whichever lane owns `PluginEditor.cpp` after S3's Wave-0 split adopts req #4 **and**
fixes PF-006 in one change. Until the split lands the file is S3's (shell); the prompt-passing
half may migrate into S2's `PromptPanel`. S5 will keep PF-006 open until re-verified post-split.

### Per-lane routing

**S1 Backend Core** — owns `PluginProcessor.*`, `FaustEngine.*`, `ParamPool.*`, `llm/*`, `tools/*`,
hooks-adjacent tooling.
- **PF-002** (state persistence) — *reconcile, then close.* Implementation is in S1's working tree
  and S1's roll-call row says "format signed off via plan approval," but the FLEET Gate table and
  overseer ruling #3 still read "awaiting human sign-off (§2 trigger-3)," and `HEAD` is still the
  empty stub. **Action: overseer confirms whether the §2 trigger-3 sign-off is real and flips the
  gate; S1 commits.** S5 moves PF-002 → fixed only on a commit SHA + a flipped gate.
- **PF-018** (live-DSP sample-rate re-init) — S1 fix. `prepare()` must re-init the live DSP (or
  mark it stale and trigger the async recompile path) when `sampleRate`/`blockSize` change while a
  DSP is live, not only store the members. Pre-existing, out of the P11 scope S1 flagged. Tier 2
  (audio-thread-adjacent): needs a primary source (`faust/dsp/dsp.h` `instanceInit`/`init`) cited
  by file:line and a test.
- **PF-015** (`check_rt_safety.py` can't follow a call graph; `pushToFaust` unscoped) — S1 fix.
  Extend the hook to also scope `ParamPool::pushToFaust` and refresh its stale docstring (`:9`
  still cites the PF-017 stray def as live). COLLABORATION.md §7 item 1.
- **PF-014** (no user-prompt telemetry in `generate.py`) — S1, low priority; opt-in/privacy is a
  design question, park behind PF-008-class work.

**S3 Plugin UX** — owns `ParamGridPanel.*`, the `PluginEditor` shell + top-level `resized()`.
- **PF-006** (editor generate-thread shutdown UAF) — **S3 fix, high severity, currently on nobody's
  radar** — the arch review folded it into §2.2 and `d10f59e` closed only the compile-thread half.
  Fix shape: `std::atomic<bool> shuttingDown` + a latch the teardown waits on, mirroring PF-003;
  do it in the same edit that adopts req #4 (see hotspot above). **This is the most urgent routing
  action** — propose as a new cross-lane request so S3 sees it before it re-touches the file.
- **PF-005** (8-of-64 knob cap) — S3, already scheduled in FLEET Wave 1 (`ParamGridPanel`
  auto-layout + lift the cap + kind-aware widgets). No new routing; just carries the ID now.

**S4 Testing** — owns `tests/*`, `host/tests/*`, the P6 battery, CI proposals.
- **PF-008** (nothing ever heard) — S4 authors the copy-pasteable listening script; execution
  needs the human's ears. Highest-value verification-debt item — unblocks trusting PF-001.
- **PF-009 / PF-010** (void baseline / prompt unmeasured) — S4 re-runs the benchmark against the
  unified prompt; overwriting `.prompt_baseline.json` is a §2 trigger-1 act needing human
  authorization (FLEET gate). Coupled: one authorized run closes both.
- **PF-011** (efficacy pilot) / **PF-013** (semantic fidelity, `--judge` off) — S4, after the
  baseline is re-established. **PF-016** (CI never green with prompt steps) — S4 proposes; the five
  Ubuntu-Faust `TODO: VERIFY` items are build/dependency-gated (overseer/human).
- **PF-012** (no cross-model comparison, ADR-008) — S4, lowest priority; a study, not a fix.

**Overseer (S6)** — routing + STATUS/gate authority.
- Append the cross-lane request rows below; flip the PF-002 gate once sign-off is confirmed; fold
  the IDs into STATUS.md per the earlier sync proposal.

### Cross-lane request rows S5 proposes the overseer append
| From | Needs | To |
|---|---|---|
| S5 | **PF-006** — editor's generate thread `.detach()`es (`PluginEditor.cpp:205`) and calls `loadFaustCode` through a raw `&proc` (`:191`); the processor can be destroyed while that thread is parked in `waitForProcessToFinish`. High-sev shutdown UAF, distinct from the compile-thread UAF `d10f59e` fixed. **Fix in the same edit that adopts req #4** (same line). | S3 (shell owner after split) |
| S5 | **PF-018** — filing per S1 req #5: `FaustEngine::prepare()` (`FaustEngine.cpp:154-158`) stores `sr`/`block` but never re-inits a live DSP, so a host sample-rate change after a patch is live leaves the DSP at the old rate. Now PF-018. | S1 (ack; fix when in scope) |
| S5 | **PF-002 reconcile** — roll-call says "signed off," Gate table + ruling #3 say "awaiting." Please confirm the §2 trigger-3 status and flip the gate, or correct the roll-call row, so S5 can close or hold accurately. | Overseer |

---

## Open — detail

### PF-002 — No state persistence. Saving a DAW session discards the plugin.
**high · in-progress · S1 Backend · was arch-review §2.3 (P1), STATUS Broken #1**
At HEAD, `PluginProcessor.h:31` is an empty stub:
```cpp
void getStateInformation(juce::MemoryBlock&) override {}
void setStateInformation(const void*, int) override {}
```
Reopening a saved project restores 64 macro slots to defaults with no DSP loaded and no way to
recover the generated patch — the Faust source exists nowhere but the JIT'd factory and the
user's memory. For a plugin whose entire value is a generated artifact, this is **data loss**,
not a missing feature.
**In flight:** S1 has an uncommitted working-tree draft — `PluginProcessor.{h,cpp}` now
serialize a versioned `ValueTree→XML` blob (Faust source + originating prompt + 64 APVTS values +
slot-label map), and `host/tests/StatePersistenceTest.cpp` exists. Not yet committed and **not
landed**: the persisted-state **format is a COLLABORATION.md §2 trigger-3 contract** that needs
human sign-off before it ships (FLEET.md gate: "Persisted-state format signed off"). Moves to
`fixed` when the format is signed off and the change commits.
**Blocks:** the embedded code editor and iterate/refine (S2 Wave-1 work).

### PF-005 — Editor exposes only 8 of 64 parameters.
**medium · open · S3 Plugin UX · was arch-review §2.5 (P2), STATUS Broken #2**
`MAX_KNOBS = 8` (`PluginEditor.h:41`) against `POOL_SIZE = 64`. The value-loss half is already
fixed by PF-001 (all 64 slots push to Faust), but patches with >8 controls have **no on-screen
control** for the remainder (`refreshParamKnobs` clamps with `jmin` and moves on), and
toggle-kind params render as rotaries. Deterministic auto-layout is specified in
`docs/ui_design_plan.md` §3; `FaustEngine.h` already exposes the per-param `Kind` enum. S3 owns
this in Wave 1 (`ParamGridPanel` — draft files already on disk, uncommitted).

### PF-006 — Shutdown use-after-free on the editor's detached *generate* thread.
**high · open · S3 Plugin UX · was arch-review §2.2 (P1), second half**
PF-003 fixed the FaustEngine *compile* thread (`d10f59e`). The **editor's generate thread** is a
separate, still-open instance of the same bug: `PluginEditor.cpp:94` launches a `std::thread`
that is `.detach()`ed (`:205`) and calls `proc.loadFaustCode(faustCode)` (`:191`) through a
**raw `&proc` reference**. `SafePointer` correctly guards the *editor*, but the detached thread
can be parked in a 120 s `waitForProcessToFinish` when the DAW tears the whole plugin down — so
the *processor* can be destroyed out from under the raw reference.
**Fix shape (from review):** an `std::atomic<bool> shuttingDown` + latch the teardown waits on,
mirroring PF-003. Routed to S3 as owner of the `PluginEditor` shell; coordinate with S1 if
`loadFaustCode`'s signature is involved. **Verify** this is still live after S3's Wave-0 editor
split lands (the code may move to a panel).
**Collision with cross-lane req #4:** req #4 asks the editor to adopt
`loadFaustCode(faustCode, promptInput.getText())` at *this same line* (`:191`). Fix both in one
edit — see the "Coordination hotspot — `PluginEditor.cpp:191`" note in the Routing & fix plan.

### PF-008 — No generated plugin has ever been listened to.
**high · open · S4 Testing · STATUS "Assumed, never checked"**
The PF-001 denormalization fix is verified by unit test and by construction, **not by ear**. The
P6 audible battery (`docs/prototype_test_plan.md` Part A, `docs/p6_test_battery.md`) has never
run. This is the fastest way to find whatever the old denormalization bug was masking; the review
predicted "it will fail on the first patch" before PF-001. Needs the human's ears (use `groq`,
not Gemini's ~20/day quota).

### PF-009 — Every benchmark number on record is void.
**medium · open · S4 Testing · STATUS "Assumed, never checked"**
All recorded numbers were measured against the deleted `bench/prompts/system_faust.txt`, which
taught three functions that do not exist. `bench/results/.prompt_baseline.json` (0.88) has **not**
been overwritten (gated — overwriting it is a §2 trigger-1 act) but describes nothing that
currently exists. Closes when a benchmark is re-run against the unified prompt (needs human
authorization — FLEET.md gate).

### PF-010 — The prompt rewrite is unmeasured.
**medium · open · S4 Testing · STATUS "Assumed, never checked"**
`llm/prompts/system_prompt.txt` is verified *correct* (every `ns.func` resolves, all five
few-shot examples compile — `tests/test_prompt_stdlib.py`, `check_prompt_invariants.py`), not
verified *better*. Same closing condition as PF-009.

### PF-011 — The efficacy pilot generalizes to nothing.
**medium · open · S4 Testing · STATUS "Assumed, never checked"**
N=50, one model, the old prompt, two of five categories. The full 125-prompt run (P9) has never
produced valid data — the 2026-07-20 attempt was rejected pre-generation for insufficient
Anthropic credit (0 tokens spent). Re-run on a free provider once billing/quota allows.

### PF-012 — No cross-model comparison exists.
**low · open · S4 Testing · STATUS "Assumed, never checked"**
ADR-008 has been "Under evaluation" since 2026-04-29. No data comparing model choices.

### PF-013 — Semantic fidelity is unmeasured.
**medium · open · S4 Testing · STATUS "Assumed, never checked"**
Every metric on record is compile rate. `bench/score_efficacy.py`'s `--judge` rubric — the only
fidelity signal in the project — is **off by default** and has never run. Compile-rate says the
Faust builds, not that it does what the words asked.

### PF-014 — No real user prompt has ever been recorded.
**low · open · S1 Backend · STATUS "Assumed, never checked"**
`llm/generate.py` logs nothing. There is no corpus of real prompts to measure against or to seed
a generation cache. (Privacy/opt-in is a design question, not just a code change.)

### PF-015 — `check_rt_safety.py` cannot follow a call graph.
**medium · open · S1 Backend · STATUS "Assumed" + COLLABORATION.md §7**
The hook scopes exactly two named functions (`FaustEngine::process`, `processBlock`) by brace
counting and cannot follow a call graph (its own documented KNOWN LIMITATION,
`check_rt_safety.py:22`). `ParamPool::pushToFaust` — now on the audio thread and reachable from
`processBlock` — is **not** scoped. COLLABORATION.md §7 item 1 flags this as load-bearing: at
minimum the hook should also scope `pushToFaust` and anything else reachable from `processBlock`.
**Residual doc-debt (fold in here):** the hook's docstring (`:9`) still cites a stray
`ParamPool::pushToFaust()` in `FaustEngine.cpp` as a live "separately-tracked bug" — that stray
def is PF-017 and was removed; the docstring reference is now stale.

### PF-016 — CI has never run green with the new prompt steps.
**medium · open · S4 Testing · STATUS "Assumed, never checked"**
The `build-host` job runs `tools/gen_stdlib_block.py --check` and the prompt tests, and carries
five pre-existing `TODO: VERIFY` items about Ubuntu Faust packaging — none checkable from the
Arch dev box. Green on CI has never been observed with these steps in place.

### PF-018 — `FaustEngine::prepare()` does not re-init a live DSP on sample-rate change.
**medium · open · S1 Backend · filed 2026-07-23 per S1 cross-lane req #5**
`FaustEngine::prepare(double sampleRate, int blockSize)` (`FaustEngine.cpp:154-158`) assigns
`sr = sampleRate; block = blockSize;` and returns — it never re-inits an already-live DSP. If the
host changes sample rate *after* a patch is live, the DSP keeps running at the rate it was
`instanceInit`'d with (wrong pitch/timing for anything rate-dependent) until the next recompile.
Pre-existing and out of the P11 state-persistence scope S1 flagged: S1's deferred restore-recompile
avoids *creating* a wrong-SR DSP but does not fix a rate *change* on a live one.
**Fix shape:** on a changed `sampleRate`/`blockSize` with a DSP live, re-init it (Faust
`instanceInit`/`instanceConstants`, `faust/dsp/dsp.h`) or mark it stale and drive the existing
async recompile path — off the audio thread, respecting the swap protocol. Tier 2: cite the Faust
init API by file:line and add a test. Routed to S1.

---

## Closed — archive

### PF-001 — Parameter values are never denormalized. *(fixed `efbb5a5`, 2026-07-21)*
**critical · was arch-review §2.1 (P0), STATUS old Broken #1/#4**
All 64 slots were created 0–1 and `pushToFaust` wrote the raw 0–1 value into Faust zones with
real-world ranges (`MapUI::setParamValue` does `*zone = value`, no clamp, no mapping —
`/usr/include/faust/gui/MapUI.h:150-171`). A `hslider("Cutoff",1000,20,20000,1)` received 0.0–1.0,
pinning cutoff under 1 Hz regardless of knob position — every filter/delay/dB/frequency patch
inaudible or wrong. Fixed by `efbb5a5` ("Denormalize macro slots into Faust zones"): new
`ParamMap.h` (180 lines) converts slot 0–1 ↔ Faust zone (Hz/dB/ms) with log/exp/linear curves and
discrete/menu quantization; `ParamCapture` records `scale`/`unit`/`isMenu`/`min`/`max`/`step`/`zone`
per param. Covered by `host/tests/ParamMapTest.cpp`. **Not yet verified by ear → PF-008.**

### PF-003 — Shutdown use-after-free on the detached compile thread. *(fixed `d10f59e`, 2026-07-22)*
**high · was arch-review §2.2 (P1), STATUS old Broken #3**
`FaustEngine::compile()` launched `std::thread(...).detach()` capturing `this`, with no join /
shutdown flag / wait; `~FaustEngine()` freed `activeDSP` and the factory. Unloading the plugin
mid-JIT (a tens-to-hundreds-of-ms LLVM window) left the detached thread touching freed member
state and calling a lambda holding a freed `PluginForgeProcessor*`. Fixed by `d10f59e` ("Own the
compile thread: persistent worker, joined on shutdown"): `FaustEngine` now runs a persistent
`workerLoop`/`shutdown`; `~PluginForgeProcessor` calls `faustEngine.shutdown()` first. **Note:**
the *editor's generate thread* is a separate instance of this bug and is still open → **PF-006**.

### PF-004 — Param path not RT-safe (`fprintf` / `std::map` reachable on audio thread). *(fixed `efbb5a5`, 2026-07-21)*
**high · was arch-review §2.5 first bullet (P2), folded into STATUS old Broken #4**
`MapUI::setParamValue` (`MapUI.h:170`) calls `fprintf(stderr,...)` on a label miss — a lock +
syscall inside `processBlock` — and did 64 `std::map<std::string>` lookups per block. Fixed by
`efbb5a5`: `pushToFaust` now writes cached `FAUSTFLOAT*` zone pointers directly — no `std::map`
lookups, no `MapUI`, no `fprintf` on the audio thread. **Coverage caveat:** the hook that should
guard this can't see `pushToFaust` → **PF-015**.

### PF-007 — Benchmark measured a prompt that diverged from production. *(fixed by prompt unification, 2026-07-21)*
**high · was arch-review §2.4 (P1)**
`llm/prompts/system_prompt.txt` and `bench/prompts/system_faust.txt` had drifted substantially
(a 16-line stdlib-highlights block, extra stereo-wiring rules, a different example set), and
`check_adr009_prompt_sync.py` verified only one sentence — so it never caught the drift. Every
benchmark number was measured on a prompt materially different from what the plugin shipped.
Resolved by unifying to **one** prompt file (`llm/prompts/system_prompt.txt`); the stdlib section
is now generated from `/usr/share/faust/*.lib` by `tools/gen_stdlib_block.py`, and
`bench/prompts/system_faust.txt` is **deleted** (confirmed absent 2026-07-23). Consequence: the
old baseline is now void → **PF-009**.

### PF-017 — Stray `ParamPool::pushToFaust()` definition in `FaustEngine.cpp`. *(fixed, removed 2026-07-16)*
**medium · referenced by `check_rt_safety.py:9` and arch review**
A non-compiling stray `ParamPool::pushToFaust()` fragment (and a fabricated `UI::failSafe()`
override — no such method exists in `faust/gui/UI.h`) sat in `FaustEngine.cpp`. Per CLAUDE.md it
was removed 2026-07-16 and the file compiles clean; confirmed absent by grep 2026-07-23. Predates
the current git history (base commit `23d16dc`), so there is no closing SHA in this repo — closed
by documented removal. **Residual:** `check_rt_safety.py:9` still describes it as a live
"separately-tracked bug" — stale docstring, tracked as a fold-in under **PF-015**.
