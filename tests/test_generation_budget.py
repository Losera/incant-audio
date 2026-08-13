#!/usr/bin/env python3
"""
PF-019 regression tests — the generation timeout cliff.

WHAT BROKE (2026-07-24 P6 listening battery). Prompts #11-#14 failed
consecutively, each with "LLM subprocess timed out after 120s and was killed",
and #14 — the robustness test that must *never* hang — hung. The cause was not a
slow provider. It was that three separate timeouts were all set to the same
number, or to no number at all:

  * llm/providers.py    httpx per-POST timeout   120s
  * host/.../PromptPanel.cpp  subprocess cap     120s
  * llm/generate.py     3 attempts x <=5 backoff tries, unbounded in wall clock

So one stalled or 429'd POST consumed the whole outer budget, and the host killed
the child before generate.py could emit the ADR-011 JSON explaining why.

WHAT THESE TESTS PIN. The property that actually matters is not "the fix is
present" but "the run finishes with a typed answer before the host's cap". Every
test below therefore asserts a *bound* and a *reason*, never an implementation
detail:

  1. three attempts fit inside the total budget (no single request may eat it)
  2. a backoff longer than the remaining budget is refused, not slept through
  3. throttling and stalling produce DIFFERENT typed reasons
  4. the whole thing terminates under the host's cap even when every call stalls

Test 4 is the direct regression for P6 #14.

NOT COVERED, stated explicitly (COLLABORATION.md §3): these drive a mocked
transport. They prove the budget arithmetic and the reason plumbing; they do not
prove that httpx honours the timeout it is handed, nor that groq's live
Retry-After values fall in the range assumed here. The end-to-end property is
covered by the integration test at the bottom, which is deselected by default.
"""
import json
import sys
import time
from pathlib import Path
from unittest.mock import MagicMock, patch

import pytest

sys.path.insert(0, str(Path(__file__).parent.parent / "llm"))
import generate  # noqa: E402
import providers  # noqa: E402

FAUST = 'import("stdfaust.lib");\nprocess = _;'

# The host's backstop, host/Source/PromptPanel.h kSubprocessTimeoutMs. If you
# change that constant, change this one and watch test_finishes_under_host_cap.
HOST_SUBPROCESS_CAP_S = 180.0


def _ok_response(content):
    response = MagicMock()
    response.status_code = 200
    response.json.return_value = {"choices": [{"message": {"content": content}}]}
    return response


def _longest_sleep(sleep_mock) -> float:
    """Longest sleep actually performed.

    NB: a ~1s pacing sleep (Budget.min_interval) is expected and correct — it is
    PF-019 measure 4. What must never happen is sleeping out a 58s Retry-After
    into the host's kill. So these tests assert on the LONGEST sleep, not on
    whether sleep was called at all.
    """
    if not sleep_mock.call_args_list:
        return 0.0
    return max(call.args[0] for call in sleep_mock.call_args_list)


def _throttled(retry_after=None):
    response = MagicMock()
    response.status_code = 429
    response.text = "rate_limit_exceeded"
    response.headers = {"Retry-After": retry_after} if retry_after else {}
    return response


# ---------------------------------------------------------------------------
# 1. Budget arithmetic — three attempts must fit inside the total
# ---------------------------------------------------------------------------

