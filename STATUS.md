# PluginForge — Status  (2026-09-18)

Rewritten each session per COLLABORATION.md §5. Single writer, no merge conflicts.
Narrative history lives in git and in `docs/sessions/`.

**2026-09-18 — full rewrite, reconciled against `origin/main` (`d54d8fe`) and `docs/BUGS.md`.**
Prompted by an adversarial review this session ran across all three concurrently-running
sessions on this checkout (findings: artifact `28036260-0d47-4fa1-b4e7-2c7cde41f4c0`), which
surfaced three separate truth defects in this file and one in a published artifact — all now
fixed here:

1. **"Waiting on you #7" was stale** — it asked for review on PR #78 before merge; #78
   merged 2026-09-12 as `55fc78d`. Removed; see "Landed 2026-09-12 → 2026-09-15" below.
2. **"Broken #7" (PF-043) was marked fixed in its own text and left in the ranked list
   anyway** — its own parenthetical said to remove it "on the next full STATUS.md rewrite."
   This is that rewrite; it's gone.
3. **PF-011 disagrees with `docs/BUGS.md`'s own registry** — this file's "Assumed" section
   says PF-011 closed 2026-09-08; `docs/BUGS.md`'s registry row still lists it `open`. Both
   are partly right and the drift is now stated explicitly rather than picked a side (see
   "Assumed," below).
4. **Landed work since the last full rewrite (2026-09-09) was under-recorded**: PR #82
   (F1, bipolar-knob), #83 (soundfetch 0.4.0), #84 (PF-043 fix), #85 (ADR-039/040), #86
   (ADR-041 accepted), #87 (PF-032 WP2), #88 (ADR reclassification), #89 (PF-024 named-mono
   sub-case), #90 (ADR-041 step 1) are folded in below.
