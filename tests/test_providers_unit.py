"""Unit tests for llm/providers.py — the multi-provider registry.

No network, no API key, no cost: every adapter's transport is mocked. The point
is to pin the behaviors that are easy to get silently wrong — the free-only rule,
markdown-fence stripping, rate-limit backoff, and which request shape each
provider family expects.
"""
import json
from unittest.mock import MagicMock, patch

import pytest

# conftest.py puts llm/ on sys.path.
import providers


# ---------------------------------------------------------------------------
# Registry integrity
# ---------------------------------------------------------------------------

class TestRegistry:
    def test_every_spec_key_matches_its_name(self):
        for key, spec in providers.PROVIDERS.items():
            assert spec.name == key

    def test_every_kind_has_an_adapter(self):
        for spec in providers.PROVIDERS.values():
            assert spec.kind in providers._ADAPTERS

    def test_openai_compat_specs_have_a_base_url(self):
        for spec in providers.PROVIDERS.values():
            if spec.kind == "openai_compat":
                assert spec.base_url and spec.base_url.startswith("http")

    def test_exactly_one_paid_provider(self):
        paid = [s.name for s in providers.PROVIDERS.values() if not s.free]
        assert paid == ["anthropic"]

    def test_get_spec_rejects_unknown(self):
        with pytest.raises(ValueError, match="Unknown provider"):
            providers.get_spec("nope")


# ---------------------------------------------------------------------------
# Selection precedence
# ---------------------------------------------------------------------------

class TestSuiteHermeticity:
    """Regression guard for a live-network hang found 2026-07-21.

    llm/generate.py calls load_dotenv() at import. When PLUGINFORGE_PROVIDER was
    first written into a real .env, the unit tests — which mock only the anthropic
    client — began dispatching real Gemini calls and hung. conftest.py now pins the
    provider for the session.
    """

    def test_session_provider_is_pinned_to_anthropic(self):
        import generate
        assert generate.DEFAULT_PROVIDER == "anthropic"

    def test_session_model_is_not_taken_from_dotenv(self):
        import generate
        assert generate.DEFAULT_MODEL == providers.PROVIDERS["anthropic"].default_model


class TestResolution:
    def test_explicit_beats_env(self, monkeypatch):
        monkeypatch.setenv("PLUGINFORGE_PROVIDER", "groq")
        assert providers.resolve_provider("gemini") == "gemini"

    def test_env_beats_default(self, monkeypatch):
        monkeypatch.setenv("PLUGINFORGE_PROVIDER", "groq")
        assert providers.resolve_provider() == "groq"

    def test_falls_back_to_anthropic(self, monkeypatch):
        monkeypatch.delenv("PLUGINFORGE_PROVIDER", raising=False)
        assert providers.resolve_provider() == providers.DEFAULT_PROVIDER == "anthropic"

    def test_unknown_env_value_raises_at_selection(self, monkeypatch):
        monkeypatch.setenv("PLUGINFORGE_PROVIDER", "definitely-not-a-provider")
        with pytest.raises(ValueError, match="Unknown provider"):
            providers.resolve_provider()

    def test_model_precedence(self, monkeypatch):
        monkeypatch.setenv("PLUGINFORGE_MODEL", "from-env")
        assert providers.resolve_model("gemini", "explicit") == "explicit"
        assert providers.resolve_model("gemini") == "from-env"
        monkeypatch.delenv("PLUGINFORGE_MODEL")
        assert providers.resolve_model("gemini") == providers.PROVIDERS["gemini"].default_model


# ---------------------------------------------------------------------------
# The free-only rule
# ---------------------------------------------------------------------------

