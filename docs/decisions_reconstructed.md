# Decision Log (Reconstructed)

**Note:** `docs/decisions.md` already exists as the primary ADR log (ADR-001 through ADR-009) and should not be modified. This file is an analysis companion — it synthesizes the same decisions into the requested format, adds cross-references, and flags any decisions that are implied by code but not yet formally documented.

Dates: no git history exists in this repository; all dates are taken from the ADR metadata inside `docs/decisions.md` and `docs/architectural_decisions/`.

---

### [001] Faust DSL as the LLM output target
**Status:** Decided
**Date approximated:** 2026-04
**Decision:** The LLM outputs Faust DSL code directly; no intermediate representation exists between LLM and compiler.
**Why:** Faust is a small, well-defined algebraic DSL with good LLM training-data coverage. Its compiler (`libfaust`) can be embedded as a library inside the JUCE plugin binary, enabling JIT without a system install. The standard library (`stdfaust.lib`) covers the vast majority of common DSP primitives, reducing the surface area of hallucinations.
**Alternatives considered:**
- Raw C++ generation (rejected: too verbose, too large a hallucination surface, no embedded compiler path)
- JSON IR intermediary (rejected separately in ADR-002)
- Cmajor DSL (evaluated empirically in ADR-007; Faust retained)
**Consequences:** The plugin binary links libfaust + LLVM (~30 MB added size). All prompts must be Faust-idiomatic. Changing DSL later requires rewriting every prompt and example.

---

### [002] Reject JSON IR as an intermediary
**Status:** Decided
**Date approximated:** 2026-04
**Decision:** No custom JSON intermediate representation will sit between LLM output and compiled DSP.
**Why:** A JSON IR schema is a second language the LLM must learn on top of DSP concepts. A transpiler between JSON and the target DSL would create a silent failure mode. Faust already serves as a higher-level IR.
**Alternatives considered:**
- JSON IR → Faust transpiler
- JSON IR → C++ transpiler
**Consequences:** Prompt engineering must target Faust semantics directly. If Faust is ever replaced, prompts must be rewritten from scratch (no DSL-neutral layer to swap underneath).

---

### [003] JUCE 7 as the audio plugin host framework
**Status:** Decided
**Date approximated:** 2026-04
**Decision:** The plugin host is built with JUCE 7 (C++17) targeting VST3 and AU formats from a single codebase.
**Why:** JUCE is the de-facto standard for cross-format audio plugin development. It provides CMake integration, a mature audio buffer/parameter system, and a UI toolkit. It generates VST3 and AU from one build.
**Alternatives considered:**
- iPlug2 (smaller community, less momentum)
- Raw VST3 SDK (no AU support without extra work)
**Consequences:** JUCE license terms apply (GPL or commercial). JUCE cannot rename plugin parameters at runtime — this forces the 64-slot pre-allocation design (see [004]).

---

### [004] Pre-allocate a fixed 64-slot parameter pool
**Status:** Decided
**Date approximated:** 2026-04
**Decision:** 64 `AudioParameterFloat` slots (`macro_0`..`macro_63`) are registered at plugin startup; generated patch parameters are mapped into these fixed slots.
**Why:** VST3 and AU specs require that the number and identity of parameters be fixed at load time. Generated patches have variable parameter counts. The 64-slot pool allows hot-swapping DSP without reloading the plugin or breaking DAW automation lane IDs.
**Alternatives considered:**
- Reload the plugin on each new patch (unacceptable UX; breaks automation)
- Fewer slots (64 is chosen as safe headroom — complex FM patches rarely exceed 20 params)
**Consequences:** The DAW always shows 64 parameters even if only 3 are active; display names mitigate this. `ParamPool::pushToFaust()` must remap slot indices to Faust labels after every compile. `ParamPool::remap()` is currently PARTIAL — JUCE does not officially support renaming parameters after construction, requiring a custom `AudioProcessorParameter` subclass in a future iteration.

---

### [005] 3-attempt LLM retry loop with compiler stderr feedback
**Status:** Decided
**Date approximated:** 2026-04
**Decision:** On compile failure, the Faust compiler's stderr is appended to the next LLM request; up to 3 total attempts are made before surfacing an error.
**Why:** Faust compiler errors are short, structured, and correctable by the LLM. The retry loop handles the majority of first-attempt failures without user intervention. 3 attempts caps API cost at a predictable maximum.
**Alternatives considered:**
- Surface raw error to user (poor UX)
- Unlimited retries (unbounded cost)
- 1 retry (insufficient; some errors require two correction steps)
**Consequences:** Worst-case latency is ~15 seconds (3 × ~5s round-trips at Opus-class models). `generate.py` owns the retry loop; `FaustEngine.cpp` is intentionally retry-unaware (separation of concerns).

---

### [006] First-try compile rate as the primary benchmark metric
**Status:** Decided
**Date approximated:** 2026-04
**Decision:** The benchmark measures the fraction of prompts for which the LLM's raw first response compiles without any retry, stratified by 5 difficulty categories.
**Why:** First-try success directly predicts average API cost and latency in the production retry loop. It is fully automatable — no human listening required. Stratification reveals per-category weaknesses.
**Alternatives considered:**
- Subjective quality rating (not automatable; deferred to manual review)
- Multi-attempt success rate (conflates LLM quality with retry loop behavior)
**Consequences:** Subjective quality (parameter range choices, musical appropriateness) is not captured by this metric. The benchmark explicitly excludes retry logic.

---

