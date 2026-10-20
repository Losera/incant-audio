# PF-079 prompt fix — landed 2026-10-20

Supersedes `docs/records/pf079-prompt-fix-draft-2026-10-18.md` (not landed, blocked on
headroom). This session trimmed headroom, landed the rule, and ran a real quota-spending
benchmark against groq to show it changes model output — the step that draft left open.

## Headroom trim (`tools/gen_stdlib_block.py`)

Starting headroom was 85 tokens against groq (`tests/test_prompt_headroom.py`), and the
proposed rule cost ~170 estimated tokens -- not enough room. Per that file's own standing
instruction ("Re-check the evidence before trimming further; do not extend this list by
feel"), re-measured real usage with plain-substring search across every
`bench/results/*.json` and `bench/results/efficacy/*.json` archive (the same methodology
the 2026-07-31 trim used, confirmed by reading that comment directly — not recalled). Two
entries met the file's own bar (zero occurrences AND a sibling already demonstrating the
same shape), and neither is one of the entries that comment explicitly protects
(`ef.transpose`, `ba.midikey2hz`, `ve.moog_vcf`/`ve.moogLadder`'s Hz-vs-normalised pairing):

- `pf.vibrato2_mono` (EFFECT profile): 0 occurrences; `pf.flanger_mono` +
  `pf.phaser2_mono` already demonstrate this group's entire point (the `pf.` namespace,
  not `ef.`).
- `os.triangle` (EFFECT profile only -- `CURATED_INSTRUMENT`'s own `os.triangle` left
  untouched): 0 occurrences; the "oscillator takes one frequency arg" shape is already
  shown three times over (`osc`/`sawtooth`/`square`) plus `lf_triangle`.

Freed 85 -> 148 tokens of raw headroom.

## The rule (`llm/prompts/system_prompt.txt`)

Added to STRICT RULES, next to the existing units rule (same "this compiles clean and
renders wrong" theme):

```
- Expander/gate gain_db: clamp after computing (min(gain_db,60)) — linear2db
  floors near -758dB on silence, not -inf. select2(cond,False,True) is NOT
  True-first — prefer ba.if(cond,then,else) for threshold/clip logic.
```

Citations (read directly this session, not recalled): `/usr/share/faust/basics.lib:163`
(`ba.linear2db`'s `max(ma.MIN, g)` floor), `:1888` (`ba.if`'s `select2` arm order),
`/usr/share/faust/maths.lib:276` (`ma.MIN` at single precision = -758.6dB in log domain).

Final headroom: **80 tokens** (`tests/test_prompt_headroom.py`), all 6 of that file's own
guard assertions pass, including the one that failed at 26 tokens of slack (simulates a
5% stdlib growth and requires positive headroom after it -- the file's own documented
minimum-margin bar, not a number this session invented).

`tools/gen_presentation_prompt.py --write` re-run (the presentation variant is generated
FROM `system_prompt.txt`+`presentation_block.txt`; it was stale after the edit) --
`--check` now passes. `tools/gen_stdlib_block.py --verify-prompt` confirms every `ns.func`
reference in all three prompt files still resolves, including the two new ones in the
rule itself (`ba.if`, `ba.linear2db`).

## Benchmark proof (quota spent, groq, `openai/gpt-oss-120b`)

Per COLLABORATION.md's Tier-2 rule ("a prompt edit owes a benchmark statement"), generated
3 fresh samples each for the two exact PF-079 prompts, against the live provider, then ran
every sample through `bench/render_oracle.py`:

| Prompt | Trial | Approach taken | Oracle result |
|---|---|---|---|
| upward expander, threshold+ratio | 1 | `exp()`/`log()` power-law + `ba.if` | PASS, rms +7.9dB |
| | 2 | multiplicative `ba.if(abs(x)>thresh, ratio, 1)` | PASS, rms +5.9dB |
| | 3 | real stdlib `co.compressor_stereo` w/ ratio<1 | PASS, rms +27.6dB |
| brick-wall limiter, threshold | 1 | `max(min(_,thLin),-thLin)` | PASS, rms -0.0dB |
| | 2 | multiplicative `ba.if(abs(x)>thresh, thresh/abs(x), 1)` | PASS, rms -0.0dB |
| | 3 | `max(-threshold, min(threshold,_))` | PASS, rms +0.0dB |

**6/6 render-safe. Zero occurrences of either original mechanism** (the unbounded
dB-domain `gain_db = select2(level<thresh, 0, (thresh-level)*(ratio-1))` formula, or a
hand-written `select2` for clip/threshold logic at all) across all six samples -- every
generation reached for `ba.if` or a direct `max`/`min` clamp instead, which is exactly
what the new rule recommends.

**What this does and does not prove.** n=3 per prompt is still small -- not the n>=3
aggregated-per-cell rigor `bench/results/efficacy/efficacy_groq_n3_rep1_20260915.json`
is building toward for the full 125-cell grid (PF-031). It is real evidence against the
two specific PF-079 prompts that motivated the rule, generated fresh this session against
the shipping provider, not a simulation. It does not establish a base rate across the
wider `dynamics` category, and none of the six samples happened to reach for `select2` at
all, so the arm-order half of the rule was not directly exercised -- only the
expander-gain-clamp half was.

## Verification

- [x] `python3 tests/test_prompt_headroom.py`: 80 tokens slack.
- [x] `pytest tests/test_prompt_headroom.py -q`: 6 passed.
- [x] `python3 tools/gen_stdlib_block.py --check`: 41 effect + 21 instrument entries resolve.
- [x] `python3 tools/gen_stdlib_block.py --verify-prompt`: all three prompt files resolve.
- [x] `python3 tools/gen_presentation_prompt.py --check`: matches its sources.
- [x] `tools/check.sh fast`: green.
- [x] 6 live groq generations + `bench/render_oracle.py` on each: 6/6 PASS (table above).

Not verified: the full efficacy grid (PF-031, separately in progress), and the arm-order
half of the rule under a sample that actually reaches for `select2`.
