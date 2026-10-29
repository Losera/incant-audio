# PF-032 noise gate — groq re-measurement, 2026-10-20

WP2 (`fedb377`, 2026-09-15) landed the att/hold/rel SECONDS rule + a worked few-shot, and
measured a clean 6/6 free-tier (ollama) result against a confound-corrected control's 5/6
broken. The one step it left open: the shipping model, groq. This closes that gap.

## Method

Same methodology as WP1's original re-measurement (`7ba7d8c`): six fresh generations of
the exact prompt "a noise gate with threshold and hold time," against the live provider,
each checked for whether it renders silent via `bench/render_oracle.py`.

- Provider: groq, `openai/gpt-oss-120b` (the shipping default, `PLUGINFORGE_PROVIDER=groq`).
- Prompt file at generation time: `llm/prompts/system_prompt.txt` as it stood on
  `fix/pf079-oracle-red-cases` (includes WP2's gate fix plus this session's PF-079
  dynamics-safety rule — the two edits don't interact, the gate few-shot and the
  expander/limiter rule are in different STRICT RULES clauses).

## Result: 6/6 pass

| Trial | thresh | hold | Oracle |
|---|---|---|---|
| 1 | raw dB literal | `*(0.001)` from ms | PASS, rms -0.0dB |
| 2 | raw dB literal | `*(0.001)` from ms | PASS, rms -0.0dB |
| 3 | raw dB literal | `*(0.001)` from ms | PASS, rms -0.0dB |
| 4 | raw dB literal | `*(0.001)` from ms | PASS, rms -0.0dB |
| 5 | raw dB literal | `*(0.001)` from ms | PASS, rms -0.0dB |
| 6 | raw dB literal | `*(0.001)` from ms | PASS, rms -0.0dB |

All six: `thresh` is always a clean raw dB literal (never pre-converted through
`ba.db2linear`), `hold` is always correctly scaled from an ms slider via `*(0.001)`
(never left as raw ms, never run through `ma.SR`). `rms -0.0dB` means the gate passes the
oracle's always-above-threshold test signal through at its input level -- the correct
behavior for an open gate, not the "exactly 0.0, rendered silent" failure WP1/WP2 were
measuring against.

**6/6, matching WP2's free-tier figure exactly.** PF-032 is reclassified `fixed`.

## Verification

- [x] 6 live groq generations via `llm/generate.py --prompt "a noise gate with threshold and hold time"`.
- [x] Each rendered through `bench/render_oracle.py`: 6/6 PASS (table above).

Not verified: a broader sweep of gate/compressor prompt phrasings beyond this exact one,
or the instrument-profile path (this prompt routes to the effect profile).
