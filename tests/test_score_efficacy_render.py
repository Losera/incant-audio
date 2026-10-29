"""bench/score_efficacy_render.py — render-level rescoring of efficacy archives.

Frozen-answer style, matching tests/test_render_oracle.py and
tests/test_fidelity_gate.py: expected values below are derived BY HAND from
render_oracle's own known-answer patches (bench/render_oracle.py's
_SELF_TEST corpus) and from scipy's exact binomial, before this test runs —
not read back out of the module under test.

Needs faust2sndfile (part of the Faust install this project already requires);
self-skips where it's absent, same as test_render_oracle.py.
"""
import shutil
import sys
from pathlib import Path

import pytest

ROOT = Path(__file__).parent.parent
sys.path.insert(0, str(ROOT / "bench"))

pytestmark = pytest.mark.skipif(
    shutil.which("faust2sndfile") is None,
    reason="faust2sndfile not installed (expected in the Python-only CI job)",
)

# Same known-answer patches as test_render_oracle.py's TestKnownAnswers —
# reused, not re-derived, so a drift in the oracle's own answer shows up there
# first rather than being independently (and possibly wrongly) re-justified here.
LOWPASS = ('import("stdfaust.lib");\n'
           'process = fi.resonlp(1000, 0.707, 1.0), fi.resonlp(1000, 0.707, 1.0);')
SILENT = 'import("stdfaust.lib");\nprocess = _*0, _*0;'
DEAD_DELAY = ('import("stdfaust.lib");\n'
              'dtime = hslider("Time [unit:ms]", 250, 10, 1000, 1);\n'
              'fb    = hslider("Feedback", 0.4, 0, 0.95, 0.01);\n'
              'process = _, _;')
GENERATOR = 'import("stdfaust.lib");\nprocess = os.osc(440) * 0.1;'


def _rec(effect_id, tier, category, code, **kw):
    base = {"effect_id": effect_id, "tier": tier, "category": category,
            "code": code, "first_try_compiles": True, "retry_success": False,
            "terminal_reason": "compiled", "prompt": f"prompt for {effect_id}"}
    base.update(kw)
    return base


@pytest.fixture(scope="module")
def ser():
    import score_efficacy_render as m
    return m


class TestSelection:
    """Which records count as 'compiled' and which are excluded outright."""

    def test_first_try_and_retry_rescue_both_count_as_compiled(self, ser):
        first_try = _rec("e1", "L4", "filters", LOWPASS)
        retried = _rec("e2", "L4", "filters", DEAD_DELAY,
                       first_try_compiles=False, retry_success=True)
        assert ser.is_compiled(first_try)
        assert ser.is_compiled(retried)

    def test_rate_limited_with_no_code_is_censored_not_failed(self, ser):
        rl = _rec("e3", "L1", "filters", "", first_try_compiles=False,
                  terminal_reason="rate_limited")
        assert ser.is_censored(rl)
        assert not ser.is_compiled(rl)


class TestRenderVerdict:
    """render_verdict() against known-answer patches from render_oracle."""

    def test_lowpass_renders_safely(self, ser):
        safe, _ = ser.render_verdict(_rec("e1", "L4", "filters", LOWPASS))
        assert safe is True

    def test_silent_patch_fails_the_safety_gate(self, ser):
        safe, reason = ser.render_verdict(_rec("e1", "L1", "filters", SILENT))
        assert safe is False
        assert "silent" in reason

    def test_dead_delay_compiles_clean_but_would_miss_a_tail_expectation(self, ser):
        # render_oracle's own self-test: dead_delay is green on every
        # measurement gate (ok=True) even though it would miss a stated tail
        # expectation. score_efficacy_render does not gate on tail
        # expectations (module docstring) so this must read as SAFE — proving
        # the module doesn't silently import a stricter gate than intended.
        safe, _ = ser.render_verdict(_rec("e1", "L2", "time-based", DEAD_DELAY))
        assert safe is True

    def test_generator_is_unsupported_not_pass_or_fail(self, ser):
        safe, reason = ser.render_verdict(_rec("e1", "L4", "generative", GENERATOR))
        assert safe is None
        assert "unsupported" in reason


class TestScore:
    """Per-tier aggregation: n / censored / compiled / rendered_safe / unsupported."""

    def test_tier_buckets_separate_safe_from_unsafe(self, ser):
        records = [
            _rec("e1", "L4", "filters", LOWPASS),
            _rec("e2", "L4", "filters", LOWPASS),
            _rec("e1", "L1", "filters", SILENT),
            _rec("e2", "L1", "filters", SILENT),
            _rec("e3", "L0", "filters", "", first_try_compiles=False,
                terminal_reason="rate_limited"),
        ]
        s = ser.score(records)
        assert s["by_tier"]["L4"]["n"] == 2
        assert s["by_tier"]["L4"]["compiled"] == 2
        assert s["by_tier"]["L4"]["rendered_safe"] == 2
        assert s["by_tier"]["L1"]["compiled"] == 2
        assert s["by_tier"]["L1"]["rendered_safe"] == 0
        assert s["by_tier"]["L0"]["censored"] == 1
        assert s["by_tier"]["L0"]["compiled"] == 0
        assert len(s["failures"]) == 2  # both L1 silent records

    def test_unsupported_generator_counted_separately_not_as_pass_or_fail(self, ser):
        records = [_rec("e1", "L4", "generative", GENERATOR)]
        s = ser.score(records)
        assert s["by_tier"]["L4"]["compiled"] == 1
        assert s["by_tier"]["L4"]["unsupported"] == 1
        assert s["by_tier"]["L4"]["rendered_safe"] == 0
        assert s["failures"] == []  # unsupported is not a failure


class TestPairwise:
    """McNemar over matched effect_id pairs — exact values pinned via scipy
    directly (binomtest(0, 3, 0.5, alternative='two-sided').pvalue == 0.25),
    independent of this module's own mcnemar_exact import."""

    def test_fully_discordant_tier_pair(self, ser):
        records = []
        for i in range(3):
            records.append(_rec(f"e{i}", "L4", "filters", LOWPASS))
            records.append(_rec(f"e{i}", "L1", "filters", SILENT))
        pairwise = ser.pairwise_tier_tests(records)
        pair = next(p for p in pairwise if p["tier_a"] == "L4" and p["tier_b"] == "L1")
        assert pair["n_pairs"] == 3
        assert pair["a_only"] == 3   # L4 safe, L1 unsafe, every pair
        assert pair["b_only"] == 0
        assert pair["mcnemar_p"] == pytest.approx(0.25)

    def test_unmatched_effect_ids_are_dropped_not_miscounted(self, ser):
        # e1 only exists at L4; e2 only at L1 -- neither forms a pair.
        records = [_rec("e1", "L4", "filters", LOWPASS),
                  _rec("e2", "L1", "filters", SILENT)]
        pairwise = ser.pairwise_tier_tests(records)
        pair = next(p for p in pairwise if p["tier_a"] == "L4" and p["tier_b"] == "L1")
        assert pair["n_pairs"] == 0
        assert pair["mcnemar_p"] == 1.0
