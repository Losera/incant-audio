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


def _call_api(content: str, provider: str, model: str | None = None) -> str:
    """Single API dispatch point — delegates to the llm/providers.py registry."""
    generate = providers.make_generator(
        provider, system_prompt=SYSTEM_PROMPT, model=model, max_tokens=1024
    )
    return generate(content)


def generate_faust(user_prompt: str, error_context: str = "",
                   provider: str = DEFAULT_PROVIDER,
                   model: str | None = None) -> str:
    content = user_prompt
    if error_context:
        content += f"\n\nYour previous output had this compiler error — fix it:\n{error_context}"
    return _call_api(content, provider, model)


def validate_faust(faust_code: str) -> tuple[bool, str]:
    """Returns (is_valid, error_message)."""
    with tempfile.NamedTemporaryFile(suffix=".dsp", mode="w", delete=False) as f:
        f.write(faust_code)
        tmp = f.name
    try:
        result = subprocess.run(
            ["faust", "-lang", "cpp", tmp, "-o", "/dev/null"],
            capture_output=True, text=True, timeout=15,
        )
        return result.returncode == 0, result.stderr.strip()
    finally:
        os.unlink(tmp)


def generate_with_retry(user_prompt: str, max_retries: int = 3,
                        provider: str = DEFAULT_PROVIDER,
                        model: str | None = None) -> str:
    """Returns validated Faust code string. Raises RuntimeError after exhausting retries."""
    error_ctx = ""
    for attempt in range(1, max_retries + 1):
        code = generate_faust(user_prompt, error_ctx, provider=provider, model=model)
        valid, error = validate_faust(code)
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
    """
    prompt = request["prompt"]
    provider = request.get("provider", DEFAULT_PROVIDER)
    # None → providers.resolve_model() picks the selected provider's default, so a
    # request naming a provider but no model can't inherit another provider's model.
    model = request.get("model")
    max_retries = request.get("max_retries", 3)

    error_ctx = ""
    for attempt in range(1, max_retries + 1):
        code = generate_faust(prompt, error_ctx, provider, model)
        valid, error = validate_faust(code)
        if valid:
            return {"success": True, "faust_code": code, "attempts": attempt, "error": None}
        error_ctx = error

    return {"success": False, "faust_code": None, "attempts": max_retries, "error": error_ctx}


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
    }


def _exception_response(exc: BaseException) -> dict:
    """ADR-011 failure shape for an unexpected exception in a subprocess mode."""
    return {"success": False, "faust_code": None, "attempts": 0, "error": str(exc)}


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
    try:
        response = generate_json(build_request())
        print(json.dumps(response))
    except Exception as exc:  # noqa: BLE001 - convert to ADR-011 JSON, never a stdout traceback
        traceback.print_exc(file=sys.stderr)
        print(json.dumps(_exception_response(exc)))


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
