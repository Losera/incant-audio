# Session 017 — Phase 2: the interactive host session (operator run-script)

**Living document.** Written 2026-08-27 in auto mode from the plan
`~/.claude/plans/you-are-an-expert-silly-boot.md` (WP5). It is a **copy-paste operator
script plus a results table** for the first fully interactive DAW session — Carla, both
plugin targets, a human driving and listening. Modelled on the deleted
`docs/p6_human_run_script.md` (recover with `git show 9a6b39c^:docs/p6_human_run_script.md`),
which was effects-only and Standalone-only; this one adds the instrument target and a real
host.

**This doc does not run anything in a host.** Running it is WP6, which is
**human-required** — see the plan. The assistant's role in WP6 is: watch the `tee` log,
record each step into §6's table, and file `PF-NNN` rows for anything found.

---

## 1. Session-log preamble — what the planning session(s) settled

Recorded here so WP6 starts from settled fact, not reconstruction.

- **Tree synced to `main` @ `ce22ee6`**, up to date with `origin/main`. Four dead
  `/tmp/*` worktree pointers pruned.
- **JACK unblock — DONE (prior session, re-verified 2026-08-27).** `jack2` removed,
  `cmajor` removed, `pipewire-jack 1:1.6.8` installed and serving JACK: `jack_lsp`
  enumerates real ports, `pw-top` lists clients. This closes STATUS.md "Waiting on you
  #9".
- **WP6 tooling prerequisite — ALREADY SATISFIED.** The plan said
  `sudo pacman -S vmpk jack-example-tools` was still needed; both are in fact installed
  (`vmpk 0.9.2`, `jack-example-tools 4` — which provides `jack_lsp`). `carla 2.5.10` and
  `pluginval` (`~/.local/bin`) are present. Nothing to install before WP6.
- **MIDI path = `vmpk` (virtual MIDI keyboard) and/or Carla's built-in patchbay/keyboard.**
  The processor's MIDI walk handles noteOn/noteOff/allNotesOff only
  (`host/Source/PluginProcessor.cpp:318-333`) — **no CC, pitch-bend, or aftertouch** — so
  a mod-wheel / sustain / pitch-bend test is a *known-negative* probe, not a bug hunt.
- **PF-065** (generation fails as an installed VST3, "generate.py not found"): its
  architecture decision was made 2026-08-20 (option C, XDG-install resolution —
  `docs/BUGS.md` PF-065 detail, "Decision (2026-08-20…)"). A partial fix landed. Only a
  narrow residual is open: the `COPY_PLUGIN_AFTER_BUILD` dev-copy to `~/.vst3` still has
  neither a repo above it nor an XDG-installed runtime, so it still needs
  `PLUGINFORGE_LLM_SCRIPT` exported. Phase 3 below reproduces that residual; it does not
  close it.
- **ADR-030** (LangGraph deferred) and **ADR-031** (knowledge tooling) were drafted and
  **Accepted 2026-08-27** — `docs/decisions.md`. ADR-031's `COLLABORATION.md §8` Obsidian
  constraints landed with acceptance. They do not gate WP6.
- **`tools/kg.py` + `tests/test_control_wiring.py::TestIdReferencesResolve`** landed this
  session (WP3). Not relevant to the audio path; noted so a WP6 `git log` is not a surprise.

---

## 2. Phase 0 — prep (mechanical, can run before the live session)

Run each block. Do not proceed past a block that fails.

### 0.1 — Rebuild both targets against `ce22ee6` (or later)

Current `~/.vst3` bundles are dated 2026-08-26 and the Standalone Host build is 2026-08-25
— both **pre-`ce22ee6`**. Rebuild is required.

```bash
cd /home/losera/PluginForge
cmake -S host -B host/build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DJUCE_PATH=$HOME/JUCE
cmake --build host/build --target \
  PluginForgeHost PluginForgeHost_Standalone PluginForgeHost_VST3 \
  PluginForgeSynth PluginForgeSynth_Standalone PluginForgeSynth_VST3
```

`COPY_PLUGIN_AFTER_BUILD TRUE` (both targets, `host/CMakeLists.txt:60,139`) re-installs the
VST3s to `~/.vst3/` automatically.

### 0.2 — `pluginval` both fresh `.vst3` at strictness 5

```bash
pluginval --strictness-level 5 --validate "$HOME/.vst3/PluginForge Host.vst3"  2>&1 | tee ~/pv_host_$(date +%s).log
pluginval --strictness-level 5 --validate "$HOME/.vst3/PluginForge Synth.vst3" 2>&1 | tee ~/pv_synth_$(date +%s).log
```

Record **PASS/FAIL and the seed** each prints. (PF-062 was found exactly here: strictness-5
Audio-processing sub-tests on the Synth, freshly loaded, before any generation.)

