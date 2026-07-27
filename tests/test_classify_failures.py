"""Tests for bench/classify_failures.py.

The fixtures are not invented. Every `REAL_*` string below is verbatim Faust compiler
output taken from a recorded failure — `bench/results/results_20260719_claude.json` for
the benchmark corpus, and docs/BUGS.md PF-024 for the 2026-07-24 P6 battery. A classifier
tested only on strings its author wrote will classify strings its author wrote.
"""
import json
import subprocess
import sys
from pathlib import Path

import pytest

REPO = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(REPO / "bench"))

import classify_failures as cf  # noqa: E402


# ── Real compiler output ──────────────────────────────────────────────────────

# From bench/results/results_20260719_claude.json (the 2026-07-19 corpus, 3 failures).
REAL_PINGPONG = ("ERROR : after 400 evaluation steps, the compiler has detected an "
                 "endless evaluation cycle of 16 steps")
REAL_FLANGER = "/tmp/tmpautus4m4.dsp:8 : ERROR : undefined symbol : flanger_mono"
REAL_SIDECHAIN = "/tmp/tmpo4mjv_jm.dsp:17 : ERROR : syntax error, unexpected ARROW"

# From the 2026-07-24 P6 battery (docs/BUGS.md, PF-024). NOTE: these two are the
# registry's PARAPHRASE of the failure, not the compiler's own words.
REAL_STEREO_MONO = "ERROR : 2 outputs must equal 1 input"
REAL_UNBOUNDED_DELAY = "ERROR : invalid delay parameter range: interval(0,2.1e9,0)"
REAL_LOVELESS = "ERROR : syntax error, unexpected IDENT"
REAL_RE201 = "ERROR : syntax error, unexpected WITH"

# Verbatim Faust 2.85.5 output, captured 2026-07-27 by compiling the failing forms in
# .claude/skills/faust-idioms/SKILL.md. These differ from the paraphrases above, and
# that difference is the point: classifying only the paraphrase would file real runs
# as `unclassified`.
FAUST_ARITY = (
    "ERROR : sequential composition A:lowpass(2)(800)\n"
    "The number of outputs [2] of A must be equal to the number of inputs [1] "
    "of lowpass(2)(800)")
FAUST_DELAY = (
    "ERROR : possible negative values of : int(min(Delay(proj0(letrec(W0 = ...)))))\n"
    "        used in delay expression : IN[0]\n"
    "        interval(-2.14748e+09,2.14748e+09,0)")


class TestTheFourPF024Classes:
    """PF-024's failure classes must land in FOUR distinct buckets.

    This is the whole reason the module exists: score_efficacy.classify_error puts
    the cycle, the arity error and the delay range all in `SEMANTIC`, and those have
    three different fixes.
    """

    def test_ping_pong_is_a_recursion_cycle(self):
        assert cf.classify(REAL_PINGPONG).klass == cf.RECURSION_CYCLE

    def test_stereo_to_mono_is_a_routing_arity_error(self):
        assert cf.classify(REAL_STEREO_MONO).klass == cf.ROUTING_ARITY

    def test_unbounded_delay_is_a_delay_range_error(self):
        assert cf.classify(REAL_UNBOUNDED_DELAY).klass == cf.DELAY_RANGE

    def test_invented_function_is_a_hallucinated_symbol(self):
        # Verbatim from the archived corpus: the model wrote ef.flanger_mono, and
        # flanger_mono lives in pf., not ef.
        code = ('flangerMono = _ <: _ * (1 - mix), '
                '(ef.flanger_mono(dmax, cmax, depth, feedback, rate) * mix) :> _;')
        assert cf.classify(REAL_FLANGER, code).klass == cf.HALLUCINATED_SYMBOL

    def test_the_compilers_own_arity_wording_also_classifies(self):
        """The registry paraphrases; the compiler does not. Both must land."""
        assert cf.classify(FAUST_ARITY).klass == cf.ROUTING_ARITY

    def test_the_compilers_own_delay_wording_also_classifies(self):
        """An unbounded delay on 2.85.5 says "possible negative values ... used in
        delay expression", not "invalid delay parameter range". Same defect, and
        missing this form filed it as `unclassified` until 2026-07-27."""
        assert cf.classify(FAUST_DELAY).klass == cf.DELAY_RANGE

    def test_the_four_do_not_collapse_into_one_bucket(self):
        classes = {
            cf.classify(REAL_PINGPONG).klass,
            cf.classify(REAL_STEREO_MONO).klass,
            cf.classify(REAL_UNBOUNDED_DELAY).klass,
            cf.classify(REAL_FLANGER).klass,
        }
        assert len(classes) == 4, f"classes collapsed: {classes}"


