# PluginForge — Claude Code Context

## Naming  *(2026-08-11)*
**The product is Incant Audio.** `PluginForge` is the internal synthesis engine and the
name of this working directory. Use "Incant Audio" for anything user-facing; keep
`PluginForge` for the engine, the repo, and every identifier.

Three names are in play right now and the mismatch is deliberate, not an oversight:

| Where | Says |
|---|---|
| `README.md:1` | **Incant Audio** — "Powered by the PluginForge Synthesis Engine" |
| Everything else (this file, `STATUS.md`, `COLLABORATION.md`, all of `docs/`) | PluginForge |
| GitHub remote | `Losera/incant-audio` — **already renamed upstream.** The local `origin` still said `audio-smith` until 2026-08-11 and only worked via GitHub's redirect; it now points at the real name. If a fetch ever 404s, check this first. |
| The window title the user sees, `PRODUCT_NAME` in `host/CMakeLists.txt` | PluginForge |

**No rename of identifiers, namespaces, CMake targets, `PLUGINFORGE_*` env vars, or the
`macro_N` slot scheme is authorized.** A full refactor is deferred deliberately — it
would touch the persisted state format and the `PLUGINFORGE_PROVIDER`/`PLUGINFORGE_ALLOW_PAID`
contract, which is consult-trigger territory (COLLABORATION.md §2.3). Do not start one
opportunistically. When it happens it gets its own session and its own ADR.

## What this project is
LLM-guided program synthesis for real-time DSP audio plugins.
Pipeline: Natural language prompt → LLM → Faust DSL → LLVM JIT → VST3/AU

## Key architectural decisions
- LLM outputs Faust DSL (not raw C++ or JSON IR)
- Faust chosen over JSON IR: algebraic DSL, LLMs generate it more reliably
- JIT via libfaust/LLVM inside JUCE host plugin (single distributable artifact)
- Pre-allocated 64-slot parameter pool to satisfy DAW fixed-param requirements
- Error correction loop: compiler stderr fed back to LLM, up to 3 retries
- Generated-plugin visual identity: heuristic native-widget palette, not a new LLM
  artifact or WebView (ADR-022)

## Stack
- JUCE 7 (C++17) — audio plugin framework, VST3/AU output
- libfaust + LLVM — JIT compiler embedded in host plugin
- Python — LLM prompt layer; provider-agnostic via llm/providers.py (free-only by
  default: gemini / groq / openrouter / local ollama; anthropic is paid and gated
  behind PLUGINFORGE_ALLOW_PAID=1). Rationale: ADR-012 in `docs/decisions.md`.
- CMake + Ninja — build system

## The machine this is built on
**Re-verified 2026-07-30 and four of these had drifted** — the block below had said Faust
2.85.5, LLVM 22.1.6, CMake 4.3.3, Python 3.14.5 since 2026-07-27, while the box had been
upgraded underneath it. Every number here is read from the installed tool, not recalled.
These are the versions the JIT, the oracle and the prompt's stdlib block are pinned to in
practice — when a version here moves, expect the generated Faust and the measured audio to
move with it, which is exactly why silent drift matters.

- **Arch Linux** (Omarchy). Kernel **7.0.10-arch1-1 running**, **7.1.4.arch1-1 installed** —
  the box has not rebooted since the upgrade. That gap is also why `nvidia-smi` fails
  (`NVML: Driver/library version mismatch`): the loaded module is 610.43.02 against 610.43.03
  userspace, so **ollama runs CPU-only** until a reboot. Primary and only dev target.
- **Faust 2.85.9**, stdlib at `/usr/share/faust/`. This is the ground truth
  `tools/gen_stdlib_block.py` generates the prompt's stdlib block from, and what
  `check_prompt_invariants.py` resolves every `ns.func` against.
  ⚠️ The block in `llm/prompts/system_prompt.txt:71` is still stamped
  `Faust version at generation: 2.85.5`. That is **not** a defect: `--check` validates the
  entry *names*, which are version-independent by design (`gen_stdlib_block.py:300-308`), and
  it passes. Regenerating under 2.85.9 would rewrite signatures and spend prompt headroom
  there are only ~124 tokens of, so it is a deliberate deferral, not an oversight.
