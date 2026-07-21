# Next Steps

Generated: 2026-05-04. **Status refresh 2026-07-19: B5 and B8 — the last two open
items below — are now closed (ADR-011 ratified 2026-07-19; benchmark re-run 2026-07-19,
see B8). This document's Part 1/Part 2 content is historical record. The live
cross-track priority queue is the section immediately below; per-file status lives in
CLAUDE.md "Current status".**

---

## Priority Queue (2026-07-19) — cross-track, ordered

Executable task text for items marked P9–P12 lives in the root README's prompt series.

**1. HUMAN attention required (report-only — Claude does not execute these):**
- **Anthropic account is out of credit balance.** The P9 full 125-prompt efficacy
  run (2026-07-20) failed 125/125 with `BadRequestError: Your credit balance is too
  low to access the Anthropic API` — every request was rejected before generation
  (zero tokens spent, but zero data produced). This blocks P9, P8 (Gemini uses a
  separate account so unaffected), and the live `generate.py` path in the plugin
  itself. Top up billing, then re-run per docs/prompt_efficacy_study.md §7.2.
- ADR-009 verdict: the 2026-07-19 full re-run measured **22/25 (88%)** first-try —
  the ≥96% prediction did not hold. Two failures are exact repeats of 2026-05
  (ping-pong: SEMANTIC endless-cycle; flanger: HALLUCINATION `flanger_mono`), one new
  (sidechain: SYNTAX `unexpected ARROW`). Decide whether to author targeted prompt
  rules / few-shot examples (HUMAN-OWNED — see docs/prompt_efficacy_study.md §6, §9).
- `docs/audio_thread_example.md:126-130` — five unchecked human-verification boxes.
- PluginEditor.cpp SafePointer thread-safety reasoning — PAIR read-through pending.
- ADR-008 (Claude vs Gemini) — still "Under evaluation"; Gemini side never run (P8).
- ADR-011 hardening table — two rows still marked Open in docs/decisions.md
  (interpreter discovery: deferred to distribution; ready-state UX: implemented
  2026-07-19, row is stale) — ADR edits are HUMAN-OWNED, so the human closes them.
- Review this session's diffs (all DELEGATE): efficacy-study files, tiered prompt
  dataset (spot-check L0/L1 for tier-rule leakage), UI/UX planning docs, generate.py
  point-F hardening, baseline update.

**2. Prompt-efficacy study (primary track):** design + harness landed 2026-07-19;
pilot (filters+generative, 50 gens) run and scored 2026-07-20 — see
docs/prompt_efficacy_study.md §7.1. Headline: first-try compile rate is
non-monotonic (90%→50%→60% from L4 down to L1/L0); L1 (pure vibe/metaphor, no
effect-category or parameter words) was the hardest tier, harder than L0's
artist-reference tier. Retry recovers most tiers to 80–90%. A taxonomy gap
(ADR-009 duplicate-`process` regressions and arity-mismatch errors were
UNCLASSIFIED) was found and fixed in bench/score_efficacy.py during scoring.
**P9** (full 125-prompt run) was attempted 2026-07-20 but is BLOCKED, not run for
real — the Anthropic account had insufficient credit (item 1 above); all 125
requests were rejected pre-generation. Re-run once billing is resolved, then a
judge pass (~$3–5 total) to test the 3 remaining categories and confirm/deny H1–H4.

**3. P10 — JUCE/Faust ecosystem survey** — **DONE 2026-07-20.** 21 repos surveyed
(3 parallel research agents), merged into docs/juce_plugin_survey.md. Headline:
zero of 19 fixed-param entries used bare GenericAudioProcessorEditor, even at
1-2 params — supports the planned auto-layout as the UI floor. Also surfaced a
4th UI paradigm (declarative/GUI-Magic) not yet in ui_design_plan.md's taxonomy,
and found Effect-type plugins invest in custom UI more than §2 predicted (likely
survivorship bias — flagged, not treated as settled). Non-binding implications
in the survey doc §4; any taxonomy update is a separate future task.

