# PluginForge — Status  (2026-08-31)

Rewritten each session per COLLABORATION.md §5. Single writer, no merge conflicts.
Narrative history lives in git and in `docs/sessions/`.

**Start a session with `/orient`**, not by reading this file top to bottom. It injects live
repo state, **the CI line**, this file's open sections, and a staleness banner if it falls
behind HEAD. `/brief` is the heavier cold-re-entry read.

The 759-line "targeted update" that preceded the 2026-08-29 rewrite was compressed to the
current picture; this file is kept current by §5 update, not re-narrated from scratch each
time. The blow-by-blow of every closed defect and past session is in `git log` and
`docs/sessions/NNN-*.md`.

---

## Works — and how we know

One line per capability, each naming its evidence. "Builds clean" is not a capability.

- **NL prompt → Faust → LLVM JIT → VST3/AU, generating a working effect.** Verified by ear
  first 2026-07-22; many patches since. The render oracle (`bench/render_oracle.py`, $0)
  proves no NaN / silence / DC / runaway over the corpus on every `check.sh audio`.
- **Runs interactively in a real DAW, both plugin targets.** Session 017 WP6, 2026-08-28,
  REAPER: `PluginForge Host` (Fx) audibly filtered/reverbed a looping signal; `PluginForge
  Synth` generated instrument patches and **a human pressed real QWERTY keys and heard
  notes** — the OS→JUCE keypress hop, end-to-end, which had only ever had a static-contract
  test. Both plugins coexisted in one project. Run log:
  `docs/sessions/017-phase2-interactive-host.md` §9.
- **Plugin-format contract holds.** `pluginval --strictness 5` → `SUCCESS` for both plugins
  across seeds; Carla's independent `carla-discovery-native` scan → `SUCCESS` for both.
- **New / Add / Redo generation modes**, selectable from the real UI. ADR-011 amendment,
  closed 2026-08-06; `EditorSessionTest` scenario 25 is the red-then-green proof.
- **State persistence** (save patch + knob values, reopen, recompile). Implemented `c34bbb6`;
  covered by `EditorSessionTest`.
- **Sample search via Internet Archive.** PF-054/PF-055 fixed 2026-08-13, `SoundfetchClientTest`
  + a live venv smoke test. (Freesound still 403s on a dead key — PF-056, Waiting on you.)
- **The full 125-cell efficacy grid runs end to end.** 2026-08-28, `ollama qwen2.5-coder:7b`
  (CPU), PF-041/PF-042-fixed judge — `bench/results/efficacy/efficacy_ollama_20260828_judged.json`.
  Compile rate 84–92% retry-corrected and tier-independent; semantic fidelity declines
  monotonically, judge mean 1.57/2 (L4) → 0.36/2 (L0). The pipeline degrades gracefully.
- **Spectral judge produces a per-prompt verdict** (report-only, not gated). PF-041/PF-042
  closed 2026-08-06.

- **ADR-032 v1: the plugin reads and writes its own config file.** Backend PR #42, picker
  PR #43 — both landed 2026-08-31. `host/Source/PluginConfig.h` (header-only) reads/writes
  `~/.config/pluginforge/config.json` (`$XDG_CONFIG_HOME`-aware); `resolveGenerateScript()`
  consults `generate_script_path` **between** the parent-dir walk and the XDG step (the
  PF-071 ordering fix) and reports which of the four steps won; `PromptPanel` has a
  **provider ComboBox + free-text model field** (a third control row, `Chrome::promptH`
  220→254) that writes `active_provider`/`active_model` on change (load-modify-write) and
  puts them in the request JSON, omitting the keys when unset so `generate.py`'s
  `DEFAULT_PROVIDER` still applies; the status-line tooltip names the resolved runtime;
  `SoundfetchClient` honours `soundfetch_interpreter_path`. **No Python change**
  (`llm/generate.py:507-510`). Verified: `check.sh full` all green; `EditorSessionTest`
  scenarios 43 + 44 (config present/absent/malformed; picker seeds/writes/round-trips;
  anthropic still carries through) 365/0; `PromptPanelPathResolutionTest` +8 cases.
  **Not** done: PF-065's install-layout half (a real `install.sh`); in-plugin API-key
  entry (v2). **Not** verified: a real launcher-started DAW (see "Waiting on you").

