#!/usr/bin/env python3
"""
Provider registry — the one place any PluginForge component gets an LLM callable.

Every call site (llm/generate.py, bench/run_benchmark.py, bench/run_efficacy_study.py,
bench/score_efficacy.py) used to build its own client inline. They now all go through
make_generator(), so adding a provider is a one-line registry entry, not a fourth
copy of a client constructor.

Doctor CLI — costs nothing, model-list endpoints are not billed generations:

    python llm/providers.py --check all
    python llm/providers.py --check gemini

Free-only rule
--------------
`anthropic` is the only paid entry in the registry and is marked free=False. The
runnable entry points (each module's __main__, and the bench preflights) call
assert_free(), which refuses to proceed without PLUGINFORGE_ALLOW_PAID=1.

  SUBTLE: the guard lives in assert_free(), called from __main__ blocks — NOT inside
  make_generator(). Every path that can actually spend money is a CLI invocation (the
  plugin shells out to `generate.py --prompt`), so __main__ is complete coverage,
  while library functions stay drivable by unit tests that mock the transport.

Selection
---------
    PLUGINFORGE_PROVIDER   gemini | groq | openrouter | ollama | anthropic
    PLUGINFORGE_MODEL      override the registry's default model id
    PLUGINFORGE_ALLOW_PAID 1 to permit the paid provider
    PLUGINFORGE_MIN_INTERVAL  seconds to space calls apart (free-tier RPM caps)

generate.py loads PluginForge/.env before reading these, and juce::ChildProcess
inherits the environment, so setting PLUGINFORGE_PROVIDER in .env reaches the plugin
without a rebuild.
"""
import json
import os
import re
import sys
import time
from dataclasses import dataclass, field

import httpx

DEFAULT_PROVIDER = "anthropic"

# Free-tier request budgets as advertised 2026-07; they move, and every provider
# below rate-limits by account age/region. Treat as ordering hints, not contracts.
_HTTP_TIMEOUT = 120.0
_MAX_BACKOFF_TRIES = 5


class PaidProviderError(RuntimeError):
    """Raised by assert_free() when a paid provider is selected without opt-in."""


@dataclass(frozen=True)
class ProviderSpec:
    name: str
    kind: str                      # "anthropic" | "gemini" | "openai_compat"
    default_model: str
    env_var: str | None = None     # None → no credential needed (local ollama)
    base_url: str | None = None    # openai_compat only
    strip_fences: bool = True
    free: bool = True
    signup_url: str = ""
    notes: str = ""
    # Models that reject a `temperature` parameter outright (400). Prefix match.
    no_temperature_models: tuple[str, ...] = field(default_factory=tuple)
    # Floor applied to the caller's max_tokens. Reasoning models bill hidden
    # thinking tokens against the SAME cap, so a caller asking for "1024 tokens of
    # Faust" needs a bigger raw budget to actually receive it. Measured 2026-07-21
    # on gemini-3.6-flash: max_output_tokens=1024 → finish=MAX_TOKENS, 981 thinking
    # tokens, 39 visible tokens, output truncated mid-sentence. At 4096 → finish=STOP,
    # 1016 thinking + 164 visible, valid Faust.
    min_max_tokens: int = 0