**4. P11 — state persistence** (PAIR; spec in docs/ux_roadmap.md Phase 1). The
enabling step for all iterate/refine UX. Needs the human present for the PAIR draft
review; do not run unattended.

**5. P12 — UI design-plan execution** (docs/ui_design_plan.md §3: widget-kind field
in ParamCapture, auto-layout, >8 param surfacing). After P11.

**6. Background/deferred:** CI first real-runner push (5 TODO VERIFY in
.github/workflows/test.yml — unverifiable until a push happens); embedded code editor
(ux_roadmap Phase 3); N=3 efficacy repeats if tier deltas < 8 points.

---

## Part 1 — Immediate Decision: Cmajor Recovery Test — **RESOLVED, see ADR-007**

ADR-007 (2026-05-01) closed the DSL choice in favor of Faust, citing a 28-point benchmark gap. The benchmark data supports this, but before permanently discarding Cmajor consideration, a focused recovery test is warranted to determine whether the gap is prompt-engineering noise or structural.

**The question the recovery test must answer:** If the Cmajor system prompt is hardened against the specific failure patterns observed in the benchmark, does the 9-failure set shrink to ≤2? If yes, the gap is noise. If not, the failures are structural and Faust is confirmed unconditionally.

### Recovery test tasks

**T1 — Classify and document the 9 Cmajor failures** `[USER]`
Read `docs/benchmark_analysis.md` (already done). Confirm whether the classifications (3 hallucinations, 2 syntax, 4 semantic) match your understanding of the Cmajor language. You may have direct knowledge of the Cmajor stdlib that the benchmark cannot capture.

**T2 — Write the hardened Cmajor system prompt** `[PAIR]`
Add the following five rules to `bench/prompts/system_cmajor.txt`. Claude drafts; you review each rule against the actual Cmajor language spec before saving.

Rule candidates (based on benchmark failure analysis):
1. SVF endpoint: "The SVF filter (`std::filters::tpt::svf::Processor`) has exactly one output: `.out`. It does not expose `highpassOut`, `bandOut`, `notch`, or any other named per-mode output. To change the filter mode, you would need a different processor type."
2. Type casting: "Always wrap `processor.frequency` and `processor.period` in `float32(...)` before using them in arithmetic. All per-sample calculations should use `float32` unless explicitly needed."
3. Reserved keywords: "Never use `input`, `output`, `graph`, `processor`, `node`, `connection`, `event`, `value`, or `stream` as local variable, function parameter, or member names. These are Cmajor keywords."
4. Literal syntax: "Do not use C-style type suffixes on literals. Write `1664525` not `1664525u`. Integer type is inferred from context."
5. Graph connections: "Literal constants cannot be connected to node endpoints with `->`. To supply a constant to a node parameter, declare `input value float myParam` and use it."

**T3 — Extract the 9 failed prompts** `[CLAUDE]`
The 9 failed Cmajor prompts are:
- high-pass filter (filters)
- band-pass filter (filters)
- notch filter (filters)
- high-shelf filter (filters)
- chorus (time-based)
- tape-style flanger (time-based)
- plate reverb (time-based)
- FM synth (generative)
- Karplus-Strong (generative)

Create a file `bench/prompts/recovery_prompts.json` containing only these 9 prompts, preserving their category labels.

**T4 — Add a `--prompts` flag to the benchmark harness** `[PAIR]`
Modify `bench/run_benchmark.py` to accept an optional `--prompts <file>` argument that overrides `bench/prompts/prompts.json`. This lets the recovery test run against only the 9 failed prompts without touching the full 25-prompt benchmark. Claude implements; you review the argument parsing and the file-loading change.

**T5 — Run the recovery test** `[USER]`
```bash
python bench/run_benchmark.py --provider claude --prompts bench/prompts/recovery_prompts.json
python bench/score_results.py
```
Record the pass rate on the 9 previously-failed prompts.