- **LLVM 22.1.8** — backs the libfaust JIT inside the host plugin.
- **JUCE 7.0.9 at `$HOME/JUCE` — NOT vendored in this repo** *(corrected 2026-08-11; this
  line had said "vendored at `host/JUCE`", which is a CMake **build** directory produced by
  `add_subdirectory(${JUCE_PATH} JUCE)`, not a checkout).* `host/CMakeLists.txt:5` defaults
  `JUCE_PATH` to `$ENV{HOME}/JUCE` and hard-errors if it is absent; override with
  `-DJUCE_PATH=...`. Read real JUCE sources from `/home/losera/JUCE/modules/` — the Tier 2
  citation rule means reading the header, not recalling it.
  CMake 4.4.0, Ninja 1.13.2, Python 3.14.6,
  libsndfile 1.2.2 (the render oracle's static-link closure).
- **Wayland session under Hyprland.** The Standalone runs here, so UI capture is
  compositor-specific: `tools/screenshot_ui.sh` drives `hyprctl` + `grim` and cannot be
  replaced with an X11 tool. There is no `$DISPLAY`-based fallback.

## Where it currently stands
**Run `/orient` first, every session.** It is the session-start read, authorized
2026-07-27 to replace "read STATUS.md in full." It injects live repo state, **the CI status
of the current branch** (added 2026-07-28 — red, green-on-an-older-commit and unreachable
all print a banner; silence is the one forbidden output), plus the open half of STATUS.md —
Broken, Assumed, Next three, Waiting on you — at about a fifth of the tokens, and computes
the rest rather than recalling it. `STATUS.md` is still the only
status record and is still rewritten each session; the digest just stops you paying for
its "Works — and how we know" evidence archive (57% of the file) on every read. Read the
file directly when you need that evidence, or whenever the digest prints
**DIGEST INCOMPLETE**.

The per-file narrative that used to live here was deleted on 2026-07-25, completing a
migration this file had already announced. It had become actively wrong — it still said
state persistence was an empty stub (implemented in `c34bbb6`), that the P6 audible test
had never run (it ran 2026-07-24), and that the suite held 231 tests (312). A document
that lies is worse than a missing one, because a reader cannot tell which half to trust.
`git log` has every word of it.

## The development cycle
One command, cost-ordered, cumulative. Run the cheapest level that covers what you touched.

```
tools/check.sh fast     ~2 s     every commit    unit tests + control wiring
tools/check.sh full     ~2 min   every push      + prompt grounding, build, TSan
tools/check.sh audio    ~1 min   touched llm/    + render oracle over the corpus  ($0)
tools/check.sh quota    opt-in   deliberate      the 25-prompt benchmark (spends quota)
tools/check.sh assumed           anytime         how many claims are still unverified
```

**Done means pushed and green, not committed.** *(2026-07-28.)* On 2026-07-28 CI had been
red for four consecutive pushes with a SIGILL in `OfflineRenderTest` (PF-027) — a harness
**the ladder below has never built or run**, unlike CI (PF-029). The ladder is necessary and
is not sufficient, and a green `check.sh` is a statement about a smaller set of tests than
CI runs. `/orient` now opens with the CI line so the gap is visible at session start.

**The one number is `assumed`.** Every piece of work should move at least one claim out of
STATUS.md's "Assumed, never checked" list. It cannot be improved by writing documentation,
which is the entire reason it is the metric. Since 2026-07-28 one of STATUS.md's three
"Next three things" is **reserved** for an item that moves this number
(tagged `*(evidence)*`, enforced by `tests/test_control_wiring.py`) — all 18 closed defects
were code defects, while all six evidence defects sat open, because a list ranked by urgency
never schedules work whose defining property is that it is not urgent.

**One judgment has no instrument: whether a generated plugin sounds like what was asked
for.** The render oracle proves a patch is not broken — no NaN, no silence, no DC, no
runaway gain — and cannot tell you the filter is musical. That is the human's job, it is not
delegable to a hook or a model, and `YOUR MOVE` lines should be written on the assumption
that a listening pass outranks a diff read. See COLLABORATION.md §1.

**A control counts only once it has been seen failing.** On 2026-07-25 all five enforcement
hooks were found never to have run — `.claude/settings.json` had `PreToolUse` at the file
root instead of nested under `"hooks"`, and Claude Code ignores a wrongly-shaped file
silently. This was the third time the project mistook a declared control for a running one,
after PAIR mode and the ADR-009 sync hook. So: new gates ship with a red case,
`tests/test_control_wiring.py` asserts every hook still has teeth, and "it's configured" is
not evidence.

## Invariants worth knowing before you touch the audio path
- **DSP swap protocol.** `FaustEngine` compiles off-thread; an `audioBusy` drain guard
  (`enterAudio()`/`exitAudio()` bracketing `processBlock`, seq_cst store→load) closes the
  `activeUI` TOCTOU, and the compile callback fires **before** `ready=true` so ParamPool
  labels and the new DSP publish together. That ordering fixed ~1,100 "setParamValue not
  found" errors the first TSan run exposed. Rationale: `docs/fixplan_pushtofaust_swap.md`.
- **Parameters are declared once, in `PluginProcessor::createParameterLayout()`**, and
  ParamPool looks them up by the shared `ParamPool::slotId()` scheme. ParamPool must never
  create them — doing so trips `jassert(!state.isValid())` on every slot.
- **`pushToFaust()` is on the audio thread.** No allocation, no locks, no logging, no map
  lookups. `check_rt_safety.py` guards `FaustEngine.cpp` and `PluginProcessor.cpp` only, and
  cannot follow a call graph — it does not cover this function (PF-015).
- **Three prompt files, not one** *(corrected 2026-08-11 — this said "One system prompt"
  long after the second and third landed).* `llm/prompts/system_prompt.txt` (effects) and
  `llm/prompts/instrument_prompt.txt` (instruments, `d587665`) are selected by
  `llm/router.py`'s keyword scoring — never an LLM call. `llm/prompts/system_prompt_presentation.txt`
  is a **generated** variant (`tools/gen_presentation_prompt.py` from `system_prompt.txt` +
  `llm/presentation_block.txt`) selectable via `PLUGINFORGE_PROMPT_VARIANT=presentation`;
  do not hand-edit it. It teaches `hgroup`/`vgroup`/`[style:knob]`/`[scale:log]`, and it
  currently **fails `tests/test_prompt_headroom.py` against groq** by ~267 tokens
  (OPEN_QUESTIONS.md Q6) — it needs a larger-context provider.
  Each stdlib block is generated from the installed `/usr/share/faust/*.lib`
  by `tools/gen_stdlib_block.py`; `check_prompt_invariants.py` rejects any `ns.func` that
  does not resolve, across the whole `llm/prompts/` directory rather than one filename
  (`09786ec`).
  **Benchmark numbers recorded before 2026-07-21 measured a since-deleted prompt file and
  are not comparable to anything.**
- **`tests/conftest.py` pins `PLUGINFORGE_PROVIDER=anthropic`.** Without it a developer's
  `.env` makes the mocked-client unit tests dispatch real network calls, which once hung the
  suite and burned free-tier quota.

## Do not
- Do not suggest switching from Faust to raw C++ generation
- Do not suggest JSON IR as an intermediary (decision was made against this)
- Do not use sudo npm install
- Do not stage or commit across the whole tree — the human edits this repo at the same
  time, sharing one index. Use explicit paths. (Enforced by `check_bash_denylist.py`.)

## File map
```
host/Source/FaustEngine.*     libfaust JIT wrapper, compile worker, swap protocol
host/Source/PluginProcessor.* lifecycle, state blob, load path (Fresh/Iterate modes)
host/Source/ParamPool.*       64-slot parameter pre-allocation
host/Source/ParamMap.h        0–1 ↔ real units (Hz/dB/ms), log/exp/linear curves
host/Source/OutputGuard.*     NaN/DC/limit/runaway safety net before the speakers
host/Source/*Panel.*          editor shell: prompt, code editor, param grid, keyboard,
                              sample browser
llm/generate.py               LLM call + Faust validation + retry loop
llm/providers.py              five providers, three adapters, free-only rule
llm/prompts/system_prompt.txt the one prompt (stdlib block is generated)
bench/render_oracle.py        renders compiled Faust offline and measures it
tools/check.sh                the gate ladder — start here
examples/*.dsp                reference Faust patches
```

## Collaboration protocol
Before any non-trivial task, also load COLLABORATION.md (revision 2, 2026-07-21). Claude
writes the code, including the audio path and the prompts; what is gated is consequence,
not file category. It defines the four consult-first triggers, the two-tier evidence bar
(Tier 2 requires a primary source cited by file:line), the change-report format, and
STATUS.md. The three-mode protocol (DELEGATE / PAIR / HUMAN-OWNED) is retired — see
COLLABORATION.md §9.

CLAUDE.md answers what this project is; COLLABORATION.md answers how we build it;
STATUS.md answers where it currently stands.
