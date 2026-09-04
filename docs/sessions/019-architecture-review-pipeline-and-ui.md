# 019 — Architecture review: generated-plugin UX and the generation pipeline

Lead-architect review requested by the user: (1) assess the four target face mockups and
the two shell directions against the current architecture, (2) judge whether the
generation pipeline should grow toward a
`Intent → Understanding → Planning → DSP IR → Constraint Validation → Generation →
Compilation → Evaluation → Automatic Refinement → Plugin` shape, (3) rule on
Electron/WebView. Grounded against `origin/main` @ `3e79739`/`c50c898`,
`docs/design/incant-ui/` (screenshots read directly, not only described), and
ADR-019/021/022/024/027/029/030/033/035/036. Full analysis in the session's approved
plan; this doc records the findings and decisions that became repository artifacts.

## Findings not previously on record

Read against the mockups and the live code, not restated from the design bundle:

- **F1 — one visual form, three meanings in 2a.** The horizontal accent rule in
  `2a-command-bar-1160.png` serves as a per-control value bar, the output/level meter,
  and the generation progress bar, with no visual distinction between the three. Needs
  resolving (tick/alpha for values, a distinct treatment for progress) before the lattice
  ships, not after.
- **F2 — the 180s generation wait has no staged feedback in either mockup.**
  `PromptPanel.h:407`'s `kSubprocessTimeoutMs = 180 * 1000` and ADR-011's
  non-streaming design mean a multi-minute wait; 2a's progress affordance is a thin bar
  far from the Generate button with no stage text, though stage information already
  exists in the JSON contract. Flagged as the single largest smoothness win available,
  independent of the shell-direction pick.
- **F3 — face-swap-after-audio has no stated rule.** ADR-035 §5's `ui_face` call lands
  after the DSP is already live and audible; nothing says what happens if it arrives
  mid-gesture on the grid. Proposed invariant, not yet decided: apply immediately only if
  it arrives before the first user gesture; otherwise cache to the state blob (already
  persisted, PR #51) and apply at the next patch boundary. **Open — needs a decision
  before A3b (the wiring step) lands**, since A3b is what makes this race live for the
  first time.
- **F4 — unlabeled icon squares in 2a** (`{ }`, `◎`, `⋯` at 30px) are consequential mode
  toggles with no first-use affordance. Text label at ≥900px, tooltips always.
- **F5 — the 1a/1b response plots would be fabricated readings.** The bundle calls them
  "placeholders for real curves the DSP can already provide," but the DSP does not
  provide them today. **Decided this session:** no compile-time response probe is
  authorized; the plot regions ship as non-committal graphics, never implying they show
  this patch's measured response.

## Decisions made this session

1. **Shell direction: 2a, picked now rather than via ADR-036 §1's build-both prototype.**
   Recorded as a same-day amendment to ADR-036 (`docs/decisions.md`), which also replaces
   §2's persistent-right-column error region with a bottom sheet anchored to the prompt
   row. Reasoning, alternatives, and consequences are in the amendment itself — not
   duplicated here.
2. **PF-052 (meter rendering) approved**, satisfying `PLUGIN_HEALTH_PLAN.md` P1.10's
   UI-direction gate. Scheduled after the shell and archetype-layout work, ahead of the
   decorative display widgets that depend on it.
3. **Offline-only Evaluation + Automatic Refinement stage approved**, via a new ADR-037
   (drafted this session, `docs/decisions.md`) — not a live-path critique gate. Persists
   the accepted `recommendation.py` design plan as an acceptance-criteria record and
   drives evaluation from `bench/render_oracle.py`, offline. ADR-027 §1's decline of a
   *live* gate is unchanged.
4. **Not authorized:** the compile-time response probe (F5); any live-path critique gate;
   a DSP IR (ADR-021's 0/19-corpus reopen trigger has not fired — re-confirmed, not
   re-measured, this session); WebView or Electron (ADR-019 stands; the live-signal
   display class in §1's finding is filed as evidence *toward* ADR-019's reopen trigger —
   "generated patches routinely need a control the grid cannot express" — not as the
   trigger firing).

## Pipeline mapping (the "should we build this pipeline" question)

The requested `Intent → ... → Plugin` shape maps onto the existing codebase as:

| Stage | Status | Mechanism |
|---|---|---|
| Human Intent | built | `PromptPanel` |
| Intent Understanding | built, deterministic | `llm/router.py` keyword scoring |
| Planning | built, opt-in | `llm/recommendation.py` (ADR-033) |
| DSP IR | **rejected, unfired trigger** | ADR-021 |
| Constraint Validation | built, deterministic | `recommendation.constraints_for()` |
| Generation | built | `generate_json()` retry loop |
| Compilation | built, two-stage | Faust CLI validate → libfaust JIT + `validatePatch()` |
| Evaluation | offline only, now scoped for extension | `bench/render_oracle.py` |
| Automatic Refinement | compile-error only; semantic is human Add/Redo | ADR-027 §1 |
| Plugin | built | — |

Net: 7 of 10 stages exist. The two declined stages (DSP IR, live critique) both have
ADRs with unfired reopen triggers; nothing in this review changes that. The genuinely
missing piece — an acceptance-criteria record plus an evaluation pass over it — is what
ADR-037 builds, offline.

**ADR-030 tripwire note.** ADR-030 revisits pipeline orchestration only if ≥2 of {a
`faust-rs` error-code advisor (issue #26), an offline critic pass, provider failover}
land. ADR-037 is the second. The issue-#26 work is the first. Landing both triggers
ADR-030's own re-evaluation clause — against a hand-rolled dispatch table, not an
orchestration framework by default. Recorded here so it is not rediscovered cold.

## Not verified

- Whether the generated faces, once built, actually read better than today's grid on a
  real corpus of patches — a looking pass, not a test (COLLABORATION.md §1).
- Whether JUCE 8's `WebBrowserComponent` genuinely changes ADR-019's calculus, mentioned
  as a caveat for any future WebView reopening — not checked against this codebase.
- No code was written this session; this is a review-and-decision session only. Every
  item above is a decision to schedule work, not evidence that the work is correct.

## Next recommended action

Per the approved plan: `feat/shell-command-bar` (2a per the ADR-036 amendment) can start
once ADR-035's A4 (`ArchetypeLayout.h`) lands, since 2a's lattice consumes the same free
functions. ADR-037 needs to be filled in with a concrete state-blob field and evaluation
entry point before it moves from Proposed to Accepted — drafted, not implemented, this
session.