PROVIDERS: dict[str, ProviderSpec] = {
    "gemini": ProviderSpec(
        name="gemini",
        kind="gemini",
        # Verified live 2026-07-21. The 2.5-* family now 404s for new accounts
        # ("no longer available to new users"), so do not pin it. `gemini-flash-latest`
        # is an alias tracking the current flash model — handy, but pin an explicit id
        # for benchmark reproducibility.
        default_model="gemini-3.6-flash",
        env_var="GOOGLE_API_KEY",
        signup_url="https://aistudio.google.com/apikey",
        min_max_tokens=4096,
        notes="Free tier, no credit card. Measured 2026-07-21 on this account: "
              "5 requests/MINUTE and only 20 requests/DAY, both PER MODEL. Quotas "
              "are per-model, so switching PLUGINFORGE_MODEL gets a fresh daily "
              "budget. Far too small for a 125-prompt study — use groq for volume.",
    ),
    "groq": ProviderSpec(
        name="groq",
        kind="openai_compat",
        default_model="openai/gpt-oss-120b",
        env_var="GROQ_API_KEY",
        base_url="https://api.groq.com/openai/v1",
        signup_url="https://console.groq.com",
        min_max_tokens=4096,
        notes="Free tier, no credit card, up to ~14,400 RPD — the volume option for "
              "the efficacy study. Model choice here is load-bearing; all four "
              "candidates were measured against a real faust compile 2026-07-21 on "
              "the L4 anchor prompt:\n"
              "  gpt-oss-120b  COMPILES clean — the default.\n"
              "  gpt-oss-20b   spends the ENTIRE output budget on hidden reasoning "
              "and returns EMPTY content at 1024/4096/8192 alike (reasoning_tokens "
              "tracks max_tokens-2), reaching Faust as an empty .dsp: 'syntax "
              "error, unexpected $end'. Raising max_tokens does not fix it; "
              "reasoning_effort='low' does. Never default to it.\n"
              "  llama-3.3-70b-versatile  emits no reasoning tokens, but "
              "hallucinates stdlib (ba.log2linear, ba.linear2log) and writes ': * "
              "gain'. NOTE it is NOT deprecated — an earlier note here said so, "
              "wrongly; it is live.\n"
              "  qwen/qwen3.6-27b  overruns 4096 without finishing.\n"
              "TPM cap is 8000 for gpt-oss-120b: max_tokens above ~7500 returns 413 "
              "rate_limit_exceeded, so 4096 is chosen to sit between the reasoning "
              "floor and that ceiling.",
    ),
    "openrouter": ProviderSpec(
        name="openrouter",
        kind="openai_compat",
        default_model="deepseek/deepseek-chat-v3-0324:free",
        env_var="OPENROUTER_API_KEY",
        base_url="https://openrouter.ai/api/v1",
        signup_url="https://openrouter.ai/keys",
        notes="Many ':free' models behind one key, but only ~50 free-model requests "
              "per day until the account has spent $10. Good for a smoke test.",
    ),
    "ollama": ProviderSpec(
        name="ollama",
        kind="openai_compat",
        default_model="qwen2.5-coder:7b",
        env_var=None,
        base_url="http://localhost:11434/v1",
        signup_url="https://ollama.com",
        notes="Fully local: no key, no quota, works offline, can never be billing "
              "blocked. Needs `sudo pacman -S ollama` + `ollama pull <model>`.",
    ),
    "anthropic": ProviderSpec(
        name="anthropic",
        kind="anthropic",
        default_model="claude-opus-4-8",
        env_var="ANTHROPIC_API_KEY",
        # The tuned system prompt already forbids markdown, and every recorded
        # benchmark number was measured without stripping. Leaving this False keeps
        # the historical 0.88 baseline bit-comparable.
        strip_fences=False,
        free=False,
        signup_url="https://console.anthropic.com",
        notes="PAID. Requires PLUGINFORGE_ALLOW_PAID=1.",
        no_temperature_models=("claude-opus-4-7", "claude-opus-4-8"),
    ),
}


# ── Selection ─────────────────────────────────────────────────────────────────

def get_spec(provider: str) -> ProviderSpec:
    try:
        return PROVIDERS[provider]
    except KeyError:
        known = ", ".join(sorted(PROVIDERS))
        raise ValueError(f"Unknown provider: {provider!r} (known: {known})") from None


def resolve_provider(explicit: str | None = None) -> str:
    """explicit argument > PLUGINFORGE_PROVIDER > DEFAULT_PROVIDER."""
    name = explicit or os.environ.get("PLUGINFORGE_PROVIDER") or DEFAULT_PROVIDER
    name = name.strip()
    get_spec(name)  # validate eagerly so a typo fails at selection, not mid-run
    return name


def resolve_model(provider: str, explicit: str | None = None) -> str:
    """explicit argument > PLUGINFORGE_MODEL > the registry default."""
    return explicit or os.environ.get("PLUGINFORGE_MODEL") or get_spec(provider).default_model


def supports_temperature(provider: str, model: str) -> bool:
    return not any(model.startswith(p) for p in get_spec(provider).no_temperature_models)


def check_credentials(provider: str) -> str | None:
    """Returns an error message, or None when the provider is usable.

    Providers with env_var=None (local ollama) always pass — there is no key.
    """
    spec = get_spec(provider)
    if spec.env_var is None:
        return None
    if not os.environ.get(spec.env_var):
        return (f"{spec.env_var} is not set — add it to PluginForge/.env "
                f"(get a key at {spec.signup_url})")
    return None