class TestUndefinedSymbolSplitsIntoTwoDefects:
    """Faust says `undefined symbol : X` for two unrelated mistakes.

    Both are in the corpus. Merging them points at the wrong fix half the time, so the
    classifier splits on whether the symbol was reached through a namespace.
    """

    # Verbatim from the 2026-07-27 groq baseline: the model copied the prompt's own
    # echoCh(x) few-shot and dropped the parameter list, leaving `x` unbound.
    UNBOUND_CODE = (
        'import("stdfaust.lib");\n'
        'mix = hslider("Dry/Wet", 0.5, 0, 1, 0.01) : si.smoo;\n'
        'delayCh = x * (1 - mix) + (x : (+ : de.fdelay(maxD, dtime * ma.SR)) ~ (* (fb))) * mix;\n'
        'process = delayCh, delayCh;\n')
    UNBOUND_ERR = "maths.lib:1575 : ERROR : undefined symbol : x"

    NAMESPACED_CODE = 'x = ef.flanger_mono(dmax, cmax, depth, feedback, rate);'

    def test_bare_unbound_name_is_not_a_hallucinated_stdlib_call(self):
        assert cf.classify(self.UNBOUND_ERR, self.UNBOUND_CODE).klass == cf.UNBOUND_VARIABLE

    def test_namespaced_call_is_a_hallucinated_symbol(self):
        assert cf.classify(REAL_FLANGER, self.NAMESPACED_CODE).klass == cf.HALLUCINATED_SYMBOL

    def test_the_two_are_distinguishable(self):
        a = cf.classify(self.UNBOUND_ERR, self.UNBOUND_CODE).klass
        b = cf.classify(REAL_FLANGER, self.NAMESPACED_CODE).klass
        assert a != b

    def test_a_symbol_the_program_does_define_is_not_called_unbound(self):
        """If the name IS defined, something subtler is wrong — don't blame scope."""
        code = 'foo = 0.5;\nprocess = _ * foo;'
        assert cf.classify("ERROR : undefined symbol : foo", code).klass == cf.HALLUCINATED_SYMBOL


class TestSyntaxKeepsItsToken:
    """`unexpected IDENT` and `unexpected WITH` are different findings."""

    @pytest.mark.parametrize("error,token", [
        (REAL_LOVELESS, "IDENT"),
        (REAL_RE201, "WITH"),
        (REAL_SIDECHAIN, "ARROW"),
    ])
    def test_token_is_captured(self, error, token):
        f = cf.classify(error)
        assert f.klass == cf.SYNTAX
        assert f.detail == token
        assert f.label == f"syntax:{token}"

    def test_two_syntax_errors_with_different_tokens_are_distinguishable(self):
        assert cf.classify(REAL_LOVELESS).label != cf.classify(REAL_RE201).label


class TestLetIsTheIdentSignature:
    """The finding that started this: the system prompt recommends `let`, Faust has no
    `let`, and the compiler answers `syntax error, unexpected IDENT` — which is exactly
    what P6 #9 recorded. This test pins the classification, not the compiler.
    """

    def test_let_program_classifies_as_syntax_ident(self):
        assert cf.classify(REAL_LOVELESS).label == "syntax:IDENT"

    @pytest.mark.skipif(not __import__("shutil").which("faust"),
                        reason="faust not on PATH")
    def test_faust_really_rejects_let_with_unexpected_ident(self, tmp_path):
        """Red case for the premise. If Faust ever grows `let`, this fails and the
        prompt rule derived from it should be revisited."""
        dsp = tmp_path / "let.dsp"
        dsp.write_text('import("stdfaust.lib");\n'
                       'process = let g = 0.5; in _ * g, _ * g;\n')
        r = subprocess.run(["faust", str(dsp), "-o", "/dev/null"],
                           capture_output=True, text=True, timeout=30)
        assert r.returncode != 0, "faust accepted `let` — the PF-024 premise is stale"
        assert "unexpected IDENT" in r.stderr, r.stderr


