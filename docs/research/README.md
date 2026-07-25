# docs/research

Session 2 lane R, 2026-07-25. Written against the adversarial audit of the same date.
Every claim cites `file:line` or a URL; every repo figure has a reproduction command in
its own note.

| Note | Verdict |
|---|---|
| [truncation-confound-HANDOFF-S1](truncation-confound-HANDOFF-S1.md) | **Action for S1.** `max_tokens=1024` with no truncation detection invalidates the recorded compile rates and burns the retry budget on a loop that cannot succeed |
| [R1-grammar-constrained-decoding](R1-grammar-constrained-decoding.md) | **No-go.** Covers 36% of failures, needs self-hosting no provider supports, and a static pre-flight check covers 50% for free |
| [R2-restricted-faust-dialect](R2-restricted-faust-dialect.md) | **Do not fork Faust.** The restricted dialect already exists (59 of 1,511 symbols) — it is just never enforced on output |
| [R3-perceptual-oracle](R3-perceptual-oracle.md) | **Shipped:** `bench/render_oracle.py`. 17/17 renderable patches produce usable audio. CLAP is the wrong instrument for effects |
| [R4-training-our-own-model](R4-training-our-own-model.md) | **No.** 4 GB VRAM, ~67 semantically unlabelled examples. The scarce asset is measurement, not model capability |
| [R5-publishable-run](R5-publishable-run.md) | **Both 2026 venues closed.** Three confounds to fix first; target the 2027 cycle |

Lane W is separate: [`docs/workflow/recommendation.md`](../workflow/recommendation.md).

Published: [The 1024-Token Ceiling](https://claude.ai/code/artifact/b6233afc-4898-4c44-8c95-927ad86f934b) ·
[One Pilot](https://claude.ai/code/artifact/277d4397-17b2-4813-ba9f-b0b586f8ac2a)