def assert_free(provider: str) -> None:
    """Enforce the free-only rule. Called from runnable entry points only."""
    spec = get_spec(provider)
    if spec.free or os.environ.get("PLUGINFORGE_ALLOW_PAID") == "1":
        return
    free_names = ", ".join(sorted(n for n, s in PROVIDERS.items() if s.free))
    raise PaidProviderError(
        f"Provider {provider!r} is a paid API and PluginForge is configured free-only. "
        f"Set PLUGINFORGE_PROVIDER in PluginForge/.env to one of: {free_names} "
        f"(see llm/README.md), or set PLUGINFORGE_ALLOW_PAID=1 to override."
    )


# ── Response cleanup ──────────────────────────────────────────────────────────

def strip_code_fences(text: str) -> str:
    """Return the first fenced code block's body, or the text unchanged.

    Open-weight models (llama/qwen/deepseek) habitually wrap output in ```faust
    fences, which fails `faust -lang cpp` on line 1. The tuned system prompts
    forbid this, but they are HUMAN-OWNED product IP (COLLABORATION.md §1), so the
    cleanup belongs here rather than in a prompt edit.

    Tolerates a missing closing fence — a truncated response still yields its code.
    """
    stripped = text.strip()
    if "```" not in stripped:
        return stripped

    after_open = stripped.split("```", 1)[1]
    # Drop the info string ("faust", "cpp", "") on the remainder of the fence line.
    body = after_open.split("\n", 1)[1] if "\n" in after_open else ""
    body = body.split("```", 1)[0].strip()

    # Never let a parsing quirk turn a real response into an empty string.
    return body or stripped


# ── Adapters ──────────────────────────────────────────────────────────────────

_anthropic_client = None


def anthropic_client():
    """Process-wide anthropic client.

    SUBTLE: llm/generate.py binds its module-level `client` to this same object, so
    tests that patch generate.client.messages.create still intercept calls made
    through this module (anthropic's `.messages` is a cached_property, so the
    Messages instance is identical across accesses).
    """
    global _anthropic_client
    if _anthropic_client is None:
        import anthropic
        _anthropic_client = anthropic.Anthropic()
    return _anthropic_client


def _make_anthropic(spec, model, system_prompt, temperature, max_tokens):
    client = anthropic_client()

    def generate(user_message: str) -> str:
        kwargs = {
            "model": model,
            "max_tokens": max_tokens,
            "system": system_prompt,
            "messages": [{"role": "user", "content": user_message}],
        }
        if temperature is not None:
            kwargs["temperature"] = temperature
        response = client.messages.create(**kwargs)
        return response.content[0].text.strip()

    return _call_with_retry(generate)


def _make_gemini(spec, model, system_prompt, temperature, max_tokens):
    # Lifted from bench/run_benchmark.py's gen_gemini (2026-07 vintage, working).
    from google import genai as gai
    from google.genai import types as gai_types

    client = gai.Client(api_key=os.environ.get(spec.env_var, ""))
    config_kwargs = {
        "system_instruction": system_prompt,
        "max_output_tokens": max_tokens,
    }
    if temperature is not None:
        config_kwargs["temperature"] = temperature

    def generate(user_message: str) -> str:
        response = client.models.generate_content(
            model=model,
            contents=user_message,
            config=gai_types.GenerateContentConfig(**config_kwargs),
        )
        return (response.text or "").strip()

    return _call_with_retry(generate)


def _make_openai_compat(spec, model, system_prompt, temperature, max_tokens):
    """Serves groq, openrouter and ollama — all three speak /chat/completions."""
    api_key = os.environ.get(spec.env_var, "") if spec.env_var else "local"
    url = f"{spec.base_url}/chat/completions"
    headers = {"Authorization": f"Bearer {api_key}", "Content-Type": "application/json"}

    def generate(user_message: str) -> str:
        payload = {
            "model": model,
            "max_tokens": max_tokens,
            "messages": [
                {"role": "system", "content": system_prompt},
                {"role": "user", "content": user_message},
            ],
        }
        if temperature is not None:
            payload["temperature"] = temperature
        data = _post_with_backoff(url, headers, payload)
        try:
            return (data["choices"][0]["message"]["content"] or "").strip()
        except (KeyError, IndexError, TypeError) as exc:
            raise RuntimeError(
                f"{spec.name}: unexpected response shape: {json.dumps(data)[:300]}"
            ) from exc

    return generate


_ADAPTERS = {
    "anthropic": _make_anthropic,
    "gemini": _make_gemini,
    "openai_compat": _make_openai_compat,
}


# ── Transport: backoff + free-tier pacing ─────────────────────────────────────

_last_call_at = 0.0


