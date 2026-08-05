touches:  new files under bench/ only -- a new script (e.g.
          bench/check_layered_voice_generalization.py) plus a results JSON it writes under
          bench/results/. Do NOT touch bench/results/.prompt_baseline.json under any
          circumstance.
depends:  llm/CONTRACT.md -- the generate.py guarantees section and the provider
          assumptions section. Read it first; do not violate its stated guarantees.
provides: none

This is an EVIDENCE brief, not a feature brief. The goal is to either DELETE or NARROW (with
a measured bound) one specific claim currently sitting in STATUS.md's "Assumed, never
checked" section: that "the layered-voice control-exposure prompt fix generalizes beyond the
2 prompts it was tested on."

## Read first

1. STATUS.md, IN FULL, to find the exact current wording of this claim and confirm it is
   still present in that form.
2. llm/CONTRACT.md -- the generate.py guarantees and provider-assumptions sections.
3. Identify "the layered-voice control-exposure prompt fix": commit bc061ff
   ("instrument_prompt.txt: a layered voice needs its own control, always") is the fix in
   question. Run `git show bc061ff` and read the full diff to know exactly what "exposed"
   means mechanically -- e.g. whether each layer gets its own named Faust slider vs. one
   shared control. Do not guess this from the commit message alone; read the diff.
4. llm/providers.py, to confirm the free-only provider rule (gemini / groq / openrouter /
   local ollama). Per ADR-012 (docs/decisions.md), never use anthropic here --
   PLUGINFORGE_ALLOW_PAID must NOT be set for this brief's runs.

## Task

Design and run a small generalization study:

1. Generate a batch of at least 8-10 NEW layered-voice instrument prompts -- distinct from
   whatever 2 prompts were originally used to validate the bc061ff fix -- against a free LLM
   provider.
2. For each generation, deterministically check (do not eyeball) whether the per-layer
   controls were actually exposed as separate Faust parameters. Grep the compiled/generated
   Faust source or the parameter list for the fix's expected pattern, using the mechanical
   definition of "exposed" you extracted from the bc061ff diff in step 3 above.
3. Record pass/fail per prompt.

## Mechanics

This work is $0 / free-tier and produces information only -- it is UNGATED under this
project's consult-gate rule (a gate belongs on the destructive write, never on the run that
produces information).

- Take the quota lock before running. Reuse acquire_lock/release_lock from
  bench/run_benchmark.py (also imported the same way in bench/score_efficacy.py around
  lines 42-50) to avoid colliding with any other quota-spending run.
- Write results to a NEW json file under bench/results/, following the naming pattern of
  existing files there (e.g. bench/results/layered_voice_generalization_<something>.json).
  bench/results/ is in check_doc_naming.py's EXEMPT_DIRS list, so a dated filename is fine
  there and even preferred for this kind of archived run.
- Do NOT touch bench/results/.prompt_baseline.json under any circumstance.
- Produce a clear number: N prompts tested, M passed (controls exposed correctly).

## End state

- A runnable script under bench/.
- A results JSON under bench/results/ recording the N prompts, per-prompt pass/fail, and
  the provider/model used.
- A one-paragraph verdict, stated plainly: is this enough evidence to delete the STATUS.md
  claim entirely, or only to narrow it (e.g. "generalizes across N layered-voice prompts of
  style X; untested for style Y")? Write this paragraph so the calling session can drop it
  directly into STATUS.md's rewrite.

## Out of scope

Do not edit STATUS.md yourself -- that is the calling session's job once your verdict
paragraph exists. Do not touch bench/results/.prompt_baseline.json. Do not use a paid
provider. Do not touch any file outside bench/.
