# Session 017 — Phase 2: the interactive host session (operator run-script + as-run log)

**As-run log.** Written 2026-08-27 in auto mode from the plan
`~/.claude/plans/you-are-an-expert-silly-boot.md` (WP5); **host section redrafted
2026-08-28 for REAPER** after Carla-vs-REAPER came up mid-session; **run in REAPER
2026-08-28 (WP6)** — see §9 for the as-run results. It began as a **copy-paste
operator script plus a blank results table** for the first fully interactive DAW session —
both plugin targets, a human driving and listening — modelled on the deleted
`docs/p6_human_run_script.md` (recover with `git show 9a6b39c^:docs/p6_human_run_script.md`),
which was effects-only and Standalone-only; this one adds the instrument target and a real
host. §§0–8 are the script as issued; **§9 is what actually happened.**

**Host: REAPER (primary). Carla is fine too.** The interactive session needs nothing
Carla-specific — instantiate, generate, play MIDI, move controls, automate, save/reopen,
listen. REAPER does all of it, the operator is fluent in it (fewer operator errors, more
attention on *listening* — which §1 says is the point), it is closer to the advertised
workflow, and PF-065 was originally found in it. Carla's one prior contribution — the
headless `carla-discovery-native` scan — is **done and cited as existing evidence**; it is
not part of this session. Every step below that names "Carla" works identically in REAPER;
the few real differences (audio backend, MIDI panic, the raw-slot parameter view) are
called out inline.

**§§0–8 were the script; running it was WP6, human-required.** WP6 ran on 2026-08-28 in
REAPER. The as-run record is **§9** — a per-step verdict table that supersedes §6's blank
grid (§6 is kept as the script's original shape, not filled in retroactively: §9 is the
only faithful record and is candid about what was not captured). The assistant's role in
WP6 was: watch the `tee` log, record each step, and file `PF-NNN` rows for anything found
(PF-072–075 were).

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
- **PF-065 / PF-071 — the plugin carries no config; it depends on the DAW's inherited
  environment for everything** (`PLUGINFORGE_PROVIDER`, `PLUGINFORGE_LLM_SCRIPT`,
  `PLUGINFORGE_SOUNDFETCH_PYTHON`, keys via `.env`). A DAW started from a desktop launcher
  has none of it. `resolveGenerateScript()` (`host/Source/PromptPanel.cpp:110-144`) then
  falls through its chain — env override → 10-level upward walk from the `.so` → **XDG
  install** — to `~/.local/share/pluginforge/llm/generate.py`, a **stale 2026-08-15 copy**
  where `DEFAULT_PROVIDER = "anthropic"` (predates the groq default) and there is **no
  `.env`** beside it. Result: an unset provider resolves to the *paid* provider and the UI
  shows an "anthropic provider error" — **reproduced 2026-08-28 in both REAPER and Carla**.
  That is PF-071 (filed). **§0.0 below removes the trap and sets the environment** so
  generation works from any host. Phase 3 still reproduces the raw PF-065
  script-not-found signature deliberately.
- **ADR-030** (LangGraph deferred) and **ADR-031** (knowledge tooling) were drafted and
  **Accepted 2026-08-27** — `docs/decisions.md`. ADR-031's `COLLABORATION.md §8` Obsidian
  constraints landed with acceptance. They do not gate WP6.
- **`tools/kg.py` + `tests/test_control_wiring.py::TestIdReferencesResolve`** landed this
  session (WP3). Not relevant to the audio path; noted so a WP6 `git log` is not a surprise.

---

## 2. Phase 0 — prep (mechanical, can run before the live session)

Run each block. Do not proceed past a block that fails.

### 0.0 — Environment: remove the stale trap, then launch the host from a configured shell

**Do this first.** It is the fix for the "anthropic provider error" (PF-071) and for
Soundfetch "cannot fetch anything".

```bash
# 1. Remove the stale XDG-installed runtime (2026-08-15, defaults to the PAID provider,
#    has no .env). With it gone the plugin resolves generate.py from the in-tree build
#    (step 0.5) or cleanly reports "not found" — never silently runs the wrong thing.
rm -rf ~/.local/share/pluginforge

# 2. Launch the DAW FROM THIS SHELL so it inherits the config:
cd /home/losera/PluginForge
set -a; source .env; set +a                       # PLUGINFORGE_PROVIDER, API keys
export PLUGINFORGE_LLM_SCRIPT="$PWD/llm/generate.py"
export PLUGINFORGE_SOUNDFETCH_PYTHON="$HOME/soundfetch/.venv/bin/python"
python3 llm/providers.py --check "$PLUGINFORGE_PROVIDER"   # confirm the provider is live

reaper 2>&1 | tee ~/pf_phase2_$(date +%s).log      # or: carla 2>&1 | tee ...
```

- If `--check` shows the provider throttled / out of quota: **groq's daily token limit was
  spent 2026-08-28**. Set `PLUGINFORGE_PROVIDER=gemini` in `.env` (key present, free) and
  re-source.
- Soundfetch still failing after this = **PF-056** — the Freesound key is 403'd; it needs
  replacing from freesound.org. Not fixable here.
- A DAW **already running** from a launcher will not pick up these exports — quit it first.

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

> **Raw console capture moved.** The full `cmake --build` + two `pluginval` logs from
> this run (~485 lines) were pasted inline here during WP6; they are now in
> **Appendix A** at the end of this document so §0.2's instructions stay readable.

### 0.3 — SHA-256 of both shared objects (P0.4 evidence)

```bash
sha256sum "$HOME/.vst3/PluginForge Host.vst3/Contents/x86_64-linux/PluginForge Host.so" \
          "$HOME/.vst3/PluginForge Synth.vst3/Contents/x86_64-linux/PluginForge Synth.so"
```

Paste both hashes into §6's header. This ties every observation below to an exact binary.

### 0.4 — Verify the toolchain

```bash
which reaper vmpk pluginval                 # host + MIDI keyboard + validator
which carla jack_lsp 2>/dev/null            # only needed if you use Carla
pw-top -b -n1 | head                        # lists clients => PipeWire audio server up
```

**REAPER** talks to PipeWire directly (ALSA or PipeWire backend) — no JACK layer needed.
**Carla** needs JACK: `jack_lsp` must enumerate ports (pipewire-jack provides it).

### 0.5 — Point the host at the in-tree VST3s

The **in-tree build** path is what makes generation work: `resolveGenerateScript()`
(`host/Source/PromptPanel.cpp:110-144`) walks ≤10 parent dirs from the loaded `.so` and
finds `llm/generate.py` in the repo. Combined with `PLUGINFORGE_LLM_SCRIPT` from §0.0,
either route resolves the fresh script.

