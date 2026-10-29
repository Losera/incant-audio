# Professional LLM generation: reliability, routing, quota, and UX

**Research snapshot — 2026-09-05.** This is an evidence and design proposal, not
an accepted architecture decision. It does not change the generation wire contract,
provider registry, prompt, credential storage, or audio path.

## Executive hypothesis and decision gates

**Hypothesis, not decision:** Incant Audio may be able to make local Faust synthesis a
dependable floor and treat a cloud model as an optional design planner. The current data
does not justify changing the shipped default (`llm/providers.py:58`, `groq`) or calling
the local route recommended: local semantic results miss the promotion bar below, GPU
latency is unmeasured, and a first run with Ollama absent or the model not pulled has no
designed recovery. The shipped provider contract and default remain unchanged.

If—and only if—the local evaluation and first-run work clears the gates below, propose a
new ADR for two product roles: candidate **Local Synthesis** for Faust generation and
compiler-driven repair, plus explicitly enabled **Cloud Assist** for a one-call design
brief. This note does not select those roles.

Separately, evaluate making the post-compile UI-face producer deterministic or local.
That would reverse shipped ADR-035 §5/A3b behavior: `host/Source/PluginEditor.cpp:366-372`
forwards the active provider/model into the `ui_face` subprocess. It therefore requires
an ADR-035 amendment rather than adoption as a principle in this note.

This proposal **activates ADR-030's re-evaluation**, rather than merely meeting it “in
spirit.” ADR-037 already records item 2 of 3; this proposal adds provider failover (item 3)
and may add error-specific repair branching (item 1). The required fresh ADR must compare
LangGraph with a hand-rolled dispatch table. The current research recommendation remains
the small typed runner because the proposed live path is bounded and one-shot, but that
conclusion is architecture input, not settled policy. LangGraph's relevant benefits are
durable execution, persistence, streaming, and human-in-the-loop interrupts [1]; none is
yet a demonstrated live-path requirement here.

Reopen the LangGraph decision only when the shipped workflow needs at least one of:

- a generation that survives DAW/plugin shutdown and resumes later;
- parallel candidate fan-out with partial-result recovery;
- a pause that persists state while waiting for human approval;
- a topology that cannot be reviewed more clearly as an explicit state-transition table.

## What is failing now

### Generation quality is not one failure class

An uncommitted partial Groq efficacy checkpoint contained 89 real generations when read
(`sha256:3eaac913fa020ad41f7fce0251297d5865d31ae37753e59cb97d4238e8cb324c`).
It is provisional evidence from another session's actively written working tree, not a
reproducible benchmark attached to this note:

| Measure | Result |
|---|---:|
| First-try compile | 70/89 (79%) |
| L4 first-try compile | 15/18 (83%) |
| L1 first-try compile | 9/18 (50%) |
| L4 heuristic semantic pass | 15/16 (94%) |
| L1 heuristic semantic pass | 11/15 (73%) |
| L0 heuristic semantic pass | 10/16 (62%) |

Thirteen of the nineteen first-attempt failures are labelled `routing_arity`; all
remaining failures are labelled parser errors. This is a classifier result, not a
validated root cause: the coarse label needs a blinded manual audit or inter-rater check
before it drives an intervention.

The complete local Ollama run is weaker on the first attempt but frequently repairable:

| Measure | Result |
|---|---:|
| First-try compile | 85/124 (69%; one transport record excluded) |
| Retry-corrected L4/L3 | 92% / 92% |
| Retry-corrected L2/L1/L0 | 84% / 84% / 88% |
| L4/L3 heuristic semantic pass | 91% / 91% |
| L2/L1/L0 heuristic semantic pass | 62% / 43% / 41% |

Twenty of the local model's 39 first-attempt compile failures are `routing_arity`.
Small local models can produce usable programs, but "it compiles after retries" is not
enough: semantic faithfulness falls hardest for musicians who describe intent without
DSP vocabulary.

Reproduction for the committed local artifact, without any provider call:

```bash
python bench/score_efficacy.py \
  --results bench/results/efficacy/efficacy_ollama_20260828.json
python bench/classify_failures.py \
  bench/results/efficacy/efficacy_ollama_20260828.json
```

The branch's committed Groq artifact is only the 51-cell checkpoint and will not reproduce
the table above. Recalculate and cite Groq results only after the owning session commits a
stable completed artifact; until then, do not use the provisional comparison as a
promotion decision.

### Better diagnostics alone do not rescue a small model

