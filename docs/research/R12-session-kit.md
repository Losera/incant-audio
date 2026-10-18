# R12 — Human listening-session kit (prepared 2026-10-14)

Point-in-time record, not maintained after today (COLLABORATION.md §8). Prepared so a
human (Juan) can spend a REAPER hour almost entirely on the one judgment no tool in this
repo can make: **does a generated plugin sound like what was asked for** (COLLABORATION.md
§1, CLAUDE.md). Everything in this document is either read from the repo or the output of
a command actually run today — no LLM calls were made while preparing it, and none were
needed: every candidate below is reused from a committed archive, not freshly generated.

**Scope note, stated up front.** This kit prepares the session; it does not run it and
does not make the sounds-right-or-wrong call. That stays Juan's, per COLLABORATION.md §1.

---

## 0. Two corrections to this kit's own brief

Worth stating plainly rather than silently working around, since a wrong premise here
would have picked the wrong candidates.

1. **`examples/*.dsp` holds 4 files, not 30.** `chorus.dsp`, `compressor.dsp`, `gain.dsp`,
   `lowpass.dsp` — hand-written reference patches (CLAUDE.md's file map: "reference Faust
   patches"), with no prompt or generation provenance attached. They are not LLM output,
   so there is no "does it match the prompt" question to ask of them — they served here
   only as a sanity check that the oracle itself works (all 4 render clean, `codes: []`).
   The actual population of 30-ish prompts with real generation provenance lives in
   `bench/results/efficacy/*.json` (125-cell grids, 5 categories × 5 tiers × multiple
   providers) and in `bench/prompts/p6_battery.json` (14 hand-curated prompts, see below).
2. **`artifacts/p6_20260730/` is a `--dry-run` fixture, not real model output.** Its
   `scorecard.md` looks exactly like what this task asked for, which is why it's worth
   flagging precisely: `bench/p6_capture.py --dry-run` ships **canned placeholder Faust**
   (e.g. prompt #4 "make it sound underwater" literally produced `process = _*g, _*g`
   gated by a checkbox) to test the *harness*, not the model. It is not reused as a
   candidate source for that reason. Its **format** — one WAV + one scorecard row per
   prompt, objective gate pre-filled, listening column blank — and its companion script
   (`bench/p6_capture.py`) and checklist (`bench/CAPTURE_SESSION_CHECKLIST.md`) *are* reused
   below, because they are exactly the right shape and nothing about them needed reinventing.

---

## 1. Candidate patches — 8 passed the $0 pre-screen

Source: `bench/results/efficacy/efficacy_groq_20260831.json` — the 125-cell grid on
`groq`/`openai/gpt-oss-120b`, **the shipping-tier model** (STATUS.md), generated
2026-08-31→09-07, committed on `task/efficacy-groq-125`. Picked for spread: one trivial
control, two filters, two time-based effects, two dynamics cells (the documented weak
category), and one instrument (`generative`, Incant's synth target). Every row below was
independently re-rendered today with `bench/render_oracle.py` against the exact `code`
field pulled from that archive — not assumed from the archive's own `first_try_compiles`
flag, which is precisely the number PF-079 shows cannot be trusted alone.

| # | effect_id/tier | category | prompt (verbatim — do not reword) | oracle verdict | note for the listener |
|---|---|---|---|---|---|
| 1 | trivial-01/L4 | trivial | "A stereo gain utility: single gain parameter in decibels, range -60 to +12 dB, default 0 dB, 0.1 dB steps, converted dB-to-linear and applied equally to both channels." | PASS — peak 0.47, dc 0.0002, rms_ratio **-0.07 dB** (correctly ≈unity at 0 dB default) | Run this first. If it isn't transparent, the session is broken, not the model. |
| 2 | filters-01/L4 | filters | "A stereo resonant low-pass filter: 24 dB/oct ladder-style response, cutoff 20 Hz-20 kHz with exponential taper defaulting to 1 kHz, resonance Q 0.5-10, and gentle saturation for analog character." | PASS — peak 0.24, centroid shift **-3.88 oct**, 8-20kHz band **-62 dB** | Should sound obviously darker/rounder than dry, with a resonant peak near 1 kHz. |
| 3 | filters-05/L3 | filters | "a high-shelf filter for adding air to vocals" | PASS — 8-20kHz band **+4.4 dB**, centroid shift **+0.30 oct** | Should sound brighter/airier, not harsh — a lift, not a cut. |
| 4 | time-based-01/L4 | time-based | "A stereo delay: delay time 1-2000 ms defaulting to 350 ms, feedback 0-95%, dry/wet mix 0-100%, using an interpolated delay line for click-free time changes." | PASS — has a real tail (`env_db_final` -29.6 dB, decays, not a drone) | Discrete repeats at ~350 ms, fading — not a wash. |
| 5 | time-based-03/L3 | time-based | "a subtle chorus effect with rate and depth" | PASS — rms_ratio -2.6 dB, no runaway | Should be a gentle widening/shimmer, "subtle" is the claim to check. |
| 6 | dynamics-01/L1 | dynamics | "glue my mix so it feels dense and controlled, like a firm hand keeping every sound in its place" | PASS (clean) | **Worth extra attention**: dynamics is the weakest category at every tier and L1 is the weakest tier (STATUS.md: dynamics 20-60% first-try; L1 overall 48%) — this specific draw cleared the $0 gate anyway. Listen for whether it actually glues (reduces dynamic range) or just changes level. |
| 7 | dynamics-01/L0 | dynamics | "that punchy 'everything glued together' drum sound on Dr. Dre records" | PASS, but **flagged** — peak **12.6**, rms **+29.0 dB** vs input. Under the oracle's own thresholds (runaway ≥40 dB, peak ≥100, DC ≥0.05 — see `bench/render_oracle.py:450-463`) so it does not fail the gate, but a +29 dB gain is unusual for a 6-stage chain (HP → low-shelf → high-shelf → saturation → compressor → reverb). | Pay attention to whether this is "loud by design" (a mastering-style chain) or an accumulating-gain artifact the gate's threshold happens not to catch. If it's harsh/uncontrolled, that's real signal even though the oracle passed it. |
| 8 | generative-02/L3 | generative (**instrument**) | "a sawtooth synth with an ADSR envelope" | **Not screenable by the oracle** — zero-input patch (`faust2sndfile` needs an input file; the oracle's own docstring: "Karplus-Strong and every other generator... raise `UnsupportedPatch`"). Archive shows `first_try_compiles: true`. | This is the one candidate the $0 tools cannot pre-vet at all — compile success is all that's known going in. Play it with a held note; listen for the ADSR actually shaping amplitude (attack/decay/sustain/release distinguishable) rather than a static tone. |

Full source Faust for all 11 patches probed today (8 kept + 3 screened out below) is in
`bench/results/oracle_20261014_session_kit.json` (same `{prompt, category, analysis}`
shape as the precedent `bench/results/oracle_20260725.json`), alongside each one's complete
oracle JSON — not just the summary above.

---

## 2. Screened OUT — and why (so the pre-screen's own work is visible)

| effect_id/tier | prompt | what the oracle found | disposition |
|---|---|---|---|
| dynamics-01/L3 | "a compressor with threshold, ratio, attack, and release" | Archive says `first_try_compiles: true, terminal_reason: compiled` — the compile gate calls this a clean success. Independently re-rendered today: **completely silent** (rms 0.0, `codes: ["silent"]`). Code: `process = co.compressor_stereo(ratio, thresh, att, rel, _, _);` — the compressor's trailing `_, _` arguments are the same idiom STATUS.md's defect #1 names as a *hard arity compile error* elsewhere in this same archive (dynamics-01 L4 and L2, below). Here the identical idiom type-checks and renders silent instead of failing to compile. | **Excluded.** This is a live instance of exactly what PF-079 is about (compile gate overstates success) and, as far as this kit's research found, not yet a line item in `docs/BUGS.md` — the existing PF-079 entry names only the limiter (DC offset) and expander (runaway gain) dynamics failures, not a silent base compressor. Flagged here for Juan to decide whether it's worth its own entry; this kit does not file one. |
| dynamics-01/L4 | "A stereo feed-forward compressor: threshold -60 to 0 dB..." | Archive already records `first_try_compiles: false, retry_success: false, attempts: 3, terminal_reason: compile_failed`. Re-running the oracle on the retained last-attempt code reproduces a `faust -json` arity error, consistent with the archive — **not a new finding**, just confirmed. | Excluded (already known-broken). |
| dynamics-01/L2 | "something that evens out my track's volume..." | Same as above: archive already says `compile_failed` after 3 attempts; oracle reproduces a syntax/arity error on the retained code. **Not a new finding.** | Excluded (already known-broken). |
| generative-05 (all tiers) | Karplus-Strong plucked string | **Not independently re-tested this session** — already documented in `docs/BUGS.md` PF-024: fails to compile across every committed archive, and the one tier that does compile renders **+79.6 dB runaway**. Zero-input, so the oracle could not have screened it live here anyway. | Excluded a priori, on existing record. |

Net: **8 of 12 candidates considered made the final listening list** (8 passed, 1 is
oracle-unscreenable but compiles, 3 excluded — 1 newly-found silent render, 2 already-known
compile failures).

---

## 3. Exact commands run (reproduce or extend this pre-screen yourself)

All $0 — no provider calls, no network. `faust2sndfile` is the only external dependency
(already required by this project).

```bash
cd /home/losera/PluginForge

# Pull one candidate's code out of a committed archive and render+measure it:
python3 - <<'PY'
import json
d = json.load(open('bench/results/efficacy/efficacy_groq_20260831.json'))
for r in d:
    if r['effect_id'] == 'filters-01' and r['tier'] == 'L4':
        open('/tmp/candidate.dsp', 'w').write(r['code'])
PY
python3 bench/render_oracle.py /tmp/candidate.dsp --json
```

Ran individually (not via `--corpus`) for all 12 patches in §1/§2 — a full `--corpus` pass
over the 114-compiled-cell archive timed out past 180s in this session (each render is a
real `faust2sndfile` compile + offline render; 114 of them is the PF-079 review's job, not
this kit's). Single-patch invocation is the right grain for a hand-picked candidate list.

`tools/check.sh audio` was deliberately **not** run for this kit: it calls `level_full`
first (full C++ build + prompt-grounding checks, ~2 min) before reaching the same oracle
pass — correct for CI, unnecessary overhead for pre-screening eight specific patches.
`render_oracle.py` directly is the same engine at the right grain.

Raw output for every patch probed: `bench/results/oracle_20261014_session_kit.json`.

---

## 4. REAPER runbook

Adapted from `docs/sessions/017-phase2-interactive-host.md` §§0–2 (environment + build +
host setup — reused verbatim, not re-derived) and `bench/CAPTURE_SESSION_CHECKLIST.md`
(capture mechanics). **One correction to how a prior plan might read this:**
`host/Source/CodeEditorPanel.h` is **read-only today** ("Phase 3a is deliberately the view
alone... 3b is editable + a Compile button," not yet shipped) — there is no way to paste a
candidate's exact saved Faust source into the live host and compile it directly. The only
way to hear a candidate through the real shipping audio path (JIT compile → ParamPool →
OutputGuard, not `faust2sndfile`) is to **retype the verbatim prompt and regenerate live**.

**This means what you hear in REAPER is a fresh draw from the model, not a byte-identical
replay of what the oracle pre-screened above.** The repo's own evidence says this is
usually close but not guaranteed identical — PF-031's measurement: groq at temperature 0
gave 88%/84% on two runs of an *unchanged* tree with only partial failure-set overlap. The
pre-screen above tells you which prompts are *known not to be a trap* at this point in the
model's output distribution; it is not a guarantee about the specific sample REAPER hands
you today. If what you hear seems to contradict a PASS verdict above, that disagreement is
itself useful data — note it, it is not a bug in this kit.

### 4.1 — Setup (mechanical, ~5 min)

```bash
cd /home/losera/PluginForge
set -a; source .env; set +a                       # PLUGINFORGE_PROVIDER, keys
export PLUGINFORGE_LLM_SCRIPT="$PWD/llm/generate.py"
python3 llm/providers.py --check "$PLUGINFORGE_PROVIDER"   # confirm it's live; must be
                                                             # groq/gemini/openrouter/ollama,
                                                             # never anthropic unless you
                                                             # have explicitly decided to
                                                             # spend real money (PLUGINFORGE_ALLOW_PAID=1)
export PLUGINFORGE_CAPTURE=1                        # leaves an exact repro per generation
reaper 2>&1 | tee ~/pf_r12_session_$(date +%s).log
```

Confirm REAPER's VST path includes the in-tree build
(`host/build/PluginForgeHost_artefacts/.../VST3` and the `PluginForgeSynth` equivalent —
see session 017 §0.5 if rebuilding is needed) and rescan. Insert both `PluginForge Host`
(effects, candidates #1-7) and `PluginForge Synth` (candidate #8) on separate tracks.
Candidates #1-7 need a signal running through the Fx track (session 017 §3's anchor: a
sustained source, or `mpv --loop artifacts/audio/input_testsignal.wav`); #8 needs a MIDI
source (`vmpk` or REAPER's own virtual keyboard) feeding the Synth track.

Generate once on each track and confirm `bench/captures/<today>/001-*.json` exists before
starting the real run — capture fails open and silently if misconfigured
(`CAPTURE_SESSION_CHECKLIST.md` step 2).

### 4.2 — Run order

Work §1's table top to bottom. #1 is the control — if it fails, the session is broken, not
the model, and nothing past it is worth running. For each candidate:

1. Paste the exact verbatim prompt (no edits — a reworded prompt is a different data point).
2. Generate. Note the capture ID printed to stderr.
3. Listen against dry (effects) or play a held note / a 5-note chord (the instrument).
4. Fill one row of the scorecard (§5) immediately, while it's still in your ears.

Budget: ~2-3 min per candidate including the generation wait, ~20-25 min for all 8 —
comfortably inside the hour, and candidates #9-12 (screened out above) cost you none of it.

### 4.3 — Optional: audit against the exact pre-screened patch

If a live result surprises you (sounds broken when §1 said PASS, or vice versa), the
*exact* code this kit pre-screened is sitting in `bench/results/oracle_20261014_session_kit.json`
— diff it against your capture's `faust_source` field to see whether the model drew the
same patch or a different one this time.

---

## 5. Scorecard — one row per candidate, under a minute to fill

| field | fill with |
|---|---|
| **#** / capture ID | from §1's table / the `bench/captures/.../NNN-*.json` path |
| **Primitive match?** | Did the model reach for the *right kind* of DSP (filter vs. delay vs. dynamics vs. oscillator) for this prompt? Y / N / partial |
| **Direction right?** | Did it move the sound the way the prompt implies (darker/brighter, wetter/drier, glued/loose, shaped/static)? Y / N |
| **One telling detail** | A specific moment, not a vibe — "feedback decays unnaturally fast," "glue is just a level drop, no compression curve," "ADSR release is inaudible" |
| **Oracle agreed?** | Did what you heard match §1's PASS/caveat? Y / N — a mismatch here is itself worth a note (see §4.3) |
| **Keep / retry / reject** | Would this ship as-is, or does it need a regeneration / is it a hard fail? |
| **Severity if rejected** | n/a / cosmetic / material / reject-outright |

---

## 6. Headline numbers — what's currently $0-publishable today, and what's stale

Three numbers were asked for. Research finding, stated plainly: **none of the three need a
paid re-measurement** — every path to a fresher number runs through the existing free-tier
providers (groq/ollama), and the project's own notes name the gap as *not-yet-run*, not
*paid-gated*. The one number on record that genuinely needs a paid account is a different,
older, explicitly non-comparable baseline (see the note at the end).

| number | current value | measured | $0 today? | stale how |
|---|---|---|---|---|
| **First-try compile rate** | Overall 93/125 (74.4%); by tier L2/L3/L4 all 84%, **L1 48%** (trough), L0 72%. `dynamics` weak at every tier (20-60%). | `efficacy_groq_20260831.json`, scored 2026-09-08. **n=1 per cell** — PF-031's ≥3 bar is unmet. | Yes — the number itself needed no money to produce; re-running it would cost quota-time, not cash. | Archive **predates** both the 2026-09-09 dynamics/routing-arity fix and the 2026-09-15 PF-032 gate-time-unit fix. The dynamics subset alone was spot-re-measured post-fix (2026-09-09, `efficacy_groq_dynamics_postfix_20260909.json`): first-try barely moved, 8/25→9/25. |
| **Within-3-retries (retry-corrected)** | Overall 114/125 (91.2%); by tier L4 92%, L3 96%, L2 96%, **L1 84%**, L0 88%. | Same archive, same date (2026-09-08). Same n=1 caveat. | Yes, same reasoning. | Same staleness as above — same archive, same unpatched prompt state for everything except the re-tested dynamics subset. |
| **Render-safe rate** | groq **56-68%**, ollama **75-95%**, over 325 compiled cells across the 4 committed archives. 26 compiled-but-broken patches found (silence, NaN/Inf, up to +715 dB runaway, DC offset up to 39.5). | PF-079, 2026-09-23, `bench/render_oracle.py` — $0, no model calls. **Independently re-confirmed in part today** (2026-10-14): re-running the oracle on 8 fresh candidates drawn from the same `efficacy_groq_20260831.json` archive found exactly the pattern PF-079 describes — 1 of 2 `dynamics` candidates silent despite a clean compile flag (§2 above). | Yes — this is the one of the three that is itself an oracle run, zero model calls either time. | Measured over archives that **predate** the same two fixes (2026-09-09, 2026-09-15) named above — so current render-safety, especially in `dynamics`, is plausibly *better* than 56-68% today, but that has not been measured. |

**What would make all three current:** re-run the 125-cell groq grid (and ideally ollama)
post-fix, at n≥3. This is explicitly STATUS.md's reserved "Next three things" #1 (PF-031),
**already in flight** at `.worktrees/efficacy-pf031` (38/125 cells checkpointed,
uncommitted) — **do not disturb that worktree**; this kit did not touch it and nothing
above required touching it.

**A fourth number exists and is not one of the three above — do not conflate them.** A
frozen `claude`/`claude-opus-4-6` compile rate of 88%, recorded 2026-07-19, sits in
`bench/results/.prompt_baseline.json`. Its own note says it explicitly: *"the Anthropic
account is out of credit... this number is not currently re-measurable and must not be
overwritten."* That one genuinely needs a funded Anthropic account to refresh — but it also
measured a since-deleted prompt file and is marked not comparable to anything current. It
is not being presented as a headline number here.

---

## 7. One related, standing ask this kit does not replace

STATUS.md "Waiting on you" #2: *"A listening pass on the interactive-session patches"* —
the four PF-072/073/074/075 findings from session 017, which need the *same* REAPER
environment but a *different* run script (session 017 §§3-8, not this kit's §1 candidates).
If you're already in REAPER for this kit, folding that pass into the same sitting costs
only the setup once; §4.1's environment setup is identical for both.
