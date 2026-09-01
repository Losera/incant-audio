# PluginForge — Status  (2026-09-01)

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
  **Config half verified live 2026-09-01:** a human generated "a haunting reverb" in a
  launcher-started REAPER against the installed VST3, resolving `generate.py` through a
  `~/.config/pluginforge/config.json` — but that config was **hand-written** that day
  (provider `groq` over `httpx`, key from `PluginForge/.env`). **Install-layout half now
  landed** — PR #45 (`ee11db6`, 2026-09-01): `tools/install_release.sh` builds a dedicated
  venv from `requirements.txt`, seeds `.env` from the example, and writes a **merged**
  `config.json` with `generate_script_path` + `python_path`; `PluginConfig` gained an
  additive `python_path` field, `PromptPanel` gained `resolvePythonExe()`
  (env `PLUGINFORGE_PYTHON` → config `python_path` if it names an existing file → `python3`)
  and a **"Paths…" callout** that writes both runtime paths so nobody hand-edits JSON.
  Verified: `check.sh full` all green from a clean build (full C++ build, TSan,
  `EditorSessionTest` 372/0 incl. scenario 48, `PromptPanelThreadingTest`);
  `test_release_packaging` covers the merge, the picker-key preservation, and the
  no-`venv` fallback. **Not** done: a real clean-machine install rehearsal (Waiting on
  you #2); in-plugin API-key entry (v2).

**Decisions on record.** ADR-030 (no LangGraph), ADR-031 (no Obsidian infra; ID-resolution
checks + `tools/kg.py` instead) — both Accepted 2026-08-27, merged PR #30. **ADR-032**
(in-plugin provider/model + a non-secret plugin-read `config.json`, narrow v1) — Accepted
2026-08-29, PR #37; **fully implemented 2026-08-31** (backend PR #42, picker PR #43).
**ADR-033** (pre-generation "recommend" review workflow, Codex `feat/recommendation-mvp` /
PR #39) — **Accepted with conditions 2026-08-31**. The four conditions (opt-in "Plan"
mode; `detect_target_mismatch` off the legacy path; contract reconciled onto ADR-032 with a
provider-precedence rule; §3–§5 hygiene) were **applied 2026-08-31** and pushed to PR #39
as merge commit `8edd7cd`; `check.sh full` green from a clean build. **Reviewed 2026-09-01**
(diff read, conditions confirmed applied); two fixes go on the branch before merge — see
"Waiting on you" #1. Merge stays with the human — it edits the ADR-011 wire contract.

**Landed 2026-09-01:** PR #45 (`ee11db6`, PF-065 install-layout half — `install_release.sh`
writes a venv + seeded `.env` + a merged `config.json`; plugin gains `python_path` config
+ `resolvePythonExe()` + the "Paths…" callout; the callout button is gated during a run so
`reresolveRuntime()` cannot race the generation worker — found in review).

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

**3. Generation as an installed VST3 — code complete, clean-machine rehearsal outstanding.**
*(PF-065, high, found 2026-08-19, re-confirmed in REAPER 2026-08-28.)* Root cause:
`resolveGenerateScript()` resolves once at construction; the env override is never inherited
by a launcher-started DAW, the parent walk starts at the `.so` under `~/.vst3` with no repo
above it, the XDG fallback misses the dev copy. **Fixed across four PRs:** the config-file
source (PR #42), the provider/model picker (PR #43), and the **install-layout half** (PR #45,
`ee11db6`, 2026-09-01) — `tools/install_release.sh` now writes a venv + seeded `.env` + a
merged `config.json` with `generate_script_path` **and** `python_path`, the plugin resolves
the interpreter via `resolvePythonExe()`, and the "Paths…" callout sets both without a
hand-edit. `check.sh full` green from a clean build; `test_release_packaging` covers the
installer. **Still not seen working end to end:** a real install from a packaged tarball
into a clean `HOME`, then a launcher-started REAPER with nothing hand-set — Waiting on you
#2. Stays listed until that runs (per "a control counts only once it has been seen
failing").

