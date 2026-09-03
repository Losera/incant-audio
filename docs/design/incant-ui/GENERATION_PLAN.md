# Generating plugin faces in the current stack

What exists, what is missing, and the order to build it. Grounded in the repo at `main`.

## Already landed

`UiIr.h` is at **schema 3**. `Theme` is a struct with Ember defaults per field, `parse()` accepts
`[1,3]`, `colourOr`/`oneOf` degrade per-token, and `toVar()` always writes the `theme` block. The
"never reject the Layout" policy the bone-swatch incident argued for is implemented. Step 1 of the
earlier handoff is done — start at Step 2.

`ParamGridPanel` already computes, per compile, everything the face needs as INPUT:
`deriveLayoutFromGroups()` (canonical section ranking), `derivePalette()`, `deriveComponents()`,
`deriveTitle()`. All four are pure functions of `(params, isInstrument)`, unit-testable without a
panel. The gap is not derivation. It is that nothing consumes `Layout::theme`, and
`layoutSectioned()` places every control as one full-width row at `kCellH`.

## The five gaps

| # | Gap | Where |
|---|---|---|
| 1 | No contrast validation for a theme | nothing yet — new `ThemeValidate.h` |
| 2 | Nothing reads `Layout::theme` | `ParamGridPanel::applyUiIr` ignores it |
| 3 | One LookAndFeel for the whole editor | `PluginEditor.h`'s single `ForgeLookAndFeel lnf` |
| 4 | Sectioned layout is a one-column list | `ParamGridPanel::layoutSectioned()` |
| 5 | No producer — nothing emits the IR | `llm/generate.py` has no `ui` action |

---

## Gap 5 first: the producer

This is the cheapest and the least risky, because the host already ignores what it would produce.

**`recommendation.py` is the exact template.** It is already a second, bounded, non-DSP LLM call
with its own prompt file, its own `parse_and_validate_*` that raises a typed error, hard caps
(`MAX_MODULES`, `MAX_CONTROLS`, `_LIMITS`), and a `recommend_plugin()` that calls
`providers.make_generator(...)` with a small `max_tokens`. Copy that shape into `llm/ui_face.py`:

- `PROMPT_PATH = prompts/ui_face_prompt.md` — the text in `ui_ir_system_prompt.md`.
- `parse_and_validate_face(raw, params, is_instrument) -> dict`, raising `InvalidFace`.
- `generate_face(request, budget=None) -> {"success", "action": "ui_face", "face": {...}}`.

**Dispatch.** `generate.py:650` already reads `action = request.get("action", "generate")` and
`recommend` rides that seam. Add `"ui_face"` the same way. Two contract facts to honour, both from
`llm/CONTRACT.md`: exactly one JSON line on stdout, and always exit 0 in subprocess mode — failure
is a `reason`, never an exit code.

**Do not put this in the DSP prompt.** `prompt_builder.py` already fights for headroom (measured
5177–7258 tokens for one prompt across retries; `_MIN_UNFILTERED_HEADROOM` exists because 191–207
tokens of margin was judged unsafe). Asking the DSP call to also emit a face spends headroom on the
one call that must not fail. A separate call after a successful compile costs nothing when it fails.

**Input is the captured table, not the prompt.** The face call runs post-compile, so it gets real
`ParamInfo` — label, kind, group, range, unit — plus `isInstrument`. That is what makes
"every writable param appears exactly once" checkable rather than aspirational.

**Validation, all host-reachable:**

1. `schema` in `[1,3]`, else drop.
2. Every writable captured param named exactly once; unknown labels dropped.
3. No `Button`/`CheckButton` given a continuous style — PF-005 is structural, keep it structural.
4. No `Kind::Meter` in `controls`.
5. Theme passes contrast (below).
6. Anything failing → `deriveLayoutFromGroups()`. The deterministic layout stays the floor.