class TestFreeOnlyGuard:
    def test_paid_provider_is_refused(self, monkeypatch):
        monkeypatch.delenv("PLUGINFORGE_ALLOW_PAID", raising=False)
        with pytest.raises(providers.PaidProviderError):
            providers.assert_free("anthropic")

    def test_refusal_names_the_free_alternatives(self, monkeypatch):
        monkeypatch.delenv("PLUGINFORGE_ALLOW_PAID", raising=False)
        with pytest.raises(providers.PaidProviderError) as exc:
            providers.assert_free("anthropic")
        for free_name in ("gemini", "groq", "ollama", "openrouter"):
            assert free_name in str(exc.value)

    @pytest.mark.parametrize("provider", ["gemini", "groq", "openrouter", "ollama"])
    def test_free_providers_pass(self, provider, monkeypatch):
        monkeypatch.delenv("PLUGINFORGE_ALLOW_PAID", raising=False)
        providers.assert_free(provider)  # must not raise

    def test_explicit_opt_in_permits_paid(self, monkeypatch):
        monkeypatch.setenv("PLUGINFORGE_ALLOW_PAID", "1")
        providers.assert_free("anthropic")  # must not raise

    def test_only_the_exact_value_1_opts_in(self, monkeypatch):
        monkeypatch.setenv("PLUGINFORGE_ALLOW_PAID", "true")
        with pytest.raises(providers.PaidProviderError):
            providers.assert_free("anthropic")


# ---------------------------------------------------------------------------
# Credentials
# ---------------------------------------------------------------------------

class TestCheckCredentials:
    def test_missing_key_names_the_right_env_var(self, monkeypatch):
        monkeypatch.delenv("GROQ_API_KEY", raising=False)
        message = providers.check_credentials("groq")
        assert "GROQ_API_KEY" in message
        assert "ANTHROPIC_API_KEY" not in message

    def test_message_includes_where_to_get_a_key(self, monkeypatch):
        monkeypatch.delenv("GROQ_API_KEY", raising=False)
        assert providers.PROVIDERS["groq"].signup_url in providers.check_credentials("groq")

    def test_present_key_passes(self, monkeypatch):
        monkeypatch.setenv("GROQ_API_KEY", "gsk-test")
        assert providers.check_credentials("groq") is None

    def test_empty_key_is_treated_as_missing(self, monkeypatch):
        monkeypatch.setenv("GROQ_API_KEY", "")
        assert providers.check_credentials("groq") is not None

    def test_keyless_provider_always_passes(self, monkeypatch):
        monkeypatch.delenv("OPENROUTER_API_KEY", raising=False)
        assert providers.check_credentials("ollama") is None


# ---------------------------------------------------------------------------
# Markdown fence stripping
#
# Open-weight models wrap output in fences; `faust -lang cpp` then fails on line 1.
# Observed live 2026-07-21 from gemini-3.6-flash (truncated at max_tokens):
#   '-friendly to avoid any syntax bugs...\n\n    Option A (Moog VCF):\n    ```faust\n'
# ---------------------------------------------------------------------------

FAUST = 'import("stdfaust.lib");\nprocess = _ * 0.5;'


class TestStripCodeFences:
    def test_language_tagged_fence(self):
        assert providers.strip_code_fences(f"```faust\n{FAUST}\n```") == FAUST

    def test_bare_fence(self):
        assert providers.strip_code_fences(f"```\n{FAUST}\n```") == FAUST

    def test_prose_preamble_is_dropped(self):
        text = f"Sure! Here is your filter:\n\n```faust\n{FAUST}\n```\nHope that helps."
        assert providers.strip_code_fences(text) == FAUST

    def test_unterminated_fence_still_yields_code(self):
        # A truncated response (finish_reason=MAX_TOKENS) has no closing fence.
        assert providers.strip_code_fences(f"```faust\n{FAUST}") == FAUST

    def test_unfenced_text_is_returned_unchanged(self):
        assert providers.strip_code_fences(FAUST) == FAUST

    def test_surrounding_whitespace_is_trimmed(self):
        assert providers.strip_code_fences(f"\n\n  {FAUST}  \n") == FAUST

    def test_empty_body_falls_back_to_the_original(self):
        # Never turn a real (if odd) response into an empty string.
        assert providers.strip_code_fences("```\n```") != ""

    def test_anthropic_does_not_strip(self):
        # The tuned system prompt already forbids markdown, and every recorded
        # benchmark number was measured without stripping.
        assert providers.PROVIDERS["anthropic"].strip_fences is False

    @pytest.mark.parametrize("name", ["gemini", "groq", "openrouter", "ollama"])
    def test_free_providers_do_strip(self, name):
        assert providers.PROVIDERS[name].strip_fences is True


