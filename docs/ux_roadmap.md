# PluginForge UX Roadmap: from one-shot generation to iterative development

Status: DRAFT for human review (DELEGATE). Companion doc: `docs/ui_design_plan.md`
(the UI mechanics; this doc is the delivery sequence).

**Framing.** PluginForge is a no-code tool for musicians: type a sentence, get a working
plugin. But a one-shot generator is a demo, not an instrument-building tool. The roadmap
below grows the ability to *continue developing* an already-generated plugin — refine it,
compare versions, save it, and (for advanced users) open the hood on the Faust source in
an embedded code editor. Every later phase depends on Phase 1.

---

## Phase 1 — State persistence (enabling step)

**Everything downstream is blocked on this.** Today `getStateInformation` /
`setStateInformation` are empty stubs (`host/Source/PluginProcessor.h:30-31`): closing a
DAW project discards the compiled DSP, the Faust source, the prompt, and all knob values.
The processor doesn't even retain the source after `loadFaustCode()` hands it to the
engine (`PluginProcessor.cpp:74-91`), so there is nothing to save yet.

**Spec.**
- Serialize via `juce::ValueTree` → XML (the APVTS already owns a "STATE" tree,
  `PluginProcessor.cpp:8`):
  - current Faust source (processor must start retaining it — new member)
  - originating prompt (editor must start passing it alongside the code — today
    `loadFaustCode()` takes only the code)
  - APVTS param values (all 64 `macro_*` slots)
  - slot-label mapping (which slot → which Faust label, mirroring what
    `ParamPool::remap()` published)
  - schema version int (so later phases can migrate old blobs)
- `setStateInformation`: restore param values, then trigger `loadFaustCode()` on the
  stored source to recompile — the DSP itself is never serialized, the source is the
  artifact of record.
- Lifecycle interaction: the restore-time recompile rides the existing compile/swap
  protocol — background compile thread, audioBusy drain guard, callback-before-ready
  ordering (`FaustEngine.h:38,53-64`). `processBlock` itself is untouched; until the
  swap lands it early-outs to passthrough exactly as it does today
  (`PluginProcessor.cpp:60-64`).
- Edge cases to spec with the human: setState arriving before `prepareToPlay`;
  setState racing an in-flight user-triggered compile; empty/corrupt blob.

**Engagement mode: PAIR — the human must confirm this classification before
implementation.** Rationale: it touches the compile/swap lifecycle and DAW host
contracts (call ordering of setState/prepare), which the human must own mentally for
future debugging, even though no audio-thread code changes. On confirmation this becomes
**P-series task P11**.

---

## Phase 2 — Iterate & refine

Depends on: Phase 1 (prompt/source retention).

| Item | What | Mode |
|---|---|---|
| Prompt history | Persisted list of past prompts (rides the Phase-1 blob); dropdown or up-arrow recall in the prompt box | DELEGATE |
| "Refine" action | Send current Faust source + the user's delta request ("make the resonance stronger") to the LLM; result flows through the existing `loadFaustCode()` path | split — see below |
| A/B toggle | Keep the last two successful compiles' sources; one-click toggle recompiles the other (swap protocol already handles live switching, `docs/prototype_test_plan.md` step 6) | DELEGATE |
| Named snapshots | User-named {prompt, source, param values} tuples inside the state blob; precursor to Phase-4 presets | DELEGATE |

**Refine (landed 2026-08-04** — `--request-file` CLI mode, `prior_source` folded into the
existing user message via `llm/generate.py`'s `_REFINE_PREAMBLE`, threaded through
`PromptPanel`'s existing subprocess bridge; see
`docs/sessions/002-refine-loop-and-ui-redesign.md` and the dated ADR-011 amendment in
`docs/decisions.md`). No sibling prompt file was needed — three reasons are recorded in the
session doc, the shortest being that `select_prompt()` returns exactly one file, so a
second one would have to duplicate the ~11,400-char stdlib block to keep output compiling.
The DELEGATE/PROPOSED/HUMAN-OWNED vocabulary this paragraph used to gate on was retired by
COLLABORATION.md §9 (2026-07-21): a prompt edit is Tier 2 evidence-bar work under
COLLABORATION.md §3 now, not an authorship gate, and this paragraph was still describing
the old gate three sessions after it stopped existing.