5. **The false artifact** (a 2026-09-17 UI-roadmap page written from a checkout 38 commits
   behind `origin/main`, presenting shipped ADR-035 steps 2–6 as "nothing like this exists
   yet") was corrected and republished to its original URL this session.

Also newly true and worth stating plainly: **ADR-038's explicitly-first move, E1 (AOT
export emit), has not started** — `tools/export_repo.py:100` is still
`// Placeholder: passthrough`, no branch existed for it before today. Two sessions were
dispatched today (`feat/e1-aot-emit`, `feat/interactive-capture-harness`) to work on E1 and
on an interactive-session capture harness respectively — see the artifact above for why.

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
- **Sample search via Internet Archive and Openverse, credential-free.** PF-054/PF-055
  fixed 2026-08-13; upgraded to soundfetch 0.4.0 2026-09-13 (the installed 0.4.0 *build*
  predated `src/soundfetch/__main__.py`, so `python -m soundfetch` — the argv PluginForge
  builds — failed on it despite the matching version string; the 0.4.0 *tag*, `651b021`,
  has it). Openverse added and made the sample browser's default: no credentials, unaffected
  by PF-056. `SoundfetchClientTest` (32 checks) + `EditorSessionTest` scenario 38 + a live
  network smoke test (`openverse search "rain"`, real result). (Freesound still 403s on a
  dead key — PF-056, Waiting on you — but the failure is now legible: a non-network
  `status --json` preflight distinguishes "no key configured" from "key configured but
  rejected", and a 403 names Freesound instead of showing the raw message unattributed.)
- **The full 125-cell efficacy grid runs end to end.** 2026-08-28, `ollama qwen2.5-coder:7b`
  (CPU), PF-041/PF-042-fixed judge — `bench/results/efficacy/efficacy_ollama_20260828_judged.json`.
  Compile rate 84–92% retry-corrected and tier-independent; semantic fidelity declines
  monotonically, judge mean 1.57/2 (L4) → 0.36/2 (L0). The pipeline degrades gracefully.
- **The grid also ran end to end on `groq` / `openai/gpt-oss-120b`** — the shipping-tier
  model. All 125 cells generated code (started 2026-08-31, completed 2026-09-07); committed
  on branch `task/efficacy-groq-125` (`efficacy_groq_20260831.json`, 125 cells: 114
  compiled, 9 compile_failed, 2 rate_limited). Re-scored 2026-09-08 with
  `bench/score_efficacy.py` ($0, no quota, no `--judge`): retry-corrected compile **L4 23/25
  (92%) · L3 24/25 (96%) · L2 24/25 (96%) · L1 21/25 (84%) · L0 22/25 (88%)**. Not a
  monotonic tier gradient — L2–L4 are flat and L1 is a trough. First-try tells the story:
  L2/L3/L4 all 84%, **L1 collapses to 48%**, L0 recovers to 72%. The L1 tier is "sensory
  metaphor only, no effect or parameter names"; L0 is "named artist/gear reference" — a
  specific device name (`SSL console fader`, `LA-2A`) is more actionable to the model than
  an abstract metaphor. L1's failures are semantic, not syntactic (error-class tagging:
  SEMANTIC 9 / SYNTAX 4 at L1, vs 2–4 elsewhere). Orthogonally, the `dynamics` category is
  weak at *every* tier (first-try 20–60%) — largely a `routing_arity` mechanism now
  diagnosed and partly mitigated (defect #1; prompt fix PR #75, single re-run 2026-09-09).
  Caveat: n=1 per cell (PF-031's ≥3-run bar unmet), unjudged, and `dynamics-03`/L1's 3rd
  corrective attempt was cut off by a rate limit and recorded as a failure — so
  retry-corrected is a lower bound, understated by at most 1/125 (inside L1).
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
  no-`venv` fallback. **Clean-machine rehearsal PASSED 2026-09-01** —
  `package_release.sh` → tarball → `install.sh` into a scratch `HOME` → REAPER started
  against it with **no `PLUGINFORGE_*` and no `config.json` hand-edit** → the human
  generated a working effect, provider `ollama` (the seeded default; local, zero
  credentials), runtime resolved through the installed `config.json` + venv. PF-065 and
  PF-071 close on this. The `google-genai` follow-up (PR #46, `52903d5`, merged 2026-09-01)
  synced the shipped `requirements.txt` to `bench/requirements.txt` so the seeded `gemini`
  default runs on a clean install — PF-077 closed. Deferred: in-plugin API-key entry (v2).

**Decisions on record.** ADR-030 (no LangGraph), ADR-031 (no Obsidian infra; ID-resolution
checks + `tools/kg.py` instead) — both Accepted 2026-08-27, merged PR #30. **ADR-032**
(in-plugin provider/model + a non-secret plugin-read `config.json`, narrow v1) — Accepted
2026-08-29, PR #37; **fully implemented 2026-08-31** (backend PR #42, picker PR #43).
**ADR-033** (pre-generation "recommend" review workflow) — **Accepted with conditions
2026-08-31, MERGED 2026-09-01 as PR #39 (`8ebfb43`).** The four conditions (opt-in "Plan"
mode; `detect_target_mismatch` off the legacy path; contract reconciled onto ADR-032 with a
provider-precedence rule; §3–§5 hygiene) were applied, plus the two 2026-09-01 review fixes
(re-arm the accept button after `markStale()`; wire the provider picker to
`onRecommendationInvalidated`) and two dead-branch removals (`target_mismatch` off the plain
`generate()` path; the `request.get("budget")` fallback). `check.sh full` + CI green.
**ADR-035** (per-plugin generated faces) — Accepted 2026-09-03, PR #56; pipeline Steps 1–5
wired end to end by 2026-09-07 (above), Step 6 / A5 open as PR #78. **ADR-038** (re-sequence
generated-face richness + plugin export ahead of Phase 6) — Accepted 2026-09-09 by explicit
user decision, PR #79 (`3162647`, docs only). Accepts ADR-023 + its 2026-08-13 amendment as
the export design of record (`Proposed` → `Accepted`); opens a "faces v2" track and runs its
cheap steps — **faces F1–F3**, **export E1–E3** — *parallel to* Phases 3–4, not after a
release. **F4** (visualizer subsystem) is now ADR-041 (Accepted 2026-09-15, PR #86) — see
below. **E4** (face export) remains gated behind its own future ADR. The critical path —
PF-024, PF-032, the Phase-4 release — is explicitly unchanged, and export is not cut as a
public release while those defects are open. **F1 landed** (PR #82, `b73c860`, 2026-09-12 —
bipolar-knob arc fill + centre detent, `GKnobGeometry.h`). Next: F2/F3, and **E1** (in-process
AOT Faust emit via `libfaust`'s `generateAuxFilesFromString`) — **not yet started as of
2026-09-18**, ahead of ADR-038's own explicit sequencing (dispatched today, see the rewrite
note above). Ladders: `docs/sessions/020-generated-faces-v2.md`. **ADR-038 itself carries a
2026-09-15 reclassification note** (below, PR #88) — "recorded here as a routine
scheduling/resequencing decision, not architectural direction under COLLABORATION.md §2
trigger-2." That downgrades the *decision record*, not the work it authorized: F1–F3/E1–E3
still describe what's actually landing and in what order, and nothing above should be read
as still-live architectural weight beyond that.

**ADR-039** (cross-provider failover: cloud rate limits fall back to local only) —
**Proposed**, PR #85 (docs only, 2026-09-13), not reclassified. **ADR-040** (hardware
envelope for local generation — measure before optimizing) — also PR #85, but **carries the
same 2026-09-15 reclassification as ADR-038 above** (PR #88): "a routine measurement
methodology note," not live architectural direction, despite still showing status
`Proposed` in `docs/decisions.md` itself. **ADR-041** (a `FaceVisual` drawing layer for
generated faces — ADR-038's F4, six independently-reviewable steps, each its own Tier
review) — **Accepted 2026-09-15 by explicit user decision, PR #86**, not reclassified;
step 1 (`ArchetypeLayout::VisualRegion` + `Result::visuals`, `rail()` returns its reserved
display region instead of discarding it) **landed same day, PR #90**. **Eight prior
decisions — ADR-006, 026, 028, 030, 031, 034, 038, 040 — were reclassified as routine
scheduling/sequencing/methodology calls dressed as ADRs, in their own status notes, nothing
removed or reworded** — **PR #88** (`98b7906`), branch docs/adr-reclassify-routine; per that
commit's own message, triggered by the maintainer's doc-output-pollution concern. The first
version of this rewrite named PR #88 without connecting it back to ADR-038/040 above —
fixed here.

**Landed 2026-09-04 → 2026-09-07 — the ADR-035 generated-face pipeline is now wired end
to end** (five PRs on top of #58/#59/#61 below; narrative in `docs/sessions/020-generated-faces-v2.md`):

- **PR #63** (`886be7a`, A4 / ADR-035 gap 4) — `host/Source/ArchetypeLayout.h`: free
  functions (no JUCE dep), `columns()` / `split()` / `rail()` section-and-control geometry
  for `synth-panel` / `channel-strip` / `tape-unit` / `texture-field`. `pedal` / `utility` /
  `""` / unknown keep the existing grid unchanged. Inert until an IR names a supported
  archetype.
- **PR #64** (`487155f`, A3d) — `GeneratedFaceLookAndFeel::drawRotarySlider` GKnob arc
  geometry (1.2π→2.8π, hairline track from `README.md:253`'s ink-alpha rule, 2px pointer).
  **Real pre-existing bug fixed along the way:** `ParamGridPanel::applyPresentation()`
  (ADR-022 §3) hardcoded each slider's colours from the old heuristic accent, shadowing
  *any* LookAndFeel's ColourScheme — a generated face's accent never reached a widget.
  `faceAccentActive` gate added.
- **PR #65** (`421821a`, A3b) — `PluginEditor.cpp`'s compile-success callback builds the
  captured param table, calls `promptPanel.requestUiFace()`, and applies a valid schema-3
  answer via `applyUiIr()`/`applyGeneratedFace()`, falling back to the deterministic layout
  on any failure. **The redesign this PR actually is:** a first attempt gave `ui_face` its
  own subprocess-spawning thread (`UiFaceClient`), which reproduced a real `EditorSessionTest`
  hang — root-caused via gdb thread dumps + `/proc/<pid>/task` (`anon_pipe_read` on the stuck
  thread was the evidence) to two lifecycle races only a **second thread** can create:
  (1) a Generate arriving before the ui_face job's `ChildProcess` registers is invisible to
  `activeChild->kill()` (a no-op on null), so the subprocess ran anyway and blocked the
  worker; (2) `ChildProcess::kill()` is SIGKILL to the **direct child only**, so an orphaned
  `sleep`-under-`/bin/sh` grandchild keeps the pipe open and `readAllProcessOutput()` blocks.
  **Fix:** route `ui_face` through `PromptPanel`'s existing single worker thread as a
  lower-priority job kind — `UiFaceClient.h/.cpp` and an interim `SubprocessForkLock.h` are
  gone. Also patched `FakeGenerator.h` to bypass `action:"ui_face"` capture so a background
  call can't clobber the `request.json`/`argv.txt` ~20 scenarios assert against.
  `EditorSessionTest` 428/0 (was hanging); `PromptPanelThreadingTest` 9/0; TSan trio clean;
  CI green on `421821a`. **Merged directly by the human** — like #61, no formal GitHub
  review artifact.
- **PR #67** (`fa85f59`, A3c) — five embedded display/readout typefaces (Space Grotesk,
  Barlow Condensed, Oswald, Archivo, IBM Plex Mono; OFL, variable fonts instanced to static
  weights), `GeneratedFaceLookAndFeel` font dispatch, theme-aware `ParamGridPanel` label /
  heading fonts, `setFaceAccent()` refactored so accent + fonts refresh atomically.
- **PR #69** (`c8f27b4` + `1b43361`) — layout fix from a live screenshot that reproduced
  over-sectioning (one heading per knob), `lg`-knob misalignment, and ~5/6 dead panel
  space on a 4-param generation. `perRowFor(count, span)` packs 1–4 controls per row (a
  pure function of count, never pixel width — preserves the `contentHeightForSections()`
  invariant); `layoutFor()` is now **total** (`row()` as `default:`, `isSupported()`
  deleted); `size:"lg"` renders solo full-width; thin sections merge in
  `deriveLayoutFromGroups()`. `UiDesignGallery` baseline re-accepted (`1b43361`).

**Not verified:** a real DAW/Standalone session with a live LLM provider driving the face —
`EditorSessionTest`'s `FakeGenerator` is the evidence so far. **PR #78 (ADR-035 Step 6 / A5)** was the
verification-loop step — **merged 2026-09-12 as `55fc78d`.** It consumes the persisted
`uiIr` on reopen (new `uiIrSourceKey` state-blob attribute, a `juce::String::hashCode64`
fixed polynomial hash) so a restored project re-applies the accepted face instead of
re-deriving Ember and re-spending a `ui_face` call, plus a gallery quota-leak fix found
doing it. Additive v3 amendment, no `kStateSchemaVersion` bump.

**Landed 2026-09-08 (reconciled by the 2026-09-09 consolidation pass):** **PR #73**
(`2f03141`, doc corrections to the session-loaded files — `CLAUDE.md` toolchain re-read from
the box (kernel 7.1.8-arch1-3, CMake 4.4.2, Python 3.14.7), `VST3/AU` → `VST3 + Standalone`,
the already-regenerated "Faust 2.85.5 deferral" paragraph deleted; `INTERFACE.md` citations
re-anchored to symbols after a ~700-line line-number drift; `PLUGIN_HEALTH_PLAN.md` internal
contradiction removed. Prose only, `check.sh fast` green). **PR #72** (`7970d66`, ADR-011
credential precheck now resolves the request's own `provider` instead of the import-time
`DEFAULT_PROVIDER` — closes an ADR-012 free-only bypass where a request naming paid
`anthropic` walked around `assert_free` when `DEFAULT_PROVIDER` was free; a malformed
`--json` payload now returns ADR-011 JSON, not a traceback. Human-reviewed 2026-09-07.
**Watch:** `tests/conftest.py` now `setdefault`s `PLUGINFORGE_ALLOW_PAID=1` suite-wide, so
the free-only guard is off by default in tests — a future free-only regression test must pop
the key. 843 non-integration tests pass; CI green). **PR #70** (`704963a`, issue-#26 repro
suite hardening — `score_repair_ab` aggregates `--samples K>1` cells before pairing (K=1 a
strict no-op), `bench/repair_ab_repro/requirements.txt` pins scipy, `verify.py` floors any p-value
below `1e-8`; `expected.json` moves 4 bounds, `checks_expected` stays 367; `verify.py`
REPRODUCED, issue26 suite 101 passed). **PR #74 / PR #75** are reflected in the PF-011
closure ("Assumed" section) and defect #1 (PF-024) respectively. `/change-report` for all
four: `docs/records/change-reports-2026-09-08-merges.md`.

**Landed 2026-09-12 → 2026-09-15:** **PR #82** (`b73c860`, F1 — the bipolar-knob bugfix,
ungated per COLLABORATION.md §1; see the ADR paragraph above). **PR #83** (soundfetch
upgraded to 0.4.0 + Openverse; reflected in the sample-search "Works" bullet above).
**PR #84** (`c9655da`, PF-043 fixed — ollama's declared default model raised to
`qwen2.5-coder:7b-16k`, 16384-token context; detail folded into Broken defect history,
`docs/BUGS.md`). **PR #85 / #86** (ADR-039/040 proposed, ADR-041 accepted — see the ADR
paragraph above). **PR #87** (`fix/pf032-gate-time-units`, PF-032 WP2 — the att/hold/rel
unit-contract fix + gate few-shot; free-tier 6/6 pass vs. a confound-corrected control's
5/6 broken; groq still owed — full detail in Broken #2 below). **PR #88** (ADR
reclassification, docs only — see the ADR paragraph above). **PR #89**
(`fix/pf024-named-mono-routing`, PF-024's named-mono-fed-a-stereo-chain sub-pattern — mixed
free-tier result, 2/5 cells clearly fixed, 1 ambiguous, 1 shifted, 1 unchanged; groq still
owed — full detail in Broken #1 below). **PR #90** (`feat/adr041-step1-visual-region`, ADR-041
step 1 — see the ADR paragraph above).