class TestBudgetArithmetic:
    def test_request_timeout_never_exceeds_the_per_attempt_cap(self):
        budget = providers.Budget(total=100.0, per_attempt_cap=30.0)
        assert budget.request_timeout() == 30.0

    def test_request_timeout_shrinks_to_what_remains(self):
        budget = providers.Budget(total=100.0, per_attempt_cap=30.0)
        budget.started = time.monotonic() - 90.0   # 10s left
        assert budget.request_timeout() == pytest.approx(10.0, abs=0.5)

    def test_request_timeout_never_goes_negative(self):
        """A blown budget must still produce a valid timeout, not a crash."""
        budget = providers.Budget(total=100.0, per_attempt_cap=30.0)
        budget.started = time.monotonic() - 500.0
        assert budget.request_timeout() >= 1.0

    def test_three_attempts_fit_inside_the_default_budget(self):
        """THE regression assertion for the cliff itself.

        Before PF-019 one attempt could be issued with a 120s timeout while the
        host killed at 120s. Now N attempts at the per-attempt cap must fit
        inside the total, with room for a faust compile.
        """
        budget = generate.generation_budget()
        assert budget.per_attempt_cap * 3 <= budget.total
        assert budget.total + generate.FAUST_VALIDATE_TIMEOUT_S < HOST_SUBPROCESS_CAP_S

    def test_generation_budget_sits_below_the_host_cap_with_margin(self):
        """The two numbers must never converge again — that WAS the bug."""
        budget = generate.generation_budget()
        worst_case = budget.total + generate.FAUST_VALIDATE_TIMEOUT_S
        assert worst_case < HOST_SUBPROCESS_CAP_S, (
            f"generate.py's worst case ({worst_case}s) must stay under the host's "
            f"{HOST_SUBPROCESS_CAP_S}s cap, or the host kills the child before it "
            "can report a reason — that is PF-019.")
        # Not merely below: comfortably below. 2026-08-13: the floor here moved
        # 30->20 as a DIRECT, deliberate consequence of _DEFAULT_GENERATION_BUDGET_S
        # 100->140 (see that constant's comment in generate.py) -- raising the
        # budget to actually honour groq's observed Retry-After hints (37s/25s/
        # 51s) necessarily spends some of this margin. generate.py's own comment
        # documents the resulting worst case as ~157s (155s here plus ~2s
        # interpreter startup this arithmetic doesn't count) against the 180s
        # host cap, i.e. ~23s -- 20.0 is a floor a little below that measured
        # number, not a re-loosening of the invariant itself.
        assert HOST_SUBPROCESS_CAP_S - worst_case >= 20.0

    def test_malformed_budget_env_falls_back_to_the_default(self, monkeypatch):
        monkeypatch.setenv("PLUGINFORGE_GENERATION_BUDGET", "not-a-number")
        assert generate.generation_budget().total == generate._DEFAULT_GENERATION_BUDGET_S


# ---------------------------------------------------------------------------
# 2. A sleep longer than the budget is refused, not slept through
# ---------------------------------------------------------------------------

class TestBackoffRefusal:
    def test_can_sleep_is_false_when_no_room_remains(self):
        budget = providers.Budget(total=100.0)
        budget.started = time.monotonic() - 95.0   # 5s left
        assert budget.can_sleep(58.0) is False

    def test_retry_after_longer_than_the_budget_raises_rate_limited(self):
        """P6's exact shape: groq says 'wait 58s', 10s of budget remains.

        Sleeping burns the budget to fail anyway AND guarantees the host kills us
        first. Refuse, and say why.
        """
        budget = providers.Budget(total=100.0)
        budget.started = time.monotonic() - 90.0
        with patch.object(providers.httpx, "post", return_value=_throttled("58")), \
             patch.object(providers.time, "sleep") as sleep:
            with pytest.raises(providers.RateLimited):
                providers._post_with_backoff("http://x", {}, {}, budget)
        assert _longest_sleep(sleep) < 5.0, (
            "refused the retry but slept the 58s Retry-After anyway — that is "
            "exactly the PF-019 behaviour, just relocated")

    def test_sdk_path_refuses_the_same_way(self):
        budget = providers.Budget(total=100.0)
        budget.started = time.monotonic() - 90.0
        call = MagicMock(side_effect=RuntimeError("429 retryDelay: '58s'"))
        with patch.object(providers.time, "sleep") as sleep:
            with pytest.raises(providers.RateLimited):
                providers._call_with_retry(call, budget)()
        assert _longest_sleep(sleep) < 5.0

    def test_an_expired_budget_raises_before_making_a_request(self):
        budget = providers.Budget(total=10.0)
        budget.started = time.monotonic() - 60.0
        post = MagicMock()
        with patch.object(providers.httpx, "post", post):
            with pytest.raises(providers.BudgetExhausted):
                providers._post_with_backoff("http://x", {}, {}, budget)
        assert post.call_count == 0

    def test_a_37s_hint_is_honored_with_room_in_the_140s_budget(self):
        """The concrete number from logs/prompts.jsonl's first throttle event.

        A fresh 140s budget (generate.generation_budget()'s default since
        this change) has ample room for a 37s wait plus a follow-up request.
        """
        budget = generate.generation_budget()
        assert budget.total == pytest.approx(140.0)
        assert budget.can_sleep(37.0) is True

    def test_the_same_37s_hint_is_refused_near_the_140s_deadline(self):
        """Same hint, spent budget — still refused, not slept through.

        Mirrors test_retry_after_longer_than_the_budget_raises_rate_limited's
        58s/10s-remaining shape at the new 140s total: 30s remaining cannot
        absorb a 37s sleep plus _MIN_USEFUL_REQUEST_S headroom for the retry
        that would follow it.
        """
        budget = generate.generation_budget()
        budget.started = time.monotonic() - (budget.total - 30.0)   # 30s left
        assert budget.can_sleep(37.0) is False

    def test_a_healthy_budget_still_retries_normally(self):
        """The refusal must not break ordinary backoff — regression guard."""
        budget = providers.Budget(total=300.0)
        responses = [_throttled("2"), _ok_response(FAUST)]
        with patch.object(providers.httpx, "post", side_effect=responses), \
             patch.object(providers.time, "sleep") as sleep:
            data = providers._post_with_backoff("http://x", {}, {}, budget)
        assert data["choices"][0]["message"]["content"] == FAUST
        assert sleep.called


