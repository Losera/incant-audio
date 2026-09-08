# PluginForge health and forward plan

Assessed 2026-08-11 at `55aeeb4` on `main`.

## Current health

PluginForge is a credible developer prototype with unusually strong internal test
infrastructure, but it is not release-ready. The Python unit/control-wiring gate is green
in this session (`tools/check.sh fast`). Existing status records describe broader local
checks as green, but the full/audio gates were not rerun here and remote CI could not be
queried. The repository is clean and tracks `origin/main`.

The strongest areas are the provider-neutral LLM/Faust pipeline, separate effect and synth
targets, real JIT/audio harnesses, sanitizer coverage, render oracle, prompt-grounding
checks, and an explicit cost-ordered verification ladder.

The main blockers at assessment were distribution truth and real-host evidence:
VST3/Standalone-only CMake despite AU claims, stale quickstart artifact names, and
installed-plugin discovery of `llm/generate.py` not yet exercised. Known product gaps
include the silent noise-gate case, raw DAW macro-slot presentation, Ollama context
mismatch, and incomplete end-to-end QWERTY coverage.

*Update 2026-09-07: several P0 blockers have since resolved — `LICENSE` is tracked (P0.1
done), a release path exists (`tools/package_release.sh` / `install_release.sh`), and
`pluginval --strictness 5` + a REAPER run are recorded in `STATUS.md`. Treat `STATUS.md`
as authoritative for current state; the checkboxes below have not all been re-ticked.*

One new inspection finding needs prompt reproduction: `FaustEngine::prepare()` writes the
plain sample-rate field before taking `compileMutex`, while the persistent compile worker
reads it during `runCompile()`. This may be a data race and may initialize a first patch at
the wrong rate. It is not called confirmed until a targeted test or sanitizer reproduces it.

## Work queue

Each task is deliberately small enough to review and verify independently. Do not combine
architecture, contract, dependency, or distribution decisions with implementation; those
remain human-gated under `COLLABORATION.md`.

### P0 — establish product and distribution truth

- [x] **P0.1 Preserve proprietary licensing.** The repository now carries an all-rights-
  reserved notice and no open-source grant. Before public or commercial distribution,
  confirm the JUCE/libfaust/LLVM distribution obligations with counsel.
- [ ] **P0.2 Correct build claims and quickstart paths.** Replace the stale `IncantAudio`
  artifact paths with actual PluginForge effect/synth paths. Either narrow current claims
  to VST3 + Standalone or approve a macOS/AU build plan. Verify commands from a clean build.
- [ ] **P0.3 Close the CI/ladder coverage mismatch.** `AuditionThreadingTest` and
  `ValidationGateTest` already build and run in CI. The ladder-to-CI structural check now
  classifies the reporting-only presentation metric and requires every deterministic gate;
  CI now includes the four gates it was missing. The guard was seen red before the workflow
  fix and is locally green afterward; remote green CI remains the completion gate.
- [ ] **P0.4 Perform the first installed-host smoke test.** Install both effect and synth
  VST3 bundles to an explicit user-local scan path, run pluginval, then scan/load each in one
  DAW. Record OS, host/pluginval versions, artifact hashes, scan result, generation helper
  discovery, audio/MIDI behavior, state restore, automation, and teardown. Do not globally
  enable `COPY_PLUGIN_AFTER_BUILD` until install semantics are chosen.

### P0 — investigate consequential runtime risks

- [ ] **P0.5 Reproduce or dismiss the sample-rate synchronization finding.** Add a targeted
  prepare-versus-paused-compile test/TSan scenario around `FaustEngine.cpp` sample-rate
  publication. If red, propose the smallest synchronization fix with JUCE/Faust primary
  header citations; then run the targeted harness and `tools/check.sh full`. This is Tier 2.
- [ ] **P0.6 Reproduce PF-045 envelope-unit generation.** Turn the frozen seconds-versus-
  samples example into a deterministic regression, fix only after seeing red, and run the
  prompt-required Tier-2 evidence path.
- [ ] **P0.7 Isolate the surviving PF-032 silent noise gate.** Reduce it to the smallest DSP
  and input fixture, determine generation defect versus oracle/input defect, add the corpus
  regression, then run targeted render tests and the audio gate.

### P1 — make builds reproducible and shippable

- [ ] **P1.1 Run a clean-checkout Release smoke.** In a temporary checkout, install only
  documented dependencies, configure/build both plugin targets, run non-network gates, and
  validate documented artifact paths plus `llm/generate.py` discovery/override behavior.
- [ ] **P1.2 Choose a supported-platform matrix.** Decide Linux VST3-only versus adding
  macOS VST3/AU and/or Windows. For each promised platform, define one Release CI build and
  one host-validation obligation before advertising support.