def _pace() -> None:
    """Space calls by PLUGINFORGE_MIN_INTERVAL seconds — free tiers cap RPM."""
    global _last_call_at
    try:
        interval = float(os.environ.get("PLUGINFORGE_MIN_INTERVAL", "0"))
    except ValueError:
        interval = 0.0
    if interval > 0:
        wait = interval - (time.monotonic() - _last_call_at)
        if wait > 0:
            time.sleep(wait)
    _last_call_at = time.monotonic()


def _retry_after_seconds(response, attempt: int) -> float:
    header = response.headers.get("Retry-After") if response is not None else None
    if header:
        try:
            return float(header)
        except ValueError:
            pass
    return min(2.0 ** attempt, 60.0)


# Both forms Google returns: "'retryDelay': '54s'" and "Please retry in 54.0051s".
_RETRY_HINT_RE = re.compile(r"(?:retryDelay['\"]?[:=]\s*['\"]?|retry in\s+)(\d+(?:\.\d+)?)s")

# Word-bounded so a status code can't false-match inside an unrelated number
# ("max_tokens: 1500" must not read as a 500).
_RETRYABLE_STATUS_RE = re.compile(r"\b(429|500|502|503|504)\b")
_RETRYABLE_PHRASES = ("resource_exhausted", "rate limit", "rate_limit", "quota",
                      "overloaded", "retrydelay", "retry in", "try again",
                      "timeout", "temporarily unavailable")


# A *daily* quota is not worth retrying: the server still sends a ~60s retryDelay,
# but the window doesn't reopen for hours. Measured 2026-07-21 — Gemini's free tier
# returned quotaId "GenerateRequestsPerDayPerProjectPerModel-FreeTier", quotaValue 20,
# alongside retryDelay '58s'. Honoring that hint burns five minutes to fail anyway.
_DAILY_QUOTA_MARKERS = ("perday", "per day", "requests per day", "daily limit",
                        "daily quota")


def _is_daily_quota(exc: BaseException) -> bool:
    lowered = str(exc).lower().replace("-", "")
    return any(marker in lowered for marker in _DAILY_QUOTA_MARKERS)


def _is_retryable(exc: BaseException) -> bool:
    text = str(exc)
    if _is_daily_quota(exc):
        return False
    if _RETRYABLE_STATUS_RE.search(text):
        return True
    lowered = text.lower()
    # A server that tells us WHEN to retry is by definition telling us to retry.
    return any(phrase in lowered for phrase in _RETRYABLE_PHRASES)


def _call_with_retry(call):
    """Wrap an SDK call with the same backoff the raw-HTTP path gets.

    The google-genai and anthropic SDKs raise typed exceptions rather than
    returning a status code, so _post_with_backoff can't cover them. Free tiers
    throttle hard (Gemini: 5 requests/minute/model, measured 2026-07-21), which
    makes this load-bearing for any multi-prompt run, not a nicety.
    """
    def wrapped(*args, **kwargs):
        for attempt in range(_MAX_BACKOFF_TRIES):
            _pace()
            try:
                return call(*args, **kwargs)
            except Exception as exc:  # noqa: BLE001 — re-raised below if not retryable
                if attempt == _MAX_BACKOFF_TRIES - 1 or not _is_retryable(exc):
                    raise
                hint = _RETRY_HINT_RE.search(str(exc))
                # +1s: the server's own hint is the earliest permissible moment.
                delay = float(hint.group(1)) + 1.0 if hint else min(2.0 ** attempt, 60.0)
                time.sleep(delay)
        raise AssertionError("unreachable")  # pragma: no cover

    return wrapped


def _post_with_backoff(url: str, headers: dict, payload: dict) -> dict:
    """POST with exponential backoff on 429/5xx. Free tiers throttle aggressively."""
    last_error = ""
    for attempt in range(_MAX_BACKOFF_TRIES):
        _pace()
        try:
            response = httpx.post(url, headers=headers, json=payload, timeout=_HTTP_TIMEOUT)
        except httpx.RequestError as exc:
            last_error = f"{type(exc).__name__}: {exc}"
            time.sleep(min(2.0 ** attempt, 30.0))
            continue

        if response.status_code == 200:
            return response.json()

        last_error = f"HTTP {response.status_code}: {response.text[:300]}"
        if response.status_code == 429 or response.status_code >= 500:
            if attempt < _MAX_BACKOFF_TRIES - 1:
                time.sleep(_retry_after_seconds(response, attempt))
                continue
        break

    raise RuntimeError(f"Request to {url} failed after {_MAX_BACKOFF_TRIES} attempts — {last_error}")