### 0.3 — SHA-256 of both shared objects (P0.4 evidence)

```bash
sha256sum "$HOME/.vst3/PluginForge Host.vst3/Contents/x86_64-linux/PluginForge Host.so" \
          "$HOME/.vst3/PluginForge Synth.vst3/Contents/x86_64-linux/PluginForge Synth.so"
```

Paste both hashes into §6's header. This ties every observation below to an exact binary.

### 0.4 — Verify the toolchain (no installs expected)

```bash
which carla vmpk jack_lsp pluginval        # all four must resolve
jack_lsp | head                            # non-empty => JACK (via pipewire-jack) is live
pw-top -b -n1 | head                       # lists clients => PipeWire audio server up
```

If `jack_lsp` is empty or errors: PipeWire's JACK layer is not running — stop and fix
before Carla.

### 0.5 — Point Carla at the in-tree VST3s, and also `~/.vst3`

In Carla → *Settings → Plugin Paths → VST3*, add:

```
/home/losera/PluginForge/host/build/PluginForgeHost_artefacts/Debug/VST3
/home/losera/PluginForge/host/build/PluginForgeSynth_artefacts/Debug/VST3
/home/losera/.vst3
```

The **in-tree** path is what makes generation work with **no env var**:
`resolveGenerateScript()` (`host/Source/PromptPanel.cpp:110-144`) walks ≤10 parent dirs
from the loaded `.so` and finds `llm/generate.py` inside the repo. The `~/.vst3` copy is
kept in the list on purpose — it is the Phase 3 PF-065 repro.

### 0.6 — Launch Carla with the JACK driver, keep the log

```bash
carla 2>&1 | tee ~/carla_phase2_$(date +%s).log
```

In Carla, confirm *Settings → Engine → Audio driver = JACK* (not "Dummy"). Rescan plugins;
confirm `PluginForge Host` and `PluginForge Synth` both appear with **no red "failed to
load"**.

> **"Silence in the terminal is part of a pass."** Any `setParamValue … not found` line in
> the `tee` log is the signature of the param-mapping bug class the 2026-07-19 swap fix
> closed (`docs/fixplan_pushtofaust_swap.md`). Seeing it here is a real regression — capture
> the lines and file a `PF-NNN`.

---

## 3. Phase 1 — Effect target: `PluginForge Host` (Fx)

Load on an **audio track** with a signal running through it. Judge every generated sound
**against the dry input** — get the dry sound in your ears first (play a sustained source:
guitar, pad, drum loop). Source material for the anchor step: the sweep+noise reference,
`mpv --loop /home/losera/PluginForge/artifacts/audio/input_testsignal.wav`.

| # | Do | Expect | Watch for |
|---|----|--------|-----------|
| 1.1 | Scan + instantiate `PluginForge Host` on an Fx track | Loads, editor opens | red load error → stop, file it |
| 1.2 | Look at the editor rendered **in-host** at this machine's 1.5× fractional Wayland scale | It renders | **record any blur / stretched text — DO NOT FIX** (compositor-scale issue, no `hyprctl` mutation allowed). Note only. |
| 1.3 | Read the status label before generating | `Ready.` | an **empty-path** `generate.py not found at ` = PF-065 reproducing in-tree (should NOT happen — the parent-walk should find it) |
| 1.4 | Play audio through, no patch yet | Meter dances, dry passthrough audible | silence = audio routing wrong, not a plugin bug — check Carla patchbay first |
| 1.5 | Prompt `a warm low-pass filter with a cutoff knob` → Generate | status walks `Generating…` → `JIT compiling…` → `Ready — DSP live, N params mapped`; highs audibly rolled off | a hang, a crash, or a stuck status string |
| 1.6 | Count the parameter lanes Carla exposes | 64 `macro_*` automation targets | fewer than 64 = ParamPool contract broken (PF-051 family) |
| 1.7 | Sweep `macro_0` from Carla's generic editor | cutoff sweeps **audibly and smoothly** | zipper noise; `setParamValue … not found` in the log |
| 1.8 | Write an automation lane on `macro_0`, play it back | cutoff tracks the lane | value snaps / quantises visibly (PF-040 was 100-step quantisation) |
| 1.9 | Prompt `an aggressive distortion with drive and output level` → Generate (2nd gen over the 1st) | new "Ready — DSP live", different param count, sound switches | click / dropout **longer than one block** during the swap |
| 1.10 | Save the Carla project, close Carla, reopen it | plugin reloads, **recompiles**, knob values restored | knobs reset to patch defaults (PF-033 was exactly this) |
| 1.11 | Close and reopen just the editor window | reopens, state intact | crash on 2nd open |
| 1.12 | Remove the plugin, quit Carla | clean teardown | orphan `generate.py` process left running (`pgrep -af generate.py`) |

