#!/usr/bin/env python3
"""
Regression test for the ROUTING_ARITY retry hint (2026-09-08).

WHAT BROKE. `routing_arity` is PF-024's largest first-attempt failure class
and it dominates the dynamics category. On the 2026-08-31 groq efficacy grid
(bench/results/efficacy/efficacy_groq_20260831.json) two cells emitted the
IDENTICAL arity error on all three attempts:

    dynamics-03/L4  ef.gate_stereo(thresh, attack, hold, release, _,_)
    dynamics-01/L4  co.compressor_stereo(ratio, threshold, attack, release, _, _) * makeup

    ERROR : The number of outputs [2] of A must be equal to the number of
    inputs [6] of gate_stereo(h..._,0.001f : *)(_)(_)

The model passed the audio signal as a trailing `_` argument to a stdlib
effect. system_prompt.txt already carried the rule ("A stdlib effect takes
only its CONTROL arguments; the audio arrives by composition"), and the
retry loop fed back only the raw stderr above — Faust's post-desugaring
lambda soup, which names no remedy — so the model repeated the mistake
verbatim. Same failure shape as the DELAY_RANGE case
(tests/test_delay_range_hint.py).

WHAT THIS PINS. A ROUTING_ARITY classification (llm/error_classes.py, the
same rule table bench/classify_failures.py uses) triggers a concrete,
class-specific repair hint APPENDED to (not replacing) the raw stderr on
retry. The hint reaches the model's NEXT attempt and does NOT appear in the
final user-facing failure message, which stays raw stderr — the same
separation _TRUNCATION_HINT and the DELAY_RANGE hint already keep.

NOT COVERED: this mocks the transport and the validator. It proves the hint
is classified and routed correctly; it does not prove a live model stops
making the mistake when it sees it — that needs a real bench run
(bench/classify_failures.py --compare, or a fresh efficacy grid) once this
lands. The dynamics few-shot added to system_prompt.txt in the same change
is covered by tests/test_prompt_stdlib.py::test_every_few_shot_example_compiles.
"""
import sys
from pathlib import Path
from unittest.mock import patch

import pytest

sys.path.insert(0, str(Path(__file__).parent.parent / "llm"))
import generate  # noqa: E402

FAUST = 'import("stdfaust.lib");\nprocess = _;'
ARITY_ERROR = (
    "ERROR : sequential composition A:gate_stereo(h..._,0.001f : *)(_)(_)\n"
    "The number of outputs [2] of A must be equal to the number of inputs "
    "[6] of gate_stereo(h..._,0.001f : *)(_)(_)"
)


class TestRoutingArityRetryHint:
    def test_routing_arity_retries_with_the_stdlib_argument_hint(self):
        """THE regression: raw stderr alone let the model repeat the
        identical arity mistake 3 attempts in a row on the groq grid."""
        sent = []

        def capture(content, *_a, **_k):
            sent.append(content)
            return FAUST

        def validate(code, *_a, **_k):
            if len(sent) == 1:
                return (False, ARITY_ERROR)
            return (True, "")

        with patch.object(generate, "_call_api", side_effect=capture), \
             patch.object(generate, "validate_faust", side_effect=validate):
            payload = generate.generate_json({"prompt": "a stereo noise gate"})

        assert payload["success"] is True
        assert len(sent) == 2, "should have retried once"
        retry = sent[1]
        assert "only its control arguments" in retry.lower()
        assert "par(i, 2, E)" in retry
        # Additive, not a replacement — the raw stderr must still be present.
        assert "gate_stereo(h..._,0.001f : *)(_)(_)" in retry

    def test_a_generic_syntax_error_gets_no_arity_hint(self):
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
        assert "only its control arguments" not in sent[1].lower()

    def test_the_final_failure_message_stays_unhinted(self):
        """error_ctx feeds _failure()'s user-facing message too — the hint is
        LLM-directed repair text ("re-emit the full program"), not something a
        human reading the error panel should see. Same separation the
        DELAY_RANGE hint keeps."""
        with patch.object(generate, "_call_api", return_value=FAUST), \
             patch.object(generate, "validate_faust", return_value=(False, ARITY_ERROR)):
            payload = generate.generate_json({"prompt": "a stereo noise gate"})

        assert payload["success"] is False
        assert payload["reason"] == "invalid_faust"
        assert payload["error"] == ARITY_ERROR, (
            "the final error must be exactly the raw stderr, with no hint appended"
        )


class TestClassificationOfTheRealGridErrors:
    """The two errors that actually recurred 3x on the 2026-08-31 groq grid
    must land in ROUTING_ARITY, or the hint above never fires for them."""

    @pytest.mark.parametrize("err", [
        "ERROR : The number of outputs [2] of A must be equal to the number "
        "of inputs [6] of gate_stereo(h..._,0.001f : *)(_)(_)",
        "ERROR : sequential composition compressor_st...ER4[nil]]]}]):B\n"
        "The number of outputs [2] of compressor_st...ER4[nil]]]}]) must be "
        "equal to the number of inputs [1] of B",
    ])
    def test_grid_arity_errors_classify_as_routing_arity(self, err):
        import error_classes as ec
        assert ec.classify_error(err) == ec.ROUTING_ARITY
        assert ec.RETRY_HINT.get(ec.ROUTING_ARITY), "ROUTING_ARITY must have a retry hint"
