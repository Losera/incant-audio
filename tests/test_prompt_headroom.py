"""The system prompt and the output budget share one 8,000-token allowance on groq.

THE DEFECT THIS GUARDS (measured live 2026-07-28, openai/gpt-oss-120b, free tier).
groq admits a request only when

    prompt_tokens + max_tokens <= 8000

and rejects with a NON-RETRYABLE 413 "Request too large" when it does not. The
boundary is exact: at prompt_tokens=3283, max_tokens=4717 is admitted and 4718 is
refused. `_post_with_backoff` retries 429 and >=500 only, so a 413 does not retry —
it surfaces as an untyped RuntimeError and the generation is simply dead.

At the 2026-07-28 calibration, production sat at 92% of that allowance:

    3283 (llm/prompts/system_prompt.txt, + a short user message)
  + 4096 (providers.MAX_OUTPUT_TOKENS)
  = 7379 of 8000        ->  621 tokens of slack

621 is the RAW slack. It is not the budget a prompt edit may spend, because
estimate_prompt_tokens() applies SAFETY_FACTOR below: at the calibration point the
estimate was 3447 rather than 3283, so this module's assertions tripped at **457**
tokens of slack rather than 621. Plan an edit against the estimator, not the raw
figure. Run this file as a script to print the live number.

46% of that prompt is the stdlib block, GENERATED from the installed
/usr/share/faust/*.lib by tools/gen_stdlib_block.py. So the failure mode this file
exists to catch is: someone upgrades Faust, the stdlib block grows, and every groq
request begins failing with a hard 413 — an outage on the volume provider, caused by
upgrading an unrelated dependency, with nothing in the test suite objecting. Nobody
would connect the two.

*** THE MARGIN IS NOW THIN, AND THIS IS THE PLACE THAT SAYS SO. *** The PF-024/PF-032
prompt edit (2026-07-29) spent most of the slack, deliberately and with the user's
decision on record. Measured after it:

    prompt         12,414 chars   (was 11,505 -> 7.9% above the calibration anchor)
    stdlib block    5,737 chars   (46% of the file)
    slack             185 tokens  (was 457)  = ~617 chars of room left
    trips at         10.8% growth of the stdlib block   (was ~29%)

A Faust upgrade adding a tenth to the block now takes groq down. That is a real
change in exposure, not a documentation detail, and the bounds in
TestTheGuardCanActuallyFail were re-bracketed to match it rather than relaxed to
hide it — see that class for the arithmetic.

**A re-measure is due.** At 7.9% drift the anchor still describes the file within
tolerance (test_calibration_anchor_still_describes_the_file allows 10%), but the
3283 figure is now an extrapolation over 909 characters it never saw. Re-measuring
costs ONE live groq generation: send this exact file and read usage.prompt_tokens.
Do it on the next authorized run and update MEASURED_* below.

WHY AN ESTIMATE AND NOT A TOKENIZER. No tokenizer for this model is installed, and
adding one is a dependency change (COLLABORATION.md §2 trigger 4). Instead the
estimate is CALIBRATED against a real measurement — groq's own usage.prompt_tokens
for this exact file — and scaled linearly by character count, then made
deliberately pessimistic. That is weaker than tokenizing and stronger than nothing:
it cannot tell you the exact count, but it fires well before a 413 reaches
production.

WHAT THIS DOES NOT CATCH, stated per COLLABORATION.md §7:
  * It is calibrated on ONE model on ONE provider on ONE day. TPM limits are
    server-side and move without notice.
  * It models a SHORT user message. See TestUserPromptAllowance below — the slack
    is consumed by whatever the user types, and nothing anywhere enforces that.
  * It says nothing about tokens-per-DAY (200,000 on this account), which is the
    limit that actually blocks the efficacy study.

No network. No faust compiler. No tokenizer.
"""
import sys
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "llm"))
import providers  # noqa: E402

PROMPT_PATH = ROOT / "llm" / "prompts" / "system_prompt.txt"

# ── The calibration anchor ────────────────────────────────────────────────────
# Measured live 2026-07-28 against groq openai/gpt-oss-120b. usage.prompt_tokens
# for a request carrying exactly this file as the system message plus the short
# user message "a simple gain control in decibels". Includes chat-template
# overhead, which is what the TPM check actually counts.
MEASURED_CHARS = 11505
MEASURED_PROMPT_TOKENS = 3283

