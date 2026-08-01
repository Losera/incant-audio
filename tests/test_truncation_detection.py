#!/usr/bin/env python3
"""
Regression tests for the output-truncation confound.

WHAT BROKE. Nothing in the pipeline read the provider's "I ran out of output
budget" signal — no adapter inspected finish_reason or stop_reason — and the
truncated body was deliberately passed through. So a cut-off program reached
`faust`, which said `syntax error, unexpected $end` (bison's $end IS the EOF
token: "the parser ran out of input"), and generate.py fed that stderr back with
"Your previous output had this compiler error - fix it". The model, told it made
a syntax error it had not made, wrote another program of the same length, which
truncated again. Three attempts, three guaranteed failures, the entire request
budget spent — a mechanism for PF-019's cliff independent of the timeout bug, and
a candidate explanation for part of PF-024.

The evidence, re-verified against bench/results/efficacy/pilot_20260720.json on
2026-07-25 before any of this code was written:
  * failed generations average 1447 chars, successful ones 581
  * the longest sit at ~2200 chars — where a 1024-token cap on punctuation-dense
    Faust bites
  * the `anthropic` spec, which produced EVERY recorded benchmark and efficacy
    number, had min_max_tokens=0 and therefore ran at a raw 1024 cap

Credit: the parallel research session (docs/research/truncation-confound-
HANDOFF-S1.md).

WHAT THESE PIN. Three properties, one per layer:
  1. every adapter raises OutputTruncated instead of returning a partial program
  2. no provider spec can silently run at a cap below the shared floor
  3. generate.py retries truncation with a BREVITY instruction, never with
     compiler stderr, and reports reason="truncated" rather than "invalid_faust"

NOT COVERED: these mock the transport. They prove the signal is read and routed;
they do not prove a live provider sets the field in the shape assumed here. The
shapes were taken from each SDK's documented contract, and the openai-compatible
one is additionally confirmed by the recorded gpt-oss-20b behaviour noted in
llm/providers.py's groq spec.
"""
import sys
from pathlib import Path
from unittest.mock import MagicMock, patch

import pytest

sys.path.insert(0, str(Path(__file__).parent.parent / "llm"))
import generate  # noqa: E402
import providers  # noqa: E402

FAUST = 'import("stdfaust.lib");\nprocess = _;'
# A real truncation: cut mid-identifier, exactly as pilot record L1/generative-03
# ended ("chorusR(x) = x * 0.6 + de.").
CUT_OFF = 'import("stdfaust.lib");\nchorusR(x) = x * 0.6 + de.'


# ---------------------------------------------------------------------------
# 1. Every adapter surfaces the signal
# ---------------------------------------------------------------------------

class TestAdaptersDetectTruncation:
    def _openai_response(self, finish_reason):
        response = MagicMock()
        response.status_code = 200
        response.json.return_value = {
            "choices": [{"finish_reason": finish_reason,
                         "message": {"content": CUT_OFF}}]
        }
        return response

    def test_openai_compat_raises_on_finish_reason_length(self, monkeypatch):
        monkeypatch.setenv("GROQ_API_KEY", "gsk-test")
        with patch.object(providers.httpx, "post",
                          return_value=self._openai_response("length")):
            generate_fn = providers.make_generator("groq", system_prompt="x")
            with pytest.raises(providers.OutputTruncated):
                generate_fn("a lush chorus")

    def test_openai_compat_passes_a_complete_response_through(self, monkeypatch):
        """The guard must not fire on a normal stop — regression guard."""
        monkeypatch.setenv("GROQ_API_KEY", "gsk-test")
        response = MagicMock()
        response.status_code = 200
        response.json.return_value = {
            "choices": [{"finish_reason": "stop", "message": {"content": FAUST}}]
        }
        with patch.object(providers.httpx, "post", return_value=response):
            generate_fn = providers.make_generator("groq", system_prompt="x")
            assert generate_fn("a filter") == FAUST

    def test_anthropic_raises_on_stop_reason_max_tokens(self):
        response = MagicMock()
        response.stop_reason = "max_tokens"
        response.content = [MagicMock(text=CUT_OFF)]
        client = MagicMock()
        client.messages.create.return_value = response
        with patch.object(providers, "anthropic_client", return_value=client):
            generate_fn = providers.make_generator("anthropic", system_prompt="x")
            with pytest.raises(providers.OutputTruncated):
                generate_fn("a lush chorus")

    def test_anthropic_passes_a_complete_response_through(self):
        response = MagicMock()
        response.stop_reason = "end_turn"
        response.content = [MagicMock(text=FAUST)]
        client = MagicMock()
        client.messages.create.return_value = response
        with patch.object(providers, "anthropic_client", return_value=client):
            generate_fn = providers.make_generator("anthropic", system_prompt="x")
            assert generate_fn("a filter") == FAUST

    def test_truncation_is_not_treated_as_retryable_transport_noise(self):
        """It must reach generate.py, not get swallowed by the backoff wrapper.

        _call_with_retry retries anything _is_retryable() likes. A truncation
        message must not look retryable, or the SDK adapters would silently burn
        five attempts on a condition that retrying cannot fix.
        """
        exc = providers.OutputTruncated(
            "claude-opus-4-8 hit its 4096-token output cap mid-program")
        assert providers._is_retryable(exc) is False