# ---------------------------------------------------------------------------
# make_generator — guards and normalization
# ---------------------------------------------------------------------------

class TestMakeGenerator:
    def test_unknown_provider_raises(self):
        with pytest.raises(ValueError, match="Unknown provider"):
            providers.make_generator("openai", system_prompt="x")

    def test_temperature_on_a_no_temperature_model_raises(self):
        # claude-opus-4-7+ reject `temperature` with a 400; fail early and clearly.
        with pytest.raises(ValueError, match="temperature"):
            providers.make_generator("anthropic", system_prompt="x",
                                     model="claude-opus-4-8", temperature=0.0)

    def test_temperature_allowed_on_older_anthropic_model(self):
        assert providers.supports_temperature("anthropic", "claude-opus-4-6") is True
        assert providers.supports_temperature("anthropic", "claude-opus-4-8") is False

    @pytest.mark.parametrize("name", ["gemini", "groq", "openrouter", "ollama"])
    def test_every_free_provider_accepts_temperature_zero(self, name):
        # This is what restores the study's locked temperature=0 confound control.
        assert providers.supports_temperature(name, providers.PROVIDERS[name].default_model)

    def test_min_max_tokens_floor_is_applied(self):
        # Reasoning models bill hidden thinking against the same cap: measured
        # 2026-07-21 on gemini-3.6-flash, max_output_tokens=1024 produced 981
        # thinking tokens and a truncated 39-token answer.
        captured = {}

        def fake_adapter(spec, model, system_prompt, temperature, max_tokens):
            captured["max_tokens"] = max_tokens
            return lambda msg: FAUST

        with patch.dict(providers._ADAPTERS, {"gemini": fake_adapter}):
            providers.make_generator("gemini", system_prompt="x", max_tokens=1024)
        assert captured["max_tokens"] == providers.PROVIDERS["gemini"].min_max_tokens
        assert captured["max_tokens"] > 1024

    def test_caller_max_tokens_wins_when_above_the_floor(self):
        captured = {}

        def fake_adapter(spec, model, system_prompt, temperature, max_tokens):
            captured["max_tokens"] = max_tokens
            return lambda msg: FAUST

        with patch.dict(providers._ADAPTERS, {"gemini": fake_adapter}):
            providers.make_generator("gemini", system_prompt="x", max_tokens=99999)
        assert captured["max_tokens"] == 99999

    def test_fences_are_stripped_through_the_public_seam(self):
        def fake_adapter(spec, model, system_prompt, temperature, max_tokens):
            return lambda msg: f"```faust\n{FAUST}\n```"

        with patch.dict(providers._ADAPTERS, {"openai_compat": fake_adapter}):
            generate = providers.make_generator("groq", system_prompt="x")
        assert generate("a filter") == FAUST


# ---------------------------------------------------------------------------
# OpenAI-compatible adapter (groq / openrouter / ollama)
# ---------------------------------------------------------------------------

def _ok_response(content: str):
    response = MagicMock()
    response.status_code = 200
    response.json.return_value = {"choices": [{"message": {"content": content}}]}
    return response


