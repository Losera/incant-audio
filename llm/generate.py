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
import anthropic

load_dotenv(Path(__file__).parent.parent / ".env")

DEFAULT_PROVIDER = "anthropic"
# Bumped from claude-opus-4-6 2026-07-21. Benchmark numbers recorded before this
# date (the 88% ADR-009 baseline, the 50-generation tier pilot) are opus-4-6-era
# and are not directly comparable — see docs/prompt_efficacy_study.md.
#
# NOTE: opus-4-7 and later reject temperature/top_p/top_k and the old
# thinking={"type": "enabled", "budget_tokens": N} form with a 400. _call_api()
# sends none of those, which is why this bump is a plain string swap. Anything
# added to the request below must respect that.
DEFAULT_MODEL = "claude-opus-4-8"

SYSTEM_PROMPT = (Path(__file__).parent / "prompts" / "system_prompt.txt").read_text()

# Default Anthropic client — kept at module level so existing tests can mock it.
# Verified 2026-07-20: anthropic.Anthropic() does not raise when no API key is
# present (it only stores None); the SDK's "Could not resolve authentication
# method" error is raised lazily, at request-send time. So this stays eager —
# no need for a get_client() indirection.
client = anthropic.Anthropic()


def _call_api(content: str, provider: str, model: str) -> str:
    """Single API dispatch point. Add new providers here."""
    if provider == "anthropic":
        response = client.messages.create(
            model=model,
            max_tokens=1024,
            system=SYSTEM_PROMPT,
            messages=[{"role": "user", "content": content}],
        )
        return response.content[0].text.strip()
    raise ValueError(f"Unknown provider: {provider!r}")


def generate_faust(user_prompt: str, error_context: str = "",
                   provider: str = DEFAULT_PROVIDER,
                   model: str = DEFAULT_MODEL) -> str:
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
                        model: str = DEFAULT_MODEL) -> str:
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
    model = request.get("model", DEFAULT_MODEL)
    max_retries = request.get("max_retries", 3)

    error_ctx = ""
    for attempt in range(1, max_retries + 1):
        code = generate_faust(prompt, error_ctx, provider, model)
        valid, error = validate_faust(code)
        if valid:
            return {"success": True, "faust_code": code, "attempts": attempt, "error": None}
        error_ctx = error

    return {"success": False, "faust_code": None, "attempts": max_retries, "error": error_ctx}


def _missing_api_key_response() -> dict:
    """ADR-011 failure shape for a missing/empty ANTHROPIC_API_KEY."""
    return {
        "success": False,
        "faust_code": None,
        "attempts": 0,
        "error": "ANTHROPIC_API_KEY is not set. Add it to PluginForge/.env or "
                 "the plugin's environment.",
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
    if not os.environ.get("ANTHROPIC_API_KEY"):
        print(json.dumps(_missing_api_key_response()))
        return
    try:
        response = generate_json(build_request())
        print(json.dumps(response))
    except Exception as exc:  # noqa: BLE001 - convert to ADR-011 JSON, never a stdout traceback
        traceback.print_exc(file=sys.stderr)
        print(json.dumps(_exception_response(exc)))


if __name__ == "__main__":
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