class TestTransportIsNotACompileFailure:
    """Reuses score_efficacy.is_transport_error rather than reimplementing it."""

    def test_billing_refusal_is_transport(self):
        rec = {"errors": ["Your credit balance is too low to access the API"]}
        assert cf.classify("", "", rec).klass == cf.TRANSPORT

    def test_real_compiler_output_is_never_transport(self):
        """A record whose first error is real compiler output is a generation failure
        even if it also mentions a transport-ish word."""
        rec = {"errors": [REAL_SIDECHAIN]}
        assert cf.classify(REAL_SIDECHAIN, "", rec).klass == cf.SYNTAX

    def test_transport_is_excluded_from_the_denominator(self):
        records = [
            {"first_try_compiles": True},
            {"first_try_compiles": False, "errors": ["quota exceeded"]},
        ]
        s = cf.summarise(cf.classify_records(records))
        assert s["transport_excluded"] == 1
        assert s["denominator"] == 1
        assert s["compile_rate"] == 1.0


class TestTruncation:
    def test_output_limit_is_truncation_not_syntax(self):
        f = cf.classify("Generation was cut off at the output limit")
        assert f.klass == cf.TRUNCATED

    def test_truncation_is_checked_before_syntax(self):
        """A truncated program often ALSO produces a syntax error. Blaming the model
        for an output-budget defect is the confound 07d0997 fixed."""
        f = cf.classify("output was truncated; syntax error, unexpected EOF")
        assert f.klass == cf.TRUNCATED


class TestRecordShapes:
    def test_benchmark_shape(self):
        recs = [{"first_try_compiles": False, "error": REAL_PINGPONG,
                 "category": "time-based", "prompt": "a ping-pong delay"}]
        f = cf.classify_records(recs)[0]
        assert f.klass == cf.RECURSION_CYCLE
        assert f.category == "time-based"

    def test_efficacy_shape_with_errors_list(self):
        recs = [{"errors": [REAL_FLANGER], "category": "modulation"}]
        f = cf.classify_records(recs)[0]
        assert f.klass == cf.HALLUCINATED_SYMBOL

    def test_success_is_class_ok(self):
        assert cf.classify_records([{"first_try_compiles": True}])[0].klass == cf.OK


class TestAgainstTheArchivedCorpus:
    """End-to-end over the real archived file, if it is present."""

    ARCHIVE = REPO / "bench" / "results" / "results_20260719_claude.json"

    @pytest.mark.skipif(not ARCHIVE.exists(), reason="archive not present")
    def test_the_three_known_failures_classify_as_three_distinct_classes(self):
        records = json.loads(self.ARCHIVE.read_text())
        failures = [f for f in cf.classify_records(records) if f.klass != cf.OK]
        assert len(failures) == 3, [f.label for f in failures]
        assert {f.label for f in failures} == {
            "recursion_cycle", "hallucinated_symbol", "syntax:ARROW"}

    @pytest.mark.skipif(not ARCHIVE.exists(), reason="archive not present")
    def test_compile_rate_matches_the_recorded_22_of_25(self):
        records = json.loads(self.ARCHIVE.read_text())
        s = cf.summarise(cf.classify_records(records))
        assert (s["ok"], s["denominator"]) == (22, 25)


class TestNothingSilentlyBecomesUnclassified:
    """A classifier that answers `unclassified` for everything still passes every
    positive test above. This asserts the negative."""

    @pytest.mark.parametrize("error", [
        REAL_PINGPONG, REAL_FLANGER, REAL_SIDECHAIN, REAL_STEREO_MONO,
        REAL_UNBOUNDED_DELAY, REAL_LOVELESS, REAL_RE201,
        FAUST_ARITY, FAUST_DELAY,
    ])
    def test_every_real_error_is_classified(self, error):
        assert cf.classify(error).klass != cf.UNCLASSIFIED

    def test_genuinely_unknown_output_is_unclassified(self):
        f = cf.classify("ERROR : the flux capacitor is misaligned",
                        "import(\"stdfaust.lib\"); process = _;")
        assert f.klass == cf.UNCLASSIFIED

    def test_every_class_has_a_fix_hint(self):
        """A class nobody can act on should not exist."""
        for name in [cf.HALLUCINATED_SYMBOL, cf.ROUTING_ARITY, cf.DELAY_RANGE,
                     cf.RECURSION_CYCLE, cf.DUPLICATE_SYMBOL, cf.SYNTAX, cf.UNBOUND_VARIABLE,
                     cf.TYPE_MISMATCH, cf.INCOMPLETE, cf.TRUNCATED,
                     cf.TRANSPORT, cf.UNCLASSIFIED]:
            assert cf.FIX_HINT.get(name), f"{name} has no fix hint"
