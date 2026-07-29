"""The prompt's PROSE claims about Faust, checked against the installed compiler.

WHY THIS FILE EXISTS
--------------------
On 2026-07-27 `llm/prompts/system_prompt.txt:21` was found instructing the model to
"use let bindings or with { } blocks". Faust has `with`, `letrec` and `environment`.
It has no `let`, and that word produced the `syntax error, unexpected IDENT` the P6
battery recorded for #9. The prompt had been teaching the failure it was blamed for.

Neither existing control could see it:

  * `gen_stdlib_block.verify_prompt_references()` resolves `ns.func` tokens. `let` is
    not a namespaced function, so there was nothing to resolve.
  * `test_prompt_stdlib.test_every_few_shot_example_compiles` compiles the few-shots.
    The claim was not in a few-shot; it was in a bullet.

`docs/BUGS.md` proposed "extract every construct the prompt recommends and compile
it". That would NOT have caught this one: the sentence contained no code span to
extract. So the coverage is split three ways, and only the third is new territory:

  L1  every prose DEFINITION the prompt shows as correct must compile
  L2  every prose COUNTER-EXAMPLE must fail, with the error the prompt claims
  L3  a construct from another language, recommended rather than denied
      -> .claude/hooks/check_prompt_invariants.py, which needs no compiler and so
         can run on every edit instead of only in CI

L0, below, is what keeps L1/L2 honest: the fixture and the prompt are checked
against each other in BOTH directions. A claim added to the prompt with no fixture
entry fails; a fixture entry whose snippet has left the prompt fails. Without that,
this file becomes another `attention-report` — confidently reporting on a document
it no longer matches.

NOT COVERED, deliberately:
  * Bare-expression claims that are not definitions and carry no quoted error. The
    extractors find definitions and quoted error strings; a new claim written as
    neither passes unseen.
  * Whether a claim is USEFUL. `_,_ : E` really does fail, but nothing here can say
    whether telling the model so improves generation. That is the benchmark's job.
"""
import json
import re
import subprocess
import shutil
from pathlib import Path

import pytest

ROOT = Path(__file__).parent.parent
PROMPT_FILE = ROOT / "llm" / "prompts" / "system_prompt.txt"
FIXTURE = ROOT / "tests" / "fixtures" / "prompt_claims.json"

BEGIN_MARKER = "# BEGIN GENERATED STDLIB REFERENCE"

# A definition: optional argument list, `=`, then a body that may contain balanced
# `{ }` groups (a with-block's internal semicolons must not end the match).
DEF_RE = re.compile(r"\b[a-zA-Z_]\w*\s*(?:\([^)]*\))?\s*=\s*(?:[^;{]|\{[^}]*\})+;(?:\s*\};)?")

# Quoted spans of >=3 words in prose read as claims about compiler output.
# Shorter ones are hslider labels and file names ("Label [unit:Hz]", "stdfaust.lib").
QUOTED_RE = re.compile(r'"([^"]{3,})"')

faust_required = pytest.mark.skipif(
    shutil.which("faust") is None, reason="faust not installed"
)


def _fixture() -> dict:
    return json.loads(FIXTURE.read_text())


def _prose() -> str:
    """The hand-written part of the prompt: everything before the generated block.

    The few-shots after it are already covered by test_prompt_stdlib.py; this file
    is about the rules, which nothing compiled until now.
    """
    return PROMPT_FILE.read_text().split(BEGIN_MARKER)[0]


def _flat_prose() -> str:
    """Line wrapping collapsed — the rules are bullets that wrap mid-statement."""
    return re.sub(r"\s+", " ", _prose())


def _compile(program: str) -> subprocess.CompletedProcess:
    import tempfile
    with tempfile.NamedTemporaryFile("w", suffix=".dsp", delete=False) as f:
        f.write(program)
        path = f.name
    return subprocess.run(
        ["faust", path, "-o", "/dev/null"], capture_output=True, text=True, timeout=60
    )