**T6 — Interpret the result** `[USER]`
- If 7/9 or better pass: the gap is prompt-engineering noise. Proceed to the Cmajor migration path below.
- If 4/9 or fewer pass: the failures are structural. Proceed to the Faust confirmation path below.
- If 5–6/9 pass: marginal recovery. Consider whether the cost of continued Cmajor work is worth the benefit, given the existing Faust infrastructure.

---

## Part 2 — Post-Decision Paths

### Path A — If Cmajor recovers (gap closes to ≤10pp after prompt hardening)

This path applies if T6 shows ≥7/9 recovery.

**A1 — Formally update ADR-007** `[USER]`
Add a "Superseded" note to `docs/architectural_decisions/ADR-007-faust-vs-cmajor.md` and write ADR-010 documenting the switch to Cmajor, referencing the recovery test results.

**A2 — Audit Cmajor JIT embed path** `[USER]`
Research whether `cmaj` exposes a stable C++ embedding API equivalent to `libfaust`'s `createDSPFactoryFromString()`. The ADR-007 risk log notes "less documented." Confirm whether a `FaustEngine`-equivalent is feasible before proceeding. This is product-defining work that must come from you.

**A3 — Write CmajorEngine.h stub** `[CLAUDE]`
Parallel to `FaustEngine.h`, create `host/Source/CmajorEngine.h` with the same interface: `prepare()`, `release()`, `process()`, `compile()`, `isReady()`. Use the same `ParamList`/`CompileCallback` types. Claude implements; mark all methods as stubs.

**A4 — Write system_cmajor_production.txt** `[PAIR]`
The benchmark prompt (`bench/prompts/system_cmajor.txt`) is not the production prompt. Create `llm/prompts/system_cmajor.txt` based on the hardened version from T2, with production-appropriate few-shot examples covering at least: gain, lowpass filter, stereo delay, compressor. Claude drafts; you review each example against compiler output.

**A5 — Migrate llm/generate.py** `[PAIR]`
Add a `dsl` parameter to `generate_faust()` and `generate_with_retry()` (or rename to `generate_dsl()`). Wire in `validate_cmajor()` from the benchmark harness. Claude implements; you review the validation logic since Cmajor validation is more fragile (`cmaj play --dry-run` quirks).

**A6 — Port examples/ to Cmajor** `[USER]`
Rewrite `examples/gain.dsp`, `examples/lowpass.dsp`, `examples/chorus.dsp`, `examples/compressor.dsp` as equivalent `.cmajor` files. These serve as canonical reference patches and few-shot examples. Musical correctness and parameter range choices are product decisions — must come from you.

**A7 — Update tests** `[CLAUDE]`
Update `tests/test_project_structure.py` to accept `.cmajor` example files. Update `tests/test_generate_unit.py` to test the Cmajor generation path. Retire `test_faust_compile.py` or add a Cmajor equivalent.

---

### Path B — If Cmajor does not recover (gap remains >10pp)

This path applies if T6 shows ≤4/9 recovery, confirming ADR-007 conclusively.

**Note:** The `faust-archive` branch (referenced in project context) contains the Day-1 Faust scaffold. The current main branch already has the Faust infrastructure. Path B is therefore largely "stay the course and execute Day 2."

