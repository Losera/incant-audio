#!/usr/bin/env python3
"""
Regression test for the DELAY_RANGE retry hint (2026-08-13).

WHAT BROKE. system_prompt.txt already carries the bounded-delay rule
("The FIRST argument to de.delay, de.fdelay and ef.echo is the MAXIMUM
delay ... must be a compile-time constant") and four correctly-bounded
few-shots. Live evidence (logs/prompts.jsonl, 2026-08-13T04:46:48Z): "a
simple reverb" still failed with

    ERROR : invalid delay parameter range: interval(0,2.14748e+09,0).
    The range must be between 0 and INT_MAX

three attempts in a row. The retry loop fed back only the raw stderr
above, which names no expression, no line, and no remedy -- the model saw
a rule it had already been given and had nothing new to act on, so it
made the same mistake three times.

WHAT THIS PINS. A DELAY_RANGE classification (llm/error_classes.py, the
same rule table bench/classify_failures.py uses) triggers a concrete,
class-specific repair hint APPENDED to (not replacing) the raw stderr on
retry -- the same pattern _TRUNCATION_HINT already proved for a different
failure signal (tests/test_truncation_detection.py). The hint must reach
the model's NEXT attempt and must NOT appear in the final user-facing
failure message, which stays raw stderr, matching _TRUNCATION_HINT's own
separation.

NOT COVERED: this mocks the transport and the validator. It proves the
hint is classified and routed correctly; it does not prove a live model
actually stops making the mistake when it sees it -- that needs a real
bench run (bench/classify_failures.py --compare) once this lands.
"""
import sys
from pathlib import Path
from unittest.mock import patch

import pytest

sys.path.insert(0, str(Path(__file__).parent.parent / "llm"))
import generate  # noqa: E402

FAUST = 'import("stdfaust.lib");\nprocess = _;'
DELAY_ERROR = (
    "ERROR : invalid delay parameter range: interval(0,2.14748e+09,0). "
    "The range must be between 0 and INT_MAX"
)


class TestDelayRangeRetryHint:
    def test_delay_range_retries_with_the_bounded_delay_hint(self):
        """THE regression: raw stderr alone let the model repeat the
        identical mistake 3 attempts in a row, live."""
        sent = []

        def capture(content, *_a, **_k):
            sent.append(content)
            return FAUST

        def validate(code, *_a, **_k):
            if len(sent) == 1:
                return (False, DELAY_ERROR)
            return (True, "")

        with patch.object(generate, "_call_api", side_effect=capture), \
             patch.object(generate, "validate_faust", side_effect=validate):
            payload = generate.generate_json({"prompt": "a simple reverb"})

        assert payload["success"] is True
        assert len(sent) == 2, "should have retried once"
        retry = sent[1]
        assert "compile-time constant" in retry
        assert "MAXIMUM delay" in retry
        # Additive, not a replacement -- the raw stderr must still be present.
        assert "interval(0,2.14748e+09,0)" in retry

    def test_a_generic_syntax_error_gets_no_delay_hint(self):
        """Regression guard: the hint must not leak onto unrelated failures."""
        sent = []

        def capture(content, *_a, **_k):
            sent.append(content)
            return FAUST

        def validate(code, *_a, **_k):
            if len(sent) == 1:
                return (False, "syntax error, unexpected IDENT")
            return (True, "")

        with patch.object(generate, "_call_api", side_effect=capture), \
             patch.object(generate, "validate_faust", side_effect=validate):
            payload = generate.generate_json({"prompt": "a filter"})

        assert payload["success"] is True
        assert "compile-time constant" not in sent[1]

    def test_the_final_failure_message_stays_unhinted(self):
        """error_ctx feeds _failure()'s user-facing message too -- the hint
        is LLM-directed repair text ("re-emit the full program"), not
        something a human reading the error panel should see. Same
        separation _TRUNCATION_HINT already keeps (never part of a
        _failure() call)."""
        with patch.object(generate, "_call_api", return_value=FAUST), \
             patch.object(generate, "validate_faust", return_value=(False, DELAY_ERROR)):
            payload = generate.generate_json({"prompt": "a simple reverb"})

        assert payload["success"] is False
        assert payload["reason"] == "invalid_faust"
        assert payload["error"] == DELAY_ERROR, (
            "the final error must be exactly the raw stderr, with no hint appended")
