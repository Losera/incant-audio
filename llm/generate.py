#!/usr/bin/env python3
"""
Entry point: natural language prompt -> validated Faust DSL string.

Run modes:
  python generate.py "a low-pass filter"   — CLI, prints Faust to stdout
  python generate.py --json                 — JSON mode, reads request from stdin,
                                             writes response to stdout (legacy)
  python generate.py --prompt "..."         — arg mode, outputs JSON to stdout;
                                             used by the C++ host via juce::ChildProcess
"""
import datetime
import json
import os
import subprocess
import sys
import tempfile
import traceback
from pathlib import Path
from dotenv import load_dotenv

sys.path.insert(0, str(Path(__file__).parent))
import providers  # noqa: E402

load_dotenv(Path(__file__).parent.parent / ".env")

# Resolved at import, AFTER load_dotenv — so PLUGINFORGE_PROVIDER in PluginForge/.env
# reaches the plugin (which shells out to this script) with no C++ change and no
# rebuild. Falls back to "anthropic" when unset, preserving the historical default.
DEFAULT_PROVIDER = providers.resolve_provider()
# Per-provider; see llm/providers.py PROVIDERS. For anthropic this is
# claude-opus-4-8 (bumped from opus-4-6 2026-07-21) — benchmark numbers recorded
# before that date are opus-4-6-era and not directly comparable, see
# docs/prompt_efficacy_study.md.
#
# NOTE: opus-4-7 and later reject temperature/top_p/top_k with a 400. This layer
# passes temperature=None (the parameter is omitted entirely), which is why the
# model bump was a plain string swap. providers.make_generator() raises with an
# actionable message if a caller ever passes a temperature to such a model.
DEFAULT_MODEL = providers.resolve_model(DEFAULT_PROVIDER)

SYSTEM_PROMPT = (Path(__file__).parent / "prompts" / "system_prompt.txt").read_text()

# Default Anthropic client — kept at module level so existing tests can mock it.
# Verified 2026-07-20: anthropic.Anthropic() does not raise when no API key is
# present (it only stores None); the SDK's "Could not resolve authentication
# method" error is raised lazily, at request-send time. So this stays eager —
# no need for a get_client() indirection.
#
# SUBTLE: this is providers.anthropic_client()'s singleton, not a second client.
# Tests patch generate.client.messages.create; because it is the same object, the
# patch is still seen by calls dispatched through the providers module.
client = providers.anthropic_client()


# ── Generation budget (PF-019) ───────────────────────────────────────────────
# The wall-clock ceiling for ONE `--prompt` invocation, covering all attempts.
#
# This number's only real constraint is that it must sit COMFORTABLY BELOW the
# host's subprocess cap (kSubprocessTimeoutMs in host/Source/PromptPanel.cpp).
# Before PF-019 those two were both 120s, so whichever fired first was a race,
# and in the 2026-07-24 P6 battery the host won four times running — killing the
# child before it could emit the ADR-011 JSON that would have said *why*. The
# host cap is now a backstop for a wedged interpreter, not the normal exit path:
# generate.py finishes first and reports a typed reason.
#
# Margin arithmetic, so the next person changing either number can check it:
#   worst case here  = GENERATION_BUDGET_S (all LLM work, budget-enforced)
#                    + one FAUST_VALIDATE_TIMEOUT_S (the last attempt's compile)
#                    + interpreter startup/import
#                    = 100 + 15 + ~2  = ~117s
#   host cap         = 180s
_DEFAULT_GENERATION_BUDGET_S = 100.0
FAUST_VALIDATE_TIMEOUT_S = 15.0


def generation_budget() -> providers.Budget:
    """One Budget per generation, sized so max_retries attempts fit inside it."""
    try:
        total = float(os.environ.get("PLUGINFORGE_GENERATION_BUDGET",
                                     _DEFAULT_GENERATION_BUDGET_S))
    except ValueError:
        total = _DEFAULT_GENERATION_BUDGET_S
    # 3 attempts inside the total, with the faust compile of each budgeted out.
    per_attempt = max(10.0, (total / 3.0) - FAUST_VALIDATE_TIMEOUT_S / 3.0)
    return providers.Budget(total=total, per_attempt_cap=per_attempt)


# Re-exported so existing callers and tests can keep saying generate.MAX_OUTPUT_TOKENS.
# It lives in providers.py because the bench harnesses need it too and must not have
# to import this module (which resolves a provider and reads the prompt at import).
MAX_OUTPUT_TOKENS = providers.MAX_OUTPUT_TOKENS