**Landed 2026-09-09 → 2026-09-10:** **PR #76** (`1bdb38d`, the consolidation pass itself —
the four change reports above + the targeted §5 edits for the 09-08 batch); **PR #77**
(`c2cc779`, the single 25-cell dynamics re-run verifying the #75 fix — result folded into
defect #1); **PR #71** (`6fe6679`, renamed the issue-#26 repro package to
`bench/repair_ab_repro/` — the old `bench/issue26/` path no longer exists — and its scripts
to descriptive names (`frs_rederive_issue26.py` → `frs_rederive.py`,
`run_issue26_pipeline.sh` → `run_repair_ab_pipeline.sh`, etc.); held for the GRAME handoff,
merged once the rename was confirmed not to break `verify.py`).

**Landed 2026-09-03 (evening, after PR #57 below):** four PRs, in landing order — **PR #58**
(`da5594d`, `host/Source/ThemeValidate.h` — WCAG contrast gate for a `UiIr::Theme`: `text` ≥
7:1 / `textDim` ≥ 4.5:1 / `accent` ≥ 3:1 **against `surface`** (the B1 decision — the panel
fill the face paints on, not the window ground `#050505`), plus accent/accentAlt/text
separation; per-token Ember fallback, never rejects the whole theme; 71-check
`ThemeValidateTest`, wired into `check.sh` + CI. ADR-035 Step 2.) **PR #60** (`1ad968a`,
**issue-#26 integrity pass 2** — found and corrected a **backwards mechanism claim** in the
drafted GRAME reply before it was posted: the docs had said faust-rs's caret makes the model
"edit at that spot and re-break it"; measured over the committed run it's the opposite — the
quoted source line survives **verbatim** into attempt 1 in 94/161 (58%) of arm-B rewrites vs
7/161 (4%) of arm A's, McNemar p≈2e-22, replicated on the 7B. The model treats the
caret-quoted line as fixed and edits around it; the plain C++ error gives it nothing to hold
onto, so it discards and rewrites, which compiles more often. Also corrected `README.md`
number drift (rescue 49/87→49/88; a cap-percentage sentence sitting above the wrong table)
and closed two hygiene gaps — the documented pipeline scripts were silently reverting six
report artifacts to the pre-screen view for want of a `--screen` flag, plus an orphan
pre-screen chart PNG. New `bench/score_repair_ab.py` caret-preservation metric + a
`verify.py` check, `expected.json` checks_expected 311→367, new
`tests/test_repair_ab_readme_numbers.py` pinning the README prose to `verify.observe()`.
**`~/issue26-reply.md` was updated to match** — confirmed this session by reading the draft:
it already cites `1ad968a` and the 58%/4% figures, not stale.) **PR #59** (`4508ac4`,
`llm/ui_face.py` + a `ui_face` action in `generate.py` + `llm/prompts/ui_face_prompt.md` +
`tests/test_ui_face.py` (25 checks) — post-compile: captured param table → schema-3 face
JSON, mirroring `recommendation.py`'s shape. Host re-validates and is authoritative; the
Python side is a lenient structural filter (drops unknown labels, dedupes, normalises theme
enums, forces button-continuous-style to `""`, caps `lg` at 2) and raises `InvalidFace` only
on the unrenderable. ADR-035 Step 5.) **PR #61** (`3e79739`, A3a:
`host/Source/GeneratedFaceLookAndFeel.h` (`: public ForgeLookAndFeel`, one
`setColourScheme()` from `ThemeValidate::validate(theme)`),
`PluginForgeEditor::applyGeneratedFace()` (detach-then-rebuild from the compile callback),
`UiIr::Theme::operator==`, `EditorSessionTest` scenario 50. **Still a no-op in production**
— `deriveLayoutFromGroups()` only ever emits the Ember default, so the face never attaches
until A3b wires PR #59's producer into the compile callback. ADR-035 flags this step as new
UI architecture needing its own review (AGENTS.md §4); it was **merged directly by the
human** — `gh pr view 61` shows no formal GitHub review submitted, so there is an informal
review but no artifact of one beyond the merge itself.) `check.sh full` green locally
pre-merge on #61 (fast suite, prompt grounding, build, TSan, `EditorSessionTest` 421 checks
incl. scenario 50, `ThemeValidateTest`, `OfflineRenderTest`, `UiDesignGallery`); CI ran green
on the combined head (`60d75c7`, covering #58+#60+#59) and again on `3e79739` (#61 on top) —
**confirmed live this session** via `gh run list`. **Next:** A3b — wire the `ui_face`
subprocess call into `PluginEditor.cpp`'s compile-success callback (non-blocking, falls back
on any failure), which is what makes A3a visible.