class TestOpenAICompatAdapter:
    def _generate(self, provider="groq", **kwargs):
        with patch.object(providers.httpx, "post", return_value=_ok_response(FAUST)) as post:
            generate = providers.make_generator(provider, system_prompt="SYS", **kwargs)
            result = generate("a filter")
        return post, result

    def test_posts_to_the_providers_chat_completions_endpoint(self, monkeypatch):
        monkeypatch.setenv("GROQ_API_KEY", "gsk-test")
        post, _ = self._generate()
        url = post.call_args[0][0]
        assert url == providers.PROVIDERS["groq"].base_url + "/chat/completions"

    def test_system_prompt_is_the_first_message(self, monkeypatch):
        monkeypatch.setenv("GROQ_API_KEY", "gsk-test")
        post, _ = self._generate()
        messages = post.call_args[1]["json"]["messages"]
        assert messages[0] == {"role": "system", "content": "SYS"}
        assert messages[1] == {"role": "user", "content": "a filter"}

    def test_bearer_token_comes_from_the_providers_env_var(self, monkeypatch):
        monkeypatch.setenv("GROQ_API_KEY", "gsk-secret")
        post, _ = self._generate()
        assert post.call_args[1]["headers"]["Authorization"] == "Bearer gsk-secret"

    def test_temperature_omitted_when_none(self, monkeypatch):
        monkeypatch.setenv("GROQ_API_KEY", "gsk-test")
        post, _ = self._generate(temperature=None)
        assert "temperature" not in post.call_args[1]["json"]

    def test_temperature_sent_when_given(self, monkeypatch):
        monkeypatch.setenv("GROQ_API_KEY", "gsk-test")
        post, _ = self._generate(temperature=0.0)
        assert post.call_args[1]["json"]["temperature"] == 0.0

    def test_ollama_needs_no_key(self, monkeypatch):
        monkeypatch.delenv("OPENROUTER_API_KEY", raising=False)
        post, result = self._generate(provider="ollama")
        assert result == FAUST
        assert post.call_args[0][0].startswith("http://localhost:11434")

    def test_unexpected_response_shape_is_reported_clearly(self, monkeypatch):
        monkeypatch.setenv("GROQ_API_KEY", "gsk-test")
        broken = MagicMock()
        broken.status_code = 200
        broken.json.return_value = {"error": "model not found"}
        with patch.object(providers.httpx, "post", return_value=broken):
            generate = providers.make_generator("groq", system_prompt="x")
            with pytest.raises(RuntimeError, match="unexpected response shape"):
                generate("a filter")


# ---------------------------------------------------------------------------
# Rate-limit backoff — free tiers throttle hard (Gemini: 5 requests/minute/model)
# ---------------------------------------------------------------------------

def _status_response(code: int, retry_after: str | None = None):
    response = MagicMock()
    response.status_code = code
    response.text = f"error {code}"
    response.headers = {"Retry-After": retry_after} if retry_after else {}
    return response


class TestPostBackoff:
    def test_retries_a_429_then_succeeds(self):
        responses = [_status_response(429), _ok_response(FAUST)]
        with patch.object(providers.httpx, "post", side_effect=responses), \
             patch.object(providers.time, "sleep") as sleep:
            data = providers._post_with_backoff("http://x/chat/completions", {}, {})
        assert data["choices"][0]["message"]["content"] == FAUST
        assert sleep.called

    def test_honors_retry_after_header(self):
        responses = [_status_response(429, retry_after="42"), _ok_response(FAUST)]
        with patch.object(providers.httpx, "post", side_effect=responses), \
             patch.object(providers.time, "sleep") as sleep:
            providers._post_with_backoff("http://x/chat/completions", {}, {})
        assert sleep.call_args[0][0] == 42.0

    def test_retries_server_errors(self):
        responses = [_status_response(503), _ok_response(FAUST)]
        with patch.object(providers.httpx, "post", side_effect=responses), \
             patch.object(providers.time, "sleep"):
            providers._post_with_backoff("http://x/chat/completions", {}, {})

    def test_gives_up_after_the_retry_budget(self):
        responses = [_status_response(429)] * providers._MAX_BACKOFF_TRIES
        with patch.object(providers.httpx, "post", side_effect=responses), \
             patch.object(providers.time, "sleep"):
            with pytest.raises(RuntimeError, match="failed after"):
                providers._post_with_backoff("http://x/chat/completions", {}, {})

    def test_client_errors_are_not_retried(self):
        # A 401 will never succeed on retry; fail fast instead of burning the budget.
        post = MagicMock(return_value=_status_response(401))
        with patch.object(providers.httpx, "post", post), \
             patch.object(providers.time, "sleep"):
            with pytest.raises(RuntimeError):
                providers._post_with_backoff("http://x/chat/completions", {}, {})
        assert post.call_count == 1


