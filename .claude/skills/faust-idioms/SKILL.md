---
name: faust-idioms
description: Verified Faust patterns for the four failure classes PF-024 records — endless evaluation cycle, stereo/mono routing mismatch, unbounded delay, and with{} syntax errors. Use when writing or debugging Faust DSP, editing llm/prompts/system_prompt.txt few-shots, or when a generated patch fails to compile. Trigger on "faust", "dsp won't compile", "endless evaluation cycle", "PF-024", "/faust-idioms".
---

# Faust idioms that actually compile

Every pair below was reproduced and re-verified against **Faust 2.85.5** on this machine
on 2026-07-27. The failing forms are the ones PF-024 recorded from the 2026-07-24 P6
battery (`docs/BUGS.md:288-298`) and `bench/results/results.json`. Do not paste an idiom
from here into `llm/prompts/system_prompt.txt` without re-running
`tools/check.sh audio` — a prompt edit is Tier 2 and owes a benchmark statement.

## 1. `endless evaluation cycle` — P6 #2, ping-pong delay

A definition that refers to itself outside a delay is a *compile-time* recursion, not a
feedback loop. This is a semantic error, not syntax, which is why prompt grounding that
only checks symbol resolution never caught it.

```faust
// FAILS: "after 400 evaluation steps, the compiler has detected an
//         endless evaluation cycle of 3 steps"
pingpong = pingpong : *(0.5);
```

Feedback belongs in `~`, which inserts the one-sample delay that breaks the cycle. A
working stereo ping-pong — verified compiling:

```faust
import("stdfaust.lib");
MAXD  = 96000;
dtime = hslider("time[unit:ms]", 250, 10, 1000, 1) : si.smoo : *(ma.SR/1000.0);
fb    = hslider("feedback", 0.4, 0, 0.95, 0.01);

process = _,_ <: si.bus(2) : (cross ~ (de.fdelay(MAXD, dtime), de.fdelay(MAXD, dtime)))
with {
  // the cross is what makes it ping-pong: each side feeds the OTHER side back
  cross(fbL, fbR, inL, inR) = inL + fbR*fb, inR + fbL*fb;
};
```

## 2. `2 outputs must equal 1 input` — P6 #6, stereo routing

```faust
// FAILS: "sequential composition A:lowpass(2)(800)" — 2 outs into a 1-in block
process = _,_ : fi.lowpass(2, 800);
```

`:` demands the left side's output count equal the right side's input count. Use `par` to
apply a mono block across both channels:

```faust
process = par(i, 2, fi.lowpass(2, 800));      // stereo in -> stereo out
```

Related operators, so the choice is deliberate rather than guessed:
`<:` splits (1→N), `:>` merges (N→1), `,` is parallel, `si.bus(n)` is n straight wires.

## 3. `invalid delay parameter range` — P6 #6, unbounded delay

```faust
// FAILS: "possible negative values of : int(min(Delay(...)))"
d = hslider("t", 100, 1, 2000, 1) : si.smoo;
process = de.delay(d, d);                      // max delay is not a constant
```

The **first** argument to `de.delay`/`de.fdelay` is the maximum delay and must be a
compile-time constant — it sizes the buffer. The second is the live delay, and it must be
clamped inside that buffer, because `si.smoo` produces intermediate values the slider
range does not bound:

```faust
import("stdfaust.lib");
MAXD = 192000;                                 // literal: sizes the buffer
d = hslider("time[unit:ms]", 100, 1, 2000, 1) : si.smoo
    : *(ma.SR/1000.0) : min(MAXD-1) : max(1);  // clamp BOTH ends
process = de.fdelay(MAXD, d);
```

`de.fdelay` interpolates and is the right choice for a modulated delay; `de.delay` is
integer-only and will zipper if `d` moves.

## 4. `syntax error, unexpected WITH` — P6 #10, RE-201

`with {}` is not a top-level statement. It binds to the expression immediately before it,
inside the same definition:

```faust
// FAILS: "syntax error, unexpected WITH"
process = _ * g;
with { g = 0.5; };
```

```faust
process = _ * g with { g = 0.5; };             // inline form

process = out with {                            // block form, for anything longer
  g   = hslider("gain", 0.5, 0, 1, 0.01) : si.smoo;
  out = par(i, 2, _ * g);
};
```

Note the semicolons: every definition *inside* the block ends with one, and the closing
`};` ends the enclosing definition.

## 5. `undefined symbol` — invented stdlib functions

`bench/results/results.json` records `undefined symbol : flanger_mono`. There is no such
function. Every `ns.func` must resolve against the installed
`/usr/share/faust/*.lib` (53 libraries on this machine); `.claude/hooks/check_prompt_invariants.py`
blocks a prompt edit that names one that does not, and `tools/gen_stdlib_block.py`
generates the prompt's stdlib block from what is actually installed.

**Check before writing, do not recall:**

```bash
grep -rn "^flanger" /usr/share/faust/*.lib     # does it exist, and in which namespace?
faust /tmp/probe.dsp -o /dev/null              # the only real answer
```

## The standing rule

Faust is a compiled language with a fast compiler and no runtime cost to being wrong at
build time. Any idiom you are not certain of is one `faust file.dsp -o /dev/null` away
from being verified. Under COLLABORATION.md §3 that command *is* the artifact — an idiom
asserted without it does not land in a prompt file.
