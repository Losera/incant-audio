# llm/ Contract

## generate.py guarantees (subprocess mode: `--json` / `--prompt` / `--request-file`)
- Exactly ONE JSON line on stdout — "the ADR-011 contract" (`_run_subprocess_mode`
  docstring, generate.py:590). Response schema: `{success, faust_code, attempts,
  error, reason}`, plus ADDITIVE `kind`, `prior_source_dropped`,
  `prior_source_refused` (generate_json docstring, :338-362; the refusal field's
  own reasoning is at `_prior_source_refused_response`, :470-488 — the ADR-011
  `reason` enum stays closed, so a refusal is discriminated by this flag, not a
  new `reason` value).
- Request schema (never previously written down here — there is no shared
  header, INTERFACE.md's whole premise): `prompt` (required), and optional
  `provider`, `model`, `max_retries`, `budget`, `kind`, `prior_source`,
  `refine_mode`. `refine_mode` is `"surgical" | "context" | absent` and picks
  which preamble folds `prior_source` in (`_refine_preamble_for`, :200-210) —
  absent is the legacy `_REFINE_PREAMBLE` path, unchanged since before
  `refine_mode` existed. `"surgical"` failing the token-budget preflight
  short-circuits to the refusal response above instead of `generate_faust` ever
  running; `"context"` and absent keep the older soft-drop
  (`prior_source_dropped: true`, prior_source silently cleared).
- Always exits 0 in subprocess mode (:590-591): failure is signalled by
  `reason`, never by exit code. NOT enforced for an exception at MODULE
  IMPORT (:26-47) — those precede every try/except and exit non-zero with a
  bare traceback on stderr. UNTESTED invariant.
- `faust_code` is `faust`-validated (`validate_faust`, :271) before
  `success` is ever true — never merely "the model's last answer".
- Diagnostics and tracebacks go to stderr only (:593), never stdout.

## Actions (`process_json_request`, request key `action`)
Absent / `"generate"` is the Faust path above. Two additive metadata-to-metadata
actions ride the same one-JSON-line / always-exit-0 contract but never produce
`faust_code`; each stamps its own name into the response `action` key on every
path, and each carries an action-specific `reason` because ADR-011's `reason`
enum is closed to the generation path.
- `"recommend"` (ADR-033) — pre-generation design plan. Request adds `kind`,
  `family`. Success: `recommendation` (the typed plan). Failure `reason`:
  `invalid_recommendation` | `target_mismatch` (with `recommended_kind`) |
  `error`.
- `"ui_face"` (ADR-035 Step 5) — **post-compile** UI face. Request adds `params`
  (required, non-empty: the captured table — `label`, `kind`, `group`, `min`,
  `max`, `default`, `unit`), `is_instrument`. Success: `face` — a normalised
  UiIr schema-3 object, or `{"schema": 0}` when the model declined. Failure
  `reason`: `invalid_face` | `error`. The host re-validates the face and falls
  back to `deriveLayoutFromGroups()` on anything it rejects — contrast
  validation is C++ only (`ThemeValidate.h`), so a hand-written or stale-cached
  IR is checked too. A slow or failed call must never delay the DSP going live.

## What it assumes about providers (llm/providers.py)
- One dispatch point, `make_generator()`. Providers signal failure through
  three provider-agnostic exceptions — `RateLimited`, `BudgetExhausted`,
  `OutputTruncated` — which the retry loop catches by class (:336-349),
  never by a provider-specific type.
- Free-only is enforced ONCE, at `__main__` (generate.py:626) — not inside
  `generate_faust`/`generate_with_retry`. A caller that imports this module
  as a library (the bench harnesses) gets NO free-tier guard.

## router.py — keyword scoring, NOT a model call
Deterministic regex scoring: no network call, no quota spent, microseconds
(router.py:12-16, "WHY IT IS NOT AN LLM CALL"). `classify()` never raises;
empty input and a scoring TIE both resolve to EFFECT, the safe default
(:82,96-99). A future session must not assume routing costs a token, can
rate-limit, or needs the retry/budget machinery `generate_faust` has.

## Violations
- The "always exits 0" promise (generate.py:590) is not honored for any
  exception raised before `_run_subprocess_mode` runs — see IMPORT above.
- `generate_json`'s docstring calls the request schema "locked" (:341), but
  `kind`, `prior_source_dropped`, and now `prior_source_refused` were added
  to the RESPONSE since, with no schema version field, so an old caller
  cannot detect which shape it received. `refine_mode` grows the same
  problem on the request side that "locked" claims doesn't exist.
- `router.explain()` is dead code from the host's perspective: nothing in
  `generate_json` calls it, so a misroute's evidence never reaches a log
  a human could read — only `classify()`'s bare verdict does.
