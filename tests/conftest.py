"""Shared pytest configuration and fixtures."""
import os
import sys
from pathlib import Path

PROJECT_ROOT = Path(__file__).parent.parent

# Must be set before any llm module is imported so anthropic.Anthropic() finds a key.
os.environ.setdefault("ANTHROPIC_API_KEY", "")

# Make llm/ importable from all test modules.
sys.path.insert(0, str(PROJECT_ROOT / "llm"))
