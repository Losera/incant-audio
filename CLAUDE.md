# PluginForge — Claude Code Context

## What this project is
LLM-guided program synthesis for real-time DSP audio plugins.
Pipeline: Natural language prompt → LLM → Faust DSL → LLVM JIT → VST3/AU

## Key architectural decisions
- LLM outputs Faust DSL (not raw C++ or JSON IR)
- Faust chosen over JSON IR: algebraic DSL, LLMs generate it more reliably
- JIT via libfaust/LLVM inside JUCE host plugin (single distributable artifact)
- Pre-allocated 64-slot parameter pool to satisfy DAW fixed-param requirements
- Error correction loop: compiler stderr fed back to LLM, up to 3 retries

## Stack
- JUCE 7 (C++17) — audio plugin framework, VST3/AU output
- libfaust + LLVM — JIT compiler embedded in host plugin
- Python — LLM prompt layer; provider-agnostic via llm/providers.py (free-only by
  default: gemini / groq / openrouter / local ollama; anthropic is paid and gated
  behind PLUGINFORGE_ALLOW_PAID=1). Draft rationale: docs/ADR-012-free-provider-layer-DRAFT.md
- CMake + Ninja — build system
- Arch Linux (primary dev target)

## Current status
- FaustEngine.cpp — IMPLEMENTED: full libfaust JIT compile/swap with correct acquire/release
  ordering, compileMutex re-entrancy guard, ParamCapture metadata collection, off-audio-thread
  DSP cleanup. Reference implementation in docs/audio_thread_example.md. Swap protocol revised
  2026-07-19: an audioBusy drain guard (enterAudio()/exitAudio() bracketing processBlock,
  seq_cst store→load handshake) closes the activeUI TOCTOU reported 2026-07-18, and the compile
  callback now fires BEFORE ready=true so ParamPool labels and the new DSP publish together —
  this fixed the ~1,100 "setParamValue not found" errors the first TSan run exposed (re-run:
  PASS, zero errors). Design rationale: docs/fixplan_pushtofaust_swap.md.
- ParamPool.cpp — IMPLEMENTED (PAIR draft): pushToFaust() iterates active slots each block.
  The activeLabels audio-thread/compile-thread data race was fixed 2026-07-17 (double-buffer
  + atomic index, mirroring FaustEngine's activeDSP/ready swap; see docs/collaboration_log.md
  2026-07-17 entry). A second bug was found and fixed 2026-07-18 (PAIR): ParamPool used to
  create all 64 params itself via the legacy createAndAddParameter() after PluginProcessor's
  apvts was already constructed with a (until today, empty) ParameterLayout — this tripped
  jassert(!state.isValid()) on every slot. Params are now declared in
  PluginProcessor::createParameterLayout() and ParamPool looks them up by ID (shared
  ParamPool::slotId() scheme) instead of creating them; see docs/collaboration_log.md
- PluginProcessor.cpp — IMPLEMENTED: loadFaustCode() wires compile() callback to remap();
  createParameterLayout() now builds all 64 params (see ParamPool.cpp entry above).
- PluginEditor.cpp — IMPLEMENTED (2026-07-16, PAIR draft landed): generateButton.onClick
  spawns generate.py --prompt via juce::ChildProcess, parses JSON, calls loadFaustCode().
  All JUCE API calls verified against /home/losera/JUCE/modules headers. Awaiting human
  read-through per PAIR mode, notably the SafePointer thread-safety reasoning. Hardened
  2026-07-19: both TODO VERIFY path markers resolved — the old getSiblingFile("llm") guess
  never matched any real layout; replaced with a bounded upward search from the binary
  (verified live: finds llm/generate.py at depth 5 from the Standalone artefact dir) plus the
  PLUGINFORGE_LLM_SCRIPT env override for installed bundles. Also added a 120s
  waitForProcessToFinish cap + kill() so a hung generate.py can no longer wedge the worker
  thread forever.