**Landed 2026-09-03 (later):** PR #55 (`78fb9db`, **issue-#26 integrity pass before the
GRAME handoff**) — the A/B evidence made defensible before a public reply:
`bench/corpus_screen.py` applies two outcome-blind syntactic rules (no top-level `process`
binding; a literal `...` in the source) and drops **10 of the 202** C++-rejected corpus rows
that are not programs — 9 English refusals, one mid-token truncation; screened headline (3B)
A 143/192 (74%) / B 82 (43%) / C 80 (42%), raw 202 gives 75/44/43, same finding. The arm-A
500-char stderr cap (the product's real feedback) is now **disclosed and stratified**: on the
158 programs where arm A's attempt-1 stderr was never truncated, arm A still repairs 75% vs
faust-rs 45%, p≈3e-7 — the cap handicaps arm A, it is not the effect. Mechanism numbers moved
onto fixed script-backed denominators (rescue 49/87 not 50/101; recidivism 21/39 not 21/51)
and the second-error identity now prints "no corrective attempt N" so `same + new + no_attempt
== failed`. `verify.py` rewritten around `observe()`/`freeze()` with a **checks-expected
N-guard** (311 checks; a stale harness vs a newer `expected.json` now fails loudly instead of
printing REPRODUCED over a subset — the exact failure that let a stale container pass 44/75
last week). WP2 transport robustness in `repair_ab_standalone.py` (retry/backoff on 429/5xx,
`EmptyResponse`/`OutputTruncated` excluded from the arm comparison, strict per-cell majority,
≥25% aborted → exit 3). MIT harness made self-contained (vendored `system_prompt.txt` snapshot
`a2d90956` + `frs_rederive_cells.json`, proprietary `COPY` dropped from the Dockerfile). First
`tests/test_score_repair_ab.py` (13 cases) + `tests/test_corpus_screen.py` (frozen excluded
SHAs). `check.sh full`-equiv + CI green (8 commits; §4 report given per commit in-session).
PR #54 (`90c7733`, README showcase `<img>` tags pointed into gitignored `artifacts/` — moved
two current-UI harness renders to tracked `docs/img/` and repointed; a real showcase pass
still waits on interactive captures). PR #56 (`3767978`, **ADR-035 Accepted** by explicit
user decision — per-plugin generated faces; a status note records that `GENERATION_PLAN.md`'s
5-gap order supersedes the 1–6 step numbering and flags the unresolved contrast-reference
colour. **ADR-036 Proposed** — shell redesign: the fixed `kLeftFraction=0.65` collides at the
900px default (PromptPanel widgets hit 0px), fix is a direction-neutral prep commit then
prototype Command-Bar vs Rail+Dock on two branches and delete the loser. `docs/design/incant-ui/`
is the distilled read-only design record; `docs/sessions/018-incant-ui-faces-and-shell.md`
the multi-session build plan.
`check.sh fast` green, `test_control_wiring.py` 122 passed).

**Landed 2026-09-03:** PR #51 (`2c7c5bb`, **UiIr schema 3** — `host/Source/UiIr.h` gains a
`theme` block on `Layout`: seven colour-string tokens + four enum tokens
(`display`/`readout`/`knob`/`density`), each defaulting to its Ember Console value, so a
schema 0/1/2 layout is byte-unaffected. `parse()` degrades a missing/blank colour or an
unrecognised enum **per-token** to the default, never rejecting the layout (the ADR-022 §3
bone-swatch lesson); the ceiling moved 2→3. The IR is now persisted in the state blob as a
`uiIr` root attribute — a v3 **amendment**, not a `kStateSchemaVersion` bump: a blob without
it parses to `UiIr::empty()`, the un-themed state every patch already had. **Inert** —
nothing renders the theme yet, and the editor re-derives the layout on the restore
recompile, so no behaviour changed. New `host/tests/UiIrTest.cpp` (41/0, wired into
`check.sh full` + CI); `StatePersistenceTest` +9; `check.sh full` all green. **ADR-035**
records the six-step "generated-plugin faces" direction — Steps 2–6 (host-side WCAG theme
validation, a `paramGridPanel`-scoped `LookAndFeel`, archetype layouts, a post-compile LLM
call that emits the IR, a verification loop) are **Proposed**; Step 3 is flagged as new UI
architecture that still needs its own review. Design bundle
`design_handoff_generated_plugin_faces/` is kept untracked by decision.). PR #52 (`0eefe50`,
**`.claude/hooks/session_collision_guard.py`** — a `SessionStart` hook that warns when this
checkout may be shared with another session: `.claude/HANDOFF.md` recorded on a different
branch, a working tree already dirty before the session acted, or another transcript in
this project written in the last 8 min. Cannot block a start, never silent — same
discipline as `handoff_injector.py`. Motivated by a real 2026-09-02 collision: the
issue-#26 and UI sessions both ran in the primary checkout and one `/handoff` overwrote the
other effort's still-owed handoff. `test_control_wiring.py` +10 incl. a verified-failing red
case; `COLLABORATION.md` §7 hook-table row; `check.sh full` + CI green).

