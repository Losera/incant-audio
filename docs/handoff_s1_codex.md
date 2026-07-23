# Codex handoff — PluginForge, Backend Core (S1) lane

You are taking over the **Backend Core (S1)** lane of PluginForge, an LLM-guided
program-synthesis tool for real-time DSP audio plugins (natural-language prompt → LLM →
Faust DSL → LLVM JIT → VST3/AU inside a JUCE host). Work is split across parallel sessions
coordinated by an overseer; you own one lane and must stay inside it.

## First, read these (in order)
1. `STATUS.md` — current state (Works / Broken / Assumed / Next three / Waiting on you).
2. `docs/FLEET.md` — the coordination board: your lane, the cross-lane request log, gate state.
3. `COLLABORATION.md` — the consult gate (§2), the Tier-1/Tier-2 evidence bar (§3), the
   5-line change-report format (§4).
4. `CLAUDE.md` — what the project is and the "Do not" list.

## Your lane — write ONLY these
`PluginProcessor.{h,cpp}`, `FaustEngine.*`, `ParamMap.h`, `ParamPool.*`, `OutputGuard.*`,
`llm/*`, `tools/*`, `bench/*`, `host/CMakeLists.txt`.
**Do NOT touch** `PluginEditor*` or any `*Panel.*` files (UX sessions own those),
`STATUS.md` or `docs/FLEET.md` gate/ruling tables (overseer owns), `docs/BUGS.md` (S5 owns).
This is a shared working tree — other sessions have uncommitted changes in it. When you
commit, stage ONLY your own files/hunks; never `git add -A`.

## What was just completed (do not redo)
**State persistence (P11) — DONE, committed to `main` as `c34bbb6`.**
- `getStateInformation`/`setStateInformation` implemented in `PluginProcessor.cpp`.
- Format: versioned `ValueTree`→XML blob — `<PluginForgeState schemaVersion="1"
  faustSource=… prompt=…>` wrapping verbatim `apvts.copyState()` `<STATE>` (64 macro
  values) + a `<SlotLabels>` hint. Corrupt/foreign/unknown-version blobs are ignored.
- Processor now retains Faust source + prompt; `loadFaustCode()` gained an optional
  `prompt` arg (default `{}`). Restore recompile is deferred to `prepareToPlay` so the DSP
  JITs at the real sample rate.
- Test: `host/tests/StatePersistenceTest.cpp` (+ CMake target), 13/13 pass, clean ASan/UBSan.
- Build/run the test: `cmake --build build --target StatePersistenceTest && ASAN_OPTIONS=detect_leaks=0 ./build/StatePersistenceTest_artefacts/Debug/StatePersistenceTest`

## Rules that govern every change you land
- **Tier 2** (anything on/synchronizing with the audio thread, `std::atomic`/memory
  ordering, APVTS↔Faust parameter mapping, wire contracts, the generation prompts):
  requires (a) a **primary source cited by `file:line`** — read the header, never recall
  it; (b) a **test or runnable check**; (c) an explicit statement of **what you did NOT
  verify**. "Verified" is banned without a named artifact.
- **Consult-first gates (§2)** — draft, then STOP for human sign-off: overwriting a
  benchmark baseline/results file; a new/reversed ADR; changing a cross-component contract
  (persisted-state format, ADR-011 JSON, param-slot scheme); new dependency / packaging.
- **A known defect is never gated** — fix it or name it as deferred in your report and file
  it with S5 via a FLEET.md cross-lane request.
- After each landed change, emit the **5-line report**: CHANGED / WHY / VERIFIED / RISK /
  YOUR MOVE. Do NOT rewrite `STATUS.md` — the overseer consolidates from your report.

## Next candidate work (pick per STATUS.md "Next three" + FLEET requests)
1. **Broken #1 in an earlier STATUS (verify current state first):** parameter
   denormalization / zone-pointer cache. Per the CLAUDE.md note this may already be handled
   by `ParamMap.h` (commits efbb5a5/91a5a89/d10f59e) — **trust the code, re-read
   `ParamPool.cpp` + `ParamMap.h` before acting.**
2. **Shutdown race (Broken #3)** and **RT-safety in the param path (#4)** — likewise
   reportedly already fixed; confirm against the code before touching.
3. **FLEET req #2 (routed to S1):** a `generate.py` progress/attempt indicator that keeps
   ADR-011's one-JSON-line-on-stdout contract intact. Overseer's ruling (FLEET.md "Overseer
   rulings") says option (a) is S2-only; option (b) is an S1+S2 co-design using incremental
   reads. Only start if S3/S2 actually request it.
4. **Bug I filed (FLEET req #5), not yet in `docs/BUGS.md`:** `FaustEngine::prepare()`
   updates the `sr`/`block` members but does NOT re-init an already-live DSP
   (`FaustEngine.cpp:154-158`), so a host that changes sample rate after a patch is live
   keeps the old rate. Needs a fix in the FaustEngine lifecycle (re-init or re-JIT on rate
   change). Coordinate with S5 for the BUGS.md entry.

## Parked / gated — do NOT do unilaterally
- **Benchmark re-run + overwriting `bench/results/.prompt_baseline.json`** — §2 trigger-1
  (a measurement is data). Route to the human via the overseer; the current 0.88 baseline
  is stale (measured on a deleted prompt) but must not be overwritten without authorization.

## Coordination
You cannot message the overseer directly. The async channel is `docs/FLEET.md`: sign your
roll-call row ACTIVE, and append to the **Cross-lane request log** (append-only, any
session may) — do not edit the overseer's gate-state or ruling tables. S1's row and reqs
#4/#5 are already posted there (working tree, uncommitted).

## Environment
Arch Linux, JUCE 7 at `~/JUCE`, libfaust at `/usr/lib/libfaust.so`. Configure once:
`cmake -S host -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug`. Python tests:
`pytest -m "not integration"`. Free LLM providers only unless `PLUGINFORGE_ALLOW_PAID=1`
(anthropic is gated); default provider is gemini via `.env`.