- **REAPER** → *Options → Preferences → Plug-ins → VST → Edit path list*, add:
  ```
  /home/losera/PluginForge/host/build/PluginForgeHost_artefacts/Debug/VST3
  /home/losera/PluginForge/host/build/PluginForgeSynth_artefacts/Debug/VST3
  ```
  then *Re-scan*. (Add `/home/losera/.vst3` **only** for the Phase 3 repro, on a separate
  scan — you want the in-tree one loaded for Phases 1–2.)
- **Carla** → *Settings → Plugin Paths → VST3*, same list.

### 0.6 — Launch the host, keep the log

You already launched it in §0.0 with `tee`. Confirm:

- **REAPER**: *Options → Preferences → Audio → Device* = ALSA or PipeWire, running. Insert
  a track; the FX browser shows `PluginForge Host` and `PluginForge Synth` with **no scan
  error**.
- **Carla**: *Settings → Engine → Audio driver = JACK* (not "Dummy"). Rescan; both plugins
  appear with **no red "failed to load"**.

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
| 2.4 | With the **plugin editor window focused** (REAPER: the floating FX window; Carla: the plugin GUI), press a QWERTY letter key | a note sounds | this is the **only route that narrows STATUS.md "Broken #1"** (OS→JUCE keypress hop, `KeyboardPanel.cpp:150-152`) — record verbatim what happens. REAPER note: turn OFF *Options → "Send all keyboard input to plug-in"* is NOT what you want — you DO want the plug-in to get keys; make sure REAPER isn't stealing them for its own shortcuts (focus the FX window, not the arrange view) |
| 2.5 | Hold a 3-note chord | **one** note sounds — mono, last-note priority | expected, `FaustEngine.cpp:547-562`. Not a bug; confirm it matches the spec |
| 2.6 | Play fast trills / repeated notes | each note articulates, none cut short | block-granularity note handling, `PluginProcessor.cpp:290-297` |
| 2.7 | **Tail (P1.9):** long-release pad, release the key, listen | tail is **cut at ~2.0 s** | hardcoded `getTailLengthSeconds() = 2.0` for synth, `PluginProcessor.h:79-85` — record the actual audible cutoff |
| 2.8 | **Stuck-note:** hold a MIDI note down, hit Generate while held | the swap window **drops the note-off** (`PluginProcessor.cpp:266-274`) → predict a stuck note → does the host's MIDI panic recover it? (REAPER: *Actions → "Reset all MIDI devices"* or the track's panic; Carla: the keyboard panic button) | record whether panic clears it |
| 2.9 | **CC probe:** send mod-wheel, sustain pedal, pitch-bend | **silently ignored** | known-negative — `PluginProcessor.cpp:325-333` handles only noteOn/Off/allNotesOff. Confirm "ignored", not "crashes" |
| 2.10 | **Timing:** play steady fast 16ths | audible ~10.7 ms (one block @ 512/48k) quantisation of note starts | expected; note if it's worse |
| 2.11 | **Runaway:** generate a deliberately loud square-wave synth, play a low note | audio **continues** at ≈ −0.3 dBFS, **no UI warning** | ADR-020 honest gap, `docs/decisions.md:569-573` + `OutputGuard.h:53-96` — instruments are report-only. Confirm the gap is real and visible (i.e. invisible). |
| 2.12 | Change Carla's sample rate mid-session (48k → 44.1k) with a patch loaded | patch keeps working, re-prepared cleanly | probes the P0.5 `prepare()` race (`FaustEngine.cpp`, PF-018 was this) — **probed, not proven** by one observation |
| 2.13 | Save the Carla project with an instrument patch, reopen | patch + knob values restore, recompiles | — |
| 2.14 | Load **both** `PluginForge Host` and `PluginForge Synth` in one Carla project | both run; `Pfh1` / `Pfs1` stay separate; two editors openable | class-name / instance separation |

---

## 5. Phase 3 — the PF-065 / PF-071 repro (deliberately recreate the broken state)

§0.0 fixed the environment for Phases 1–2. This phase puts it back the way a real installed
launch sees it, to confirm the two failure modes on the record.

| # | Do | Expect |
|---|----|--------|
| 3.1 | **Quit the host.** From a *plain launcher* (or `env -i … reaper` with no `PLUGINFORGE_*`), load the plugin **from `~/.vst3`** (not the in-tree build). Prompt anything → Generate | With `~/.local/share/pluginforge` **absent** (you removed it in §0.0): the empty-path `generate.py not found at ` — the raw PF-065 signature. |
| 3.2 | Recreate the stale XDG runtime: `git show a869e29:… ` is overkill — instead `mkdir -p ~/.local/share/pluginforge && cp -r llm ~/.local/share/pluginforge/` from an **old checkout**, or just note this step as "not re-run" if you don't want to rebuild the trap. With a stale copy present and no `.env` beside it → Generate | the **"anthropic provider error"** — PF-071. (Skip if you'd rather not rebuild the trap; the 2026-08-28 in-REAPER/in-Carla reproduction is already on the PF-071 record.) |

Neither closes PF-065 or PF-071. To make generation work from `~/.vst3` today, the
supported mechanism is `export PLUGINFORGE_LLM_SCRIPT=/home/losera/PluginForge/llm/generate.py`
before launching the host (as §0.0 does).

---

## 6. Phase 4 — results table (blank template — **see §9 for the as-run results**)

> **This grid was never filled in.** WP6's results are in **§9** as a per-step verdict
> table. §9 is the only faithful record of the 2026-08-28 REAPER run and is explicit about
> the steps it could not capture. This template is kept unmodified to show the shape the
> script asked for; do not back-fill it from memory.

**Binary under test:**
`PluginForge Host.so` sha256 `__________`  ·  `PluginForge Synth.so` sha256 `__________`
· built from `git rev-parse HEAD` `__________` · host: `REAPER __ / Carla __`
· `tee` log: `~/pf_phase2_________.log` · `pluginval` seeds: Host `______` Synth `______`

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
  the host + a Standalone + two editor windows open — it can grab the wrong surface. REAPER's
  FX window is its own top-level surface (class `REAPER`), so a `hyprctl clients -j | grep -i
  pluginforge` disambiguates the plugin editor from the host. Never call `hyprctl keyword
  monitor` or any live compositor mutation (HARD CONSTRAINT).
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

---

## 9. WP6 results — as run, 2026-08-28 (REAPER)

The raw `cmake` + `pluginval` console capture is in Appendix A; this is the settled
summary. Host:
**REAPER** on JACK (pipewire-jack). `tee` log `~/pf_phase2_1787934530.log` (JUCE/JACK
startup only — the generate.py subprocess output is captured by the plugin, not REAPER's
stderr, so `setParamValue`/generation errors do not land here; this is a real limit of the
"watch the tee log" plan).

