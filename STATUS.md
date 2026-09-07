# PluginForge — Status  (2026-09-04)

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
  no-`venv` fallback. **Clean-machine rehearsal PASSED 2026-09-01** —
  `package_release.sh` → tarball → `install.sh` into a scratch `HOME` → REAPER started
  against it with **no `PLUGINFORGE_*` and no `config.json` hand-edit** → the human
  generated a working effect, provider `ollama` (the seeded default; local, zero
  credentials), runtime resolved through the installed `config.json` + venv. PF-065 and
  PF-071 close on this. **One follow-up** (PR #46): the installer seeds
  `PLUGINFORGE_PROVIDER=gemini` but omits `google-genai` from `requirements.txt`, so the
  *seeded default* can't run until #46 lands or the seed changes to `ollama`. Deferred:
  in-plugin API-key entry (v2).

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

**Open, not yet landed — PR #65** (`feat/ui-face-wire`, ADR-035 A3b): wires the ui_face
producer (#59) into the compile-success callback so `GeneratedFaceLookAndFeel` (#61) sees a
real theme instead of always the Ember default — `PluginEditor.cpp`'s compile callback
builds the captured param table, calls `promptPanel.requestUiFace()`, applies the result via
the existing `applyUiIr()`/`applyGeneratedFace()` when it's a valid schema-3 answer, falls
back to the deterministic layout on any failure. **The redesign this PR actually is:** the
first attempt gave `ui_face` its own independent subprocess-spawning thread
(`UiFaceClient`), which reproduced a real `EditorSessionTest` hang (JUCE assertions in
`juce_TemporaryFile.cpp`/`juce_Component.cpp`, then a stall) — root-caused via gdb thread
dumps and `/proc/<pid>/task` inspection (ptrace was blocked in this sandbox; `wchan`/
`anon_pipe_read` on the stuck thread was the actual evidence) to two lifecycle races only a
**second thread** can create: (1) a real Generate arriving before a ui_face job's
`ChildProcess` registers is invisible to `activeChild->kill()` — a documented no-op on
null — so the subprocess started anyway and blocked the worker behind it; (2)
`ChildProcess::kill()` is SIGKILL to the **direct child only**, so an orphaned grandchild
(`sleep 30` under `/bin/sh`, exactly `EditorSessionTest` scenario 9's teardown probe) keeps
the pipe open and `readAllProcessOutput()` blocks on it regardless of the direct child
already being dead. **Fix:** route `ui_face` through `PromptPanel`'s existing, already-proven
worker thread as a second, lower-priority job kind instead of a second thread — there is
exactly one thread in the process that ever spawns a subprocess, same as before this
session; `UiFaceClient.h/.cpp` and an intermediate `SubprocessForkLock.h` (a narrower,
insufficient first fix attempt) are gone, not in the PR. Also fixed:
`FakeGenerator.h`'s capture-based test doubles (`writeSuccessCapturing`,
`writeRecommendation{,Failure}ThenSuccess`) were letting a background ui_face call clobber
the `request.json`/`argv.txt` files ~20 pre-existing scenarios assert against, since it hits
the same installed fake script — patched to bypass `action:"ui_face"` requests without
touching the capture (a test-harness fix, not production). **Verified:** every `check.sh
full` harness run directly (the wrapper itself kept getting killed by the sandbox on the
rebuild step, so each of the ~20 binaries was run by hand with the same flags/env);
`EditorSessionTest` 428/0 in ~60s (was hanging / 23-50 failures / 4-5 min before both race
fixes); `PromptPanelThreadingTest` (the PF-006 regression test) 9/0, teardown 423ms;
`ParamPoolTsanTest`/`AuditionThreadingTest`/`NoteRingTsanTest` clean under ThreadSanitizer;
every other harness green. PR CI: `test` passed, `build-host` still running as of this
write. **Not verified:** a real DAW/Standalone session with a live LLM provider —
`EditorSessionTest`'s `FakeGenerator` is the evidence so far; `docs/sessions/018-incant-ui-faces-and-shell.md` and
`docs/design/incant-ui/GENERATION_PLAN.md` still call this gap undifferentiated "A3", not
updated. Tier 2
(COLLABORATION.md §3, new threading surface) — **needs a human review before merge**, the
same bar #61 got and didn't (below).

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

**1. One generation defect is evidenced; the rest is sampling.** *(PF-024/PF-032, high,
open.)* Karplus-Strong's `recursion_cycle` fails to compile across every archive and the one
tier that compiles renders +79.6 dB runaway; the sidechain compressor fails every run with a
*different* error. `routing_arity` is now PF-024's dominant first-attempt failure class
(22/44 error strings on the 125-cell grid).

**2. The noise gate still renders silent.** *(PF-032's surviving half, high, open.)* Warm-LP
renders silent 1/4 at L4 on the grid.

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

**7. The declared ollama model cannot hold its own prompt.** *(PF-043, medium, open.)*

**8. `score_efficacy.py --judge` spends quota.** *(unfiled, medium, open.)* Takes a lock
(`bench/score_efficacy.py:558,569`); the quota cost is the real remaining half.

**9. Audible discontinuity on the 2nd-generation DSP swap in a host.** *(PF-073, low, open,
found 2026-08-28, needs a captured repro.)* "Not a very smooth transition." No `setParamValue`
flood in the log. A brief recompile gap is expected; a click is not.

**10. A chord sounded like ~5 voices from a strictly-mono engine.** *(PF-075, low, open,
found 2026-08-28.)* Almost certainly overlapping release tails + reverb from the mono voice —
the disambiguating held-chord test (all 5 sustain vs 4 decay) has not been run.

**11. Knob ordering is Faust's own.** *(PF-038, low, open.)*

**12. MIDI-fidelity gaps in a real session.** *(triaged 2026-08-16, all pre-existing, none a
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
`anthropic` pin (2026-08-25).

**Filed this session, on a branch not yet merged** (so no ID cited here until it lands —
ADR-031): the installer's `requirements.txt` omits `google-genai`, so the seeded-default
provider `gemini` can't run on a clean install. Fix + registry row in **PR #46**, `check.sh`
+ CI green, awaiting merge. Also unfiled: a granular-family observation from the REAPER pass
— `"pitch synchronous granulizer"` routed to `granular_effect` and the family control gate
(`llm/generate.py:435-448`) rejected every retry for missing required control groups; folds
into PF-024's family-failure sampling, notes in `scratchpad/pf065-reaper-observations.md`.

---

## Assumed, never checked

- **The efficacy tier gradient is unknown on the shipping model.** *(PF-011.)* The 125-cell
  grid ran once, on `ollama qwen2.5-coder:7b` (CPU) — compile rate tier-independent,
  fidelity monotonically declining. **Still assumed:** whether that gradient holds on
  `groq`'s `openai/gpt-oss-120b`. Progress since the last rewrite: 51 cells committed at
  `HEAD` (up from ~29), and the run has advanced to **89/125 in the working tree, not yet
  committed** — re-scored this session with `bench/score_efficacy.py` ($0, no quota spent):
  retry-corrected compile by tier is L4 16/18 (89%), L3 17/18 (94%), L2 17/18 (94%), L1
  15/18 (83%), L0 16/17 (94%) — noisier than "flat" but not a clean gradient either.
  **The heuristic semantic-pass-rate proxy *does* now show a monotonic decline** — L4 94% →
  L3 88% → L2 82% → L1 73% → L0 62% — consistent in shape with the ollama run's fidelity
  finding, though it is a heuristic (`expected_primitives` any-of match), not the judged
  score. Resume with `python bench/run_efficacy_study.py --provider groq --resume --out
  bench/results/efficacy/efficacy_groq_20260831.json` next quota window; still n=1 per cell
  (PF-031's ≥3-run bar unmet) and unjudged (`--judge` spends quota — defect #8).

---

## Next three things

1. *(evidence)* **Resume and commit the groq 125-cell efficacy run.** Same command as
   before — at **89/125 in the working tree, uncommitted** (51/125 is the last committed
   checkpoint). Resumes when the daily quota permits; it checkpoints harmlessly if not.
   Worth a `git add -p`/commit of the current progress before it resumes further, so a crash
   doesn't lose the 38 uncommitted cells. Moves PF-011 out of Assumed, the one number this
   project steers by. Score with `bench/score_efficacy.py` (compile rate first; `--judge`
   spends quota — defect #8).
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

1. **Post the GRAME reply.** `~/issue26-reply.md` — read in full this session, current and
   not stale: it cites `1ad968a` (PR #60, the *second* integrity pass — supersedes #55's
   `78fb9db`) and the corrected 58%/4% caret-preservation mechanism, not the earlier "edit
   at that spot and re-break it" framing #60 found backwards. Every link resolves
   anonymously; a cold clone at `1ad968a` runs `verify.py` green (367 checks). COLLABORATION.md
   §2 — **a human reviews the wording and posts it** (Claude must not `gh issue comment`).
   Delete the draft after. Then (deferred to a later session): edit the existing
   `issue-26-repro` release body **in place** to mark it superseded and repoint its Method
   link at `1ad968a`; optionally cut a new non-prerelease `issue-26-repro-v2` at `1ad968a`.
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
6. **The provider-resilience work moved, corrected this session** — it is no longer an
   uncommitted worktree. It was pushed as `origin/feat/provider-resilience` (`9f260a0`,
   "WIP: interactive provider resilience (fallback chain) — UNREVIEWED SNAPSHOT") and the
   worktree removed. Still needs a review/merge decision, just not at risk of being lost.
   `design/ember-console` + `origin/fix/ember-console-palette` still kept pending your
   triage (Ember Console repaint) — unchanged.
7. **PR #65 (A3b) needs a review before merge — this is now the pressing one.** ADR-035
   flags this step as new UI architecture (AGENTS.md §4); #61 (A3a) merged directly with no
   GitHub review recorded, and #65 is what actually makes that code live in production, so
   the gap #61 left open now matters. `~/.worktrees/ui-face-wire` (branch
   `feat/ui-face-wire`) holds the work; remove the worktree once #65 merges or is
   abandoned.
8. **Untracked personal files left alone**, as always — the two notes at the repo root, the
   unshipped brief skill, the product-architecture draft under bench/. The
   `design_handoff_generated_plugin_faces/` bundle from the last rewrite is **resolved,
   confirmed this session** — it was distilled into `docs/design/incant-ui/` in PR #56 (a
   dated, read-only point-in-time record per COLLABORATION.md §8) and the original bundle
   directory no longer exists on disk. Nothing left to commit there.