**B1 — Apply ADR-009 prompt hardening** `[CLAUDE]` — **DONE**, applied to both prompt files.
Add the duplicate-symbol rule from ADR-009 to `llm/prompts/system_prompt.txt`:
> "Every Faust program must define `process` exactly once. Never define the same variable or function name more than once in a single file. If you need intermediate values, use `let` bindings or `with { }` blocks inside the existing `process` definition."
Also apply the same rule to `bench/prompts/system_faust.txt`. Verify by re-running the 3 affected benchmark prompts (ping-pong delay, flanger — which are the actual time-based failures in the JSON; note the dynamics failures from ADR-007's earlier run may have been fixed already).

**B2 — Wire libfaust JIT in FaustEngine.cpp** `[USER]` — **DONE**, see
docs/audio_thread_example.md and CLAUDE.md.
Replace the stub with a real `createDSPFactoryFromString()` call. Launch the compile on a background thread. Implement the `std::atomic<llvm_dsp*>` hot-swap. This is audio-thread-critical code — must be written and reviewed by you. The interface is already stubbed in `FaustEngine.h`; the commented Day-2 fields show exactly what needs to be added.

**B3 — Implement ParamPool::pushToFaust()** `[PAIR]` — **DONE**, but the thread-safety
review this task called for is still open — see the activeLabels race noted in
ParamPool.cpp and CLAUDE.md.
For each active slot, call `engine.setParamValue(label, value)` with the current APVTS value. Claude implements the loop; you review the thread safety (this runs on the audio thread; the engine must be ready before calling).

**B4 — Wire Editor → LLM → loadFaustCode()** `[PAIR]` — **DONE** (2026-07-16), using
argv (`--prompt`) rather than stdin/stdout. B5 below (ADR-011) was never written first,
as this task required — the IPC contract still needs formal ratification.
Implement the IPC mechanism in `PluginEditor.cpp::generateButton.onClick`. The minimal path: spawn `llm/generate.py` as a subprocess with the prompt text on stdin, read the generated Faust from stdout, call `processor.loadFaustCode()`. Claude implements the subprocess launch; you decide the IPC contract (stdin/stdout pipe vs socket — see Decision [011] in `docs/decisions_reconstructed.md`).

**B5 — Add the missing IPC contract ADR** `[USER]` — **RESOLVED 2026-07-19.** ADR-011
(argv one-shot subprocess) ratified by the human and recorded in docs/decisions.md;
see docs/architectural_decisions/ADR-011-ipc-argv-subprocess.md.
Write ADR-011 documenting the chosen IPC mechanism (stdin/stdout pipe or socket) before implementation. This is an architectural decision with long-term consequences for how parameters, errors, and cancellation are communicated. Must come from you.

**B6 — Surface errors in the UI** `[CLAUDE]` — **DONE 2026-07-19.** LLM/generation
failures show in statusLabel, and Faust compile failures/successes now reach the UI
via PluginForgeProcessor::onFaustCompileError / onFaustCompileSuccess (ADR-011
hardening, point E of docs/pair_draft_editor_llm_bridge.md).
After all 3 LLM retries fail, display the Faust compiler error in `statusLabel` (truncated to fit). Currently the error is swallowed after `generate_with_retry` raises. Claude implements; pattern follows existing `statusLabel.setText()` calls.

**B7 — Add pytest CI workflow** `[CLAUDE]` — **DONE**, .github/workflows/test.yml.
Create `.github/workflows/test.yml` running `pytest tests/ -m "not integration"` on push. Fix `tests/test_llm_output.py` first — it currently runs without `@pytest.mark.integration` and would fail on a clean CI runner that lacks `ANTHROPIC_API_KEY`.

**B8 — Re-run benchmark after B1** `[USER]` — **RESOLVED 2026-07-19.** Full 25-prompt
re-run executed (P5): **22/25 (88%)** first-try, up from the committed 84% but short of
ADR-009's ≥96% prediction — the prediction did not hold. Failure detail and follow-on
options: docs/prompt_efficacy_study.md §6. Baseline
(bench/results/.prompt_baseline.json) updated 0.84 → 0.88 same day.
After applying ADR-009 prompt hardening, run `python bench/run_benchmark.py --provider claude` to confirm ≥96% Faust compile rate. This validates the prompt fix before shipping.

---

## Task ownership key

| Label | Meaning |
|-------|---------|
| `[USER]` | Must be written by you — load-bearing DSP code, architectural decisions, product-defining choices |
| `[CLAUDE]` | Safe to delegate — mechanical, pattern-following, no audio-thread risk |
| `[PAIR]` | Claude drafts first; you review and edit line-by-line before it lands |