| step | verdict | note |
|---|---|---|
| 0.0–0.6 environment / launch | PASS | after `rm -rf ~/.local/share/pluginforge` + launch from a `.env`-sourced shell with `PLUGINFORGE_LLM_SCRIPT` exported. The stale-XDG "anthropic provider error" (PF-071) hit first; the empty-path "generate.py not found" (PF-065) hit when the `~/.vst3` bundle was loaded without the env — both then resolved. |
| 1.1–1.4 Fx instantiate / render / status / dry passthrough | PASS | in-tree resolver found `generate.py`; status `Ready.`; looping `input_testsignal.wav` audible, meter moving. |
| 1.5 generate low-pass | PASS | highs audibly rolled off. |
| 1.x "New"-mode generation (reverb; "reverb with chorus") | PASS | full patches, all expected controls. |
| refine / "Add" mode ("reverb with chorus" over a working reverb) | **FAIL → PF-072** | produced a patch with only a dry/wet knob. Same prompt in New mode = full patch. Mechanism verified OK on gemini afterward; groq repro blocked (TPD spent). |
| 1.9 / 2.9 2nd-generation DSP swap | **PARTIAL → PF-073** | "didn't seem to be a very smooth transition." Not characterised (click vs gap vs dropout). No `setParamValue` flood in the log. |
| 2.1 Synth instantiate | PASS | no NaN/garbage before a note (PF-062 stays fixed). |
| 2.2 generate instrument; keyboard enables | PASS | "warm analog synth pad" generated; keyboard panel enabled. |
| 2.3 first audible instrument note in a host | **PASS — first time ever.** | |
| **2.4 QWERTY note, editor focused** | **PASS — closes Broken #1.** | "expanded to the entirety of my keyboard pretty much," mild latency (block quantization + JIT). |
| 2.5 chord | **AMBIGUOUS → PF-075** | heard "up to 5 voices" from a strictly-monophonic engine. Almost certainly overlapping release/reverb tails. Held-chord test (with a MIDI controller) pending. |
| 2.6 fast trills | PASS | none cut short. |
| 2.7 tail | not isolated | (long-release pad tail not separately measured). |
| 2.8 stuck-note on generate | PASS (tentative) | no stuck note when a chorus refine ran; confirm a key was held *down* during Generate. |
| 2.10 timing / 16ths | ~as expected | ~10.7 ms quantization felt as latency under load. |
| 2.11 runaway | inconclusive | no deliberately-loud patch generated. |
| 2.12 sample-rate change mid-session | deferred | operator did not run it. |
| — NaN during play | **FAIL → PF-074** | a generated instrument patch produced NaN/Inf while playing, needed regeneration. Not characterised (silent vs stayed-broken; triggering patch/action not captured). `OfflineSynthRenderTest` is 184/0. |
| 2.13 save/reopen with an instrument patch | not run | |
| 2.14 both plugins in one project | PASS | Host on the audio track + Synth on a new track; no cross-destabilisation. |
| — Synth on a *dedicated* track: no sound | not a plugin bug | REAPER track arm/monitor config. `OfflineSynthRenderTest` green; the Synth *did* play when on the sample track. |
| 3.1 PF-065 repro | observed (early, by accident) | the empty-path "not found at " before the environment was fixed. |

**Wins:** STATUS.md **Broken #1 and #2 both closed.** First QWERTY-key-to-audible-note in a
real host; first interactive session for both plugin targets.

**Filed:** PF-072 (refine 1-knob), PF-073 (rough swap), PF-074 (NaN in play), PF-075
(chord-poly from a mono engine) — all open, all needing a captured repro.

**Not advanced as hoped:** 2.7 (tail), 2.11 (runaway), 2.12 (sample-rate), 2.13
(save/reopen) — left for a follow-up session or a MIDI-controller pass.

---

## Appendix A — raw console capture (§0.2 run, 2026-08-28)

Operator's inline paste from the WP6 run: `cmake --build` output (compiler warnings
included) followed by both `pluginval --strictness-level 5` logs, each ending `SUCCESS`.
Verbatim except one stray ```` ``` ```` marker removed from mid-paste so this block renders.
Kept as run evidence for §0.2 and §0.3.

```
Info from pluginval and cmake: PluginForge main ? ❯ cmake --build host/build --target \
  PluginForgeHost PluginForgeHost_Standalone PluginForgeHost_VST3 \
  PluginForgeSynth PluginForgeSynth_Standalone PluginForgeSynth_VST3
[0/2] Re-checking globbed directories...
[6/26] Building CXX object CMakeFiles/Pl...ForgeHost.dir/Source/KeyboardPanel.cpp.o
In file included from /home/losera/JUCE/modules/juce_audio_processors/juce_audio_processors.h:145,
                 from /home/losera/JUCE/modules/juce_audio_utils/juce_audio_utils.h:60,
                 from /home/losera/PluginForge/host/Source/KeyboardPanel.h:2,
                 from /home/losera/PluginForge/host/Source/KeyboardPanel.cpp:1:
/home/losera/JUCE/modules/juce_audio_processors/processors/juce_AudioProcessor.h:280:18: warning: ‘virtual void juce::AudioProcessor::processBlock(juce::AudioBuffer<double>&, juce::MidiBuffer&)’ was hidden [-Woverloaded-virtual=]
  280 |     virtual void processBlock (AudioBuffer<double>& buffer,
      |                  ^~~~~~~~~~~~
In file included from /home/losera/PluginForge/host/Source/KeyboardPanel.h:3:
/home/losera/PluginForge/host/Source/PluginProcessor.h:42:10: note:   by ‘virtual void PluginForgeProcessor::processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&)’
   42 |     void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
      |          ^~~~~~~~~~~~
[7/26] Building CXX object CMakeFiles/PluginForgeSynth.dir/Source/FaustEngine.cpp.o
/home/losera/PluginForge/host/Source/FaustEngine.cpp: In member function ‘FaustEngine::ParamInfo ParamCapture::consume(const char*, float*, float, float, float, float, FaustEngine::Kind)’:
/home/losera/PluginForge/host/Source/FaustEngine.cpp:135:88: warning: missing initializer for member ‘FaustEngine::ParamInfo::unit’ [-Wmissing-field-initializers]
  135 |         FaustEngine::ParamInfo info { label ? label : "", init, fmin, fmax, step, kind };
      |                                                                                        ^
/home/losera/PluginForge/host/Source/FaustEngine.cpp:135:88: warning: missing initializer for member ‘FaustEngine::ParamInfo::style’ [-Wmissing-field-initializers]
/home/losera/PluginForge/host/Source/FaustEngine.cpp:135:88: warning: missing initializer for member ‘FaustEngine::ParamInfo::group’ [-Wmissing-field-initializers]
/home/losera/PluginForge/host/Source/FaustEngine.cpp:135:88: warning: missing initializer for member ‘FaustEngine::ParamInfo::id’ [-Wmissing-field-initializers]
/home/losera/PluginForge/host/Source/FaustEngine.cpp: In member function ‘void FaustEngine::prepare(double, int)’:
/home/losera/PluginForge/host/Source/FaustEngine.cpp:613:42: warning: comparing floating-point with ‘==’ or ‘!=’ is unsafe [-Wfloat-equal]
  613 |     const bool rateChanged = (sampleRate != sr);
      |                               ~~~~~~~~~~~^~~~~
