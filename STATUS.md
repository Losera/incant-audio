# PluginForge — Status  (2026-07-31, session 2)

Rewritten each session per COLLABORATION.md §5. Single writer, no merge conflicts.
Narrative history lives in git.

**Start a session with `/orient`**, not by reading this file top to bottom. It injects live
repo state, **the CI line**, this file's open sections, and a staleness banner if it falls
behind HEAD.

---

## Works — and how we know

**This session found a hole in the audio path that had been open since the JIT landed, and
closed it.** The work was scoped by a three-agent review of the whole tree (DSP, UI, LLM
pipeline) run before anything was touched. The headline: **`FaustEngine::process` had no
arity guard of any kind**, and `check.sh audio` was measuring a lottery. Both are fixed,
both were seen failing first, and **the whole ladder is green including `audio` for the
first time**. Five commits pushed; `origin/main` was four behind at session start.

- **The host is stereo and the patch may not be.** *(PF-049 critical, PF-050 high — both
  found and fixed today.)* `process()` handed JUCE's channel array straight to
  `dsp->compute` with no bounds check. `juce_AudioSampleBuffer.h:342` returns the raw
  `channels` array and **`:441` null-terminates it** at index `numChannels`, so on a
  2-channel buffer `io[2]` is `nullptr` and `io[3]` is past the allocation. `faust -lang cpp`
  on `process = _,_,_;` emits `FAUSTFLOAT* output2 = outputs[2];` then `output2[i0] = ...` —
  **a null dereference on the audio thread**, reachable from the current corpus. Nothing in
  `host/Source/` called `getNumInputs`/`getNumOutputs`; `llm/faust_validator.py:6-16` only
  checks faust's return code. Now: `runCompile` rejects `numOuts < 1 || numOuts > 2 ||
  numIns > 2` before publishing, through the existing failure path, so it surfaces as a
  compile error the user can read. **Rejected, not clamped** — silently dropping channels 3+
  produces plausible-sounding wrong audio, which is harder to diagnose than a refusal.
- **A mono patch now reaches both speakers.** *(PF-050.)* `process()` routes mismatched arity
  through a scratch buffer **sized in `prepare()`, never in `process()`**, and duplicates a
  1-output patch to both channels. Faust only contracts in-place `compute()` when the arities
  agree, so 1-in/2-out could not stay in place. The equal-arity fast path is byte-identical to
  the old code: the common case pays nothing.
- **Seen failing, with numbers.** `OfflineRenderTest`'s corpus was twelve entries, **all
  2-in/2-out** — nothing had ever exercised this. Six arity cases added; before the fix
  **five failed**: the three over-wide patches went live with an arity the host cannot serve,
  and the two mono patches came out at `max |L-R|` of **0.73** and **0.25** (RMS L 0.212 /
  R 0.262 — oscillator left, dry signal right). After: **L == R exactly, `max |L-R| = 0.0`**,
  and all three over-wide patches refused at compile. `OfflineRenderTest` went to **124
  checks, 0 failures**. `test_the_real_audio_path_passes_its_own_hook` still green, so the
  new code satisfies `check_rt_safety.py`.
- **The audio rung stops being a lottery.** *(PF-046 fixed.)* It gated on
  `bench/results/results.json` — the file `level_quota` overwrites — so its verdict was a
  property of the last model draw. It now gates on `bench/ladder_corpus.json`: 19 frozen
  records from the 2026-07-31 ollama run, same schema so the oracle takes either file.
  **Verified rather than assumed:** the old gate pointed at a different archived draw fails 1,
  and a *different* patch (PF-032's noise gate); replacing `results.json` with that draw
  leaves the new gate at **16 passed, 0 failed, exit 0**. The 3 zero-input generators are kept
  in deliberately, so every run prints a standing reminder that the generative category sits
  outside the gate.
- **PF-045 is excluded from the ladder on purpose, and stays open.** The sawtooth+ADSR record
  fails `dc_offset` and `never_decays` because the patch passes `releaseTime * ma.SR / 1000.0`
  to `en.adsr`, whose times are in **seconds** (`/usr/share/faust/envelopes.lib:192-202`).
  The ladder gates the render path and the oracle — what it can control. Generation quality
  belongs to `check.sh quota`, which still writes `results.json` and still surfaces it.
- **The UI is untouched and proven so.** `tools/ui_iterate.sh`: **15 rendered, 0 broken,
  `no change`** against the committed reference after the DSP work.
- **Carried forward, unchanged:** the control-style selector and its in-place restyle
  (PF-047); Faust group capture (`ParamInfo::group`) — still captured, still nothing lays out
  by it; the render oracle's `tail()` burst probe and `never_decays` gate; the prompt
  regenerated under Faust 2.85.9; the noise floor is provider-side (PF-031); PF-012's
  cross-model comparison; pluginval at strictness 10; the full audio-path invariant list
  (PF-001/002/005/006/015/018/019/020/021/022/023).
- **Suite: `tools/check.sh audio` green end to end** — every rung, including `audio`.

---

## Broken — ranked

Registry with IDs, severity and discovery dates: `docs/BUGS.md`.

**1. There is no instrument support at all, and the corpus pretends otherwise.** *(unfiled,
high — the honest answer to "how are the synthesizer features developing".)* They are not.
`acceptsMidi()` returns `false` (`PluginProcessor.h:23`); the `MidiBuffer` parameter in
`processBlock` is **unnamed and never touched** (`PluginProcessor.cpp:91-92`);
`host/CMakeLists.txt:16-21` has `IS_SYNTH FALSE`, `NEEDS_MIDI_INPUT FALSE`,
`VST3_CATEGORIES Fx`; `FaustEngine` uses the plain mono JIT path — **zero repo-wide hits**
for `nvoices`, `midi:on`, `dsp_poly`, `MidiUI`; all seven prompt few-shots are 2-in/2-out
effects. Meanwhile the benchmark corpus has a `generative` category whose records the render
oracle **structurally cannot measure** (`UnsupportedPatch`, `render_oracle.py:161-174`). The
"sawtooth synth" record is `process = sawtoothOsc(_), adsrEnv;` — it treats the left input
channel as oscillator frequency. **This is an effects processor**, and today's arity fix is
what makes the mono generators it does produce at least audible on both channels.

**2. "Refine" does not refine.** *(unfiled, high.)* `LoadMode::Iterate` is a **knob-value
retention policy** (`PluginProcessor.h:59-63`). Ticking the toggle changes nothing about the
LLM call — the subprocess argv is `--prompt <text>` and nothing else
(`PromptPanel.cpp:367-370`). The model has never seen the prior patch, so "add a resonance
control" regenerates from imagination and **replaces** the live DSP. The same shape sits in
the retry loop: `generate.py:133` feeds back compiler stderr but **never the failed code**.
`currentSource()` already exists (`PluginProcessor.h:137-148`) and nothing reads it into the
generation path. Three findings from this session's design pass, all load-bearing:
  - **`juce::ChildProcess` cannot write to a child's stdin.** The complete API is
    `juce_ChildProcess.h:35-110` — no write method, no stdin handle, any platform. The POSIX
    implementation creates one pipe and `dup2`s it onto the child's stdout/stderr only
    (`juce_SharedCode_posix.h:1096-1155`); stdin is inherited from the host process. So
    `generate.py --json` over stdin **would hang in a DAW**. The transport must be
    `--request-file <path>` on argv.
  - **A refine payload can exceed groq's request ceiling.** groq admits a request only when
    `prompt_tokens + max_tokens <= 8000` (`llm/providers.py:68-72`), `max_tokens` cannot go
    below 4096 (`providers.py:764-766` × `:274`), and the prompt already leaves ~311 tokens
    of estimated slack. Generated patches measure p50 242 / p90 527 / max 1705 chars. So the
    tail **413s non-retryably** on the default provider. ollama has no such wall.
  - **The HUMAN-OWNED prompt gate no longer exists.** COLLABORATION.md Revision 2 §9 retired
    the three-mode protocol on 2026-07-21 and `protect_human_owned.py` was deleted in
    `cf1d8e8`. `docs/ux_roadmap.md:62-68` still describes it — a document that outlived its
    mechanism, the failure class COLLABORATION.md §5 exists to prevent. **Fix or delete that
    line.** What still binds is the evidence rule: a prompt edit owes a benchmark statement.

**3. One generation defect is actually evidenced; the rest is sampling.** *(PF-024/PF-032,
high.)* Karplus-Strong's `recursion_cycle` is the only failure reproducing prompt *and* class
across runs — four archives. The sidechain compressor fails every run with a *different*
error. **Fixing anything else on the strength of one run is fixing noise.**

**4. The noise gate still renders silent.** *(PF-032's surviving half, high.)*
`ef.gate_stereo(threshold, attack, hold, release, _, _)` — the signal written into the
argument list where `misceffects.lib:159` documents `_,_ : gate_stereo(t,a,h,r) : _,_`. The
prompt rule targeting it measured as not working on ollama.

**5. PF-045 — `en.*` time units.** *(medium, open, deliberately not spent today.)* The
two-line fix is a unit annotation at `tools/gen_stdlib_block.py:143-146`, the precedent one
group above. It costs prompt headroom (~124 tokens) and owes a benchmark statement, and the
**last two prompt rules aimed at this same class measured as doing nothing on ollama.**
Recorded as a decision, not an oversight.

**6. The DAW still sees raw slots.** *(follow-up to PF-037, open, unfiled.)*

**7. Knob ordering is Faust's own.** *(PF-038 low.)* Alphabetical per group and per widget;
`ParamGridPanel` does not sort.

**8. The only fidelity instrument is not interpretable.** *(PF-041 high, PF-042 medium.)*

**9. The declared ollama model cannot hold its own prompt.** *(PF-043, medium.)*

**10. `score_efficacy.py --judge` spends quota and takes no lock.** *(unfiled.)*

---

## Assumed, never checked

**Two claims. The number did not move today** — and this session bought a critical audio-path
fix rather than evidence about generation quality. Said plainly because the metric exists so
that a productive session cannot disguise itself as a measured one.

- **The efficacy pilot generalizes to nothing.** *(PF-011)* 125 generations ≈ 437k tokens ≈
  **2.2 days** on groq. Needs sharding — **or ollama**, which is unmetered.
- **Semantic fidelity is unmeasured.** *(PF-013)* Blocked **on the instrument, not on quota**
  — PF-041 and PF-042.

## Next three things

1. **The listening pass — it is the only thing today's work was for.** The build is current
   and the ladder is green; what is unproven is whether a generated plugin *sounds* like what
   was asked for. Effects only, and note the input bus is **required** — with no audio going
   in you hear nothing regardless of the patch.
2. **Make Refine carry the source.** The highest-leverage gap in the product, and
   `docs/competitive_landscape.md:107-109` names it. Sequence: one live groq generation
   measuring `usage.prompt_tokens` at `max_tokens=3000` (30 min, settles whether the token
   wall is real), then `--request-file` + `prior_source`, then the token pre-flight, then feed
   the failed code back into the retry. The red case is `scenario15_refineCarriesTheSource` in
   `EditorSessionTest` asserting the request payload contains the prior patch — **and its
   negative**, that a Fresh request does not, or "always send the source" passes while
   doubling every request.
3. *(evidence)* **Fix the judge before using it — PF-041 first.** Still the cheapest route to
   moving the `assumed` number, and it did not move today.

## Waiting on you

1. **The listening pass.** `host/build/PluginForgeHost_artefacts/Debug/Standalone/PluginForge
   Host` is current. The **VST3 has never been installed** — `COPY_PLUGIN_AFTER_BUILD FALSE`
   (`host/CMakeLists.txt:22`), and `~/.vst3` holds only Vital — so a DAW test needs the bundle
   copied into a scan path first. Provider: groq is active and fastest; ollama is free but
   **CPU-only until the box reboots** (the NVIDIA driver mismatch is still live).

2. **A ruling on instruments.** Item 1 under Broken is a scope question, not a defect. Minimum
   real scope: `IS_SYNTH TRUE` + `NEEDS_MIDI_INPUT TRUE`, `acceptsMidi()`, an input-optional
   bus layout, `dsp_poly_factory`/`createPolyDSPInstance` with `nvoices`, a MIDI →
   `freq`/`gain`/`gate` dispatcher inside the `enterAudio()` bracket, a prompt contract, at
   least one synth few-shot, and a generator path in `OfflineRenderTest` — the Python oracle
   structurally cannot cover 0-input patches. Each is a session.

3. **`docs/ux_roadmap.md:62-68` describes a gate that no longer exists** (Broken #2). One-line
   fix, but it is exactly the drift class this project keeps paying for.

4. **`bench/results/.prompt_baseline.json` is still untouched**, deliberately. It records
   `0.88` for the deleted pre-unification prompt. Replacing it should record a *spread*.

5. **`UDHR.md` is a 1-byte stub** and `IDEAS.md` is your idea dump — both untracked, both left
   alone.
