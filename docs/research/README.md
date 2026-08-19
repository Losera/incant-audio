# docs/research

**Frozen point-in-time index, 2026-07-25. Not maintained** — each linked note is itself a frozen
record; verdicts below are what was true that day, not a live status board.

Session 2 lane R, 2026-07-25. Written against the adversarial audit of the same date.
Every claim cites `file:line` or a URL; every repo figure has a reproduction command in
its own note.

| Note | Verdict |
|---|---|
| [truncation-confound-HANDOFF-S1](truncation-confound-HANDOFF-S1.md) | **Fixed 2026-07-25 (`07d0997`).** `max_tokens=1024` with no truncation detection invalidated the recorded compile rates and burned the retry budget on a loop that could not succeed. The *pipeline* is fixed; the *confound* stands until the study is re-run |
| [R1-grammar-constrained-decoding](R1-grammar-constrained-decoding.md) | **No-go.** Covers 36% of failures, needs self-hosting no provider supports, and a static pre-flight check covers 50% for free |
| [R2-restricted-faust-dialect](R2-restricted-faust-dialect.md) | **Do not fork Faust.** The restricted dialect already exists (59 of 1,511 symbols) — it is just never enforced on output |
| [R3-perceptual-oracle](R3-perceptual-oracle.md) | **Shipped:** `bench/render_oracle.py`. 17/17 renderable patches produce usable audio. CLAP is the wrong instrument for effects |
| [R4-training-our-own-model](R4-training-our-own-model.md) | **No.** 4 GB VRAM, ~67 semantically unlabelled examples. The scarce asset is measurement, not model capability |
| [R5-publishable-run](R5-publishable-run.md) | **Both 2026 venues closed.** Three confounds to fix first; target the 2027 cycle |

Lane W's `docs/workflow/recommendation.md` (the AXI-conformant `generate.py` pilot proposal) was
deleted 2026-08-19, never adopted — see `docs/records/doc-purge-2026-08-19.md`.

Published: [The 1024-Token Ceiling](https://claude.ai/code/artifact/b6233afc-4898-4c44-8c95-927ad86f934b) ·
[One Pilot](https://claude.ai/code/artifact/277d4397-17b2-4813-ba9f-b0b586f8ac2a)