/home/losera/PluginForge/host/Source/FaustEngine.cpp: In member function ‘void FaustEngine::runCompile(const std::string&, const CompileCallback&)’:
/home/losera/PluginForge/host/Source/FaustEngine.cpp:864:41: warning: declaration of ‘lock’ shadows a previous local [-Wshadow]
  864 |             std::lock_guard<std::mutex> lock(jobMutex);
      |                                         ^~~~
/home/losera/PluginForge/host/Source/FaustEngine.cpp:829:37: note: shadowed declaration is here
  829 |         std::lock_guard<std::mutex> lock(compileMutex);
      |                                     ^~~~
[8/26] Building CXX object CMakeFiles/PluginForgeHost.dir/Source/PromptPanel.cpp.o
In file included from /home/losera/JUCE/modules/juce_audio_processors/juce_audio_processors.h:145,
                 from /home/losera/PluginForge/host/Source/PromptPanel.h:2,
                 from /home/losera/PluginForge/host/Source/PromptPanel.cpp:1:
/home/losera/JUCE/modules/juce_audio_processors/processors/juce_AudioProcessor.h:280:18: warning: ‘virtual void juce::AudioProcessor::processBlock(juce::AudioBuffer<double>&, juce::MidiBuffer&)’ was hidden [-Woverloaded-virtual=]
  280 |     virtual void processBlock (AudioBuffer<double>& buffer,
      |                  ^~~~~~~~~~~~
In file included from /home/losera/PluginForge/host/Source/PromptPanel.h:3:
/home/losera/PluginForge/host/Source/PluginProcessor.h:42:10: note:   by ‘virtual void PluginForgeProcessor::processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&)’
   42 |     void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
      |          ^~~~~~~~~~~~
/home/losera/PluginForge/host/Source/PromptPanel.cpp: In member function ‘void PromptPanel::updateAutoFamilyLabel()’:
/home/losera/PluginForge/host/Source/PromptPanel.cpp:464:17: warning: possibly dangling reference to a temporary [-Wdangling-reference]
  464 |     const auto& autoResolved = GenerationProfiles::resolveAuto(promptInput.getText(), PF_IS_SYNTH != 0);
      |                 ^~~~~~~~~~~~
/home/losera/PluginForge/host/Source/PromptPanel.cpp:464:83: note: ‘juce::String’ temporary created here
  464 |     const auto& autoResolved = GenerationProfiles::resolveAuto(promptInput.getText(), PF_IS_SYNTH != 0);
      |                                                                ~~~~~~~~~~~~~~~~~~~^~
/home/losera/PluginForge/host/Source/PromptPanel.cpp: In member function ‘void PromptPanel::runGeneration(const juce::String&, juce::uint64, PluginForgeProcessor::LoadMode, const juce::String&, const juce::String&, const juce::String&)’:
/home/losera/PluginForge/host/Source/PromptPanel.cpp:728:47: warning: conversion to ‘int’ from ‘juce::uint32’ {aka ‘unsigned int’} may change the sign of the result [-Wsign-conversion]
  728 |         const int exitCode = child.getExitCode();
      |                              ~~~~~~~~~~~~~~~~~^~
[9/26] Building CXX object CMakeFiles/Pl...orgeSynth.dir/Source/KeyboardPanel.cpp.o
In file included from /home/losera/JUCE/modules/juce_audio_processors/juce_audio_processors.h:145,
                 from /home/losera/JUCE/modules/juce_audio_utils/juce_audio_utils.h:60,
                 from /home/losera/PluginForge/host/Source/KeyboardPanel.h:2,
                 from /home/losera/PluginForge/host/Source/KeyboardPanel.cpp:1:
/home/losera/JUCE/modules/juce_audio_processors/processors/juce_AudioProcessor.h:280:18: warning: ‘virtual void juce::AudioProcessor::processBlock(juce::AudioBuffer<double>&, juce::MidiBuffer&)’ was hidden [-Woverloaded-virtual=]
  280 |     virtual void processBlock (AudioBuffer<double>& buffer,
      |                  ^~~~~~~~~~~~
In file included from /home/losera/PluginForge/host/Source/KeyboardPanel.h:3:
/home/losera/PluginForge/host/Source/PluginProcessor.h:42:10: note:   by ‘virtual void PluginForgeProcessor::processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&)’
   42 |     void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
      |          ^~~~~~~~~~~~
[10/26] Building CXX object CMakeFiles/P...geSynth.dir/Source/CodeEditorPanel.cpp.o
In file included from /home/losera/JUCE/modules/juce_audio_processors/juce_audio_processors.h:145,
                 from /home/losera/PluginForge/host/Source/CodeEditorPanel.h:2,
                 from /home/losera/PluginForge/host/Source/CodeEditorPanel.cpp:1:
/home/losera/JUCE/modules/juce_audio_processors/processors/juce_AudioProcessor.h:280:18: warning: ‘virtual void juce::AudioProcessor::processBlock(juce::AudioBuffer<double>&, juce::MidiBuffer&)’ was hidden [-Woverloaded-virtual=]
  280 |     virtual void processBlock (AudioBuffer<double>& buffer,
      |                  ^~~~~~~~~~~~
In file included from /home/losera/PluginForge/host/Source/CodeEditorPanel.h:4:
/home/losera/PluginForge/host/Source/PluginProcessor.h:42:10: note:   by ‘virtual void PluginForgeProcessor::processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&)’
   42 |     void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
      |          ^~~~~~~~~~~~
[11/26] Building CXX object CMakeFiles/P...rgeHost.dir/Source/CodeEditorPanel.cpp.o
In file included from /home/losera/JUCE/modules/juce_audio_processors/juce_audio_processors.h:145,
                 from /home/losera/PluginForge/host/Source/CodeEditorPanel.h:2,
                 from /home/losera/PluginForge/host/Source/CodeEditorPanel.cpp:1:
/home/losera/JUCE/modules/juce_audio_processors/processors/juce_AudioProcessor.h:280:18: warning: ‘virtual void juce::AudioProcessor::processBlock(juce::AudioBuffer<double>&, juce::MidiBuffer&)’ was hidden [-Woverloaded-virtual=]
  280 |     virtual void processBlock (AudioBuffer<double>& buffer,
      |                  ^~~~~~~~~~~~
In file included from /home/losera/PluginForge/host/Source/CodeEditorPanel.h:4:
/home/losera/PluginForge/host/Source/PluginProcessor.h:42:10: note:   by ‘virtual void PluginForgeProcessor::processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&)’
   42 |     void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
      |          ^~~~~~~~~~~~