---

## 4. Phase 2 — Instrument target: `PluginForge Synth` (Instrument) — **never done before**

Load on an **instrument/MIDI track**. Route MIDI from `vmpk` (or Carla's built-in keyboard
/ a MIDI clip). This is the first interactive exercise of the instrument path in a host.

| # | Do | Expect | Grounding / watch for |
|---|----|--------|-----------------------|
| 2.1 | Instantiate `PluginForge Synth` on an instrument track | loads; MIDI-in shown; **no** NaN/garbage on the output meter before any note | PF-062 (fixed 2026-08-16) was garbage-out here — confirm it stays fixed |
| 2.2 | Prompt an instrument, e.g. `a warm analog pad with a slow attack` → Generate | "Ready — DSP live"; the keyboard panel **enables** (stops looking dimmed) | PF-057 / PF-068 family — a keyboard that looks live but eats notes |
| 2.3 | Play a note from `vmpk` / a MIDI clip | **first-ever audible instrument note in a host** | if silent: check `FaustEngine` voice contract captured a `freq`/`gate`/`gain` control |
| 2.4 | With the **editor window focused in Carla**, press a QWERTY letter key | a note sounds | this is the **only route that narrows STATUS.md "Broken #1"** (OS→JUCE keypress hop, `KeyboardPanel.cpp:150-152`) — record verbatim what happens |
| 2.5 | Hold a 3-note chord | **one** note sounds — mono, last-note priority | expected, `FaustEngine.cpp:547-562`. Not a bug; confirm it matches the spec |
| 2.6 | Play fast trills / repeated notes | each note articulates, none cut short | block-granularity note handling, `PluginProcessor.cpp:290-297` |
| 2.7 | **Tail (P1.9):** long-release pad, release the key, listen | tail is **cut at ~2.0 s** | hardcoded `getTailLengthSeconds() = 2.0` for synth, `PluginProcessor.h:79-85` — record the actual audible cutoff |
| 2.8 | **Stuck-note:** hold a MIDI note down, hit Generate while held | the swap window **drops the note-off** (`PluginProcessor.cpp:266-274`) → predict a stuck note → does Carla's MIDI-panic recover it? | record whether panic clears it |
| 2.9 | **CC probe:** send mod-wheel, sustain pedal, pitch-bend | **silently ignored** | known-negative — `PluginProcessor.cpp:325-333` handles only noteOn/Off/allNotesOff. Confirm "ignored", not "crashes" |
| 2.10 | **Timing:** play steady fast 16ths | audible ~10.7 ms (one block @ 512/48k) quantisation of note starts | expected; note if it's worse |
| 2.11 | **Runaway:** generate a deliberately loud square-wave synth, play a low note | audio **continues** at ≈ −0.3 dBFS, **no UI warning** | ADR-020 honest gap, `docs/decisions.md:569-573` + `OutputGuard.h:53-96` — instruments are report-only. Confirm the gap is real and visible (i.e. invisible). |
| 2.12 | Change Carla's sample rate mid-session (48k → 44.1k) with a patch loaded | patch keeps working, re-prepared cleanly | probes the P0.5 `prepare()` race (`FaustEngine.cpp`, PF-018 was this) — **probed, not proven** by one observation |
| 2.13 | Save the Carla project with an instrument patch, reopen | patch + knob values restore, recompiles | — |
| 2.14 | Load **both** `PluginForge Host` and `PluginForge Synth` in one Carla project | both run; `Pfh1` / `Pfs1` stay separate; two editors openable | class-name / instance separation |

---

## 5. Phase 3 — the PF-065 residual repro (1 step)

| # | Do | Expect |
|---|----|--------|
| 3.1 | Remove the plugin, re-add it **from the `~/.vst3` path** (not the in-tree build path), with **no `PLUGINFORGE_LLM_SCRIPT`** in Carla's environment. Prompt anything → Generate | status shows the empty-path `generate.py not found at ` — the exact PF-065 signature. Generalises the REAPER report to Carla; **does not close PF-065.** |

To make generation work from `~/.vst3` today: `export PLUGINFORGE_LLM_SCRIPT=/home/losera/PluginForge/llm/generate.py`
before launching Carla. Confirm that path does work — it is the mechanism
`PromptPanel.cpp`'s own comment names as supported for this case.

---

## 6. Phase 4 — results table (fill in live, not from memory)

**Binary under test:**
`PluginForge Host.so` sha256 `__________`  ·  `PluginForge Synth.so` sha256 `__________`
· built from `git rev-parse HEAD` `__________` · `tee` log: `~/carla_phase2_________.log`
· `pluginval` seeds: Host `______` Synth `______`

