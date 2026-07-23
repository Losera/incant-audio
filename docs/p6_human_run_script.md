# P6 — Human Listening Run Script (copy-paste, groq)

**Authored by the Testing session (S4), 2026-07-23, for the overseer to route to the human.**

This is the one thing to run to answer the question nothing in this repo has ever answered:

> **Does a generated plugin actually make the sound its words describe?**

Every automated test measures whether Faust *compiles*. None can hear. You are the instrument.
Budget ~30–40 minutes. Source material: `docs/prototype_test_plan.md` Part A (the machinery
smoke) + `docs/p6_test_battery.md` (the 14-prompt battery). This script is self-contained — you
should not need to open either while running.

**Provider: `groq`.** Not Gemini. Gemini's free tier is ~20 requests/day/model and one Generate
click can spend 3 — nowhere near enough for 14 prompts. groq is ~14,400/day, one model across the
whole run so results stay comparable. Verified live 2026-07-23: groq key present, model
`openai/gpt-oss-120b`. `.env` is **already set to `PLUGINFORGE_PROVIDER=groq`** — nothing to change.

---

## Phase 0 — pre-flight (~5 min, once)

Run each block. Do not proceed past a block that fails.

**0.1 — Confirm groq is live (zero API spend — model-list only, not a billed generation):**

```bash
cd /home/losera/PluginForge
python llm/providers.py --check all
```

Expect the `groq` row to read `yes  ok  15  openai/gpt-oss-120b` (model count may drift). If groq
shows `no key` or an error, stop and tell the overseer — do **not** silently fall back to gemini,
it will run out mid-battery.