[12/26] Building CXX object CMakeFiles/PluginForgeHost.dir/Source/FaustEngine.cpp.o
/home/losera/PluginForge/host/Source/FaustEngine.cpp: In member function ‘FaustEngine::ParamInfo ParamCapture::consume(const char*, float*, float, float, float, float, FaustEngine::Kind)’:
/home/losera/PluginForge/host/Source/FaustEngine.cpp:135:88: warning: missing initializer for member ‘FaustEngine::ParamInfo::unit’ [-Wmissing-field-initializers]
  135 |         FaustEngine::ParamInfo info { label ? label : "", init, fmin, fmax, step, kind };
      |                                                                                        ^
/home/losera/PluginForge/host/Source/FaustEngine.cpp:135:88: warning: missing initializer for member ‘FaustEngine::ParamInfo::style’ [-Wmissing-field-initializers]
/home/losera/PluginForge/host/Source/FaustEngine.cpp:135:88: warning: missing initializer for member ‘FaustEngine::ParamInfo::group’ [-Wmissing-field-initializers]
/home/losera/PluginForge/host/Source/FaustEngine.cpp:135:88: warning: missing initializer for member ‘FaustEngine::ParamInfo::id’ [-Wmissing-field-initializers]
/home/losera/PluginForge/host/Source/FaustEngine.cpp: In member function ‘void FaustEngine::prepare(double, int)’:
/home/losera/PluginForge/host/Source/FaustEngine.cpp:613:42: warning: comparing floating-point with ‘==’ or ‘!=’ is unsafe [-Wfloat-equal]
  613 |     const bool rateChanged = (sampleRate != sr);
      |                               ~~~~~~~~~~~^~~~~
/home/losera/PluginForge/host/Source/FaustEngine.cpp: In member function ‘void FaustEngine::runCompile(const std::string&, const CompileCallback&)’:
/home/losera/PluginForge/host/Source/FaustEngine.cpp:864:41: warning: declaration of ‘lock’ shadows a previous local [-Wshadow]
  864 |             std::lock_guard<std::mutex> lock(jobMutex);
      |                                         ^~~~
/home/losera/PluginForge/host/Source/FaustEngine.cpp:829:37: note: shadowed declaration is here
  829 |         std::lock_guard<std::mutex> lock(compileMutex);
      |                                     ^~~~
[13/26] Building CXX object CMakeFiles/P...orgeHost.dir/Source/ParamGridPanel.cpp.o
In file included from /home/losera/JUCE/modules/juce_audio_processors/juce_audio_processors.h:145,
                 from /home/losera/PluginForge/host/Source/ParamGridPanel.h:2,
                 from /home/losera/PluginForge/host/Source/ParamGridPanel.cpp:1:
/home/losera/JUCE/modules/juce_audio_processors/processors/juce_AudioProcessor.h:280:18: warning: ‘virtual void juce::AudioProcessor::processBlock(juce::AudioBuffer<double>&, juce::MidiBuffer&)’ was hidden [-Woverloaded-virtual=]
  280 |     virtual void processBlock (AudioBuffer<double>& buffer,
      |                  ^~~~~~~~~~~~
In file included from /home/losera/PluginForge/host/Source/ParamGridPanel.h:3:
/home/losera/PluginForge/host/Source/PluginProcessor.h:42:10: note:   by ‘virtual void PluginForgeProcessor::processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&)’
   42 |     void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
      |          ^~~~~~~~~~~~
/home/losera/PluginForge/host/Source/ParamGridPanel.cpp: In member function ‘ParamGridPanel::WidgetKind ParamGridPanel::controlKindForTest(int) const’:
/home/losera/PluginForge/host/Source/ParamGridPanel.cpp:932:16: warning: enumeration value ‘LinearBar’ not handled in switch [-Wswitch-enum]
  932 |         switch (sl->getSliderStyle())
      |                ^
/home/losera/PluginForge/host/Source/ParamGridPanel.cpp:932:16: warning: enumeration value ‘LinearBarVertical’ not handled in switch [-Wswitch-enum]
/home/losera/PluginForge/host/Source/ParamGridPanel.cpp:932:16: warning: enumeration value ‘TwoValueHorizontal’ not handled in switch [-Wswitch-enum]
/home/losera/PluginForge/host/Source/ParamGridPanel.cpp:932:16: warning: enumeration value ‘TwoValueVertical’ not handled in switch [-Wswitch-enum]
/home/losera/PluginForge/host/Source/ParamGridPanel.cpp:932:16: warning: enumeration value ‘ThreeValueHorizontal’ not handled in switch [-Wswitch-enum]
/home/losera/PluginForge/host/Source/ParamGridPanel.cpp:932:16: warning: enumeration value ‘ThreeValueVertical’ not handled in switch [-Wswitch-enum]
[14/26] Building CXX object CMakeFiles/P...nForgeSynth.dir/Source/PromptPanel.cpp.o
In file included from /home/losera/JUCE/modules/juce_audio_processors/juce_audio_processors.h:145,
                 from /home/losera/PluginForge/host/Source/PromptPanel.h:2,
                 from /home/losera/PluginForge/host/Source/PromptPanel.cpp:1:
/home/losera/JUCE/modules/juce_audio_processors/processors/juce_AudioProcessor.h:280:18: warning: ‘virtual void juce::AudioProcessor::processBlock(juce::AudioBuffer<double>&, juce::MidiBuffer&)’ was hidden [-Woverloaded-virtual=]
  280 |     virtual void processBlock (AudioBuffer<double>& buffer,
      |                  ^~~~~~~~~~~~
In file included from /home/losera/PluginForge/host/Source/PromptPanel.h:3:
/home/losera/PluginForge/host/Source/PluginProcessor.h:42:10: note:   by ‘virtual void PluginForgeProcessor::processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&)’
   42 |     void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
      |          ^~~~~~~~~~~~
/home/losera/PluginForge/host/Source/PromptPanel.cpp: In member function ‘void PromptPanel::updateAutoFamilyLabel()’:
/home/losera/PluginForge/host/Source/PromptPanel.cpp:464:17: warning: possibly dangling reference to a temporary [-Wdangling-reference]
  464 |     const auto& autoResolved = GenerationProfiles::resolveAuto(promptInput.getText(), PF_IS_SYNTH != 0);
      |                 ^~~~~~~~~~~~
/home/losera/PluginForge/host/Source/PromptPanel.cpp:464:83: note: ‘juce::String’ temporary created here
  464 |     const auto& autoResolved = GenerationProfiles::resolveAuto(promptInput.getText(), PF_IS_SYNTH != 0);
      |                                                                ~~~~~~~~~~~~~~~~~~~^~
/home/losera/PluginForge/host/Source/PromptPanel.cpp: In member function ‘void PromptPanel::runGeneration(const juce::String&, juce::uint64, PluginForgeProcessor::LoadMode, const juce::String&, const juce::String&, const juce::String&)’:
/home/losera/PluginForge/host/Source/PromptPanel.cpp:728:47: warning: conversion to ‘int’ from ‘juce::uint32’ {aka ‘unsigned int’} may change the sign of the result [-Wsign-conversion]
  728 |         const int exitCode = child.getExitCode();
      |                              ~~~~~~~~~~~~~~~~~^~