**Landed 2026-09-02:** PR #49 (`e35ff58`, `bench/fidelity_gate.py` + test — the compile-only
half of the issue-#26 A/B; retracted the unwarranted temp-0 determinism claim and disclosed
the arm-A-vs-faust-rs wrapper confound); PR #50 (`778003e`, `bench/corpora/README.md` dataset
card, corpus relicensed CC-BY-4.0); PR #53 (`6d790bd`, **issue-#26 repro package
handoff-ready for GRAME** — root `LICENSE` gains an EXCEPTIONS section (corpus CC-BY-4.0,
harness MIT); `verify.py` grows `verify_fidelity()` + `expected.json` `"fidelity"` block;
README mechanism numbers restated on single denominators + the 9/15→8/15 C++-location
errata; new `METHODOLOGY.md`, `.dockerignore`, Dockerfile version assertion, cold-clone
quickstart, CI runs `verify.py`). The GRAME reply is drafted (`~/issue26-reply.md`), links
`6d790bd`, and is **waiting on a human to post** (below). `check.sh full`-equivalent + CI
green on all three.

**Landed 2026-09-01:** PR #39 (`8ebfb43`, ADR-033 pre-generation recommendations — above);
PR #45 (`ee11db6`, PF-065 install-layout half — `install_release.sh` writes a venv + seeded
`.env` + a merged `config.json`; plugin gains `python_path` config + `resolvePythonExe()` +
the "Paths…" callout; the callout button is gated during a run so `reresolveRuntime()`
cannot race the generation worker — found in review). PF-065 + PF-071 then verified closed
by a clean-machine REAPER rehearsal (above).

**Landed 2026-08-30/31:** PR #40 (`docs/phases/` living per-phase rollup), PR #41 (faust-rs
issue-#26 repair-loop A/B harness + **PF-076**), PR #42 (ADR-032 v1 backend, above).

**Landed 2026-08-29:** PR #34 (`/recap` skill), PR #35 (PF-069/PF-070 efficacy-harness
fixes), PR #36 (session-017 WP6 closeout). The faust-rs evaluation was written up as a reply
to GRAME issue #26 (`faust-rs 0.8.0`: 51/51 accept–reject agreement with the C++ compiler,
source location 15/15 vs 9/15 — *re-derived 2026-08-30 as 8/15, erratum carried in the
2026-09-02 reply* — stable error code 15/15 vs 0/15); the **loop-level** follow-up
is PF-076 — feeding those diagnostics back to the repair model made it *worse* (75%→44%
repaired-within-2 on `qwen2.5-coder:3b`). Caveat: all of it is small models on CPU.

---

## Broken — ranked

Registry with IDs, severity, discovery dates: `docs/BUGS.md`.