- Full build — CONFIRMED WORKING 2026-07-18, for the first time in this project's history.
  The GTK/webkit2gtk blocker mentioned below was stale (gtk3 and webkit2gtk-4.1 were already
  installed); the actual root cause was JUCE_WEB_BROWSER / JUCE_USE_CURL /
  JUCE_VST3_CAN_REPLACE_VST2 all defaulting to 1 regardless of the plugin's NEEDS_* options
  (those only gate linking, not the #include/symbol guards). Fixed via three
  target_compile_definitions in host/CMakeLists.txt — no system package changes needed.
  PluginForgeHost, PluginForgeHost_Standalone, and PluginForgeHost_VST3 all build and link
  clean. See docs/collaboration_log.md 2026-07-18 entries.
- FaustEngine.cpp — a stray non-compiling ParamPool::pushToFaust() fragment and a
  fabricated UI::failSafe() override (no such method exists in faust/gui/UI.h) were
  removed 2026-07-16; the file now compiles clean.
- IPC mechanism (editor ↔ generate.py) — DECIDED: ADR-011 (argv one-shot subprocess)
  ratified by the human 2026-07-19 and recorded in docs/decisions.md. Remaining open items
  from its hardening table were implemented the same day: PLUGINFORGE_PYTHON interpreter
  override, and true ready-state UX via PluginForgeProcessor::onFaustCompileSuccess
  (point E of docs/pair_draft_editor_llm_bridge.md — status label now shows
  "Ready — DSP live, N params mapped" when the JIT swap lands, not just when the
  subprocess returns).
- LLM layer (llm/generate.py) — functional: --prompt / --json / CLI modes; generate_json()
  used by the C++ subprocess path.
- llm/providers.py — IMPLEMENTED 2026-07-21: single provider registry used by generate.py
  and all three bench harnesses. Five providers, three adapters (groq/openrouter/ollama
  share one OpenAI-compatible httpx path); no new dependencies. Free-only rule enforced by
  assert_free() at each __main__. Also carries two things that could not go in the
  HUMAN-OWNED prompts: markdown-fence stripping (open models fence their output; off for
  anthropic to keep the 0.88 baseline bit-comparable) and ProviderSpec.min_max_tokens
  (reasoning models bill hidden thinking against the output cap — gemini-3.6-flash at
  max_output_tokens=1024 gave 981 thinking / 39 visible tokens, truncated). Doctor CLI:
  `python llm/providers.py --check all`.
- END-TO-END GENERATION — WORKING 2026-07-21 on a free provider, for the first time since
  the Anthropic credit ran out. `python llm/generate.py --prompt "..."` returns valid,
  faust-compiled DSP via gemini-3.6-flash (verified first-try and via the stderr-feedback
  retry path). .env now carries PLUGINFORGE_PROVIDER=gemini; the plugin inherits it through
  juce::ChildProcess with no C++ change and no rebuild. The audible half of P6
  (docs/prototype_test_plan.md Part A) is still unrun — it needs the human's ears.
- PROMPT SURFACE — UNIFIED 2026-07-21. There is now ONE system prompt,
  llm/prompts/system_prompt.txt, loaded by generate.py, run_benchmark.py and
  run_efficacy_study.py alike; bench/prompts/system_faust.txt is deleted. Root cause of
  the change: both files taught Faust functions that DO NOT EXIST (ef.ping_pong,
  ef.chorus, ef.flanger) and two of the four production few-shot examples did not
  compile — the actual source of the flanger HALLUCINATION and ping-pong SEMANTIC
  failures logged as "persistent" since 2026-05. The stdlib section is now GENERATED
  from the installed /usr/share/faust/*.lib by tools/gen_stdlib_block.py; the ADR-009
  duplicate-symbol rule lives in that one file. Guarded by
  .claude/hooks/check_prompt_invariants.py (every ns.func must resolve) and
  tests/test_prompt_stdlib.py (5 tests, incl. every few-shot example must compile).
  ALL benchmark numbers recorded before 2026-07-21 were measured on the old bench
  prompt and are NOT comparable to a run made today.
- CI — .github/workflows/test.yml runs pytest -m "not integration" on every push.
- Test suite — 231 unit tests pass (145 pre-existing + 82 added 2026-07-21 for the
  provider registry + 4 for efficacy-record provenance); 10 integration tests guarded by
  @pytest.mark.integration. The 145 pre-existing tests were NOT modified by the provider
  refactor — that was its acceptance gate. NOTE: tests/conftest.py now pins
  PLUGINFORGE_PROVIDER=anthropic for the session; without it, a developer's .env
  selection makes the mocked-client unit tests dispatch real network calls (this
  happened 2026-07-21 and hung the suite).
- ADR-009 verdict — SETTLED 2026-07-19: full 25-prompt Faust re-run measured 22/25
  (88%), not the ADR-009-predicted ≥96%. bench/results/.prompt_baseline.json updated
  0.84→0.88. Two of three failures (ping-pong SEMANTIC, flanger HALLUCINATION) are
  exact repeats of the 2026-05 run. Detail: docs/prompt_efficacy_study.md §6.
- Prompt-efficacy study — DESIGNED 2026-07-19, PILOT RUN 2026-07-20:
  bench/run_efficacy_study.py + bench/score_efficacy.py + bench/prompts/tiered_prompts.json
  (25 effects × 5 knowledge tiers, L4 DSP-engineer → L0 artist-reference) measure how
  prompt phrasing affects Faust generation. Pilot (filters+generative, 50 gens):
  first-try compile rate is non-monotonic (90% L4/L3 → 50% L1 → 60% L0) — vibe/metaphor
  prompts (L1) were harder than artist-reference prompts (L0). Retry recovers most
  tiers to 80-90%. Found and fixed a scoring-taxonomy gap (ADR-009 duplicate-`process`
  regressions and Faust arity-mismatch errors were UNCLASSIFIED; now SEMANTIC).
  Full 125-prompt run (P9) attempted 2026-07-20 but INVALID — Anthropic account had
  insufficient credit, all 125 requests rejected pre-generation (0 tokens spent, 0
  data produced). Re-run once billing is topped up. Detail: docs/prompt_efficacy_study.md §7.1-7.2.
- UI/UX planning — docs/ui_design_plan.md (design-type taxonomy, AI-centered layout
  loop, GitHub ecosystem-survey spec P10) and docs/ux_roadmap.md (persistence →
  iterate/refine → embedded editor phasing) written 2026-07-19; no code changed yet.
  Phase 1 (state persistence) is the priority blocker — getStateInformation/
  setStateInformation are still empty stubs in PluginProcessor.h.
- generate.py — hardened 2026-07-19 (point F): missing ANTHROPIC_API_KEY and any
  unexpected exception now surface as clean ADR-011 JSON on stdout instead of a
  traceback, so the host never has to parse garbage. See docs/pair_draft_editor_llm_bridge.md.
- P10 ecosystem survey — DONE 2026-07-20: docs/juce_plugin_survey.md, 21 open-source
  JUCE/Faust repos surveyed (3 parallel research agents, read-only). Headline: zero
  of 19 fixed-param entries used bare GenericAudioProcessorEditor, even at 1-2
  params — supports keeping the planned auto-layout as the UI floor rather than a
  GenericEditor fallback. Also found a 4th UI paradigm (declarative/GUI-Magic) not
  yet named in ui_design_plan.md's taxonomy.

## Do not
- Do not suggest switching from Faust to raw C++ generation
- Do not suggest JSON IR as an intermediary (decision was made against this)
- Do not use sudo npm install

## File map
host/Source/FaustEngine.h/.cpp  — libfaust JIT wrapper (stub → real on Day 2)
host/Source/ParamPool.h/.cpp    — 64-slot parameter pre-allocation
llm/generate.py                 — LLM call + Faust validation + retry loop
llm/prompts/system_prompt.txt   — few-shot Faust generation prompt
examples/*.dsp                  — reference Faust patches

## Collaboration protocol
Before any non-trivial task, also load COLLABORATION.md (revision 2, 2026-07-21). Claude
writes the code, including the audio path and the prompts; what is gated is consequence,
not file category. It defines the four consult-first triggers, the two-tier evidence bar
(Tier 2 requires a primary source cited by file:line), the change-report format, and
STATUS.md. The three-mode protocol (DELEGATE / PAIR / HUMAN-OWNED) is retired — see
COLLABORATION.md §9. CLAUDE.md answers what this project is; COLLABORATION.md answers how
we build it; STATUS.md answers where it currently stands.

Read STATUS.md at the start of every session — it supersedes the per-file status narrative
below, which is being migrated out of CLAUDE.md and should be treated as stale where the
two disagree.