The 120-program repair A/B already rejects a tempting shortcut. On
`qwen2.5-coder:7b-instruct-q3_K_S`, ordinary C++ Faust stderr repaired 87/120 (72%) within
two corrections. Full `faust-rs` diagnostics repaired 60/120 (50%), and the reduced
variant repaired 57/120 (48%). More diagnostic structure caused more failures to remain
in the same class.

The next experiment should therefore ground the *solution shape*, not merely elaborate
the error message: inject one compiler-validated, family/error-specific routing example
for arity-heavy requests and keep the raw C++ stderr that currently performs best.

### Grammar constraints attack the minority problem

The repository's R1 evaluation found that a Faust context-free grammar would prevent
only the parseable subset of failures; arity, recursion, interval analysis, and symbol
resolution survive. `llama.cpp` supports arbitrary GBNF [2], while Ollama structured
outputs constrain JSON schemas [3], but changing inference servers to constrain syntax
would not address the dominant current `routing_arity` class. Do not replace Ollama or
add a grammar runtime in this work.

### Quota use is larger and less visible than the UI implies

At current `origin/main`, a successful generation schedules `ui_face` after compilation
and passes the same active provider/model. The user sees one Generate action, but the
provider can see:

- **New/Add/Redo:** one Faust call plus one UI-face call;
- **Plan:** one recommendation call, one Faust call, plus one UI-face call.

The UI-face failure is intentionally silent because deterministic layout is already
live. Silence is appropriate for visual fallback but inappropriate for quota accounting:
an invisible enhancement must not consume a scarce cloud allocation.

Free cloud allocations cannot be treated as production capacity:

- Groq's current free `gpt-oss-120b` limit is 200K tokens/day and 8K tokens/minute, and
  the API returns remaining/reset headers plus `retry-after` on 429 [4].
- Gemini evaluates RPM, TPM, and RPD per project; limits vary by model and project, and
  RPD resets at midnight Pacific [5]. An API key is not an independent quota bucket.
- OpenRouter documents roughly 50 total free-model requests/day until the account has
  purchased at least $10 of credits [6].

Quota status therefore has three honesty levels:

1. **Known** — learned from authoritative response headers/error metadata.
2. **Estimated** — locally counted use against documented limits.
3. **Unavailable** — the provider does not disclose enough information.

Never render an estimate as a guaranteed remaining balance.

## Candidate bounded pipeline — unbuilt

Every node below after prompt intake is a candidate. In particular, the repository has
only keyword-based effect/instrument routing (`llm/router.py:116`), not a validated DSP
family classifier.

```text
User prompt
    |
    v
Candidate target + family routing            $0, no network; accuracy unmeasured
    |
    +--> Cloud Assist enabled and available? -- no --> continue
    |            |
    |            v
    |       One bounded design brief
    |       429 daily -> open circuit -> continue without brief
    v
Local Faust synthesis
    |
    v
Compiler + metadata validation
    | invalid
    +--> classified local repair, at most 2 corrections after the initial attempt
         (unchanged current 3-attempt ceiling)
    |
    v
JIT publish
    |
    +--> deterministic layout immediately
    +--> optional local-only UI face
```

### State and transitions

Use plain typed data, not a workflow framework:

```text
GenerationRoute
  synthesis_profile_id
  planner_profile_id?          # absent means no Cloud Assist
  allow_local_fallback         # meaningful only for explicit cloud synthesis

StageResult
  stage                        # route | plan | synthesize | validate | repair | face
  provider / model?
  outcome                      # ok | skipped | retryable | terminal
  reason
  elapsed_ms
  usage?                       # tokens when supplied by provider

QuotaObservation
  profile_id / model_id
  state                        # available | throttled | exhausted | unknown
  source                       # headers | provider_error | local_estimate
  observed_at / reset_at?
```

These are proposal-level types, not additions to ADR-011 in this pass. Cross-invocation
quota state has no owner today. A host-owned circuit requires new ADR-011 fields; a Python
state file is a new persistent component. The follow-up ADR must choose and specify one,
including concurrency, corruption, reset, and rollback behavior. Until then, the diagram
must not be read as existing quota infrastructure.

### Routing policy

- Current default remains Groq. A local default is eligible for proposal only after the
  evaluation, interactive-latency, installation, missing-runtime, missing-model, and
  first-run recovery gates pass.
- Cloud Assist: one planner call capped at its existing 1200-token output; no automatic
  retry for a daily quota and at most one short wait for an explicitly short throttle.