# Sent instead of compiler stderr when the previous attempt was cut off. A
# truncated program never reached `faust`, so there is no compile error to feed
# back — and feeding one anyway is precisely the bug this replaces: the model was
# told it made a syntax error it had not made, so it rewrote a program of the same
# length, which truncated again.
_TRUNCATION_HINT = (
    "\n\nYour previous answer was CUT OFF at the output limit before it finished — "
    "it was not wrong, it was too long. Write a SHORTER program this time: no "
    "comments, fewer helper definitions, and prefer a stdlib function over "
    "reimplementing one. Emit only the Faust source."
)


def _call_api(content: str, provider: str, model: str | None = None,
              budget: "providers.Budget | None" = None) -> str:
    """Single API dispatch point — delegates to the llm/providers.py registry."""
    generate = providers.make_generator(
        provider, system_prompt=SYSTEM_PROMPT, model=model,
        max_tokens=MAX_OUTPUT_TOKENS, budget=budget,
    )
    return generate(content)


def generate_faust(user_prompt: str, error_context: str = "",
                   provider: str = DEFAULT_PROVIDER,
                   model: str | None = None,
                   budget: "providers.Budget | None" = None,
                   truncated: bool = False) -> str:
    """One generation attempt.

    `truncated` and `error_context` are mutually exclusive repair signals, and the
    caller must not send both: code that was cut off mid-token never reached the
    compiler, so any stderr in hand belongs to an earlier attempt and would
    misdirect the model.
    """
    content = user_prompt
    if truncated:
        content += _TRUNCATION_HINT
    elif error_context:
        content += f"\n\nYour previous output had this compiler error — fix it:\n{error_context}"
    return _call_api(content, provider, model, budget)


def validate_faust(faust_code: str,
                   budget: "providers.Budget | None" = None) -> tuple[bool, str]:
    """Returns (is_valid, error_message).

    PF-019: with a Budget, the compiler subprocess timeout is clamped to what is
    left. `faust` on a pathological generated patch is one more way to overrun the
    host's cap, so it counts against the same clock as the LLM calls.
    """
    # SUBTLE: encoding and errors are both load-bearing, and neither is a default.
    # An LLM reply can contain non-ASCII (curly quotes, em dashes, a degree sign in
    # a slider label). When it does, `faust` echoes the offending source line to
    # stderr but cuts it mid-character, emitting a lone 0xe2 — invalid UTF-8.
    # With the default errors="strict", subprocess.run(text=True) then RAISES
    # UnicodeDecodeError instead of returning, so this function never gets to
    # report a compile failure and the ADR-005 retry loop never engages: the whole
    # generation dies on what should have been a recoverable attempt.
    # Reproduced 2026-07-21 against faust on the Arch dev box; regression test in
    # tests/test_faust_validator_unit.py::test_non_utf8_compiler_output.
    with tempfile.NamedTemporaryFile(suffix=".dsp", mode="w", encoding="utf-8",
                                     delete=False) as f:
        f.write(faust_code)
        tmp = f.name
    timeout = FAUST_VALIDATE_TIMEOUT_S
    if budget is not None:
        timeout = max(2.0, min(timeout, budget.remaining()))
    try:
        result = subprocess.run(
            ["faust", "-lang", "cpp", tmp, "-o", "/dev/null"],
            capture_output=True, text=True, timeout=timeout,
            encoding="utf-8", errors="replace",
        )
        return result.returncode == 0, result.stderr.strip()
    except subprocess.TimeoutExpired:
        # A patch the compiler cannot finish is an invalid patch, not a crash.
        # Report it as a compile error so the retry loop can feed it back.
        return False, (f"faust compiler did not finish within {timeout:.0f}s — "
                       f"the generated patch is likely pathological "
                       f"(unbounded delay, runaway recursion).")
    finally:
        os.unlink(tmp)


def generate_with_retry(user_prompt: str, max_retries: int = 3,
                        provider: str = DEFAULT_PROVIDER,
                        model: str | None = None) -> str:
    """Returns validated Faust code string. Raises RuntimeError after exhausting retries."""
    error_ctx = ""
    truncated = False
    budget = generation_budget()
    for attempt in range(1, max_retries + 1):
        try:
            code = generate_faust(user_prompt, error_ctx, provider=provider, model=model,
                                  budget=budget, truncated=truncated)
        except providers.OutputTruncated as exc:
            print(f"[!] Attempt {attempt} was cut off at the output limit: {exc}\n")
            truncated, error_ctx = True, ""
            continue
        truncated = False
        valid, error = validate_faust(code, budget)
        if valid:
            print(f"[+] Valid Faust on attempt {attempt}")
            return code
        print(f"[!] Attempt {attempt} failed:\n{error}\n")
        error_ctx = error
    raise RuntimeError("Failed to generate valid Faust after retries.")