class TestSdkRetryWrapper:
    """The google-genai and anthropic SDKs raise instead of returning a status."""

    def test_retries_a_resource_exhausted_error(self):
        call = MagicMock(side_effect=[
            RuntimeError("429 RESOURCE_EXHAUSTED. quota exceeded"),
            FAUST,
        ])
        with patch.object(providers.time, "sleep"):
            assert providers._call_with_retry(call)() == FAUST
        assert call.call_count == 2

    def test_extracts_googles_retry_delay_hint(self):
        call = MagicMock(side_effect=[
            RuntimeError("429 RESOURCE_EXHAUSTED {'retryDelay': '54s'}"),
            FAUST,
        ])
        with patch.object(providers.time, "sleep") as sleep:
            providers._call_with_retry(call)()
        assert sleep.call_args[0][0] == pytest.approx(55.0)  # hint + 1s margin

    def test_extracts_the_prose_retry_hint(self):
        call = MagicMock(side_effect=[RuntimeError("Please retry in 12.5s"), FAUST])
        with patch.object(providers.time, "sleep") as sleep:
            providers._call_with_retry(call)()
        assert sleep.call_args[0][0] == pytest.approx(13.5)

    def test_non_retryable_errors_propagate_immediately(self):
        call = MagicMock(side_effect=ValueError("400 INVALID_ARGUMENT: bad model"))
        with patch.object(providers.time, "sleep") as sleep:
            with pytest.raises(ValueError):
                providers._call_with_retry(call)()
        assert call.call_count == 1
        assert not sleep.called

    def test_gives_up_after_the_retry_budget(self):
        call = MagicMock(side_effect=RuntimeError("429 RESOURCE_EXHAUSTED"))
        with patch.object(providers.time, "sleep"):
            with pytest.raises(RuntimeError):
                providers._call_with_retry(call)()
        assert call.call_count == providers._MAX_BACKOFF_TRIES


class TestIsRetryable:
    @pytest.mark.parametrize("message", [
        "429 RESOURCE_EXHAUSTED",
        "503 Service Unavailable",
        "The model is overloaded",
        "Please retry in 54.005s",
        "{'retryDelay': '54s'}",
        "You exceeded your current quota",
    ])
    def test_retryable(self, message):
        assert providers._is_retryable(RuntimeError(message)) is True

    @pytest.mark.parametrize("message", [
        "400 INVALID_ARGUMENT",
        "401 Unauthorized",
        "404 NOT_FOUND: model no longer available to new users",
        "credit balance is too low",
        # A status code must not false-match inside an unrelated number.
        "max_tokens: 1500 exceeds the limit",
    ])
    def test_not_retryable(self, message):
        assert providers._is_retryable(RuntimeError(message)) is False

    def test_daily_quota_is_not_retried_despite_looking_retryable(self):
        # Measured 2026-07-21: Gemini returns a ~60s retryDelay for a quota that
        # doesn't reopen for hours. Honoring the hint burns 5 minutes to fail anyway.
        daily = ("429 RESOURCE_EXHAUSTED {'quotaId': "
                 "'GenerateRequestsPerDayPerProjectPerModel-FreeTier', "
                 "'quotaValue': '20'} retryDelay: '58s'")
        assert providers._is_retryable(RuntimeError(daily)) is False

    def test_per_minute_quota_is_still_retried(self):
        per_minute = ("429 RESOURCE_EXHAUSTED {'quotaId': "
                      "'GenerateRequestsPerMinutePerProjectPerModel-FreeTier'} "
                      "retryDelay: '54s'")
        assert providers._is_retryable(RuntimeError(per_minute)) is True

    def test_daily_quota_fails_fast(self):
        call = MagicMock(side_effect=RuntimeError(
            "429 RESOURCE_EXHAUSTED GenerateRequestsPerDayPerProjectPerModel-FreeTier"))
        with patch.object(providers.time, "sleep") as sleep:
            with pytest.raises(RuntimeError):
                providers._call_with_retry(call)()
        assert call.call_count == 1
        assert not sleep.called