### [007] DSL selection: Faust vs Cmajor
**Status:** Decided
**Date approximated:** 2026-04 (evaluation), 2026-05-01 (closed)
**Decision:** Faust is retained as the sole LLM output DSL. Cmajor will not be integrated into the production pipeline.
**Why:** The 2026-05-01 benchmark (Claude, 25 prompts, 50 generations) yielded Faust 92% and Cmajor 64% first-try compile rate — a 28-point gap. Faust's two failures are both in the same category (time-based) and have distinct, fixable causes. Cmajor's 9 failures span 4 independent error classes (endpoint hallucination, float type mixing, reserved keywords, literal syntax), each requiring a separate prompt fix with uncertain coverage given Cmajor's lower LLM training-data representation.
**Alternatives considered:**
- Cmajor (benchmarked; rejected on quantitative grounds)
- Hybrid (generate Faust for effects, Cmajor for instruments; rejected: unnecessary complexity)
**Consequences:** Day-2 work is libfaust JIT wiring. Cmajor tooling (`cmaj`) remains installed for reference only. ADR-009 prompt hardening addresses Faust's remaining failures. If a future Cmajor-trained model closes the gap to <5pp, this decision should be re-opened.

---

### [008] LLM provider: Claude vs Gemini
**Status:** Tentative (evaluation pending)
**Date approximated:** 2026-04
**Decision:** Production `generate.py` hard-codes the Anthropic Claude API (`claude-opus-4-6`). Gemini will be benchmarked as an alternative before committing.
**Why:** Claude is already integrated and the retry loop has been tested against it. Gemini may offer different price/latency points. The benchmark harness supports both providers.
**Alternatives considered:**
- GPT-4 (not evaluated; not in benchmark harness)
- Gemini 2.0 Flash (in harness; benchmark pending)
**Consequences:** [NEEDS USER INPUT: Gemini benchmark run not yet completed as of 2026-05-04. Run `python bench/run_benchmark.py --provider gemini` to complete this evaluation. If results are within 5pp of Claude, prefer Claude (already integrated, retry loop tested).]

---

### [009] System prompt hardening against duplicate Faust symbol errors
**Status:** Decided
**Date approximated:** 2026-05-01
**Decision:** Add a single rule to both Faust system prompts: "define `process` exactly once; never redefine any symbol."
**Why:** All 3 Faust failures in the ADR-007 benchmark involved duplicate symbol definitions (two `process =` lines, or a reused variable name). One rule is estimated to eliminate all three.
**Alternatives considered:**
- Post-process LLM output to strip duplicate definitions (fragile; modifies generated code)
- Add more few-shot examples of complex patches (indirect; may not suppress the pattern)
**Consequences:** `llm/prompts/system_prompt.txt` and `bench/prompts/system_faust.txt` each gain one constraint rule. Benchmark should be re-run after applying the fix to confirm ≥96% Faust compile rate.

---

### [010] Single distributable plugin artifact (not server-side compilation)
**Status:** Tentative
**Date approximated:** 2026-04 (implied by architecture; not formally documented)
**Decision:** The entire pipeline — LLM call, Faust compilation, and JIT execution — runs inside the plugin process. No external compilation server exists.
**Why:** The architecture document describes libfaust LLVM JIT "embedded in JUCE host plugin" as a "single distributable artifact." The `compiler/Dockerfile` exists but appears to be an alternative compilation path, not the primary one. The Day-2 task is specifically to wire `libfaust` inside `FaustEngine.cpp`, confirming the in-process JIT approach.
**Alternatives considered:**
- Server-side compilation (lower binary size; requires network; separate deployment; not chosen)
- AOT compilation at generate time (would prevent live hot-swap; not chosen)
**Consequences:** Plugin binary links libfaust + LLVM. Startup time and binary size are increased. No network dependency at runtime for DSP compilation (only for LLM generation). [NEEDS USER INPUT: Clarify role of `compiler/Dockerfile` — is it a development utility, a fallback, or a deprecated approach?]

---

### [011] IPC mechanism between Python LLM layer and C++ plugin host
**Status:** Decided — ADR-011 ratified by the human 2026-07-19 and recorded in `decisions.md`
(argv one-shot subprocess; see also
`architectural_decisions/ADR-011-ipc-argv-subprocess.md`). The argv implementation shipped
2026-07-16; the paragraphs below preserve the pre-decision analysis as originally
reconstructed.
**Date approximated:** 2026-04 (implied by current stub; not decided)
**Decision:** No decision made. The `PluginEditor.cpp` generate button has a `// TODO Day 2: call LLM service` comment but no implementation or protocol defined.
**Why:** The Python `generate.py` script runs as a standalone process; the C++ plugin runs in a DAW process. These must communicate. Options include stdin/stdout subprocess pipe, local socket, HTTP, or embedding Python via pybind11.
**Alternatives considered:**
- stdin/stdout subprocess pipe (simplest; `generate.py` is already a CLI tool)
- Local TCP/Unix socket (more robust for streaming; more setup)
- Embedded Python interpreter (single process; higher complexity; license risk)
- Pre-compiled Faust library (bypasses Python entirely; would require rewriting LLM layer in C++)
**Consequences:** This is a blocking decision for Day-2 "Editor → LLM call" task. `docs/goals_and_next_steps.md` mentions "IPC pipe or embedded subprocess" as the implementation approach. [NEEDS USER INPUT: Confirm the intended IPC mechanism before implementing. stdin/stdout pipe is the natural path given the existing CLI shape of `generate.py`.]
