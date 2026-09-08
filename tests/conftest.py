"""Shared pytest configuration and fixtures."""
import os
import sys
from pathlib import Path

PROJECT_ROOT = Path(__file__).parent.parent

# Must be set before any llm module is imported so anthropic.Anthropic() finds a key.
os.environ.setdefault("ANTHROPIC_API_KEY", "")

# SUBTLE: llm/generate.py calls load_dotenv() at import, so PluginForge/.env leaks
# into the test process. Without this pin, a developer whose .env selects a live
# free provider would have the unit tests — which only mock the *anthropic* client —
# silently dispatch real network calls and burn free-tier quota. setdefault, not
# a hard set, so `PLUGINFORGE_PROVIDER=groq pytest ...` still works deliberately.
os.environ.setdefault("PLUGINFORGE_PROVIDER", "anthropic")
os.environ.setdefault("PLUGINFORGE_MODEL", "")

# Same reasoning as the provider pin above, made explicit rather than
# incidental: unit tests use "anthropic" purely as a mocked stand-in and
# never spend real money regardless of which provider they nominally
# select, so the ADR-012 free-only guard (providers.assert_free) is not a
# concern they need to satisfy per-test. This used to be true by ACCIDENT --
# assert_free() was only ever reachable through generate.py's __main__
# block, which no unit test calls (they call _run_subprocess_mode()
# directly) -- until 2026-09-04's fix moved the check inside
# _run_subprocess_mode so it could see a request's own provider override,
# which made every one of those tests trip the paid-provider guard for the
# first time. setdefault, not a hard set, so a test that deliberately wants
# to exercise the guard (see TestRequestFileMode's real-subprocess test,
# which pops this key from its OWN copied env before spawning) still can.
os.environ.setdefault("PLUGINFORGE_ALLOW_PAID", "1")

# Make llm/ importable from all test modules.
sys.path.insert(0, str(PROJECT_ROOT / "llm"))
