# PluginForge — Bug Registry

**The durable, IDed source of truth for defects.**
Seeded 2026-07-23; reconciled against HEAD 2026-07-27. Read alongside `STATUS.md`,
`COLLABORATION.md`, `CLAUDE.md`. (The S5-lane ownership model and `docs/FLEET.md` are retired —
this file is now maintained by whoever is working.)

## Why this file exists
Before this, defects lived only as prose in `STATUS.md`'s "Broken — ranked" and "Assumed,
never checked" sections — which COLLABORATION.md §5 *rewrites* every session. Once a bug dropped
off the top-N list it survived only in git history: no stable ID, no cross-session record, no
way to say "PF-003 is the one we fixed in `d10f59e`." This registry is that record.

## How it relates to STATUS.md
- **BUGS.md is the durable, IDed source.** Every defect gets a permanent `PF-NNN` here and stays
  (as `fixed`/`wontfix`), it is never deleted.
- **STATUS.md "Broken — ranked" is the live top-N view** and should reference IDs (e.g.
  "Broken #1 → PF-002"). The two are synced by hand, and **they have drifted twice**: on
  2026-07-27 ten entries here said `open` while their fixes were live in the tree. When they
  disagree, believe neither — read the code at HEAD and fix both.
- IDs are assigned in discovery order and never reused.
- **A row flips to `fixed` only after someone reads the cited code at HEAD.** A commit message
  claiming a fix is not evidence; that assumption is what produced the drift above.

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
| PF-006 | Shutdown UAF on the editor's detached *generate* thread (raw `&proc`) | high | fixed | S2 Prompting UX | `PromptPanel.cpp:182,231` | 2026-07-21 | `18e862e` (2026-07-25) |
| PF-007 | Benchmark measured a prompt that diverged from production | high | fixed | S1 Backend | `bench/prompts/system_faust.txt` (deleted) | 2026-07-21 | prompt-unify (2026-07-21) |
| PF-008 | No generated plugin has ever been listened to (P6 audible battery unrun) | high | fixed | S4 Testing | `docs/p6_test_battery.md` | 2026-07-23 | ran 2026-07-24 (4 clean / 3 flaky / 7 fail) |
| PF-009 | Every benchmark number on record is void (measured on the deleted prompt) | medium | fixed | S4 Testing | `bench/results/results_20260728_groq.json` | 2026-07-23 | 22/25 measured 2026-07-28 |
| PF-010 | Prompt rewrite is unmeasured — verified *correct*, not *better* | medium | fixed | S4 Testing | `llm/prompts/system_prompt.txt` | 2026-07-23 | before/after measured 2026-07-28 |
| PF-011 | Efficacy pilot generalizes to nothing (N=50, 1 model, 2/5 categories) | medium | open | S4 Testing | `bench/run_efficacy_study.py` | 2026-07-23 | — |
| PF-012 | No cross-model comparison exists (ADR-008 "Under evaluation"). **Closed 2026-07-30: groq/gpt-oss-120b vs ollama/qwen2.5-coder:7b, 2 runs each, with error bars** | low | fixed | S4 Testing | `bench/results/results_20260730_*` | 2026-07-23 | pending commit |
| PF-013 | Semantic fidelity unmeasured. **Judge EXECUTED 2026-07-30 (44 graded, 0 errors) — that half closed; measurement now blocked on PF-041/PF-042, not quota** | medium | open | S4 Testing | `bench/score_efficacy.py` | 2026-07-23 | — |
| PF-014 | No real user prompt has ever been recorded (`generate.py` logs nothing) | low | fixed | S1 Backend | `llm/generate.py` `log_user_prompt` | 2026-07-23 | pending commit |
| PF-015 | `check_rt_safety.py` scopes only 2 functions; `pushToFaust` (now RT) uncovered | medium | fixed | S1 Backend | `.claude/hooks/check_rt_safety.py:57,65` | 2026-07-23 | `fed704e` (2026-07-26) |
| PF-016 | CI has never run green with the new prompt steps (5 unchecked Ubuntu-Faust TODOs) | medium | fixed | S4 Testing | `.github/workflows/test.yml` | 2026-07-23 | green `30181544354` (2026-07-26) |
| PF-017 | Stray `ParamPool::pushToFaust()` definition in `FaustEngine.cpp` | medium | fixed | S1 Backend | `FaustEngine.cpp` (removed) | 2026-07-16 | pre-history (see detail) |
| PF-018 | `FaustEngine::prepare()` does not re-init a live DSP on sample-rate change | medium | fixed | S1 Backend | `FaustEngine.cpp:154` | 2026-07-23 | `be83d1e` (2026-07-26) |
| PF-019 | Generation timeout cliff — 120s frozen UI under sustained groq use; one stalled/429'd POST eats the whole retry budget | high | fixed | S1 Backend | `providers.py:143-160,508`, `generate.py:76-85` | 2026-07-24 | `4bea5f3` (2026-07-25) |
| PF-020 | Cross-generation state contamination — no fresh/iterate mode; old APVTS values leak into new patches by slot index; headless never seeds defaults | high | fixed | S1 Backend / S2 UX | `PluginProcessor.h:58-73`, `PluginProcessor.cpp:166-170` | 2026-07-24 | `4a84c1c` (2026-07-25) |
| PF-021 | Stale error persists in PromptPanel across a new Generate (never cleared on submit) | medium | fixed | S2 Prompting UX | `PromptPanel.cpp:195-200` | 2026-07-24 | `18e862e` (2026-07-25) |
| PF-022 | `currentFaustSource`/`currentPrompt` committed before compile success — a failed generate poisons the source-of-record and any later save/restore | high | fixed | S1 Backend | `PluginProcessor.cpp:148,180-181` | 2026-07-24 | `4a84c1c` (2026-07-25) |
| PF-023 | `FaustEngine::process()` has no `activeDSP` null guard (latent audio-thread segfault; defense-in-depth) | medium | fixed | S1 Backend | `FaustEngine.cpp` `process()` | 2026-07-24 | `4a84c1c` (2026-07-25) |
| PF-024 | Generation produces invalid Faust for stereo routing / unbounded delays / ping-pong / artist-reference prompts (P6 #2,#6,#9,#10) | high | in-progress | S1 Backend | `llm/prompts/system_prompt.txt` | 2026-07-24 | `a4f942e` prompt-side; unmeasured |
| PF-025 | Benchmark harness has no concurrency guard and overwrites `results.json` unconditionally — two runs destroy each other's evidence and share one rate limit | high | fixed | S4 Testing | `bench/run_benchmark.py:32-115,296-322` | 2026-07-27 | pending commit |
| PF-026 | CI red on four consecutive pushes and no artifact in the loop reported it — the digest, the Broken list and `check.sh` were all silent | high | fixed | S4 Testing | `tools/status_digest.sh` | 2026-07-28 | pending commit |
| PF-027 | `OfflineRenderTest` dies with SIGILL (exit 132) on the CI runner — missing MessageManager. **Its "not the CPU" conclusion was wrong; see PF-036** | high | fixed | S4 Testing | `host/tests/OfflineRenderTest.cpp` `main()` | 2026-07-28 | `144e023` (green run `30409357504`) |
| PF-028 | COLLABORATION.md §7's hook table named two hooks retired six days earlier and omitted the one that was running | medium | fixed | S4 Testing | `COLLABORATION.md` §7 | 2026-07-28 | pending commit |
| PF-029 | `tools/check.sh` never builds or runs `OfflineRenderTest` or `PromptPanelThreadingTest` — CI is the only thing that does | high | fixed | S4 Testing | `tools/check.sh` `level_full` | 2026-07-28 | `558ac96` |
| PF-030 | `run_efficacy_study.py` takes no PF-025 lock — it can run concurrently with `run_benchmark.py` and share one free-tier rate limit | medium | fixed | S4 Testing | `bench/run_efficacy_study.py:299-322` | 2026-07-28 | `e867483` (2026-07-29) |
| PF-031 | The 25-prompt benchmark's noise floor is unmeasured — no delta can be called significant. **Measured: 4pp rate spread, and only 1 of 5 failing prompts reproduces its class** | medium | fixed | S4 Testing | `bench/run_benchmark.py` | 2026-07-28 | pending commit |
| PF-033 | Reopening a saved project resets every knob to the patch defaults — the editor's seeding overwrites the restore | high | fixed | S3 Plugin UX | `ParamGridPanel.cpp` `refreshParamKnobs` | 2026-07-28 | `81fc75b` |
| PF-034 | `EditorSessionTest` scenario 6 raced the message thread — green locally, red on the runner | medium | fixed | S4 Testing | `host/tests/EditorSessionTest.cpp` `loadAndSettle` | 2026-07-28 | pending commit |
| PF-032 | 2 of 22 compiling patches render SILENT — a warm lowpass and a noise gate. **Measured 07-30: the lowpass (Hz contract) is fixed; the noise gate is not** | high | in-progress | S1 Backend | `llm/prompts/system_prompt.txt` | 2026-07-28 | lowpass closed `a4f942e`; gate open |
| PF-035 | `min_max_tokens` makes a per-call output budget unenforceable — the judge asks for 300 and silently gets 4096 | low | open | S4 Testing | `bench/score_efficacy.py:465`, `providers.py` `make_generator` | 2026-07-29 | — |
| PF-036 | libfaust's JIT emits AVX-512 on CI runners that name the ISA but cannot execute it — SIGILL in `computemydsp`. It **was** the CPU; PF-027 closed that hypothesis wrongly | high | fixed | S4 Testing | `host/tools/pf_cpu_shim.cpp`, `.github/workflows/test.yml` | 2026-07-30 | pending commit |
| PF-037 | Every parameter displays as a raw 0–1 slot number — 800 Hz reads `0.04`. `ParamMap` denormalizes into the DSP and nothing denormalizes for the display | medium | fixed | S3 Plugin UX | `ParamMap.h` `formatZone`, `ParamGridPanel.cpp` `applyPresentation` | 2026-07-28 | pending commit |
| PF-038 | Knobs appear alphabetically, not in declaration order — a 40-param patch lists `P0, P1, P10, P11 … P2` | low | open | S3 Plugin UX | `ParamGridPanel.cpp` `refreshParamKnobs` | 2026-07-28 | — |
| PF-039 | The rotary fallback in `refreshParamKnobs` is unreachable dead code; `docs/ui_design_plan.md` still describes it as the fallback widget | low | open | S3 Plugin UX | `ParamGridPanel.cpp`, `docs/ui_design_plan.md` | 2026-07-28 | — |
| PF-040 | Every macro slot was quantised to 100 positions — JUCE's `AudioParameterFloat` min/max convenience ctor hardcodes `interval 0.01`, so a patch default usually could not be represented (800 Hz became 819 Hz) | high | fixed | S1 Backend | `PluginProcessor.cpp` `createParameterLayout` | 2026-07-30 | pending commit |
| PF-041 | ~~The semantic judge grades L4 against a ground truth **byte-identical to the L4 generation prompt** (10/10), so L4 scores 2.00/2.00 tautologically and the tier gradient is confounded~~ | high | fixed | S4 Testing | `bench/score_efficacy.py` `JUDGE_RUBRIC`, `run_judge`, `bench/prompts/acceptance_specs.json` | 2026-07-30 | 2026-08-16 |
| PF-042 | ~~The judge's 0/1/2 rubric collapses to binary in practice — score `1` used **once in 44** gradings, so "partially implements" is not a category the instrument actually returns~~ | medium | fixed | S4 Testing | `bench/score_efficacy.py:458-490` | 2026-07-30 | 2026-08-16 |
| PF-043 | ollama's stock 4096-token context cannot hold the ~3.3k system prompt plus the 4096 output floor (PF-035), leaving ~480 tokens of generation headroom on the repo's own declared default model | medium | open | S4 Testing | `llm/providers.py:330-343` | 2026-07-30 | — |
| PF-044 | `run_benchmark.py` recorded `provider` but never `model`, so a cross-model study could not identify its own subject from its own archives — while `model_for()`'s docstring says numbers are "only comparable per model" | medium | fixed | S4 Testing | `bench/run_benchmark.py:267` | 2026-07-30 | pending commit |
| PF-045 | Generated envelopes convert ms→samples for `en.*`, whose time arguments are in **seconds** — a 1000 ms release becomes 48000 s, so the patch holds sustain forever as DC | medium | open | S2 Prompt | `bench/results/results.json` (sawtooth+ADSR record) | 2026-07-31 | — |
| PF-046 | `check.sh audio` gates on `bench/results/results.json`, which every benchmark run overwrites — so the level goes red on whatever the model last happened to emit, not on the change under test | medium | fixed | S4 Testing | `tools/check.sh` `level_audio`, `bench/ladder_corpus.json` | 2026-07-31 | `24e6064` |
| PF-047 | `scenario13_styleSwitchDoesNotThrash` was defined and never called; the control-style feature's only test did not run, and every gate was green | medium | fixed | S3 Plugin UX | `host/tests/EditorSessionTest.cpp:971` | 2026-07-31 | pending commit |
| PF-048 | `UiDesignGallery` printed each record's `groups:` line before its own header, so the design loop's console attributed every fixture's group structure to the previous record | low | fixed | S3 Plugin UX | `host/tests/UiDesignGallery.cpp:229` | 2026-07-31 | pending commit |
| PF-049 | No arity guard anywhere: `FaustEngine::process` passed JUCE's null-terminated channel array to `dsp->compute` unchecked, so a patch declaring >2 channels dereferenced `io[2] == nullptr` **on the audio thread** (and read out of bounds from 4 outputs up) | critical | fixed | S1 Backend | `FaustEngine.cpp` `process`/`runCompile` | 2026-07-31 | `6a3f5e7` |
| PF-050 | A mono patch reached one channel only — `process()` wrote `io[0]` and left channel 1 holding the untouched dry input, so a generated oscillator played left with the dry signal still on the right | high | fixed | S1 Backend | `FaustEngine.cpp` `process`, scratch sized in `prepare` | 2026-07-31 | `6a3f5e7` |
| PF-051 | A patch declaring more than 64 controls lost the surplus **silently** — `remap()` looped to `POOL_SIZE` and never read `params[64+]`, and `pushToFaust`'s `min(infos.size(), slots.size())` hid it again. 70 controls compiled, 6 were unreachable, nothing said so | medium | fixed | S1 Backend | `ParamPool.cpp` `remap`, `RemapResult::overflowed` | 2026-08-01 | identity-keyed remap (2026-08-02) |
| PF-052 | Faust `hbargraph`/`vbargraph` were **dropped at capture** — `ParamCapture::addHorizontalBargraph`/`addVerticalBargraph` were empty function bodies, so a patch publishing a level meter, gain-reduction readout or envelope follower published *nothing*. No metering could exist anywhere in the product regardless of UI work | medium | in-progress | S3 Plugin UX | `FaustEngine.cpp:150-151` (was) | 2026-08-01 | capture closed 2026-08-02 (`Kind::Meter`); **rendering still absent** |
| PF-053 | `tools/export_repo.py` emits an export that cannot make sound: its invalid generated C++ bool expressions and doubled editor braces were corrected 2026-08-12, with effect/instrument shared-code compilation covered by `tests/test_export_repo.py`, but `processBlock` remains a passthrough stub that never loads or invokes `Patch.dsp` (`:98-103`). Session 008's "✅ manually verified" for Phase 2 is false | medium | open | S1 Backend | `tools/export_repo.py:98-159`, `tests/test_export_repo.py` | 2026-08-06 | `/export` remains gated by `.claude/skills/export/SKILL.md` until an exported plugin builds, loads, and makes sound; the ✅ in `docs/sessions/008-vision-architecture.md` was corrected 2026-08-06 |
| PF-060 | `preflight_prior_source()` took no `provider` argument — every Add-mode refine, on EVERY provider, was gated by groq's fixed 8,000-token rate limit regardless of what the selected provider could actually hold. In surgical mode this is a hard refusal, so Add was silently broken on every large-context provider | high | fixed | S1 Backend | `providers.py:142,152` (was), `generate.py:520` | 2026-08-13 | this session |
| PF-054 | Sample search resolved the bare name `"soundfetch"`, never on PATH on this machine (only ever installed inside venvs). `juce::ChildProcess::start()` returns true even when `execvp()` fails in the forked child, so the friendly "unavailable" message was dead code — every query, both providers, reported "Soundfetch returned no JSON." instead | critical | fixed | S1 Backend | `SoundfetchClient.cpp` `commandPrefix`/`run` | 2026-08-13 | `0694a32` |
| PF-055 | Sample search merged soundfetch's stderr (unconditional INFO logging, Internet-Archive `"archive metadata: n/50"` progress) into the same pipe as stdout via JUCE's default `ChildProcess::start()` flags, corrupting the JSON for the DEFAULT provider even once PF-054 was fixed | critical | fixed | S1 Backend | `SoundfetchClient.cpp` `run` | 2026-08-13 | `0694a32` |
| PF-056 | The configured Freesound API key is sent but rejected (HTTP 403). Not a code defect — needs a replacement key from freesound.org — but zero code anywhere surfaced *which* provider or *why*; a caller only ever saw "no JSON" (PF-054) masking this underneath | medium | open | S1 Backend | `SoundfetchClient.cpp` | 2026-08-13 | needs a new key; PF-054/PF-055 fixes make the real 403 message reach the user once this is resolved externally |
| PF-057 | `KeyboardPanel`'s constructor called `setPlayable(false)`, but the member initializer already reads `false` and `setPlayable` early-returns on an unchanged value — the widget-disabling code (dim, disabled label) never ran on construction. A fresh editor's keyboard looked and felt fully playable while every note was silently discarded downstream | high | fixed | S3 Plugin UX | `KeyboardPanel.cpp` ctor / `applyPlayableVisuals` | 2026-08-13 | `410770b` |
| PF-058 | Auto family resolution could route a prompt naming both generator-family language ("drone", "generative") AND a synth ("a generative synth") to the mute `generator` family — kind instrument, zero MIDI voice contract by design — silently, with nothing telling the user their "synth" request became an unplayable drone | medium | fixed | S2 Prompting UX | `generation_profiles.py` `resolve`; `GenerationProfiles.generated.h` `resolveAuto` | 2026-08-13 | `addfd57` |
| PF-059 | `generate.py`'s voice-contract gate lowercased UI labels before the synth/drum_synth membership check, so `hslider("Freq", ...)` (or any other-cased spelling) passed generation validation while `FaustEngine::extractVoiceControls`'s exact-case match silently refused to recognise it — a "successful" generation with a dead keyboard | high | fixed | S1 Backend | `generate.py` `_validate_profile_metadata`; `voice_contract.py` | 2026-08-13 | `2b8d4e3` |
| PF-061 | QWERTY notes: `keyStateChanged` forwarded key-up events while a `juce::TextEditor` held focus (JUCE swallows key-DOWN but not key-UP there) and `MidiKeyboardComponent::keyStateChanged` re-polls every mapped key on every call — so fast-typing rollover could fire a spurious note; separately, QWERTY did not work after an instrument generation without first clicking the on-screen piano | medium | fixed | S3 Plugin UX | `host/Source/KeyboardPanel.cpp` `keyStateChanged` / `focusForPlaying`, `host/Source/PluginEditor.cpp` `onFaustCompileSuccess` | 2026-08-15 | `dcf0af5` (2026-08-15) |
| PF-062 | `processBlock`'s pre-generation early-return path (`enterAudio()` returns false, no patch loaded yet) left the output buffer untouched on the assumption it held "the host's real input" — true for the Fx target, false for the Synth target, which has no input bus at all. A freshly-loaded, never-generated `PluginForge Synth` echoed back whatever memory the host/JUCE last left in that buffer: `pluginval --strictness-level 5` found literal NaN and subnormal output, 200/450 Audio-processing sub-tests failing | high | fixed | S1 Backend | `PluginProcessor.cpp:251-273` | 2026-08-16 | 2026-08-16, same session |
| PF-063 | `/orient` warned that green CI was behind HEAD but counted every merged side-branch commit, so its distance disagreed with the first-parent branch history and its own regression test | low | fixed | S4 Testing | `tools/status_digest.sh` `commit_line` | 2026-08-16 | `c9a1c7a` (2026-08-17) |
| PF-064 | The efficacy harness passed no generation budget, so a daily-quota `Retry-After` could suspend the process for hours before incremental results reached a recoverable checkpoint | medium | fixed | S4 Testing | `bench/run_efficacy_study.py` | 2026-08-17 | `8c0d724` |
| PF-065 | Generation fails with "generate.py not found" when the plugin runs as an installed VST3 (confirmed in REAPER) — ADR-011 marks this row Closed and it is not. **Fixed across PR #42 (config-file source), #43 (picker), #45 (install-layout: venv + seeded `.env` + merged `config.json` with `generate_script_path` + `python_path`; `resolvePythonExe()`; "Paths…" callout). Clean-machine REAPER rehearsal PASSED 2026-09-01** — `package_release.sh` → tarball → `install.sh` into a scratch `HOME` → REAPER launched against it, **no `PLUGINFORGE_*`, no `config.json` hand-edit** → working effect generated, provider `ollama`, runtime resolved via the installed `config.json` + venv | high | fixed | S1 Backend | `host/Source/PromptPanel.cpp` (`resolveGenerateScript` / `resolvePythonExe`), `tools/install_release.sh` | 2026-08-19 | 2026-09-01 |
| PF-066 | `EditorSessionTest` scenario 20's octave-hit-test assertion is stale against the instrument-conditional keyboard band: `KeyboardPanel::resized()` never runs for a fresh/non-instrument editor, so the octave buttons it positions stay at default zero-size bounds | low | fixed | S3 Plugin UX | `host/Source/PluginEditor.cpp:648-654`, `host/tests/EditorSessionTest.cpp:1560-1561` | 2026-08-25 | this session |
| PF-067 | `requirements.txt` pins `anthropic>=0.40.0` with no upper bound; `anthropic` 1.0.0 (released after the 2026-08-20 last-green CI run) ships `httpx2` instead of `httpx`, so every `import httpx` in `llm/providers.py` fails — breaks real generation, not just tests, anywhere a fresh `pip install -r requirements.txt` resolves 1.0.0 | critical | fixed | S1 Backend | `requirements.txt:1`, `llm/providers.py:56` | 2026-08-25 | this session |
| PF-068 | Auto family selection silently stopped working from the second keystroke onward: `ComboBox::getSelectedId()` returns 0 once the displayed label desyncs from the stored item text, which `updateAutoFamilyLabel()`'s per-keystroke `changeItemText(1, ...)` call always caused — `selectedFamilyId()` then read "effect"/"synth" instead of "auto", skipping `resolveAuto()` entirely | high | fixed | S2 Prompting UX | `PromptPanel.cpp` `updateAutoFamilyLabel`; `/home/losera/JUCE/modules/juce_gui_basics/widgets/juce_ComboBox.cpp:250-256,133-139` | 2026-08-25 | this session |
| PF-069 | `bench/run_efficacy_study.py`'s `GENERATION_BUDGET_S` is hardcoded at 140.0 with no env override (unlike `llm/generate.py`'s `PLUGINFORGE_GENERATION_BUDGET`). Per-attempt cap is `140/3 ≈ 47s`; a slow provider (7B-on-CPU ollama regularly takes 60–90s) times out every retry, the failure is classified `transport`, and the checkpoint-and-stop logic halts the whole run on the first one — the study cannot complete on any slow provider without a source edit | medium | fixed | S4 Testing | `bench/run_efficacy_study.py:83` (`_DEFAULT_GENERATION_BUDGET_S`), `:154-168` (`generation_budget_s`/`make_generation_budget`) | 2026-08-29 | this session |
| PF-070 | `bench/run_benchmark.py::validate_faust` runs `faust -lang cpp` with `timeout=30` but never catches `subprocess.TimeoutExpired`. A generated program the C++ compiler does not terminate on within 30s (endless-evaluation-cycle shapes, deep recursion — a 7B model produces these) raises the exception uncaught out of `run_study`, **crashing the entire efficacy run**. A resume then re-hits the same cell and crashes again — an infinite loop. A compiler hang is a validation failure, not an exception | high | fixed | S4 Testing | `bench/run_benchmark.py:230-254` (`validate_faust`, `except subprocess.TimeoutExpired` at `:247`) | 2026-08-29 | this session |
| PF-071 | The XDG-installed Python runtime (`~/.local/share/pluginforge/llm/`, the PF-065 fix's step-3 resolution target) is a **stale, unconfigured trap**: it is a 2026-08-15 copy where `providers.py` still has `DEFAULT_PROVIDER = "anthropic"` (predates the groq default) and there is no `.env` beside it. When a DAW launched from a desktop launcher loads the plugin (no `PLUGINFORGE_LLM_SCRIPT`, `~/.vst3` bundle so the upward walk misses the repo), resolution lands here → unset provider resolves to the **paid** provider → `PaidProviderError` surfaces as an "anthropic provider error". PF-065's "partial fix" traded an honest "generate.py not found" for a silent-wrong 6-week-old runtime. **Fixed on the same PR chain as PF-065 (#42/#43/#45) and closed on the same clean-machine REAPER pass 2026-09-01**: `install_release.sh` now *creates* `~/.config/pluginforge/config.json` (pointing at a fresh venv + `generate.py`) and `resolveGenerateScript()` consults it before the XDG step, so a launcher-started DAW resolves the installed runtime, never the stale XDG copy. Verified provider on the pass was `ollama` (free/local), not the paid fallback | high | fixed | S1 Backend | `host/Source/PromptPanel.cpp` (`resolveGenerateScript`), `tools/install_release.sh` | 2026-08-28 | 2026-09-01 |
| PF-072 | Refine ("Add" / "Redo") mode produced a **degenerate single dry/wet knob** patch where the same prompt in "New" mode produced a full patch with all the expected controls. Observed once in-host (session 017 WP6), `groq` / `gpt-oss-120b`, prompt "a warm analog reverb with chorus" as a refine over a working reverb. The refine *mechanism* verified correct on `gemini` afterward (surgical Add kept all 6 prior controls + added the 2 chorus controls), so this is generation quality / model variance, not a routing bug — but a 1-knob result is a broken user experience regardless of cause | medium | open | S2 Prompting UX | `llm/generate.py` refine path; `llm/prompts/system_prompt.txt` | 2026-08-28 | — |
| PF-073 | Audible discontinuity when a 2nd generation swaps the live DSP in a host — the operator reported "there didn't seem to be a very smooth transition between the generates." Not characterised (click vs. gap vs. dropout length; no `setParamValue` flood seen). The swap protocol (`audioBusy` drain guard + compile-callback-before-`ready`) is a load-bearing invariant; a brief gap during recompile is expected, an audible click is not. Needs a captured repro to tell which | low | open | S1 Backend | `host/Source/FaustEngine.cpp` swap path, `docs/fixplan_pushtofaust_swap.md` | 2026-08-28 | — |
| PF-074 | A generated **instrument patch produced NaN/Inf during normal play**, forcing a regeneration to recover. `OutputGuard` is designed to catch non-finite output and **latch-mute even for instruments** (`OutputGuard.h:89`, "NonFinite always latches regardless of policy"), so either it caught it (silence, regen cleared the latch) or it did not (a real hole). The operator saw "NaN/inf" — where (REAPER meter? the plugin?) and whether audio went silent or stayed broken was not captured. `OfflineSynthRenderTest` (184 checks, 0 failures) shows the synth audio path + OutputGuard are healthy on the tested fixtures, so this is a specific generated patch under a specific runtime condition. Needs the triggering patch + action captured | medium | open | S1 Backend | `host/Source/OutputGuard.{h,cpp}`, `host/Source/FaustEngine.cpp` | 2026-08-28 | — |
| PF-075 | Instrument polyphony: `FaustEngine` is **strictly monophonic** (one `currentNote`, one gate, last-note priority, no per-voice cloning — `FaustEngine.cpp:547-562`), but the operator playing a chord in-host heard "more than 3 voices, up to 5". Most likely overlapping **release tails + reverb** from a mono voice (each note gated off when the next arrives, but its envelope/reverb keeps ringing) — which would confirm the mono model and be nice musical behaviour. The disambiguating test — hold a sustained 5-note chord: do all 5 sustain, or do 4 decay leaving 1 — has not been run | low | open | S3 Plugin UX | `host/Source/FaustEngine.cpp:547-562` | 2026-08-28 | — |
| PF-076 | **faust-rs diagnostics fed to the repair loop *reduce* its success rate** (issue #26). A/B over 202 C++-rejected Faust programs, repaired from the identical start with raw C++ stderr vs `faust-rs --check` rendered: repaired-within-2 dropped from 75% to 44% (`qwen2.5-coder:3b`, n=202) and 72% to 50% (`7b` Q3, n=120), McNemar p<1e-3 both, arm A also fewer attempts. Trimming faust-rs to code+message+caret (arm C) changed nothing. Mechanism: precise localisation makes a weak model edit at the caret and re-break it (same-class-again ~75–80% vs ~45%); vague stderr provokes a broader rewrite that compiles. Not a defect in faust-rs (diagnostics are a human win — stable code + location 15/15 vs C++ 0/15 + 8/15); a finding about how to use it. Untested: frontier model, FRS code as a *branch* signal, resending the failing source | — (evidence) | closed | S4 Testing | `bench/run_repair_ab.py`, `bench/results/repair_ab/repair_ab_20260830{,_7b}.json` | 2026-08-30 | 2026-08-30 |
| PF-077 | The shipped `requirements.txt` omits `google-genai`, so a clean `install.sh` builds a venv that **cannot run the provider its own seeded `.env` defaults to** (`.env.example` sets `PLUGINFORGE_PROVIDER=gemini`). `providers.py`'s gemini adapter does `from google import genai` (`llm/providers.py:680,991`) → `ModuleNotFoundError: No module named 'google'` for a launcher-started DAW off a fresh install. groq / openrouter (both httpx) and ollama (local) work; anthropic is already pinned. `bench/requirements.txt:2` has carried `google-genai>=1.0.0` since the gemini adapter landed — the root file the installer ships was just never synced to it. Found during the PF-065 clean-machine install rehearsal (2026-09-01, scratch `HOME`, `install.sh` → venv → `providers.py --check gemini`) | medium | fixed | S1 Backend | `requirements.txt`, `llm/providers.py:680` | 2026-09-01 | this session |

---

## Routing & fix plan (2026-07-23) — HISTORICAL

> **Superseded 2026-07-27.** This section routes bugs to FLEET.md lanes (S1…S6) and a
> cross-lane request log. That apparatus was retired; `docs/FLEET.md` no longer exists. Every
> code defect it routes below is now **fixed** — see the registry. It is kept because the
> reasoning about *who owns what* still explains several design choices, but do not use it as a
> work queue. The live queue is the registry table plus STATUS.md.

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
  (StatePersistenceTest 33/33, ASan/UBSan clean). The residual §2 trigger-3 format gate is
  **discharged**: confirmed by the human 2026-07-27, with `<SlotLabels>` dropped from v1 as
  part of that confirmation. See the Closed archive entry for the amendment.
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

## Detail

Entries stay here in discovery order once written, and carry their own status line. A bug that
closes gets its status flipped and a **CLOSED** paragraph appended saying what was verified at
HEAD — the investigation prose is the most valuable thing in this file, and moving it to an
archive on close has twice meant it was quietly dropped instead.

**Reconciled 2026-07-27.** Ten entries below were marked `open` while their fixes were live in
the tree — PF-006, PF-008, PF-015, PF-016, PF-018, PF-019, PF-020, PF-021, PF-022, PF-023. Each
was re-verified by reading the cited code at HEAD, not by trusting a commit message. This is the
same declared-vs-actual drift CLAUDE.md records three prior instances of, inverted: the registry
declared broken what was already fixed. Cheaper than the other direction, but it still misroutes
work, and it is why the day that found it started here.

### PF-006 — Shutdown use-after-free on the editor's detached *generate* thread.
**high · FIXED `18e862e` (2026-07-25) · owner S2 Prompting UX · was arch-review §2.2 (P1), second half**
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

**CLOSED `18e862e`, verified at HEAD 2026-07-27.** There is no `.detach()` left in
`PromptPanel.cpp`. One persistent worker, owned by the panel and joined in the destructor
(`:182-183`), started lazily on first use (`:231-232`); an in-flight run is **superseded** rather
than stacked, and its subprocess killed (`:173`, `:238`). The header states the threading
contract as a comment block (`PromptPanel.h:47-68`) and exposes `submitPromptForTest` /
a worker-exists predicate specifically so a test can assert it. Covered by
`host/tests/PromptPanelThreadingTest.cpp` (263 lines, added in the same commit). The
`// TODO: VERIFY: PF-006` marker is gone.

### PF-008 — No generated plugin has ever been listened to.
**high · DISCHARGED 2026-07-24 · S4 Testing**
The PF-001 denormalization fix is verified by unit test and by construction, **not by ear**. The
P6 audible battery (`docs/p6_test_battery.md`; its deleted companion `docs/prototype_test_plan.md` Part A
was superseded by the landed prototype) has never run. This is the fastest
way to find whatever the old denormalization bug was masking; the review
predicted "it will fail on the first patch" before PF-001. Needs the human's ears (use `groq`,
not Gemini's ~20/day quota).

**DISCHARGED 2026-07-24.** The battery ran with human ears on groq/gpt-oss-120b: **4 clean, 3
flaky, 7 failures of 14.** The review's prediction was right — it did fail, repeatedly. That is a
*bad result*, not a missing one, and the distinction matters: this entry asked whether anyone had
ever listened, and someone has. The reliability problem the run exposed is **PF-024**, and the
crash it exposed was **PF-006** (both tracked separately). A second pass after the prompt work is
worth doing but is a new question, not this one.

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
**medium · in-progress · S4 Testing · STATUS "Assumed, never checked"**
N=50, one model, the old prompt, two of five categories. The full 125-prompt run (P9) has never
produced valid data — the 2026-07-20 attempt was rejected pre-generation for insufficient
Anthropic credit (0 tokens spent). Re-run on a free provider once billing/quota allows.

**2026-08-28 — the complete 125-cell grid ran, once, on `ollama` (`qwen2.5-coder:7b`, CPU).**
The current unified prompt on `main`; the judge is the PF-041/PF-042-fixed checklist rubric
(`bench/score_efficacy.py`, `acceptance_specs.json`), run via ollama
`qwen2.5-coder:7b-16k`. Archives:
`bench/results/efficacy/efficacy_ollama_20260828{,_judged}.json` +
`_chart.png`. It cost two harness bugs to get there (**PF-069**, **PF-070**), worked around
with reverted-before-commit local patches.

| tier | first-try compile | retry-corrected | LLM judge mean /2 | fully-correct (2/2) |
|---|---|---|---|---|
| L4 (DSP engineer)       | 76% | 92% | **1.57** | 65% |
| L3 (informed producer)  | 75% | 92% | **1.35** | 52% |
| L2 (casual)             | 60% | 84% | **0.90** | 38% |
| L1 (vibe/metaphor)      | 64% | 84% | **0.57** | 19% |
| L0 (artist reference)   | 68% | 88% | **0.36** |  9% |

Overall: 85/125 (68%) first-try, 110/125 (88%) retry-corrected. Judge distribution
0→45, 1→24, 2→41, errors→15 — the middle score is a real, functioning category now
(22%, was 2.3% pre-PF-042). Worst *fidelity* categories despite compiling fine:
**filters 0.57/2, dynamics 0.57/2** (where PF-032 lives); best: time-based 1.50/2.

**What this establishes.** Compile rate is roughly **tier-independent** (84–92% retry-corrected
across all five tiers, non-monotonic first-try). Semantic fidelity declines **monotonically and
steeply** — 1.57/2 at L4 to 0.36/2 at L0, against ground truth that is independent of the
generation prompt (PF-041 closed). The pipeline **degrades gracefully** as prompts lose
prescriptive detail — it does not cliff-drop and does not improve. That is a concrete answer to
"generalizes to nothing"; the claim is no longer that there is no signal, it is that the signal
is characterised — for this model.

**Still open, hence `in-progress` not `fixed`:**
- **One run, one model, and not the shipping one.** `qwen2.5-coder:7b` is not `groq`'s
  `openai/gpt-oss-120b` (PF-012: the 120B succeeds where the 7B fails on material the prompt
  covers). A groq 125-cell run is owed — blocked 2026-08-28 by groq's daily token limit, which
  a ~33-call probe sweep exhausted.
- **The judge is a 7B grading a 7B.** Cheap and independent-of-the-generator, but blunt.
- **No per-cell reproducibility.** PF-031's bar (reproduce both prompt and error class across
  ≥3 runs) is not met — this is n=1 per cell.
- `--compare` against the 2026-07-30 groq archive is **not valid** (different model *and*
  different corpus: 25 informed-producer prompts vs 125 tiered).

### PF-012 — No cross-model comparison exists.
**low · open · S4 Testing · STATUS "Assumed, never checked"**
ADR-008 has been "Under evaluation" since 2026-04-29. No data comparing model choices.

### PF-013 — Semantic fidelity is unmeasured.
**medium · open · S4 Testing · STATUS "Assumed, never checked"**
Every metric on record is compile rate. `bench/score_efficacy.py`'s `--judge` rubric — the only
fidelity signal in the project — is **off by default** and has never run. Compile-rate says the
Faust builds, not that it does what the words asked.

**2026-07-30 — half closed, and the other half got a prerequisite.** ollama was installed,
which supplies a judge model independent of the generator at zero quota cost, and the judge
was run for the first time: 50 pilot records, **44 graded, 0 errors**.

- ✅ *"`--judge` has never executed"* — **closed.** It executes and parses cleanly.
- ❌ *"Semantic fidelity is unmeasured"* — **still open**, now for two reasons rather than one:
  1. The only gradeable records are the 2026-07-20 pilot: **the deleted pre-unification
     prompt**, paid `claude`, 2 of 5 categories. Judging them measures a system that no longer
     exists — the PF-007/PF-009 trap. This half stays downstream of PF-011 as always.
  2. **The instrument is not yet interpretable** — PF-041 (L4 grades against a ground truth
     identical to its own prompt, 10/10) and PF-042 (the 0/1/2 scale returns `1` once in 44).

**So the ordering has changed.** This entry used to be blocked purely on quota. It is now
blocked on *fixing the judge first*: running it against a fresh corpus before PF-041 is fixed
would produce authoritative-looking tier numbers that are partly tautological. That is a worse
outcome than having no numbers, and it is exactly the failure this registry exists to prevent.

Recorded scores, for the archive only, **not** to be cited as fidelity:
`L4 2.00 · L3 1.22 · L2 0.44 · L1 0.50 · L0 0.89` (n=9,9,9,8,9), mean 1.02/2.
`bench/results/efficacy/pilot_20260720_judged.json`.

### PF-014 — No real user prompt has ever been recorded.
**low · open · S1 Backend · STATUS "Assumed, never checked"**
`llm/generate.py` logs nothing. There is no corpus of real prompts to measure against or to seed
a generation cache. (Privacy/opt-in is a design question, not just a code change.)

### PF-015 — `check_rt_safety.py` cannot follow a call graph.
**medium · FIXED `fed704e` (2026-07-26) · S1 Backend · COLLABORATION.md §7**
The hook scopes exactly two named functions (`FaustEngine::process`, `processBlock`) by brace
counting and cannot follow a call graph (its own documented KNOWN LIMITATION,
`check_rt_safety.py:22`). `ParamPool::pushToFaust` — now on the audio thread and reachable from
`processBlock` — is **not** scoped. COLLABORATION.md §7 item 1 flags this as load-bearing: at
minimum the hook should also scope `pushToFaust` and anything else reachable from `processBlock`.
**Residual doc-debt (fold in here):** the hook's docstring (`:9`) still cites a stray
`ParamPool::pushToFaust()` in `FaustEngine.cpp` as a live "separately-tracked bug" — that stray
def is PF-017 and was removed; the docstring reference is now stale.

**CLOSED `fed704e`, verified at HEAD 2026-07-27.** `ANCHOR_RE` (`:65-70`) now matches all four
functions that actually run on the audio thread — `FaustEngine::process`, `processBlock`,
`ParamPool::pushToFaust`, `OutputGuard::process` — and `WATCHED_FILE_RE` (`:57-59`) covers
`ParamPool.cpp` and `OutputGuard.cpp` alongside the original two files. The stale PF-017
docstring reference is gone; `:11-28` now documents the real audio path. `tests/test_control_wiring.py`
carries a parametrised **red case per newly-scoped function**, so a function silently losing its
teeth fails the suite.
**Known limitation, deliberately retained:** it still cannot follow a call graph. A *fifth*
function arriving on the audio thread must be added to that list by hand — the red tests catch a
scoped function losing coverage, not an unscoped one appearing. That residual is why
COLLABORATION.md §7 item 1 stays open as a design concern even though this bug is closed.

### PF-016 — CI has never run green with the new prompt steps.
**medium · FIXED (2026-07-26) · S4 Testing**
The `build-host` job runs `tools/gen_stdlib_block.py --check` and the prompt tests, and carries
five pre-existing `TODO: VERIFY` items about Ubuntu Faust packaging — none checkable from the
Arch dev box. Green on CI has never been observed with these steps in place.

**CLOSED, verified 2026-07-27 via `gh run list`.** Run `30181544354` (2026-07-26, 11m52s) is
green on `main` at HEAD and includes the `build-host` job *and* the newly-added audio gate. Three
consecutive runs failed first (`30180544187`, `30180604270`, `30180674842`) while the libsndfile
static-link closure was worked out — so this is a green observed *after* a red, which is the only
kind worth recording. The five Ubuntu-Faust `TODO: VERIFY` items are answered by those runs.
**What remains true and is not this bug:** CI is *starved* — `main` has repeatedly run many
commits ahead of `origin`.

### PF-018 — `FaustEngine::prepare()` does not re-init a live DSP on sample-rate change.
**medium · FIXED `be83d1e` (2026-07-26) · S1 Backend · filed 2026-07-23 per S1 cross-lane req #5**
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

**CLOSED `be83d1e`, verified at HEAD 2026-07-27.** `prepare()` now computes `rateChanged` before
storing the members and, when the rate changed *and* a DSP is live, drives a real re-init through
the swap protocol: take `compileMutex` first (so it cannot interleave with a compile's own swap),
re-read `activeDSP` under the lock, `ready.store(false)`, drain `audioBusy` to zero, then
`instanceConstants` + `instanceClear` — the documented pair for a rate change that *keeps*
control values (`faust/dsp/dsp.h:135-143`). Neither mutex is ever held on the audio thread, so
this cannot block it. Two early-outs keep the common path free: unchanged rate returns
immediately, and a null `activeDSP` returns because a later `compile()` will init at the new rate
anyway.

### PF-019 — Generation timeout cliff (120s frozen UI under sustained use).
**high · FIXED `4bea5f3` (2026-07-25) · S1 Backend · found in the 2026-07-24 P6 battery**
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

**CLOSED `4bea5f3`, verified at HEAD 2026-07-27.** All four elements of the fix shape shipped:
- **Per-attempt budget.** `providers.Budget` (`providers.py:143-160`) carries a total and a
  `per_attempt_cap`; `generate.py:generation_budget()` (`:76-85`) sizes one Budget per generation
  so `max_retries` attempts *plus* each attempt's faust compile fit inside the C++ subprocess cap.
  `_HTTP_TIMEOUT` (120s) is now only the fallback used when no Budget is supplied (`:188`).
- **Bounded backoff.** A backoff sleep that would overrun the deadline is **refused** and raises
  rather than silently eating the budget (`:154`).
- **Typed reasons.** `run()` returns one of `ok | invalid_faust | truncated | timeout |
  rate_limited | error` (`generate.py:213`), with `rate_limited` and `timeout` raised distinctly
  (`:243`, `:245`, `:254`, `:301-303`).
- **Default pacing.** `Budget.min_interval` defaults to 1.0s and `_pace()` honours it whenever a
  Budget is present — i.e. always on the product path — while an explicit `PLUGINFORGE_MIN_INTERVAL`
  still wins so the bench harnesses are unaffected (`providers.py:508-537`). The pacing sleep is
  itself clamped to the remaining budget, so pacing can never be what blows the deadline.
Covered by `tests/test_generation_budget.py`.
**Not verified:** the original symptom was consecutive prompts #11–14 timing out under sustained
groq use. No one has re-run four consecutive live generations to confirm the cliff is gone — the
tests pin the budget arithmetic, not the field behaviour. Worth folding into the next P6 pass.

### PF-020 — Cross-generation state contamination; no fresh/iterate mode.
**high · FIXED `4a84c1c` (2026-07-25) · S1 Backend (state) + S2 UX (affordance) · found 2026-07-24**
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

**CLOSED `4a84c1c`, verified at HEAD 2026-07-27.** `LoadMode { Fresh, Iterate }` exists
(`PluginProcessor.h:58-61`) and `loadFaustCode` takes it, **defaulting to `Fresh`**
(`:73`) — a newly generated patch is a new plugin, so inheriting the last one's values is the
wrong default, and that choice is now explicit rather than an accident of whether the editor
happens to be open. The reset runs **in the processor** (`PluginProcessor.cpp:166-170`), which is
the whole point: the old UI-layer seeding did nothing headless. Both state-restore call sites
correctly pass `Iterate` (`:69`, `:332`) because `replaceState()` has just written the saved
values and resetting them would defeat the restore. Covered by additions to
`host/tests/StatePersistenceTest.cpp` (+143 lines in the same commit).
**Residual, not a defect:** the S2 half — a *UI affordance* distinguishing "New plugin" from
"Refine" — is not built. The mode is correct by default and reachable from code; the user just
cannot choose it from the editor yet. That belongs to the deferred UI work, not here.

### PF-021 — Stale error persists across a new Generate.
**medium · FIXED `18e862e` (2026-07-25) · S2 Prompting UX · reported by the human 2026-07-24**
`setError()` (`PromptPanel.cpp:309-316`) writes `errorBox` and its own comment notes the error is
"retained across a later success." Neither `submitPrompt()` (`:124-144`) nor `startWorking()`
(`:319-326`) ever clears `errorBox` on a new run, so a previous failure's text stays on screen
through the next generation — including a successful one — and reads as the current result. **Fix
shape:** clear/hide `errorBox` on submit, or prefix each error with its attempt/timestamp so it
is never mistaken for the live run. Human explicitly asked for this.

**CLOSED `18e862e`, verified at HEAD 2026-07-27.** `submitPrompt()` calls `clearError()` at
`PromptPanel.cpp:200`, *before* starting the run, with a comment naming PF-021 and the reason
(`:195-199`). A dedicated `clearError()` (declared `PromptPanel.h:89`) hides and empties the
region. The distinction the fix preserves is the right one: an error survives a later *success*
within the same run, but never survives the next **submit** — so it can no longer be read as the
current result.

### PF-022 — Source-of-record committed before compile success.
**high · FIXED `4a84c1c` (2026-07-25) · S1 Backend · found 2026-07-24**
`loadFaustCode` sets `currentFaustSource`/`currentPrompt` **unconditionally at
`PluginProcessor.cpp:114-118`, before the compile is even queued.** A failed compile therefore
overwrites the retained source-of-record with non-compiling code while `activeDSP`,
`currentLabels`, and the APVTS values still belong to the *previous* successful patch. A DAW save
in that window (or after any failed generate) persists a broken-source / old-labels / old-values
triple; on reload the restore-recompile fails and no DSP goes live. **Fix shape:** commit
`currentFaustSource`/`currentPrompt` only on compile **success**. Interacts with state
persistence (`getStateInformation` `:206-236`).

**CLOSED `4a84c1c`, verified at HEAD 2026-07-27.** `loadFaustCode` no longer touches
`currentFaustSource`/`currentPrompt`; an explicit comment at `PluginProcessor.cpp:148` records
that they are deliberately *not* set there and why. They are assigned only inside the compile
**success** branch (`:180-181`). A failed generate therefore leaves the previous good source, its
labels, and its values consistent with each other, so a DAW save in that window persists a triple
that still restores.

### PF-023 — `process()` has no `activeDSP` null guard.
**medium · FIXED `4a84c1c` (2026-07-25) · S1 Backend · found 2026-07-24 (defense-in-depth)**
`FaustEngine::process()` (`FaustEngine.cpp:178-183`) loads `activeDSP` and calls
`dsp->compute(...)` relying *solely* on the `ready` flag to guarantee non-null. The invariant
(`ready==true ⟹ activeDSP!=null`) holds in current code, so this is latent, not a live crash —
but there is zero defense if the swap ordering is ever broken, and the audio thread would
segfault. **Fix shape:** null-check `activeDSP` in `process()` and passthrough if null.

**CLOSED `4a84c1c`, verified at HEAD 2026-07-27.** `process()` now null-checks the loaded pointer
and returns (passthrough) if null, matching the `!ready` early-return above it. The in-code
comment is explicit that the branch is *unreachable today* — which is precisely the argument for
it being cheap: without it there is zero margin if the swap ordering in `compile()` is ever
changed, and the failure mode would be a segfault on the audio thread.

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

**2026-07-27 — root cause found, and it is one gap, not four.** These read as four unrelated
failures. They are mostly one: **the prompt barely teaches Faust's routing algebra, and the one
place it does teach a language construct, it teaches a construct that does not exist.**

1. **`let` is not a Faust construct.** `llm/prompts/system_prompt.txt:21` instructs the model:
   *"use let bindings or with { } blocks."* Faust has `with`, `letrec` and `environment`. It has
   no `let`. Verified against the installed compiler (2.85.5):

   ```
   process = let g = 0.5; in _ * g, _ * g;
     → ERROR : syntax error, unexpected IDENT
   ```

   That is **exactly** the signature recorded above for #9 (Loveless). The prompt is teaching the
   failure. This is a hallucination *inside* the artifact built to prevent hallucination:
   `tools/gen_stdlib_block.py --verify-prompt` checks that every `ns.func` resolves and cannot
   see a bad **language construct** in prose. See the follow-up note below.
2. **`with { }` is recommended but never demonstrated.** Same line recommends it; none of the
   five few-shot examples contain a `with` block. Being told to use a construct one is never
   shown is a plausible route to #10's `syntax error, unexpected WITH`.
3. **The routing operators are essentially absent.** `<:` (split) and `:>` (merge) appear
   **nowhere** in the 173-line prompt. `~` (recursion) appears exactly once, buried inside the
   delay example's body at `:149`, never named or explained. #6's `2 outputs must equal 1 input`
   is a split/merge arity error and the sidechain compressor's `unexpected ARROW` (2026-07-19
   corpus) is the same gap.
4. **Ping-pong is described in prose and still fails.** `:26-27` tells the model to cross the
   feedback between two `de.fdelay` lines. `endless evaluation cycle` is what you get from a `~`
   loop with no delay *in* the loop — a recursion-topology error that prose cannot convey and an
   example can.
5. **Nothing states that a delay's first argument is a compile-time constant maximum.** Cause of
   `invalid delay parameter range: interval(0,2.1e9,0)`.

**Follow-up worth its own decision (not this bug):** `--verify-prompt` validates `ns.func`
references only. A control that extracts every construct the prompt *recommends* and compiles it
would have caught `let` the day it was written. That is a new enforcement mechanism — route it
through `/architecture-planning`.

**2026-07-29 — the prompt side is done, and one recorded fact was wrong.** `a4f942e` addresses
the two syntax classes remaining in the 07-28 run. Both were the `let` pattern repeating:
**a construct invented because the prompt named no alternative.**

| class | prompt | generated | why |
|---|---|---|---|
| `syntax:FLOAT` | ping-pong | `outL = _.0*(1-mix) + wet.0*mix;` | no channel indexing exists; `.0` lexes as FLOAT |
| `syntax:EXTRA` | sidechain compressor | `(env > thresh) ? (...) : 1` | C-style ternary; `select2` was **absent from the prompt** |

`select2` appeared nowhere in `llm/prompts/system_prompt.txt` while
`bench/prompts/tiered_prompts.json:62` lists it as an expected primitive — the benchmark
expected a construct the prompt never taught.

**The correction.** STATUS.md and this file both said the verified ping-pong few-shot had
never been folded into the prompt. **It had**, and its `USER:` line matches the failing
benchmark prompt *verbatim*. The model was handed a matching example and failed anyway, so
"add the few-shot" was never the fix. The real gap was narrower: **every dry/wet in the prompt
was on a mono function taking `x`** (`echoCh(x)` at what is now `:181`, `chorusCh(f, x)` at
`:224`), so mixing across a **multi-output** block was undemonstrated — and ping-pong is
inherently multi-output. The model needed a stereo dry/wet, had no pattern for one, and
invented indexing to get it. The few-shot now shows `_,_ <: dry, wet :> _,_`.

**Why the new claims are checked and not asserted.** `select2`'s argument order and which side
`(_ , !)` keeps are semantic; **both orders compile and have identical arity**, so the existing
compile gate could not see them. Teaching either backwards would ship plugins that compile,
load, run, and invert every conditional — PF-032's shape exactly. `tests/test_prompt_claims.py`
now folds constant programs and reads the surviving literal from the generated C++, closing
that file's self-declared blind spot (bare-expression claims with no quoted error) for these
three constructs. Both tests were seen red first.

**2026-07-30 — the benchmark statement is now PAID, and it is a mixed result.**
Two runs, 50 generations, first measurement of any kind against `a4f942e`.

*What the prompt edit fixed, with evidence:*
- **The Hz unit contract.** PF-032's warm low-pass (`moog_vcf` fed a normalised cutoff) has
  not recurred in either fresh corpus. The render oracle over run 1 reports **18 passed /
  1 failed / 3 unsupported** against 07-28's 16/2/4, and over run 2 **17 / 0 / 4** — zero
  silent patches in the second corpus.
- **`select2`.** The sidechain compressor no longer emits a C-style ternary. It now fails
  *later* and differently (`duplicate_symbol`, then `unbound_variable`), which is progress
  that a compile-rate metric cannot show.
- **The stereo dry/wet few-shot.** Ping-pong delay compiled in both fresh runs.

*What it did not fix:*
- **The dB unit contract.** PF-032's noise gate is still silent at rms **exactly 0.0** in run
  1 (it failed to compile at all in run 2, so the gate is unhealthy in both runs, differently).
  The double `ba.db2linear` remains the standing hypothesis; the prompt edit did not close it.
- **`recursion_cycle` on Karplus-Strong**, unchanged across all four archives on record.

*The honest caveat, which PF-031 supplies:* run-to-run instability is large enough that only
Karplus-Strong reproduces both prompt and class. So "ping-pong is fixed" and "`select2`
worked" are **single-observation claims repeated twice**, which is better than the one
observation they replace and is not proof. The oracle results are firmer, because a silent
render is a property of the emitted patch rather than of the sampling. `check.sh audio` still reports PF-032's two silent patches because it renders the
**stored** 07-28 corpus, which a prompt edit cannot retroactively change — expect it to stay
red until the benchmark re-runs. The Tier-2 benchmark statement is **unpaid**, and per PF-031
it must be per-class rather than aggregate.

**Cost, recorded because it constrains the next edit.** Prompt headroom fell 457 → **185
tokens**; the stdlib block now needs only **10.8%** growth (was ~29%) to 413 every groq
request. The calibration anchor is 7.9% stale and a re-measure is due — one live generation.

---

**2026-07-30 — a prompt rule was added, measured, and DID NOT WORK. Recorded because a
negative result nobody writes down gets re-attempted.**

Reading the failing code across both models suggested one shared root cause: the signal
written into a stdlib effect's argument list rather than arriving by composition —
`ba.bypass2(mute, _ , _)` on ollama and `ef.gate_stereo(t,a,h,r,_,_)` on groq, the latter
being PF-032's surviving silent render. A rule was added to STRICT RULES stating it in plain
terms with a correct/wrong example pair, costing **61 tokens** (slack 185 → 124).

**Result on ollama, one run against a two-run identical baseline: no improvement.** 80% → 80%.

| case | before | after |
|---|---|---|
| mute toggle | `ba.bypass2(mute, _ , _)` | `ba.bypass2(mute, _, _)` — **unchanged** |
| polarity inverter | `ba.bypass2(bypass, _ * -1)` | `ba.bypass2(bypass, _ : *(-1))` — better form, still mono-into-stereo |
| sidechain compressor | `routing_arity` | compiles |
| Karplus-Strong | compiles | emits prose, `syntax:IDENT` |

**What this actually shows.** The model was told, in the imperative, with a worked example,
not to write trailing `_` arguments — and wrote them anyway, byte-identically. That is an
instruction-following limit, not a prompt gap, and it is the conclusion PF-012's data
supported all along: a 7B model fails where a 120B one does not, on material the prompt
already covers. One case fixed and one broken is inside the 5-in-25 run-to-run variation
ollama shows even at temperature 0, so **neither the fix nor the regression is attributable.**

**Two things the attempt got right and are worth keeping.** The polarity case now fails
purely on mono-vs-stereo — which is precisely the `bypass1`/`bypass2` sentence that was cut
from the rule to fit the headroom budget, so the budget shaped the outcome. And the headroom
guard (`tests/test_prompt_headroom.py`) refused the first, larger draft and its docstring
pre-registered the correct response — *"do NOT widen these bounds, buy headroom back"* —
which is a control behaving exactly as designed under pressure to tune it away.

**Status of the rule:** retained **pending a groq run**, not because it is proven but because
it is unproven on the model that ships and groq's `ef.gate_stereo(...,_,_)` is its exact
target. If a groq run shows no benefit it must be **reverted** — 61 tokens of a 185-token
budget is not payable by a rule that only might work.

**2026-08-28 — the failure-class distribution, measured over the complete 125-cell grid**
(`ollama` 7B, current prompt — see PF-011's sub-note). 15 cells failed every attempt; 44
error strings across all failed attempts. Classified with `bench/classify_failures.py`:

| class | count (of 44 error strings) |
|---|---|
| `routing_arity` (sequential/recursive composition width) | **22** |
| `syntax:*` (IDENT / WIRE / REC / WITH / INT / EXTRA) | 13 |
| `unbound_variable` | 3 |
| `duplicate_symbol` | 3 |
| `hallucinated_symbol` / `recursion_cycle` / `unclassified` | 1 each |

**`routing_arity` is now the dominant class by a wide margin** — Faust's composition algebra
(`:`, `~`, `<:`, `:>` widths), not symbol invention. The first-attempt error tags per tier
(`score_efficacy.py`): SEMANTIC dominates L1/L2 (7 each), SYNTAX dominates L0 (4) — vague
prompts miss *what* to build, reference prompts flail on *how*.

**Karplus-Strong: 0 of 5 tiers produce usable audio.** 4 fail to compile
(`recursive composition A~B`, `recursion_cycle`); the 1 that compiles (L2) renders **+79.6 dB
runaway, DC offset 2.35, peak 4541** — a feedback loop with gain ≥ 1. This is the class
PF-024 has recorded as "unchanged across all archives" since 2026-07-30, now confirmed on a
full tiered grid.

**faust-rs on these failures** (issue #26 / ADR-030 evidence): all 15 never-compiled cells
re-checked with `faust-rs 0.8.0 --check --error-format json` against the *same final program*
C++ rejected. **Accept/reject agreement 15/15.** C++ carried a source location on 8/15
*(re-derived 2026-08-30 as 8, recorded as 9 on 2026-08-28 — a stderr-scoring-heuristic
difference plus Faust 2.85.5→2.85.9; not material)*; faust-rs on **15/15**, plus a stable
`FRS-*` code on **15/15** (C++: 0). The 6 `routing_arity` failures — C++'s worst case, no
location + a Box-expression dump — map 1:1 to `FRS-PROP-0002` with a caret and the arities as
numbers. Reproducible from a clean checkout via `bench/frs_rederive_issue26.py`
(replaces the uncommitted-and-lost `scratchpad/frs_annotate.py`; measurement-only, faust-rs
is not a project dependency). The 36-program hand-built corpus behind the "51/51" figure was
in the same lost scratchpad and is **not** recoverable — 15/51 re-derives.

**Loop-level result (PF-076, 2026-08-30):** feeding those diagnostics *back to the model* in
place of C++ stderr made the repair loop **worse** — 75%→44% repaired-within-2 on
`qwen2.5-coder:3b` (n=202), 72%→50% on `7b` Q3 (n=120), McNemar p<1e-3. So the diagnostic
quality is a human win but not, as-is, an automated-repair win. See PF-076 and
`bench/results/repair_ab/`.

**Methodology retractions (2026-09-01, issue-#26 review).** Two claims that supported the
A/B design had no artifact and are withdrawn: (1) `bench/build_repair_corpus.py`'s
"deterministic at temperature=0 (measured 2026-08-30: 3/3 byte-identical)" — `git log -S`
shows the only commit touching that string *added the comment*; the closest real measurement
is the ~20% ollama temp-0 output-flip rate two sections above (`:659-660`). n=1 per cell is
now flagged as an unaudited limitation, with a determinism audit pre-registered as WP5 in
`bench/issue26/METHODOLOGY.md`. (2) The "~24% of arm A's wins shrink the program — arm A
partly buys the compile with fidelity" line in `bench/issue26/README.md` compared each arm
to its own (different) set of wins and reversed the sign. Recomputed paired on the **67
programs both A and B repaired** (`bench/fidelity_gate.py`): arm A shrank 19/67 (28%),
arm B 24/67 (36%); primitive-lost A 10/63, B 6/63. Arm A does not buy compiles with
fidelity; the loop-level result above is unaffected.

**Repro-package handoff (2026-09-02).** `bench/issue26/` + the harness are relicensed
(MIT; corpus CC-BY-4.0 — `/LICENSE` exceptions, `bench/corpora/LICENSE`,
`bench/issue26/LICENSE`); `verify.py` gains a `verify_fidelity()` checksum of the
committed sidecars; `METHODOLOGY.md` collects the WP1–WP5 follow-up; the Dockerfile
asserts the Faust/faust-rs versions; CI now runs `verify.py`. The GRAME reply links an
immutable commit SHA, not the `issue-26-repro` tag (which is not moved — a new tag is cut
alongside). See STATUS.md.

**Integrity pass (2026-09-03, before the GRAME handoff).** A second adversarial review
found the headline ROBUST but the package not yet defensible. Fixed: (1) **10** of the 202
"C++-rejected Faust programs" are not programs (9 prose, 1 truncated) — a mechanical
outcome-blind screen `bench/corpus_screen.py` drops them; everything is now the 192/115
view, `--no-screen` gives the raw 202 (75/44/43, unchanged finding). (2) **arm A's C++
stderr is `.strip()[:500]`, arms B/C uncapped** — disclosed, and stratified: on the 158
never-truncated programs arm A still wins 75%→45%, McNemar p≈3e-7, so the cap (which
handicaps arm A) is not the cause. (3) mechanism denominators fixed and script-backed —
rescue-after-attempt-1 49/87 (was mis-stated 50/101), same-class recidivism 21/39 (the
shipped report showed 21+18≠51); `score_repair_ab.py` grew per-class McNemar + a rescue
block + the cap strata, all checked by `verify.py`, which now has an N-guard
(`checks_expected`) so a stale verify can't print REPRODUCED over a subset. (4) transport
robustness in `repair_ab_standalone.py` (retry/backoff/Retry-After; a 429/timeout is
`terminal_reason` and excluded, not scored as a repair failure; ≥25% aborts → exit 3).
(5) the MIT harness is now legally runnable — `system_prompt.txt` + `frs_rederive_cells.json`
vendored into `bench/issue26/`, the proprietary `COPY`s dropped from the Dockerfile.
(6) `verify.py` had ZERO tests on `score_repair_ab.py` — `tests/test_score_repair_ab.py`
(13) + `tests/test_corpus_screen.py` (10) added. Branch `issue26-integrity`, ~7 commits.

**Caveat, unchanged:** the diagnostic-quality half is the 7B on CPU. groq's 120B is barely
sampled (a 2026-08-28 probe sweep got 4 PF-024 syntax classes clean on groq — no
reproduction — and the `duplicate_symbol` residual once in 3, before the daily token limit
stopped it).

---

### PF-035 — a per-call output budget cannot be expressed. *(open, found 2026-07-29)*

**low · open · S4 Testing · noticed while auditing `max_tokens` call sites for the groq
headroom work, not by a failure**

`make_generator` floors every request at the provider's own minimum:

```python
max_tokens = max(max_tokens, spec.min_max_tokens)     # llm/providers.py:766
```

Every spec sets `min_max_tokens=4096` (`llm/providers.py:258` and the four siblings;
confirmed for groq / gemini / openrouter / ollama / anthropic). So a caller asking for a
*smaller* budget is silently overridden, and there is no way to express "this call should
be short". The one caller that tries is the semantic judge:

```python
judge = providers.make_generator(
    JUDGE_PROVIDER, system_prompt="", model=JUDGE_MODEL,
    temperature=0.0, max_tokens=300,          # bench/score_efficacy.py:465 -> becomes 4096
)
```

**Why this is filed low and not fixed here.** The floor exists for a real reason and it is
documented at `llm/providers.py:92-97`: the original truncation confound was a spec —
anthropic — whose floor was 0 while every recorded benchmark ran through it. The floor is
the fix for that, and inverting it to respect small requests would reopen the hole. So this
is a design tension, not a defect with an obvious patch.

**What it actually costs today: nothing measurable.** `max_tokens` is a cap, not a spend —
TPD is billed on completion tokens actually produced, and a judge verdict is short whatever
the cap says. TPM admission (`prompt_tokens + max_tokens <= 8000`, see PF-032's neighbours
and `tests/test_prompt_headroom.py`) *does* count the cap, but the judge passes
`system_prompt=""`, so its prompt side is a few hundred tokens and 4096 is nowhere near the
ceiling. The one number that would move is a per-call cost bound, and nothing depends on one.

**Why it is worth writing down anyway.** If a future caller needs a genuinely bounded reply
— a classifier, a yes/no gate, anything where a runaway completion is the failure — it
cannot get one through this seam, and the override is silent. A caller that asks for 300 and
receives a 4096 budget has no way to find out.

**Not verified.** That no current caller depends on a small cap being honoured: checked the
five call sites (`llm/generate.py:110`, `bench/run_benchmark.py:193`,
`bench/run_efficacy_study.py:143`, `bench/score_efficacy.py:465`, and `make_generator`'s own
default) and none does, but "none does" is a statement about today's tree. The judge has
never executed (PF-013), so its behaviour under the 4096 cap is unobserved rather than
observed-fine.

---

### PF-036 — libfaust's JIT emits AVX-512 the CI runner cannot execute. *(fixed 2026-07-30)*

**high · S4 Testing · found by reading the CI log at HEAD, not by any control**

CI was red at `a4f942e` (run `30501160287`) with the same SIGILL PF-027 had closed two days
earlier. **PF-027's fix was real and PF-027's conclusion was wrong.** There were two
independent SIGILLs. The missing `ScopedJuceInitialiser_GUI` was one. This is the other, and
it is the one PF-027's CLOSED paragraph explicitly ruled out:

> The `AMD EPYC` / libfaust-LLVM / instruction-set hypothesis was never tested and was never
> evidence.

Half of that sentence is true — it was never *tested*. The other half is not: it was correct.

**The evidence that settles it.** Runner CPU predicts the outcome perfectly across the last
twelve runs on `main`:

| run | conclusion | runner CPU |
|---|---|---|
| `30501160287` (HEAD) | failure | AMD EPYC 9V74 |
| `30500293013` | success | AMD EPYC 7763 |
| `30499723334` | success | AMD EPYC 7763 |
| `30412830839` | success | Intel Xeon Platinum 8573C |
| `30412033722` | success | AMD EPYC 7763 |
| `30411209139` | failure | AMD EPYC 9V74 |
| `30409357504` — *the run PF-027 was closed on* | success | AMD EPYC 7763 |

Both failures are 9V74. No success is. And the gdb post-mortem the PF-027 work itself added
names the instruction:

```
Thread 1 "OfflineRenderTe" received signal SIGILL, Illegal instruction.
0x00007ffff6a7e27d in computemydsp ()
=> 0x7ffff6a7e27d <computemydsp+93>:   kmovd  %r9d,%k1
   0x7ffff6a7e288 <computemydsp+104>:  vmovss %xmm2,%xmm2,%xmm2{%k1}{z}
   0x7ffff6a7e28e <computemydsp+110>:  vroundss $0x9,%xmm2,%xmm2,%xmm3
```

`kmovd` and `{%k1}{z}` are AVX-512 opmask instructions, inside JIT'd Faust, not inside our
C++.

**Why those three CPUs behave differently.** EPYC 7763 is Zen 3 — no AVX-512 exists, LLVM
knows `znver3` has none, nothing is emitted. Xeon 8573C is Emerald Rapids — AVX-512 is
present *and enabled*, so the emitted code runs. EPYC 9V74 is Azure's custom Genoa: it
reports as `znver4`, whose LLVM default feature set includes AVX-512, while the hypervisor
masks AVX-512 out of the guest. libfaust asks LLVM for the host CPU **by name** and takes
that name's default features without rechecking CPUID. So the JIT emits instructions the
guest traps on.

That is a ~1-in-5 runner draw, which is the whole reason three readings produced three causes
and why one green run read as a fix. **A green CI run is not evidence about this bug.**

**The documented knob is inert, and pinning it would have been a placebo.**
`createDSPFactoryFromString` takes a `target` parameter documented at
`/usr/include/faust/dsp/llvm-dsp.h:226-228` as *"the LLVM machine target ... and
`i386-apple-macosx10.6.0:generic` kind of syntax for a generic processor"*. Measured against
libfaust 2.85.5 on 2026-07-30, by JITting a patch and disassembling the resulting executable
mapping:

| target passed | result |
|---|---|
| `""` (host-native) | 28 VEX-prefixed AVX instructions |
| `x86_64-pc-linux-gnu:x86-64` | 28 — byte-identical mnemonic stream |
| `x86_64-pc-linux-gnu:i486` | 28 — a CPU with no SSE at all |
| `totally-bogus-triple:nonexistent-cpu` | accepted without error, 28 |

`writeDSPFactoryToObjectcodeFile` and `writeDSPFactoryToMachine` are equally unhelpful: they
emit target-independent baseline code, so neither is a window into JIT codegen. This was
found the hard way, and it is recorded because the first plan for this bug was to pin that
parameter — a fix that would have "worked" only by runner luck and been indistinguishable
from a real one for weeks.

**The lever that does work.** libfaust leaves the symbol UNDEFINED and resolves it from
libLLVM at load time, so it is interposable:

```
$ nm -D --undefined-only /usr/lib/libfaust.so.2.85.5 | grep -i hostcpu
                 U _ZN4llvm3sys14getHostCPUNameEv@LLVM_22.1
```

`host/tools/pf_cpu_shim.cpp` is an `LD_PRELOAD` shim returning a conservative CPU name. It is
**CI-only** and is never loaded by the shipping plugin: on a user's machine the detection is
honest, and real-time DSP should have the best ISA available. The residual risk — a user
running a DAW inside a VM with a masked ISA — is real, unreported, and deliberately not paid
for with a permanent performance cost.

**Which patch was actually crashing.** `tremolo`, from `OfflineRenderTest`'s `kWellBehaved`.
Under `znver4` it emits 1 EVEX instruction and 2 opmask references; `os.osc`'s phasor wrap is
the `vroundss $0x9` sitting immediately after the faulting `kmovd` in the backtrace above.
`toggle blend` emits 9 EVEX. Under `x86-64`, all seven corpus patches emit zero — and zero
VEX as well. The workflow comment written on 2026-07-27 had already guessed `tremolo` and
already guessed "the JIT emits an instruction this runner's CPU lacks", labelling it *"That
is a guess."* The guess was right for three days.

**Seen failing before being believed** (CLAUDE.md's rule), at three levels:

1. `host/tests/JitTargetTest.cpp` run **without** the preload fails loudly on its first
   assertion rather than passing vacuously — checked, exit 1.
2. Its red arm (`znver4` → AVX-512 present) is what stops the green arm being empty. If the
   shim silently died, both arms would produce identical host-native code and that assertion
   is the one that fails.
3. `tests/test_control_wiring.py::TestJitTargetIsPinnedInCI` was mutation-tested against four
   breakages — preload dropped from a step, shim ordered before libasan, shim dropped from
   the `--target` list, `$SHIM` rebound — each caught by the intended assertion, green again
   after restore.

**A detail worth keeping.** `libasan`/`libtsan` must lead `LD_PRELOAD`. An ASan-instrumented
binary aborts with *"ASan runtime does not come first in initial library list"* if anything
precedes it — observed locally while wiring this, which is why the ordering has its own test.

**Confirmed on the CI toolchain, run `30574593504`.** The first pushed run drew an EPYC 7763
— a safe CPU — so its green conclusion says nothing about this bug. What *is* evidence is
that `JitTargetTest` reproduced the red case **on the runner image**, against Ubuntu's Faust
2.70.3 and its LLVM rather than the Arch 2.85.5 the diagnosis was built on:

```
  tremolo    evex=1   kmask=2   vex=54       <- under znver4
  toggle     evex=24  kmask=0   vex=116
  highpass   evex=0   kmask=0   vex=55
  [PASS] tremolo: no AVX-512 (evex=0 kmask=0) <- under x86-64
```

So both halves of the causal chain now hold on CI's own toolchain: libfaust *does* emit
AVX-512 when it believes the host is znver4, and the shim *does* suppress it. That is
independent of runner draw, which is what makes it worth more than a green run.

**CONFIRMED END-TO-END 2026-07-30, run `30577386079`: green on an `AMD EPYC 9V74`.**

That is the exact condition named as the only acceptable proof — a pass on the hazard CPU
itself, not on one of the four-in-five safe draws. It arrived two pushes later without being
sought. Before the shim, that CPU produced SIGILL every time it was drawn (`30501160287`,
`30411209139`); with the shim it runs the full harness set clean. The hypothesis is now
closed at both ends: the mechanism was reproduced on the CI toolchain, and the fix is
observed working on the machine that exhibited the fault.

Recorded because the discipline is the point: this bug was mis-closed once (PF-027) by
reading a lucky green run as evidence. The rule that a green run on a *safe* CPU proves
nothing remains correct — it is simply no longer the only evidence available.

**Still not verified.** `ParamPoolTsanTest` and `StatePersistenceTest` also JIT and are
covered by the same preload, but neither has been *observed* failing this way, so their
coverage is precautionary rather than a fix for an observed symptom.

---

### PF-037 / PF-038 / PF-039 — the parameter grid, as the harness photographed it.
**PF-037 fixed 2026-07-30 · PF-038/PF-039 low, open · S3 Plugin UX · observed 2026-07-28, filed 2026-07-30**

All three were recorded in STATUS.md under *"Two things the harness measured that were
nobody's claim either way"* and had no IDs, which means they were one STATUS.md rewrite away
from disappearing. Visible in `artifacts/images/session_*.png`.

- **PF-037 — every value displays as a raw 0–1 slot number.** A cutoff of 800 Hz reads
  `0.04`; a voice count of 2 reads `0.14`. `ParamMap.h` denormalizes into the DSP correctly
  (that is PF-001) and *nothing* denormalizes for the display. STATUS.md declined to file
  this as "a design question, not a bug." It is a bug: the DSP is right, the readout is
  unreadable, and no user can tell what any knob is set to.
- **PF-038 — knobs are ordered lexicographically, not by declaration.** A 40-param patch
  lists `P0, P1, P10, P11 … P2`.
- **PF-039 — the rotary fallback is dead code.** `FaustEngine::Kind` has five values and
  `refreshParamKnobs` handles all five explicitly, so the `default:` arm is unreachable.
  `docs/ui_design_plan.md` still describes it as the fallback widget, so the doc describes a
  widget no generated plugin has ever shown.

**PF-037 CLOSED 2026-07-30, and it found a bigger bug underneath.**
`ParamMap::formatZone`/`parseZone` convert for the eye, installed on each slider's
`textFromValueFunction` in `ParamGridPanel::applyPresentation`.

*Order is load-bearing:* `SliderParameterAttachment`'s constructor assigns both text
functions itself, delegating to the parameter's `getText()`
(`juce_ParameterAttachments.cpp:118-119`) — which for a plain `AudioParameterFloat(0..1)`
is exactly what printed `0.04`. Ours must be assigned **after** the attachment exists.
`refreshParamKnobs` already calls `applyPresentation` after constructing it; moving it
earlier silently restores the bug, which is why the ordering has a comment and a test.

*Scope, chosen deliberately:* plugin UI only. The slot stays 0..1 and the parameter is
untouched, so the DAW's automation lane still reads `Macro 7: 0.04`. Making the *parameter*
unit-aware would mean feeding per-patch metadata to a `stringFromValue` lambda the host can
call from any thread while the compile thread republishes it — a real concurrency change
next to the "parameters are declared once" invariant, and not worth taking on to fix a
readout. Filed as a follow-up rather than done quietly.

*Format:* plain, in Faust's declared unit — `800 Hz`, `-6.0 dB`, `250 ms`; a bare number
when no unit is declared. No kHz auto-scaling: the box must parse what it prints, and
rounding `12000 Hz` to `12.0 kHz` makes typing a value and reading it back shift it.
Precision comes from the declared step, falling back to the **range span** rather than the
current value — a control whose digit count changes as you turn it reads as a glitch.

**Two things the tests found that reading could not.**

1. **The defect report's own number was a clue nobody followed.** STATUS.md said "a cutoff
   of 800 Hz reads `0.04`". On the log curve 800 Hz is slot **0.534**; `0.04` is the
   *linear* slot. So the photographed patch declared neither `[unit:Hz]` nor `[scale:log]`
   — the common generated case, and the one `curveFor()` cannot help. A test written to the
   remembered number failed and said so.
2. **`0.01f` is not 0.01.** It is 0.009999999776, so `-log10` is 2.0000000097 and a bare
   `ceil()` returns **three** decimals — a 0.01-step control rendered `0.500`. Found
   end-to-end, then pinned per-step at unit level so the epsilon cannot be tuned away.

**Not verified.** That the grid *looks* right, as opposed to reporting right strings.
`artifacts/images/session_12_readout.png` is written for exactly that, and looking at it is
a human's job.

---

### PF-040 — every macro slot had 100 positions. *(fixed 2026-07-30)*

**high · S1 Backend · found by PF-037's regression test, not by reading**

`createParameterLayout` built each of the 64 slots with the bare min/max/default overload:

```cpp
juce::AudioParameterFloat(slotId(i), "Macro N", 0.0f, 1.0f, 0.0f)
```

That overload is not a thin wrapper. `juce_AudioParameterFloat.cpp:76`:

```cpp
AudioParameterFloat::AudioParameterFloat (const ParameterID& pid, const String& nm,
                                          float minValue, float maxValue, float def)
   : AudioParameterFloat (pid, nm, { minValue, maxValue, 0.01f }, def)
```

It **hardcodes an interval of 0.01**. So every slot had exactly 101 reachable positions,
for the entire life of the project, and a patch's declared default usually could not be
represented: an 800 Hz cutoff on a linear 20..20000 control needs slot 0.039039, got 0.04,
and came back as **819 Hz**. Measured, not inferred — writing 0.039039 straight to the
parameter and reading it back returned 0.040000.

Fixed by passing an explicit `juce::NormalisableRange<float>(0.0f, 1.0f)`, whose two-argument
constructor leaves `interval` at 0 (`juce_NormalisableRange.h:63-66`), i.e. continuous.

**Why nobody saw it.** The knobs displayed the raw slot, so the symptom of a coarse slot was
a slightly different meaningless number. PF-037 made the readout honest and the coarseness
became a wrong frequency on screen in the same session. That is the argument for fixing
display bugs even when the DSP is provably correct: an unreadable UI hides everything behind
it.

**Blast radius checked.** `StatePersistenceTest` passes — saved values are floats in the
ValueTree and restoring is unaffected; removing an interval is strictly more permissive than
adding one, so no previously-saved value becomes unrepresentable.

**Not verified.** Whether any DAW's automation UI relied on the 0.01 step for its own
increment behaviour. No host has been driven; only the plugin's own editor.

---

---

### PF-041 / PF-042 / PF-043 — the judge ran for the first time, and it has two design defects.
**high / medium / medium · open · S4 Testing · found 2026-07-30 by executing the control**

`bench/score_efficacy.py --judge` has been described since 2026-07-20 as *"the only fidelity
signal in the project"* and had **never been executed** (PF-013). It was unblocked by
installing ollama, which supplies a judge model independent of the generator at no quota cost.

**It runs.** 50 pilot records, 44 gradeable (6 never compiled), **0 judge errors**, digits
parsed cleanly. Mechanically the control is sound. That much is now known rather than assumed.

**PF-041 — L4 grading is tautological.** The rubric hands the judge
`effect["tiers"]["L4"]` as "expert-level specification of the target effect"
(`score_efficacy.py:439`). For L4 records, that string **is the generation prompt** — verified
byte-identical for **10 of 10**:

```
L4 generation prompt : A stereo resonant low-pass filter: 24 dB/oct ladder-style response, …
L4 judge ground truth: A stereo resonant low-pass filter: 24 dB/oct ladder-style response, …
```

So L4 scores 2.00/2.00 across the board by construction — the judge is asked whether code
written from instruction X satisfies instruction X. That is not an independent standard, and
it poisons the tier gradient: the apparent decline (L4 2.00 → L3 1.22 → L2 0.44) mixes "vaguer
prompts produce worse code" with "the ground truth exactly matches L4's prompt and diverges
progressively from the others." **The tier comparison cannot be read until this is separated.**
A fix needs a ground truth that is not any tier's prompt — an independent spec per effect.

**PF-042 — the middle of the rubric is unused.** Over 44 gradings: `0` → 21, `2` → 22,
**`1` → 1**. The documented category "partially implements the effect (missing key behavior or
parameters)" is returned 2.3% of the time. Whether that is the judge model (a 7B local model
is a blunt grader) or the rubric's phrasing is untested, but as it stands the instrument
returns a boolean while claiming three levels, and any mean computed from it is a proportion
wearing a 0-2 costume.

**PF-043 — the declared ollama model cannot hold the prompt plus the output floor.**
`providers.py:333` declares `default_model="qwen2.5-coder:7b"`, and ollama loads it with a
stock **4096-token** context. The system prompt measures **3,614 tokens** as sent, and
`min_max_tokens=4096` (PF-035) floors every request's output budget at 4096 — so the arithmetic
is 3,614 + 4,096 against a 4,096 window. Anyone following the repo's own instructions gets
~480 tokens of generation headroom and silent truncation beyond it.
**This is PF-035 acquiring a real cost.** That entry was filed `low` on the reasoning that the
floor "costs nothing measurable today"; on a 4k-context local model it costs the entire
generation budget. Worked around here by deriving `qwen2.5-coder:7b-16k` with
`PARAMETER num_ctx 16384`; the durable fix is to raise the declared default's context or
document the requirement in the spec's `notes`.

**Verified by canary, not assumed.** A token planted at the head of the system prompt was
echoed back intact at 3,614 prompt tokens, which is how the no-truncation claim above is
known rather than inferred from ollama's silence.

**What this does and does not close.** PF-013 had two halves. *"`--judge` has never executed"*
is closed. *"Semantic fidelity is unmeasured"* is **not**, for two independent reasons: the
scores above grade the **deleted** pre-unification prompt on the paid `claude` provider across
2 of 5 categories (PF-011), and PF-041/PF-042 mean the instrument's output is not yet
interpretable. Fixing the rubric is now a **prerequisite** for the measurement, not a
follow-up to it.

---

**PF-041 / PF-042 — fixed 2026-08-16, per ADR-027 §2 (`docs/decisions.md`).** Scoped exactly
as authorized: `bench/score_efficacy.py`'s offline benchmark path only, never wired into
`llm/generate.py` or any live request.

**PF-041 fix.** `run_judge()` no longer reads `effect["tiers"]["L4"]` as ground truth. A new
file, `bench/prompts/acceptance_specs.json`, holds one independently-authored acceptance-
criteria checklist per effect (25 effects), written fresh from each effect's real DSP
behavior and deliberately never copied or paraphrased from any tier's prompt text. A new
regression guard (`tests/test_efficacy_unit.py::TestAcceptanceSpecsAreIndependentOfTierPrompts`)
runs against the real shipped files (not a fixture) and asserts no spec matches or contains
any tier's text — this is the control that would have caught PF-041 before it shipped, and
now exists to stop it recurring.

**PF-042 fix.** Investigated rubric phrasing vs. judge-model bluntness per the ADR's own
instruction to test before deciding. Rubric phrasing was the tractable, cheap-to-test lever
(no larger/different judge model was swapped in — same `qwen2.5-coder:7b-16k`, same free
local provider, isolating the rubric as the only changed variable against the historical
baseline). `JUDGE_RUBRIC` was rewritten from an unguided holistic "0/1/2, partial means
missing key behavior" call into a numbered-checklist scoring rule: the judge evaluates each
acceptance criterion, then applies an explicit mechanical rule mapping criteria-met count to
a score. This converts a fuzzy judgment into a checklist-counting task, which a 7B model
handles far more consistently. Because the new rubric asks the judge to reason through
criteria before answering, the old first-digit-found parser would misread a `"(1)"` inside
that reasoning as the score — `_parse_judge_digit()` now reads from the end of the response
instead, covered by 4 new unit tests.

**Re-measured against the same 44-record set** (`bench/results/efficacy/pilot_20260720.json`,
the exact source behind the `0→21, 1→1, 2→22` baseline above), output written to
`bench/results/efficacy/pilot_20260720_pf041042_recheck_judged.json`:

| | old (L4-as-ground-truth, unguided rubric) | new (independent spec, checklist rubric) |
|---|---|---|
| Overall distribution (44 graded) | `0`→21, `1`→1, `2`→22 | `0`→12, `1`→9, `2`→23 |
| `1` (the collapsed category) | 1/44 (2.3%) | 9/44 (20.5%) |
| L4 mean | 2.00/2.00 (all 10, tautological) | 1.89/2.00 — `{2: 8, 1: 1, None: 1}` |
| Judge errors | 0 | 0 |

**L4 is no longer pinned at a perfect score by construction** — one L4 record graded `1`
under the independent checklist, which is impossible under the old ground truth (a record
cannot fail to match text that is itself the record's own generation prompt). This is the
direct, mechanical proof the tautology is closed, not an inference from the aggregate numbers
alone. **The apparent tier gradient reported above (L4 2.00 → L3 1.22 → L2 0.44) does not
survive the fix**: re-measured tier means are L4 1.89, L3 1.22, L2 1.33, L1 0.38, L0 1.33 —
L2 and L0 now score *above* L3, non-monotonic. This confirms PF-041's own diagnosis exactly:
"the apparent decline mixes 'vaguer prompts produce worse code' with 'the ground truth exactly
matches L4's prompt.'" The clean-looking decline was substantially an artifact; what remains
is noisier and, at n=8–10 per tier, not a claim this session makes any stronger reading of.

**What this does and does not close.** The instrument's output is now interpretable — L4 is
graded like any other tier and the middle score is a real, functioning category. **Not
closed**: PF-011 (the efficacy pilot's N=50/1-model/2-of-5-category sample still generalizes
to nothing — a bigger run is a separate, unstarted piece of work) and PF-013's second half
(semantic fidelity is measured now, but only over this same small, stale sample; a fresh run
against the current unified prompt has not been done). Fixing the judge was a prerequisite
for a trustworthy fresh measurement, not the measurement itself.

---

### PF-065 — "generate.py not found" when hosted as an installed VST3 (confirmed in REAPER). *(FIXED 2026-09-01, found 2026-08-19)*

> **Closed 2026-09-01.** Fix chain: PR #42 (config-file resolution source), PR #43
> (provider/model picker), PR #45 (`ee11db6` — install-layout: `install_release.sh` writes a
> venv + seeded `.env` + a merged `config.json` carrying `generate_script_path` **and**
> `python_path`; `PromptPanel::resolvePythonExe()`; the "Paths…" callout, gated during a run).
> **Clean-machine rehearsal PASSED**: `package_release.sh` → tarball → `install.sh` into a
> scratch `HOME` → REAPER started against it with no `PLUGINFORGE_*` and no `config.json`
> hand-edit → the human generated a working effect (provider `ollama`; local, zero
> credentials). Two install-adjacent follow-ups landed after: **PF-077** (`requirements.txt`
> omitted `google-genai` so the then-seeded `gemini` default couldn't run — PR #46,
> `52903d5`), and `.env.example`'s default switched `gemini` → `ollama` so a fresh install
> needs no credential at all. The rest of this entry is the original diagnosis.

**high · open · S1 Backend · user-reported, running the built VST3 in REAPER**

`PromptPanel`'s constructor resolves `generate.py` once (`host/Source/PromptPanel.cpp:136-157`):
env override `PLUGINFORGE_LLM_SCRIPT`, else walk up to 10 parent directories from the loaded
plugin binary looking for a sibling `llm/generate.py`. `currentExecutableFile` for a plugin
resolves via `dladdr` to the loaded `.so`'s own path
(`JUCE/modules/juce_core/native/juce_SharedCode_posix.h:609-624`), not the host process's.

- **Standalone / an in-tree-built VST3**: the binary sits a handful of levels under the repo
  root, so the upward walk finds `llm/generate.py` inside the 10-level budget. Works, but by
  layout accident.
- **The VST3 REAPER actually loads**: `host/CMakeLists.txt` sets `COPY_PLUGIN_AFTER_BUILD
  TRUE`, installing to `~/.vst3/PluginForge Host.vst3/Contents/x86_64-linux/`. Confirmed on
  the dev machine. There is no repo above `~/.vst3` — all 10 upward steps miss, `generateScript`
  stays default-constructed, and `PromptPanel.cpp:306` reports `"generate.py not found at "`
  with an **empty path**, which is why the on-screen error looks contentless.
- The only fallback, `PLUGINFORGE_LLM_SCRIPT`, is inherited from whatever process launched the
  DAW. A DAW started from a desktop launcher does not have it; a terminal session that exported
  it does. The plugin is a guest in the host's environment with no other channel.

**ADR-011 is wrong.** `docs/architectural_decisions/ADR-011-ipc-argv-subprocess.md`'s hardening
table marked "Locating `generate.py` from the installed binary" **Closed 2026-07-19** —
corrected to open, cross-referencing this entry, in the same commit that added this row.
`PLUGIN_HEALTH_PLAN.md:19-20` had already said the opposite ("not yet exercised") the whole
time; the ADR was never reconciled against it.

**Also on record and undisturbed by this defect**: `docs/distribution.md:46-49` documents a
product design that requires the user to export `PLUGINFORGE_LLM_SCRIPT` before every DAW
launch, with nothing in the installer, bundle, or code enforcing or detecting it —
`tests/test_release_packaging.py:105-106` only asserts the installer *prints* the string, never
that a plugin can actually resolve the script from it.

**Decision (2026-08-20, COLLABORATION.md §2 consult — human chose the option, not Claude).**
Presented three options: (A) bundle `generate.py` inside the plugin bundle and always resolve
relative to the loaded binary; (B) an installer-written config file; (C) teach resolution about
the fixed location `install.sh` already documents. Chose **C**: `docs/distribution.md`'s
`install.sh` already places the Python runtime at `$XDG_DATA_HOME/pluginforge` or
`~/.local/share/pluginforge` and only then tells the user to export `PLUGINFORGE_LLM_SCRIPT`
pointing at it — nothing previously checked that location directly. (B) was folded into (C):
the install location isn't user-configurable today, so a separate config file would record
nothing (C) doesn't already know. (A) was logged as a deferred, larger question rather than
solved here — it tends to reopen "should PluginForge vendor its own Python interpreter/deps,"
which is bigger than this bug.

**Partial fix landed** (`host/Source/PromptPanel.cpp`, `resolveGenerateScript()`, now a named,
externally-linked function above the constructor): added a third resolution step after the env
override and the upward walk — check `$XDG_DATA_HOME/pluginforge/llm/generate.py`, else
`~/.local/share/pluginforge/llm/generate.py`. Covered by a new harness,
`host/tests/PromptPanelPathResolutionTest.cpp` (5 checks: env override still wins over an XDG
install; the pre-existing upward walk still finds a dev-tree sibling; the new XDG_DATA_HOME
fallback; the new `~/.local/share` default; and the not-found case stays an invalid `File`),
run against scratch directories so this repo's own real `llm/generate.py` cannot mask what's
being tested. `cmake --build host/build --target PromptPanelPathResolutionTest` then running the
binary directly: 5/5 passed, exit 0. Wired into `tools/check.sh full`'s pure/no-display group and
into `.github/workflows/test.yml`'s matching build + run steps;
`pytest tests/test_control_wiring.py -k Ladder` (the CI/ladder-parity guard) passes.

**What this does NOT fix — still open, hence the status stays `open`.** The confirmed REAPER
repro was against `COPY_PLUGIN_AFTER_BUILD`'s dev-loop copy to `~/.vst3`, not against a real
`install.sh` install — that dev copy has no repo above it *and* no XDG-installed runtime, so the
new step 3 does not fire for it either. Reproducing the exact reported failure today still
requires exporting `PLUGINFORGE_LLM_SCRIPT` before launching the DAW — unchanged, and already
the mechanism `PromptPanel.cpp`'s own comment names as supported for this case. Closing that
residual either means running `install.sh` for real before the interactive-host session (Next
three #1), or later choosing option (A) above. Not claiming "fixed" while the reported repro is
unchanged — see ADR-011's own history of this exact mistake, just above.

### PF-066 — `EditorSessionTest` scenario 20's octave-hit-test assertion is stale against the instrument-conditional keyboard band. *(fixed 2026-08-25, this session)*

**low · fixed, this session · S3 Plugin UX · found while verifying an unrelated change (Ember
Console typography, `cf336ff`) — reproduced independently on a clean build of committed HEAD
with none of that change's diff present, so it predates it and is not caused by it**

`89268ec` ("feat: instrument-conditional keyboard band") changed `keyboardPanel` from an
always-laid-out, dimmed-when-disabled child (`addAndMakeVisible`) to one that starts and
stays invisible (`addChildComponent`) until `processor.isInstrumentForTest()` is true
(`PluginEditor.cpp:648-654`): `keyboardPanel.setBounds(keyboardArea)` is called **only**
inside `if (instrument)`. JUCE calls a component's own `resized()` only when `setBounds()`
actually runs on it — so for a fresh editor (no patch compiled yet, `isInstrumentForTest()`
false) or any effect patch, `KeyboardPanel::resized()` never fires at all, and everything it
positions (`octaveUpButton`, `octaveDownButton`, `octaveLabel`, `disabledLabel`,
`keyboardComponent` — `KeyboardPanel.cpp:109-127`) stays at JUCE's default `{0,0,0,0}`.

`EditorSessionTest.cpp:1560-1561` (scenario 20, "an effect patch disables the keyboard")
checks, on a brand-new `Session`, `s.editor.keyboardOctaveUpIsHitTargetForTest()` — which
resolves to `getComponentAt(octaveUpButton.getBounds().getCentre()) == &octaveUpButton`
(`KeyboardPanel.h:174-177`). Against a zero-size button this asks whether point `(0,0)`
inside a zero-area rectangle hit-tests as that rectangle, which JUCE's
`Rectangle::contains()` treats as false, so `getComponentAt` returns something other than
`&octaveUpButton` and the check fails. **311 checks, 1 failure**, reproduced identically on
a clean worktree build of committed HEAD (`915655c`) with zero of this session's font/timing
changes present — confirmed pre-existing, not introduced by `cf336ff`.

**Read as a test-currency bug, not a functional regression.** The assertion's own docstring
("the disabled overlay does not block the enabled octave controls") encodes the *pre-89268ec*
invariant — from back when the keyboard band was always laid out, just dimmed
(`89268ec`'s own diff removes the comment "Always in the layout... an effect patch's 'you
can't play this' is communicated by dimming... not by removing the control"). `89268ec`
deliberately inverted that design (an effect gets 80px less chrome instead of a dimmed
band), and correctly re-lays-out the band once a real instrument compiles —
`onFaustCompileSuccess`'s explicit `resized()` call (`PluginEditor.cpp`, same commit) covers
exactly that transition. Nothing found here suggests the octave controls are unreachable
once an instrument is actually loaded; the gap is a fresh/effect state whose keyboard band
is entirely invisible anyway, where "are its internal buttons hit-testable" is moot — the
test just never got updated to stop asserting it.

**Why this is `docs/BUGS.md`'s first record of it despite `89268ec` landing a day earlier**:
this project's own recurring finding applies again — "a control counts only once it has been
seen failing" (`CLAUDE.md`). Nothing indicates `EditorSessionTest` was run between `89268ec`
landing and this discovery; `tools/check.sh fast` (pytest-only) would not have caught it.

**Fixed, same session, once CI blocking every push on this branch made it worth the small
detour.** Removed scenario 20's stale `keyboardOctaveUpIsHitTargetForTest()` check (and the
now-unused `KeyboardPanel::octaveUpIsHitTargetForTest()` / `PluginEditor::
keyboardOctaveUpIsHitTargetForTest()` accessors it was the only caller of), replacing it with
a comment recording why the old assertion no longer applies — not a relocated equivalent
check elsewhere, since no other scenario compiles an instrument AND needs octave-button
hit-testing, and manufacturing one just to preserve superficial coverage would test a JUCE
implementation detail (zero bounds) rather than a real product invariant. Verified locally
(built and run against the working tree's actual current state, which also carries other
in-flight, uncommitted branch work): the octave-hit-test failure this entry describes is
gone; a different, unrelated failure remains from that other in-progress work, not from this
fix — not this defect's concern.

### PF-067 — `anthropic>=0.40.0`'s uncapped upper bound resolved to 1.0.0, which ships `httpx2` instead of `httpx`, breaking every `import httpx` in `llm/providers.py`. *(fixed 2026-08-25, `73f3263`)*

**critical · fixed `73f3263` · S1 Backend · found via CI on `design/ember-console`, reproduced
on any branch/PR pushed that day — confirmed unrelated to that branch's own content**

`requirements.txt:1` pins `anthropic>=0.40.0` with no upper bound. `pip index versions
anthropic` (run this session): latest is `1.0.0`; the last pre-1.0 release is `0.125.0`.
CI's own pip resolution log (`design/ember-console` run `32876117541`, `test` job) shows it
picking up `anthropic-1.0.0-py3-none-any.whl` and, transitively, `Collecting httpx2<3,>=2.0.0
(from anthropic>=0.40.0->...)` — a **different package**, not a version bump of `httpx`.
`llm/providers.py:56` still does `import httpx` directly (plus three more uses:
`:886,888,982`), so any environment where `pip install -r requirements.txt` resolves
`anthropic` to `1.0.0+` fails at that import — 14 test modules fail to even collect
(`ModuleNotFoundError: No module named 'httpx'`), and so does anything that imports
`llm/providers.py` at runtime, including `llm/generate.py` itself. This is not a
test-only defect: a fresh install of the real product breaks the same way.

**Confirmed pre-existing, not caused by anything pushed this session.** The last green CI run
on `main` (`31ba9414`→`9a6b39c`, through 2026-08-20) predates whenever `anthropic` 1.0.0 was
actually published; nothing in `requirements.txt`'s history changed (`git log` shows one
commit, the initial import) and `httpx` was never listed there directly — it rode in only as
`anthropic`'s own transitive dependency, silently, until that dependency itself changed shape
upstream. Every branch's CI is red on this today, including a rebuild of committed `main`.

**Fixed, same day, in a later session** — reversing the original "record it, don't fix it
now" call once CI blocking every branch made the cost of waiting concrete. Pinned
`requirements.txt:1` to `anthropic>=0.40.0,<1.0.0`, the narrowest fix of the three named
above: restores `httpx` (the transport `providers.py:56,886,888,982` already assumes) without
touching `providers.py` itself or vendoring anything. Verified against a fresh venv replicating
CI's exact install sequence (`pip install -r requirements.txt` then `pip install numpy scipy`,
per `.github/workflows/test.yml:36-45`) — `anthropic` resolves to `0.125.0`, `httpx` to a real
`httpx` (not `httpx2`) `0.28.1`, `import providers` succeeds, and the full unit suite
(`pytest tests/ -q -m "not integration"`) passes: 666 passed, 21 skipped, 17 deselected, 1
xfailed. Revisit if `providers.py` ever needs a `1.0.0+`-only feature — that would mean
doing the `httpx2` migration this fix deliberately deferred.

### PF-068 — Auto family selection silently broke on the second keystroke: `ComboBox::getSelectedId()` desyncs against `changeItemText()`, so `selectedFamilyId()` read "effect"/"synth" instead of "auto". *(fixed 2026-08-25, same session)*

**high · fixed · S2 Prompting UX · found while adding queue item 3's prompt-writing hint,
confirmed against a real X11 window, not just the headless test harness**

`PromptPanel::updateAutoFamilyLabel()` calls `familySelector.changeItemText(1, "Auto -> " +
displayName)` on every keystroke (`promptInput.onTextChange`), to keep the combo box's first
item showing the live "Auto -> Effect" / "Auto -> Drum Synth" preview. `ComboBox::changeItemText`
(`/home/losera/JUCE/modules/juce_gui_basics/widgets/juce_ComboBox.cpp:133-139`) rewrites the
item's *stored* text but never touches the *displayed* label. `ComboBox::getSelectedId()`
(`:250-256`) does not trust its own `currentId` — it returns `0` unless `getText() ==
item->text`, i.e. unless the label and the stored text still agree. The very first
`changeItemText` call after construction (the second keystroke, since the first
`updateAutoFamilyLabel()` call happens once at construction with an empty prompt) desyncs them
permanently: the label keeps whatever text `setSelectedId` last wrote, the stored item text keeps
changing underneath it every keystroke, and `getSelectedId()` returns `0` from then on for the
rest of that editor's lifetime.

`PromptPanel::selectedFamilyId()` treats any ID that is not literally `1` or `3` as `"effect"`
(or, on the synth side, falls through to `"synth"`) — there is no "0 means invalid, treat as
auto" branch. `familyForTest()` (and the equivalent production request-building path) checks
`selectedFamilyId() != "auto"` first and, if true, uses that value directly, **skipping
`resolveAuto()` entirely**. Net effect: any user who leaves "Auto" selected and types anything
has every subsequent keystroke silently routed as if they had explicitly picked Effect (or
Synth) — Auto's entire purpose, detecting drum/granular/generator language in the prompt,
stopped working the moment typing began. This is not cosmetic: it changes which family
`generate.py` is asked to produce.

**Confirmed with a genuine window, not just this project's headless `EditorSessionTest`
harness** (which never calls `addToDesktop()` — a real, separately-documented limitation,
`PluginForgeEditor::isTextEditorFocusTarget`'s own comment). Built an isolated repro linking
real `juce_gui_basics`/`juce_gui_extra` against a `juce::TopLevelWindow` with `addToDesktop()`
called for real: `getSelectedId()` read `1` immediately after `setSelectedId(1, ...)`, then `0`
after exactly one `changeItemText(1, ...)` call on the same item — byte-identical to the
headless harness's own behavior, ruling out "no peer" as the cause.

**Fixed** in `updateAutoFamilyLabel()`: read `familySelector.getSelectedId() == 1` into
`wasAuto` *before* calling `changeItemText` (so it reflects real state, not the
about-to-be-corrupted one), then, only if `wasAuto`, call
`familySelector.setText(newAutoItemText, juce::dontSendNotification)` immediately after.
`ComboBox::setText` (`:311-333`) searches for an item whose stored text already equals the
argument — which item 1 now does, since `changeItemText` just set it — and routes through
`setSelectedId()` internally, which is what actually keeps `currentId` and the label in
agreement. An explicit (non-Auto) selection is left untouched, since `wasAuto` is false for it.

Verified via `EditorSessionTest` scenario 42 (`host/tests/EditorSessionTest.cpp`), which
exercises the real async `onTextChange`/`pumpUntil` path end-to-end and would not have passed,
no matter how long it waited, before this fix — added specifically as the regression test for
this bug. Full suite: 343 checks, 0 failures. Also reproduced and confirmed fixed against the
standalone X11 repro above.

---

### PF-071 — the XDG-installed runtime is a stale, unconfigured trap. *(FIXED 2026-09-01, found 2026-08-28)*

> **Closed 2026-09-01**, same fix chain and same clean-machine REAPER pass as PF-065.
> `install_release.sh` now *creates* `~/.config/pluginforge/config.json` pointing at a fresh
> venv + `generate.py`, and `resolveGenerateScript()` consults `generate_script_path` before
> the XDG step — so a launcher-started DAW resolves the installed runtime, never the stale
> `~/.local/share/pluginforge/llm/` copy. On the verifying pass the resolved provider was
> `ollama` (free/local), confirming the paid-fallback path is no longer reachable on a real
> install. Original diagnosis below.
**high · open · S1 Backend · reproduced in REAPER and Carla while starting session 017 WP6**

`resolveGenerateScript()` (`host/Source/PromptPanel.cpp:110-144`) resolves `generate.py` in
three steps: (1) `$PLUGINFORGE_LLM_SCRIPT`, (2) a ≤10-level upward walk from the loaded
`.so` looking for a sibling `llm/generate.py`, (3) `$XDG_DATA_HOME/pluginforge/llm/generate.py`
else `~/.local/share/pluginforge/llm/generate.py`. Step 3 is PF-065's "option C" fix.

**On this machine step 3 resolves to a trap.** `~/.local/share/pluginforge/llm/` is a copy
from **2026-08-15** (`ls` shows every file `Aug 15 19:43`). In it:

- `providers.py:58` &mdash; `DEFAULT_PROVIDER = "anthropic"`. The repo flipped this to
  `"groq"` afterward; the installed copy never caught up.
- `generate.py:33` &mdash; `load_dotenv(Path(__file__).parent.parent / ".env")` resolves to
  `~/.local/share/pluginforge/.env`, **which does not exist** (only `.env.example` is
  installed).

So when a DAW is launched from a desktop launcher (no `PLUGINFORGE_LLM_SCRIPT` in its
environment) and the plugin is the `~/.vst3` bundle (`COPY_PLUGIN_AFTER_BUILD`, no repo
above it): steps 1&nbsp;&amp;&nbsp;2 miss, step 3 hits the stale copy, no `.env` loads,
`PLUGINFORGE_PROVIDER` is unset, `resolve_provider()` returns `anthropic`,
`assert_free("anthropic")` raises `PaidProviderError`, and the UI shows an **"anthropic
provider error."** Reproduced 2026-08-28 in **both REAPER and Carla**, identically.

**PF-065's partial fix made the failure worse, not better.** Before step 3 existed, this
case produced an honest empty-path *"generate.py not found."* Now it silently runs a
six-week-old runtime that defaults to the paid provider &mdash; a silent-wrong outcome in
place of a loud-missing one, which is the failure ordering this project's own conventions
put last.

**Same root shape hits Soundfetch.** `SoundfetchClient.cpp` finds its interpreter via
`SOUNDFETCH_BIN` / `PLUGINFORGE_SOUNDFETCH_PYTHON` / `PLUGINFORGE_PYTHON` / `python3 -m
soundfetch` &mdash; none set in a launcher-started DAW &mdash; so "Soundfetch cannot fetch
anything" is the same "plugin is a guest in an environment with none of its config"
problem, layered on top of PF-056 (the Freesound key is 403'd).

**Immediate mitigation** (`docs/sessions/017-phase2-interactive-host.md` &sect;0.0): `rm -rf ~/.local/share/pluginforge`
to delete the trap, and launch the host from a shell that sourced `.env` and exported
`PLUGINFORGE_LLM_SCRIPT` + `PLUGINFORGE_SOUNDFETCH_PYTHON`.

**Durable fix** &mdash; the plugin must carry its own configuration rather than inherit it
from the host process. Designed in `docs/research/plugin-evolution-ui-provider-architecture-2026-08-13.md`
&sect;4 / &sect;10.2; scoped as a narrow v1 in the ADR drafted alongside this filing
(provider/model request fields + an in-plugin picker + a plugin-read config file; keys stay
in `.env` for v1). The install-layout half &mdash; `install.sh` writing a fresh runtime +
`.env` and the plugin preferring a version-matched install over a stale one &mdash; is
PF-065's residual and belongs with its real fix.

**Not covered.** Whether a real `install.sh` run (as opposed to this hand-copied 2026-08-15
tree) produces a correctly-configured runtime today &mdash; `install.sh` has never been run
end-to-end on this machine (PF-065's own "What this does NOT fix" note).

---

### PF-072 / PF-073 / PF-074 / PF-075 — findings from the session 017 WP6 interactive REAPER session. *(all open, found 2026-08-28)*

The first fully interactive host session (`docs/sessions/017-phase2-interactive-host.md`,
run in REAPER 2026-08-28). It **closed STATUS.md Broken #1 and #2** — a human pressed real
QWERTY keys in a host and heard notes, and both plugin targets ran and were played/heard.
Four things came out of it that are not those wins:

**PF-072 — refine produced a 1-knob patch.** Generating "a warm analog reverb" fresh gave a
full patch; asking to add chorus via refine ("a warm analog reverb with chorus" over the
working reverb) gave a patch with **only a dry/wet knob**. The same prompt in "New" mode
gave a full patch. Investigated offline: `groq` was out of daily tokens by then so the exact
repro is blocked, but on `gemini` the refine mechanism is correct — surgical Add kept all 6
prior controls and added the 2 chorus ones (8 total), contextual Redo regenerated with 7.
So the 1-knob result is `gpt-oss-120b` generation variance (its output is
documented-noisy — PF-024/PF-031), not a broken refine path. Still: a refine that silently
drops every control is a bad experience. **Next:** re-run on groq when TPD resets; if it
recurs, it is a prompt/refine-preamble problem for that model, not a mechanism one.

**PF-073 — rough DSP swap.** "There didn't seem to be a very smooth transition between the
generates." Not characterised. The swap protocol (`FaustEngine`'s `audioBusy` drain guard,
compile-callback-before-`ready`) makes a brief silent gap during recompile expected; an
audible click or a multi-block dropout would be a defect. No `setParamValue not found`
flood was seen in the `tee` log (which only catches JUCE-level stderr, not the generate.py
subprocess). **Next:** a captured A/B recording of the swap moment.

**PF-074 — NaN during play.** A generated instrument patch produced NaN/Inf while being
played, requiring regeneration. `OutputGuard` is built to catch this and latch-mute even
for instruments (`OutputGuard.h:89`). `OfflineSynthRenderTest` passes 184/0 including its
NaN and sample-rate-change checks, so the audio path and guard are healthy on the tested
fixtures — this was a specific generated patch under a specific runtime condition (a note,
a parameter value, a sustained state). What was not captured: whether audio went *silent*
(guard worked, regen cleared the latch) or *stayed broken* (guard hole). **Next:** if it
recurs, note the exact patch source and the action immediately before, and whether MIDI
panic / stop-start / editor reopen recovers it short of regenerating.

**PF-075 — chord sounded like 5 voices from a mono engine.** `FaustEngine::noteOn/noteOff`
is strictly monophonic — one `currentNote`, one gate, last-note priority
(`FaustEngine.cpp:547-562`); there is no per-voice DSP cloning anywhere. The operator heard
"more than 3 voices, up to 5" playing a chord. Almost certainly **overlapping release tails
+ reverb**: each note is gated off when the next arrives, but its amplitude envelope and any
reverb tail keep sounding, so five fast notes stack into five decaying tails. That would
*confirm* the mono model and is musically pleasant. **Next (with a MIDI controller):** hold a
sustained 5-note chord — if all five sustain indefinitely it is real polyphony and a genuine
doc/engine contradiction; if four decay and one stays, it is tails and the entry closes as
"not a bug, worth documenting as a feature."

**Also observed, filed nowhere because they are pre-existing and known:** keyboard latency
under sustained load (the ~10.7 ms block quantization plus JIT compile — Broken #2b's block-
jitter gap), and the Synth producing no sound on a *dedicated* REAPER track (REAPER track
arm/monitor config, not a plugin bug — `OfflineSynthRenderTest` is green and the Synth
*did* play when on the sample track).

---

## Closed — archive

### PF-070 — a C++ compiler hang crashes the entire efficacy run (uncaught `TimeoutExpired`). *(fixed `73b538a`, 2026-08-29)*
**high · fixed · S4 Testing · found running the first complete 125-cell efficacy grid on ollama, one cell after PF-069 was worked around**

`bench/run_benchmark.py::validate_faust` (shared by both benchmark harnesses) ran
`faust -lang cpp … -o /dev/null` with `timeout=30` and did not catch
`subprocess.TimeoutExpired`, so a compiler hang (endless-evaluation-cycle shapes, deep
recursion — a 7B model produces these) crashed the whole efficacy run with a traceback, and
because the in-progress cell's record was never written, `--resume` deterministically
re-generated and re-hung on the same cell at temperature=0 — an infinite loop only a code
fix could break. Fixed by adding `except subprocess.TimeoutExpired: return False, "…did not
finish…"`, mirroring `llm/generate.py::validate_faust`'s already-correct handling exactly —
**confirmed while fixing this: `llm/generate.py`'s own validator does NOT share this hole**
(its "Not covered" question above is answered: no). `run_effect_tier`'s existing fallback
logic needed no change — it already turns a `(False, err)` return into
`terminal_reason = "compile_failed"`, which was already outside the halt-condition set, so
the grid correctly continues past a hung cell instead of stopping.
**How we know:** new regression test
`tests/test_efficacy_unit.py::TestRunEffectTier::test_compiler_hang_is_recorded_not_raised`
mocks `subprocess.run` to raise `TimeoutExpired`, confirmed to fail (uncaught exception,
matching the real crash) against the pre-fix code, and to pass against the fix.

### PF-069 — `run_efficacy_study.py`'s generation budget is hardcoded, and too small for a slow provider. *(fixed `73b538a`, 2026-08-29)*
**medium · fixed · S4 Testing · found running the first complete 125-cell efficacy grid on ollama**

`bench/run_efficacy_study.py`'s `GENERATION_BUDGET_S` was a hardcoded `140.0` module
constant with no environment override, unlike `llm/generate.py`'s
`PLUGINFORGE_GENERATION_BUDGET`. A 7B coder model running CPU-only through ollama regularly
took 60–90s per generation, exceeding the ~47s per-attempt cap derived from it; every such
attempt raised `providers.BudgetExhausted`, and the study's checkpoint-and-stop logic
(sound for a real daily-quota `Retry-After`) halted the entire 125-cell grid on the first
slow cell — the grid could not complete on any slow provider without a source edit.
Fixed by adding `generation_budget_s()`, which reads `PLUGINFORGE_GENERATION_BUDGET`
(falling back to the same `140.0` default on an unset or malformed value) — reusing the
product path's own env var rather than adding a second name, so the harness cannot drift
from the product path it is designed to mirror. `run_study`'s halt-on-timeout behavior is
deliberately left unchanged: it remains a real safety net against a genuinely hung/dead
provider, which this bug's stated impact never asked to remove.
**How we know:** new tests
`tests/test_efficacy_unit.py::TestGenerationBudgetEnvOverride` (env override changes the
budget total; a malformed value falls back to the default) confirmed to fail against the
pre-fix code (`AttributeError`/wrong value) and pass against the fix; the existing
`test_default_generator_gets_a_fresh_budget_per_cell` updated to read the new
`generation_budget_s()` accessor instead of the removed module constant.
**Not covered.** Whether the fast providers (groq/gemini) ever hit this — they generate in
1–15s, far under the cap, so this was never their bug.

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
(schemaVersion=1: Faust source + originating prompt + 64 APVTS values); setState
restores values then triggers an async recompile; unknown/corrupt/foreign blobs are ignored.
Retained metadata is `metaMutex`-guarded, never touched on the audio thread. Covered by
`host/tests/StatePersistenceTest.cpp` (round-trips through two processors 33/33, ASan/UBSan clean);
JUCE headers cited (`juce_AudioProcessorValueTreeState.h:375-395`, `juce_AudioProcessor.h:1306-1312`).
**Residual — DISCHARGED 2026-07-27.** The persisted-state **format is a §2 trigger-3
contract**, and the earlier claim of a plan-mode sign-off was never independently visible. It
has now been confirmed by the human directly, against the literal emitted document rather
than the doc comment describing it — which is what surfaced the amendment below.

**Amendment to v1 (2026-07-27): `<SlotLabels>` dropped.** v1 carried a slot→label hint node
documented as letting the editor label knobs during the async restore recompile. **Nothing
ever read it** — `setStateInformation` restores `<STATE>`, `faustSource` and `prompt` and
never looked the node up — so it was written on every save and consumed by no one. Removed
while v1 was still the only blob in the wild. Consequences:
- No schemaVersion bump, and no migration: children are resolved by name
  (`getChildWithName`), so an old blob's unrecognised `<SlotLabels>` is simply never read.
- The slot→label map is unaffected in memory; it was never sourced from the blob. Tests that
  need it read `PluginForgeProcessor::currentLabelsForTest()`, added for the purpose —
  `OfflineRenderTest.cpp` had been counting `<SlotLabels>` children as its only observable
  for "mapped param count matches the patch", and was repointed at the accessor.
- `StatePersistenceTest` now asserts the node's **absence** (five assertions, red-cased by
  re-adding the emitter and watching all five fail), so it cannot quietly return.

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
`llm/prompts/system_prompt.txt` and the (deleted) `bench/prompts/system_faust.txt` had drifted
substantially (a 16-line stdlib-highlights block, extra stereo-wiring rules, a different example set), and
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

### PF-025 — Benchmark harness had no concurrency guard, and overwrote `results.json` unconditionally. *(fixed 2026-07-27)*
**high · S4 Testing · `bench/run_benchmark.py`**

Two occurrences of one defect, six days apart:

1. **2026-07-21.** Two `--dry-run` invocations silently overwrote the committed 25-record Claude
   run the ADR-009 verdict rests on. Recovered from git. The fix was narrow — it separated
   dry-run output into `results_dryrun.json` and left the general case open. The comment
   recording it is still at the write site.
2. **2026-07-27.** Two full 25-prompt groq runs executed *concurrently*, launched six minutes
   apart by two agents that could not see each other (one from a session whose context had since
   been cleared, so no record of it survived into the second). Both were headed for the same
   `results.json`, so one run's evidence was going to vanish with no error and no trace of which
   half was lost. They also shared one free-tier token budget: the second run took
   `HTTP 429 — rate limit ... tokens per minute (TPM): Limit 8000, Used 4460, Requested 4019`
   on its **first** prompt after five retries. `classify_failures` files that as `transport` —
   a measurement corrupted by the collision rather than by the model, which is the worst kind,
   because it looks like data.

Caught before any loss: the duplicate was killed and `results.json` verified byte-identical to
the archived baseline.

**Fix, two-part because the incident had two failure modes.** (a) An `O_EXCL` lock at
`bench/results/.run.lock` carrying the holder's pid — a second concurrent run exits **2** with
the holder named. Deliberately *not* an `flock`: an flock releases the instant a killed
process's fd closes, which is correct for mutual exclusion and useless for the "who holds this?"
message that makes the failure actionable. A stale lock (holder gone) is reclaimed with a
warning, because a guard that stays latched after a crash gets deleted by the first person it
blocks. (b) Every real run now writes `results_<date>_<provider>.json` *and* copies to
`results.json`, so even sequential runs cannot overwrite each other's evidence, and the archive
no longer depends on someone remembering to `cp` before the next run.

**Seen failing before being believed** (CLAUDE.md's rule). The red case in
`tests/test_control_wiring.py::TestBenchmarkConcurrencyGuard` was run against the pre-fix
harness at `e6d5353` with the lock held: it ignored the lock entirely and began generating,
i.e. the test fails on the old code and passes on the new. The refusal path is exercised
end-to-end through the CLI (free — the lock is checked before any generation); the allow path is
asserted at function level, because proving it by running the harness would spend 25 prompts of
quota.

**Not covered.** Nothing stops two agents from colliding on any *other* shared resource in this
repo — the same hazard produced concurrent commits to this working tree during the same session.
That is an architecture question, not a benchmark one.

---

### PF-026 — CI was red on four consecutive pushes and nothing in the loop said so. *(fixed 2026-07-28)*

**Found** 2026-07-28, during a workflow audit — not by any control. The last green run on
`main` was `30181544354` (2026-07-26). Every push after it failed: `30295123178`,
`30296235090`, `30297455014`, `30299041776`. Meanwhile STATUS.md's *Works* section said
**"CI is green. `ae5d213` passed 2026-07-26"**, its Broken list held one item that was not
this, and `/orient` printed nothing about CI at all.

**Why every existing control missed it.** They all watch the code; none watched whether the
remote gate had reported. And the local ladder could not have caught it: `tools/check.sh`
builds four targets and runs one (`check.sh:96-97`), while CI additionally builds and runs
`OfflineRenderTest` and `PromptPanelThreadingTest` (`test.yml:167,191`). The failing test is
one the ladder has never executed — filed separately as PF-029. So the read half of the loop
said green about a smaller set of tests, the unread half said red, and the two never met.

This is the project's signature defect one level up — not a control that was wrong, a
control that reported to nobody. Same family as the five hooks that never fired
(`a5e0275`), the ADR-009 sync hook that verified a proxy, and the CI that was once green 17
commits behind. `tools/check.sh`'s own header names the class: *"believing a control runs
when it does not."*

**Fix.** A CI section in `tools/status_digest.sh`, printed immediately after repo state, so
`/orient` cannot open without it. It reports the newest completed run on the current
branch, its conclusion, its consecutive-failure streak, and **how far behind HEAD the
tested commit is** — a green run on an older commit is evidence about that commit only.
Three states, three banners: red, green-but-behind, and unreachable. The one forbidden
output is silence, because a short digest reads like good news — the inference that made
the deleted `attention-report` skill useless for weeks.

An unknown CI status is loud but **not fatal**: exit codes here mean "STATUS.md no longer
has the shape this script reads," and overloading them with "you aren't logged into `gh`"
would make the real signal ignorable on any machine without the CLI.

**Seen failing before being believed** (CLAUDE.md's rule). `TestDigestReportsCI` in
`tests/test_control_wiring.py` — 15 tests over the red, streak, stale-green, in-flight,
malformed, empty and unreachable cases, plus overreach counterparts asserting a clean pass
at HEAD raises no alarm. Mutation-tested 2026-07-28: with the red banner disabled, i.e. the
pre-fix behaviour, `test_red_ci_is_announced` fails and the other 14 still pass. A test seam
(`PLUGINFORGE_CI_RUNS_JSON`) drives every case offline; production never sets it.

**Not covered.** That `gh` reports GitHub truthfully, and that a human actually runs
`/orient` at session start. The first is trusted; the second is a habit, not a mechanism.

---

### PF-027 — `OfflineRenderTest` dies with SIGILL on the CI runner. *(fixed 2026-07-28; second cause split out as PF-036, 2026-07-30)*

The defect PF-026 was hiding — and it is **not** what the first draft of this entry said it
was. Correcting that misreading is most of the value here.

**What actually happens.** The bare run reaches the fourth patch and dies:

```
  bounded delay  (P6 #2/#10 — delay, with a BOUNDED line)   [10 OK]
  tremolo  (P6 #8 — 'it should breathe')
timeout: the monitored command dumped core
line 17:  4339 Illegal instruction     timeout 300 "$BIN"
```

Exit 132 = 128+4 = **SIGILL**. The first three patches pass all ten checks. Runner CPU is an
`AMD EPYC 9V74`; the workflow already prints `lscpu` flags for exactly this hypothesis —
libfaust's LLVM JIT emitting an instruction the runner does not implement. **Unconfirmed.**
The faulting frame has never been seen, because:

**Why it was misdiagnosed, twice.** `adab1fc` added a gdb post-mortem whose stated purpose
was "make the render harness's SIGILL describe itself instead of being guessed at." It
describes the wrong thing. `jassertfalse` breaks **only under a debugger** —
`if (juce_isRunningUnderDebugger()) JUCE_BREAK_IN_DEBUGGER`. So the bare run printed 19
benign JUCE assertions and continued, while the gdb re-run trapped on the *first* one, on
the *first* patch, and reported a `juce::Timer::startTimer` backtrace it never got past. Both
this registry's first draft and the 2026-07-28 workflow audit read that backtrace as the
cause. It is the post-mortem's own breakpoint.

**Partial fix landed:** `juce::ScopedJuceInitialiser_GUI` at the top of
`OfflineRenderTest::main()`. The APVTS ctor calls `startTimerHz(10)`
(`juce_AudioProcessorValueTreeState.cpp:265` → `juce_Timer.cpp:352`) and
`Timer::startTimer` asserts a MessageManager exists (`juce_Timer.cpp:336`); a test that
constructs a full processor should have one. Locally this takes the run from **19 assertions
and 4 leak reports to 0 and 0**, exit 0, PASS either way. Its real value is that the next
gdb post-mortem has nothing benign to trap on and should finally reach the SIGILL.

**Still open, and explicitly not fixed by the above.** SIGILL is a CI-only failure — the
same binary passes locally on this machine (Arch, LLVM 22.1.6) at every patch including
tremolo. Nothing here has been proven about the runner. The next red run's post-mortem is
the evidence to wait for.

**CLOSED 2026-07-30, and this entry was right while the summaries were wrong.** The
paragraph immediately above called it: it declined to claim the CPU was exonerated, named
the missing evidence, and said to wait for the next red run's post-mortem. That post-mortem
arrived at run `30501160287` and is diagnosed as **PF-036** — the JIT emits AVX-512 on
Azure's EPYC 9V74, which names `znver4` while the hypervisor masks the ISA out of the guest.

What went wrong was the *summarising*, not the investigation. This entry said "still open,
unproven." The registry row said "it was the missing MessageManager, **not the CPU**," and
STATUS.md's Works section repeated that as settled fact, adding that the instruction-set
hypothesis "was never evidence." A cautious finding was flattened into a confident one in
the two places anyone actually reads, on the strength of one green run — which, at a 1-in-5
failure rate, was 80% likely regardless of the fix.

This is a new shape of the project's signature defect. The prior instances were controls
that did not run. This is a control that ran, reported honestly, and had its finding
overwritten by a more quotable summary one layer up. The MessageManager fix in `144e023`
remains correct and necessary; it was simply never the whole story, and this entry never
claimed it was.

**Not covered.** Whether the tremolo patch is special or merely fourth. Whether the fault is
in JIT-compiled code at all. Both need the backtrace that does not exist yet.

**CLOSED `144e023`, 2026-07-28, by run `30409357504` going green at HEAD.** The
`ScopedJuceInitialiser_GUI` was the fix, not a diagnostic aid. It was pushed **alone**, ahead
of the rest of a sixteen-path working tree, specifically so the result would be attributable —
and it was: one commit, one run, green, including the `Run OfflineRenderTest` step that had
failed the previous four times.

**So the CPU hypothesis was wrong, and it was wrong for three days.** `lscpu` flags, the
`AMD EPYC 9V74`, CI's faust 2.70.3 against local 2.85.5, an `ud2` from libfaust's LLVM — none
of it was ever evidence, and the workflow comment that enumerates it says as much in its last
line (*"That is a guess"*) before two separate readings treated it as a finding anyway. The
actual cause was the thing the same comment dismissed: *"NOT the JUCE Timer assertion in the
log — that fires locally too, 19x, on runs that pass."* True, and irrelevant. `Timer::startTimer`
asserting and then proceeding with no MessageManager leaves the timer machinery in a state that
is fine until it isn't; locally it survived, on the runner it did not. "It fires locally on runs
that pass" ruled out the assertion as a *symptom* and was read as ruling it out as a *cause*.

**Three readings, three wrong answers, one cheap experiment.** The registry's first draft
blamed the Timer assertion via the gdb backtrace (wrong frame — the post-mortem's own
breakpoint). The correction blamed SIGILL at the tremolo patch (right symptom, invented
cause). The workflow blamed the runner's instruction set (never tested). What settled it was
pushing one commit by itself and reading one run. The lesson is not about JUCE: it is that
three sessions spent their effort on better hypotheses when the cheapest available experiment
was already sitting uncommitted in the working tree.

**Not covered.** Why it survives locally and not on the runner. The fix removes the
undefined-behaviour window entirely, so the difference no longer matters — but it was never
explained, and if a fifth timer-dependent construct ever appears in a headless harness, that
gap is where it will bite.

### PF-028 — §7's hook table described hooks that did not exist. *(fixed 2026-07-28)*

COLLABORATION.md §7 tabulates what is mechanically enforced. It listed
`check_adr009_prompt_sync.py` and `protect_human_owned.py` — both retired in `cf1d8e8`,
neither on disk — and omitted `check_prompt_invariants.py`, which is registered in
`.claude/settings.json` on Write/Edit/MultiEdit and running. Two of three live rows wrong,
in the one section whose job is telling a reader what is actually enforced, six days after
that section was last revised.

The registry has the mirror-image case on record: `1fc1092` found ten entries marked `open`
whose fixes were live. Declared-vs-actual runs in both directions, and neither direction is
detectable by reading the document that is wrong.

**Fix.** The table is corrected, and `TestHookTableMatchesReality` now asserts it names
exactly what `settings.json` registers, that every hook on disk appears in it, and that
every hook the prose calls retired is really gone. §7 gained the general rule this is an
instance of: **a document that describes a mechanism is either mechanically checked against
it, or dated and read-only.** The prompt has lived under that rule since `cf1d8e8` — it
cannot name a Faust function that does not resolve. Prose about mechanisms now does too.

**Not covered.** Whether each row's *description* is accurate. A hook can be listed
correctly and described wrongly; only reading the docstring catches that. Writing this fix
produced an instance immediately — the first draft of the corrected table credited
`check_prompt_invariants.py` with verifying the prompt's prose claims against the compiler,
which is `tests/test_prompt_claims.py`'s job in `check.sh full`, not the hook's.

---

### PF-029 — the local ladder does not run the tests CI runs. *(fixed `558ac96`, 2026-07-28)*

`tools/check.sh full` builds `PluginForgeHost`, `PluginForgeHost_Standalone`,
`PluginForgeHost_VST3` and `ParamPoolTsanTest` (`check.sh:96-97`) and runs exactly one of
them, the TSan target. CI builds those **plus `OfflineRenderTest` and
`PromptPanelThreadingTest`** and runs both (`test.yml:167,191`).

So two C++ test harnesses — one of them the objective half of the P6 battery, the other the
263-line threading contract for the generate worker — are executed only in CI. That is how
PF-027 survived four pushes with nobody able to reproduce it locally: not an environment
difference, an execution gap. `check.sh`'s header says it "invents no new verification, it
only wires up what is here"; these two were never wired.

The obvious fix is to add both targets to `level_full`, which costs local build time on a
level already budgeted at ~2 min. Not done in the session that found it: it lengthens the
gate everyone runs, and PF-027 means one of the two harnesses would be red on arrival if the
SIGILL turns out to reproduce anywhere but the runner.

---

### PF-009 / PF-010 — the prompt is measured again, and the fix has a directional result. *(fixed 2026-07-28)*

**PF-009 closes on a number that describes something that exists.** `bench/run_benchmark.py
--provider groq`, 25 prompts, $0, run 2026-07-28: **22/25 = 88% first-try compile**, archived
at `bench/results/results_20260728_groq.json`. Every prior number measured either the
deleted `bench/prompts/system_faust.txt` or a provider the project can no longer pay for.

**PF-010 closes on a genuine before/after, on one provider and one model.** The ordering is
what makes it a measurement rather than a coincidence:

| | commit | prompt | rate |
|---|---|---|---|
| before | run archived by `e3019c0`, 07-27 **14:50** | pre-`f3453c4` | **20/25 = 80%** |
| after | this run, 07-28 | post-`f3453c4` | **22/25 = 88%** |

`f3453c4` ("PF-024: teach the routing algebra, and stop teaching a construct Faust lacks")
landed 07-27 **14:52** — two minutes after the baseline was committed, so the two runs
straddle it and nothing else touched `llm/prompts/system_prompt.txt` in between
(`git log -1 -- llm/prompts/system_prompt.txt`).

**Report the classes, not the aggregate** — STATUS.md's own rule, and here it is the whole
finding. `bench/classify_failures.py --compare`:

```
compile rate  80% → 88%   (20/25 → 22/25)
  routing_arity      2 → 0   fixed 2
  unbound_variable   1 → 0   fixed 1
  recursion_cycle    2 → 1   fixed 1
  syntax:EXTRA       0 → 1   WORSE +1
  syntax:FLOAT       0 → 1   WORSE +1
```

`routing_arity` and `unbound_variable` are **exactly** what `f3453c4` targeted: it taught
`<:`/`:>`/`~` and deleted the instruction to use `let`, which Faust does not have. Both went
to zero. That is a directional prediction made before the measurement and confirmed by it,
which is worth considerably more than the aggregate.

**What this does NOT establish, and it matters.** +2 of 25 is roughly 1.1 standard errors of
a binomial at p≈0.85 (SE ≈ 7 points). **The aggregate move is inside the noise.** Two new
failure classes appeared in slots that were empty before, and single-instance classes cannot
distinguish a fixed defect from a resample. Nobody has ever run this benchmark twice on an
unchanged prompt, so **the noise floor of the 25-prompt harness is unmeasured** — filed as
PF-031. Until it exists, no prompt change smaller than roughly ±3 prompts can be called an
improvement on aggregate, and the per-class deltas carry the argument.

**Not covered.** One provider, one model, one run per arm. Semantic fidelity is not measured
by a compile rate at all (PF-013) — a patch that compiles can still be the wrong effect.

---

### PF-014 — real user prompts are recorded. *(fixed 2026-07-28)*

`log_user_prompt()` in `llm/generate.py`, called from `_run_subprocess_mode` on **both** the
normal and the exception path — a prompt that blew up is the most interesting kind to have.
One JSONL record per generation: timestamp, prompt, provider, model, success, reason,
attempts, the generated Faust, and the error.

**Isolation is the property that makes the log worth anything.** It is written only from the
subprocess entry points (`--json` / `--prompt`) that the C++ host invokes. The bench
harnesses call `generate_faust`/`generate_with_retry` directly and shell out only to `faust`
itself, so a benchmark run cannot inject 25 synthetic prompts into the record of real ones.
`tests/test_prompt_log.py::TestOnlyRealUserPromptsAreLogged` asserts both directions against
the source, rather than by running the benchmark — proving it live would cost 25 prompts of
quota, which is PF-025's lesson.

**Fail-open, deliberately inverted from this project's hooks.** A hook exists to stop the
work; this exists to observe it, so a full disk or an unwritable path costs a log line and
never the user's generation. The red case drives a genuinely unwritable directory
(`chmod 0o500`) and asserts the warning lands on **stderr** — stdout carries exactly one
ADR-011 JSON line and a log warning must never join it.

Default `logs/prompts.jsonl`, gitignored: what a person typed is observation data, not repo
content. `PLUGINFORGE_PROMPT_LOG` sets a path or takes `0/off/false/no` to disable. 19 tests.

**Not covered.** Nothing yet *reads* the log. Deriving benchmark prompts from real ones is a
deliberate manual step — an auto-generated corpus that feeds itself would be self-referential
in exactly the way PF-011 warns about. And a missing credential does not log: the precheck
returns before any generation is attempted, so there is no outcome to record.

---

### PF-031 — nobody knows how noisy the benchmark is. *(fixed 2026-07-30, found 2026-07-28)*

The 25-prompt harness has been run many times and **never twice on an unchanged prompt**.
So its run-to-run spread is unknown, and every historical claim of the form "the rate moved
from X to Y" has been made against an instrument of unmeasured precision.

The arithmetic is unforgiving at this n. At p≈0.85 the binomial standard error over 25
trials is ≈7 percentage points — nearly two prompts. Today's 80%→88% is ~1.1 SE. The 2026-07-19
note in `.prompt_baseline.json` reads "0.88, up from the 0.84 recorded 2026-07-16" — one
prompt of difference, reported as movement. Per-class deltas are worse off: a class holding
one or two instances cannot tell a fixed defect from a resample.

**The fix is cheap and nobody has spent it:** run the benchmark 3–5 times back to back with
the prompt untouched and record the spread. On groq that is 75–125 generations at $0, bounded
only by the free-tier pacing. Until it exists, the honest reporting rule is the one this
session used — lead with the per-class table, and treat any aggregate move under about three
prompts as unresolved.

**Why it was not done in the session that found it:** the same session was already spending
its quota window on PF-009/PF-010 (25), PF-012 (25) and PF-011 (125). Sequencing, not
disagreement — the runs share one rate limit and PF-030 means they cannot safely overlap.

**CLOSED 2026-07-30. Measured, and the answer is worse than the arithmetic predicted.**
Two back-to-back 25-prompt runs, same prompt file, same corpus, unchanged tree, 50 groq
generations: `results_20260730_groq.json` **88%**, `results_20260730_groq_2.json` **84%**.

**Rate spread: 4 percentage points — one prompt.** That alone retires the historical claims:
`80%→88%` was two prompts, i.e. half a spread above noise, and the `.prompt_baseline.json`
note reporting `0.84→0.88` as movement was reporting exactly one resample.

**But the rate spread understates it badly, and this is the finding worth keeping.** Two runs
can reach a similar rate through entirely different failures, and these did. Of the prompts
that failed at all across the two runs:

| prompt | failed | classes seen |
|---|---|---|
| a sidechain-capable compressor | 2/2 | `duplicate_symbol`, `unbound_variable` |
| a Karplus-Strong plucked string synthesizer | 2/2 | `recursion_cycle`, `recursion_cycle` |
| a sawtooth synth with an ADSR envelope | 1/2 | `syntax:FLOAT` |
| a simple plate reverb with decay time and damping | 1/2 | `delay_range` |
| a noise gate with threshold and hold time | 1/2 | `syntax:ENDDEF` |

**Exactly one prompt failed both times with the same class.** One more failed both times with
a *different* class each time. Three failed once and passed once.

So the project's standing reporting rule — "lead with the per-class table" — was **only half
right**. Per-class is the right unit, but a per-class count read off a *single run* is mostly
sampling. `classify_failures --compare` between two single runs compares two draws, not two
prompts. Every historical per-class delta on record, including this month's
`routing_arity 2→0, unbound_variable 1→0`, was computed that way.

**The rule that replaces it:** a defect is evidenced when the same prompt fails across
repeated runs *with the same class*. On today's data that is **one** defect — Karplus-Strong's
`recursion_cycle`, which has now held across four archives (07-27, 07-28, and both 07-30
runs). The sidechain compressor is evidenced as *unreliable* but has no stable signature.

**Not verified.** n=2 is the minimum that can detect instability at all, and it cannot
estimate the spread's own precision — a third and fourth run would tighten it. 50 generations
was the day's budget (groq admits ~57: 200k TPD ÷ ~3.5k per generation). Also unmeasured:
whether instability is model temperature (pinned to 0.0 here, so this is *not* sampling
temperature — it is provider-side non-determinism) or corpus-specific.

**Cost note for whoever runs this next.** Wall-clock is set by TPM, not TPD: each request
admits `prompt_tokens + max_tokens` = 3,283 + 4,096 = **7,379 against an 8,000/min bucket**,
so roughly one request per minute gets through and a 25-prompt run takes ~18-30 minutes
regardless of any client-side pacing. `PLUGINFORGE_MIN_INTERVAL` is not the binding pacer.

**AMENDMENT, same day, from PF-012: the noise is PROVIDER-SIDE, not inherent to LLM
generation.** Two ollama runs of the identical corpus at temperature 0, hours after the groq
pair:

| | run-to-run behaviour |
|---|---|
| groq `gpt-oss-120b` | rates 88%/84%; failure sets overlap on **2** prompts |
| ollama `qwen2.5-coder:7b` | rates 80%/80%; failure sets **identical**, classes identical, and **20 of 25 generations byte-identical** |

So "temperature 0 does not mean reproducible" is a statement about **groq**, not about language
models. A local model at temperature 0 is very nearly deterministic, and the 4-point spread
measured this morning is a property of the hosted provider — batching, hardware variation,
whatever it is — rather than of the task.

**This has a direct methodological consequence.** Prompt-regression testing does not have to
be statistical. A deterministic local provider can answer *"did this prompt edit change the
generated code?"* exactly and for free, in one run, with a byte-diff — which is a far sharper
instrument than a compile rate with a two-prompt error bar. groq runs then answer the
different question of *"can the shipping model handle it"*, where repeats remain mandatory.
`bench/check_prompt_regression.py` currently spends 9 groq generations to answer the first
question badly; it could answer it exactly on ollama for nothing.

**Not verified:** that ollama's determinism holds across model sizes, longer outputs, or GPU
inference (this was CPU-only). 5 of 25 generations still differed, so it is *near*-deterministic,
not deterministic.

---

### PF-012 — CLOSED 2026-07-30. The comparison exists, and it found a prompt defect.

**Method.** Same 25-prompt corpus, same system prompt, same harness, **two runs per model** —
because the 07-28 attempt's 18/20-vs-17/20 was, by today's measured noise floor (PF-031), a
one-prompt difference and therefore worthless. Models: shipping `groq/openai/gpt-oss-120b`
against local `ollama/qwen2.5-coder:7b-16k` (CPU, temperature 0, unmetered).

| model | run 1 | run 2 | failure classes |
|---|---|---|---|
| groq gpt-oss-120b | 88% | 84% | shuffle completely between runs |
| ollama qwen2.5-coder 7B | **80%** | **80%** | `routing_arity` ×4, `unclassified` ×1 — *identical* |

**Finding 1 — a 7B local model is within one prompt of a 120B cloud model on compile rate.**
80% against 84–88%. Given a 4-point noise floor, that gap is one to two prompts. Nobody
expected this; the working assumption was that a small local model would be far behind.

**Finding 2, and the important one — the failure profiles are disjoint and the small model's
is diagnostic.** Four of ollama's five failures are `sequential composition` arity errors, and
**two are on `trivial`-category prompts**: *"a mute toggle with one boolean parameter"* and
*"a polarity inverter with a bypass switch"*. Those are the easiest items in the corpus and
groq passes them without difficulty.

**CORRECTION, same evening, before any fix was attempted.** The paragraph that stood here
said this confirmed PF-024's 2026-07-27 note that the prompt *"barely teaches Faust's routing
algebra — `<:` and `:>` appear nowhere in it"*, and concluded the large model had been masking
a missing prompt section.

**That was wrong, and wrong in this registry's signature way.** The July 27 note described the
prompt *as it was then*; `a4f942e` changed it. At HEAD `llm/prompts/system_prompt.txt:37-47`
carries a full SIGNAL ROUTING section — all five operators, `par(i,N,E)`, `si.bus(N)`, and an
explicit warning that `_,_ : E` is a "2 outputs must equal 1 input" error. The claim was
written by quoting the registry instead of re-reading the artifact, which is the third recorded
instance of exactly that mistake and the first committed by the same session that was
documenting the other two.

**What the failing code actually shows.** Reading it, rather than the error label:

```faust
process = ba.bypass2(mute, _ , _);      // signals written into the argument list
process = ba.bypass2(bypass, _ * -1);   // mono effect handed to the stereo bypass
```

Both misuse a stdlib function's **argument contract**, not the routing operators. And the same
shape explains groq's surviving silent render (PF-032):

```faust
process = ef.gate_stereo(threshold, attack, hold, release, _, _);
```

`misceffects.lib:159` documents the usage as `_,_ : gate_stereo(thresh,att,hold,rel) : _,_` —
four control arguments, signal by composition. All three generations wrote the signal into the
argument list instead. **One root cause, two models, three failures**, and it is genuinely
untaught: the prompt covers routing operators and unit contracts but never says where the
audio enters a stdlib effect.

`classify_failures` labels the ollama cases `routing_arity` because the composition does fail
on arity. The label is accurate about the symptom and is what led the first reading astray —
noted under PF-024 as a taxonomy concern, not fixed here.

**What survives from Finding 2:** a 7B model fails where a 120B model does not, on the
corpus's easiest prompts, reproducibly. That remains the useful result. Only the inference
about *why* was wrong.

**Finding 3 — the noise is provider-side, not inherent.** See the PF-031 amendment below.

**Not verified.** One model per side, two runs each. The 7B result is a single model's
behaviour, not "small models generally". Quantisation, CPU-only inference, and the
`num_ctx 16384` override (PF-043) are all uncontrolled. ADR-008 can move off "Under
evaluation", but on the strength of a two-point comparison, not a survey.

**Bookkeeping:** the archives for this comparison are the first to record which model produced
them — see PF-044, found while running it.

---

### PF-012 — a cross-model comparison was attempted and got 80% of the way. *(superseded, see above)*

**Method.** The same 25-prompt corpus, the same prompt file, the same harness, one variable:
`PLUGINFORGE_MODEL=llama-3.3-70b-versatile` against the pinned default
`openai/gpt-oss-120b`. Both free, both groq, $0.

**Result over the 20 prompts both models completed:**

| model | first-try compile |
|---|---|
| `openai/gpt-oss-120b` (default) | **18/20 = 90%** |
| `llama-3.3-70b-versatile` | **17/20 = 85%** |

**The rates are the least interesting part.** Three prompts disagreed, and they disagree in
*both directions*:

```
prompt 02  "a mute toggle with one boolean parameter"      gpt-oss ✓   llama ✗
prompt 05  "a polarity inverter with a bypass switch"      gpt-oss ✓   llama ✗
prompt 12  "a ping-pong delay that bounces L/R"            gpt-oss ✗   llama ✓
```

llama fails two *trivial* prompts the default handles, and solves the ping-pong delay that
is the default's signature PF-024 failure. **The models do not differ by a scalar; they
differ by failure profile.** A comparison reported as "90% vs 85%" would have hidden the
only fact worth having — and it is direct evidence for the reporting rule this project
already adopted for its own failure classes.

**Why it is still open.** The llama arm was **truncated at prompt 21 of 25** and killed after
it spent 15 minutes on a single generation. Diagnosis: not a defect — groq throttles that
model harder, and each generation gets a fresh budget, so the harness legitimately honors a
long `Retry-After` per prompt. The backoff itself is correctly clamped
(`providers.py:658`, `budget.can_sleep(delay)` raises rather than oversleeping), which is
PF-019's fix doing its job. So five prompts are unmeasured, including four of the five
`generative` category where the default's remaining failure lives.

**What closing it needs:** re-run the llama arm alone, off-peak or with a wider
`PLUGINFORGE_GENERATION_BUDGET`, and compare the complete 25. Roughly 25 generations at $0.

**Not covered.** Two models on one provider is not the provider comparison ADR-008 framed —
that still needs a second free key (gemini or openrouter), which is not configured on this
machine. Whether "cross-model" in ADR-008 means cross-provider is a question for whoever
resolves that ADR, and it is the reason this row stays open rather than being reworded.

---

### PF-032 — compiling is not working: two patches render silent. *(open, found 2026-07-28)*

The render oracle over the fresh 2026-07-28 groq corpus:

```
16 passed, 2 failed, 4 unsupported (0-input generators)
  FAIL a warm analog-style low-pass filter with cutoff and res -- output is silent (rms 2.48e-08)
  FAIL a noise gate with threshold and hold time              -- output is silent (rms 0.00e+00)
```

**Both patches are inside the 22/25 = 88% headline.** They compiled on the first try and
count as successes. They produce no audio. So the metric this project has steered by for
months measures *the compiler accepting the program*, and the gap between that and "the
plugin does something" is at least two prompts wide on a 25-prompt corpus.

Of renderable patches the real rate is **16/18**, not 22/22.

**The two are not equally damning.**

- **The low-pass filter at rms 2.48e-08 is a defect.** A filter fed a ~0.28-RMS signal must
  pass something. Eight orders of magnitude down is a filter whose cutoff has been driven to
  DC or whose coefficients are degenerate — the PF-001 family of symptom, though PF-001
  itself is fixed and the denormalisation path is tested.
- **The noise gate at exactly 0.0 may be correct behaviour.** A gate whose threshold sits
  above the test signal's level *should* mute. That would make it a false positive of the
  oracle rather than a generation defect — the oracle asserts "not silent" without knowing
  the patch is supposed to be conditional. Unresolved; it needs the generated source read
  and the threshold compared against the harness's signal level.

**Why this was invisible until today.** The oracle's "17 of 17 renderable patches produce
usable audio" was true — of the **2026-07-19 corpus, all provider `claude`, generated by the
since-deleted prompt**. STATUS.md flagged that caveat on 2026-07-27 without anyone re-running
the gate against current output. The first run over a current corpus found this immediately.

**Not covered.** Whether either patch is *musically* right, which no oracle can answer — only
the P6 listening pass. Silence is the one wrong answer a machine can detect.

---

### PF-033 — Reopening a saved project resets every knob to the patch defaults. *(fixed 2026-07-28)*

**high · S3 Plugin UX · found 2026-07-28 by `EditorSessionTest` scenario 10, on its first
green run**

`ParamGridPanel::refreshParamKnobs()` seeded every mapped slot from the patch's declared
defaults, unconditionally, on every compile. That was correct when it was written and became
a data-loss bug the moment `LoadMode` existed.

**The failing path.** `setStateInformation` replaces the APVTS state with the SAVED values and
then recompiles with `LoadMode::Iterate` **precisely so that nothing resets them**
(`PluginProcessor.cpp:65-69`). The compile succeeds; the callback hops to the message thread;
it lands in `refreshParamKnobs`; the seeding overwrites every restored value with the patch
default. Measured, not inferred — a 4-param patch saved with slot 1 at 0.95 and slot 3 at
0.05 came back at **0.250 and 0.750**, which are exactly those slots' declared defaults.

So: reopen a saved DAW project and every knob is back at factory position — **but only if the
editor happened to be open.** That is the same conditional-on-the-UI defect as PF-020, running
in the other direction. PF-020 was the UI-layer seeding failing to run headless; this is the
same seeding running when it must not.

**Why nothing caught it for five days.** `StatePersistenceTest` round-trips 33/33 and never
constructs an editor, so the clobber cannot happen there. Nothing else constructed one either
— that is the hole `EditorSessionTest` was built to fill, and this is what was in it. The
class is familiar: PF-026's CI blindness, PF-029's ladder scope, the five hooks that never
fired. Every control was real; none of them was pointed here.

**Fix: delete the seeding, and do not replace it.** The processor already does the job
properly. `resetMappedSlotsToDefaults()` (`PluginProcessor.cpp:112-142`) covers all 64 slots
rather than only the mapped ones, zeroes the unmapped remainder so a stale value cannot
reappear under a later patch, uses the same `ParamMap` conversion, and runs inside the swap
protocol's safe window — after the `audioBusy` drain, before `ready=true` — which is the only
point at which slot values can be rewritten without `pushToFaust` concurrently reading them. A
message-thread write from the panel had none of those properties. `ParamMap.h` was dropped
from `ParamGridPanel.cpp`'s includes so the seeding cannot quietly return.

The reason the seeding mattered at all still holds and is still honoured: `pushToFaust`
denormalises, so a slot left at 0.0 maps to its zone MINIMUM (a 20 Hz cutoff = silence). The
processor's `Fresh` path guarantees that on every load, with or without an editor.

**Seen failing before believed.** `EditorSessionTest` scenario 10 fails on the pre-fix code
with the measured values above and passes after, and it prints both the saved and the restored
value on every run so the next reader does not have to trust the assertion's wording. Note the
first draft of that scenario moved slot 2 to 0.1 — which is that slot's own declared default —
so it passed whether the value was restored or reset. A test that cannot distinguish the two
outcomes is not evidence; the values were changed to 0.95 and 0.05 for that reason.

**Not covered.** Whether a real DAW's save/restore ordering matches
`getStateInformation`/`setStateInformation` called directly on the message thread, which is
what the harness does. A host that restores from another thread, or interleaves the restore
with a user-triggered compile, is not exercised and cannot be without a host.

### PF-034 — `EditorSessionTest` scenario 6 raced the message thread. *(fixed 2026-07-28)*

**medium · S4 Testing · found by CI on the harness's first pushed run**

`loadAndSettle()` waited for the processor's source of record to match and for the grid's
control count to equal an expected value. Both can be true while the grid still shows the
PREVIOUS patch: `currentFaustSource` is assigned on the **compile thread**
(`PluginProcessor.cpp:180-181`), whereas the widgets are rebuilt later, on the **message
thread**, via `callAsync`. When consecutive patches have the same parameter count — scenario 6
loads a 1-param patch over a 1-param patch — the count discriminates nothing, so the wait
resolved on the compile-thread assignment and the test read stale labels.

Green on this dev box every time. Red on the CI runner: `[FAIL] the label still followed`,
1 failure out of 61.

**This is the third instance of one shape in a single day**, and that is the reason it is
filed rather than quietly fixed. PF-027 was a defect that reproduced only on the runner.
PF-029 was a ladder that ran less than CI did. This is a test whose *timing assumption* held
locally and not remotely. Every one of them is the dev box and the runner disagreeing, and in
every one the dev box was the more flattering answer.

It is also the second time the SAME function got this wrong. The first version watched only
the control count, which was not a wait at all when the count already matched — caught locally,
because it produced obviously false passes. Adding the source of record fixed the loud half of
the bug and left the quiet half, and the quiet half needed a slower machine to show up.

**Fix.** `ParamGridPanel::refreshCountForTest()` — a counter bumped once per
`refreshParamKnobs`, on the message thread, after the widgets exist. `loadAndSettle` waits for
it to **advance** past the value read before the load. Advancement cannot be satisfied by any
prior state, unlike equality against an expected count, so the class of bug is closed rather
than the instance.

**Not covered.** No local reproduction exists — the race needs the runner's timing, and this
was verified by pushing and reading CI rather than by constructing a delay. That is the same
cheap experiment PF-027 was closed with, and the same one three sessions avoided in favour of
better hypotheses.

### PF-032 — Two compiling patches render silent. *(diagnosed 2026-07-28, not fixed)*

**high · open · S1 Backend · found by the render oracle 2026-07-28**

Both are **unit-contract errors on stdlib function arguments**, not audio-path faults, and
both are one line from working. Verified by rendering the generated code and a one-argument
variant of it through `bench/render_oracle.py`:

| patch | as generated | with the unit fixed |
|---|---|---|
| warm analog low-pass | rms **2.48e-08** (−132 dB) | rms **0.0114** (−18.9 dB), passes |
| noise gate | rms **exactly 0.0** | rms **0.0995** (−0.1 dB), passes |

**The low-pass divides its cutoff by the sample rate.** `vaeffects.lib:71` documents
`moog_vcf(res, fr)` as taking *"`fr`: corner-resonance frequency in **Hz**"*. The patch writes
`hslider("Cutoff [unit:Hz]", 1000, ...) : si.smoo : *(1.0/ma.SR)`, so it passes
1000/48000 = **0.0208 Hz**. The filter is doing exactly what it was told; a 0.02 Hz corner
removes everything. The model applied the normalisation that *some* DSP APIs want and this one
does not. (Its `res` slider also runs 0–4.0 where the same doc block, `:69-70`, says
normalised 0–1 — harmless at the 0.5 default, wrong at the extremes.)

**The gate converts dB to linear, and so does the library.** `misceffects.lib:164` documents
`thresh` as *"dB level threshold above which gate opens (e.g., -60 dB)"*, and `:188` shows the
implementation doing the conversion itself: `rawgatesig(x) = inlevel(x) > ba.db2linear(thresh)`.
The patch pre-converts with `ba.db2linear`, so −40 dB arrives as 0.01, is read as **0.01 dB**,
and becomes a threshold of ~1.0 linear. Nothing in a −18 dB test signal ever exceeds it, the
gate never opens, and the output is not merely quiet but **identically zero** — which is what
distinguishes this from the low-pass and is the tell for a double conversion.

**Why this is not the oracle's fault, and why the compile rate overstates the product.** Both
patches are valid Faust, both JIT cleanly, both would load in a DAW and produce nothing. A
compile-rate metric cannot see this by construction, which is the argument for the oracle
existing. 22/25 compiling becomes **16/18 of renderable patches actually producing audio.**

**Why the existing controls could not prevent it.** `check_prompt_invariants.py` resolves every
`ns.func` in the system prompt against the installed stdlib, so it guarantees the functions
exist. It says nothing about what their arguments **mean**. `tools/gen_stdlib_block.py` emits
names and arities; units and normalisation conventions are in the `//` doc blocks it does not
read. So the prompt teaches the model a vocabulary and none of the grammar of units, and Hz
versus normalised-frequency versus dB versus linear is exactly where a plausible-looking
conversion silently produces silence.

**Fix shape (not done here — it is a prompt edit, i.e. PF-024's territory and its evidence
bar).** Either extend the generated stdlib block to carry the argument units for the functions
whose doc blocks state them, or add an explicit rule plus a few-shot for the two conventions
that actually bit: pass frequencies in Hz unless the doc says normalised, and never pre-convert
dB for a function that documents a dB parameter. A prompt edit owes a benchmark statement per
`.claude/rules/tier2-evidence.md`.

**Not covered.** Whether these two are representative. Two patches out of 22 is a diagnosis of
two patches; the claim that *unit contracts* are a general failure class is a hypothesis this
supports and does not establish. The efficacy grid (PF-011) is what would size it.

**2026-08-28 — re-measured on the complete 125-cell grid** (`ollama` 7B, current prompt —
PF-011's sub-note). The prompt already carries the fix text (`system_prompt.txt:19-22`,
`:23-25`). Render-oracle over the two PF-032 shapes across all 5 tiers each:

- **`filters-01` (warm analog low-pass, the `moog_vcf`/Hz-normalisation shape):** 4/5 tiers
  compile; **1/4 renders identically silent (rms 0.0e+00), at L4.** The bug still reproduces
  on the 7B, on the *most* detailed prompt tier. L3/L2/L0 render fine.
- **`dynamics-03` (noise gate, the double-`ba.db2linear` shape):** 3/5 compile; **0/3 render
  silent** this run (rms 0.01–0.10). The gate half was **not reproduced** — but n=1 per tier
  and the 7B may simply not have written the double-conversion this time.

So: the Hz half of PF-032 still bites (1 clear silent render); the dB half is unreproduced
on this single grid. Neither is closed. `filters`/`dynamics` were also the two worst
*fidelity* categories overall (judge 0.57/2 each — PF-011 sub-note), which is consistent
with "compiles, sounds wrong" being their signature.

---

### PF-045 — `en.*` envelope times are in seconds, and the model converts to samples.
**medium · open · S2 Prompt · found 2026-07-31 by the render oracle's first tail run**

The corpus record for *"a sawtooth synth with an ADSR envelope"* compiles, JITs, and is not
silent. It fails two objective gates at once — `DC offset 1.000` and the new
`never_decays` — and the mechanism is one expression:

```faust
adsrEnv = en.adsr(attackTime * ma.SR / 1000.0, decayTime * ma.SR / 1000.0,
                  sustainLevel, releaseTime * ma.SR / 1000.0);
```

`en.adsr`'s time arguments are **seconds**. The patch converts milliseconds to *samples*,
so a declared 1000 ms release is passed as 48000 — a forty-eight-thousand-second release.
The envelope reaches sustain and stays there for the entire render, which is why the output
is a DC level rather than a decaying tail.

**This is the same class as PF-032, not a new one.** PF-032 was `ba.db2linear` applied to a
parameter that already documents itself in dB. This is a time unit instead of a level unit,
and the same root cause: `tools/gen_stdlib_block.py` emits names and arities, and the units
live in the `//` doc blocks it does not read (see the PF-032 detail above, "Why the existing
controls could not prevent it"). The prompt teaches a vocabulary and none of the grammar of
units.

**Why it was invisible until now.** Every pre-2026-07-31 gate is computed from a *continuous*
probe, and nothing decays while the input is still going. The DC gate would have caught this
one, but only because sustain happens to be non-zero; the general shape — an envelope or
feedback path whose time constant is wrong by three orders of magnitude — needs the burst
probe. See `bench/render_oracle.py`'s `tail()` header for what that check still cannot see,
including that every `generative` patch is outside it (zero-input patches raise
`UnsupportedPatch`); this record is reachable only because the patch takes an input.

**Not verified.** Whether this generalises. It is one record from one ollama run. The
strong version of the claim — that unit contracts are *the* dominant semantic failure class —
is what PF-011's efficacy grid would size, and it remains unsized. No prompt edit was made:
that owes a benchmark statement per `.claude/rules/tier2-evidence.md`, and the last two
prompt rules aimed at this model measurably did nothing (`c50855b`, `a451350`).

**2026-08-28 — re-measured on the 125-cell grid** (`ollama` 7B, current prompt). The fix
text is present in `instrument_prompt.txt:68-69` ("All envelope times are in SECONDS … Do
NOT multiply by ma.SR"). Render-oracle over `generative-02` ("a sawtooth synth with an ADSR
envelope") across all 5 tiers:

- **L4 / L3 / L2:** compile, but **oracle-blind** — zero-input synths raise
  `UnsupportedPatch` (exactly the gap this entry's "Not covered" already names, and the
  `phase3-pf045` plan's WP1 works around with a static source scan).
- **L1:** compiles and renders fine.
- **L0:** compiles, and the burst probe reports **"output never decays — loop gain at or
  above unity 1.75s after the input stopped"** (`never_decays` gate). A non-decaying
  envelope — the PF-045 shape, or the `en.ar`/`en.adsr`-misuse shape (`instrument_prompt.txt:63-65`).
  Not distinguished here; needs the generated source read.

So the failure **reproduces once** (L0) among the 2 oracle-visible cells, and the oracle is
blind to 3 of 5. The dedicated static-scan probe run the `phase3-pf045` plan specifies is
still owed — the groq probes for it all hit the daily token limit unrun.

Also, separately: `generative-02/L4-L3` failed a first attempt with `undefined symbol : freq`
(the model referenced `freq` without declaring it) — a `hallucinated_symbol` / missing-decl
error in the instrument voice contract, adjacent to but distinct from the unit bug.

---

### PF-046 — the audio gate measures the last benchmark draw, not the change under test.
**medium · open · S4 Testing · filed 2026-07-31**

`tools/check.sh audio` runs the oracle over `bench/results/results.json`, which is the file
every benchmark run overwrites. So the level's verdict is a property of whatever the model
last happened to emit. Measured 2026-07-31, same command, two corpora:

| corpus | result |
|---|---|
| `results.json` at HEAD `a451350` | 15 passed, **3 failed** (runaway gain; two silent) |
| `results.json` in the working tree (ollama run 2) | 16 passed, **1 failed** (PF-045) |

Both are red, with disjoint failure sets, from the same harness and the same code. At an 80%
compile rate a broken-but-compiling patch is near-certain in every draw, so this level is red
essentially always, for reasons unrelated to the diff being gated.

**This is the argument `corpus_main` already accepted, applied one step further.** Its own
docstring makes the case for keeping *tail expectations* non-enforcing: an expectation miss is
a joint property of the patch, the model's chosen defaults, and a threshold table, "and the
corpus is regenerated by a nondeterministic model on every benchmark run. Breaking the ladder
on that would make check.sh audio fail for reasons unrelated to the change under test, which
is how ladders come to be skipped." Every clause of that applies to `measurement.ok` too. The
asymmetry is that a gate failure names a real defect in a real patch — which is worth *seeing*,
just not worth wiring to the exit code of the level developers run before every push.

**Fix shape (not done — deliberately left to the human).** Point the level at a small
committed corpus that changes only when someone decides it should, and keep the mutable
`results.json` as a separate reporting run. That makes the level a statement about the
harness and the DSP path, which is what a ladder rung should be, and leaves generation
quality to the benchmark where it belongs.

**Consequence today.** `tools/check.sh audio` exits 1 on this tree and on HEAD. `fast` and
`full` are green. Do not read a green `full` as covering the audio path.

---

### PF-047 / PF-048 — a test that never ran, and an instrument that mislabelled its own output.
**both fixed 2026-07-31 · S3 Plugin UX**

**PF-047 — `scenario13_styleSwitchDoesNotThrash` was defined and never called.** The
control-style selector shipped with exactly one test, asserting the load-bearing claim that a
style flip restyles widgets in place rather than rebuilding them (so the `SliderAttachment`s
survive and no parameter moves). It sat at `EditorSessionTest.cpp:759` while the runner's last
call was `scenario12_readout()`. `tools/check.sh full` was green, CI was green, and
`EditorSessionTest` reported `PASS (68 checks, 0 failures)` — all of which were true and none
of which said anything about the feature.

This is the fourth recorded instance of the same shape: PAIR mode, the ADR-009 sync hook, the
five `PreToolUse` hooks with the wrongly-nested `settings.json`, and now this. The first three
were *configuration* believed to be running; this one is *code*, in the harness whose own
`PF_SUMMARY` line exists because "0 failures" was indistinguishable from "ran nothing". The
denominator was doing its job — 68 was visibly not 100 — and nobody was reading it.

Wired in; 32 checks, all green. Note what the existing parity control could not do:
`TestLadderRunsWhatCIRuns` compares *targets*, not scenario registration, so nothing
mechanical would have caught this. An uncalled scenario is invisible to every gate this repo
has.

**PF-048 — the design gallery attributed each fixture's groups to the previous record.**
`UiDesignGallery.cpp:229` printed `groups:` before the record's own header line, which cannot
be printed until `writeSnapshot` supplies the pixel dimensions. Read literally, the loop's
console said `03_effect_grouped__horizontal` had 04's sections (`Env, Filter, Fx, Osc`) and
`04_generator_grouped__horizontal` had none — while `manifest.json`, written from the same
`rec` in the same run, had all fifteen records correct.

Cosmetic in the sense that no captured data was wrong, and not cosmetic at all in the sense
that this is a *design instrument*: its entire purpose is that a human reads its output and
judges. An instrument that misattributes its own evidence is worse than one that omits it,
because the reader cannot tell. Moved below the header; `tools/ui_iterate.sh` re-run,
15 rendered, 0 broken, layout diff `no change` — confirming the fix touched only stdout.

**Also closed here, without a defect number:** group capture (`ParamInfo::group`) and style
persistence across `getStateInformation`/`setStateInformation` were both new behaviour with no
assertion anywhere. Group capture was read only by the gallery, which asserts nothing by
design. Both now have one: `scenario14_groupCapture` (expectations read off `faust -lang cpp`
for the exact patch, not guessed) and an extension to `scenario10_stateRoundTrip` that asserts
the reopened *panel* is in the restored style rather than merely that the processor stored the
string. Scenario 14 was seen failing — stubbing `info.group` to `""` turns it red on exactly
the two grouped cases and leaves both fallback cases green.

**Not verified.** Two editors open on one processor — the case `onUiStyleChanged`'s `callAsync`
hop exists for — is still untested. Scenario 14 pins Faust 2.85.9's box-emission order; if a
future libfaust changes how the filename wrapper is emitted, it will fail and will read as a
capture bug rather than a version change.

---

### PF-060 — Add-mode refines were gated by groq's rate limit on every provider, not the selected one.
**fixed 2026-08-13 · S1 Backend**

`preflight_prior_source()` (`providers.py:152`, before this fix) took no `provider` argument.
Its budget came from `request_ceiling()`, hardcoded to `GROQ_TPM_LIMIT - max_output_tokens`
(`GROQ_TPM_LIMIT = 8000`, a measured groq rate limit, not a context window). `generate_json()`
(`generate.py:520`, before this fix) called it with no provider either. So every refine — on
every provider, including ones with far larger context — was refused by groq's number. In
surgical ("Add") mode this is a HARD refusal (the "minimal, surgical change" contract Add
promises means a too-large prior source cannot silently fall back to a full regeneration), so
Add mode was broken on every large-context provider whenever the prior program's size, plus
the ~3.3k-token system prompt, exceeded groq's 8,000-token ceiling — regardless of which
provider was actually selected.

**This was reproduced live, not inferred from reading the code.** `docs/research/
plugin-evolution-ui-provider-architecture-2026-08-13.md` §1 traced the mechanism from the
2026-08-13 trial log (`logs/prompts.jsonl`: "A dual chorus effect" → succeeded, 1,669 chars;
"Add [...] a warm analog polysynth" → refused, `attempts: 0`; "A compressor" → succeeded,
2,043 chars; "A delay"/"A reverb" → both refused, `attempts: 0`) but stopped at "the preflight
runs for every selected provider... [that] can still be rejected by Groq's rule," an inference
from the code rather than a run experiment. §10.1 (added the same day, adversarial review
follow-up) closed that gap: the trial's actual refused payload — the exact 2,043-char
`faust_code` from the "A compressor" step — was replayed through the real `generate_json()`
against `provider="ollama"` (16,384-token context, a `num_ctx 16384` Modelfile) and
`provider="gemini"` (1,048,576-token context, measured live via the models API). **Both
refused identically** — `attempts: 0`, `prior_source_refused: true`, 0.00s elapsed, no network
call — confirming the gate was firing before either provider was ever asked, on a payload
(~4,497 estimated tokens) that fit easily inside either one's actual context. Bypassing the
gate (`generate_faust()` directly, no preflight), gemini then produced a **compiling** reverb
addition to that same prior program in 29.2s — direct evidence the refusal was payload
pressure against the wrong number, not the model being unable to do the job.

**Fix.** `ProviderSpec` gained `request_token_budget: int | None` (`providers.py`), populated
per provider with a cited source: groq reuses `GROQ_TPM_LIMIT` (unchanged); gemini and
anthropic are set conservatively below their measured/documented 1M-token windows; openrouter
is set conservatively below its default model's measured 131K, since `PLUGINFORGE_MODEL` can
override to a different openrouter model this spec cannot see at import time; ollama defaults
to 4096 (its stock runtime default — **see PF-043**, open, not fixed here) with a new
`PLUGINFORGE_OLLAMA_NUM_CTX` override for the `num_ctx 16384` setup `README.md` already
documents. `request_ceiling()`/`headroom_tokens()`/`preflight_prior_source()` all gained a
trailing, defaulted `provider` parameter — `provider=None` (every call site before this
change) reproduces the old groq-only figure exactly, so groq's measured behavior does not
change. `generate_json()` now passes the request's resolved `provider` through.

**Verified.** `tests/test_providers_unit.py::TestProviderAwarePreflight` and
`tests/test_generate_unit.py::TestGenerateJsonProviderAwarePreflight` replay the trial's actual
payload size and assert: groq still refuses it; gemini admits it; `provider=None` is
bit-identical to the old behavior; an unregistered provider name falls back conservatively
rather than raising. Full unit suite (604 tests, `-m "not integration"`) green, no regressions.

**Not verified.** Whether Add mode now behaves correctly in the running Standalone plugin — the
fix is Python-only and reasoned to reach the plugin via `PLUGINFORGE_PROVIDER` (no host changes
were needed; `PromptPanel.cpp`/`PluginProcessor.*` have zero `"provider"`/`"model"` request-JSON
fields today, so `generate_json()` always falls through to the env-resolved default), but that
chain was not exercised end-to-end through the host UI. Per `CLAUDE.md`, that listening/UI pass
is a human judgment, not delegable to a hook or a model.

---

### PF-061 — QWERTY rollover fired spurious notes, and QWERTY was dead until the piano was clicked. *(fixed 2026-08-15, filed 2026-08-27)*
**fixed `dcf0af5` (2026-08-15) · S3 Plugin UX**

Filed retroactively 2026-08-27 (ADR-031's ID-resolution check): the fix landed on
`main` in `dcf0af5` and was tracked only in `STATUS.md`'s prose ("PF-061, unfiled
tracking") and `docs/decisions.md`, with no registry row — exactly the "believe neither"
drift this file exists to prevent, one layer up (a defect with an ID and no row).

Two distinct problems in the QWERTY→note path, both closed by `dcf0af5`:

1. **Spurious notes from typing rollover.** `keyStateChanged` re-forwarded key state
   while a `juce::TextEditor` held focus. `juce::TextEditor::keyStateChanged` swallows
   key-DOWN but not key-UP while focused, and `juce::MidiKeyboardComponent::keyStateChanged`
   ignores its own argument and re-polls every mapped key on every call — so a key-up
   from ordinary fast-typing rollover could fire a note for a letter never registered as
   down. Fixed by suppressing forwarding while any `TextEditor` holds focus.
2. **QWERTY dead until the piano was clicked.** `KeyboardPanel::focusForPlaying()` is now
   called from `onFaustCompileSuccess` for a successful instrument generation, so QWERTY
   works without clicking the on-screen keyboard first.

**Not covered by the fix or this row.** The OS→JUCE keypress hop still has a
static-contract test, not an end-to-end one (STATUS.md "Broken #1") — an interactive
in-host QWERTY press is the only thing that closes it, and that is the WP6 listening pass,
not this filing.

---

### PF-062 — Freshly-loaded `PluginForge Synth`, before any patch is generated, output NaN/subnormal garbage instead of silence.
**fixed 2026-08-16 · S1 Backend**

Discovered while closing STATUS.md's "Get it into a DAW" item: `COPY_PLUGIN_AFTER_BUILD` was
flipped from `FALSE` to `TRUE` (`host/CMakeLists.txt:42,120` — a separate, ungated build/install
change, not a defect), which for the first time put a build built from current `main` — rather
than a manually-copied binary of unknown provenance — into `~/.vst3`. `pluginval
--strictness-level 5` against that binary failed 200/450 Audio-processing sub-tests: NaN and
subnormal samples at block sizes 64/128/512/1024 (all three tested sample rates), consistent
enough across the run to rule out `pluginval`'s random seed as the explanation (confirmed by
re-running with three different explicit seeds after the fix — all `SUCCESS`). The earlier
"clean" `PluginForge Synth.vst3` this project's evidence chain had never actually run pluginval
against was of unknown build origin — see STATUS.md Broken #2's stale "pluginval is not on
PATH" / "never installed" claims, both false at HEAD (`pluginval 1.0.4` at `~/.local/bin`, an
AUR install; the VST3 bundles already existed in `~/.vst3` with real `.so` binaries newer than
either build-dir copy).

**Root cause.** `PluginProcessor.cpp:251` — `processBlock`'s early-return path, taken whenever
`faustEngine.enterAudio()` returns `false` (`FaustEngine.h:264`: this happens exactly when
`ready == false`, i.e. no patch has ever been JIT-compiled). The comment there read "input
passes through untouched" and left the buffer alone. That is correct for `PluginForge Host`
(the Fx target): its input bus is required, so the buffer holds the host's real input audio and
passthrough is the right behavior. It is wrong for `PluginForge Synth` (the instrument target):
confirmed via `pluginval`'s own bus report, `Main bus num input channels: 0` — there is no input
bus, so there is nothing to "pass through." The buffer instead held whatever memory the
host/JUCE runtime had last written there, uncleared, and that memory could be NaN or a subnormal
float left over from prior processing.

**Fix.** `PluginProcessor.cpp:251-273` — inside the early-return branch, call `buffer.clear()`
when `getTotalNumInputChannels() == 0` (`juce_AudioProcessor.h:743`, a cached, `noexcept` field
read — RT-safe, no allocation). This only changes behavior for the Synth target; the Fx target
has a nonzero input channel count and keeps its existing passthrough exactly as before.

**Verified.**
- `pluginval --strictness-level 5` against the rebuilt `PluginForge Synth.vst3`: `SUCCESS`,
  repeated with three additional explicit random seeds, all `SUCCESS`.
- New test, `host/tests/OfflineRenderTest.cpp` "PF-062" block (built into both
  `OfflineRenderTest`, PF_IS_SYNTH=0, and `OfflineSynthRenderTest`, PF_IS_SYNTH=1): poisons a
  fresh, never-generated processor's output buffer with `NaN` before calling `processBlock`,
  then asserts the instrument build clears it (`! anyNaN && ! anyInf`) and the effect build
  still echoes the poison through untouched (proving the Fx passthrough path is unchanged).
  **Confirmed red-then-green**: temporarily disabled the `buffer.clear()` call (`if (false)`)
  and reran `OfflineSynthRenderTest` — the new check failed exactly as expected before the fix
  and passed after restoring it.
- `tools/check.sh full`: green except one pre-existing, unrelated failure (see below).

**Not verified.** Real-DAW scan/load behavior — no DAW or plugin host was installed on this
machine as of this session (Carla installation was in progress, pending the human running
`sudo pacman -S carla` themselves). `pluginval` covers the plugin-format contract; it is not a
substitute for a real host actually loading and playing the plugin.

**Unrelated finding surfaced by the same `tools/check.sh full` run, not fixed here (out of
scope):** `tests/test_control_wiring.py::TestDigestReportsCI::test_green_on_an_older_commit_is_not_reported_as_a_pass`
fails at HEAD — the `/orient` digest's CI-staleness banner does not include the required
"N commit(s) behind" phrase. Confirmed pre-existing and unrelated: this session's diff touches
only `host/CMakeLists.txt`, `host/Source/PluginProcessor.cpp`, and
`host/tests/OfflineRenderTest.cpp` — none of which the failing test or the code it exercises
touches. Needs its own investigation.

---

### PF-077 — the shipped `requirements.txt` can't run the installer's own default provider. *(fixed 2026-09-01)*

**medium · fixed · S1 Backend · found during the PF-065 clean-machine install rehearsal**

`tools/package_release.sh` ships the repo-root `requirements.txt` into the release tarball as
`runtime/requirements.txt`; `tools/install_release.sh` then `pip install`s it into the venv it
writes `python_path` at. That file listed only `anthropic`, `httpx`, `python-dotenv`, `pytest`.

`llm/providers.py`'s gemini adapter imports the Google SDK lazily — `from google import genai as
gai` at `llm/providers.py:680` (and again at `:991`) — so nothing fails at import of
`providers.py` itself; it fails only when gemini is actually selected. And gemini **is** what
gets selected on a clean install: `.env.example` (which `install.sh` seeds verbatim as `.env`)
sets `PLUGINFORGE_PROVIDER=gemini`. Net effect: a DAW started from a desktop launcher against a
fresh install, with no `PLUGINFORGE_*` overrides, runs `generate.py` → `providers.py` → gemini
→ `ModuleNotFoundError: No module named 'google'`.

**Scope.** Only gemini. `groq` and `openrouter` go over `httpx` (already pinned); `ollama` is
local HTTP over `httpx`; `anthropic` has its own pin. So three of five providers work from a
clean install and the PF-065 *resolution* mechanism (config.json → venv interpreter →
generate.py) is unaffected — verified end-to-end in the same rehearsal with
`PLUGINFORGE_PROVIDER=ollama` (`{"success": true, ...}`, no `PLUGINFORGE_*` set).

**How it was missed.** `bench/requirements.txt:2` has carried `google-genai>=1.0.0` since the
gemini adapter landed — the benchmark harness's deps were kept in sync, the shipped file was
not. `tests/test_release_packaging.py` writes an **empty** `requirements.txt` into its fake
package (`:25`) so `pip install -r` is an offline no-op — it never exercises the real
dependency closure, so no test caught the gap.

**Fix.** `requirements.txt` — add `google-genai>=1.0.0`, matching `bench/requirements.txt`.

**Verified.** Re-ran the clean-machine rehearsal from a fresh tarball: `install.sh` → venv →
`venv/bin/python3 llm/providers.py --check gemini` now resolves (reports `ok` / a key prompt,
not `ModuleNotFoundError`). `tools/check.sh full` green.

**Not fixed here (follow-ups):** (a) `test_release_packaging.py` still never installs the real
requirement set — a test that `pip install`s the actual `requirements.txt` into a throwaway
venv would have caught this and would catch the next omission; (b) nothing asserts
`requirements.txt` ⊇ `bench/requirements.txt`, or that every provider adapter's imports are
covered by a pinned dep.