def generate_json(request: dict) -> dict:
    """
    JSON wire-mode entry point called by the C++ host via subprocess.
    Accepts the locked ADR-011 request schema; returns the response schema.

    ADR-011 response, with one ADDITIVE field as of PF-019:

        {"success": bool, "faust_code": str|None, "attempts": int,
         "error": str|None, "reason": str}

    `reason` is one of ok | invalid_faust | truncated | timeout | rate_limited | error.

    `truncated` means the model ran out of output budget mid-program on every
    attempt. It is deliberately distinct from invalid_faust: the code never reached
    the compiler, the model did nothing wrong, and the user-facing advice is
    "ask for something simpler", not "the generator produced bad Faust". Conflating
    the two is what made four consecutive P6 prompts look like generation failures.
    Additive because every existing consumer reads success/faust_code/error and is
    unaffected by an extra key; the host uses it to tell "the provider throttled
    you, wait a moment" apart from "something stalled", which was indistinguishable
    during the 2026-07-24 battery. Treating this as ungated under COLLABORATION.md
    §2 trigger-3: it extends the schema without changing any existing field's
    meaning, and both sides move in this commit.
    """
    prompt = request["prompt"]
    provider = request.get("provider", DEFAULT_PROVIDER)
    # None → providers.resolve_model() picks the selected provider's default, so a
    # request naming a provider but no model can't inherit another provider's model.
    model = request.get("model")
    max_retries = request.get("max_retries", 3)
    budget = request.get("budget") or generation_budget()

    error_ctx = ""
    truncated = False
    attempt = 0
    for attempt in range(1, max_retries + 1):
        try:
            code = generate_faust(prompt, error_ctx, provider, model, budget,
                                  truncated=truncated)
        except providers.RateLimited as exc:
            return _failure(attempt, "rate_limited", str(exc))
        except providers.BudgetExhausted as exc:
            return _failure(attempt, "timeout", str(exc))
        except providers.OutputTruncated as exc:
            # Recoverable, and specifically NOT a compile failure — the code never
            # reached the compiler. Retry asking for a shorter program rather than
            # feeding back stderr that belongs to nothing.
            truncated, error_ctx = True, ""
            if attempt == max_retries:
                return _failure(attempt, "truncated", str(exc))
            if budget.expired():
                return _failure(attempt, "timeout", str(exc))
            continue

        truncated = False
        valid, error = validate_faust(code, budget)
        if valid:
            return {"success": True, "faust_code": code, "attempts": attempt,
                    "error": None, "reason": "ok"}
        error_ctx = error

        # Don't start an attempt there is no time to finish — returning the last
        # compiler error now beats being killed mid-request with nothing to show.
        if budget.expired() and attempt < max_retries:
            return _failure(attempt, "timeout",
                            f"generation budget exhausted after {attempt} attempt(s). "
                            f"Last compiler error:\n{error_ctx}")

    return _failure(max_retries, "invalid_faust", error_ctx)


def _failure(attempts: int, reason: str, error: str) -> dict:
    return {"success": False, "faust_code": None, "attempts": attempts,
            "error": error, "reason": reason}


def _missing_api_key_response(message: str | None = None) -> dict:
    """ADR-011 failure shape for a missing/empty credential.

    SUBTLE: the default message is the anthropic one, kept verbatim — the host
    surfaces this string to the user and tests/test_generate_unit.py asserts it
    character-for-character as part of the ADR-011 wire contract. Other providers
    pass their own message (naming their own env var) explicitly.
    """
    return {
        "success": False,
        "faust_code": None,
        "attempts": 0,
        "error": message or ("ANTHROPIC_API_KEY is not set. Add it to PluginForge/.env or "
                             "the plugin's environment."),
        "reason": "no_credentials",
    }


def _exception_response(exc: BaseException) -> dict:
    """ADR-011 failure shape for an unexpected exception in a subprocess mode."""
    reason = "error"
    if isinstance(exc, providers.RateLimited):
        reason = "rate_limited"
    elif isinstance(exc, providers.BudgetExhausted):
        reason = "timeout"
    return {"success": False, "faust_code": None, "attempts": 0,
            "error": str(exc), "reason": reason}


def _prompt_log_path() -> Path | None:
    """Where real user prompts get recorded, or None when logging is off.

    `PLUGINFORGE_PROMPT_LOG` unset -> the default path. Set to a path -> that path.
    Set to 0/off/false/no/"" -> disabled.
    """
    raw = os.environ.get("PLUGINFORGE_PROMPT_LOG")
    if raw is None:
        return Path(__file__).parent.parent / "logs" / "prompts.jsonl"
    if raw.strip().lower() in {"", "0", "off", "false", "no"}:
        return None
    return Path(raw).expanduser()