[15/26] Building CXX object CMakeFiles/P...ForgeSynth.dir/Source/PluginEditor.cpp.o
In file included from /home/losera/JUCE/modules/juce_audio_processors/juce_audio_processors.h:145,
                 from /home/losera/PluginForge/host/Source/PluginEditor.h:2,
                 from /home/losera/PluginForge/host/Source/PluginEditor.cpp:1:
/home/losera/JUCE/modules/juce_audio_processors/processors/juce_AudioProcessor.h:280:18: warning: ‘virtual void juce::AudioProcessor::processBlock(juce::AudioBuffer<double>&, juce::MidiBuffer&)’ was hidden [-Woverloaded-virtual=]
  280 |     virtual void processBlock (AudioBuffer<double>& buffer,
      |                  ^~~~~~~~~~~~
In file included from /home/losera/PluginForge/host/Source/PluginEditor.h:3:
/home/losera/PluginForge/host/Source/PluginProcessor.h:42:10: note:   by ‘virtual void PluginForgeProcessor::processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&)’
   42 |     void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
      |          ^~~~~~~~~~~~
/home/losera/PluginForge/host/Source/PluginEditor.cpp: In lambda function:
/home/losera/PluginForge/host/Source/PluginEditor.cpp:193:30: warning: declaration of ‘const auto& p’ shadows a parameter [-Wshadow]
  193 |             for (const auto& p : params)
      |                              ^
/home/losera/PluginForge/host/Source/PluginEditor.cpp:46:60: note: shadowed declaration is here
   46 | PluginForgeEditor::PluginForgeEditor(PluginForgeProcessor& p)
      |                                      ~~~~~~~~~~~~~~~~~~~~~~^
[16/26] Building CXX object CMakeFiles/P...geSynth.dir/Source/PluginProcessor.cpp.o
In file included from /home/losera/JUCE/modules/juce_audio_processors/juce_audio_processors.h:145,
                 from /home/losera/PluginForge/host/Source/PluginProcessor.h:2,
                 from /home/losera/PluginForge/host/Source/PluginProcessor.cpp:1:
/home/losera/JUCE/modules/juce_audio_processors/processors/juce_AudioProcessor.h:280:18: warning: ‘virtual void juce::AudioProcessor::processBlock(juce::AudioBuffer<double>&, juce::MidiBuffer&)’ was hidden [-Woverloaded-virtual=]
  280 |     virtual void processBlock (AudioBuffer<double>& buffer,
      |                  ^~~~~~~~~~~~
/home/losera/PluginForge/host/Source/PluginProcessor.h:42:10: note:   by ‘virtual void PluginForgeProcessor::processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&)’
   42 |     void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
      |          ^~~~~~~~~~~~
[17/26] Building CXX object CMakeFiles/P...nForgeHost.dir/Source/PluginEditor.cpp.o
In file included from /home/losera/JUCE/modules/juce_audio_processors/juce_audio_processors.h:145,
                 from /home/losera/PluginForge/host/Source/PluginEditor.h:2,
                 from /home/losera/PluginForge/host/Source/PluginEditor.cpp:1:
/home/losera/JUCE/modules/juce_audio_processors/processors/juce_AudioProcessor.h:280:18: warning: ‘virtual void juce::AudioProcessor::processBlock(juce::AudioBuffer<double>&, juce::MidiBuffer&)’ was hidden [-Woverloaded-virtual=]
  280 |     virtual void processBlock (AudioBuffer<double>& buffer,
      |                  ^~~~~~~~~~~~
In file included from /home/losera/PluginForge/host/Source/PluginEditor.h:3:
/home/losera/PluginForge/host/Source/PluginProcessor.h:42:10: note:   by ‘virtual void PluginForgeProcessor::processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&)’
   42 |     void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
      |          ^~~~~~~~~~~~
/home/losera/PluginForge/host/Source/PluginEditor.cpp: In lambda function:
/home/losera/PluginForge/host/Source/PluginEditor.cpp:193:30: warning: declaration of ‘const auto& p’ shadows a parameter [-Wshadow]
  193 |             for (const auto& p : params)
      |                              ^
/home/losera/PluginForge/host/Source/PluginEditor.cpp:46:60: note: shadowed declaration is here
   46 | PluginForgeEditor::PluginForgeEditor(PluginForgeProcessor& p)
      |                                      ~~~~~~~~~~~~~~~~~~~~~~^
[18/26] Building CXX object CMakeFiles/P...rgeSynth.dir/Source/ParamGridPanel.cpp.o
In file included from /home/losera/JUCE/modules/juce_audio_processors/juce_audio_processors.h:145,
                 from /home/losera/PluginForge/host/Source/ParamGridPanel.h:2,
                 from /home/losera/PluginForge/host/Source/ParamGridPanel.cpp:1:
/home/losera/JUCE/modules/juce_audio_processors/processors/juce_AudioProcessor.h:280:18: warning: ‘virtual void juce::AudioProcessor::processBlock(juce::AudioBuffer<double>&, juce::MidiBuffer&)’ was hidden [-Woverloaded-virtual=]
  280 |     virtual void processBlock (AudioBuffer<double>& buffer,
      |                  ^~~~~~~~~~~~
In file included from /home/losera/PluginForge/host/Source/ParamGridPanel.h:3:
/home/losera/PluginForge/host/Source/PluginProcessor.h:42:10: note:   by ‘virtual void PluginForgeProcessor::processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&)’
   42 |     void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
      |          ^~~~~~~~~~~~
/home/losera/PluginForge/host/Source/ParamGridPanel.cpp: In member function ‘ParamGridPanel::WidgetKind ParamGridPanel::controlKindForTest(int) const’:
/home/losera/PluginForge/host/Source/ParamGridPanel.cpp:932:16: warning: enumeration value ‘LinearBar’ not handled in switch [-Wswitch-enum]
  932 |         switch (sl->getSliderStyle())
      |                ^
/home/losera/PluginForge/host/Source/ParamGridPanel.cpp:932:16: warning: enumeration value ‘LinearBarVertical’ not handled in switch [-Wswitch-enum]
/home/losera/PluginForge/host/Source/ParamGridPanel.cpp:932:16: warning: enumeration value ‘TwoValueHorizontal’ not handled in switch [-Wswitch-enum]
/home/losera/PluginForge/host/Source/ParamGridPanel.cpp:932:16: warning: enumeration value ‘TwoValueVertical’ not handled in switch [-Wswitch-enum]
/home/losera/PluginForge/host/Source/ParamGridPanel.cpp:932:16: warning: enumeration value ‘ThreeValueHorizontal’ not handled in switch [-Wswitch-enum]
/home/losera/PluginForge/host/Source/ParamGridPanel.cpp:932:16: warning: enumeration value ‘ThreeValueVertical’ not handled in switch [-Wswitch-enum]
[19/26] Building CXX object CMakeFiles/P...rgeHost.dir/Source/PluginProcessor.cpp.o
In file included from /home/losera/JUCE/modules/juce_audio_processors/juce_audio_processors.h:145,
                 from /home/losera/PluginForge/host/Source/PluginProcessor.h:2,
                 from /home/losera/PluginForge/host/Source/PluginProcessor.cpp:1:
/home/losera/JUCE/modules/juce_audio_processors/processors/juce_AudioProcessor.h:280:18: warning: ‘virtual void juce::AudioProcessor::processBlock(juce::AudioBuffer<double>&, juce::MidiBuffer&)’ was hidden [-Woverloaded-virtual=]
  280 |     virtual void processBlock (AudioBuffer<double>& buffer,
      |                  ^~~~~~~~~~~~
/home/losera/PluginForge/host/Source/PluginProcessor.h:42:10: note:   by ‘virtual void PluginForgeProcessor::processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&)’
   42 |     void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
      |          ^~~~~~~~~~~~
[22/26] Linking CXX shared module "Plugi...tents/x86_64-linux/PluginForge Synth.so"
JUCE v7.0.9
-- Destination /home/losera/.vst3/PluginForge Synth.vst3 exists, overwriting
-- Installing: /home/losera/.vst3/PluginForge Synth.vst3
-- Installing: /home/losera/.vst3/PluginForge Synth.vst3/Contents
-- Installing: /home/losera/.vst3/PluginForge Synth.vst3/Contents/x86_64-linux
-- Installing: /home/losera/.vst3/PluginForge Synth.vst3/Contents/x86_64-linux/PluginForge Synth.so
-- Installing: /home/losera/.vst3/PluginForge Synth.vst3/Contents/Resources
-- Installing: /home/losera/.vst3/PluginForge Synth.vst3/Contents/Resources/moduleinfo.json
[25/26] Linking CXX shared module "Plugi...ntents/x86_64-linux/PluginForge Host.so"
JUCE v7.0.9
-- Destination /home/losera/.vst3/PluginForge Host.vst3 exists, overwriting
-- Installing: /home/losera/.vst3/PluginForge Host.vst3
-- Installing: /home/losera/.vst3/PluginForge Host.vst3/Contents
-- Installing: /home/losera/.vst3/PluginForge Host.vst3/Contents/x86_64-linux
-- Installing: /home/losera/.vst3/PluginForge Host.vst3/Contents/x86_64-linux/PluginForge Host.so
-- Installing: /home/losera/.vst3/PluginForge Host.vst3/Contents/Resources
-- Installing: /home/losera/.vst3/PluginForge Host.vst3/Contents/Resources/moduleinfo.json

PluginForge main ? ❯ pluginval --strictness-level 5 --validate "$HOME/.vst3/PluginForge Host.vst3"  2>&1 | tee ~/pv_host_$(date +%s).log
pluginval --strictness-level 5 --validate "$HOME/.vst3/PluginForge Synth.vst3" 2>&1 | tee ~/pv_synth_$(date +%s).log

Started validating: /home/losera/.vst3/PluginForge Host.vst3
Random seed: 0x2bbfb3f
Validation started
Strictness level: 5
-----------------------------------------------------------------
Starting tests in: pluginval / Scan for plugins located in: /home/losera/.vst3/PluginForge Host.vst3...
Num plugins found: 1

Testing plugin: VST3-PluginForge Host-3ddbac07-5b34fef6
yourcompany: PluginForge Host v0.1.0
Completed tests in pluginval / Scan for plugins located in: /home/losera/.vst3/PluginForge Host.vst3
-----------------------------------------------------------------
Starting tests in: pluginval / Open plugin (cold)...
JUCE v7.0.9
Completed tests in pluginval / Open plugin (cold)
-----------------------------------------------------------------
Starting tests in: pluginval / Open plugin (warm)...
Running tests 1 times
Completed tests in pluginval / Open plugin (warm)
-----------------------------------------------------------------
Starting tests in: pluginval / Plugin info...

Plugin name: PluginForge Host
Alternative names: PluginForge Host
SupportsDoublePrecision: no
Reported latency: 0
Reported taillength: 0
Completed tests in pluginval / Plugin info
-----------------------------------------------------------------
Starting tests in: pluginval / Plugin programs...
Num programs: 0
All program names checked
Completed tests in pluginval / Plugin programs
-----------------------------------------------------------------
Starting tests in: pluginval / Editor...
Completed tests in pluginval / Editor
-----------------------------------------------------------------
Starting tests in: pluginval / Open editor whilst processing...
Completed tests in pluginval / Open editor whilst processing
-----------------------------------------------------------------
Starting tests in: pluginval / Audio processing...
Testing with sample rate [44100] and block size [64]
Testing with sample rate [44100] and block size [128]
Testing with sample rate [44100] and block size [256]
Testing with sample rate [44100] and block size [512]
Testing with sample rate [44100] and block size [1024]
Testing with sample rate [48000] and block size [64]
Testing with sample rate [48000] and block size [128]
Testing with sample rate [48000] and block size [256]
Testing with sample rate [48000] and block size [512]
Testing with sample rate [48000] and block size [1024]
Testing with sample rate [96000] and block size [64]
Testing with sample rate [96000] and block size [128]
Testing with sample rate [96000] and block size [256]
Testing with sample rate [96000] and block size [512]
Testing with sample rate [96000] and block size [1024]
Completed tests in pluginval / Audio processing
-----------------------------------------------------------------
Starting tests in: pluginval / Plugin state...
Completed tests in pluginval / Plugin state
-----------------------------------------------------------------
Starting tests in: pluginval / Automation...
Testing with sample rate [44100] and block size [64] and sub-block size [32]
Testing with sample rate [44100] and block size [128] and sub-block size [32]
Testing with sample rate [44100] and block size [256] and sub-block size [32]
Testing with sample rate [44100] and block size [512] and sub-block size [32]
Testing with sample rate [44100] and block size [1024] and sub-block size [32]
Testing with sample rate [48000] and block size [64] and sub-block size [32]
Testing with sample rate [48000] and block size [128] and sub-block size [32]
Testing with sample rate [48000] and block size [256] and sub-block size [32]
Testing with sample rate [48000] and block size [512] and sub-block size [32]
Testing with sample rate [48000] and block size [1024] and sub-block size [32]
Testing with sample rate [96000] and block size [64] and sub-block size [32]
Testing with sample rate [96000] and block size [128] and sub-block size [32]
Testing with sample rate [96000] and block size [256] and sub-block size [32]
Testing with sample rate [96000] and block size [512] and sub-block size [32]
Testing with sample rate [96000] and block size [1024] and sub-block size [32]
Completed tests in pluginval / Automation
-----------------------------------------------------------------
Starting tests in: pluginval / Editor Automation...
Completed tests in pluginval / Editor Automation
-----------------------------------------------------------------
Starting tests in: pluginval / Automatable Parameters...
Completed tests in pluginval / Automatable Parameters
-----------------------------------------------------------------
Starting tests in: pluginval / auval...
Completed tests in pluginval / auval
-----------------------------------------------------------------
Starting tests in: pluginval / vst3 validator...
INFO: Skipping vst3 validator as validator path hasn't been set
Completed tests in pluginval / vst3 validator
-----------------------------------------------------------------
Starting tests in: pluginval / Basic bus...
Completed tests in pluginval / Basic bus
-----------------------------------------------------------------
Starting tests in: pluginval / Listing available buses...
Inputs:
	Named layouts: Mono, Stereo
	Discrete layouts: Discrete #1
