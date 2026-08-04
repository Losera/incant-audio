# llm/ Contract

## generate.py guarantees (subprocess mode: `--json` / `--prompt` / `--request-file`)
- Exactly ONE JSON line on stdout — "the ADR-011 contract" (generate.py:488).
  Schema: `{success, faust_code, attempts, error, reason}`, plus ADDITIVE
  `kind`, `prior_source_dropped` (generate_json docstring, :285-302).
- Always exits 0 in subprocess mode (:489-490): failure is signalled by
  `reason`, never by exit code. NOT enforced for an exception at MODULE
  IMPORT (:26-47) — those precede every try/except and exit non-zero with a
  bare traceback on stderr. UNTESTED invariant.
- `faust_code` is `faust`-validated (`validate_faust`, :213) before
  `success` is ever true — never merely "the model's last answer".
- Diagnostics and tracebacks go to stderr only (:491), never stdout.

## What it assumes about providers (llm/providers.py)
- One dispatch point, `make_generator()`. Providers signal failure through
  three provider-agnostic exceptions — `RateLimited`, `BudgetExhausted`,
  `OutputTruncated` — which the retry loop catches by class (:336-349),
  never by a provider-specific type.
- Free-only is enforced ONCE, at `__main__` (:524) — not inside
  `generate_faust`/`generate_with_retry`. A caller that imports this module
  as a library (the bench harnesses) gets NO free-tier guard.

## router.py — keyword scoring, NOT a model call
Deterministic regex scoring: no network call, no quota spent, microseconds
(router.py:12-16, "WHY IT IS NOT AN LLM CALL"). `classify()` never raises;
empty input and a scoring TIE both resolve to EFFECT, the safe default
(:82,96-99). A future session must not assume routing costs a token, can
rate-limit, or needs the retry/budget machinery `generate_faust` has.

## Violations
- The "always exits 0" promise (generate.py:489) is not honored for any
  exception raised before `_run_subprocess_mode` runs — see IMPORT above.
- `generate_json`'s docstring calls the response schema "locked" (:283),
  but `kind` and `prior_source_dropped` were added since with no schema
  version field, so an old caller cannot detect which shape it received.
- `router.explain()` is dead code from the host's perspective: nothing in
  `generate_json` calls it, so a misroute's evidence never reaches a log
  a human could read — only `classify()`'s bare verdict does.
