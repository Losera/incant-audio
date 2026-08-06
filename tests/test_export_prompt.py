"""
tests/test_export_prompt.py — Unit tests for llm/export_prompt.py
"""

import json
import subprocess
import sys
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "llm"))

import export_prompt


def test_build_payload_contains_system_prompt_and_request():
    user_req = "Analog ladder lowpass filter with resonance"
    payload = export_prompt.build_payload(user_req)

    assert "USER REQUEST:" in payload
    assert user_req in payload
    assert "# BEGIN GENERATED STDLIB REFERENCE" in payload
    assert "# END GENERATED STDLIB REFERENCE" in payload
    assert "Never define the same variable" in payload
    assert "```faust" in payload


def test_export_prompt_requires_no_api_keys_or_network(monkeypatch):
    # Strip all provider API keys from environment
    for key in ["ANTHROPIC_API_KEY", "GEMINI_API_KEY", "GROQ_API_KEY", "OPENROUTER_API_KEY"]:
        monkeypatch.delenv(key, raising=False)

    payload = export_prompt.build_payload("Tape delay effect with feedback")
    assert len(payload) > 1000
    assert "Tape delay effect with feedback" in payload


def test_export_prompt_cli_json_mode(monkeypatch):
    for key in ["ANTHROPIC_API_KEY", "GEMINI_API_KEY", "GROQ_API_KEY", "OPENROUTER_API_KEY"]:
        monkeypatch.delenv(key, raising=False)

    cmd = [sys.executable, str(ROOT / "llm" / "export_prompt.py"), "--prompt", "Stereo chorus", "--json"]
    proc = subprocess.run(cmd, capture_output=True, text=True, check=True)

    lines = proc.stdout.strip().splitlines()
    assert len(lines) == 1, "CLI --json must emit exactly one JSON line"
    data = json.loads(lines[0])
    assert data["ok"] is True
    assert "Stereo chorus" in data["payload"]