# ---------------------------------------------------------------------------
# 2. No provider can silently run below the shared output floor
# ---------------------------------------------------------------------------

class TestOutputBudgetFloor:
    def test_anthropic_has_a_floor(self):
        """The specific gap: this spec produced every recorded number at 1024."""
        assert providers.PROVIDERS["anthropic"].min_max_tokens >= 4096

    def test_every_hosted_provider_floors_at_or_above_the_caller_default(self):
        # ollama is local and unmetered; openrouter proxies many models with
        # differing caps. The three PluginForge actually generates with must floor.
        for name in ("anthropic", "gemini", "groq"):
            spec = providers.PROVIDERS[name]
            assert spec.min_max_tokens >= generate.MAX_OUTPUT_TOKENS, (
                f"{name} floors at {spec.min_max_tokens}, below the caller default "
                f"{generate.MAX_OUTPUT_TOKENS} — it can run truncated")

    def test_the_caller_default_is_no_longer_1024(self):
        """1024 tokens is ~2200 chars of Faust; the pilot's longest hit 2210."""
        assert generate.MAX_OUTPUT_TOKENS > 1024


# ---------------------------------------------------------------------------
# 3. generate.py routes truncation correctly
# ---------------------------------------------------------------------------

class TestGenerateHandlesTruncation:
    def test_truncation_retries_with_brevity_not_compiler_stderr(self):
        """THE regression. Feeding back stderr is the guaranteed-failure loop."""
        sent = []

        def capture(content, *_a, **_k):
            sent.append(content)
            if len(sent) == 1:
                raise providers.OutputTruncated("cut off at the cap")
            return FAUST

        with patch.object(generate, "_call_api", side_effect=capture), \
             patch.object(generate, "validate_faust", return_value=(True, "")):
            payload = generate.generate_json({"prompt": "a lush chorus"})

        assert payload["success"] is True
        assert len(sent) == 2, "should have retried once"
        retry = sent[1]
        assert "SHORTER" in retry or "shorter" in retry, (
            "the retry must ask for a shorter program")
        assert "syntax error" not in retry and "compiler error" not in retry, (
            "the retry must NOT feed back compiler stderr — that is the bug")

    def test_persistent_truncation_reports_reason_truncated(self):
        """Not invalid_faust: the code never reached the compiler."""
        with patch.object(generate, "_call_api",
                          side_effect=providers.OutputTruncated("cut off")):
            payload = generate.generate_json({"prompt": "an enormous plugin"})

        assert payload["success"] is False
        assert payload["reason"] == "truncated", (
            "truncation must be distinguishable from the model writing bad Faust")

    def test_truncation_then_a_real_compile_error_does_not_mix_signals(self):
        """A stale stderr must never ride along with a truncation retry."""
        sent = []

        def capture(content, *_a, **_k):
            sent.append(content)
            if len(sent) == 1:
                return "process = ;"            # compiles badly
            if len(sent) == 2:
                raise providers.OutputTruncated("cut off")
            return FAUST

        def validate(code, *_a, **_k):
            return (code == FAUST, "syntax error, unexpected SEMI")

        with patch.object(generate, "_call_api", side_effect=capture), \
             patch.object(generate, "validate_faust", side_effect=validate):
            payload = generate.generate_json({"prompt": "x"})

        assert payload["success"] is True
        # Attempt 2 carried the compile error; attempt 3 followed a truncation and
        # must therefore carry the brevity hint and NOT the stale stderr.
        assert "syntax error" in sent[1]
        assert "syntax error" not in sent[2], (
            "stale compiler stderr leaked into a truncation retry")

    def test_a_complete_response_is_unaffected(self):
        with patch.object(generate, "_call_api", return_value=FAUST), \
             patch.object(generate, "validate_faust", return_value=(True, "")):
            payload = generate.generate_json({"prompt": "a filter"})
        # `kind` is additive, 2026-08-01; "a filter" routes to the effects prompt.
        assert payload == {"success": True, "faust_code": FAUST, "attempts": 1,
                           "error": None, "reason": "ok", "kind": "effect"}
