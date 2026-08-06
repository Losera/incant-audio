"""
tests/test_prompt_builder.py — Unit tests for llm/prompt_builder.py
"""

import sys
from pathlib import Path
import pytest

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "llm"))

import prompt_builder
import providers


def test_dynamic_prompt_builder_shrinks_prompt_for_filter():
    user_prompt = "Design a 4-pole lowpass filter with resonance control."
    full_prompt = prompt_builder.SYSTEM_PROMPT_PATH.read_text()
    dynamic_prompt = prompt_builder.build_dynamic_prompt(user_prompt)
    
    assert len(dynamic_prompt) < len(full_prompt)
    assert "# BEGIN GENERATED STDLIB REFERENCE" in dynamic_prompt
    assert "# END GENERATED STDLIB REFERENCE" in dynamic_prompt
    assert "Never define the same variable" in dynamic_prompt
    assert "process" in dynamic_prompt


def test_dynamic_prompt_builder_preserves_headroom():
    user_prompt = "Create a distortion fuzz pedal with soft clipping."
    dynamic_prompt = prompt_builder.build_dynamic_prompt(user_prompt)
    
    slack = providers.headroom_tokens(dynamic_prompt, providers.MAX_OUTPUT_TOKENS)
    assert slack > 300, f"Expected >300 tokens slack, got {slack}"