# groq gpt-oss-120b, free tier "on_demand", read from x-ratelimit-limit-tokens and
# corroborated by the 413 body ("Limit 8000, Requested 10783").
GROQ_TPM_LIMIT = 8000

# The estimate is scaled up by this much before being asserted on. Punctuation-dense
# Faust with dotted identifiers (ba.linear2log) tokenizes worse than the prose this
# ratio was calibrated across, and a guard that trips slightly early is useful while
# a guard that trips slightly late is not.
SAFETY_FACTOR = 1.05


def estimate_prompt_tokens(text: str) -> int:
    """Calibrated linear estimate of what groq will count for this system prompt.

    Exact at the calibration point by construction; pessimistic by SAFETY_FACTOR
    everywhere else. Deliberately NOT a general tokenizer — it is only meaningful
    for text of the same character as system_prompt.txt.
    """
    ratio = MEASURED_PROMPT_TOKENS / MEASURED_CHARS
    return int(len(text) * ratio * SAFETY_FACTOR)


def headroom_tokens(text: str, max_output_tokens: int) -> int:
    """Tokens of slack before groq refuses the request with a 413. May be negative."""
    return GROQ_TPM_LIMIT - estimate_prompt_tokens(text) - max_output_tokens


class TestTheRealPromptStillFits:
    def test_calibration_anchor_still_describes_the_file(self):
        """If the prompt changed size, the anchor is stale — say so loudly.

        This is not a size limit. It fires when the measured pair above no longer
        describes the file, which means the 3283 figure is now an extrapolation
        rather than a measurement, and someone should re-measure rather than trust
        a scaled guess indefinitely.
        """
        actual = len(PROMPT_PATH.read_text())
        drift = abs(actual - MEASURED_CHARS) / MEASURED_CHARS
        assert drift < 0.10, (
            f"system_prompt.txt is {actual} chars; the 2026-07-28 calibration was "
            f"measured at {MEASURED_CHARS} ({drift:.0%} drift). The token estimate "
            f"below is now an extrapolation. Re-measure with a live groq request "
            f"(read usage.prompt_tokens) and update MEASURED_* in this file."
        )

    def test_prompt_plus_output_budget_fits_under_the_groq_tpm_limit(self):
        """The guard proper. Fails BEFORE production starts returning 413s."""
        text = PROMPT_PATH.read_text()
        slack = headroom_tokens(text, providers.MAX_OUTPUT_TOKENS)
        assert slack > 0, (
            f"system_prompt.txt (~{estimate_prompt_tokens(text)} tokens) + "
            f"MAX_OUTPUT_TOKENS ({providers.MAX_OUTPUT_TOKENS}) exceeds groq's "
            f"{GROQ_TPM_LIMIT}-token admission limit by {-slack} tokens. EVERY groq "
            f"request will fail with a non-retryable 413 'Request too large' — not a "
            f"slowdown, a hard outage on the volume provider.\n"
            f"Most likely cause: the generated stdlib block grew after a Faust "
            f"upgrade (tools/gen_stdlib_block.py). Fix by shrinking the prompt or "
            f"lowering MAX_OUTPUT_TOKENS — but note that lowering it below ~2000 "
            f"reintroduces the truncation confound this project already fixed once "
            f"(docs/research/truncation-confound-HANDOFF-S1.md)."
        )


