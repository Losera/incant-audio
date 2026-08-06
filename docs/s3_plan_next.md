# S3 (Plugin UX) — plan for the next work session

Status: **historical archive.** Written 2026-07-23 under the retired S-lane / FLEET
apparatus (`c58a281`). Since written: **§2 (auto-layout) has been executed** — the
8-knob cap is gone, `ParamGridPanel` now renders all 64 pool slots in a scrollable
Viewport with kind-aware slider/toggle widgets (`ParamGridPanel.cpp`,
`PluginEditor.cpp:256-262`). **§1 (PF-006) was reassigned to S2** and the owned-thread
+ cooperative-abort design it retained is what landed in `PromptPanel.cpp`. **§3–4 are
still open** (BYO-LLM mode affordance; LLM layout-hint post-pass) and remain relevant
to Track 2/3 toward alpha — that is why this file is kept. Treat the lane framing below
as historical; the file-area ownership still holds.

Ordering as written: **1 (PF-006) first** — small, high-severity, self-contained. Then **2
(auto-layout)** — the main Wave-1 feature. Then **3** and **4** (follow-ons).

---

## 1. PF-006 — fix the generate-thread shutdown UAF  *(REASSIGNED TO S2 — 2026-07-23)*

> **Handed off.** The detached generate thread now lives in S2's `PromptPanel.cpp`,
> and S2 is rewriting it for Wave 1, so per FLEET req #16 S2 owns this fix (folded
> into their PromptPanel rework). S3 no longer executes it. The design below is
> retained as the agreed approach for whoever implements it.

