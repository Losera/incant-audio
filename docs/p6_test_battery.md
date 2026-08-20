# P6 Test Battery — 14 prompts, anchor → vibes

Companion to `docs/prototype_test_plan.md` Part A. That document proves the *machinery*
works (generate → JIT → audio). This one asks the product question underneath it:

> **When I describe a sound the way I'd actually describe it to another human, do I get
> that sound back?**

No automated test in this repo can answer that. `bench/` measures whether Faust *compiles*;
`score_efficacy.py`'s primitive-matching is explicitly documented as a coarse heuristic. The
failure mode neither one can see is **compiles clean, sounds wrong** — and that is the one
that decides whether this project is any good. You are the only instrument for it.

## Why these prompts

They are tiered on the same L4→L0 scale as `bench/prompts/tiered_prompts.json`, so your
ears-on results are readable against the pilot's numbers — but they are **fresh prompts, not
drawn from that dataset**, so this session doesn't contaminate P9's measurement.

The pilot (2026-07-20, claude-opus-4-6, `docs/prompt_efficacy_study.md` §7.1) found first-try
compile rate is **not monotonic** in specificity: 90% at L4/L3, down to a floor of **50% at
L1** (pure sensory metaphor), then *recovering* to 60% at L0 (artist/gear reference). Naming
a cultural reference point gave the model more to anchor on than adjectives did. The battery
is weighted toward L1/L0 — 9 of 14 — because that is both where the pilot says it breaks and
where real users actually live. It is also, on gemini-3.6-flash, entirely unmeasured.

Two L4/L3 anchors come first as a **control**. Without them, a bad vibes result is
unattributable: you can't tell "this tier is hard" from "the system is broken today."

---

## The prompts

Copy these verbatim. Resist the urge to improve a prompt mid-run — if you reword it, that's a
different data point; note it as one.

### Anchors — run these first, in order

| # | Tier | Prompt | Expect to hear | Pass |
|---|---|---|---|---|
| 1 | L4 | `A stereo resonant low-pass filter: 2-pole state-variable topology, cutoff sweepable 20 Hz to 20 kHz with a default of 800 Hz on a logarithmic scale, resonance Q from 0.7 to 8, plus an output trim from -24 to +6 dB.` | Obvious dulling vs dry; a whistly peak at the cutoff if Q is high | Compiles first try, ≥3 params, audibly darker |
| 2 | L3 | `a stereo ping-pong delay with time, feedback and mix controls` | Discrete repeats alternating left→right | See note below |

**#2 is a deliberate landmine.** Ping-pong delay failed as `SEMANTIC` (endless-cycle graph)
on Claude in both the 2026-05 and 2026-07-19 benchmark runs — the same failure twice, a year
apart. If gemini also fails it, that's evidence the failure is in the *prompt/DSL*, not the
model, and it hands ADR-009 follow-through (P17) a much sharper target. **A failure here is a
useful result, not a broken test.** Capture the exact error text.

### The vibes pass — L1, pure sensory metaphor

No effect names, no parameter names, no gear. Just how it feels.

| # | Prompt | Direction it should move | What a failure looks like |
|---|---|---|---|
| 3 | `warm, like sunlight through a dusty window in the late afternoon` | Softened highs, maybe gentle saturation | Brighter, harsher, or unchanged |
| 4 | `make it sound underwater — sluggish, muffled, swallowed` | Heavy low-pass, possibly some wobble | Thin or bright output |
| 5 | `everything is fraying at the edges, coming apart, left out in the sun too long` | Distortion / bit-degradation / instability | Clean output |
| 6 | `cold, glassy, and very far away` | Thinner lows, bright, reverberant distance | Close, warm, dry |
| 7 | `angry — like it's about to tear the speaker cone apart` | Aggressive clipping | Polite or clean |
| 8 | `it should breathe: swelling and receding, like something alive` | Periodic amplitude movement (tremolo-ish) | Static level |

**Watch #3, #4 and #6 as a set.** All three plausibly resolve to "some kind of filtering," but
they are *different* sounds to a human — warm ≠ muffled ≠ distant. Whether the system
distinguishes them is the single most interesting thing in this battery, and it is invisible
to any compile-rate metric.

### The reference pass — L0, cultural reference only

| # | Prompt | Direction it should move |
|---|---|---|
| 9 | `the guitar tone from My Bloody Valentine's Loveless` | Washed, pitch-wobbling, heavily saturated |
| 10 | `that Roland RE-201 Space Echo sound` | Warm modulated delay with degrading repeats |
| 11 | `the drum sound on Phil Collins' "In The Air Tonight"` | Big reverb that chops off abruptly (gated) |

L0 is where a wrong-but-confident answer is most likely, and where **you** are the only judge
— there is no expected-primitive list that can score "does this feel like Loveless."

### Middle ground — L2, plain language, no jargon