def _emit_cpp(program: str) -> str:
    """Compile to C++ and return the source, so a folded constant can be read back.

    Used only by the `expressions` checks: select2's argument ORDER and which side
    (_ , !) keeps are semantic claims that both orders satisfy syntactically, so
    compiling proves nothing about them. Faust folds these constant programs, and the
    surviving literal is the answer.
    """
    import tempfile
    with tempfile.NamedTemporaryFile("w", suffix=".dsp", delete=False) as f:
        f.write(program)
        path = f.name
    r = subprocess.run(
        ["faust", "-lang", "cpp", path], capture_output=True, text=True, timeout=60
    )
    assert r.returncode == 0, f"expression program did not compile:\n{r.stderr[:400]}"
    return r.stdout


# ------------------------------------------------------------------ L0 completeness

class TestFixtureAndPromptAgree:
    """Neither file may drift alone. This is the anti-rot property."""

    def test_every_fixture_definition_is_still_in_the_prompt(self):
        flat = _flat_prose()
        missing = [
            e["snippet"] for e in _fixture()["definitions"]
            if re.sub(r"\s+", " ", e["snippet"]) not in flat
        ]
        assert not missing, (
            "Fixture entries whose snippet is no longer in the prompt:\n  "
            + "\n  ".join(missing)
            + "\nThe prompt changed and the fixture did not. Update or drop the entry — "
              "a fixture describing a prompt that no longer exists is the attention-report "
              "failure mode."
        )

    def test_every_prompt_definition_is_in_the_fixture(self):
        known = {re.sub(r"\s+", " ", e["snippet"]) for e in _fixture()["definitions"]}
        found = {re.sub(r"\s+", " ", m) for m in DEF_RE.findall(_flat_prose())}
        unaccounted = sorted(found - known)
        assert not unaccounted, (
            "Definition(s) in the prompt's prose with no fixture entry:\n  "
            + "\n  ".join(unaccounted)
            + "\nAdd each to tests/fixtures/prompt_claims.json with a verdict "
              "(compiles / fails / template) so it is checked rather than assumed."
        )

    def test_every_quoted_error_claim_is_in_the_fixture(self):
        claimed = {c["prompt_claim"] for c in _fixture()["error_claims"]}
        found = {
            q for q in QUOTED_RE.findall(_flat_prose())
            if len(q.split()) >= 3 and q not in claimed
        }
        # A quoted span inside a definition we already track is code, not a claim.
        defs = " ".join(e["snippet"] for e in _fixture()["definitions"])
        found = {q for q in found if q not in defs}
        assert not found, (
            "The prompt quotes compiler error(s) that nothing verifies:\n  "
            + "\n  ".join(sorted(repr(q) for q in found))
            + "\nAdd an error_claims entry with a program that reproduces it. A prompt "
              "asserting what the compiler says, unchecked, is a claim with no evidence."
        )

    def test_fixture_is_not_vacuous(self):
        """Guards the extractors. A regex that silently matches nothing would make
        every test above pass by finding no work to do."""
        fx = _fixture()
        assert len(fx["definitions"]) >= 4, "suspiciously few definitions tracked"
        assert len(fx["error_claims"]) >= 2, "suspiciously few error claims tracked"
        assert len(fx["expressions"]) >= 3, "suspiciously few expression claims tracked"
        assert DEF_RE.findall(_flat_prose()), "DEF_RE matched nothing — extractor broke"

    def test_every_expression_claim_is_still_in_the_prompt(self):
        """Forward direction only, and the limit is stated rather than implied.

        Each `expressions` entry names a construct the prose asserts; if the prose
        stops asserting it, the entry is stale and must go. The REVERSE direction —
        finding an expression claim in the prose that has no entry — is still not
        mechanised, because there is no reliable way to extract "a claim written as a
        bare expression" from prose. That remains this file's blind spot; it is now a
        narrower one, since the three constructs the 2026-07-29 edit added are pinned.
        """
        flat = _flat_prose()
        missing = [
            e["prompt_claim"] for e in _fixture()["expressions"]
            if re.sub(r"\s+", " ", e["prompt_claim"]) not in flat
        ]
        assert not missing, (
            "Expression claim(s) in the fixture that the prompt no longer makes:\n  "
            + "\n  ".join(missing)
            + "\nDrop the entry, or restore the claim. A fixture asserting things about "
              "a prompt that has moved on is the attention-report failure mode."
        )


