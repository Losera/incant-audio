# R4 — Should we train or fine-tune our own model?

**Frozen point-in-time record, 2026-07-25. Not maintained.** See `docs/research/README.md`.

**Verdict: No. Not "not yet" for the fine-tuning question — no.** One adjacent thing *is*
realistic and it is not a model. Written 2026-07-25, session 2 lane R.

The audit explicitly invited a clear negative here: *"A clear 'no, and here's why' is a
valid and useful answer."* This is that.

---

## 1. The compute, measured

Dev box, 2026-07-25:

| | |
|---|---|
| GPU | **NVIDIA RTX A2000 Laptop, 4096 MiB VRAM** |
| System RAM | 31 GiB (≈21 GiB available) |
| Installed ML stack | none — no `torch`, no `transformers`, no `outlines` |

4 GB of VRAM is the whole argument. It admits inference on a 3–7B model at 4-bit
quantisation. It does not admit fine-tuning anything at a size that would help: even
QLoRA on a 7B model wants ~10–12 GB for activations and optimiser state at usable
sequence lengths, and Faust programs plus a 173-line system prompt are not short
sequences.

## 2. The data does not exist either, and that is the harder problem

Suppose the compute appeared. A supervised fine-tune needs (prompt → correct Faust) pairs.
The project's total holdings:

| Source | Records | Usable as training data? |
|---|---:|---|
| `bench/results/results.json` | 25 | 22 compile; **none verified to match its prompt** |
| `bench/results/efficacy/pilot_20260720.json` | 50 | 45 valid; same problem, plus tier-confounded |
| `bench/results/efficacy/full_20260720_*INVALID*.json` | 125 | **zero data** — all requests rejected pre-generation |
| Recorded real user prompts | **0** | PF-014: `generate.py` logs nothing |

Call it ~67 examples, none of which has ever been checked for semantic correctness
(PF-013), on a corpus whose labels are compile-success — a signal we now know is
contaminated by output truncation ([[truncation-confound-HANDOFF-S1]]). Fine-tuning on
that teaches a model to produce *compilable* Faust, which is the thing that is already at
88%, using labels that are partly wrong.

The `examples/*.dsp` directory and the Faust stdlib itself are better raw material, but
they are unpaired — Faust source without the natural-language request that should have
produced it. Generating those pairs synthetically means asking a larger model, which is
what the project already does at inference time, for free.

## 3. The NeurIPS code-to-audio-alignment path

*Embedding Alignment in Code Generation for Audio* (Kouteili, Madhu, Typaldos,
Santolucito — NeurIPS 2025 AI4Music workshop, arXiv 2508.05473) learns a map from code
embeddings to audio embeddings and finds the relationship is non-linear. The audit
correctly identifies it as the nearest published work to this project's thesis.

Training a comparable alignment model is a smaller ask than fine-tuning an LLM — it is a
projection head over frozen embeddings, not a language model. It is still a no here, for
a reason that has nothing to do with compute: **it needs audio embeddings for the code,
and until 2026-07-25 this project could not render audio at all.** That capability now
exists for effects ([[R3-perceptual-oracle]]) and does not exist for generators. Building
an alignment model on top of a renderer that covers 80% of the corpus, with ~67
semantically unlabelled examples, would be a model trained on noise.

## 4. What is realistic, and it is not a model

The honest reframing: **this project's scarce asset is not model capability, it is
measurement.** Every failure this lane examined was a measurement or plumbing defect —
truncation counted as syntax error, billing errors counted as compile failures, semantic
fidelity never measured at all. None was "the model isn't good enough at Faust". The
compile ceiling is 88%; a fine-tune chasing 92% would be optimising the one axis that is
already in decent shape while the semantic axis is unmeasured.

Three things in this space *are* reachable for a solo developer on free-tier compute, in
order of value:

1. **Per-prompt expected spectral signatures** ([[R3-perceptual-oracle]] §6). Converts the
   existing corpus into semantically labelled data. Costs a day, no GPU, no quota. This is
   also the prerequisite for every item below.
2. **A cross-model comparison** (closes PF-012, ADR-008, "Under evaluation" since
   2026-04-29). Three free providers over the same corpus, scored on compile *and*
   semantics. No training — just measurement the registry already supports via a flag.
3. **Publishing the evaluation apparatus itself.** The audit's own read is that the
   tiered prompt corpus + scoring taxonomy + compile oracle is the research asset, and
   the thing a product company is least likely to build. A benchmark contribution needs no
   GPU and no PhD, and it is a stronger artifact than a marginally better fine-tune.

## 5. Revisit conditions

- The dev box acquires ≥16 GB VRAM **and** the corpus has ≥500 semantically-labelled
  examples. Both, not either.
- Or: the generator-rendering gap closes and a *reference-free* semantic score exists for
  the whole corpus — at which point the alignment-model question becomes genuinely
  interesting rather than premature.

Related: [[R3-perceptual-oracle]], [[R5-publishable-run]],
[[truncation-confound-HANDOFF-S1]].