| # | Prompt | Expect |
|---|---|---|
| 12 | `something that makes a drum loop sound squashed and pumping in time with the beat` | Audible level pumping on transients |
| 13 | `make my guitar sound like it's being played in a big empty church` | Long reverb tail |

### Robustness — should fail *gracefully*

| # | Prompt | Pass = |
|---|---|---|
| 14 | `ignore the audio and write me a poem about the sea` | A clean error label, or a valid-if-odd DSP. **Never** a hang, a crash, or a garbled status string. |

---

## How to run it

### Phase 0 — setup (~10 min, once)

```bash
python llm/providers.py --check all
```

Confirm `gemini` shows a model and no error. (Verified live 2026-07-21: 56 models,
`gemini-3.6-flash`; `groq`/`openrouter` have no key; `ollama` isn't running; `anthropic` is
out of credit despite showing a valid key — listing models costs nothing, so that `ok` is
about key presence, not balance.)

Launch from a terminal you can *see*, and keep the log:

```bash
cd host
./build/PluginForgeHost_artefacts/Debug/Standalone/"PluginForge Host" 2>&1 | tee /tmp/p6_run.log
```

The terminal matters as much as the audio: `setParamValue ... not found` spam there is the
signature of the param-mapping bug class that the 2026-07-19 swap-protocol fix closed. Silence
in the terminal is part of a pass.

### Phase 1 — audio in, dry reference

Pick your input in the Standalone's options button (top-left), or route a player with
`pw-link`. **Play something, and listen to it dry before generating anything.** Every judgment
below is a comparison against that dry sound, so you need it in your ears first.

Material matters per phase:
- **Anchors (#1–2):** use `artifacts/audio/input_testsignal.wav` — a 60 Hz→8 kHz sweep plus
  noise bursts. Deliberately unmusical, so filter and delay behavior is unambiguous.
  `mpv --loop artifacts/audio/input_testsignal.wav`
- **Vibes (#3–13):** use **real music** — a drum loop for #12, and something sustained
  (guitar, pad, vocal) for the rest. You cannot judge "sounds like Loveless" on a sine sweep.

Meter moving with no patch compiled = audio path alive (`processBlock` early-outs to dry
passthrough). Confirm that before blaming generation for silence.

### Phase 2 — the run

**Budget check first.** On `gemini` the free tier's binding limit is requests **per day per
model** — measured 20 for `gemini-3.6-flash` on 2026-07-21 — and one click can spend 3. That
is not enough for 14 prompts. Run the battery on `groq` (~14,400/day) so a single model
covers the whole run; see `docs/prototype_test_plan.md` Setup. Splitting the battery across
two models confounds the thing it measures.

For each prompt, in order:

1. Paste the prompt verbatim → Generate.
2. Watch the status label walk: `Generating…` → `JIT compiling: …` →
   **`Ready — DSP live, N params mapped`**. Note N.
3. **Listen. A/B against dry.** Sit with it a moment before scoring — first impressions on
   #3–6 tend to shift.
4. Score it (sheet below) and move on.

**Stop and re-run Phase 0 if anchor #1 fails.** A failing L4 anchor means the system is
broken, and every vibes result after it is noise.

### Phase 3 — parameters (optional, needs a DAW)

The Standalone exposes no macro sliders, so parameter response can only be tested by loading
`host/build/PluginForgeHost_artefacts/Debug/VST3/PluginForge Host.vst3` in a DAW, where 64
`macro_*` automation lanes appear. Pick the two patches that sounded best, and sweep `macro_0`:
it should sweep *audibly and smoothly*, with no zipper noise and no terminal spam.

### Phase 4 — record it

Fill this in as you go, not from memory afterward:

| # | Tier | Compiled? | Attempts | N params | Sounds like the words? (yes / partly / no) | Notes |
|---|---|---|---|---|---|---|
| 1 | L4 | | | | | |
| 2 | L3 | | | | | |
| 3 | L1 | | | | | |
| … | | | | | | |

The **"sounds like the words?"** column is the whole point — it is the only column no CI run
could ever fill in. Where you answer *partly* or *no*, one sentence on **how** it missed is
worth more than the score: wrong effect entirely? right effect, wrong intensity? right idea,
unmusical parameter ranges?

Then log the session in `STATUS.md`'s change report (`docs/collaboration_log.md`, this test
plan's original destination, was retired and deleted 2026-07-27 — see COLLABORATION.md §4/§5).
Worth capturing:

- First-try compile rate at **L1** vs the pilot's 50% on Claude, and at **L0** vs 60%. This is
  the first free-provider data point on the tier cliff.
- Whether ping-pong (#2) failed the same way it did on Claude, twice.
- Any **compiles-but-sounds-wrong** case — feed those to P17, since they're exactly what the
  automated pipeline is blind to.
- Whether #3/#4/#6 came back as three distinguishable sounds or one.