# ------------------------------------------------------------- L1/L2 the claims hold

@faust_required
class TestClaimsHoldAgainstTheCompiler:

    def test_positive_definitions_compile(self):
        bad = []
        for e in _fixture()["definitions"]:
            if e["verdict"] != "compiles":
                continue
            r = _compile(e["program"])
            if r.returncode != 0:
                bad.append(f"{e['snippet']}\n      {r.stderr.strip().splitlines()[0][:110]}")
        assert not bad, (
            "The prompt shows these as CORRECT, and they do not compile:\n  "
            + "\n  ".join(bad)
        )

    def test_counter_examples_fail_with_the_claimed_error(self):
        bad = []
        for e in _fixture()["definitions"]:
            if e["verdict"] != "fails":
                continue
            r = _compile(e["program"])
            if r.returncode == 0:
                bad.append(f"{e['snippet']} — COMPILED, but the prompt says it fails")
            elif e["expect_error"] not in r.stderr:
                bad.append(
                    f"{e['snippet']} — failed, but not as claimed.\n"
                    f"      expected: {e['expect_error']!r}\n"
                    f"      actual:   {r.stderr.strip().splitlines()[0][:100]!r}"
                )
        assert not bad, "Counter-example(s) the prompt describes wrongly:\n  " + "\n  ".join(bad)

    def test_quoted_error_claims_reproduce(self):
        bad = []
        for c in _fixture()["error_claims"]:
            r = _compile(c["program"])
            if r.returncode == 0:
                bad.append(f"{c['prompt_claim']!r} — program compiled; no error to claim")
            elif c["expect_error"] not in r.stderr:
                bad.append(
                    f"{c['prompt_claim']!r} — expected {c['expect_error']!r} in stderr, "
                    f"got {r.stderr.strip().splitlines()[0][:90]!r}"
                )
        assert not bad, "Quoted error claim(s) that do not reproduce:\n  " + "\n  ".join(bad)

    def test_expression_claims_select_the_side_the_prompt_says(self):
        """The order/side claims, checked by VALUE rather than by compiling.

        This is the test the 2026-07-29 prompt edit needed and did not have. Both
        select2 argument orders compile; both (_ , !) and (! , _) compile and have
        identical arity. A prompt that taught either one backwards would produce
        generated plugins that compile, load, run, and invert every conditional or
        route the wrong channel — the same shape of defect as PF-032, where valid
        Faust rendered silence.
        """
        bad = []
        for e in _fixture()["expressions"]:
            cpp = _emit_cpp(e["program"])
            kept, dropped = e["expect_constant"], e["reject_constant"]
            if kept not in cpp:
                bad.append(
                    f"{e['prompt_claim']} — expected the constant {kept} to survive "
                    f"folding and it did not. The prompt teaches this backwards."
                )
            elif dropped in cpp:
                bad.append(
                    f"{e['prompt_claim']} — {dropped} survived too, so the program did "
                    f"not fold to one branch and this check proves nothing. Fix the "
                    f"fixture program, do not delete the assertion."
                )
        assert not bad, (
            "Expression claim(s) whose semantics do not match the prompt:\n  "
            + "\n  ".join(bad)
        )

    def test_verbatim_claims_really_are_verbatim(self):
        """A claim marked verbatim must appear in stderr exactly as the prompt quotes it.

        `2 outputs must equal 1 input` is marked verbatim:false precisely because it
        does not — Faust says "The number of outputs [2] of A must be equal to the
        number of inputs [1] of ...". This test stops a paraphrase being marked
        verbatim by accident; it does not object to paraphrases that say so.
        """
        bad = []
        for c in _fixture()["error_claims"]:
            if not c.get("verbatim"):
                continue
            r = _compile(c["program"])
            if c["prompt_claim"] not in r.stderr:
                bad.append(
                    f"{c['prompt_claim']!r} is marked verbatim but does not appear in "
                    f"stderr: {r.stderr.strip().splitlines()[0][:90]!r}"
                )
        assert not bad, "\n  ".join(bad)