---

## Phase 3 — Embedded code editor (advanced users)

Depends on: Phase 1 (source retention); pairs naturally with Phase 2.

**Step 3a — read-only code view (DELEGATE).** A collapsible panel showing the current
Faust source via `juce::CodeEditorComponent` + `juce::CodeDocument`. Gap: JUCE ships
only C++ and XML tokenizers — there is no Faust tokenizer. Start with the generic/no-op
tokenizer (monochrome), then a custom-lite Faust tokenizer (keywords, comments, strings)
as a follow-on.

**Step 3b — editable + Compile button (PAIR for the wiring review).** Hand-edited code
goes through the *exact same* `loadFaustCode()` path (`PluginProcessor.cpp:74`) as LLM
output — same JIT, same validation-by-compilation, same swap safety envelope, same
`onFaustCompileError`/`onFaustCompileSuccess` surfacing. No second code path.

**Step 3c — error line-highlighting (DELEGATE).** Faust stderr carries line numbers
(the retry loop already feeds that stderr back to the LLM, `docs/architecture.md`
pipeline); parse them and highlight the offending line in the code view instead of
truncating to 200 chars in the status label (`PluginEditor.cpp:246`).

**Risks / open questions**
- Window size rework: a code panel does not fit 480×410 (`PluginEditor.cpp:7`) —
  requires the resizable-window work from `docs/ui_design_plan.md` §3.
- Undo/redo: `CodeDocument` has its own undo manager; interaction with the APVTS undo
  story is unspecified.
- Prompt/code divergence: once a user hand-edits, the stored prompt no longer describes
  the code. Store both, plus a `handEdited` flag; Refine (Phase 2) must send the *code*
  as ground truth, treating the prompt as history only.

---

## Phase 4 — Presets, sharing, model selection

Depends on: Phase 1 (blob format); Phase 2 snapshots are the natural seed.

- **Preset browser** over Phase-1 state blobs: list, load, rename, delete. DELEGATE.
- **Export/import `.pforge` files**: the state blob as a file, so users can share
  generated plugins as text-sized artifacts. Needs schema-version checks on import.
  DELEGATE.
- **Model-selection UI**: dropdown feeding the `model` key `generate_json()` already
  accepts (`llm/generate.py:92`). Ties to **ADR-008 (Claude vs Gemini — Status: Under
  evaluation, `docs/decisions.md:189`)**: build the UI provider-agnostic and do NOT
  presuppose ADR-008's outcome — the choice list ships only after the ADR resolves.
  UI work DELEGATE; the provider/model policy itself is the ADR's to decide
  (HUMAN-OWNED).

---

## Phase dependencies

```
                 Phase 1: State persistence  (P11, PAIR)
                /            |             \
               v             v              v
   Phase 2: Iterate     Phase 3: Code      Phase 4: Presets /
   & refine             editor             sharing / model UI
   (prompt history,     (read-only view -> (browser, .pforge,
    refine, A/B,         editable ->        model dropdown
    snapshots)           err highlight)     after ADR-008)
               \             |
                v            v
              Phase 2 "Refine" uses hand-edited
              code as ground truth (divergence rule)
```

## Phase → mode → P-series map

| Phase | Engagement mode | P-series |
|---|---|---|
| 1 — State persistence | PAIR (human to confirm classification) | **P11** |
| 2 — Iterate & refine | DELEGATE, except refine prompt wording: HUMAN-OWNED | unnumbered |
| 3 — Embedded code editor | 3a/3c DELEGATE; 3b wiring PAIR | unnumbered |
| 4 — Presets / sharing / model | DELEGATE; model policy gated on ADR-008 (HUMAN-OWNED) | unnumbered |