**Defect.** `PromptPanel`'s Generate handler launches `std::thread(...).detach()`
capturing a raw `&proc` (`host/Source/PromptPanel.cpp`, the `std::thread([...&proc]...)`
lambda). It blocks up to 120 s in `child.waitForProcessToFinish(120*1000)` and then
calls `proc.loadFaustCode(...)`. If the processor is destroyed during that window,
that call is a use-after-free. The `SafePointer<PromptPanel>` only guards *UI*
touches inside `callAsync`; it does **not** guard `proc.loadFaustCode`. Tracked as
PF-006 (`docs/BUGS.md`, FLEET req #10). A `TODO: VERIFY` marker already sits at the
call site (`dc3d423`); this section removes it by making the construct actually safe.
Mirror the already-fixed compile-thread UAF PF-003 (`d10f59e`).

**Design — own + join the worker with a cooperative abort.**

New `PromptPanel` members (`PromptPanel.h`):
```cpp
std::thread        worker;            // the generate thread, owned (not detached)
std::atomic<bool>  aborting { false };
```

Generate handler (`PromptPanel.cpp`):
- Before launching: `if (worker.joinable()) worker.join();` — the prior run has
  already posted its result and returned (the button is disabled for the duration,
  so at most one prior worker exists). Join is effectively instant.
- Assign the thread to `worker` instead of `.detach()`.
- Replace the single blocking `waitForProcessToFinish(120*1000)` with a bounded,
  abortable poll (APIs verified: `juce::ChildProcess::isRunning()`
  `juce_ChildProcess.h:79`; `juce::Thread::sleep(int)` `juce_Thread.h:445`):
  ```cpp
  const int timeoutMs = 120 * 1000, step = 50; int waited = 0;
  while (child.isRunning() && waited < timeoutMs && !aborting.load(std::memory_order_acquire)) {
      juce::Thread::sleep(step); waited += step;
  }
  if (aborting.load(std::memory_order_acquire)) { child.kill(); return; } // panel dying: NO UI, NO proc touch
  if (child.isRunning()) { child.kill(); /* -> existing 120s-timeout callAsync path */ }
  ```

Destructor `~PromptPanel()`:
```cpp
aborting.store(true, std::memory_order_release);
if (worker.joinable()) worker.join();
```
Runs on the message thread, **before** the processor is destroyed (JUCE destroys the
editor — hence this child panel — before the processor). Once `aborting` is set the
worker kills the child and returns within ≤ `step` ms, so the join cannot hang for
120 s. After the join no thread holds `&proc` → UAF closed. The existing
`onFaustCompileError/Success` handlers on the shell are unaffected.

**SUBTLE to encode in the code (not dilute):**
- The abort path must touch **neither** `safeThis->` UI **nor** `proc.loadFaustCode`
  — once `aborting` is observed the thread does only `child.kill(); return;`.
- `worker` as a single member enforces one-generation-at-a-time; it relies on the
  button-disable already in place. If Generate is ever made re-entrant, this needs a
  pool, not a single member.

**Verify (Tier 2):**
- Build: Standalone + VST3 + `ParamPoolTsanTest` + `StatePersistenceTest` clean.
- The race itself is **not** reliably unit-testable (COLLABORATION.md §3 allows
  naming the manual check instead). Manual/ASan check to script for the human:
  point `PLUGINFORGE_LLM_SCRIPT` at a `sleep 60` stub, click Generate, close the
  window mid-run; expect a clean exit with no UAF under an ASan build. Consider
  asking **S4** to add a host/tests harness that constructs + destroys the editor
  mid-generate under ASan.
- Report ends with the explicit unverified remainder (the live-DAW teardown path).

---

## 2. Wave-1 — deterministic auto-layout in `ParamGridPanel`  *(Tier 2)*

Per `docs/ui_design_plan.md §3`. Replaces the fixed 8-knob `std::array` grid with an
N-aware, kind-aware layout and lifts the `MAX_KNOBS = 8` cap (STATUS.md Broken #5).
All message-thread; no audio/compile-thread interaction. Keep the per-slot
`SliderAttachment` pattern, generalized to buttons.

**Data model.** Replace the three fixed `std::array<…, 8>` members with parallel
`std::vector`s (or one small owning `struct Control`) sized to the live param count:
- a widget that is a `juce::Slider` **or** a `juce::ToggleButton`, chosen by
  `FaustEngine::Kind` (`FaustEngine.h:23-30`): `Button`/`CheckButton` → `ToggleButton`
  (must **not** render as a rotary); everything else → rotary `Slider`.
- a `juce::Label`.
- an attachment: `SliderAttachment` or `ButtonAttachment`
  (`juce_AudioProcessorValueTreeState.h:587`, ctor `(state, paramID, button)`).

**`refreshParamKnobs(params)` rewrite:**
- Keep the full-pool seeding loop unchanged (seed all `numMapped` slots via
  `ParamMap::mapZoneToSlot`, `POOL_SIZE=64`).
- Rebuild controls for `i ∈ [0, numMapped)`: **destroy old attachments first**, then
  old widgets, then build new ones bound to `ParamPool::slotId(i)`. (Attachment
  lifetime is load-bearing: an attachment outliving its widget/param is a dangling
  bind — clear attachments before widgets.)
- No 8-cap: `numMapped = min(params.size(), POOL_SIZE)`.

**Layout math (`resized()`):**
- `cols = clamp(ceil(sqrt(N)), 2, 6); rows = ceil(N / cols);`
- `cellW = width / cols; cellH ≈ 95` (label 16 + knob).

**Dynamic window height (shell-owned — coordinate with the shell):**
- Window sizing is S3's own top-level `resized()` lane. After
  `paramGridPanel.refreshParamKnobs(params)` in the shell's `onFaustCompileSuccess`,
  ask the grid for its content height (add `int contentHeight(int width) const` to
  `ParamGridPanel`) and `setSize(getWidth(), header + contentHeight)` where
  `header ≈ 170` (title/prompt/status/meter bands). Update `setResizeLimits` min
  height accordingly.
- Band model from `ui_design_plan §3`: `height = header(~170) + rows × cellH`.

**Overflow (N > ~24):** wrap the grid in a `juce::Viewport` with a scrollable inner
content component (simplest first pass), or a `TabbedComponent` for sectioning
later. Start with the Viewport; tabs need grouping metadata we don't capture yet.

**Verify (Tier 2):** cite the widget/attachment headers at `file:line`; build all
editor targets; drive a synthetic `ParamList` of N = 1, 2, 12, 24, 40, 64 with mixed
kinds and confirm cols/rows/kinds and that toggles are `ToggleButton`. A small
host/tests unit that calls `refreshParamKnobs` with crafted lists and asserts widget
counts/types is feasible without a live compile — propose to **S4** or add under
`host/tests/`. State the unverified remainder (real on-screen appearance — no display
in this env; ask the human for a screenshot pass).

---

## 3. Follow-on — shell "mode affordance" (BYO-LLM, FLEET req #9)  *(Wave 1)*

`docs/byo_llm_plan.md` Phase 1, S3 slice. In the shell (no processor internals):
- **Copy-Prompt** affordance always present.
- Integrated **Generate** button present only when a provider/key is configured
  (probe `PLUGINFORGE_PROVIDER` / key env — reuse PromptPanel's existing env reads);
  degrade gracefully with no key instead of surfacing a Python traceback.
- Depends on **S1 req #7** (`onFaustCompileFailure` callback) for the error-surface
  half — the shell routes it to PromptPanel like `onFaustCompileError` today. Do not
  start the error-surface half until req #7 lands.

---

## 4. Optional — LLM layout-hint post-pass  *(lowest priority, needs S1 + a prompt)*

`ui_design_plan.md §3`. After a successful compile, send captured param metadata
(labels, ranges, kinds, groups) to a cheap **separate** LLM call returning a
layout-hint JSON (grouping / order / control kind / window-band). Metadata→metadata
only: a bad answer degrades to the §2 deterministic fallback, never bad audio.
Prereqs, both gated:
- A new `llm/generate.py` **mode** — S1 implements, S3 specs the ADR-011 JSON shape
  via a FLEET cross-lane request (contract → §2 trigger 3).
- A new layout-hint **prompt file** — Tier 2: re-run the affected benchmark or
  explicitly declare the baseline stale.

Only scope this after 1–3 are done and if the deterministic layout proves
insufficient in practice.

---

## Anchors (quick reference)
- Split shell: `host/Source/PluginEditor.{h,cpp}` (owns window/title/meter + wiring).
- My grid: `host/Source/ParamGridPanel.{h,cpp}` — `MAX_KNOBS`, `refreshParamKnobs`, `resized`.
- Prompt/LLM bridge (PF-006 lives here): `host/Source/PromptPanel.cpp`.
- Kind enum: `host/Source/FaustEngine.h:23-30`. Slot↔zone map: `host/Source/ParamMap.h`.
- Design source: `docs/ui_design_plan.md §3`. Bugs: `docs/BUGS.md` (PF-006). (The S-lane
  board this plan referenced is retired — `c58a281`.)
