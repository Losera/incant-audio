# PluginForge

LLM-guided program synthesis for real-time DSP audio plugins.

**Pipeline:** Natural language prompt → LLM → Faust DSL → LLVM JIT → VST3/AU

## Setup

```bash
./scripts/install_deps.sh
./scripts/setup_pluginforge.sh
./scripts/scaffold_files.sh
cp .env.example .env && $EDITOR .env
```

## Run the LLM layer

```bash
pf-env
cd llm && python generate.py
```

## Build the JUCE host plugin

```bash
cd host
cmake -B build -DCMAKE_BUILD_TYPE=Debug -DJUCE_PATH=$HOME/JUCE
cmake --build build --config Debug -- -j$(nproc)
```

## Agentic architecture

This repo is built human-with-Claude, under an explicit protocol. The moving parts:

| Piece | Role |
|---|---|
| `CLAUDE.md` | **What** the project is — architecture, stack, current status, hard "do not"s |
| `COLLABORATION.md` | **How** we build it — the three engagement modes (DELEGATE / PAIR / HUMAN-OWNED), pre-task protocol, stop conditions, fail-loud markers, log format |
| `.claude/hooks/` | Three PreToolUse guards: RT-safety in audio-thread code, write-protection on HUMAN-OWNED files (`llm/prompts/*`, `docs/decisions.md`), bash denylist |
| `.claude/agents/invariant-hook-writer.md` | Subagent that turns a stated project invariant into a tested, registered hook — or reports it isn't mechanically hookable |
| `.claude/skills/architecture-planning/` | `/architecture-planning` — router for any new architectural decision (hook? ADR? subagent? loop?) |
| `.claude/skills/attention-report/` | `/attention-report` — severity-ranked "what needs the human" audit; start every session with it |
| `docs/collaboration_log.md` | Append-only record of every non-trivial task: mode used, outcome, mode-signal |
| `docs/decisions.md` + `docs/architectural_decisions/` | ADRs. HUMAN-OWNED: Claude drafts, the human commits |

Subdirectory READMEs (`host/README.md`, `llm/README.md`, `bench/README.md`) orient you inside
each area and carry the area-relevant prompts from the series below.

## Working the project with Claude — prompt series

Copy-paste these into a Claude Code session, in order. Each is tagged with the engagement mode
it must run under; Claude states the mode before starting (COLLABORATION.md §2) — hold it to that.

**P0 — every session, first thing** *(read-only)*

```
/attention-report
```

**P1 — ratify ADR-011: the IPC mechanism** — ✅ **DONE 2026-07-19** (drafted by Claude,
ratified by the human into `docs/decisions.md`; hardening items closed same day — see
`docs/architectural_decisions/ADR-011-ipc-argv-subprocess.md`)

**P2 — run the ThreadSanitizer harness** *(DELEGATE)*

```
Build and run the ParamPoolTsanTest target (host/tests/ParamPoolConcurrencyTest.cpp).
Report any data races verbatim. If a race lands in HUMAN-OWNED code (the suspected
FaustEngine activeUI TOCTOU), report only — do not fix. Log the result per
COLLABORATION.md §6.
```

**P3 — extend CI beyond Python** *(DELEGATE)*

```
Extend .github/workflows/test.yml so CI also configures and builds the C++ host
(all three targets) and builds ParamPoolTsanTest. Keep the existing pytest job
intact. Note JUCE and libfaust availability on ubuntu-latest as the first problem
to solve.
```

**P4 — verify the generate.py path assumption** *(PAIR)*

```
PluginEditor.cpp:15 and :29 carry TODO VERIFY markers on the same assumption: that
llm/generate.py can be found relative to the plugin binary at runtime. Verify this
against the real install layouts (Standalone build dir, VST3 bundle in
~/.vst3/, and the PLUGINFORGE_LLM_SCRIPT env override). Draft the fix; I review
before it lands.
```

**P5 — settle the ADR-009 benchmark claim** *(DELEGATE)*