**4. The XDG-installed runtime was a stale, unconfigured trap — code complete, same
clean-machine rehearsal outstanding.** *(PF-071, high, found 2026-08-28, reproduced in
REAPER and Carla.)* PF-065's earlier partial fix traded an honest "not found" for
`~/.local/share/pluginforge/llm/` — a 2026-08-15 copy that defaults to the *paid* provider
with no `.env`, so a launcher-started DAW showed "anthropic provider error".
**Fix path implemented 2026-08-31 → 2026-09-01 (PR #42, #43, #45):** `resolveGenerateScript()`
checks `config.json`'s `generate_script_path` *before* the XDG step; the picker writes
`active_provider`/`active_model` and `PromptPanel` sends them in the request, so a user-set
config beats the stale install; PR #45's `install_release.sh` now *creates* that config
(with `python_path` too) at install time, so the stale XDG copy is never the resolved
runtime on a real install. **Config half verified 2026-09-01** in a launcher-started REAPER
(hand-written config that day). **Remaining:** the same clean-machine rehearsal as PF-065 #3
— Waiting on you #2.

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
  `groq`'s `openai/gpt-oss-120b`. A groq run **started 2026-08-31**, resumed 2026-09-01
  (+3 cells, then the transport checkpoint fired again — quota still not clear), now at
  **~29/125** (`bench/results/efficacy/efficacy_groq_20260831.json`). Retry-corrected
  compile by tier: L4 6/6, L3 5/6, L2 6/6, L1 5/6, L0 5/5 — **flat, no tier gradient**, so
  far consistent with the ollama run. Resume again next quota window. Also n=1 per cell
  (PF-031's ≥3-run bar unmet), and the judge is a 7B grading a 7B.

---

## Next three things

1. *(evidence)* **Resume the groq 125-cell efficacy run.** `python bench/run_efficacy_study.py
   --provider groq --resume --out bench/results/efficacy/efficacy_groq_20260831.json` — at
   ~29/125. Resumes when the daily quota permits; it checkpoints harmlessly if not. Moves
   PF-011 out of Assumed, the one number this project steers by. Score with
   `bench/score_efficacy.py` (compile rate first; `--judge` spends quota — defect #10).
2. **Capture repros for PF-072 and PF-074.** The two medium in-host findings are
   currently unactionable — each needs the triggering patch source and the action
   immediately before. Needs an interactive host session; until then they can only be
   re-observed.
3. **PF-032 silent noise gate — re-measure on the shipping model.** Highest-severity open
   generation defect (#2). The prompt already carries the fix text; the drafted plan
   `~/.claude/plans/phase3-pf032-silent-noise-gate.md` leads with a re-measurement WP to
   see whether `groq`/`gpt-oss-120b` obeys it.

**Displaced, not urgent.** A piano roll (requested, unplanned; needs a note grid *and* a
clock — no host transport in Standalone).

---

## Waiting on you

1. **PR #39 (Codex `feat/recommendation-mvp`) — reviewed 2026-09-01, two fixes then merge.**
   The four ADR-033 conditions are genuinely applied (`detect_target_mismatch` off the
   legacy `generate` path — `llm/generate.py:1455`; "Plan" is `refineSelector` item 4;
   scenario 47 pins the default). Diff read surfaced: **(a)** a confirmed bug —
   `RecommendationPanel::markStale()` disables the accept button and nothing re-enables it
   except Dismiss, so a second "Plan" after an edit renders a dead button; **(b)** the
   provider picker is not wired to `onRecommendationInvalidated`, so changing it after a
   plan silently diverges the shown provider from the pinned one. This block fixes both on
   the branch (WI-1), re-runs `check.sh full`, then **you merge** — it edits the ADR-011
   wire contract. Stated remainder: `submitPromptForTest` bypasses `submitPrompt()` routing,
   so scenario 47 checks the default *outcome* not the `getSelectedId()==4` branch.
2. **Clean-machine `install.sh` rehearsal (PF-065 / PF-071).** The installer code landed
   (PR #45) and is covered at the shell level by `test_release_packaging` (merge, picker-key
   preservation, no-`venv` fallback). What is still unproven end to end: `package_release.sh`
   → install the tarball into a clean-ish `HOME` → REAPER from the desktop launcher with
   **nothing** hand-set → generate. Only that closes PF-065 #3 and PF-071 #4. Watch for:
   `python3 -m venv` on this Arch box (PEP-668 externally-managed — should be fine inside a
   venv), `pip install` offline behaviour, and whether the "Paths…" tooltip names the
   `config` branch as the interpreter source.
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
