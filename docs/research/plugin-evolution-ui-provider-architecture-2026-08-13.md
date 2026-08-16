# Continuous Plugin Evolution, Generated UI, Providers, and Soundfetch

**Date:** 2026-08-13
**Status:** Research dossier and implementation roadmap; product architecture is not implemented
**Decision owner:** Human-gated under the repository's architecture policy

## Executive summary

The live product trial exposed four problems in one session. The first is a single
connected design problem; the other three surfaced alongside it without sharing its
mechanism — see the note after the list:

- a dual chorus could accept a compressor, but later Reverb/Chorus additions were refused
  because Add mode sends the entire growing Faust program and system prompt in one request;
- the generated controls remain a flat, visually undifferentiated grid even though Faust
  and PluginForge already capture meaningful group structure;
- provider choice and credentials exist only through process-wide environment configuration;
- Soundfetch is presented as an integrated browser, but the running plugin cannot discover
  the working Soundfetch interpreter or configure Freesound credentials.

Only the first item names a mechanism the others share (Add mode's payload-pressure
refusal). The UI grouping, provider-selection, and Soundfetch-discovery items are
independent defects that happened to surface in the same trial session, not three more
instances of the same design problem — Phase 0 already schedules Soundfetch's fix on its
own track for exactly this reason, and the UI and provider work below are their own
tracks too. Reading past this list as "one problem, four symptoms" risks treating the
later three as sequenced behind the module-graph work in §2, which is not the case.

The recommended product direction for the payload-pressure problem is a **hybrid
authoring workbench** built around one ordered project graph:

```text
audio input ─┐
             ├─> Chorus 1 -> Chorus 2 -> Compressor -> Reverb -> output
synth source ┘
```

The project graph is both the generation boundary and the UI information architecture.
Each source or effect is a stable module. An LLM creates or edits one selected module;
deterministic code composes the modules into the complete Faust program. The same module
metadata drives distinct visual cards or panels. This prevents prompt size from growing
with the whole project and makes the interface describe the signal path.

Renderer selection remains evidence-gated. The previous statement that iPlug2 requires a
"2-3 month rewrite" is an unmeasured session estimate, not a finding. Build the same
representative modular interface in JUCE, iPlug2, and HTML/CSS, then compare artifacts and
runtime evidence. Agent implementation speed can shorten a spike; it does not remove state,
automation, DAW, packaging, accessibility, and cross-platform verification obligations.

Provider v1 should support OpenAI, Anthropic, Gemini, Groq, OpenRouter, Ollama, and custom
OpenAI-compatible endpoints. Secrets belong in the operating-system credential vault and
must never enter a patch, DAW state, request file, argv, or log.

The observed Internet Archive failure is not an Archive failure. PluginForge selected
`/usr/bin/python3`, where `soundfetch` is not installed. Soundfetch 0.4 works through the
project virtual environment, and a live Archive JSON search succeeded there. The product
needs integration discovery and diagnostics rather than another generic "no JSON" error.

## 1. Evidence from the live trial

The prompt log records this sequence on 2026-08-13:

| Step | Result | Generated source size |
|---|---|---:|
| Generate dual chorus | succeeded | 1,669 characters |
| Add warm analog polysynth | refused before an LLM call | unchanged |
| Add compressor | succeeded | 2,043 characters |
| Add delay/reverb | refused before an LLM call | unchanged |
| Generate a fresh polysynth | succeeded | 770 characters |
| Add reverb to the smaller patch | succeeded | 1,150 characters |

This is deterministic payload pressure, not evidence that reverb is intrinsically harder
than compression. In current Add mode, `PromptPanel` sends all of
`processor.currentSource()` as `prior_source` with `refine_mode: "surgical"`.
`generate.py` constructs one user message containing the entire prior program, the new
request, and a surgical-preservation preamble. The system prompt and generation profile are
also charged to the request.

The admission check is currently Groq-specific:

- the recorded safe rule is `prompt_tokens + max_output_tokens <= 8000`;
- PluginForge reserves 4,096 output tokens;
- only about 3,904 estimated prompt tokens remain;
- the dynamic effect system prompt already consumes most of that allowance.

The preflight runs for every selected provider, so a larger-context Anthropic, Gemini,
OpenAI, or local model can still be rejected by Groq's rule. That is a concrete defect and
should be fixed independently. It is not the durable solution: every finite context fills
if each edit carries and regenerates the entire program.

Existing Add behavior correctly refuses instead of silently dropping or truncating prior
source. Silent fallback would violate the preservation contract. Redo intentionally permits
a rewrite and therefore cannot promise continuous preservation.

## 2. Continuous-evolution architecture

### 2.1 Chosen model: an ordered module project

Persist a versioned project containing:

- a stable project ID and revision;
- modules with stable IDs, display names, kind (`audio_input`, `instrument_source`, or
  `effect`), ordered graph position, and enabled/bypass state;
- each module's complete Faust source, input/output channel contract, parameter identities,
  UI group metadata, and originating request;
- explicit routing edges; v1 is a linear ordered chain with optional audio and instrument
  sources, not a general patching graph;
- the last successfully compiled composed source and enough revision history for atomic
  rollback.

The LLM boundary becomes module-scoped:

1. The user adds a new module or selects one to edit.
2. PluginForge sends only that module's contract, source when editing, and new request.
3. The candidate module is validated independently.
4. A deterministic composer namespaces it, checks channel compatibility, and connects it to
   the ordered chain.
5. The complete candidate project compiles off the audio thread.
6. Only a successful project is published; failure leaves the live graph untouched.

Adding a warm polysynth after designing effects inserts an instrument source before the
existing effects rather than asking a model to rewrite those effects:

```text
Warm Analog Poly -> Chorus 1 -> Chorus 2 -> Compressor
```

Adding reverb later appends one module. The LLM request contains the reverb contract, not the
chorus, compressor, and synth implementations.

### 2.2 Contracts the composer must own

These are product rules, not prompt suggestions:

- **Namespace isolation:** helper definitions and UI paths cannot collide across modules.
- **Channel topology:** every module declares mono/stereo input and output; incompatible
  connections fail before generation or compilation with an actionable explanation.
- **Stable controls:** module ID plus control ID forms a durable automation identity. Display
  labels may change without silently changing the identity.
- **Voice boundary:** instrument polyphony and MIDI voice controls belong to the source
  module; downstream effects process the mixed audio signal.
- **No partial publication:** module source, composed DSP, parameter map, graph revision, and
  UI layout become current as one successful revision.
- **Determinism:** the same project revision produces the same composed source and control
  order without an LLM call.

