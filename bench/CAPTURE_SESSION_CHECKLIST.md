# Interactive capture session — one-page checklist

For the next human-at-a-DAW hour. Session 017 (2026-08-28) found four medium/low
defects — **PF-072** (refine collapsed to a single dry/wet knob), **PF-073**
(audible click on the 2nd-generation DSP swap), **PF-074** (a generated
instrument emitted NaN/Inf during play), **PF-075** (a chord sounded like ~5
voices from a strictly-mono engine) — and every one of them is currently
unactionable because nobody wrote down which generation produced it
(`docs/BUGS.md` PF-072/PF-074, `docs/phases/phase-3.md`). This session has a
capture harness (`bench/capture.py`) now; use it so this hour leaves a folder
of exact repros instead of another anecdote.

**This does not replace the standing listening pass (COLLABORATION.md §1) —
if the two compete for the hour, the listening pass wins.** Capture is meant
to make repros of what you already hear, not to add its own agenda.

## Before you start

1. `export PLUGINFORGE_CAPTURE=1` in the shell that launches the Standalone
   (or the DAW, if it inherits the plugin's environment) — capture is off by
   default. **This adds a few seconds to every generation** (an offline
   `faust2sndfile` compile + short render happens before the response comes
   back), so expect turnaround to feel slower than a normal session. That is
   the cost of the repro, not a bug.
2. Confirm it's live: generate anything once, then check
   `bench/captures/<today>/001-*.json` exists. If it doesn't, capture silently
   failed open — check stderr for a `[capture] not recorded: ...` line before
   going further.
3. Note the date folder now (`bench/captures/2026-09-18/` etc.) — every
   capture ID below is relative to it.

## During the session

Work the four defects in order. For each: **generate, do the risky action,
listen, and if something's off, write down the capture ID you see printed
to stderr (`[capture] bench/captures/.../NNN-....json`).** The number is
free — noting it costs one second and is the entire point of this checklist.

| # | Do this | Listen / look for (PF-###) |
|---|---------|------------------------------|
| 1 | Generate a full effect fresh (e.g. "a warm analog reverb with chorus") | Baseline — note the capture ID, this is what "New" mode gives you |
| 2 | **Refine** the same patch (Add mode) with a related ask ("add a slow LFO to the chorus") | **PF-072** — did the refined patch keep the full control set, or collapse to ~1 knob? Compare `param_count` between the two capture JSONs |
| 3 | Generate a second, different effect right after #1/#2, while it's still playing | **PF-073** — is the transition between the old and new DSP audibly clean, or is there a click/pop? (The capture record won't show this — it's about the *live* swap, which this harness deliberately does not tap; just note the ID of the generation that swapped in) |
| 4 | Generate an instrument (e.g. "a plucked string synth") and play it for 30+ seconds, moving a few knobs while it plays | **PF-074** — any NaN/Inf, dropout, or stuck-silent behaviour? The capture's offline probe (`audio_tail.measurement`) is a *different* signal than what you played — it won't catch a knob-triggered NaN, so your ears are still the instrument here. Note the ID regardless |
| 5 | Generate a polyphonic-sounding instrument and hold a 5-note chord | **PF-075** — do all 5 notes sustain, or do 4 decay leaving 1 (confirms strictly-mono + release-tail overlap) or something else entirely? |

## After the session

- `ls bench/captures/<today>/` — every capture from the session is there:
  `NNN-<label>.json` (prompt, refine_mode, provider/model, accepted Faust
  source, declared param list, offline probe measurement) and `NNN-<label>.wav`
  for effects (skipped for zero-input instruments — see the JSON's
  `audio_tail.reason`).
- For each capture ID you wrote down: open the `.json`, and if `param_count`,
  `faust_source`, or `audio_tail` don't already tell the story, that's the
  patch to re-open with `faust2sndfile` or `host/tests/OfflineRenderTest` by
  hand.
- File or update the PF-07x entry in `docs/BUGS.md` with the capture path —
  `bench/captures/<date>/NNN-....json` — instead of a prose description of
  what you remember hearing.
- `bench/captures/` is untracked-by-default working data, not something to
  commit wholesale — pull out just the one or two captures that matter into
  the bug report / a fixplan doc if they need to travel with a PR.

## What this harness does *not* cover

- **No live audio-thread tap.** The `.wav` you get is an offline re-render of
  the *accepted Faust source* through a short noise probe
  (`bench/render_oracle.py`), not a recording of what actually played. It
  cannot see a fault that only appears after knobs move during play (exactly
  PF-074's and PF-075's failure mode) — that's still your ears' job.
- **No param snapshot from the live host.** `params` in the JSON is the
  *declared* UI metadata (label/init/min/max/step) read back from compiling
  the accepted source, not the plugin's live parameter values at the moment
  something went wrong.