The **objective** column (pluginval / hashes / log lines) and the **subjective** column
("sounds like the words?") are separate on purpose (P1.5): a patch can pass every render-
oracle check and still not sound like what was asked for, and only a human fills the last
column.

| # | Target | Step | Expect | PASS / FAIL / PARTIAL | Observed (objective) | Sounds right? (subjective) | Notable log lines | PF-NNN filed |
|---|--------|------|--------|-----------------------|----------------------|----------------------------|-------------------|--------------|
| 1.1 | Host | instantiate | loads | | | n/a | | |
| 1.2 | Host | in-host render @ 1.5× | renders | | | n/a | | (blur: record only) |
| 1.3 | Host | status pre-gen | `Ready.` | | | n/a | | |
| 1.4 | Host | dry passthrough | meter + audio | | | | | |
| 1.5 | Host | gen low-pass filter | status walk + darker | | | | | |
| 1.6 | Host | param lanes | 64 `macro_*` | | | n/a | | |
| 1.7 | Host | sweep `macro_0` | smooth, audible | | | | | |
| 1.8 | Host | automation write/playback | tracks lane | | | | | |
| 1.9 | Host | 2nd gen over 1st | clean swap | | | | | |
| 1.10 | Host | save / reopen project | values restore | | | n/a | | |
| 1.11 | Host | reopen editor window | intact | | | n/a | | |
| 1.12 | Host | remove + quit | clean, no orphan proc | | | n/a | | |
| 2.1 | Synth | instantiate | no NaN pre-note | | | n/a | | |
| 2.2 | Synth | gen instrument | keyboard enables | | | | | |
| 2.3 | Synth | first note (MIDI) | **audible** | | | | | |
| 2.4 | Synth | QWERTY note, editor focused | note sounds | | | | | (Broken #1) |
| 2.5 | Synth | 3-note chord | mono last-note | | | | | |
| 2.6 | Synth | fast trills | no cut-short | | | | | |
| 2.7 | Synth | tail | cut ~2.0 s | | | | | (P1.9) |
| 2.8 | Synth | stuck-note on gen | drop → panic recovers? | | | | | |
| 2.9 | Synth | CC / bend / sustain | ignored, no crash | | | n/a | | |
| 2.10 | Synth | fast 16ths timing | ~10.7 ms quantise | | | | | |
| 2.11 | Synth | runaway square | audio continues, no UI warn | | | | | (ADR-020 gap) |
| 2.12 | Synth | sample-rate change | clean re-prepare | | | | | (P0.5) |
| 2.13 | Synth | save / reopen w/ patch | restores | | | n/a | | |
| 2.14 | both | both loaded at once | separate, stable | | | | | |
| 3.1 | Host | PF-065 repro from `~/.vst3` | empty-path "not found" | | | n/a | | (PF-065, expected) |

**Where "sounds right?" is *partly* or *no*:** one sentence on *how* it missed (wrong
effect entirely / right effect wrong intensity / right idea, unmusical parameter range) is
worth more than the verdict.

---

## 7. Capture rules

- **Before every `grim`:** compare `hyprctl clients -j` against `hyprctl activeworkspace -j`.
  `tools/screenshot_ui.sh`'s class filter matches "PluginForge" and is **ambiguous** with
  Carla + a Standalone + two editor windows open — it can grab the wrong surface. Never call
  `hyprctl keyword monitor` or any live compositor mutation (HARD CONSTRAINT).
- Keep the `tee` log for the whole session; attach its path to §6.
- **"Silence in the terminal is part of a pass."** A quiet log is evidence, not an absence
  of it.
- File `PF-NNN` rows in `docs/BUGS.md` (registry row **and** a detail section) plus a
  STATUS.md "Broken" line for anything found. The new
  `tests/test_control_wiring.py::TestIdReferencesResolve` check means a `PF-NNN` you mention
  in STATUS.md **must** have a `docs/BUGS.md` row or `tools/check.sh fast` goes red.

---

## 8. On done (WP6 close-out — see the plan's WP6/WP7)

- Fill §6's table; attach the `tee` log path and both `.so` hashes.
- Update `STATUS.md`: Broken #1 (OS→JUCE hop — narrowed or closed by the human QWERTY
  observation at step 2.4), Broken #2 (interactive host — closed or narrowed), "Waiting on
  you #9" (JACK — close), and a new dated "Works — and how we know" entry with the evidence.
  Move `assumed` if any claim shifted.
- COLLABORATION.md §4 change report. Its `YOUR MOVE` line assumes a listening pass outranks
  a diff read.
- **Advances:** P0.4 (dev-build evidence; the installed-bundle gap stays open — Phase 3),
  P1.9 (tail data point), P0.5 (`prepare()` race — probed, not proven), ADR-020 honest gap
  (confirmed live at step 2.11).
