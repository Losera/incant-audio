"""A request that never produced a program is not a compile outcome.

THE BUG (found 2026-07-25, docs/research/truncation-confound-HANDOFF-S1.md §2.1).
bench/score_efficacy.py keyed every rate on `first_try_compiles`, regardless of why
the attempt failed. In the 2026-07-20 pilot, the prompt `generative-05` was refused
by the API in all five tiers — the account had no credit, so not one token was
generated — and all five landed in the compile-failure column.

Every figure in docs/prompt_efficacy_study.md §7.1 is therefore a compile rate
containing a non-compile event, with a denominator one too large. Because the
refusal happened once per tier the SHAPE of the headline result survives, which is
exactly what made it invisible: the numbers looked plausible and were wrong.

  published   L4 90%  L3 90%  L2 80%  L1 50%  L0 60%   (n=10, includes the refusal)
  corrected   L4 100% L3 100% L2 89%  L1 56%  L0 67%   (n=9,  compile outcomes only)

Truncation is excluded on the same principle for a different reason: a program cut
off at the output cap never reached the compiler either, so scoring it as invalid
Faust blames the model for a budget defect. Detection of that lives in
llm/providers.py and is covered by tests/test_truncation_detection.py; this file
covers only what the SCORER does with it.

No network, no faust compiler.
"""
import json
import sys
from pathlib import Path

import pytest

ROOT = Path(__file__).parent.parent
sys.path.insert(0, str(ROOT / "bench"))

import score_efficacy  # noqa: E402

PILOT = ROOT / "bench/results/efficacy/pilot_20260720.json"


class TestClassification:
    @pytest.mark.parametrize("err", [
        "BadRequestError: Error code: 400 - {'type': 'error', 'error': {'type': "
        "'invalid_request_error', 'message': 'Your credit balance is too low to "
        "access the Anthropic API.'}}",
        "AuthenticationError: invalid api key",
        "RateLimitError: rate_limit_exceeded",
        "httpx.ConnectError: connection refused",
        "anthropic: output was cut off at the output limit (stop_reason=max_tokens)",
    ])
    def test_requests_that_produced_no_program_are_excluded(self, err):
        assert score_efficacy.is_transport_error(
            {"first_try_compiles": False, "errors": [err]})

    @pytest.mark.parametrize("err", [
        "/tmp/x.dsp:12 : ERROR : syntax error, unexpected SELECT2",
        "/tmp/x.dsp:8 : ERROR : undefined symbol : flanger_mono",
        "ERROR : after 400 evaluation steps, the compiler has detected an endless "
        "evaluation cycle of 16 steps",
        "ERROR : invalid delay parameter range: interval(0,2.14748e+09,0)",
        "ERROR : [file /tmp/x.dsp : 60] : multiple definitions of symbol 'process'",
    ])
    def test_real_compiler_errors_are_kept(self, err):
        """If `faust` spoke at all, a program existed. That is a compile outcome."""
        assert not score_efficacy.is_transport_error(
            {"first_try_compiles": False, "errors": [err]})

    def test_a_success_is_never_excluded(self):
        assert not score_efficacy.is_transport_error(
            {"first_try_compiles": True, "errors": []})

    def test_no_errors_recorded_is_not_a_transport_failure(self):
        assert not score_efficacy.is_transport_error(
            {"first_try_compiles": False, "errors": []})

    def test_a_compile_error_mentioning_a_timeout_is_still_a_compile_error(self):
        """validate_faust reports a pathological patch as a compiler timeout.

        The word "timeout" appears in the transport keyword list, so this is the
        obvious way for the exclusion rule to start eating real failures.
        """
        err = ("ERROR : faust compiler did not finish within 15s — the generated "
               "patch is likely pathological (unbounded delay, runaway recursion).")
        assert not score_efficacy.is_transport_error(
            {"first_try_compiles": False, "errors": [err]})