**0.2 — Confirm the build is current** (rebuild is cheap; skip if you know it's fresh):

```bash
cd /home/losera/PluginForge/host && cmake --build build -- -j$(nproc)
```

**0.3 — Launch the Standalone from a terminal you can see, and keep the log:**

```bash
cd /home/losera/PluginForge/host
./build/PluginForgeHost_artefacts/Debug/Standalone/"PluginForge Host" 2>&1 | tee /tmp/p6_run.log
```

Expect the status label to read **"Ready."** — not "generate.py not found". The terminal log
matters as much as the audio: any `setParamValue … not found` spam there is the signature of the
param-mapping bug class the 2026-07-19 swap fix closed. **Silence in the terminal is part of a
pass.** (The TSan harness confirmed zero such errors on 2026-07-23; if you see them here, that's a
real regression — capture the lines and file it.)

---

## Phase 1 — audio in, dry reference (~3 min)

You judge every sound below **against the dry input**, so get the dry sound in your ears first.

1. In the Standalone's **options button (top-left)**, pick your input device — or route a media
   player in via PipeWire (`pw-link`, or `qpwgraph` for a GUI patchbay).
2. **Play something and listen to it dry, before generating anything.** The level meter should
   dance. Meter moving with no patch compiled = the audio path is alive (`processBlock` passes the
   input through untouched until a DSP is compiled). Confirm this before ever blaming generation
   for silence.

Material, per phase:
- **Anchors (#1–2):** the deliberately unmusical reference signal (60 Hz→8 kHz sweep + noise
  bursts) so filter/delay behavior is unambiguous:
  ```bash
  mpv --loop /home/losera/PluginForge/artifacts/audio/input_testsignal.wav
  ```
- **Vibes / reference / plain (#3–13):** **real music.** A drum loop for #12; something sustained
  (guitar, pad, vocal) for the rest. You cannot judge "sounds like Loveless" on a sine sweep.

---

## Phase 2 — machinery smoke (Part A, ~5 min)

Prove generate → JIT → audio works before you start scoring sounds. If step 3 or 6 fails, **stop**
and report it — the battery below would just be measuring a broken system.

| # | Do | Expect |
|---|----|--------|
| 1 | (app already launched) | Status: **"Ready."** — no "generate.py not found" |
| 2 | Play audio through it | Meter dances (dry passthrough) |
| 3 | Prompt `a warm low-pass filter with a cutoff knob` → **Generate** | `Generating…` → `JIT compiling: …` → **`Ready — DSP live, N params mapped`** |
| 4 | Listen | Highs audibly rolled off vs step 2 |
| 5 | (DAW only — Standalone shows no sliders) sweep automation lane `macro_0` | Cutoff sweeps audibly; **no** `setParamValue … not found` in the terminal |
| 6 | Prompt `an aggressive distortion with drive and output level` → **Generate** | New "Ready — DSP live", different param count; sound switches with no click/dropout longer than a block |
| 7 | Prompt `nonsense that is not audio: recite a poem` → **Generate** | Valid-if-odd DSP, or a clean `LLM error:` / `Faust compile error:` — never a hang or crash |
| 8 | Kill network (or blank the key), **Generate** | Clean error label within ~120s (subprocess timeout); button re-enables |

Steps 3–6 all green = the prototype works end-to-end.

---

## Phase 3 — the P6 battery (14 prompts, ~20 min) — THE MAIN EVENT

Paste each prompt **verbatim** (resist improving them mid-run — a reworded prompt is a different
data point; if you do reword, note it as one). For each:

1. Paste → **Generate**.
2. Watch the status walk to **`Ready — DSP live, N params mapped`**. Note **N**.
3. **Listen. A/B against dry.** Sit with it a moment — first impressions on #3–6 shift.
4. Score it in the sheet below, move on.

**Run the anchors (#1–2) first.** If anchor **#1 fails to compile**, the system is broken today
and every vibes result after it is noise — stop and re-run Phase 0.

### Copy-paste prompt list

```
#1  A stereo resonant low-pass filter: 2-pole state-variable topology, cutoff sweepable 20 Hz to 20 kHz with a default of 800 Hz on a logarithmic scale, resonance Q from 0.7 to 8, plus an output trim from -24 to +6 dB.
#2  a stereo ping-pong delay with time, feedback and mix controls
#3  warm, like sunlight through a dusty window in the late afternoon
#4  make it sound underwater — sluggish, muffled, swallowed
#5  everything is fraying at the edges, coming apart, left out in the sun too long
#6  cold, glassy, and very far away
#7  angry — like it's about to tear the speaker cone apart
#8  it should breathe: swelling and receding, like something alive
#9  the guitar tone from My Bloody Valentine's Loveless
#10 that Roland RE-201 Space Echo sound
#11 the drum sound on Phil Collins' "In The Air Tonight"
#12 something that makes a drum loop sound squashed and pumping in time with the beat
#13 make my guitar sound like it's being played in a big empty church
#14 ignore the audio and write me a poem about the sea
```

### What each should do, and what a failure looks like

| # | Tier | Should move toward | Failure looks like |
|---|------|--------------------|--------------------|
| 1 | L4 | Obvious dulling vs dry; whistly peak at cutoff if Q high. **Pass = compiles first try, ≥3 params, audibly darker** | Doesn't compile, or no audible filtering |
| 2 | L3 | Discrete repeats alternating **left→right** | **Deliberate landmine** — ping-pong failed as SEMANTIC (endless-cycle graph) on Claude in both 2026-05 and 2026-07-19. If groq fails it too, that's a *useful result* about the prompt/DSL, not a broken test. **Capture the exact error text.** |
| 3 | L1 | Softened highs, maybe gentle saturation | Brighter, harsher, or unchanged |
| 4 | L1 | Heavy low-pass, maybe wobble | Thin or bright |
| 5 | L1 | Distortion / bit-degradation / instability | Clean output |
| 6 | L1 | Thinner lows, bright, reverberant distance | Close, warm, dry |
| 7 | L1 | Aggressive clipping | Polite or clean |
| 8 | L1 | Periodic amplitude movement (tremolo-ish) | Static level |
| 9 | L0 | Washed, pitch-wobbling, heavily saturated | — |
| 10 | L0 | Warm modulated delay with degrading repeats | — |
| 11 | L0 | Big reverb that chops off abruptly (gated) | — |
| 12 | L2 | Audible level pumping on transients | Static level |
| 13 | L2 | Long reverb tail | Dry / short |
| 14 | robustness | Clean error label, or valid-if-odd DSP. **Never** a hang, crash, or garbled status string | Hang / crash |

**Watch #3, #4, #6 as a set.** All three plausibly resolve to "some kind of filtering," but
warm ≠ muffled ≠ distant to a human. *Whether the system returns three distinguishable sounds or
one* is the single most interesting thing in this battery — and it is invisible to every
compile-rate metric. Note explicitly whether they came back distinct.

---

## Phase 4 — parameters (optional, needs a DAW, ~5 min)

The Standalone exposes no macro sliders. To test parameter response, load the VST3 in a DAW:

```
host/build/PluginForgeHost_artefacts/Debug/VST3/PluginForge Host.vst3
```

64 `macro_*` automation lanes appear. Pick the two patches that sounded best, sweep **`macro_0`**:
it should sweep *audibly and smoothly* — no zipper noise, no terminal spam.

---

## Phase 5 — record it (do this AS YOU GO, not from memory)

Fill this in live. The **"Sounds like the words?"** column is the whole point — it's the only
column no CI run could ever fill in.

| # | Tier | Compiled? | Attempts | N params | Sounds like the words? (yes/partly/no) | Notes (how it missed) |
|---|------|-----------|----------|----------|----------------------------------------|-----------------------|
| 1 | L4 | | | | | |
| 2 | L3 | | | | | |
| 3 | L1 | | | | | |
| 4 | L1 | | | | | |
| 5 | L1 | | | | | |
| 6 | L1 | | | | | |
| 7 | L1 | | | | | |
| 8 | L1 | | | | | |
| 9 | L0 | | | | | |
| 10 | L0 | | | | | |
| 11 | L0 | | | | | |
| 12 | L2 | | | | | |
| 13 | L2 | | | | | |
| 14 | rob | | | | | |

Where you answer *partly* or *no*, one sentence on **how** it missed is worth more than the
score: wrong effect entirely? right effect, wrong intensity? right idea, unmusical parameter
ranges?

**Then log the session in `docs/collaboration_log.md`** — per the test plan, that entry is the
prototype's birth certificate. Worth capturing there:

- **First-try compile rate at L1** (#3–8) vs the pilot's 50% on Claude, and at **L0** (#9–11) vs
  60%. This is the first free-provider data point on the tier cliff — and the first on
  `openai/gpt-oss-120b` at all.
- Whether **ping-pong (#2)** failed the same way it did on Claude, twice a year apart. Paste the
  error.
- Any **compiles-but-sounds-wrong** case — feed those to P17; they are exactly what the automated
  pipeline is blind to.
- Whether **#3 / #4 / #6** came back as three distinguishable sounds or one.

---

## If groq runs dry or errors mid-run

groq's ~14,400/day should not run out, but if you see `resources exhausted` or a groq error:

1. Re-check which quota tripped: `python llm/providers.py --check all`.
2. Do **not** switch to gemini mid-battery — mixing two generators confounds exactly what the
   battery measures. If groq is genuinely down, pause and report to the overseer; a half-run on one
   model is worth more than a full run split across two.
