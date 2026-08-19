# host/ — JUCE JIT Host Plugin

The C++ side of PluginForge: a JUCE 7 plugin (VST3 + Standalone) that embeds libfaust/LLVM and
JIT-compiles LLM-generated Faust DSL at runtime. One `juce_add_plugin` call produces
`PluginForgeHost_VST3`, `PluginForgeHost_Standalone`, and the umbrella `PluginForgeHost_All`.

## Source map

| Pair | Role |
|---|---|
| `Source/FaustEngine.h/.cpp` | libfaust JIT wrapper — async `compile()` on a background thread, atomic `llvm_dsp*` swap with acquire/release ordering, `ParamInfo` metadata via `MapUI`. Protocol writeup: `docs/fixplan_pushtofaust_swap.md` |
| `Source/ParamPool.h/.cpp` | 64-slot parameter pool — slots declared in `createParameterLayout()`, looked up by `slotId(i)`; `remap()` relabels after compile (double-buffer + atomic index); `pushToFaust()` runs per audio block |
| `Source/PluginProcessor.h/.cpp` | `PluginForgeProcessor` — owns engine + pool, `loadFaustCode()` wires compile → remap, `onFaustCompileError` fires **off** the message thread |
| `Source/PluginEditor.h/.cpp` | UI — prompt box + Generate button; spawns `llm/generate.py --prompt` via `juce::ChildProcess`, parses JSON, calls `loadFaustCode()` |

⚠️ Audio-thread code (`processBlock` and everything it calls) and atomic swap patterns are
high-consequence, Tier-2-evidence territory (`COLLABORATION.md` §3) — not author-gated (§1:
"authorship is no longer gated by file category"), but `.claude/hooks/check_rt_safety.py`
blocks non-RT-safe constructs there mechanically, and a change here should cite the code it
changes, not just describe it.

## Build

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Debug -DJUCE_PATH=$HOME/JUCE
cmake --build build -- -j$(nproc)
```

If the build pulls in gtk/webkit or a VST2 header, see the comment block in `CMakeLists.txt`
(lines ~36–53): `JUCE_WEB_BROWSER=0` / `JUCE_USE_CURL=0` / `JUCE_VST3_CAN_REPLACE_VST2=0` must
stay set via `target_compile_definitions` — the `NEEDS_*` options alone only gate linking.

## ThreadSanitizer harness

`tests/ParamPoolConcurrencyTest.cpp` → target `ParamPoolTsanTest` (hand-written `main()`, not
CTest-registered). Hammers `processBlock()` against 20 alternating `loadFaustCode()` compiles:

```bash
cmake --build build --target ParamPoolTsanTest
./build/ParamPoolTsanTest_artefacts/Debug/ParamPoolTsanTest   # PASS = no TSan race reports
```

Known limitation: fixed 5s settle window for detached compile threads. Known open finding: a
suspected TOCTOU in the `FaustEngine` `activeUI` swap — report-only, not yet reproduced.

## Claude prompts for this area

From the root README series. The DELEGATE/PAIR tags below are vestigial (COLLABORATION.md §9
retired the three-mode protocol); items are listed in a sensible run order, not gated by mode.

- **P2** — build + run `ParamPoolTsanTest`, report races verbatim.
- **P3** — extend `.github/workflows/test.yml` to build the C++ targets in CI.
- **P4** — verify `generate.py` path resolution against real VST3/Standalone layouts. **Done,
  and it failed**: confirmed broken for an installed VST3 in REAPER — see `docs/BUGS.md` PF-065.
  The resolver is now `PromptPanel.cpp:136-157`, not `PluginEditor.cpp:15,29`.
- **P6** — end-to-end smoke test in the Standalone app: prompt → Faust → JIT → audible DSP
  with live parameters. This is the prototype finish line.
