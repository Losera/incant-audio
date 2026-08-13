"""Verify project scaffolding integrity — no external tools required."""
import pytest
from pathlib import Path

ROOT = Path(__file__).parent.parent

REQUIRED_FILES = [
    "CLAUDE.md",
    ".env.example",
    "host/CMakeLists.txt",
    "host/Source/PluginProcessor.h",
    "host/Source/PluginProcessor.cpp",
    "host/Source/PluginEditor.h",
    "host/Source/PluginEditor.cpp",
    "host/Source/FaustEngine.h",
    "host/Source/FaustEngine.cpp",
    "host/Source/ParamPool.h",
    "host/Source/ParamPool.cpp",
    "llm/generate.py",
    "llm/faust_validator.py",
    "llm/prompts/system_prompt.txt",
    "examples/gain.dsp",
    "examples/lowpass.dsp",
    "examples/chorus.dsp",
    "examples/compressor.dsp",
    "docs/architecture.md",
    "docs/goals_and_next_steps.md",
]


@pytest.mark.parametrize("rel_path", REQUIRED_FILES)
def test_required_file_exists(rel_path):
    assert (ROOT / rel_path).exists(), f"Missing scaffolding file: {rel_path}"


class TestSystemPrompt:
    def test_not_empty(self):
        content = (ROOT / "llm/prompts/system_prompt.txt").read_text()
        assert len(content) > 100

    def test_has_import_rule(self):
        content = (ROOT / "llm/prompts/system_prompt.txt").read_text()
        assert 'import("stdfaust.lib")' in content

    def test_has_process_rule(self):
        content = (ROOT / "llm/prompts/system_prompt.txt").read_text()
        assert "process" in content

    def test_has_hslider_rule(self):
        content = (ROOT / "llm/prompts/system_prompt.txt").read_text()
        assert "hslider" in content

    def test_has_at_least_two_examples(self):
        content = (ROOT / "llm/prompts/system_prompt.txt").read_text()
        assert content.count("process =") >= 2


class TestEnvTemplate:
    def test_has_api_key_placeholder(self):
        content = (ROOT / ".env.example").read_text()
        assert "ANTHROPIC_API_KEY" in content

    def test_has_juce_path_placeholder(self):
        content = (ROOT / ".env.example").read_text()
        assert "JUCE_PATH" in content


class TestExampleDspFiles:
    def test_at_least_four_examples(self):
        examples = list((ROOT / "examples").glob("*.dsp"))
        assert len(examples) >= 4

    @pytest.mark.parametrize("dsp", ["gain.dsp", "lowpass.dsp", "chorus.dsp", "compressor.dsp"])
    def test_has_stdfaust_import(self, dsp):
        content = (ROOT / "examples" / dsp).read_text()
        assert 'import("stdfaust.lib")' in content, f"{dsp} missing stdfaust import"

    @pytest.mark.parametrize("dsp", ["gain.dsp", "lowpass.dsp", "chorus.dsp", "compressor.dsp"])
    def test_has_process_definition(self, dsp):
        content = (ROOT / "examples" / dsp).read_text()
        assert "process" in content, f"{dsp} missing process definition"

    @pytest.mark.parametrize("dsp", ["gain.dsp", "lowpass.dsp", "chorus.dsp", "compressor.dsp"])
    def test_has_hslider_parameter(self, dsp):
        content = (ROOT / "examples" / dsp).read_text()
        assert "hslider(" in content, f"{dsp} has no hslider parameters"


class TestCmakeLists:
    def test_references_faust_engine(self):
        content = (ROOT / "host/CMakeLists.txt").read_text()
        assert "FaustEngine" in content

    def test_references_param_pool(self):
        content = (ROOT / "host/CMakeLists.txt").read_text()
        assert "ParamPool" in content

    def test_references_plugin_processor(self):
        content = (ROOT / "host/CMakeLists.txt").read_text()
        assert "PluginProcessor" in content

    def test_targets_cpp17(self):
        content = (ROOT / "host/CMakeLists.txt").read_text()
        assert "17" in content

    def test_includes_vst3(self):
        content = (ROOT / "host/CMakeLists.txt").read_text()
        assert "VST3" in content


class TestClaudeMd:
    def test_mentions_faust_engine(self):
        content = (ROOT / "CLAUDE.md").read_text()
        assert "FaustEngine" in content

    def test_mentions_param_pool(self):
        content = (ROOT / "CLAUDE.md").read_text()
        assert "ParamPool" in content

    def test_mentions_generate_py(self):
        content = (ROOT / "CLAUDE.md").read_text()
        assert "generate.py" in content

    def test_has_do_not_section(self):
        content = (ROOT / "CLAUDE.md").read_text()
        assert "Do not" in content or "do not" in content


class TestFaustEngine:
    """Verify the FaustEngine C++ implementation contract."""

    def test_header_declares_compile(self):
        content = (ROOT / "host/Source/FaustEngine.h").read_text()
        assert "compile" in content

    def test_header_declares_process(self):
        content = (ROOT / "host/Source/FaustEngine.h").read_text()
        assert "process" in content

    def test_header_declares_prepare(self):
        content = (ROOT / "host/Source/FaustEngine.h").read_text()
        assert "prepare" in content

    def test_header_declares_param_list(self):
        content = (ROOT / "host/Source/FaustEngine.h").read_text()
        assert "ParamList" in content or "ParamInfo" in content

    def test_header_has_compile_mutex(self):
        content = (ROOT / "host/Source/FaustEngine.h").read_text()
        assert "mutex" in content

    def test_cpp_calls_create_dsp_factory(self):
        content = (ROOT / "host/Source/FaustEngine.cpp").read_text()
        assert "createDSPFactoryFromString" in content

    def test_cpp_uses_memory_order_release(self):
        content = (ROOT / "host/Source/FaustEngine.cpp").read_text()
        assert "memory_order_release" in content

    def test_cpp_uses_memory_order_acquire(self):
        content = (ROOT / "host/Source/FaustEngine.cpp").read_text()
        assert "memory_order_acquire" in content


class TestParamPoolStub:
    def test_header_declares_pool_size(self):
        content = (ROOT / "host/Source/ParamPool.h").read_text()
        assert "64" in content or "POOL_SIZE" in content

    def test_header_declares_remap(self):
        content = (ROOT / "host/Source/ParamPool.h").read_text()
        assert "remap" in content

    def test_header_declares_push_to_faust(self):
        content = (ROOT / "host/Source/ParamPool.h").read_text()
        assert "pushToFaust" in content

    def test_cpp_constructs_64_params(self):
        content = (ROOT / "host/Source/ParamPool.cpp").read_text()
        assert "POOL_SIZE" in content or "64" in content