# ---------------------------------------------------------------------------
# 3. Typed reasons reach the ADR-011 JSON
# ---------------------------------------------------------------------------

class TestTypedReason:
    def _run(self, side_effect):
        with patch.object(generate, "generate_faust", side_effect=side_effect):
            return generate.generate_json({"prompt": "a warm lowpass"})

    def test_rate_limited_is_reported_as_rate_limited(self):
        payload = self._run(providers.RateLimited("wait 58s, 4s of budget left"))
        assert payload["success"] is False
        assert payload["reason"] == "rate_limited"
        assert "58s" in payload["error"]

    def test_budget_exhaustion_is_reported_as_timeout(self):
        payload = self._run(providers.BudgetExhausted("budget of 100s exhausted"))
        assert payload["reason"] == "timeout"

    def test_uncompilable_faust_is_reported_as_invalid_faust(self):
        with patch.object(generate, "generate_faust", return_value="process = ;"), \
             patch.object(generate, "validate_faust",
                          return_value=(False, "syntax error, unexpected SEMI")):
            payload = generate.generate_json({"prompt": "x"})
        assert payload["reason"] == "invalid_faust"
        assert payload["attempts"] == 3
        assert "syntax error" in payload["error"]

    def test_success_carries_reason_ok(self):
        with patch.object(generate, "generate_faust", return_value=FAUST), \
             patch.object(generate, "validate_faust", return_value=(True, "")):
            payload = generate.generate_json({"prompt": "x"})
        # `kind` joined the schema with the instrument prompt (2026-08-01),
        # additively — same treatment `reason` got under PF-019. The equality is
        # deliberately EXACT rather than a subset check: this is the ADR-011
        # contract, and a field appearing unannounced is exactly what it is here
        # to catch. "x" carries no instrument words, so the router says effect.
        assert payload == {"success": True, "faust_code": FAUST, "attempts": 1,
                           "error": None, "reason": "ok", "kind": "effect",
                           "family": "effect", "family_source": "auto"}

    def test_every_response_carries_a_reason(self):
        """The host switches on this field; it must never be absent."""
        cases = [
            (providers.RateLimited("x"), "rate_limited"),
            (providers.BudgetExhausted("x"), "timeout"),
        ]
        for exc, expected in cases:
            assert self._run(exc)["reason"] == expected

    def test_reason_survives_json_serialisation(self):
        """ADR-011 is a *wire* contract — assert it round-trips as JSON."""
        payload = self._run(providers.RateLimited("throttled"))
        assert json.loads(json.dumps(payload))["reason"] == "rate_limited"

    def test_rate_limited_failure_carries_retry_after(self):
        """2026-08-13: PromptPanel::statusForReason() needs this to say

        "try again in 37s" instead of the generic "wait a moment". Additive
        per ADR-011 (same treatment `kind`/`family` got) — assert the exact
        value round-trips through _failure() and JSON serialisation both.
        """
        payload = self._run(providers.RateLimited("wait 37s", retry_after=37.0))
        assert payload["retry_after"] == 37.0
        assert json.loads(json.dumps(payload))["retry_after"] == 37.0

    def test_retry_after_absent_when_the_provider_did_not_supply_one(self):
        """_failure()'s additive-key contract: absent, not null, when unset.

        Mirrors the older-generate.py compatibility story PromptPanel.cpp's
        comment documents for `reason` itself — a missing key must fall back
        to the generic wording, not a spurious 0s/None value.
        """
        payload = self._run(providers.RateLimited("throttled, no Retry-After header"))
        assert "retry_after" not in payload

    def test_other_reasons_never_carry_retry_after(self):
        """Regression guard: retry_after is rate_limited-specific, not global."""
        payload = self._run(providers.BudgetExhausted("budget of 100s exhausted"))
        assert "retry_after" not in payload