```
ADR-009 predicted a ≥96% first-try Faust compile rate after the duplicate-symbol
prompt fix, but the committed baseline is 84% and the 2026-07-18 9-prompt subset
showed 89%. Re-run the full 25-prompt Faust benchmark (bench/) and update the
baseline per bench/README.md's seeding rules. Report whether ADR-009's prediction
held.
```

**P6 — end-to-end smoke test (the prototype finish line)** *(PAIR)*

```
End-to-end smoke test: launch PluginForgeHost Standalone, enter a prompt (e.g.
"warm low-pass filter with cutoff control"), and confirm: generate.py returns
Faust code, the JIT compile succeeds, audio passes through the new DSP, and the
remapped parameter slots respond. I'll drive the app and listen; you instrument
and diagnose.
```

**P7 — close the prompt-sync hook gap** *(DELEGATE)*

```
protect_human_owned.py has a known gap: bench/prompts/system_faust.txt duplicates
ADR-009 rule text from llm/prompts/system_prompt.txt but isn't covered by any
hook, so they can drift. Use the invariant-hook-writer agent to add enforcement
(or have it report why the invariant isn't mechanically hookable).
```

**P8 — finish ADR-008: Claude vs Gemini** *(HUMAN-OWNED — Claude drafts, you commit)*

```
ADR-008 (LLM provider) is still "Under evaluation" because the Gemini benchmark
run never completed. Run the Gemini side of the 25-prompt benchmark, compare
against the Claude numbers, and draft the ADR-008 decision for my review.
```

**P5 — settled 2026-07-19:** full re-run measured 22/25 (88%); ADR-009's ≥96%
prediction did not hold. See `docs/prompt_efficacy_study.md` §6 and the refreshed
priority queue in `docs/next_steps.md`.

**P9 — run the full prompt-efficacy study** *(DELEGATE)*

```
Run the full 125-prompt tiered efficacy study per docs/prompt_efficacy_study.md
(design, tier scheme, and locked confound controls are all in that doc — read it
first). Command: python3 bench/run_efficacy_study.py, then score with
bench/score_efficacy.py --chart, optionally --judge. Budget ~$3–5. Append the
tier×category results and hypothesis verdicts (H1–H4) to the doc's §7.2 and log
per COLLABORATION.md §6. Do not modify any prompt file.
```

**P10 — settled 2026-07-20:** 21 repos surveyed, see `docs/juce_plugin_survey.md`.
Headline: zero of 19 fixed-param entries used bare GenericAudioProcessorEditor.

**P10 — JUCE/Faust ecosystem survey** *(DELEGATE, research agents)*

```
Execute the ecosystem survey spec'd in docs/ui_design_plan.md §4: 2–3 parallel
research agents over awesome-juce, GitHub topic:juce-plugin, and the faust2juce
ecosystem. Produce docs/juce_plugin_survey.md with the per-repo data table
(plugin type per the design taxonomy, param count, UI paradigm, complexity
proxy) and a complexity-ladder summary. Read-only research; no repo changes
beyond the new doc.
```

**P11 — state persistence (the UX-roadmap enabler)** *(PAIR — confirm mode first)*

```
Implement Phase 1 of docs/ux_roadmap.md: getStateInformation/setStateInformation
(currently empty stubs, PluginProcessor.h) serializing Faust source + prompt +
param values + slot labels, with recompile-on-restore. The roadmap classifies
this PAIR because restore interacts with the compile/swap lifecycle — confirm
that classification with me before drafting, and I review before it lands.
```

**P12 — AI-centered UI: widget metadata + auto-layout** *(DELEGATE draft, PAIR at the swap boundary)*

```
Execute docs/ui_design_plan.md §3: add the missing widget-kind field to
ParamCapture/ParamInfo (FaustEngine.h — capture currently discards whether a
param was a slider/button/checkbox), then the param-count-driven auto-layout in
refreshParamKnobs() so more than 8 of the 64 slots can surface. Anything
touching the compile/swap path gets flagged for my review.
```
