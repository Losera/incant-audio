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
| PF-002 | No state persistence — saving a DAW session discards the plugin | high | fixed | S1 Backend | `PluginProcessor.cpp:197,229` | 2026-07-19 | `c34bbb6` (2026-07-23) |
| PF-003 | Shutdown use-after-free on the detached compile thread | high | fixed | S1 Backend | `FaustEngine.cpp` | 2026-07-21 | `d10f59e` |
| PF-004 | Param path not RT-safe — `fprintf`/`std::map` lookups reachable on audio thread | high | fixed | S1 Backend | `ParamPool.cpp:75` | 2026-07-21 | `efbb5a5` |
| PF-005 | Editor exposes only 8 of 64 params; toggles render as rotaries | medium | fixed | S3 Plugin UX | `ParamGridPanel.cpp:25` | 2026-07-21 | `2e129cd` (2026-07-23) |
| PF-006 | Shutdown UAF on the editor's detached *generate* thread (raw `&proc`) | high | open | S2 Prompting UX | `PromptPanel.cpp:284,300` | 2026-07-21 | — |
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
| PF-019 | Generation timeout cliff — 120s frozen UI under sustained groq use; one stalled/429'd POST eats the whole retry budget | high | open | S1 Backend | `llm/providers.py:50`, `llm/generate.py:99,125` | 2026-07-24 | — |
| PF-020 | Cross-generation state contamination — no fresh/iterate mode; old APVTS values leak into new patches by slot index; headless never seeds defaults | high | open | S1 Backend / S2 UX | `ParamPool.cpp:94`, `PluginProcessor.cpp:114`, `ParamGridPanel.cpp:31` | 2026-07-24 | — |
| PF-021 | Stale error persists in PromptPanel across a new Generate (never cleared on submit) | medium | open | S2 Prompting UX | `PromptPanel.cpp:309,124,319` | 2026-07-24 | — |
| PF-022 | `currentFaustSource`/`currentPrompt` committed before compile success — a failed generate poisons the source-of-record and any later save/restore | high | open | S1 Backend | `PluginProcessor.cpp:114-118` | 2026-07-24 | — |
| PF-023 | `FaustEngine::process()` has no `activeDSP` null guard (latent audio-thread segfault; defense-in-depth) | medium | open | S1 Backend | `FaustEngine.cpp:178-183` | 2026-07-24 | — |
| PF-024 | Generation produces invalid Faust for stereo routing / unbounded delays / ping-pong / artist-reference prompts (P6 #2,#6,#9,#10) | high | open | S1 Backend | `llm/prompts/system_prompt.txt` | 2026-07-24 | — |

---

## Routing & fix plan (2026-07-23)

**Purpose.** Get every open defect in front of the lane that owns the code, with a concrete fix
shape and a coordination path, so no bug sits unseen between sessions. S5 records and routes; S5
does **not** fix. Routing to another lane happens through the **FLEET.md Cross-lane request log**
(overseer routes and closes) — the rows below are what S5 proposes the overseer append. Awareness
also rides each lane's own change-report loop.

### Coordination hotspot — RESOLVED (was `PluginEditor.cpp:191`)
The Task-0 split (`471d045`) landed: req #4 (`loadFaustCode(prompt)`) is **done**, and the generate
thread + call site moved into `PromptPanel.cpp` (`:284`/`:300`). So the former req-#4 ⇄ PF-006
call-site collision no longer exists — the two are now separable. PF-006 remains open in its new
home; ownership is being settled between S3 (req #10) and S2 (req #16). See the PF-006 detail.

### Per-lane routing

**S1 Backend Core** — owns `PluginProcessor.*`, `FaustEngine.*`, `ParamPool.*`, `llm/*`, `tools/*`,
hooks-adjacent tooling.
- **PF-002** (state persistence) — **FIXED `c34bbb6`**, moved to Closed archive. Committed + tested
  (StatePersistenceTest 13/13, ASan/UBSan clean). One residual *process* item, not a defect: the
  §2 trigger-3 format still awaits human confirmation (S1 reports plan-mode sign-off). Reopen only
  if the human rejects the format.
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

**S3 Plugin UX / S2 Prompting UX** — `PluginEditor` shell (S3) and `PromptPanel.*` (S2).
- **PF-006** (editor generate-thread shutdown UAF) — high severity, **still open**, now flagged
  in-code (`dc3d423` `TODO: VERIFY` marker at `PromptPanel.cpp:164`). Post-split the thread lives
  in S2's `PromptPanel.cpp`. **Owner: S2** (overseer ruling req #16, `d442dd2`), folded into the
  PromptPanel Tier-2 rework: owned+joined worker + `std::atomic<bool>` abort + `child.kill()`,
  mirroring PF-003/`d10f59e`. S5 closes on that commit.
- **PF-005** (8-of-64 knob cap) — **FIXED `2e129cd`** (S3 Wave-1 auto-layout), moved to Closed
  archive. Build green at HEAD; runtime/eye check still worth doing (advisory A1).

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

### Cross-lane request rows S5 posted (status as of 2026-07-23)
| # | Bug | To | Status |
|---|---|---|---|
| #10 | **PF-006** shutdown UAF | **S2** (ruling `d442dd2`, req #16) | open — owner resolved to S2; folded into PromptPanel Tier-2 rework. Relocated to `PromptPanel.cpp:284,300`, flagged (`dc3d423`). |
| #11 | **PF-018** live-DSP SR re-init | S1 | open — S1 acked; fix when in scope. (Req #5 can be closed by overseer.) |
| #12 | **PF-002** reconcile | Overseer | **resolved** — landed `c34bbb6`; PF-002 → fixed. Only the §2 format sign-off (human) remains, tracked in STATUS "Waiting on you". |

---

## Open — detail

### PF-006 — Shutdown use-after-free on the editor's detached *generate* thread.
**high · open · owner S2 Prompting UX · was arch-review §2.2 (P1), second half**
PF-003 fixed the FaustEngine *compile* thread (`d10f59e`). The **editor's generate thread** is a
separate, still-open instance of the same bug: a `std::thread` is `.detach()`ed and calls
`proc.loadFaustCode(...)` through a **raw `&proc` reference**. `SafePointer` correctly guards the
*editor*, but the detached thread can be parked in a 120 s `waitForProcessToFinish` when the DAW
tears the whole plugin down — so the *processor* can be destroyed out from under the raw reference.
**Relocated by the Task-0 split (`471d045`).** The thread now lives in `PromptPanel.cpp`:
`.detach()` at `:300`, raw `&proc` → `loadFaustCode(faustCode, juce::String(prompt))` at `:284`.
Re-verified still live 2026-07-23. `dc3d423` replaced the old false "capture is safe" comment with
an honest `// TODO: VERIFY: PF-006` marker at `PromptPanel.cpp:164` (marker only, no behaviour
change) — so the defect is flagged in-code, not silently carried.
**Fix shape (from review):** owned+joined worker + `std::atomic<bool>` abort + `child.kill()`,
mirroring PF-003 — a dedicated Tier-2 change, deferred out of the zero-behaviour split.
**2026-07-24 evidence — this bug now has a live repro.** During the first P6 listening battery,
rapid successive Generate clicks on failing prompts produced a **`Segmentation fault (core
dumped)`** (P6 #7, attempt 1). The FaustEngine failed-compile path early-returns clean
(`FaustEngine.cpp:238-267`), so the crash is consistent with N unbounded detached generate
threads piling up (no supersede) and one calling `loadFaustCode` through a stale `&proc`. The
crash was transient (did not recur when the same prompt ran alone), matching a race, not a
deterministic fault. This raises PF-006 to the burst's crash priority.
**Owner resolved → S2** (overseer ruling on req #16, `d442dd2`): the thread now sits in S2's
`PromptPanel.cpp`, so S2 folds the fix into its Wave-1 PromptPanel threading rework as one Tier-2
change — persistent owned worker (cite `d10f59e`/PF-003 as the pattern), atomic abort, bounded
join on teardown, testing the join+abort and stating what shutdown timing was not exercised. S5
closes on that commit. Cross-lane req #4 (`loadFaustCode(prompt)`) is **done** (`471d045`), so the
former call-site collision is gone.

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

### PF-019 — Generation timeout cliff (120s frozen UI under sustained use).
**high · open · S1 Backend · found in the 2026-07-24 P6 battery**
P6 prompts **#11–#14 failed in a consecutive run**, each with "LLM subprocess timed out after
120s and was killed" (`PromptPanel.cpp:215`). Root cause is a budget collision: `generate.py`
makes up to 3 full-regeneration LLM calls (`generate.py:99,125`), each able to fan into ≤5
provider backoff tries (`providers.py:51`), and the httpx per-POST timeout is **120s**
(`providers.py:50`) — identical to the C++ subprocess cap (`PromptPanel.cpp:208`). So one
stalled or `429`'d groq POST consumes the entire budget and the outer wait kills the run before
any retry completes. Sustained use drives groq into rate-limit state; `_retry_after_seconds`
sleeps (`providers.py:356-363`) then burn the remaining budget. No default request pacing
(`PLUGINFORGE_MIN_INTERVAL=0`, `providers.py:342-353`). **#14 is the worst instance** — it is the
robustness test that must *never* hang, and it hung. **Fix shape (Tier 2):** per-attempt LLM
budget so 3 attempts fit inside 120s (lower `_HTTP_TIMEOUT` and/or pass a per-attempt timeout);
cap cumulative backoff; return a *typed* `rate_limited` vs `timeout` reason in the ADR-011 JSON;
add light default pacing. Cite file:line, add a test, state what wasn't verified.

### PF-020 — Cross-generation state contamination; no fresh/iterate mode.
**high · open · S1 Backend (state) + S2 UX (affordance) · found 2026-07-24**
The human observed generated plugins "reiterating on each other rather than starting fresh," and
flaky P6 results (#4/#5/#7 failed/crashed under a contaminated session, passed clean). Root
cause: **no fresh-vs-iterate concept exists anywhere.** APVTS macro values are reset *only* by
UI-layer seeding (`ParamGridPanel.cpp:31-41` via `PluginEditor.cpp:53-69`), so with the editor
closed/headless the previous patch's values persist and `pushToFaust` drives the *new* patch's
zones with the *old* values by slot index (`ParamPool.cpp:94-96`) — a knob labeled "Cutoff" in
patch A silently drives "Feedback" in patch B. The "fresh" behavior today is an accident of
whether the editor is open, not a chosen mode. **Fix shape:** add `LoadMode {Fresh, Iterate}` to
`loadFaustCode`; **Fresh** resets mapped macros to patch defaults *in the processor* (S1);
**Iterate** preserves. S2 adds the UI affordance (New plugin vs Refine). Cross-lane contract —
see FLEET.md 2026-07-24.

### PF-021 — Stale error persists across a new Generate.
**medium · open · S2 Prompting UX · reported by the human 2026-07-24**
`setError()` (`PromptPanel.cpp:309-316`) writes `errorBox` and its own comment notes the error is
"retained across a later success." Neither `submitPrompt()` (`:124-144`) nor `startWorking()`
(`:319-326`) ever clears `errorBox` on a new run, so a previous failure's text stays on screen
through the next generation — including a successful one — and reads as the current result. **Fix
shape:** clear/hide `errorBox` on submit, or prefix each error with its attempt/timestamp so it
is never mistaken for the live run. Human explicitly asked for this.

### PF-022 — Source-of-record committed before compile success.
**high · open · S1 Backend · found 2026-07-24**
`loadFaustCode` sets `currentFaustSource`/`currentPrompt` **unconditionally at
`PluginProcessor.cpp:114-118`, before the compile is even queued.** A failed compile therefore
overwrites the retained source-of-record with non-compiling code while `activeDSP`,
`currentLabels`, and the APVTS values still belong to the *previous* successful patch. A DAW save
in that window (or after any failed generate) persists a broken-source / old-labels / old-values
triple; on reload the restore-recompile fails and no DSP goes live. **Fix shape:** commit
`currentFaustSource`/`currentPrompt` only on compile **success**. Interacts with state
persistence (`getStateInformation` `:206-236`).

### PF-023 — `process()` has no `activeDSP` null guard.
**medium · open · S1 Backend · found 2026-07-24 (defense-in-depth)**
`FaustEngine::process()` (`FaustEngine.cpp:178-183`) loads `activeDSP` and calls
`dsp->compute(...)` relying *solely* on the `ready` flag to guarantee non-null. The invariant
(`ready==true ⟹ activeDSP!=null`) holds in current code, so this is latent, not a live crash —
but there is zero defense if the swap ordering is ever broken, and the audio thread would
segfault. **Fix shape:** null-check `activeDSP` in `process()` and passthrough if null.

### PF-024 — Generation produces invalid Faust for whole prompt classes.
**high · open · S1 Backend · found in the 2026-07-24 P6 battery**
Consistent generation failures the prompt grounding does not prevent: **#2 ping-pong delay** →
`endless evaluation cycle` (semantic, a known landmine since 2026-05); **#6 cold/glassy** →
non-deterministically `2 outputs must equal 1 input` (stereo→mono routing) or `invalid delay
parameter range: interval(0,2.1e9,0)` (unbounded delay); **#9 Loveless** → `syntax error,
unexpected IDENT`; **#10 RE-201** → `syntax error, unexpected WITH`. **Fix shape (Tier 2):** add
few-shots / rules to `llm/prompts/system_prompt.txt` for stereo-in→stereo-out routing, bounded
delay lengths, and a correct ping-pong pattern; keep every `ns.func` resolving (hook-enforced).
Re-run the benchmark or declare the baseline stale (does not by itself overwrite
`.prompt_baseline.json` — that stays §2 trigger-1 gated).

---

## Closed — archive

### PF-005 — Editor exposes only 8 of 64 parameters. *(fixed `2e129cd`, 2026-07-23)*
**medium · was arch-review §2.5 (P2), STATUS Broken #1 (top-ranked live defect until closed)**
`MAX_KNOBS = 8` against `POOL_SIZE = 64`: patches with >8 controls had no on-screen control for the
remainder, and toggle-kind params rendered as rotaries. Fixed by `2e129cd` ("Wave-1 ParamGridPanel
auto-layout: kind-aware, N-aware, scrollable; dynamic window height"): `ParamGridPanel` now shows
**all** mapped params up to `ParamPool::POOL_SIZE` (64) — `remap()` caps at
`jmin(params.size(), POOL_SIZE)` (`ParamGridPanel.cpp:25`), no `MAX_KNOBS`; a deterministic grid
(`cols≈sqrt(N)`, 2–6) on a `controls`/`viewport`/`content` scrolled surface replaces the fixed
8-slider array, with kind-aware widgets and dynamic window height.
**How we know:** `MAX_KNOBS` grep-clean across `host/Source/` (only a "no MAX_KNOBS cap" comment
remains); committed tree is self-consistent (0 stale `paramSliders`/`numVisibleKnobs` refs), which
resolved the req-#20 build break; incremental `cmake --build host/build --target PluginForgeHost`
at HEAD → `ninja: no work to do`, exit 0 (S3 gated on green before committing).
**Not verified:** no from-scratch rebuild by S5; **not confirmed by eye/runtime** — a live patch
with >8 params (and a toggle) has not been visually confirmed to render the grid + correct widget
kinds. Competitive note (advisory A1): auto-UI parity is now table stakes, so a P6-style visual
check is worth it. Reopen if the runtime layout misbehaves.

### PF-002 — No state persistence. Saving a DAW session discards the plugin. *(fixed `c34bbb6`, 2026-07-23)*
**high · was arch-review §2.3 (P1), STATUS old Broken #1**
At HEAD before the fix, `getStateInformation`/`setStateInformation` were empty stubs — reopening a
saved project restored 64 macro slots to defaults with no DSP and no way to recover the generated
patch (the Faust source existed nowhere but the JIT'd factory and the user's memory). Data loss for
a plugin whose whole value is a generated artifact. Fixed by `c34bbb6` ("Implement state
persistence (P11)"): `PluginProcessor.cpp:197/229` now serialize a versioned `ValueTree→XML` blob
(schemaVersion=1: Faust source + originating prompt + 64 APVTS values + slot-label map); setState
restores values then triggers an async recompile; unknown/corrupt/foreign blobs are ignored.
Retained metadata is `metaMutex`-guarded, never touched on the audio thread. Covered by
`host/tests/StatePersistenceTest.cpp` (round-trips through two processors 13/13, ASan/UBSan clean);
JUCE headers cited (`juce_AudioProcessorValueTreeState.h:375-395`, `juce_AudioProcessor.h:1306-1312`).
**Residual (not a defect — a process gate):** the persisted-state **format is a §2 trigger-3
contract**. S1 reports it was signed off via plan-mode approval; the overseer could not
independently see that, so **human confirmation is still pending** (STATUS "Waiting on you"). The
blob is versioned/forward-defensive, so amending v1 while it is the only blob in the wild is cheap.
Closed on the committed, tested code; reopen only if the human rejects the format.

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