Outputs:
	Named layouts: Mono, Stereo
	Discrete layouts: Discrete #1
Main bus num input channels: 2
Main bus num output channels: 2
Completed tests in pluginval / Listing available buses
-----------------------------------------------------------------
Starting tests in: pluginval / Enabling all buses...
Completed tests in pluginval / Enabling all buses
-----------------------------------------------------------------
Starting tests in: pluginval / Disabling non-main busses...
Completed tests in pluginval / Disabling non-main busses
-----------------------------------------------------------------
Starting tests in: pluginval / Restoring default layout...
Main bus num input channels: 2
Main bus num output channels: 2
Completed tests in pluginval / Restoring default layout
SUCCESS
Started validating: /home/losera/.vst3/PluginForge Synth.vst3
Random seed: 0x267ec14
Validation started
Strictness level: 5
-----------------------------------------------------------------
Starting tests in: pluginval / Scan for plugins located in: /home/losera/.vst3/PluginForge Synth.vst3...
Num plugins found: 1

Testing plugin: VST3-PluginForge Synth-1f0c0009-5b3509f6
yourcompany: PluginForge Synth v0.1.0
Completed tests in pluginval / Scan for plugins located in: /home/losera/.vst3/PluginForge Synth.vst3
-----------------------------------------------------------------
Starting tests in: pluginval / Open plugin (cold)...
JUCE v7.0.9
Completed tests in pluginval / Open plugin (cold)
-----------------------------------------------------------------
Starting tests in: pluginval / Open plugin (warm)...
Running tests 1 times
Completed tests in pluginval / Open plugin (warm)
-----------------------------------------------------------------
Starting tests in: pluginval / Plugin info...

Plugin name: PluginForge Synth
Alternative names: PluginForge Synth
SupportsDoublePrecision: no
Reported latency: 0
Reported taillength: 2
Completed tests in pluginval / Plugin info
-----------------------------------------------------------------
Starting tests in: pluginval / Plugin programs...
Num programs: 0
All program names checked
Completed tests in pluginval / Plugin programs
-----------------------------------------------------------------
Starting tests in: pluginval / Editor...
Completed tests in pluginval / Editor
-----------------------------------------------------------------
Starting tests in: pluginval / Open editor whilst processing...
Completed tests in pluginval / Open editor whilst processing
-----------------------------------------------------------------
Starting tests in: pluginval / Audio processing...
Testing with sample rate [44100] and block size [64]
Testing with sample rate [44100] and block size [128]
Testing with sample rate [44100] and block size [256]
Testing with sample rate [44100] and block size [512]
Testing with sample rate [44100] and block size [1024]
Testing with sample rate [48000] and block size [64]
Testing with sample rate [48000] and block size [128]
Testing with sample rate [48000] and block size [256]
Testing with sample rate [48000] and block size [512]
Testing with sample rate [48000] and block size [1024]
Testing with sample rate [96000] and block size [64]
Testing with sample rate [96000] and block size [128]
Testing with sample rate [96000] and block size [256]
Testing with sample rate [96000] and block size [512]
Testing with sample rate [96000] and block size [1024]
Completed tests in pluginval / Audio processing
-----------------------------------------------------------------
Starting tests in: pluginval / Plugin state...
Completed tests in pluginval / Plugin state
-----------------------------------------------------------------
Starting tests in: pluginval / Automation...
Testing with sample rate [44100] and block size [64] and sub-block size [32]
Testing with sample rate [44100] and block size [128] and sub-block size [32]
Testing with sample rate [44100] and block size [256] and sub-block size [32]
Testing with sample rate [44100] and block size [512] and sub-block size [32]
Testing with sample rate [44100] and block size [1024] and sub-block size [32]
Testing with sample rate [48000] and block size [64] and sub-block size [32]
Testing with sample rate [48000] and block size [128] and sub-block size [32]
Testing with sample rate [48000] and block size [256] and sub-block size [32]
Testing with sample rate [48000] and block size [512] and sub-block size [32]
Testing with sample rate [48000] and block size [1024] and sub-block size [32]
Testing with sample rate [96000] and block size [64] and sub-block size [32]
Testing with sample rate [96000] and block size [128] and sub-block size [32]
Testing with sample rate [96000] and block size [256] and sub-block size [32]
Testing with sample rate [96000] and block size [512] and sub-block size [32]
Testing with sample rate [96000] and block size [1024] and sub-block size [32]
Completed tests in pluginval / Automation
-----------------------------------------------------------------
Starting tests in: pluginval / Editor Automation...
Completed tests in pluginval / Editor Automation
-----------------------------------------------------------------
Starting tests in: pluginval / Automatable Parameters...
Completed tests in pluginval / Automatable Parameters
-----------------------------------------------------------------
Starting tests in: pluginval / auval...
Completed tests in pluginval / auval
-----------------------------------------------------------------
Starting tests in: pluginval / vst3 validator...
INFO: Skipping vst3 validator as validator path hasn't been set
Completed tests in pluginval / vst3 validator
-----------------------------------------------------------------
Starting tests in: pluginval / Basic bus...
Completed tests in pluginval / Basic bus
-----------------------------------------------------------------
Starting tests in: pluginval / Listing available buses...
Inputs:
	Named layouts: Mono, Stereo
	Discrete layouts: Discrete #1
Outputs:
	Named layouts: Mono, Stereo
	Discrete layouts: Discrete #1
Main bus num input channels: 0
Main bus num output channels: 2
Completed tests in pluginval / Listing available buses
-----------------------------------------------------------------
Starting tests in: pluginval / Enabling all buses...
Completed tests in pluginval / Enabling all buses
-----------------------------------------------------------------
Starting tests in: pluginval / Disabling non-main busses...
Completed tests in pluginval / Disabling non-main busses
-----------------------------------------------------------------
Starting tests in: pluginval / Restoring default layout...
Main bus num input channels: 0
Main bus num output channels: 2
Completed tests in pluginval / Restoring default layout
SUCCESS
```