# ---------------------------------------------------------------------------
# Pacing — keeps a long run under a per-minute cap
# ---------------------------------------------------------------------------

class TestPacing:
    def test_no_sleep_when_unset(self, monkeypatch):
        monkeypatch.delenv("PLUGINFORGE_MIN_INTERVAL", raising=False)
        with patch.object(providers.time, "sleep") as sleep:
            providers._pace()
        assert not sleep.called

    def test_sleeps_to_honor_the_interval(self, monkeypatch):
        monkeypatch.setenv("PLUGINFORGE_MIN_INTERVAL", "12")
        with patch.object(providers.time, "sleep") as sleep, \
             patch.object(providers.time, "monotonic", side_effect=[100.0, 100.0]):
            providers._last_call_at = 95.0
            providers._pace()
        assert sleep.call_args[0][0] == pytest.approx(7.0)

    def test_malformed_interval_is_ignored(self, monkeypatch):
        monkeypatch.setenv("PLUGINFORGE_MIN_INTERVAL", "not-a-number")
        with patch.object(providers.time, "sleep") as sleep:
            providers._pace()
        assert not sleep.called


# ---------------------------------------------------------------------------
# Model discovery (doctor CLI)
# ---------------------------------------------------------------------------

class TestListModels:
    def test_openai_compat_returns_sorted_ids(self, monkeypatch):
        monkeypatch.setenv("GROQ_API_KEY", "gsk-test")
        response = MagicMock()
        response.json.return_value = {"data": [{"id": "zeta"}, {"id": "alpha"}]}
        with patch.object(providers.httpx, "get", return_value=response) as get:
            assert providers.list_models("groq") == ["alpha", "zeta"]
        assert get.call_args[0][0].endswith("/models")


# ---------------------------------------------------------------------------
# generate.py integration — the ADR-011 wire contract under a free provider
# ---------------------------------------------------------------------------

class TestGenerateSubprocessModeWithFreeProvider:
    def test_missing_key_message_names_the_free_providers_env_var(self, monkeypatch, capsys):
        import generate
        monkeypatch.setattr(generate, "DEFAULT_PROVIDER", "groq")
        monkeypatch.delenv("GROQ_API_KEY", raising=False)
        generate._run_subprocess_mode(lambda: {"prompt": "x"})

        payload = json.loads(capsys.readouterr().out.strip())
        assert payload["success"] is False
        assert "GROQ_API_KEY" in payload["error"]
        assert "ANTHROPIC_API_KEY" not in payload["error"]

    def test_keyless_provider_skips_the_precheck(self, monkeypatch, capsys):
        import generate
        monkeypatch.setattr(generate, "DEFAULT_PROVIDER", "ollama")
        with patch.object(generate, "generate_json",
                          return_value={"success": True, "faust_code": FAUST,
                                        "attempts": 1, "error": None}):
            generate._run_subprocess_mode(lambda: {"prompt": "x"})

        payload = json.loads(capsys.readouterr().out.strip())
        assert payload["success"] is True

    def test_exactly_one_json_line_on_stdout(self, monkeypatch, capsys):
        import generate
        monkeypatch.setattr(generate, "DEFAULT_PROVIDER", "groq")
        monkeypatch.delenv("GROQ_API_KEY", raising=False)
        generate._run_subprocess_mode(lambda: {"prompt": "x"})

        lines = [ln for ln in capsys.readouterr().out.splitlines() if ln.strip()]
        assert len(lines) == 1
        json.loads(lines[0])
