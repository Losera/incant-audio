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
| `.claude/skills/orient/` | `/orient` — session-start digest: live repo state plus the open half of STATUS.md. Start every session with it |
| `docs/BUGS.md` | The durable, IDed defect registry (`PF-NNN`). STATUS.md's "Broken" is the top-N view of it |
| `docs/decisions.md` + `docs/architectural_decisions/` | ADRs. HUMAN-OWNED: Claude drafts, the human commits |

Subdirectory READMEs (`host/README.md`, `llm/README.md`, `bench/README.md`) orient you inside
each area and carry the area-relevant prompts from the series below.

## Working the project with Claude — prompt series

Copy-paste these into a Claude Code session, in order. Each is tagged with the engagement mode
it must run under; Claude states the mode before starting (COLLABORATION.md §2) — hold it to that.

**P0 — every session, first thing** *(read-only)*

```
/orient
```
