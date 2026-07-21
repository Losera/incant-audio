"""Spot-check that the LLM generates valid Faust for common prompts."""
import pytest
import sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).parent.parent / "llm"))
from generate import generate_with_retry

PROMPTS = [
    "a stereo low-pass filter with cutoff and resonance",
    "a simple gain control in decibels",
    "a delay with feedback",
]

@pytest.mark.integration
@pytest.mark.parametrize("prompt", PROMPTS)
def test_llm_generates_valid_faust(prompt):
    code = generate_with_retry(prompt, max_retries=3)
    assert "process" in code
    assert "import" in code
