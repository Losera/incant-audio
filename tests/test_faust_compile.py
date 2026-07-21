"""Verify all example .dsp files compile cleanly."""
import subprocess
import pytest
from pathlib import Path

EXAMPLES = list((Path(__file__).parent.parent / "examples").glob("*.dsp"))

@pytest.mark.integration
@pytest.mark.parametrize("dsp_file", EXAMPLES, ids=lambda p: p.name)
def test_example_compiles(dsp_file):
    result = subprocess.run(
        ["faust", "-lang", "cpp", str(dsp_file), "-o", "/dev/null"],
        capture_output=True, text=True, timeout=15,
    )
    assert result.returncode == 0, f"Faust error in {dsp_file.name}:\n{result.stderr}"
