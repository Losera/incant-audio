# PluginForge — Status  (2026-10-26)

Rewritten each session per COLLABORATION.md §5. Single writer, no merge conflicts.
Narrative history lives in git and in `docs/sessions/`.

**2026-10-26 — full reconciliation, prompted by `/orient` being wrong.** This file was
stamped 2026-10-10; **eleven PRs merged after it** (#102–#113) and nothing here reflected
them. A session opened on the digest, believed it, and had to re-derive the project's real
state from scratch. Three separate truth defects were found, two of them in the controls
that are supposed to prevent exactly this:

1. **The "one number" was silently zero.** `tools/check.sh assumed` counts the Assumed
   section with `^\s*[-*]\s+\*\*` — bold **bullets** — while this file wrote its claims as
   bold **paragraphs**. Measured: `Assumed, never checked: 0 claims`, exit 0, with the
   section 3,044 characters long and holding a live `high`/`open` claim (PF-079). Three
   compounding failures: exit 0 reads as a triumph; no `DIGEST INCOMPLETE` fires because
   the *heading* is found; and `tools/status_digest.sh:276` gives the Broken block an
   empty-items tripwire while the Assumed block has none — the one section CLAUDE.md calls
   "the one number" was the only one without a guard. The sole test touching it
   (`tests/test_control_wiring.py:1266`) asserts the heading string, never a count.
   **Fixed by this rewrite** (every claim below is a bullet) **and by PR #114**, which adds
   a cross-check against `health_report._check_assumed_drift()` that exits 1 on drift.
2. **The staleness banner understates the gap, structurally.**
   `tools/status_digest.sh:243-247` runs `git log -1` with **no revision**, so it measures
   *local HEAD* — never `origin/main`, never today's date. On a branch 28 commits behind it
   printed "8 day(s)" for what was a 16-day, 11-PR gap: the further behind you are, the
   smaller the warning. And `:251`'s `if head and head > stamped` prints **nothing** when
   HEAD is older, violating the file's own `:34-35` rule that "silence is the one forbidden
   output." Not fixed — recorded in "Assumed" and "Waiting on you."
3. **Two `high`/`open` defects were invisible to the digest.** PF-079 and PF-081 sat in the
   Broken *preamble prose* rather than as numbered items, so the digest's `**N. ...**`
   regex never surfaced them; a reader saw 12 items and missed two highs. The ranked list
   also contained two items numbered **11**. Both fixed below.

Also folded in: **PR #114's CI was red for five days** on two `test_generation_lane.py`
tests (that file is new on #114's branch, not on main) that reached the real Faust toolchain, which CI's `test` job deliberately does not
install — green locally, `assert 0 == 1` remotely. Fixed 2026-10-26 (`5cae65f`, CI run
40431110271 green). This is the **second recorded instance** of that failure mode:
`tests/test_control_wiring.py:1747-1762` already carries a comment recording it from CI run
30687159048, generalized nowhere. Per this project's "a control counts only once it has been
seen failing" rule, that is logged here so a third instance is less likely — not to imply it
is now impossible. There is still no control asserting that a test importing `render_oracle`
carries a toolchain guard.

**2026-10-10 — targeted reconciliation (PRs #95–#98).** PF-082 fixed (PR #96, editor
replays a processor-held snapshot on window reopen; `EditorSessionTest` scenario 58 is the
red-then-green proof), with PF-083 and PF-084 filed as adjacent lifetime bugs. The health
schema landed (PR #97) — `tools/health_report.py` gained a fourth `product` lane wired into
`check.sh full`; see `docs/HEALTH_SCHEMA.md`. ADR-043 (model routing) and ADR-044 (GUI
generation) drafted, both `Proposed`, neither decided (PR #98). A sibling session corrected
the faust-rs study (PR #95): PF-076's published *mechanism* was withdrawn — the A/B result
stands, the caret-anchoring explanation does not, because neither arm's request ever
contains the program being repaired — and a new finding was added (faust-rs's own
repair-suggestion list is stripped from 87% of `syntax`-class feedback).

**2026-09-18 — full rewrite** reconciled against `origin/main` and `docs/BUGS.md`, after an
adversarial review found three truth defects here and one in a published artifact. Detail in
`git log`; the specific corrections are no longer load-bearing.

**Start a session with `/orient`**, not by reading this file top to bottom — but read its
output critically. The digest reports; it does not verify, and in 2026-10 it was wrong about
the defect list, the worktree inventory, the "one number", and the size of its own staleness
gap. `/brief` is the heavier cold-re-entry read.

---

## Works — and how we know

One line per capability, each naming its evidence. "Builds clean" is not a capability.

**Landed 2026-10-13 → 2026-10-21** (PRs #102–#113; **#111 never existed** — `gh pr view 111`
resolves to nothing). Added 2026-10-26, having gone unrecorded here for up to thirteen days:

- **PR #102** (2026-10-13) — ADR-039 step 1: typed terminal provider exceptions.
  `llm/providers.py` gains `QuotaExhausted(RateLimited)` and
  `ProviderUnavailable(RuntimeError)`, raised from `_post_with_backoff` on a daily-quota
  429 and on backoff exhaustion; 2 new tests. *Naming drift worth knowing:* ADR-039
  (`docs/decisions.md:2702-2706`) specifies `QuotaExceeded`; the code says
  `QuotaExhausted`. ADR-039 is still `Proposed`, but its own step-1 text authorizes landing
  this alone.
- **PR #103** — PF-083 fixed. Proof: `EditorSessionTest.cpp:4372` scenario 59, red→green.
- **PR #104** — PF-079's two root-caused idioms: oracle red cases `runaway_expander` and
  `inverted_limiter` land *first* (`be5515f`), then the prompt fix (`ff7ec5b`), then 6 live
  groq generations 6/6 render-safe. A real red-then-green on a generation defect.
- **PR #105** — ADR-038 **E2**: `tools/export_repo.py` AOT-compiles `Patch.dsp` through the
  real `faust -lang cpp`, vendors 6 headers, derives `PLUGIN_CODE` instead of hardcoding
  `Pfh1`, and `processBlock` calls `dsp.compute()`. The exported plugin is no longer a
  passthrough. **Caveats:** the commit body says "Verification owed next" — it landed
  unverified — and the follow-up skip-guards 10 of 13 test items behind
  `shutil.which("faust")`, so the E2 proof does not run in CI's `test` job. An exported
  *instrument* still makes no sound from MIDI (PF-053).
- **PR #106** — two public overclaims corrected: README "polyphonic" → "monophonic"
  (verified against `FaustEngine.cpp:549-562`, `FaustEngine.h:374`), and ADR-001's
  "~30 MB added to binary size" replaced with a dated correction (libfaust is a shared
  library, `host/CMakeLists.txt:106-110`).
- **PR #107** — PF-038 `open`→`wontfix`, PF-039 `open`→`fixed` in `docs/BUGS.md`.
- **PR #108** — ADR-038 **F2 + F3**: `WidgetKind::ChipRow` and `class ChipRow`
  (`ParamGridPanel.h:77,301`), a `drawToggleButton` override, and column height-balancing
  via `rowsNaturalFor()`. Proof tests `EditorSessionTest.cpp:4412` scenario 60 — which
  asserts a chip click moves the **real APVTS slot** to 1.0, not just widget state — and
  `ParamGridLayoutTest.cpp:365` `testColumnsBalanceHeight`. F3's own commit self-declared
  "NOT been built or tested"; the debt was paid at merge `4c6189a` (515 checks 0 failures;
  3006/3006).
- **PR #109** — a `juce::FileLogger` at `~/.config/Incant-Audio/Logs/`, refcounted, gated
  to VST3/Standalone, 30-file prune, logging OutputGuard mute/unmute and LoadMode per
  compile. Honestly scoped as *observability*, not a PF-074 fix; verified by a live
  Standalone run, no automated test.
- **PR #110** — transplanted the publishable half of `research/market-fit-2026-10-12`:
  `bench/repro/pf074/` (5 files), `bench/results/oracle_20261014_session_kit.json`,
  `docs/competitive_landscape.md`, `bench/PLUGIN_PRODUCT_DEVELOPMENT_ARCHITECTURE.md`,
  `.claude/skills/brief/SKILL.md`. This, not `c402aa8`, is how PF-074's repro reached main.
- **PR #112** — PF-032 re-measured on groq and closed; see "Broken".
- **PR #113** — the `/pr-cleanup` skill.
- **2026-10-26** — PR #114's CI unblocked (`5cae65f`, run 40431110271 green): two
  `test_generation_lane.py` tests reached the Faust toolchain CI deliberately lacks.
  `score_rep` now takes an injectable analyser so the counting logic runs in CI, with one
  `skipif(which("faust2sndfile"))` end-to-end test keeping the fakes honest. Red case
  reproduced first behind a faust-free PATH.

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

Registry with IDs, severity, discovery dates: `docs/BUGS.md`. **When this list and that
registry disagree, neither is automatically right — read the code.** Two such disagreements
are named explicitly below rather than silently picked.

**1. The compile gate overstates real success by 25–40pp.** *(PF-079, high, open.)*
Promoted from preamble prose to a numbered item 2026-10-26 — it was `high`/`open` for a
month while being invisible to `/orient`'s digest, which only parses `**N. ...**` items.
`faust -lang cpp` accept/reject is the gate this whole ladder ranks against, and real
render-safety runs 25–40pp below it on the committed groq/ollama 125-cell archives
(`docs/records/efficacy-adversarial-review-2026-09-23.md` §B1). Two `dynamics` idioms fail
identically across two independent generator models, both root-caused against the stdlib:
an **upward expander's makeup gain has no ceiling** (`ba.linear2db` floors at −758.6 dB
single-precision, so gain follows the signal to +728.6 dB; measured +715 dB), and a
**brick-wall limiter's `select2` arms are inverted** (`select2(c,a,b)` returns `a` when `c`
is *false*, so every sample at or below threshold becomes the threshold constant; measured
DC 0.95–0.98). **Partially addressed 2026-10-20** (PR #104): both got oracle red cases
(`render_oracle.py:982,1000` / `:1053,:1067`) and a STRICT-RULES prompt clause, verified
6/6 render-safe on live groq. The *general* claim — that the gate overstates render-safety
— is untouched and the registry row is correctly still `open`.

**2. The repair loop never sees the program it is repairing.** *(PF-081, high, open.)*
Also promoted from prose 2026-10-26, same invisibility. The shipping Fresh-mode retry path
sends diagnostics without the source being fixed. This is what falsified PF-076's published
mechanism; ADR-043 scopes landing this *before* any routing change, and nothing in ADR-043
has landed.

**3. One generation defect is evidenced; the rest is sampling.** *(PF-024, high,
in-progress.)* Karplus-Strong's `recursion_cycle` fails to compile across every archive and
the one tier that compiles renders +79.6 dB runaway; the sidechain compressor fails every
run with a *different* error. `routing_arity` is the dominant first-attempt failure class
(22/44 error strings on the 125-cell grid). Two sub-patterns are mitigated, neither closes
it: the **dynamics `_,_)` arity pattern** (PR #75) — verified n=1 2026-09-09, the targeted
pattern went 8/25 → 0/25 and `routing_arity` 10/25 → 4/25, but net compile rate barely
moved (8/25 → 9/25 first-try) because failures rotated into `syntax` (6→9); and **named
mono fed into a stereo chain** (PR #89) — 2/5 cells clearly fixed, 1 ambiguous, 1 shifted,
1 unchanged. Both measured free-tier only; groq is the shipping model and is not measured
for either.

**4. The DAW still sees raw slots.** *(follow-up to PF-037, medium, open, unfiled.)* No
section grouping or titled cards in the host parameter view.
`PluginProcessor::createParameterLayout()` (`host/Source/PluginProcessor.cpp:205-238`)
emits 64 flat `AudioParameterFloat` slots named `"Macro " + String(i+1)`; there is no
`AudioProcessorParameterGroup` anywhere in that file.

**5. Export cannot make sound.** *(PF-053, medium, open.)* Absent from this list entirely
until 2026-10-26, despite PR #105 landing ADR-038 E2 against it this cycle. E2 now
AOT-compiles `Patch.dsp` via the real `faust -lang cpp` and `processBlock` calls
`dsp.compute()`, but PR #105 states plainly that an exported **instrument** compiles and
runs without making sound from MIDI — the voice mapping is not ported. See also the stale
gate text in "Waiting on you".

**6. `en.*` envelope time units on the frozen ladder record.** *(PF-045, medium, open,
deliberately not spent.)* Reproduces at L0 on the grid.

**7. Refine produced a degenerate 1-knob patch.** *(PF-072, medium, open, found
2026-08-28, still needs a captured repro.)* `groq`/`gpt-oss-120b`, "reverb with chorus" as
a refine over a working reverb → a single dry/wet knob where New mode gave a full patch.
Refine *mechanism* verified correct on `gemini` afterward — model variance, not routing.
**No repro artifact exists:** `bench/repro/` contains only `pf074/`.

**8. A generated instrument patch went NaN during play.** *(PF-074, medium, open, found
2026-08-28.)* **Repro captured** (PR #110, `bench/repro/pf074/`) and it falsified every
internal hypothesis — `OutputGuard` latched correctly. **Observability landed** (PR #109):
a `juce::FileLogger` at `~/.config/Incant-Audio/Logs/` records OutputGuard mute/unmute and
LoadMode per compile. What is still missing is a DAW capture taken *with* that logger
running — a human in a host, not more code. The repro surfaced PF-085 (below).

**9. Resonance/Q can be declared `min = 0`, dividing by zero.** *(PF-085, medium, open,
found 2026-10-14.)* New since the last rewrite, and prose-only until now. Generated patches
can declare `min = 0` on a resonance/Q parameter with no floor anywhere in prompt,
generator, or host: `ParamMap::mapSlotToZone` (`host/Source/ParamMap.h:130`) passes the
declared minimum to the Faust zone verbatim, and `fi.resonlp`'s `a1 = 1/Q`
(`/usr/share/faust/filters.lib:2185-2193`) divides by zero at the floor. Reproduced by
`bench/repro/pf074/sweep_driver.cpp`.

**10. `score_efficacy.py --judge` spends quota.** *(unfiled, medium, open.)* The lock half
is done — `acquire_lock()` / `release_lock()` at `bench/score_efficacy.py:612` / `:624`
*(citation corrected 2026-10-26; this list previously pointed at `:558,569`, which are
argparse definitions)*. The quota cost is the real remaining half: `run_judge` at `:505`,
`providers.make_generator` at `:524`.

**11. Semantic fidelity is unmeasured.** *(PF-013, medium, open.)* Absent from this list
until 2026-10-26 though `open` in the registry throughout. The judge half is closed; the
measurement is blocked on #10's quota cost.

**12. Audible discontinuity on the 2nd-generation DSP swap in a host.** *(PF-073, low,
open, found 2026-08-28, needs a captured repro.)* "Not a very smooth transition." No
`setParamValue` flood in the log. A brief recompile gap is expected; a click is not.

**13. A chord sounded like ~5 voices from a strictly-mono engine.** *(PF-075, low, open,
found 2026-08-28.)* Mono is confirmed by construction — `FaustEngine.h:374`
`int currentNote = -1`, last-note-priority at `FaustEngine.cpp:547-562` — so this is almost
certainly overlapping release tails plus reverb. The disambiguating held-chord test (all 5
sustain vs 4 decay) has still not been run. PR #106 corrected the README overclaim that
flowed from this ("polyphonic" → "monophonic").

**14. Two simultaneously-open editors race on one callback slot.** *(PF-084, low, open,
found 2026-10-09.)* A REAPER FX-chain editor plus a floating window, or two hosts embedding
one instance, both overwrite the processor's single-slot compile callbacks; only the
last-constructed editor updates. **Possibly sharpened by PR #103**, which makes
`~PluginForgeEditor` unconditionally null those three slots — so a second editor's
destruction can now clear a first editor's live callbacks. Reasoned from the diff; no test
covers the two-editor case.

**15. Bargraph/meter zones are discarded in ParamPool.** *(PF-052, medium, in-progress.)*
In this file's prose (the ADR-041 ladder) but never in the ranked list until now.

**16. `min_max_tokens` makes a per-call output budget unenforceable.** *(PF-035, low,
open.)* `bench/score_efficacy.py:465`. Absent from this list until 2026-10-26.

**17. MIDI-fidelity gaps in a real session.** *(triaged 2026-08-16, all pre-existing, none
a regression.)* Monophonic by design (deliberate; the mono *state* is
`FaustEngine.cpp:547-562` and `FaustEngine.h:374`), block-granularity MIDI (~10.7 ms
jitter, documented in-code), a hardcoded 2.0 s tail (`PluginProcessor.h:86`), no MIDI CC
mapping (the MIDI walk is `PluginProcessor.cpp:410,485-493` and handles `isNoteOn`/
`isNoteOff` only). *(All three citations corrected 2026-10-26 — the previous ones pointed
at `:519-524`, `:85`, and `:288-317`, the last of which is `isBusesLayoutSupported` and
`reset()`.)*

**Closed since the last rewrite (2026-10-20/21):**
- **PF-032** — the silent noise gate. `docs/BUGS.md` row `in-progress`→`fixed` (PR #112).
  Evidence is 6 live groq `openai/gpt-oss-120b` generations of "a noise gate with threshold
  and hold time", 6/6 render-oracle pass, thresh always a raw dB literal and hold always
  `*(0.001)`'d. Record: `docs/records/pf032-groq-remeasurement-2026-10-20.md`. **Scope
  caveat, stated by the commit itself:** n=6 on *one prompt phrasing*, no broader sweep, no
  instrument-profile path, and **no automated regression test** — the only mechanical
  artifact is `tests/test_bugs_registry_integrity.py` asserting the row reads `fixed`,
  which proves bookkeeping, not behavior. Re-checking it costs quota.
- **PF-083** — compile callbacks never cleared in the editor destructor (PR #103,
  `4eda8c3`). Proof test `host/tests/EditorSessionTest.cpp:4372`
  `scenario59_destructorClearsProcessorCallbacks`, registered `:4570`; red-then-green
  documented in the commit (3 failures, scenario 59 only → 0 failures, 507 checks).
  **Narrower than this file previously described it:** the fix clears dangling callbacks;
  it does not implement "the result is replayed when a new editor appears" — that was
  already handled by `lastPublishedParamsSnapshot` (`PluginProcessor.cpp:834`, written
  *before* the callback at `:874`), i.e. PF-082's mechanism. **The registry row in
  `docs/BUGS.md` still reads `open` and needs flipping.**
- **PF-038** — knob ordering is Faust's own. Marked **`wontfix`** in `docs/BUGS.md` by PR
  #107; it was still ranked open here as item 10. *Resolved 2026-10-26 — this was a direct
  STATUS.md/BUGS.md contradiction on `origin/main`.*
- **PF-039** — marked `fixed` by the same PR, citing `2d11758` (2026-08-14).
- **PF-079 (partial)** — the two root-caused `dynamics` idioms, PR #104. The general claim
  stays open as item 1.

Older closures (PF-065, PF-071, PF-076, PF-069/070, PF-063, PF-066/067, PF-043, PF-082,
PF-011, PF-077) are in `git log` and `docs/BUGS.md`.

**Unfiled:** a granular-family observation from the REAPER pass — `"pitch synchronous
granulizer"` routed to `granular_effect` and the family control gate
(`llm/generate.py:435-448`) rejected every retry for missing required control groups; folds
into PF-024's sampling, notes in `scratchpad/pf065-reaper-observations.md`.

---

## Assumed, never checked

The most important section (COLLABORATION.md §5). **Format contract:** every open claim is
a top-level bullet whose headline is bolded — `- **<claim>.**` — because that is what
`tools/check.sh assumed` and `tools/status_digest.sh` parse to produce the one number.
Unbulleted prose under a bullet is supporting evidence for that claim, not a second claim.
**A claim written as a bare bold paragraph is invisible to the count while being perfectly
visible to a human** — that is not hypothetical, it is what this section looked like until
2026-10-26, reporting 0 while holding a live `high` claim. Items move out of here only when
someone produces evidence, never by reformatting.

- **The compile gate was assumed to track real plugin success; checked 2026-09-23, and it
  doesn't.** *(PF-079, high, open.)* Overstates render-safety by 25–40pp on the committed
  archives. Two of its idioms are now fixed and oracle-guarded (PR #104); the general claim
  is unmeasured since.
- **The groq 125-cell efficacy grid answers the tier-gradient question but not the
  generalization one — n≥3 per cell is still owed.** *(PF-031.)* The committed archive
  `bench/results/efficacy/efficacy_groq_n3_rep1_20260915.json` holds **38 of 125 cells, rep
  1 only, 2 of 5 categories (filters 13, trivial 25), every cell n=1** — counted
  2026-10-26. It contributes nothing toward n≥3, and no newer rep file exists on any
  branch.
- **PF-032 is recorded `fixed` on the strength of six generations of one prompt phrasing.**
  Closed 2026-10-21 with no automated regression test; a phrasing sweep and the
  instrument-profile path were never run, and re-checking costs quota. The registry says
  `fixed`; the evidence supports "fixed for the measured phrasing."
- **Nothing asserts that a test reaching the Faust toolchain carries a guard.** Logged
  2026-10-26 after the second recorded instance of a test passing locally and failing CI
  for this reason (the first is recorded in a comment at
  `tests/test_control_wiring.py:1747-1762`, generalized nowhere). A sweep this session
  found the current blast radius is **2 tests, both now fixed**, and the other ~20
  toolchain-touching test files are correctly guarded — but that is a point-in-time audit,
  not a control. The next unguarded one will be caught by CI, not by the ladder.
- **`tools/status_digest.sh`'s staleness banner cannot measure the gap it reports.**
  `:243-247` runs `git log -1` with no revision, so it compares this file's stamp to *local
  HEAD* rather than `origin/main` or today's date, and `:251` prints nothing at all when
  HEAD is older than the stamp. Unfixed. How many past sessions were under-warned is
  unknown.
- **`bench/results/.prompt_baseline.json` records `0.88` for a prompt file that no longer
  exists.** Deliberately untouched — overwriting a committed measurement is
  COLLABORATION.md §2 territory. Every comparison against it is comparing to a deleted
  artifact.
- **CI's health on `origin/main` was never confirmed this cycle.** Eleven PRs merged
  2026-10-13→21; `gh run list` was queried for PR branches, never for `main`. PR #105's own
  commit body says "Verification owed next", and its follow-up then skip-guarded 10 of 13
  export test items off CI's `test` job — so the E2 proof tests do not run there.
- **F2/F3 and PF-083 are proven on their pre-merge branches, not as integrated.** The
  quoted runs (`EditorSessionTest` 515 checks, `ParamGridLayoutTest` 3006/3006) were on
  `4c6189a` before merge. Nothing has rebuilt those harnesses against `6c3adf3`.

**Benchmark staleness.** `system_prompt.txt` has changed repeatedly since the 125-cell grid
was measured (the dynamics few-shot, the `routing_arity` retry hint, PF-032's unit
contract, PR #104's STRICT-RULES clause). `efficacy_groq_20260831.json`'s **dynamics**
numbers are a pre-fix baseline; the other four categories are unaffected by those edits.

---

## Next three things

1. *(evidence)* **Re-run the groq efficacy grid at n≥3 (PF-031).** Still the top evidence
   priority and barely started: 38/125 cells, rep 1 only, every cell n=1 (counted this
   session). A real number needs all 125 × 3 with per-cell aggregation.
   `run_efficacy_study.py --provider groq` spends quota and groq's ~8k-TPM limit paces it
   to ~1 cell/90s, so it is a multi-day grind — resume-loop it. **Resume from the committed
   archive** `bench/results/efficacy/efficacy_groq_n3_rep1_20260915.json` plus its
   `.PROVENANCE.md`; `.worktrees/efficacy-pf031` no longer exists and any instruction
   pointing there is stale. Also owed: the makeup-gain follow-up — the dynamics few-shot
   stops at the compressor call and the model then writes `: *makeup`; showing
   `: par(i, 2, *(makeup))` needs ~120 chars of prompt headroom back.
2. **Take a DAW capture for PF-074 and PF-072.** The code half is done — the capture
   harness landed (PR #93, `bench/capture.py`, `PLUGINFORGE_CAPTURE=1`), PF-074's repro is
   committed (PR #110) and the FileLogger is installed (PR #109). What is missing is a
   human in a host with the logger running. PF-072 still has no repro artifact at all.
3. **Land PF-081 — give the repair loop the program it is repairing.** Highest-severity
   open defect with no work in flight, and ADR-043 explicitly scopes it *before* any
   routing change, so it blocks that ADR. It is also what falsified PF-076's published
   mechanism, which makes it the honest next move on the GRAME thread.

**Authorized, parallel, not blocking the Next three.** ADR-038: **F1, F2, F3 all landed**
(PRs #82, #108 — chip rows at `ParamGridPanel.h:77,301`, archetype geometry via
`rowsNaturalFor()`; proof tests `EditorSessionTest.cpp:4412` scenario 60 and
`ParamGridLayoutTest.cpp:365`). **E1 and E2 landed** (PRs #92, #105). **E3 is the live
step** — pluginval + a render check, then un-gate `/export`; note two of its three stated
requirements already shipped in E2 (see "Waiting on you"). E4 stays behind its own future
ADR. **ADR-041**: step 1 landed (`ArchetypeLayout::VisualRegion`, `Result::visuals`, and
PR #108 added a second producer in `split()`); **step 2 (`FaceContext` +
`TypefaceRegistry`) has not started** — `git grep FaceContext` on main hits only `STATUS.md`
and `docs/decisions.md`. Its pause trigger applies if it crowds out PF-024/PF-079.

**Displaced, not urgent.** A piano roll (requested, unplanned; needs a note grid *and* a
clock — no host transport in Standalone).

---

## Waiting on you

1. **Adjudicate two incompatible fixes for the same defect.** New and the most urgent item
   here. `tools/check.sh assumed` reporting 0 (see the preamble) has **two** fixes in
   flight that cannot both land. **PR #114** keeps the bullet regex, reformats this file to
   match, and cross-checks against `health_report._check_assumed_drift()`, failing loud on
   drift. **`feat/status-spine`** deletes the bullet regex and delegates to a new
   `status_parse.py` (new on that branch) that counts bold paragraphs — *and* rewrites
   `_check_assumed_drift()`'s return contract, dropping the `counter_value` /
   `bold_claims_in_prose` keys #114 depends on. **Landing status-spine after #114 would
   silently break `tools/check.sh assumed` again.** Overlapping files:
   `tests/test_control_wiring.py`, `tests/test_health_report.py`, `tools/check.sh`,
   `tools/health_report.py`, `tools/status_digest.sh`. This file now uses #114's bullet
   format, which is a de-facto vote — reverse it deliberately if you prefer status-spine's
   design. The branch was committed and pushed 2026-10-26 purely to stop it being lost (it
   had 12 uncommitted files, zero commits, no remote); it is labelled UNREVIEWED and its
   own verification was never finished.
2. **Review PR #114.** Only open PR, now green (`5cae65f`, CI run 40431110271). Carries the
   assumed-KPI fix, an integration report, a decision queue, an audit skill, and the $0
   local generation lane — plus this session's CI-parity fix. It is **24 commits behind
   main** and that was left deliberately; the merge is yours.
3. **Post the GRAME correction.** The 2026-09-08 reply (comment 4 on
   `Losera/incant-audio#26`, citing `704963a`) contains a mechanism claim now **verified
   false against source**: neither arm's request contains the program being repaired, so
   there is no caret to rewrite around. The A/B *result* is unchanged. Draft:
   `docs/records/issue26-correction-draft-2026-09-24.md` — **it predates PR #95 and needs
   two additions** before posting: faust-rs's repair-suggestion list stripped from 87% of
   `syntax`-class feedback, and the softened "51/51" numbers (the 36-program corpus is
   unrecoverable, only 15/51 re-derives; the "C++: 0/15 stable error codes" cell was a
   hard-coded `+= 0`, not a measured count). COLLABORATION.md §2 — **a human posts it**;
   Claude must not `gh issue comment`. PR #95 also drafted three narrower upstream issues
   (version pinning, `FRS-EVAL-0099` misdiagnosis, a contradictory cause-note), none filed.
   Deferred: repoint the `issue-26-repro` release body in place; **do not move the tag**
   (`f50daa8`).
4. **A listening pass.** COLLABORATION.md §1 — whether a generated plugin *sounds like what
   was asked for* has no instrument and is not delegable. The render oracle proves patches
   are not broken; it cannot tell you they are musical.
5. **A replacement Freesound API key.** *(PF-056, medium, open — also absent from the
   ranked list before 2026-10-26.)* The configured key is sent and rejected (HTTP 403).
   Code cannot fix a revoked credential.
6. **Flip PF-083's registry row.** `docs/BUGS.md` still reads `open` for a defect fixed and
   merged in PR #103. One-line docs change; flagged rather than done because this rewrite
   deliberately did not touch that file's conventions.
7. **Two documents assert things that are no longer true.**
   `.claude/skills/export/SKILL.md:3,5` still reads `## STUB — DO NOT RUN` and claims the
   exporter "emits a passthrough plugin that never links Faust or produces sound" — false
   since PR #105's E2; its requirements #1 and #2 both shipped and only #3 (makes sound) is
   unmet. And `tools/check.sh:63`'s step label reads "no faust" for a level that does
   invoke faust locally. Neither is fixed here.
8. **Worktree and branch cleanup — the prior inventory was wholly stale, this one is
   measured.** The ~34-branch "safe to delete" list that stood here is **already fully
   executed**: not one of those branches exists locally or remotely. Likewise every
   worktree it named is gone. Current reality — 13 worktrees, of which **8 are merged and
   just not removed**: `claim-corrections` (#106), `f2-f3-face-ladder` (#108),
   `pf032-groq-remeasure` (#112), `pf038-knob-order` (#110 — note the **directory name does
   not match its branch**, which is the market-fit-transplant one), `pf053-export-wire`
   (#105), `pf074-filelogger` (#109), `pf079-oracle-cases` (#104),
   `pf083-editor-callback-clear` (#103), `pr-cleanup-skill` (#113). Removal is destructive
   and stays a human call.

   **Needs your judgment, not a mechanical delete:** `feat/status-spine` (item 1 above);
   `feat/shell-command-bar` (1 commit, 86 behind, no PR, ADR-036 WIP "awaiting review"
   since ~Sep 7); `research/llm-generation-professional` (1 commit + a dirty `STATUS.md`,
   89 behind, no PR); `task/offline-eval-pilot` (unmerged, remote copy exists, no PR, no
   worktree, in no prior inventory).

   **A trap:** `.worktrees/soundfetch-correctness` is a worktree of a **different
   repository** (`/home/losera/soundfetch`) physically nested inside this one's
   `.worktrees/`. Any glob over `.worktrees/*` will produce a false positive on it.

9. **`research/market-fit-2026-10-12` has four commits whose content never landed.** PR
   #110 transplanted that branch's *artifacts* faithfully but left its *narration* behind:
   `9dea0b7`, `fa9cdf7`, and the `docs/BUGS.md` half of `c402aa8` (main's PF-074 row is
   still the pre-repro text and lacks the detail section entirely). `1541593` is unique but
   moot. The branch has **no upstream** — if that directory is lost, those commits are.
   This rewrite folds their *content* in rather than cherry-picking them, so the open
   question is only whether you want the commits themselves preserved. The ten
   `docs/research/R6-R14*.md` files are **intentionally** unlanded (`3eba049`, "not meant
   for a public repo") — do not "rescue" them.

10. **Untracked personal files left alone**, as always — the notes at the repo root, the
    product-architecture draft under `bench/`.