def log_user_prompt(request: dict, response: dict) -> None:
    """Append one JSONL record per real user generation. PF-014.

    WHY: until 2026-07-28 nothing in this project had ever recorded what a person
    actually typed. Every one of the 25 benchmark prompts was invented by us, so the
    corpus measured our imagination of the product rather than its use, and no
    failure class could be weighted by how often it really occurs.

    WHERE: called only from _run_subprocess_mode, which is the sole path the C++ host
    invokes (--json / --prompt). The bench harnesses call generate_faust /
    generate_with_retry directly and never reach here, so benchmark prompts cannot
    contaminate the record of real ones.

    FAIL-OPEN, deliberately, unlike this project's hooks. A hook exists to stop the
    work; this exists to observe it. A full disk or a read-only path must cost the
    user a log line, never their generation — so every error is swallowed to stderr.
    """
    path = _prompt_log_path()
    if path is None:
        return
    try:
        record = {
            "ts": datetime.datetime.now(datetime.timezone.utc)
                          .isoformat(timespec="seconds").replace("+00:00", "Z"),
            "prompt": request.get("prompt", ""),
            "provider": request.get("provider", DEFAULT_PROVIDER),
            "model": request.get("model") or DEFAULT_MODEL,
            "success": bool(response.get("success")),
            "reason": response.get("reason", "error"),
            "attempts": response.get("attempts", 0),
            "faust_code": response.get("faust_code"),
            "error": response.get("error"),
        }
        path.parent.mkdir(parents=True, exist_ok=True)
        # One open/append/close per generation: generations are seconds apart and
        # arrive one process at a time, so a single O_APPEND write is atomic enough
        # and leaves no handle to lose on a crash.
        with path.open("a", encoding="utf-8") as fh:
            fh.write(json.dumps(record, ensure_ascii=False) + "\n")
    except Exception as exc:  # noqa: BLE001 — see FAIL-OPEN above
        print(f"[prompt-log] not recorded: {exc}", file=sys.stderr)


def _run_subprocess_mode(build_request):
    """
    Shared body for the --json and --prompt ADR-011 subprocess entry points.

    `build_request` is a zero-arg callable that produces the request dict for
    generate_json() — called only after the API-key precheck passes, so a
    malformed stdin payload (--json mode) is caught by the try/except below
    rather than escaping as an uncaught exception before the precheck.

    Prints exactly one JSON line to stdout (the ADR-011 contract) and always
    exits 0 — the host parses the JSON regardless of exit code, and a nonzero
    exit would mask the structured error the JSON already carries. All
    diagnostics/tracebacks go to stderr, never stdout.
    """
    # Provider-aware precheck: names whichever credential the selected provider
    # actually needs, and is skipped entirely for local ollama (which needs none).
    credential_error = providers.check_credentials(DEFAULT_PROVIDER)
    if credential_error:
        legacy_text = DEFAULT_PROVIDER == "anthropic"
        print(json.dumps(_missing_api_key_response(None if legacy_text else credential_error)))
        return
    request = None
    try:
        request = build_request()
        response = generate_json(request)
        log_user_prompt(request, response)          # PF-014
        print(json.dumps(response))
    except Exception as exc:  # noqa: BLE001 - convert to ADR-011 JSON, never a stdout traceback
        traceback.print_exc(file=sys.stderr)
        response = _exception_response(exc)
        # A prompt that blew up is the most interesting kind to have recorded. Only
        # skipped when build_request() itself failed, i.e. there is no prompt to log.
        if request is not None:
            log_user_prompt(request, response)
        print(json.dumps(response))


if __name__ == "__main__":
    # Free-only guard. Lives here rather than in the library functions so that
    # every path that can actually spend money is covered (the plugin invokes this
    # script as a subprocess) without blocking unit tests that drive the functions
    # directly with a mocked transport. See llm/providers.py.
    _subprocess_mode = "--json" in sys.argv or "--prompt" in sys.argv
    try:
        providers.assert_free(DEFAULT_PROVIDER)
    except providers.PaidProviderError as exc:
        if _subprocess_mode:
            # ADR-011: exactly one JSON line on stdout, exit 0 — the host reads the
            # structured error and shows it as a status label.
            print(json.dumps(_exception_response(exc)))
            sys.exit(0)
        print(f"[!] {exc}", file=sys.stderr)
        sys.exit(1)

    if "--json" in sys.argv:
        _run_subprocess_mode(lambda: json.loads(sys.stdin.read()))
    elif "--prompt" in sys.argv:
        idx = sys.argv.index("--prompt")
        prompt_arg = sys.argv[idx + 1] if idx + 1 < len(sys.argv) else ""
        _run_subprocess_mode(lambda: {"prompt": prompt_arg})
    else:
        prompt = " ".join(a for a in sys.argv[1:] if not a.startswith("-")) \
                 or "a warm analog chorus with rate and depth controls"
        print(f"Prompt: {prompt}\n")
        result = generate_with_retry(prompt)
        print("\n── Generated Faust ──")
        print(result)