**Added 2026-09-24, not yet folded into the ranking below — read this first.** *(PF-079,
high, open.)* The compile gate this whole ladder is ranked against systematically overstates
success: real render-safety runs 25–40pp below compile rate on the committed groq/ollama
125-cell archives (`docs/records/efficacy-adversarial-review-2026-09-23.md` §B1). Two
`dynamics`-category idioms fail identically across two independent generator models and are
now root-caused directly against the Faust stdlib (not "unidentified" as first filed): an
**upward expander's makeup gain has no ceiling** (`gain_db = select2(level_db < threshold, 0,
(threshold-level_db)*(ratio-1))` — `ba.linear2db` floors at −758.6 dB single-precision,
so the gain follows the signal to +728.6 dB at the silence limit; measured +715 dB), and a
**brick-wall limiter's `select2` arms are inverted** (`select2(c,a,b)` returns `a` when `c`
is *false* — every sample at or below threshold gets replaced by the threshold constant,
0.98855 at the default; measured DC 0.95–0.98). Both are promptable; neither fix is landed
yet (`docs/BUGS.md` PF-079). Also landed this date: PF-076's public mechanism
(caret-anchoring) was withdrawn — the A/B *result* is unchanged, the *causal story* was
wrong, because the repair loop (and the shipping Fresh-mode retry path, PF-081) never shows
the model the program it is repairing. See PF-076/PF-080/PF-081 in `docs/BUGS.md`.

**1. One generation defect is evidenced; the rest is sampling.** *(PF-024/PF-032, high,
open.)* Karplus-Strong's `recursion_cycle` fails to compile across every archive and the one
tier that compiles renders +79.6 dB runaway; the sidechain compressor fails every run with a
*different* error. `routing_arity` is now PF-024's dominant first-attempt failure class
(22/44 error strings on the 125-cell grid).
  **Diagnosed 2026-09-08** (full trace: the session log / PR): the dynamics category's
  `routing_arity` failures (10/25 cells) are one mechanism — the model passes the audio
  signal as a trailing `_` argument to a stdlib effect (`co.compressor_stereo(r,t,a,rl,_,_)`,
  `ef.gate_stereo(...,_,_)`), which is a hard arity error because the library function
  reuses `x,y` internally. `misceffects.lib:182`, `compressors.lib:1002`. **Mitigation
  landed** (PR #75, `a778853`): a dynamics few-shot replaced the tape-flanger few-shot in
  `system_prompt.txt`, the "renders SILENCE" claim on the gate rule was corrected to "hard
  arity error", and `error_classes.ROUTING_ARITY` got a `RETRY_HINT` (was raw stderr only;
  `dynamics-01`/L4 and `dynamics-03`/L4 each repeated the identical error on all 3 attempts).
  **Verified 2026-09-09, n=1, single re-run of all 25 dynamics cells on the fixed prompt**
  (`bench/results/efficacy/efficacy_groq_dynamics_postfix_20260909.json`):
  *the targeted pattern is gone* — `co./ef. X_stereo(...,_,_)` in generated code **8/25 → 0/25**,
  `routing_arity` first-attempt errors **10/25 → 4/25**. `dynamics-03`/L4 (baseline
  `compile_failed`, the flagship) is now a first-try pass with the correct idiom. *Net
  compile rate barely moved* — first-try **8/25 → 9/25**, retry-corrected **16/22 → 17/22**
  on the 22 cells not cut short by groq's TPM limit — because failures partly rotated into
  `syntax` errors (6→9): the model now calls the compressor correctly but botches makeup
  gain (`: *makeup`, prefix-op) and other adjacent things the fix does not target. Honest
  read: the fix does what it was built to do; dynamics has multiple independent failure
  modes and this closed one. n=1 keeps it short of a verdict (PF-031 wants ≥3).
  **A second, independent PF-024 sub-pattern was targeted 2026-09-15** (`fix/pf024-named-mono-routing`,
  PR #89): a named mono Faust definition fed into a stereo chain. Mixed free-tier result —
  2/5 cells clearly fixed, 1 ambiguous, 1 shifted to a different error, 1 unchanged. Like
  the dynamics mitigation, this is a real but partial result; groq (the shipping model) is
  not yet measured.

**2. The noise gate still renders silent.** *(PF-032's surviving half, high, open.)* Warm-LP
renders silent 1/4 at L4 on the grid. The gate: real defect is the att/hold/rel
time-constant arguments (`misceffects.lib` wants seconds; the model variously pre-converts
to samples, writes implausible whole-second literals, or runs a time value through
`ba.db2linear`) — not the standing thresh/`db2linear` hypothesis, which did not reproduce.
**2026-09-15: WP2 landed** (`fix/pf032-gate-time-units`) — the STRICT RULES unit-contract
clause and `gen_stdlib_block.py`'s Dynamics descriptions now name the seconds contract
explicitly, plus a new worked gate few-shot; paid for by trimming three unused stdlib
entries from the effect profile. **Free-tier result: 6/6 pass** vs. a confound-corrected
control's 5/6 broken (the original 2026-09-12 5/6-silent number ran on ollama's stock
4096-token context, before PF-043's fix — re-measured clean before claiming the delta).
Honest caveat: most passing generations reproduced the new few-shot near-verbatim, so this
is a real but not yet a generalization-proven result. **Groq (the shipping model) is still
not measured** — needs `--i-authorize-spend`, gated on separate authorization per this
project's own consult rule. Detail: `docs/BUGS.md` PF-032.

**3. The DAW still sees raw slots.** *(follow-up to PF-037, medium, open, unfiled.)* No
section grouping / titled cards in the host parameter view.

**4. `en.*` envelope time units on the frozen ladder record.** *(PF-045, medium, open,
deliberately not spent.)* Reproduces at L0 on the grid.

**5. Refine produced a degenerate 1-knob patch.** *(PF-072, medium, open, found 2026-08-28,
needs a captured repro.)* `groq`/`gpt-oss-120b`, "reverb with chorus" as a refine over a
working reverb → a single dry/wet knob where New mode gave a full patch. Refine *mechanism*
verified correct on `gemini` afterward — this is model variance, not routing, but a bad
experience regardless.

**6. A generated instrument patch went NaN during play.** *(PF-074, medium, open, found
2026-08-28, needs a captured repro.)* Forced a regeneration. Not captured whether `OutputGuard`
latched (silence) or missed it. `OfflineSynthRenderTest` is 184/0 on fixtures, so it is a
specific patch under a specific runtime state.

**7. `score_efficacy.py --judge` spends quota.** *(unfiled, medium, open.)* Takes a lock
(`bench/score_efficacy.py:558,569`); the quota cost is the real remaining half.

**8. Audible discontinuity on the 2nd-generation DSP swap in a host.** *(PF-073, low, open,
found 2026-08-28, needs a captured repro.)* "Not a very smooth transition." No `setParamValue`
flood in the log. A brief recompile gap is expected; a click is not.

**9. A chord sounded like ~5 voices from a strictly-mono engine.** *(PF-075, low, open,
found 2026-08-28.)* Almost certainly overlapping release tails + reverb from the mono voice —
the disambiguating held-chord test (all 5 sustain vs 4 decay) has not been run.

**10. Knob ordering is Faust's own.** *(PF-038, low, open.)*

**11. MIDI-fidelity gaps in a real session.** *(triaged 2026-08-16, all pre-existing, none a
regression.)* Monophonic by design (`FaustEngine.cpp:519-524`, deliberate), block-granularity
MIDI (~10.7 ms jitter, documented in-code), a hardcoded 2.0 s tail (`PluginProcessor.h:85`),
no MIDI CC mapping (`PluginProcessor.cpp:288-317`).

**Closed since the last rewrite** — one line each, detail in git / `docs/BUGS.md`:
**PF-065** (generate.py unresolvable as an installed VST3 — config source PR #42, picker
PR #43, install-layout PR #45; **clean-machine REAPER rehearsal PASSED 2026-09-01**,
launcher-started, nothing hand-set, `ollama`); **PF-071** (stale XDG runtime → paid
provider — same fix chain, closed on the same REAPER pass: resolution landed on the
installed `config.json` + venv, provider was free/local);
**PF-076** (evidence — faust-rs diagnostics fed to the repair loop make it worse, not
better; A/B, McNemar p<1e-3; 2026-08-30, PR #41); the OS→JUCE QWERTY keypress hop
(2026-08-28, session 017); "never been in an interactive DAW" (2026-08-28, session 017);
PF-069 hardcoded efficacy budget and PF-070 compiler-hang crash (2026-08-29, PR #35);
PF-063 CI-staleness banner (2026-08-17); PF-066 stale octave assertion and PF-067 uncapped
`anthropic` pin (2026-08-25); **PF-043** (ollama's stock 4096-token context couldn't hold
the system prompt — default model raised to `qwen2.5-coder:7b-16k`, 16384-token context;
2026-09-13, PR #84, `c9655da`).

**Unfiled:** a granular-family observation from the REAPER pass — `"pitch synchronous
granulizer"` routed to `granular_effect` and the family control gate
(`llm/generate.py:435-448`) rejected every retry for missing required control groups; folds
into PF-024's family-failure sampling, notes in `scratchpad/pf065-reaper-observations.md`.
*(The `google-genai` gap that stood here is now PF-077, fixed — PR #46, above.)*

---

## Assumed, never checked

**The compile gate was assumed to track real plugin success; checked 2026-09-23, and it
doesn't.** *(PF-079, high, open.)* `faust -lang cpp` accept/reject overstates render-safety
by 25–40pp on the committed archives — see "Broken" above for the number and the two
root-caused failure classes. This is exactly the kind of claim this section exists to track;
it was missing from here even after the defect was filed, which is its own small instance of
the thing `check.sh assumed` is supposed to catch.

PF-011 — "the efficacy tier gradient on the shipping model" — closed 2026-09-08:
the groq / `openai/gpt-oss-120b` grid completed all 125 cells and was scored ($0). Result
in "Works" above: no monotonic tier gradient (L2–L4 flat ~84% first-try), a sharp L1
metaphor-only trough (48%), and a category-level `dynamics` weakness at every tier. The
`--resume` next-action that stood here for a week was a **no-op** — `prepare_resume`
(`bench/run_efficacy_study.py:329`) keeps every record that carries `code`, and all 125
did; the grid was already generated, just uncommitted. What remains open is a *deeper* bar,
not this one: n≥3 per cell (PF-031) and a judged fidelity pass (`--judge` spends quota,
defect #7). Those are new evidence items, not a re-opening of PF-011.

**This section disagrees with `docs/BUGS.md`'s own registry, and that's stated here rather
than silently resolved.** `docs/BUGS.md`'s PF-011 row still reads `open`, under its original
2026-07-23 framing ("Efficacy pilot generalizes to nothing, N=50, 1 model, 2/5 categories") —
a narrower, older claim than "closed" above addresses. `docs/BUGS.md`'s own preamble says to
"believe neither, read the code" when the two disagree. The honest position, reconciling
both: the *specific* n=50/1-model/2-category claim PF-011 originally named is superseded by
the 125-cell/full-category groq grid; the *general* "is this generalization trustworthy" bar
PF-031 sets (n≥3 per cell) is still unmet, on either file's telling. `docs/BUGS.md`'s
registry row is not updated by this pass — that file has its own ID/severity conventions
this rewrite didn't touch — but should be reconciled the next time someone is in it.
**Reconciled 2026-09-24:** `docs/BUGS.md`'s PF-011 row flipped `open`→`closed`, citing this
paragraph and the 2026-09-08 close date, exactly as invited above.

**Benchmark staleness (2026-09-08 → partly closed 2026-09-09):** `system_prompt.txt`
changed after the 125-cell grid was measured — the tape-flanger few-shot became a
`co.compressor_stereo` few-shot and the `routing_arity` retry hint was added (defect #1).
`efficacy_groq_20260831.json`'s **dynamics** numbers are therefore the *pre-fix* baseline.
A fresh single run of the 25 dynamics cells on the fixed prompt landed 2026-09-09
(`efficacy_groq_dynamics_postfix_20260909.json`, committed) — see defect #1 for the result.
The other four categories in `efficacy_groq_20260831.json` are unaffected by the edit and
still current. A full 125-cell re-run at n≥3 (PF-031) is still owed for a real verdict.

---

## Next three things

1. *(evidence)* **Re-run the groq efficacy grid at n≥3 (PF-031).** The single dynamics
   re-run (2026-09-09, defect #1) confirmed the prompt fix kills the `_,_)` pattern but left
   the compile-rate verdict inside n=1 noise. A real number needs all 125 cells × 3 with
   per-cell aggregation. `run_efficacy_study.py --provider groq` (spends quota; groq's
   ~8k-TPM limit paces it to ~1 cell/90s, so this is a multi-day grind — resume-loop it).
   Also owed: the makeup-gain follow-up — the dynamics few-shot stops at the compressor call
   and the model then writes `: *makeup`; showing `: par(i, 2, *(makeup))` would need ~120
   chars of prompt headroom back (trim one `gen_stdlib_block.py` curated entry).
   **In flight, uncommitted:** `.worktrees/efficacy-pf031` (branch
   bench/efficacy-groq-n3-20260915) already has a rep-1 checkpoint — see "Waiting on you"
   #6, do not disturb it.
2. **Capture repros for PF-072 and PF-074.** The two medium in-host findings are
   currently unactionable — each needs the triggering patch source and the action
   immediately before. Needs an interactive host session; until then they can only be
   re-observed. **A capture harness to make that session count for PF-072/073/074/075
   together was dispatched today** — `feat/interactive-capture-harness`, not yet landed.
3. **PF-032 silent noise gate — groq re-measurement.** Highest-severity open generation
   defect (#2). WP2 (retargeted at att/hold/rel, not the original thresh/`db2linear`
   hypothesis) landed 2026-09-15 on `fix/pf032-gate-time-units` with a clean free-tier
   result (6/6 vs. a confound-corrected control's 5/6 broken); the one step left is the
   shipping-model (`groq`) benchmark, gated on separate spend authorization.

**Authorized, parallel, not blocking the Next three.** ADR-038's cheap steps —
**F1 landed** (PR #82, bipolar-knob arc fill), **F2/F3** (chip rows, bespoke archetype
geometry) still open, **E1–E3** (AOT emit → wire `processBlock` → prove sound + un-gate
`/export`). One step per session, each independently landable;
`docs/sessions/020-generated-faces-v2.md` has the order and the `file:line` traps.
**F4 is now ADR-041 (Accepted 2026-09-15, PR #86)** — its own landing ladder (prep commit →
`FaceContext` → `FaceVisual`/F4a → PF-052 split → offline eval/F4b → ring buffer/F4c).
**Step 1 (prep commit) landed** — `ArchetypeLayout::VisualRegion` + `Result::visuals`,
`rail()` returns its reserved display region instead of discarding it. Next: step 2
(`FaceContext` + `TypefaceRegistry`) once F2/F3/E1–E3 are further along — that step's own
pause trigger applies if it's seen crowding out PF-024/PF-032. E4 remains gated behind its
own future ADR.

**Displaced, not urgent.** A piano roll (requested, unplanned; needs a note grid *and* a
clock — no host transport in Standalone).

---

## Waiting on you

1. **Post a correction to the GRAME reply — the earlier draft was already sent, and one of
   its claims is now known wrong.** *(Corrected 2026-09-24; this item's prior text was
   stale.)* The reply this item used to describe **was already posted 2026-09-08**
   (comment 4 on `Losera/incant-audio#26`), citing commit `704963a`, not `1ad968a` as
   previously recorded here — `~/issue26-reply.md` no longer exists (deleted per its own
   "delete the draft after" instruction). But that posted comment's mechanism claim — *"the
   caret is precise enough that the small model treats the quoted line as correct and
   rewrites around it"* — has since been **verified false against source** (2026-09-24,
   prompted by an external adversarial review): neither arm's request contains the program
   being repaired, so there is no visible caret to rewrite around. The A/B *result* it was
   attached to is unchanged. A correction is needed on the thread. COLLABORATION.md §2 — **a
   human reviews the wording and posts it** (Claude must not `gh issue comment`). The two
   repro paths given to Letz in that comment (`docker-verify`, `bench/issue26/`) were also
   found broken and are fixed as of this session (PF-080) — worth a line in the same
   correction rather than a separate follow-up. Then (deferred to a later session): edit the
   existing `issue-26-repro` release body **in place** to mark it superseded and repoint its
   Method link at the current pin; optionally cut a new non-prerelease `issue-26-repro-v2`.
   **Do not move the existing tag** (`f50daa8`).
2. **A listening pass on the interactive-session patches.** COLLABORATION.md §1 — whether a
   generated plugin *sounds like what was asked for* has no instrument and is not delegable.
   The render oracle proved the WP6 patches were not broken; it cannot tell you they were
   musical.
3. **A replacement Freesound API key.** *(PF-056.)* The configured key is sent and rejected
   (HTTP 403). Code cannot fix a revoked credential — get one from freesound.org.
4. **`bench/results/.prompt_baseline.json` is still untouched**, deliberately. It records
   `0.88` for the deleted pre-unification prompt; overwriting it is COLLABORATION.md §2
   territory.
5. **Three Phase-3 backlog plans are drafted, session-local, awaiting a fresh session.**
   `~/.claude/plans/phase3-pf032-silent-noise-gate.md`, `phase3-pf045-envelope-time-units.md`,
   `phase3-pf024-invalid-generation-families.md`. Each leads with a re-measurement WP: the
   prompt already contains the fix text for all three, and the open question is whether the
   shipping model obeys it.
6. **Branch + worktree cleanup — reconciled 2026-09-18, not yet performed.** `task/adr035-verification-loop`
   (`.worktrees/adr035-step6`) is gone — #78 merged and it was already cleaned up between
   rewrites. `fix/ember-console-palette` no longer exists locally either — resolved off-screen,
   nothing left to triage. Two items from the 09-10 list are unchanged and still need your
   call: `.worktrees/shell-command-bar` (ADR-036 WIP, still awaiting review) and
   `.worktrees/runtime-agnostic-workflow` (still **16 uncommitted files**, not re-verified
   this pass — **still needs your triage**). `.worktrees/codex-llm-generation-professional`
   and `.worktrees/main-session` unchanged, left alone.

   **Not inventoried above:** `.worktrees/efficacy-semantics` (branch
   `bench/efficacy-semantics-2026-09-23`) — the 2026-09-23/24 faust-rs + efficacy work,
   **now pushed and PR #94 open** (was local-only when this file was rewritten 2026-09-18).
   Add to the "DO NOT delete" list below once merged, remove once the PR lands.

   **Correction to "do not touch" below, 2026-09-24:** a point-in-time snapshot of
   `.worktrees/efficacy-pf031`'s uncommitted n≥3 checkpoint (38/125 cells,
   `efficacy_groq_n3_rep1_20260915.json` + one stray record) is **now also committed**, as-is
   and unscored, on `bench/efficacy-semantics-2026-09-23`
   (`bench/results/efficacy/efficacy_groq_n3_rep1_20260915.json` +
   `…PROVENANCE.md`) — a backup against the exact "somebody's mid-run, don't lose it" risk
   this section already named, done by *copying*, not moving. **The live worktree itself is
   untouched and the "do not touch" guidance below still stands** — whoever is mid-run there
   may have added cells beyond the 38 this snapshot captured; check `git status`/file size in
   `.worktrees/efficacy-pf031` before assuming the committed copy is now the current state.

   **Nine more worktrees are now stale-but-unremoved** — their branches merged 2026-09-12→15
   (PRs #82–#90, all in "Landed 2026-09-12 → 2026-09-15" above) and nobody has run
   `git worktree remove` on them yet: `.worktrees/f1-bipolar-knob`, `soundfetch-upgrade`,
   `pf-043-ollama-context`, `adr-039-040-on-device-orchestration`, `adr-041-face-visuals`,
   `pf032-gate-time-units`, `adr-reclassify`, `pf024-routing`, `adr041-step1`. Same
   destructive-action-classifier reasoning as the branch deletions below — an agent won't run
   the removal unprompted; flagging it here is the actual "still owed."

   **Do not touch — live work, discovered this pass:** `.worktrees/efficacy-pf031`
   (branch bench/efficacy-groq-n3-20260915) already has an uncommitted n≥3 checkpoint
   (`efficacy_groq_n3_rep1_20260915.json`) toward "Next three things" #1 below — somebody's
   mid-run. **Correction to this rewrite's first pass:** `.worktrees/pf032-remeasure`
   (`chore/pf032-remeasure-noise-gate`) was wrongly described here as uncommitted live work
   — it is not. `git status` in that worktree shows a clean tree, fully pushed to origin;
   its one commit (`182996d`) carries the *identical* commit message to already-merged PR
   #81's `7ba7d8c` ("bench: re-measure PF-032 noise gate — root cause was wrong (WP1)"), but
   `git diff 7ba7d8c 182996d` shows `182996d` is missing 391 lines `7ba7d8c` has in
   `docs/decisions.md` — it branched off an older point and never picked up what landed
   there since. **Not safe to protect as live work, and not safe to delete unreviewed
   either** — merging it as-is today would delete 391 lines of `docs/decisions.md`. Needs a
   human to look at it (rebase and re-check whether the WP1 content still differs from what
   #81 shipped, or discard) rather than sitting in either "protect" or "safe to delete."
   **Three more added today** by this session's own dispatch, all active:
   `.worktrees/truth-reconcile` (this rewrite), `.worktrees/e1-aot-emit` and
   `.worktrees/capture-harness` (the two peer-session assignments — see the artifact linked
   at the top of this file).

   **Still owed by a human:** `git push origin --delete` for the merged remote branches
   below — the destructive-action classifier blocks agents from doing it.
   `feat/provider-resilience` now has a pushed remote copy (`remotes/origin/feat/provider-resilience`)
   but still no PR (`gh pr list --head feat/provider-resilience` → empty) — still an
   unreviewed snapshot, just no longer local-only. Regenerate/verify the merged list with
   `gh pr list --state merged --json headRefName,number` before running any deletion.

   ```
   MERGED — safe to delete (superset of the 09-10 list, plus 09-12→15's nine):
     bench/issue26-repro-package               docs/status-pr55
     chore/dynamics-fix-verification           feat/archetype-layout
     chore/repair-ab-repro-naming              feat/dynamic-stdlib-retrieval
     docs/adr-036-amendment-adr-037            feat/generated-face-fonts
     docs/adr-038-face-export-resequence       feat/generated-face-knob
     docs/correct-stale-session-loaded-claims  feat/generated-face-lnf
     feat/recommendation-mvp                   feat/theme-validate
     feat/ui-face                              feat/ui-face-wire
     fix/issue26-integrity-2                   fix/issue26-makefile-smoke-score
     fix/pedal-layout-packing                  fix/provider-precheck-per-request
     issue26-integrity                         task/efficacy-groq-125
     fix/bipolar-knob-arc                      feat/soundfetch-0.4.0
     fix/pf-043-ollama-context                 docs/adr-039-040-on-device-orchestration
     docs/adr-041-face-visual-layer            fix/pf032-gate-time-units
     docs/adr-reclassify-routine               fix/pf024-named-mono-routing
     feat/adr041-step1-visual-region

   DO NOT delete:
     feat/provider-resilience        UNREVIEWED SNAPSHOT, now pushed — still needs review/merge
     feat/shell-command-bar          ADR-036 WIP
     research/llm-generation-professional   Codex research
     chore/runtime-agnostic-workflow        uncommitted refactor, pending triage
     bench/efficacy-groq-n3-20260915        LIVE — uncommitted n≥3 checkpoint, do not remove
     feat/e1-aot-emit                       dispatched today, active
     feat/interactive-capture-harness       dispatched today, active
     docs/truth-reconcile-2026-09-18        this rewrite
     bench/efficacy-semantics-2026-09-23    PR #94 open, not yet merged

   NEITHER — needs your call, not a mechanical delete or keep:
     chore/pf032-remeasure-noise-gate       clean, pushed, NOT uncommitted (corrected above);
                                             diverged from an older base than merged PR #81's
                                             7ba7d8c — a raw delete could be fine or could lose
                                             something #81 doesn't have; a raw merge would
                                             regress docs/decisions.md by 391 lines. Look before
                                             either.
   ```
7. **Untracked personal files left alone**, as always — the two notes at the repo root, the
   unshipped brief skill, the product-architecture draft under bench/. The
   `design_handoff_generated_plugin_faces/` bundle from the last rewrite is **resolved,
   confirmed this session** — it was distilled into `docs/design/incant-ui/` in PR #56 (a
   dated, read-only point-in-time record per COLLABORATION.md §8) and the original bundle
   directory no longer exists on disk. Nothing left to commit there.