Faust is suitable for this model. Its official syntax treats programs as composable block
diagrams, provides lexical environments, and can reuse full programs through `component`.
See [Faust syntax and component composition](https://faustdoc.grame.fr/manual/syntax/).
The implementation spike must decide whether generated modules are isolated through emitted
environments, generated names, `component`, or another compiler-supported mechanism; it must
not rely on prompt discipline for collision avoidance.

### 2.3 Legacy patches

Existing arbitrary Faust programs cannot be safely decomposed into semantic modules by
inspection alone. Preserve them as one `legacy_monolith` module so old sessions remain
playable.

Offer an explicit migration workflow:

- **Keep legacy:** continue editing under today's whole-program limitations.
- **Convert with review:** ask the model to propose modules, compile and render the proposed
  project beside the original, and require user acceptance.
- **Redo as modules:** regenerate the described design using the old source only as reference.

Never claim that conversion preserves semantics unless an appropriate audio comparison and
human listening review have run. Keep the original revision as the rollback target.

### 2.4 Rejected shortcuts

- **Use a larger context only:** postpones the wall and does not bound project growth.
- **Return a diff only:** still requires the model to understand the monolith and introduces
  patch-application failure modes.
- **Summarize/RAG the old patch:** cannot guarantee preservation of omitted signal behavior.
- **Prompt caching or file APIs:** may reduce transfer or cost but not model admission tokens.
- **Minify the source:** useful as a temporary mitigation, not a continuous-evolution model.
- **Silently fall back to Redo:** violates the meaning of Add.

## 3. Professional generated UI

### 3.1 Current capability is underused

The flat appearance is not proof that JUCE cannot represent a professional modular UI.
PluginForge already has most of the semantic input:

- Faust `hgroup`, `vgroup`, and `tgroup` create hierarchical UI paths;
- `ParamCapture` records group, widget kind, unit, scale, menu metadata, and meters;
- `UiIr::Layout` is a versioned renderer-independent layout structure;
- `ParamGridPanel::applyUiIr()` and `layoutSectioned()` exist but have no runtime caller;
- section-heading geometry is calculated but not painted;
- captured meters are not rendered.

Faust's UI declarations are explicitly designed as an abstract UI/API description, with
hierarchical labels and metadata. See the [Faust UI architecture documentation](https://faustdoc.grame.fr/manual/architectures/)
and [Faust syntax reference](https://faustdoc.grame.fr/manual/syntax/).

The immediate baseline should therefore turn top-level DSP groups into visible module cards:

```text
┌ Chorus 1 ───────────┐  ┌ Chorus 2 ───────────┐
│ Rate  Depth  Mix    │  │ Rate  Depth  Width  │
└─────────────────────┘  └─────────────────────┘

┌ Compressor ──────────────────────────────────┐
│ Threshold  Ratio  Attack  Release  Gain  GR  │
└───────────────────────────────────────────────┘
```

Each module needs a visible title, container boundary, local spacing system, meaningful
control ordering, group-local accent treatment, bypass/status state, and typed visual roles
such as gain-reduction or level meters. Nested Faust groups become subpanels, not another
global grid row.

### 3.2 Generation boundary: constrained presentation IR

Do not generate arbitrary C++, JavaScript, or unrestricted HTML/CSS per user prompt. Generate
or derive a validated presentation manifest over a curated component library:

- module/card, subgroup, control, meter, visualizer, spacer, and label roles;
- semantic design tokens rather than raw colors at every node;
- bounded row/column spans and responsive breakpoints;
- explicit parameter bindings that must resolve to captured controls;
- accessibility label, value text, focus order, and keyboard operation;
- schema version and deterministic fallback.

DSP metadata remains authoritative. If presentation validation fails, every parameter must
still appear in a plain, operable fallback. A UI failure must never affect audio publication.

### 3.3 Comparative renderer spikes

The old "2-3 month iPlug2 rewrite" estimate is retired as unsupported. The measured
migration surface explains the risk but not elapsed agent time: `host/Source` holds 31
top-level files (20 `.h`, 11 `.cpp`; `find host/Source -maxdepth 1 -name "*.h" -o -name
"*.cpp" | wc -l`), two shipping targets, a 64-slot APVTS automation/state contract,
MIDI/audio-device handling, and 13 JUCE-based test/gallery targets.

Create one frozen fixture: dual chorus, compressor, and reverb with realistic controls,
meters, nested groups, long labels, and 700/900/1,600-pixel layouts. Feed the exact same
presentation IR and values to three isolated spikes.

#### JUCE native baseline

- Activate the existing sectioned `UiIr` path.
- Paint module cards and headings; render meters and interaction states.
- Preserve APVTS attachments, automation gestures, state, and current keyboard behavior.
- Establish the baseline runtime and accessibility evidence.

#### iPlug2 feasibility spike

- Build one isolated effect shell, not a production port.
- Represent the 64 stable parameters and load the frozen grouped fixture.
- Render cards with IGraphics using vector/SVG assets.
- Demonstrate state round-trip, one host automation gesture, resize/HiDPI, keyboard focus,
  VST3 and Standalone builds, and a real-host/pluginval smoke test.

iPlug2 officially supports multiple plug-in formats, vector/bitmap graphics, SVG, and web
views under a permissive license; those capabilities do not automatically provide a design
system or prove accessibility parity. See the [iPlug2 project](https://github.com/iPlug2/iPlug2),
[IGraphics documentation](https://iplug2.github.io/docs/class_i_graphics.html), and
[IWebView documentation](https://iplug2.github.io/docs/class_i_web_view.html).

Timebox implementation to 2-3 agent-days. Name this for what it is: an unmeasured budget,
not a measured estimate — no comparable prior PluginForge spike or agent-velocity baseline
backs this figure, which is exactly the standard this section just applied to retire the
old "2-3 month" rewrite figure two paragraphs above. Treat the number as a stop condition
("come back and report if this is running long"), not a commitment, until a spike has
actually run once and the figure can be replaced with a measured one. A production
decision requires the full state, format, host, and platform verification matrix.

#### HTML/CSS preview

- First implement as a development renderer consuming the same local IR.
- Use reusable web components for module cards and controls, with CSS custom properties and
  scoped styles.
- Bundle all assets locally; do not require network access to open an editor.
- Evaluate responsive layout, typography, animation, focus behavior, and visual iteration.

Web Components provide reusable custom elements, templates, slots, and style encapsulation;
see [MDN Web Components](https://developer.mozilla.org/en-US/docs/Web/API/Web_components) and
[Shadow DOM](https://developer.mozilla.org/en-US/docs/Web/API/Web_components/Using_shadow_DOM).

Embedding a browser is a separate decision. JUCE 8 adds C++/JavaScript integration,
parameter relays, and local resource providers while relying on platform web engines; see
[JUCE 8 WebView UIs](https://juce.com/blog/juce-8-feature-overview-webview-uis/). The current
JUCE 7 build disables its browser module and records a Linux WebKit dependency mismatch, so
an embedded WebView implies build/environment architecture approval and platform packaging
work.

### 3.4 Renderer decision gate

Compare blinded screenshots and recorded interactions rather than framework branding.
**Blinding is a limitation here, not a guarantee** — JUCE native widgets, iPlug2's
IGraphics vector rendering, and a browser-rendered HTML/CSS preview each carry distinctive
font-hinting, anti-aliasing, and native-control tells, and a reviewer who knows the three
toolkits may identify which is which on sight regardless of labeling. Strengthen the
protocol rather than relying on the label alone: randomized crops that hide chrome,
multiple independent raters, and an inter-rater agreement score are the minimum needed
before "blinded" supports the weight this gate puts on it. Minimum evidence:

| Area | Evidence |
|---|---|
| Visual quality | Human ranking of the same fixture at three widths; module boundaries and signal order understood without explanation |
| Operation | Every parameter reachable by pointer and keyboard; readable value and focus state |
| Automation/state | Stable parameter IDs, gestures, save/reload round-trip |
| Runtime | Cold-open time, idle and interaction CPU, editor FPS, RSS, binary size |
| Host behavior | Standalone plus at least one real VST3 host; resizing and window reopen |
| Packaging | Linux artifact plus documented Windows/macOS dependencies and asset handling |
| Accessibility | Screen-reader/accessibility-tree probe and keyboard-only workflow |

Suggested spike budgets are no more than one percentage point of added idle UI CPU and no
more than 25 MB RSS over the JUCE baseline. These are comparison thresholds, not release
requirements, and may be revised from measured baselines before the experiment starts.

Do not change both the plugin framework and renderer in one production migration. That would
make regressions impossible to attribute.

## 4. Provider profiles and local models

### 4.1 Current state and defect

`llm/providers.py` already centralizes Gemini, Groq, OpenRouter, Ollama, and gated Anthropic
behind three adapters. `generate_json()` accepts `provider` and `model`, and model discovery
exists. The host does not send a user selection; process-global environment variables decide
the provider and model.

Missing product capabilities are:

- first-class OpenAI support;
- custom OpenAI-compatible endpoints;
- durable non-secret provider profiles;
- in-plugin selection, connection diagnosis, and model discovery;
- secure credential entry;
- provider/model-aware input admission and typed errors.

Retain the registry instead of adding LangChain or LiteLLM. Faust generation is a narrow
non-streaming text path, while token counting, finish reasons, context limits, and model
capabilities differ enough that PluginForge must see rather than hide them.

### 4.2 ProviderProfile and adapter interfaces

Persist only non-secret profile metadata:

```text
ProviderProfile
  id: stable application-generated ID
  kind: openai | anthropic | gemini | groq | openrouter | ollama | openai_compatible
  display_name
  endpoint_url: optional; required for custom compatible profiles
  model_id
  enabled
  paid_use_acknowledged
  credential_ref: opaque OS-vault lookup key, never the secret
```

Provider adapters should expose capabilities rather than pretend every API is identical:

```text
generate(request) -> GenerationResult(text, finish_reason, usage)
list_models() -> ModelInfo[]
test_connection() -> ConnectionResult
count_input_tokens(request) -> optional exact count
```

`ModelInfo` carries ID, display name, known input/output limits, text-generation suitability,
and structured-output/tool capabilities as `supported`, `unsupported`, or `unknown`.
Generation does not need tools or structured output in v1; the fields prevent later API
breakage without adding unused behavior now.

Use a native OpenAI Responses adapter for official OpenAI, native adapters for Anthropic and
Gemini, and non-streaming Chat Completions for Groq, OpenRouter, Ollama, and custom
OpenAI-compatible profiles. Compatibility is explicitly partial; do not promise universal
tools, schemas, streaming, or token-count endpoints.

Official references:

- [OpenAI model listing](https://developers.openai.com/api/reference/resources/models/methods/list)
  and [API-key safety](https://help.openai.com/en/articles/5112595-best-practices-for-api-key-safety)
- [Anthropic model listing](https://platform.claude.com/docs/en/api/models/list) and
  [token counting](https://platform.claude.com/docs/en/build-with-claude/token-counting)
- [Gemini Models API](https://ai.google.dev/api/models),
  [token counting](https://ai.google.dev/api/tokens), and
  [API-key guidance](https://ai.google.dev/gemini-api/docs/api-key)
- [Groq models](https://console.groq.com/docs/models) and
  [rate limits](https://console.groq.com/docs/rate-limits)
- [Ollama OpenAI compatibility](https://docs.ollama.com/api/openai-compatibility),
  [installed models](https://docs.ollama.com/api/tags), and
  [context length](https://docs.ollama.com/context-length)

### 4.3 Provider tab behavior

The settings surface contains:

- a profile list with active status and provider/cost label;
- endpoint editing only for compatible/local profiles;
- masked Save/Replace/Delete credential actions that never reveal a saved secret;
- Test Connection, Refresh Models, curated recommended models, search, and manual model ID;
- capability/context summary with `unknown` shown honestly;
- Make Active, applied to the next request only;
- detection of legacy environment credentials without copying them into the vault;
- an optional Test Generation action clearly labeled as quota/cost consuming.

Model listing proves reachability and some authorization, not permission, quota, context, or
Faust-generation quality. Do not label it as a complete generation test. Never auto-fail over
to another provider: that can disclose private prompts/source to another vendor and incur
unexpected cost.

Active profile is an application/user preference, not patch state. Multiple DAW instances
share one serialized profile store; every generation captures an immutable profile snapshot
so a settings change cannot redirect an in-flight request.

### 4.4 Secret storage and transport

Use macOS Keychain, Windows Credential Manager, and Linux Secret Service/libsecret. Store an
opaque profile reference in normal application settings. The secret must not appear in:

- plugin or DAW state;
- profile JSON;
- temporary request JSON;
- process argv or process listings;
- prompt logs, errors, diagnostics, or crash reports;
- source control.

The JUCE UI writes credentials while the Python generator reads them. The implementation
therefore needs a deliberately designed cross-language credential bridge. Do not write `.env`
from the UI and do not use an application-bundled encryption key, which is obfuscation rather
than secure key management. A persistent provider worker could receive secrets over a private
stdin channel, but that reopens the accepted one-shot subprocess architecture and is not
chosen by this dossier. The bridge is an ADR-level implementation decision.

Hosted custom endpoints default to HTTPS. Permit HTTP automatically only for loopback/local
addresses; remote cleartext HTTP requires a prominent explicit confirmation.

### 4.5 Provider-aware request admission

Replace the global Groq estimate with selected-profile policy:

1. Use provider token-count APIs where available and applicable.
2. Use authoritative model input/output metadata when returned.
3. For Ollama, include installed-model metadata and the configured runtime context when it can
   be queried.
4. For compatible providers without counts, use a conservative estimate and label it as such.
5. Distinguish model context capacity from account rate-limit admission.

The UI should show `estimated input / usable input budget`, reserved output, and a
provider-specific reason. An unknown limit should produce a warning and allow an attempted
request, not inherit Groq's hard refusal.

Provider-aware admission makes current Add mode less wrong and lets larger/local models use
their capacity. It remains a mitigation until module-scoped generation lands.

## 5. Soundfetch integration

### 5.1 Confirmed failure mechanism

Current executable resolution is:

1. `SOUNDFETCH_BIN`;
2. `PLUGINFORGE_SOUNDFETCH_PYTHON`;
3. shared `PLUGINFORGE_PYTHON`;
4. `python3 -m soundfetch`.

In the running environment no Soundfetch override is set. `/usr/bin/python3 -m soundfetch`
fails with `No module named soundfetch`. The working installation is:

```text
/home/losera/soundfetch/.venv/bin/python -m soundfetch
```

It reports Soundfetch 0.4.0, and a live Internet Archive search for `drums` returned valid
JSON and an audio result. Thus the reported failure is interpreter/package discovery, not an
Archive query or API failure.

`SoundfetchClient::search()` already passes provider, verb, query, `--outdir`, path,
`--max-results`, count, and `--json` as discrete argv entries. The error display joins them
without shell quoting, so text resembling `--outdir/home/...` is not evidence that the actual
argv merged those fields.

### 5.2 Product behavior

Add a Sound Sources settings area, coordinated with but distinct from LLM providers:

- select a bundled runtime or a user-selected Soundfetch executable/interpreter;
- run a non-network startup doctor such as version and `sources --json`;
- show version, resolved executable, source availability, and actionable repair instructions;
- configure Freesound credentials through the same OS-vault policy;
- distinguish missing installation, authentication/403, provider/network, timeout, malformed
  JSON, and cancellation;
- never show raw command strings as the primary user guidance.

Packaging cannot assume the developer-only `/home/losera/soundfetch` path. Choose either a
pinned bundled Soundfetch runtime or a supported external-install discovery contract before
release.

Freesound currently reads `FREESOUND_API_KEY` from its environment and exposes no `--api-key`
argument. Passing a key in argv is forbidden. JUCE's current child-process call does not
provide a per-child environment map, so the secure credential bridge must include Soundfetch
or Soundfetch must gain a restrictive-permission credential/config interface.

### 5.3 Performance and cancellation

A live search requesting one result still used the default Archive page size of 50 and
performed many metadata fetches. PluginForge requests 10 results with a 60-second timeout.
Pass a page size no larger than the requested result count, or fix Soundfetch's engine to stop
metadata work when enough usable results exist.

Drain stdout and stderr without deadlock, keep progress separate from stdout JSON, propagate
cancellation, and never publish a partial result set as success.

## 6. Implementation roadmap

Architectural work must use reviewed task branches. The current branch already contains
uncommitted keyboard and Soundfetch changes and is not an appropriate base for interleaved
product edits.

### Phase 0 — narrow reliability fixes

1. Reconcile and review the current dirty keyboard/Soundfetch work.
2. Make token admission provider/model aware; add the regression where a Groq-sized request
   is refused for Groq but admitted for a mocked larger-context provider.
3. Add Soundfetch startup diagnostics and configured-interpreter support.
4. Bound Archive page size and verify cancellation/timeout behavior.

These improve current behavior without claiming continuous evolution.

### Phase 1 — module project and deterministic composition

1. Specify the versioned project/module schema and legacy-monolith compatibility in an ADR.
2. Prototype two isolated stereo effect modules and deterministic namespace/composition.
3. Add an instrument source before the existing effects in the hybrid project model.
4. Implement atomic compile/publish/rollback and stable module/control identities.
5. Persist and restore graph order, revision, modules, parameters, and legacy projects.

Gate: a project built effects-first can add a synth source and later append reverb without
sending prior module bodies to the LLM or changing existing module/control IDs.

### Phase 2 — semantic UI baseline and renderer comparison

1. Freeze the Chorus 1/Chorus 2/Compressor/Reverb fixture and visual acceptance questions.
2. Activate and complete the JUCE `UiIr` modular-card baseline.
3. Build isolated iPlug2 IGraphics and HTML/CSS renderers of the same fixture.
4. Collect blinded human review, runtime, automation, accessibility, host, and packaging
   evidence.
5. Record a renderer decision and migration/rollback plan in a reviewed ADR.

### Phase 3 — provider profiles and credentials

1. Specify `ProviderProfile`, `ModelInfo`, adapter results, typed errors, and selection
   precedence.
2. Add native OpenAI and custom OpenAI-compatible support.
3. Write an ADR choosing the cross-platform OS-vault bridge mechanism, then implement it with
   its redaction boundary. Listed as one step above for phase-numbering purposes only — §4.4
   explicitly declines to choose the mechanism ("The bridge is an ADR-level implementation
   decision") and rejects one candidate (a persistent provider worker over stdin) without
   naming a replacement, so the design is not yet settled enough to schedule as build-ready.
4. Build the provider tab, model discovery, connection test, immutable request snapshot, and
   application-global serialized profile store.
5. Retain legacy environment configuration during migration without copying secrets.

### Phase 4 — hybrid plug-in topology and release validation

The current effect and synth are separate compile-time targets: the effect accepts audio and
not MIDI; the synth accepts MIDI and has no audio input. The approved hybrid workbench needs
an explicit bus/format design and migration plan. Prototype optional audio and MIDI input in
an experimental target before changing either shipping identity.

Validate Standalone and VST3 in real hosts, parameter automation, state reload, bus
negotiation, MIDI, audio input, window reopen/resize, and project rollback. Exported or
specialized effect/instrument products remain later work.

## 7. Verification and acceptance matrix

### Module graph

- Effects-first project plus instrument insertion composes source before every existing
  effect.
- Appending reverb does not send existing module bodies to the model; request size remains
  bounded as module count grows.
- Editing one selected module changes only that module and the deterministic composed output.
- Existing module source and control identities remain byte-stable where no edit was asked.
- Mono/stereo incompatibility fails before publication with an actionable message.
- Compile failure leaves the previous audio graph live; rollback restores the exact prior
  revision.
- Save/reload preserves graph order, IDs, parameters, and source.
- Legacy monoliths remain playable and migration is explicit and reversible.

### UI/renderers

- Distinct Chorus 1, Chorus 2, Compressor, and Reverb modules are visually and
  programmatically identifiable.
- Every captured parameter appears exactly once and binds to the correct stable ID.
- Pointer, keyboard, focus, value text, resize, HiDPI, and accessibility behavior are tested.
- Automation gestures and state round-trip work in Standalone and a real VST3 host.
- Snapshot/gallery tests cover empty, grouped, overflow, long-label, meter, and error states.
- Runtime and artifact measurements use the same fixture and build type.

### Providers and credentials

- The same oversized prior is refused by a constrained Groq profile and not by a mocked
  larger-context provider.
- Every adapter test covers request shape, finish/truncation, usage, error normalization,
  model listing, token count, and partial/missing compatibility fields with mocked transport.
- Secrets never appear in normal settings, patch/DAW state, argv, request JSON, stdout,
  stderr, logs, or errors.
- Vault Save/Read/Replace/Delete uses a fake vault in deterministic tests plus opt-in
  platform smoke tests.
- Multiple plugin instances serialize profile changes; in-flight requests retain their
  original immutable profile.
- Live provider tests are opt-in because they consume credentials/quota; no automatic
  cross-provider failover occurs.

### Soundfetch

- Missing module produces an installation/interpreter instruction, not generic malformed
  JSON.
- Paths containing spaces remain one argv item; stderr progress cannot corrupt stdout JSON.
- A configured interpreter passes version/source doctor checks.
- Archive result count/page size is bounded and timeout/cancellation publishes no partial
  success.
- Freesound credentials never enter argv/logs; an invalid key produces a specific structured
  403/authentication status.

## 8. Risks and adversarial critique

- A module graph is a real persistent-schema and control-flow change. Poor namespace or
  parameter-identity design can break saved sessions and automation more severely than the
  current prompt refusal.
- Linear modules cover the demonstrated use case but not arbitrary feedback, parallel sends,
  sidechains, or cross-module modulation. Do not silently grow v1 into a general modular
  synthesizer.
- A framework migration can produce a more polished demo while regressing accessibility,
  host behavior, state, or packaging. Visual review alone is insufficient.
- HTML/CSS accelerates layout iteration but introduces platform WebView behavior, focus
  integration, and dependency variance. Local bundled assets and a deterministic fallback
  are mandatory.
- Model metadata and rate limits change. Cache cautiously, label unknowns, and avoid
  hard-coded global assumptions.
- OS-vault integration adds platform code and a C++/Python trust boundary. A rushed plaintext
  fallback would undermine the entire provider UI.
- Hybrid bus topology changes host-visible behavior and may affect plugin identity and
  compatibility. It requires an experimental target and real DAW evidence before migration.
- JUCE licensing must be reviewed before generated-plugin export work, particularly current
  license language concerning products that create other products. This dossier makes no
  legal conclusion.

## 9. Decisions and explicit non-decisions

Decided for subsequent architecture proposals:

- hybrid authoring workbench;
- ordered module generation/composition as the durable continuous-evolution direction;
- comparative JUCE, iPlug2, and HTML/CSS spikes before renderer selection;
- provider v1: OpenAI, Anthropic, Gemini, Groq, OpenRouter, Ollama, and custom
  OpenAI-compatible endpoints;
- OS credential-vault storage;
- one dossier now, followed by reviewed ADRs and task branches for product changes.

Not decided here:

- final persistent schema or composer mechanism;
- final renderer or a production iPlug2/JUCE migration;
- JUCE 8 upgrade or embedded WebView;
- exact credential bridge implementation;
- shipping hybrid target identity and bus layout;
- exported plug-in architecture;
- cloud-specific Bedrock, Vertex AI, or Azure OpenAI adapters.

Those choices require the experiments and human semantic review described above.

## 10. Addendum — adversarial review follow-up (2026-08-13)

An adversarial review of this dossier raised two findings that were resolvable with
evidence rather than argument. Both were run against the real code and the real
2026-08-13 trial log (`logs/prompts.jsonl`) rather than a synthetic example. This
section reports what was found; it does not change §9's decision status — both
items below still require human review before they affect Phase 1/3 ADRs.

### 10.1 Live counterfactual for §1's "deterministic payload pressure" claim

§1 inferred, from reading `generate.py`/`providers.py`, that the Add-mode refusals
were payload pressure rather than reverb being intrinsically harder to generate than
compression, but never ran the confirming experiment — §7's acceptance criteria
proposed it as a *mocked* future test. This addendum ran it live.

**Setup.** The exact `faust_code` the trial logged for the "A compressor" step
(`logs/prompts.jsonl`, `ts: 2026-08-13T23:06:23Z`, 2,043 chars — the same payload
`"A delay"`/`"A reverb"` were refused against at `23:08:45Z`/`23:08:53Z`, both
`attempts: 0`, both the same generic preflight message) was replayed verbatim as
`prior_source` in a `refine_mode: "surgical"` request asking for `"A reverb"`,
against providers other than Groq.

**Test A — reproduce the refusal on a genuinely larger-context provider.**
Calling `generate.generate_json()` (the real production entry point) with
`provider: "ollama"` (local `qwen2.5-coder:7b-16k`, a 16,384-token context — 4x
Groq's 8,000-token rule) and separately with `provider: "gemini"` (context in the
hundreds of thousands of tokens) produced the **identical** refusal both times —
`attempts: 0`, `prior_source_refused: true`, 0.00s elapsed, no network call made.
This empirically confirms the concrete defect §1 already named — `preflight_prior_source()`
(`llm/providers.py:152`) is called from `generate_json()` (`llm/generate.py:556`)
with no `provider` argument at all, so every provider is gated by Groq's fixed
`GROQ_TPM_LIMIT = 8000` (`llm/providers.py:121`) regardless of what it can actually
hold. The estimated cost of this exact request was 4,497 tokens
(`providers.estimate_tokens`) — comfortably inside Ollama's 16,384 and trivial
against Gemini's context, and refused anyway, on both, before either provider was ever asked.

**Test B — bypass the buggy gate, ask whether the model can do the job.**
Calling `generate.generate_faust()` directly (the lower-level function with no
preflight) with the same prior_source and provider `"gemini"`:

- Ollama (`qwen2.5-coder:7b-16k`) could not complete within 300s wall-clock, CPU-only
  — this box's known NVML driver/kernel version mismatch (`CLAUDE.md`, "the machine
  this is built on") makes it an infra limit, not a finding about generation
  capability, and is reported as inconclusive rather than negative.
- Gemini (`gemini-3.6-flash`) returned in 29.2s with 2,340 characters of Faust that
  **compiled clean** on the first attempt (`generate.validate_faust()` against the
  real `faust` binary, `re.stereo_freeverb` reverb stage present and wired through
  `ef.dryWetMixer`).

**Conclusion.** §1's causal claim is now backed by a reproduced experiment, not just
a code trace: the refusal is confirmed payload pressure — a fixed, provider-blind
token gate — and not evidence that reverb is harder to generate than compression. A
real model, given the identical prior program the gate never let it see, produced a
valid compiling reverb addition on the first attempt. Recommend closing this as a
confirmed finding rather than an open assumption, and consider filing the
provider-blind-preflight defect in `docs/BUGS.md` (next available ID **PF-060** as of
this writing) since it is now a reproduced failure, not a read-the-code inference —
consistent with this project's "a control counts only once it has been seen failing" rule.

**Aside, out of scope for this addendum:** the replayed `prior_source` — logged
under the prompts `"A dual chorus effect"` and `"A compressor"` — is not actually an
effect. It declares the `freq`/`gain`/`gate` instrument voice contract from
`llm/prompts/instrument_prompt.txt` and self-generates from `os.sawtooth(freq)`
rather than processing an incoming signal. Whether this reflects a router
misclassification during the trial or something else in how the session's prior
source was assembled is worth its own look, but it does not affect the counterfactual
above — the test held prior_source and task fixed and only varied the provider.

### 10.2 Considered alternative for §4's provider v1 scope

§4's proposed v1 (§9: OpenAI native + custom OpenAI-compatible + Anthropic/Gemini
native + `ProviderProfile` schema + cross-platform OS-credential-vault bridge) has no
equivalent to §2.4's "Rejected shortcuts" — no narrower alternative is named and
rejected with a reason. Checking what actually motivates §4 against the same trial
log used everywhere else in the dossier:

- `logs/prompts.jsonl`'s 2026-08-13 entries use only `groq`, `anthropic`, and
  `ollama` — zero OpenAI requests, zero custom-endpoint requests, zero credential
  failures of any kind. Unlike §1/§2 (Add-mode refusal) and §5 (Soundfetch), the
  trial does not contain an incident that motivates OpenAI support or a vault. §4.1's
  "Missing product capabilities" list is a wishlist, not a reproduced failure — a
  different evidentiary category than the rest of the dossier, and worth labeling as
  such rather than presenting alongside the others uniformly.
- `host/Source/PromptPanel.cpp` and `PluginProcessor.*` contain **zero** occurrences
  of `"provider"` or `"model"` as request-JSON fields (checked directly, not
  inferred) — there is no per-request selection plumbing today at any level, which
  means even the narrowest possible fix (an in-plugin picker for the five
  **already-integrated** providers) requires host-side work: a request field, a
  picker control, and persistence as an app preference — the last of which §4.3
  already specifies correctly as "applied to the next request only," independent of
  DAW/patch state.

**Narrower v1, considered and not currently in the dossier:**
1. Fix the provider-blind preflight (already Phase 0 item 2 — unaffected by this).
2. Add the request-JSON `provider`/`model` fields and an in-plugin picker for the
   five providers `llm/providers.py` already integrates (Gemini, Groq, OpenRouter,
   Ollama, gated Anthropic) — no new adapter code, since `_make_anthropic`,
   `_make_gemini`, and `_make_openai_compat` (`llm/providers.py:522,560,590`)
   already cover all five.
3. Defer natively-adapted OpenAI, generic custom-endpoint support, `ProviderProfile`
   persistence, and the OS-credential-vault bridge to v2.

This resolves §4.1's actually-observed defect (no in-plugin selection) without
touching the riskiest, least-scoped item in §4 at all: §4.4 explicitly declines to
choose the cross-language credential-bridge mechanism ("The bridge is an ADR-level
implementation decision") while §9 and Phase 3 step 3 still schedule "Implement the
cross-platform OS-vault bridge" as if the design were settled. A v1 that never
touches credential storage — existing `.env`-based credentials keep working exactly
as they do today — sidesteps that open question entirely rather than scheduling an
implementation step against it.

**What a narrower v1 gives up:** users who specifically want OpenAI, or who want to
type a key into the plugin instead of `.env`, wait for v2. Nothing in the trial log
suggests either was blocking anyone on 2026-08-13.

This is offered as a considered alternative, not a decision — per this dossier's own
"Decision owner: Human-gated" header, whether to narrow §4's v1 scope is for the
human reviewer, not something this addendum resolves unilaterally.