**Decisions on record.** ADR-030 (no LangGraph), ADR-031 (no Obsidian infra; ID-resolution
checks + `tools/kg.py` instead) — both Accepted 2026-08-27, merged PR #30. **ADR-032**
(in-plugin provider/model + a non-secret plugin-read `config.json`, narrow v1) — Accepted
2026-08-29, PR #37; **fully implemented 2026-08-31** (backend PR #42, picker PR #43).
**ADR-033** (pre-generation "recommend" review workflow, Codex `feat/recommendation-mvp` /
PR #39) — **Accepted with conditions 2026-08-31**. The four conditions (opt-in "Plan"
mode; `detect_target_mismatch` off the legacy path; contract reconciled onto ADR-032 with a
provider-precedence rule; §3–§5 hygiene) were **applied 2026-08-31** and pushed to PR #39
as merge commit `8edd7cd`; `check.sh full` green from a clean build. **Awaiting the human's
semantic review** — it edits the ADR-011 wire contract, so it is not auto-merged.

**Landed 2026-08-30/31:** PR #40 (`docs/phases/` living per-phase rollup), PR #41 (faust-rs
issue-#26 repair-loop A/B harness + **PF-076**), PR #42 (ADR-032 v1 backend, above).

**Landed 2026-08-29:** PR #34 (`/recap` skill), PR #35 (PF-069/PF-070 efficacy-harness
fixes), PR #36 (session-017 WP6 closeout). The faust-rs evaluation was written up as a reply
to GRAME issue #26 (`faust-rs 0.8.0`: 51/51 accept–reject agreement with the C++ compiler,
source location 15/15 vs 9/15, stable error code 15/15 vs 0/15); the **loop-level** follow-up
is PF-076 — feeding those diagnostics back to the repair model made it *worse* (75%→44%
repaired-within-2 on `qwen2.5-coder:3b`). Caveat: all of it is small models on CPU.

---

## Broken — ranked

Registry with IDs, severity, discovery dates: `docs/BUGS.md`.

**1. One generation defect is evidenced; the rest is sampling.** *(PF-024/PF-032, high,
open.)* Karplus-Strong's `recursion_cycle` fails to compile across every archive and the one
tier that compiles renders +79.6 dB runaway; the sidechain compressor fails every run with a
*different* error. `routing_arity` is now PF-024's dominant first-attempt failure class
(22/44 error strings on the 125-cell grid).