# ── The seam every call site uses ─────────────────────────────────────────────

def make_generator(provider: str, *, system_prompt: str, model: str | None = None,
                   temperature: float | None = None, max_tokens: int = 1024):
    """Returns callable(user_message) -> code string.

    temperature=None omits the parameter entirely (required by claude-opus-4-7+,
    which reject it with a 400). Passing a temperature to such a model raises here
    with an actionable message rather than failing at request time.
    """
    spec = get_spec(provider)
    model = resolve_model(provider, model)

    if temperature is not None and not supports_temperature(provider, model):
        raise ValueError(
            f"{model} rejects a `temperature` parameter — pass temperature=None, or "
            f"choose a model that supports it (any free provider does)."
        )

    # See ProviderSpec.min_max_tokens: reasoning models spend this budget on hidden
    # thinking before emitting a single visible token.
    max_tokens = max(max_tokens, spec.min_max_tokens)

    generate = _ADAPTERS[spec.kind](spec, model, system_prompt, temperature, max_tokens)

    if not spec.strip_fences:
        return generate

    def generate_clean(user_message: str) -> str:
        return strip_code_fences(generate(user_message))

    return generate_clean


# ── Model discovery ───────────────────────────────────────────────────────────

def list_models(provider: str) -> list[str]:
    """Live model ids from the provider. Not a billed generation."""
    spec = get_spec(provider)

    if spec.kind == "openai_compat":
        api_key = os.environ.get(spec.env_var, "") if spec.env_var else "local"
        response = httpx.get(
            f"{spec.base_url}/models",
            headers={"Authorization": f"Bearer {api_key}"},
            timeout=30.0,
        )
        response.raise_for_status()
        return sorted(m["id"] for m in response.json().get("data", []))

    if spec.kind == "gemini":
        from google import genai as gai
        client = gai.Client(api_key=os.environ.get(spec.env_var, ""))
        return sorted(m.name.removeprefix("models/") for m in client.models.list())

    return sorted(m.id for m in anthropic_client().models.list(limit=50).data)


# ── Doctor CLI ────────────────────────────────────────────────────────────────

def _check_one(provider: str) -> dict:
    spec = get_spec(provider)
    report = {
        "provider": provider,
        "free": spec.free,
        "credentials": check_credentials(provider) or "ok",
        "model": resolve_model(provider),
        "models_found": None,
        "status": "",
    }
    if report["credentials"] != "ok":
        report["status"] = "no key"
        return report
    try:
        models = list_models(provider)
        report["models_found"] = len(models)
        default = report["model"]
        report["status"] = "ok" if default in models else f"model {default!r} NOT in live list"
    except Exception as exc:  # noqa: BLE001 — the doctor reports failures, never raises
        report["status"] = f"unreachable: {type(exc).__name__}: {exc}"[:140]
    return report


def _print_reports(reports: list[dict]) -> None:
    header = f"{'PROVIDER':<12} {'FREE':<5} {'KEY':<6} {'MODELS':<7} {'MODEL / STATUS'}"
    print(header)
    print("─" * len(header))
    for r in reports:
        key = "ok" if r["credentials"] == "ok" else "—"
        found = str(r["models_found"]) if r["models_found"] is not None else "—"
        detail = r["model"] if r["status"] == "ok" else r["status"]
        print(f"{r['provider']:<12} {'yes' if r['free'] else 'PAID':<5} "
              f"{key:<6} {found:<7} {detail}")
    print()
    print("Set PLUGINFORGE_PROVIDER (and optionally PLUGINFORGE_MODEL) in PluginForge/.env.")


def main(argv=None) -> int:
    import argparse
    from pathlib import Path
    from dotenv import load_dotenv

    load_dotenv(Path(__file__).parent.parent / ".env")

    parser = argparse.ArgumentParser(description="PluginForge provider doctor.")
    parser.add_argument("--check", metavar="PROVIDER", default="all",
                        help="Provider to check, or 'all' (default: all)")
    parser.add_argument("--list-models", metavar="PROVIDER", default=None,
                        help="Print every live model id for one provider and exit.")
    args = parser.parse_args(argv)

    if args.list_models:
        provider = resolve_provider(args.list_models)
        for model_id in list_models(provider):
            print(model_id)
        return 0

    names = sorted(PROVIDERS) if args.check == "all" else [resolve_provider(args.check)]
    _print_reports([_check_one(n) for n in names])
    return 0


if __name__ == "__main__":
    sys.exit(main())