- [ ] **P1.3 Pin the toolchain contract.** Separate runtime and development Python
  dependencies or add constraints; document/pin JUCE and supported Faust/libfaust/LLVM
  ranges; test the oldest supported and current configurations.
- [ ] **P1.4 Create a staged release artifact.** Package effect, synth, and optional
  standalone bundles with licenses, README, version, hashes, and provenance. Add CI artifact
  upload first; signing/notarization follows only for approved platforms.
- [ ] **P1.5 Define release acceptance.** Require clean-checkout build, full/audio gates,
  pluginval, one DAW smoke, state restore, automation, MIDI/audition, tail behavior, and a
  human listening pass. Keep objective and subjective evidence distinct.

### P1 — close user-visible gaps

- [ ] **P1.6 Make Ollama context failure explicit (PF-043).** Detect an incompatible
  context/output budget before generation, produce a useful error, and test the declared
  default configuration without a network call.
- [ ] **P1.7 Complete keypress-to-note coverage.** Add a real synthetic keypress round trip
  on a supported display environment; keep the current static contract test as a cheaper
  layer.
- [ ] **P1.8 Decide DAW parameter presentation.** Document alternatives for generic stable
  macro slots versus dynamic-friendly labels/metadata, including automation/session
  compatibility. This is a public-host contract decision; obtain approval before changing it.
- [ ] **P1.9 Specify instrument tail behavior.** Test long-release instruments against the
  hard-coded two-second tail, define acceptable host behavior, and only then decide whether
  release metadata belongs in the generated patch contract.
- [ ] **P1.10 Finish or explicitly defer incomplete surfaces.** Treat PF-052 meter rendering
  and PF-053 export as separate deliverables; keep export gated until it produces a validated
  standalone project, and obtain UI-direction approval before meter work.

### P2 — evidence and maintenance

- [ ] **P2.1 Measure efficacy generalization.** Pre-register a larger, varied prompt sample
  and success rubric, run it without overwriting the protected baseline, and report variance
  and failure families rather than one aggregate score.
- [ ] **P2.2 Stress lifecycle latency.** Measure shutdown while libfaust compilation is in
  progress and exercise the audio-drain loop with adversarial block timing. Establish a
  latency/progress requirement before considering cancellation or timeout semantics.
- [x] **P2.3 Reconcile status documentation.** Deleted `docs/next_steps.md` 2026-08-19 (it
  routed to a since-deleted CLAUDE.md section) in the documentation purge —
  `docs/records/doc-purge-2026-08-19.md`. `STATUS.md` refreshed the same session. The stale
  RT-hook scope note in `CLAUDE.md` was not part of this pass — still open if found.
- [ ] **P2.4 Refresh CI evidence comments.** Replace old “first push” TODOs with links or
  durable references to actual runs after CI access is restored.

## Faust Dashboard decision

Recommendation: **experiment with the methodology; do not adopt or vendor the code now**.
Faustdashboard is a young compiler regression/history dashboard, not a plugin UI. It adds
controlled longitudinal comparisons of Faust revisions/configurations—compile success and
latency, generated-code correctness/size, and native runtime—which directly addresses this
project's documented silent toolchain drift. It does not exercise PluginForge's embedded
libfaust/LLVM JIT, background swap path, or DAW behavior, and upstream currently has no
declared license or dedicated test suite.

- [ ] **FD.1 Record a toolchain fingerprint.** Capture Faust/libfaust/LLVM versions, stdlib
  hash, C++ compiler, CPU, OS, and relevant flags beside benchmark evidence.
- [ ] **FD.2 Run a disposable two-version study.** Use 5–10 representative patches from the
  ladder corpus with current Faust and one candidate version; measure CLI compilation,
  reference-output drift, and native runtime without changing existing gates.
- [ ] **FD.3 Compare proxy and shipping-path metrics.** For the same corpus, measure embedded
  JIT compile latency, validation, first render, and output features. Determine whether the
  dashboard's CLI metrics predict actual PluginForge behavior.
- [ ] **FD.4 Make an evidence-based adoption decision.** If the experiment catches a useful
  regression missed by current gates, design a small PluginForge-owned append-only run format
  and static summary centered on embedded JIT. Otherwise stop. Ask GRAME to clarify license
  and stability before any code reuse.

## Verification snapshot

- Ran: `tools/check.sh fast` — pass (unit tests and control wiring).
- Initial sandboxed run was invalid because the checkout was read-only; it reported 559
  passes before write-dependent tests failed. The authorized rerun is the result above.
- Not run this session: `tools/check.sh full`, `tools/check.sh audio`, live-provider tests,
  pluginval, DAW scan/load, clean-checkout Release build, macOS/AU build.
- Remote CI: unknown; `gh run list` could not reach GitHub from the orientation environment.