class TestTheGuardCanActuallyFail:
    """CLAUDE.md: 'A control counts only once it has been seen failing.'

    Five enforcement hooks in this project were found on 2026-07-25 never to have
    run. So the red case is not a thought experiment recorded in a comment — it is
    a test, and it fails if the checker ever stops rejecting an oversized prompt.
    """

    def test_an_oversized_prompt_is_rejected(self):
        bloated = "x" * 30000
        assert headroom_tokens(bloated, providers.MAX_OUTPUT_TOKENS) < 0

    def test_a_realistic_stdlib_block_growth_trips_the_guard(self):
        """The concrete predicted failure, pinned so the docstring cannot rot.

        Growth is applied to the STDLIB BLOCK's share of the file (39%), not to the
        file, because those differ by more than 2x and conflating them is the error
        this test was written to stop repeating.

        THE BOUNDS HAVE NOW MOVED TWICE, IN BOTH DIRECTIONS, AND THAT IS THE POINT.
        Originally 20% / 50%, bracketing a measured threshold of ~29%. The
        PF-024/PF-032 prompt edit spent 909 characters and the threshold fell to
        **10.8%**, so on 2026-07-29 they were re-bracketed to 5% / 20%.

        On 2026-07-31 they moved back to 20% / 50%, because the threshold rose to
        **35.7%**. This is the case the old 20% assertion message named in advance —
        "either the prompt shrank a lot or the limits moved" — and the prompt shrank
        a lot: tools/gen_stdlib_block.py's curated list lost 13 entries and the block
        went 5737 -> 4505 chars, the file 12619 -> 11419, slack 124 -> 483 tokens.

        That is the sanctioned action, not a workaround. The 2026-07-29 note said: if
        a future edit pushes the threshold under 5%, do NOT widen the bounds — buy
        headroom back by trimming the curated list. The threshold was heading there
        (36 chars of drift room remained), the list was trimmed on evidence, and
        these bounds follow the file rather than leading it.

        So, for the reader who suspects a test tuned until it passed: the assertion
        did not change, the file did, and it moved in the direction that makes the
        guard LESS likely to fire, which is the direction that costs safety. Confirm
        with

            python tests/test_prompt_headroom.py

        which prints the live slack; recompute 35.7% as slack_chars/block_chars. If a
        future edit pushes the threshold under 20%, buy headroom back again — do not
        widen these bounds.
        """
        text = PROMPT_PATH.read_text()
        stdlib_chars = 4505          # measured 2026-07-31; the generated block today

        mild = text + "y" * int(stdlib_chars * 0.20)
        assert headroom_tokens(mild, providers.MAX_OUTPUT_TOKENS) > 0, (
            "A 20% stdlib growth now trips the guard. Headroom is under ~900 chars, "
            "which is a handful of function entries — the prompt is filling up again. "
            "Buy room back (trim tools/gen_stdlib_block.py's curated list) rather "
            "than widening this bound."
        )

        severe = text + "y" * int(stdlib_chars * 0.50)
        assert headroom_tokens(severe, providers.MAX_OUTPUT_TOKENS) < 0, (
            "A 50% growth in the generated stdlib block no longer trips this "
            "guard — either the prompt shrank a lot or the limits moved. Re-measure."
        )

    def test_the_measured_boundary_reproduces(self):
        """4717 admitted / 4718 refused at prompt_tokens=3283, measured 2026-07-28.

        Uses the MEASURED token count directly rather than the estimator, so this
        asserts the arithmetic of the admission rule, not the quality of the guess.
        """
        assert MEASURED_PROMPT_TOKENS + 4717 <= GROQ_TPM_LIMIT
        assert MEASURED_PROMPT_TOKENS + 4718 > GROQ_TPM_LIMIT


class TestUserPromptAllowance:
    """The slack is not spare capacity — the user's typed prompt is spent from it.

    NOT YET ENFORCED ANYWHERE. host/Source/PromptPanel.cpp sends whatever is typed,
    and .claude/agents/generation-stress-tester.md is explicitly built around
    pasting '200-line plugin specifications' into it. This test records the ceiling
    so the number is written down somewhere executable; it does not police the
    input path, which would be a host-side change.
    """

    def test_the_user_prompt_ceiling_is_small_enough_to_be_worth_knowing(self):
        text = PROMPT_PATH.read_text()
        slack = headroom_tokens(text, providers.MAX_OUTPUT_TOKENS)
        approx_chars = int(slack * (MEASURED_CHARS / MEASURED_PROMPT_TOKENS))
        # ~1,700 chars is a paragraph, not a spec sheet. If this ever becomes
        # generous the risk has gone away and the test should be deleted.
        assert approx_chars < 6000, (
            f"User prompts up to ~{approx_chars} chars now fit. That is roomy "
            f"enough that this warning may be obsolete — re-check and delete."
        )


if __name__ == "__main__":
    t = PROMPT_PATH.read_text()
    print(f"chars={len(t)} est_tokens={estimate_prompt_tokens(t)} "
          f"max_output={providers.MAX_OUTPUT_TOKENS} "
          f"slack={headroom_tokens(t, providers.MAX_OUTPUT_TOKENS)}")
