# host/ — JUCE JIT Host Plugin

The C++ side of PluginForge: a JUCE 7 plugin (VST3 + Standalone) that embeds libfaust/LLVM and
JIT-compiles LLM-generated Faust DSL at runtime. One `juce_add_plugin` call produces
`PluginForgeHost_VST3`, `PluginForgeHost_Standalone`, and the umbrella `PluginForgeHost_All`.

## Source map

| Pair | Role |
|---|---|
| `Source/FaustEngine.h/.cpp` | libfaust JIT wrapper — async `compile()` on a background thread, atomic `llvm_dsp*` swap with acquire/release ordering, `ParamInfo` metadata via `MapUI`. Reference impl: `docs/audio_thread_example.md` |
| `Source/ParamPool.h/.cpp` | 64-slot parameter pool — slots declared in `createParameterLayout()`, looked up by `slotId(i)`; `remap()` relabels after compile (double-buffer + atomic index); `pushToFaust()` runs per audio block |
| `Source/PluginProcessor.h/.cpp` | `PluginForgeProcessor` — owns engine + pool, `loadFaustCode()` wires compile → remap, `onFaustCompileError` fires **off** the message thread |
| `Source/PluginEditor.h/.cpp` | UI — prompt box + Generate button; spawns `llm/generate.py --prompt` via `juce::ChildProcess`, parses JSON, calls `loadFaustCode()` |

⚠️ Audio-thread code (`processBlock` and everything it calls) and atomic swap patterns are
HUMAN-OWNED per `COLLABORATION.md` §1; `.claude/hooks/check_rt_safety.py` blocks non-RT-safe
constructs there mechanically.

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
suspected TOCTOU in the `FaustEngine` `activeUI` swap — HUMAN-OWNED, report-only.

## Claude prompts for this area

From the root README series — run in order, hold Claude to the stated mode:

- **P2** *(DELEGATE)* — build + run `ParamPoolTsanTest`, report races verbatim, fix nothing
  HUMAN-OWNED.
- **P3** *(DELEGATE)* — extend `.github/workflows/test.yml` to build the C++ targets in CI.
- **P4** *(PAIR)* — verify the `TODO VERIFY` path assumption at `Source/PluginEditor.cpp:15,29`
  (finding `llm/generate.py` relative to the installed binary) against real VST3/Standalone
  layouts.
- **P6** *(PAIR)* — end-to-end smoke test in the Standalone app: prompt → Faust → JIT → audible
  DSP with live parameters. This is the prototype finish line.