- Cloud Assist failure: show the transition and continue with the original user prompt.
- Explicit cloud synthesis in Advanced: provider failures may fall back only to the
  selected local profile. Invalid Faust, truncation, or semantic/profile rejection stays
  with the selected model; changing models would erase the repair context.
- No automatic cloud-to-cloud transition. That prevents undisclosed prompt/source
  transfer and surprise cost.
- Per user action, allow at most one cloud planner request and no automatic planner retry.
  A 429 skips planning immediately. If explicit cloud synthesis remains available, its
  current three-attempt ceiling is the separate, visible cloud-call ceiling. A cloud
  `ui_face` call must be separately opt-in until ADR-035 is amended. Thus the candidate
  assisted-local path has a hard ceiling of one cloud call, not 4–5 implicit calls.
- Budgets cannot be independent inside today's one-shot process: planner time consumes the
  same ~140 s generation budget below the host's 180 s hard kill. True independence needs
  separate invocations or a host-owned deadline split and belongs in the ADR-011/control-
  flow decision. Until then, reserve a fixed synthesis floor and never sleep on planner
  backoff.
- A future quota circuit would be keyed by provider profile and model. Persistence and
  manual override semantics remain blocked on the ownership decision above.
- Benchmarks stay pinned to one provider/model and never use fallback.

### Small-local-model reliability work

Prioritize experiments that target the provisional failure hypothesis:

0. Blindly audit a stratified sample of `routing_arity` labels and measure agreement;
   stop if the category does not identify a coherent intervention target.
1. Create a small catalog of compiler-validated routing examples for mono duplication,
   stereo dry/wet, parallel split/merge, feedback, dynamics, and instrument voice wiring.
2. Select at most one example deterministically from target/family and the last compiler
   error. Keep the prompt prefix stable so local servers can reuse their KV cache;
   `llama.cpp` documents prompt-prefix caching and Ollama supports keeping a model loaded
   across calls [7][8].
3. Keep generated code self-contained. Do not create JSON IR, fork Faust, or make the
   runtime depend on a project-private Faust library in this experiment.
4. Preserve raw C++ compiler stderr; A/B only the targeted example injection.
5. Judge promotion on compile, semantic checks, latency, and listening—not generic coding
   leaderboards. Faust is too specialized for benchmark reputation to substitute for the
   repository corpus.

## Professional interaction model

The [interactive prototype](../prototypes/llm-generation-ux.html) accompanying this note
demonstrates the target information hierarchy. Reproducible review states are also captured
at [wide](../prototypes/llm-generation-ux-900.png),
[narrow](../prototypes/llm-generation-ux-700.png),
[settings](../prototypes/llm-generation-ux-settings.png), and
[quota fallback](../prototypes/llm-generation-ux-quota.png) widths/states.

### Main authoring surface

- Replace provider dropdown + free-text model row with one **engine pill** next to the
  primary action: `Local · Qwen Coder 7B`, green/amber/red status dot, and a disclosure
  chevron.
- If local promotion gates pass, test musician-facing route presets such as **Local** and
  **Cloud Assist**. The prototype now labels the local route **Candidate**; production
  must not say **Recommended** until the corpus and latency bars pass. Provider names and
  raw model IDs remain visible in details, not as the first decision a musician must make.
- Keep family and New/Add/Redo near the prompt because they change the current task.
- Show progress as named stages: Understand, Generate, Validate, Repair, Load.
- Show fallback as an event: “Cloud Assist quota reached; continued locally.” Never
  silently change provider.
- Put raw compiler/provider output behind Details while keeping a plain-language cause and
  next action visible.

This follows platform guidance to minimize settings, choose useful defaults, and keep
task-specific choices in the task rather than a global settings screen [9]. Error copy
must identify the problem and recovery action, not only surface an internal reason code.

### AI Engines settings

Use two role cards rather than one undifferentiated provider list:

- **Local Synthesis:** runtime health, endpoint, installed models, actual context, model
  size/quantization, memory fit, measured local speed, and Make Default.
- **Optional Cloud Assist:** provider connection cards, masked key status, Test Connection,
  quota status, data/cost disclosure, and one active planner.
- **Advanced:** explicit cloud synthesis, custom OpenAI-compatible endpoints, manual model
  IDs, timeouts, and diagnostic export.

Model discovery proves reachability and authorization, not Faust quality or quota. Label
the no-generation probe “Check connection”; label any real completion “Test generation —
uses quota.”

### Credentials: research direction requiring platform proof and a later ADR

The current `.env` path is serviceable for repository developers and not a professional
musician workflow. A platform `CredentialStore` is a candidate direction:

| Platform | Storage API |
|---|---|
| macOS | Keychain Services |
| Windows | Credential Manager (`CredWrite`/`CredRead`) |
| Linux | freedesktop Secret Service over D-Bus |

The JUCE UI should write/replace/delete a named secret through a native implementation;
the Python provider layer should read the same service/account identifier through the
platform keyring. Persist only an opaque `credential_ref` in the non-secret profile.
Apple explicitly positions Keychain for small secrets [10], Windows exposes per-user
credential storage [11], and freedesktop specifies a shared Secret Service protocol [12].

The project currently develops and tests only on Arch Linux, so it cannot responsibly
select a tri-platform production design. Start with a Linux Secret Service spike and do
not claim macOS/Windows support until those platforms join the build/test matrix. Before
implementation on any supported platform, prove an end-to-end spike and inspect the child
process environment, argv, request JSON, logs, crash text, and DAW state for secret
absence. If the cross-language vault identity cannot be made reliable, prefer a bundled
companion configurator over writing plaintext secrets to `config.json`, `.env`, argv, or a
temporary request file.

## Local evaluation protocol

The development machine currently reports an RTX A2000 Mobile and 31 GiB RAM, but
`nvidia-smi` cannot communicate with the driver. Do not interpret CPU-only Ollama latency
as the intended shipping performance.

1. Require healthy `nvidia-smi`, reachable Ollama, and recorded `/api/ps` context before a
   new local run.
2. Specify first-run behavior for Ollama absent, endpoint unavailable, and model not
   pulled, including download size/time and a non-destructive way to retain today's Groq
   path. A mandatory local dependency is not eligible without this work.
3. Compare the shipping `qwen2.5-coder:7b` with `qwen2.5-coder:3b`; do not download a
   broader model sweep before these two establish whether 4 GB-local synthesis is viable.
4. Run a stratified 25-prompt pilot, baseline prompt versus one routing-example injection.
5. Record first-try/retry compile, per-class errors, deterministic fidelity, total latency,
   prompt/eval rate, and peak RAM/VRAM.
6. Expand only if the variant gains at least three first-try successes, reduces
   `routing_arity`, and loses no more than one deterministic semantic pass.
7. Do not call a local model “recommended” unless its compile and fidelity rates land
   within five percentage points of the comparable Groq corpus and its latency is usable
   in the interactive host.
8. Human-listen to at least five matched outputs before promotion. The render oracle can
   reject broken audio; it cannot establish musical fidelity.

If GPU/Ollama remains unavailable, preserve the existing cached analysis, report the
environment blocker, and make no cloud substitution.

## Existing resilience snapshot review

`origin/feat/provider-resilience` contains useful raw material but should not be merged as
the professional design:

**Keep as ideas:** typed quota/unavailable errors, immediate daily-quota classification,
attempt provenance, no fallback on invalid DSP, and benchmark pinning.

**Reject or redesign:**

- a fourth cramped selector row in `PromptPanel`;
- generic configured cloud chains;
- fallback on missing credentials or paid-policy rejection;
- one shared budget across cloud failure and local recovery;
- `.env` as the only credential workflow;
- a stale/conflicting ADR number;
- provenance that exists in JSON/logs but has no coherent progress UI;
- a provider selector that still controls planner, synthesis, and UI-face as one role.

## Sources

1. [LangGraph overview](https://docs.langchain.com/oss/python/langgraph/overview)
2. [llama.cpp GBNF guide](https://github.com/ggml-org/llama.cpp/blob/master/grammars/README.md)
3. [Ollama structured outputs](https://docs.ollama.com/capabilities/structured-outputs)
4. [Groq rate limits and headers](https://console.groq.com/docs/rate-limits)
5. [Gemini API rate limits](https://ai.google.dev/gemini-api/docs/rate-limits)
6. [OpenRouter free-model limits](https://openrouter.ai/docs/faq)
7. [llama.cpp server prompt cache](https://github.com/ggml-org/llama.cpp/blob/master/tools/server/README.md)
8. [Ollama generate API and `keep_alive`](https://docs.ollama.com/api/generate)
9. [Apple Human Interface Guidelines — Settings](https://developer.apple.com/design/human-interface-guidelines/settings)
10. [Apple Keychain Services](https://developer.apple.com/documentation/security/keychain-services)
11. [Microsoft `CredWrite`](https://learn.microsoft.com/en-us/windows/win32/api/wincred/nf-wincred-credwritea)
12. [freedesktop Secret Service API](https://specifications.freedesktop.org/secret-service/latest/)