**2. The noise gate still renders silent.** *(PF-032's surviving half, high, open.)* Warm-LP
renders silent 1/4 at L4 on the grid.

**3. Generation fails as an installed VST3: "generate.py not found".** *(PF-065, high, open,
found 2026-08-19, re-confirmed in REAPER 2026-08-28.)* `resolveGenerateScript()`
(`host/Source/PromptPanel.cpp`) resolves once at construction; the env override is never
inherited by a launcher-started DAW, the parent walk starts at the `.so` under `~/.vst3`
with no repo above it, and the XDG fallback misses for the dev copy. **The config-file source
landed** (PR #42, resolution) **and the in-plugin picker landed** (PR #43, so a user sets
provider/model + can point `generate_script_path` without hand-editing JSON). The
*install-layout* half (a real `install.sh` writing a version-matched runtime + `.env`, and
preferring it over a stale one) stays open under PF-065. **Not verified in a real
launcher-started DAW** — that pass is what closes this (see "Waiting on you").

**4. The XDG-installed runtime is a stale, unconfigured trap.** *(PF-071, high, open, found
2026-08-28, reproduced in REAPER and Carla.)* PF-065's earlier partial fix traded an honest
"not found" for `~/.local/share/pluginforge/llm/` — a 2026-08-15 copy that defaults to the
*paid* provider with no `.env`, so a launcher-started DAW shows "anthropic provider error".
**Fix path fully implemented 2026-08-31 (PR #42 + PR #43):** `resolveGenerateScript()`
checks `config.json`'s `generate_script_path` *before* the XDG step; the picker writes the
config and `PromptPanel` sends `active_provider`/`active_model` in the request, so a
user-set config beats the stale install. **Still open only because** it is unverified in a
real launcher-started REAPER/Carla — a human verification pass.

**5. The DAW still sees raw slots.** *(follow-up to PF-037, medium, open, unfiled.)* No
section grouping / titled cards in the host parameter view.

**6. `en.*` envelope time units on the frozen ladder record.** *(PF-045, medium, open,
deliberately not spent.)* Reproduces at L0 on the grid.

**7. Refine produced a degenerate 1-knob patch.** *(PF-072, medium, open, found 2026-08-28,
needs a captured repro.)* `groq`/`gpt-oss-120b`, "reverb with chorus" as a refine over a
working reverb → a single dry/wet knob where New mode gave a full patch. Refine *mechanism*
verified correct on `gemini` afterward — this is model variance, not routing, but a bad
experience regardless.

**8. A generated instrument patch went NaN during play.** *(PF-074, medium, open, found
2026-08-28, needs a captured repro.)* Forced a regeneration. Not captured whether `OutputGuard`
latched (silence) or missed it. `OfflineSynthRenderTest` is 184/0 on fixtures, so it is a
specific patch under a specific runtime state.

**9. The declared ollama model cannot hold its own prompt.** *(PF-043, medium, open.)*

**10. `score_efficacy.py --judge` spends quota.** *(unfiled, medium, open.)* Takes a lock
(`bench/score_efficacy.py:558,569`); the quota cost is the real remaining half.

**11. Audible discontinuity on the 2nd-generation DSP swap in a host.** *(PF-073, low, open,
found 2026-08-28, needs a captured repro.)* "Not a very smooth transition." No `setParamValue`
flood in the log. A brief recompile gap is expected; a click is not.

**12. A chord sounded like ~5 voices from a strictly-mono engine.** *(PF-075, low, open,
found 2026-08-28.)* Almost certainly overlapping release tails + reverb from the mono voice —
the disambiguating held-chord test (all 5 sustain vs 4 decay) has not been run.

**13. Knob ordering is Faust's own.** *(PF-038, low, open.)*

**14. MIDI-fidelity gaps in a real session.** *(triaged 2026-08-16, all pre-existing, none a
regression.)* Monophonic by design (`FaustEngine.cpp:519-524`, deliberate), block-granularity
MIDI (~10.7 ms jitter, documented in-code), a hardcoded 2.0 s tail (`PluginProcessor.h:85`),
no MIDI CC mapping (`PluginProcessor.cpp:288-317`).

**Closed since the last rewrite** — one line each, detail in git / `docs/BUGS.md`:
**PF-076** (evidence — faust-rs diagnostics fed to the repair loop make it worse, not
better; A/B, McNemar p<1e-3; 2026-08-30, PR #41); the OS→JUCE QWERTY keypress hop
(2026-08-28, session 017); "never been in an interactive DAW" (2026-08-28, session 017);
PF-069 hardcoded efficacy budget and PF-070 compiler-hang crash (2026-08-29, PR #35);
PF-063 CI-staleness banner (2026-08-17); PF-066 stale octave assertion and PF-067 uncapped
`anthropic` pin (2026-08-25).

---

## Assumed, never checked

- **The efficacy tier gradient is unknown on the shipping model.** *(PF-011.)* The 125-cell
  grid ran once, on `ollama qwen2.5-coder:7b` (CPU) — compile rate tier-independent,
  fidelity monotonically declining. **Still assumed:** whether that gradient holds on
  `groq`'s `openai/gpt-oss-120b`. A groq run **started 2026-08-31** and the bounded harness
  checkpointed at **27/125** when the daily token limit ran low
  (`bench/results/efficacy/efficacy_groq_20260831.json`) — partial: L4 5/6, L3 4/6, L2 5/5,
  L1 4/5, L0 4/5, no tier gradient visible yet at this n. Resume with `--resume` next quota
  window. Also n=1 per cell (PF-031's ≥3-run bar unmet), and the judge is a 7B grading a 7B.

---

## Next three things

1. *(evidence)* **Resume the groq 125-cell efficacy run.** `python bench/run_efficacy_study.py
   --provider groq --resume --out bench/results/efficacy/efficacy_groq_20260831.json` when
   the daily quota clears — it is at 27/125. Moves PF-011 out of Assumed, the one number
   this project steers by. Score with `bench/score_efficacy.py` (compile rate first;
   `--judge` spends quota — defect #10).
2. **PF-065's install-layout half.** A real `install.sh` that writes a version-matched
   runtime + `.env` and has the plugin prefer it over a stale one. The config-file and
   picker halves landed (PR #42/#43); this is what is left of PF-065, and it is a
   release-rehearsal prerequisite.
3. **Capture repros for PF-072 and PF-074.** The two medium in-host findings are
   currently unactionable — each needs the triggering patch source and the action
   immediately before. Until then they cannot be fixed, only re-observed.

**Displaced, not urgent.** A piano roll (requested, unplanned; needs a note grid *and* a
clock — no host transport in Standalone).

---

## Waiting on you

1. **Semantic review of PR #39 (Codex `feat/recommendation-mvp`).** ADR-033's four
   conditions were applied 2026-08-31 and pushed as merge commit `8edd7cd`; `check.sh full`
   is green from a clean build. It edits the ADR-011 wire contract (adds `action` dispatch,
   `design_plan`, `recommendation`, two `reason` codes), so it is **not auto-merged** — it
   needs your read. The one open design point: `submitPromptForTest` bypasses
   `submitPrompt()`'s routing, so scenario 47 checks the default *outcome* but not the
   `getSelectedId()==4` branch (noted in the PR).
2. **Verify PF-065/PF-071 in a real launcher-started DAW.** Launch REAPER or Carla *from
   the desktop launcher* (not a terminal), no `PLUGINFORGE_*` exported, no `.env`, set the
   provider in the plugin's new picker (writes `~/.config/pluginforge/config.json`), and
   confirm generation succeeds. The tests prove the resolution logic; only this proves the
   defects are actually fixed.
3. **A listening pass on the interactive-session patches.** COLLABORATION.md §1 — whether a
   generated plugin *sounds like what was asked for* has no instrument and is not delegable.
   The render oracle proved the WP6 patches were not broken; it cannot tell you they were
   musical.
4. **A replacement Freesound API key.** *(PF-056.)* The configured key is sent and rejected
   (HTTP 403). Code cannot fix a revoked credential — get one from freesound.org.
5. **`bench/results/.prompt_baseline.json` is still untouched**, deliberately. It records
   `0.88` for the deleted pre-unification prompt; overwriting it is COLLABORATION.md §2
   territory.
6. **Three Phase-3 backlog plans are drafted, session-local, awaiting a fresh session.**
   `~/.claude/plans/phase3-pf032-silent-noise-gate.md`, `phase3-pf045-envelope-time-units.md`,
   `phase3-pf024-invalid-generation-families.md`. Each leads with a re-measurement WP: the
   prompt already contains the fix text for all three, and the open question is whether the
   shipping model obeys it.
7. **Untracked personal files left alone**, as always — the two notes at the repo root,
   the unshipped brief skill, and the product-architecture draft under bench/. Named in
   `.claude/HANDOFF.md`; not cited here as paths because they are not in the tree and the
   live-doc path check (correctly) rejects that.