# ---------------------------------------------------------------------------
# 4. THE regression for P6 #14 — it must terminate, under the cap
# ---------------------------------------------------------------------------

class TestTerminatesUnderTheHostCap:
    def test_every_call_stalling_still_returns_before_the_host_cap(self):
        """P6 #14 was the never-hang robustness test, and it hung.

        Simulate the worst case the battery actually hit: every provider call
        stalls for its full allowance. The run must still return a structured
        answer, and it must do so on a clock that beats the host's kill.

        Time is simulated — this test takes milliseconds, not minutes. What is
        asserted is the *arithmetic*: the sum of what the code would have waited.
        """
        budget = generate.generation_budget()
        waited = {"total": 0.0}

        def stalling_call(_message, *_):
            # Consume exactly the timeout the budget handed out, as a real stall
            # would, then fail the way httpx does on timeout.
            allowance = budget.request_timeout()
            waited["total"] += allowance
            budget.started -= allowance          # advance the simulated clock
            raise providers.BudgetExhausted(f"stalled for {allowance:.0f}s")

        with patch.object(generate, "_call_api", side_effect=stalling_call):
            payload = generate.generate_json({"prompt": "never hang", "budget": budget})

        assert payload["success"] is False
        assert payload["reason"] == "timeout"
        assert waited["total"] <= budget.total + budget.per_attempt_cap
        assert waited["total"] < HOST_SUBPROCESS_CAP_S, (
            f"stalled run consumed {waited['total']:.0f}s against a "
            f"{HOST_SUBPROCESS_CAP_S:.0f}s host cap — PF-019 has regressed")

    def test_repeated_throttling_terminates_rather_than_looping(self):
        """Sustained groq use — the #11-#14 shape. Must end, and end typed."""
        budget = providers.Budget(total=60.0, per_attempt_cap=20.0)
        budget.started = time.monotonic() - 55.0    # nearly spent, as by #14

        with patch.object(providers.httpx, "post", return_value=_throttled("58")), \
             patch.object(providers.time, "sleep") as sleep:
            with pytest.raises((providers.RateLimited, providers.BudgetExhausted)):
                providers._post_with_backoff("http://x", {}, {}, budget)
        assert _longest_sleep(sleep) < 5.0

    def test_a_pathological_patch_cannot_hang_the_faust_compiler(self):
        """PF-024's unbounded-delay patches made `faust` itself the stall point.

        A compiler that never returns is reported as a compile error so the retry
        loop can feed it back — not as an exception, and not as a hang.
        """
        import subprocess
        with patch.object(generate.subprocess, "run",
                          side_effect=subprocess.TimeoutExpired("faust", 15)):
            valid, error = generate.validate_faust("process = @(1<<31);")
        assert valid is False
        assert "did not finish" in error

    def test_validate_faust_timeout_is_clamped_to_the_remaining_budget(self):
        budget = providers.Budget(total=100.0)
        budget.started = time.monotonic() - 96.0    # ~4s left
        captured = {}

        def fake_run(*args, **kwargs):
            captured["timeout"] = kwargs["timeout"]
            result = MagicMock()
            result.returncode = 0
            result.stderr = ""
            return result

        with patch.object(generate.subprocess, "run", side_effect=fake_run):
            generate.validate_faust(FAUST, budget)
        assert captured["timeout"] < generate.FAUST_VALIDATE_TIMEOUT_S
        assert captured["timeout"] >= 2.0


# ---------------------------------------------------------------------------
# 5. Live end-to-end — deselected by default (needs a provider + a key)
# ---------------------------------------------------------------------------

@pytest.mark.integration
def test_live_generation_returns_within_the_host_cap():
    """The property the mocked tests cannot prove: a REAL run finishes in time.

    Run with:  pytest tests/test_generation_budget.py -m integration
    Requires PLUGINFORGE_PROVIDER + that provider's key in .env. Costs one free
    generation.
    """
    started = time.monotonic()
    payload = generate.generate_json({"prompt": "a gentle one-pole lowpass filter"})
    elapsed = time.monotonic() - started

    assert elapsed < HOST_SUBPROCESS_CAP_S, (
        f"live generation took {elapsed:.0f}s against a {HOST_SUBPROCESS_CAP_S:.0f}s "
        "host cap")
    assert payload["reason"] in {"ok", "invalid_faust", "rate_limited", "timeout"}