@pytest.mark.skipif(not PILOT.exists(), reason="pilot results not present")
class TestTheHistoricalCase:
    """The concrete incident, asserted end to end so it cannot silently return."""

    @pytest.fixture(scope="class")
    def records(self):
        return json.loads(PILOT.read_text())

    def test_exactly_five_records_are_excluded(self, records):
        excluded = [r for r in records
                    if not r["first_try_compiles"] and score_efficacy.is_transport_error(r)]
        assert len(excluded) == 5

    def test_they_are_one_per_tier_and_all_the_same_prompt(self, records):
        excluded = [r for r in records
                    if not r["first_try_compiles"] and score_efficacy.is_transport_error(r)]
        assert {r["tier"] for r in excluded} == {"L4", "L3", "L2", "L1", "L0"}
        assert {r["effect_id"] for r in excluded} == {"generative-05"}

    def test_corrected_first_try_rates(self, records):
        """The numbers that should have been published."""
        expected = {"L4": (9, 9), "L3": (9, 9), "L2": (8, 9), "L1": (5, 9), "L0": (6, 9)}
        got = {}
        for r in records:
            if not r["first_try_compiles"] and score_efficacy.is_transport_error(r):
                continue
            ok, n = got.get(r["tier"], (0, 0))
            got[r["tier"]] = (ok + bool(r["first_try_compiles"]), n + 1)
        assert got == expected

    def test_every_excluded_record_produced_no_code(self, records):
        """The load-bearing property: nothing was thrown away that had output."""
        for r in records:
            if not r["first_try_compiles"] and score_efficacy.is_transport_error(r):
                assert not (r.get("code") or "").strip(), (
                    f"{r['tier']}/{r['effect_id']} was excluded but has generated code — "
                    "the exclusion rule is eating real compile outcomes."
                )


class TestNoCallSiteHardcodesTheBudget:
    """The original bug was three call sites each writing `max_tokens=1024` by hand.

    ProviderSpec.min_max_tokens rescues a caller that asks for too little, but relying
    on that is what failed: the anthropic spec's floor was 0 while every recorded
    benchmark ran through it. Both layers must agree, and no caller may hardcode.
    """

    CALL_SITES = ["llm/generate.py", "bench/run_benchmark.py", "bench/run_efficacy_study.py"]

    @pytest.mark.parametrize("rel", CALL_SITES)
    def test_no_literal_max_tokens(self, rel):
        """Parsed, not grepped.

        A regex over the source text flags prose too — these files legitimately
        DISCUSS the old 1024 in docstrings, and a check that cannot tell an
        argument from a sentence is the same proxy mistake as the ADR-009 sync
        hook, which "verified one sentence and one regex" and guaranteed nothing.
        The AST sees only real keyword arguments.
        """
        import ast
        tree = ast.parse((ROOT / rel).read_text())
        hits = [
            f"line {kw.value.lineno}: max_tokens={kw.value.value}"
            for node in ast.walk(tree) if isinstance(node, ast.Call)
            for kw in node.keywords
            if kw.arg == "max_tokens" and isinstance(kw.value, ast.Constant)
            and isinstance(kw.value.value, int)
        ]
        assert not hits, (
            f"{rel} passes a literal max_tokens ({'; '.join(hits)}). Use "
            "providers.MAX_OUTPUT_TOKENS so the caller and the provider floors "
            "cannot drift apart again."
        )

    def test_every_provider_floor_matches_the_shared_constant(self):
        sys.path.insert(0, str(ROOT / "llm"))
        import providers
        low = {n: s.min_max_tokens for n, s in providers.PROVIDERS.items()
               if s.min_max_tokens < providers.MAX_OUTPUT_TOKENS}
        assert not low, (
            f"Provider spec(s) floor below MAX_OUTPUT_TOKENS "
            f"({providers.MAX_OUTPUT_TOKENS}): {low}. A spec with a lower floor can run "
            "truncated the moment a caller asks for less — which is exactly how the "
            "anthropic spec produced every recorded number at 1024."
        )