**Determinism.** `derivePalette()`'s docstring is careful that WHICH swatch it lands on is not a
promise, only that it is stable. Hold the face to the same standard: same patch, same JSON, but do
not promise a particular archetype across model versions. Cache the face in the state blob keyed on
the source hash so a reopen never re-rolls it.

**Constraints the face must not contradict.** `recommendation.py::constraints_for()` already encodes
three product truths deterministically — mono voice for synth/drum_synth, custom meters not
rendered, granular is live-input not a sample player. The face producer must read the same function,
not restate it in prose: a face promising a polyphony display or a custom meter is a face the host
cannot draw. This is also why `Components::meter` stays derived, never LLM-chosen — PF-052 discards
meters one layer upstream in `ParamPool`, so `deriveComponents()` is correct about a signal that
cannot arrive yet.

## Gap 1: contrast, host-side

New header-only `ThemeValidate.h` beside `Theme.h`. Parse hex/rgba to `juce::Colour`, compute WCAG
relative luminance, enforce text ≥ 7:1, textDim ≥ 4.5:1, accent ≥ 3:1 against `surface`, and
accent ≠ accentAlt ≠ text. Substitute the Ember token for a failing field only. `Theme.h`'s measured
table (17.94 / 5.43 / 6.09 / 11.19 / 5.16 / 9.01) is the fixture that proves the implementation, and
the bone `#f5f0e6` accent is the negative case it must reject.

Do this in C++, not Python. The host must be safe against a hand-written IR and a stale cached face,
not only against today's producer.

## Gaps 2 + 3: the per-face LookAndFeel

`GeneratedFaceLookAndFeel.h`, constructed from a validated `UiIr::Theme`, attached to
`paramGridPanel` **only** — the shell chrome stays Ember Console. Two lifetime rules, both already
load-bearing in this codebase: declare it before `paramGridPanel` in `PluginEditor.h` (reverse
declaration order teardown, the reason `ForgeLookAndFeel lnf` sits where it does), and
`setLookAndFeel(nullptr)` before it dies — `~LookAndFeel()` asserts otherwise.

**Fonts are the one hard constraint.** `Theme::Type` dispatches by exact typeface name onto bytes
embedded via `PluginForgeAssets`, and `Theme.h` documents why `juce::Font` cannot be a file-scope
constant (static-deinit ordering vs `ScopedJuceInitialiser_GUI` — it leaked when tried). So:
`theme.display`/`theme.readout` stay a closed enum over embedded faces, every face token is a
zero-arg function returning a fresh `Font`, and adding a family means adding bytes to the asset
target. Four display faces and one mono covers all four mockups.

## Gap 4: archetype layouts

`ArchetypeLayout.h` — free functions, no JUCE `Component` dependency, mirroring `ParamGridLayout.h`.
Given section count, per-section control counts, spans and a target size, return rectangles.
`synth-panel`/`channel-strip` make sections into columns; `tape-unit` splits transport/tone;
`texture-field` is a display region plus a control rail; `pedal`/`utility` keep the grid.

Keep one height function. `contentHeightForSections()` is already shared by
`contentHeightForCurrentMode()` and `layoutSectioned()` precisely so they cannot disagree — the
kChromeHeight defect shape both headers warn about. Extend that function; do not add a second.

Write `host/tests/ParamGridLayoutTest.cpp`. `ParamGridLayout.h` says the file has never existed and
asks the next reader to write it or delete the claim.

## Order

1. `ThemeValidate.h` + its test — pure, no UI risk.
2. `llm/ui_face.py` + `ui_face` action + parser tests — host still ignores the output.
3. `GeneratedFaceLookAndFeel` wired to `applyUiIr` — faces get their colours; layout unchanged.
4. `ArchetypeLayout.h` + `ParamGridLayoutTest.cpp` — faces get their geometry.
5. Cache the face in the state blob; extend the cockpit export with archetype + theme.
6. `tools/ui_iterate.sh` over the four worked prompts; diff against
   `design_handoff_generated_plugin_faces/screenshots/`.

Steps 1–3 are shippable without 4: a themed sqrt grid already looks unmistakably per-plugin.
