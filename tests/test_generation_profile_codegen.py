"""The C++ family resolver is generated from the Python catalog."""

import subprocess
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parent.parent


def test_generated_generation_profiles_are_current():
    result = subprocess.run(
        [sys.executable, str(ROOT / "tools" / "gen_generation_profiles.py"), "--check"],
        cwd=ROOT,
    )
    assert result.returncode == 0, "run tools/gen_generation_profiles.py"
