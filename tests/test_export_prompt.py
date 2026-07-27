"""Unit tests for llm/export_prompt.py — the BYO-LLM ("spend your own inference")
prompt exporter.

The whole point of BYO is that building a payload calls no provider, reads no API
key, and touches no network. These tests pin exactly that, alongside the payload's
content contract and the intake fence-stripper.

No network, no API key, no cost.
"""
import json
import sys
from pathlib import Path

import pytest

# conftest.py puts llm/ on sys.path.
import export_prompt

# The exporter must load the SAME prompt generate.py uses. Read it here the same
# way, so the "full system prompt is embedded verbatim" assertion is grounded.
SYSTEM_PROMPT = (Path(__file__).parent.parent / "llm" / "prompts"
                 / "system_prompt.txt").read_text()

# Every provider credential / selection var in llm/providers.py, so the no-key
# test can delete all of them (grep: providers.py env_var fields + PLUGINFORGE_*).
PROVIDER_ENV_VARS = (
    "PLUGINFORGE_PROVIDER",
    "PLUGINFORGE_MODEL",
    "PLUGINFORGE_ALLOW_PAID",
    "ANTHROPIC_API_KEY",
    "GOOGLE_API_KEY",
    "GROQ_API_KEY",
    "OPENROUTER_API_KEY",
)


class TestPayloadContent:
    def test_embeds_the_full_system_prompt_verbatim(self):
        payload = export_prompt.build_payload("a warm tape delay")
        assert SYSTEM_PROMPT in payload
        # And it is the real prompt, not a stub — spot-check a load-bearing line.
        assert 'import("stdfaust.lib");' in payload

    def test_contains_the_user_request_verbatim(self):
        request = "a shimmering stereo reverb with size and mix"
        payload = export_prompt.build_payload(request)
        assert request in payload

    def test_has_a_delimited_user_request_section(self):
        payload = export_prompt.build_payload("x")
        assert "USER REQUEST:" in payload

    def test_instructs_a_single_faust_fenced_reply(self):
        # Downstream fence-stripping is only deterministic if the external LLM is
        # told to return exactly one ```faust block.
        payload = export_prompt.build_payload("x")
        assert "```faust" in payload

    def test_deterministic_for_a_fixed_request(self):
        request = "a gentle low-pass filter"
        assert export_prompt.build_payload(request) == export_prompt.build_payload(request)


class TestNoProviderNoKeyNoNetwork:
    def test_build_payload_works_with_every_provider_var_unset(self, monkeypatch):
        # conftest pins PLUGINFORGE_PROVIDER=anthropic for the session; override it
        # locally (not globally) by deleting all provider vars for this test only.
        for var in PROVIDER_ENV_VARS:
            monkeypatch.delenv(var, raising=False)

        request = "a bit-crusher with drive"
        payload = export_prompt.build_payload(request)

        assert SYSTEM_PROMPT in payload
        assert request in payload

    def test_build_payload_does_not_import_the_provider_module(self, monkeypatch):
        # Strong form of "no provider call": building a payload must not even
        # import llm/providers.py (which is where any client/key/network lives).
        for var in PROVIDER_ENV_VARS:
            monkeypatch.delenv(var, raising=False)
        monkeypatch.delitem(sys.modules, "providers", raising=False)

        export_prompt.build_payload("no imports please")

        assert "providers" not in sys.modules, (
            "build_payload imported the provider module — the BYO export path must "
            "stay provider-free."
        )


class TestStripCodeFence:
    FAUST = 'import("stdfaust.lib");\nprocess = _ , _;'

    def test_strips_faust_fence(self):
        assert export_prompt.strip_code_fence(f"```faust\n{self.FAUST}\n```") == self.FAUST

    def test_strips_bare_fence(self):
        assert export_prompt.strip_code_fence(f"```\n{self.FAUST}\n```") == self.FAUST

    def test_leaves_unfenced_code_unchanged(self):
        assert export_prompt.strip_code_fence(self.FAUST) == self.FAUST

    def test_tolerates_missing_closing_fence(self):
        assert export_prompt.strip_code_fence(f"```faust\n{self.FAUST}") == self.FAUST


class TestCli:
    def test_prompt_mode_prints_payload(self, capsys):
        rc = export_prompt.main(["--prompt", "a stereo chorus"])
        out = capsys.readouterr().out
        assert rc == 0
        assert "a stereo chorus" in out
        assert SYSTEM_PROMPT in out

    def test_json_mode_prints_one_adr011_line(self, capsys):
        rc = export_prompt.main(["--prompt", "a stereo chorus", "--json"])
        out = capsys.readouterr().out
        assert rc == 0
        # Exactly one JSON object on one line.
        line = out.strip()
        assert "\n" not in line
        obj = json.loads(line)
        assert obj["ok"] is True
        assert "a stereo chorus" in obj["payload"]
        assert SYSTEM_PROMPT in obj["payload"]

    def test_json_mode_without_prompt_is_a_clean_error(self, capsys):
        rc = export_prompt.main(["--json"])
        out = capsys.readouterr().out
        assert rc == 0
        obj = json.loads(out.strip())
        assert obj["ok"] is False
        assert obj["error"]
