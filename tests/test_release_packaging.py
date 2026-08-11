import os
import subprocess
import tarfile
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
PACKAGE = ROOT / "tools" / "package_release.sh"
INSTALL = ROOT / "tools" / "install_release.sh"


def _fake_build(tmp_path: Path) -> Path:
    build = tmp_path / "build"
    artifacts = {
        "PluginForgeHost_artefacts/Release/VST3/PluginForge Host.vst3/Contents/plugin": "host",
        "PluginForgeSynth_artefacts/Release/VST3/PluginForge Synth.vst3/Contents/plugin": "synth",
        "PluginForgeHost_artefacts/Release/Standalone/PluginForge Host": "host-bin",
        "PluginForgeSynth_artefacts/Release/Standalone/PluginForge Synth": "synth-bin",
    }
    for relative, content in artifacts.items():
        path = build / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(content)
    return build


def test_repository_does_not_grant_an_open_source_license():
    license_text = (ROOT / "LICENSE").read_text()
    readme = (ROOT / "README.md").read_text()
    assert "Copyright (c) 2026 Juan Naranjo" in license_text
    assert "All rights reserved" in license_text
    assert "No license or permission is granted" in license_text
    assert "License: GPL" not in readme
    assert "GPLv3 / MIT" not in readme


def test_package_contains_both_products_runtime_and_legal_files(tmp_path):
    build = _fake_build(tmp_path)
    output = tmp_path / "dist"
    result = subprocess.run(
        ["bash", str(PACKAGE), str(build), str(output)],
        cwd=ROOT,
        text=True,
        capture_output=True,
        check=True,
    )

    archive = Path(result.stdout.strip())
    assert archive.is_file()
    checksum = archive.with_suffix(archive.suffix + ".sha256")
    assert checksum.is_file()
    subprocess.run(
        ["sha256sum", "-c", checksum.name], cwd=output, check=True, capture_output=True
    )
    with tarfile.open(archive) as packaged:
        names = packaged.getnames()
    assert any(name.endswith("plugins/PluginForge Host.vst3/Contents/plugin") for name in names)
    assert any(name.endswith("plugins/PluginForge Synth.vst3/Contents/plugin") for name in names)
    assert any(name.endswith("standalone/PluginForge Host") for name in names)
    assert any(name.endswith("standalone/PluginForge Synth") for name in names)
    assert any(name.endswith("runtime/llm/generate.py") for name in names)
    assert any(name.endswith("LICENSE") for name in names)
    assert any(name.endswith("install.sh") for name in names)


def test_package_refuses_a_partial_release_build(tmp_path):
    result = subprocess.run(
        ["bash", str(PACKAGE), str(tmp_path / "missing"), str(tmp_path / "dist")],
        cwd=ROOT,
        text=True,
        capture_output=True,
    )
    assert result.returncode == 1
    assert "required release artifact not found" in result.stderr


def test_installer_uses_user_selected_destinations(tmp_path):
    package = tmp_path / "package"
    (package / "plugins/PluginForge Host.vst3").mkdir(parents=True)
    (package / "plugins/PluginForge Synth.vst3").mkdir(parents=True)
    (package / "standalone").mkdir()
    (package / "runtime/llm").mkdir(parents=True)
    (package / "standalone/PluginForge Host").write_text("host")
    (package / "standalone/PluginForge Synth").write_text("synth")
    (package / "runtime/llm/generate.py").write_text("# runtime")
    installer = package / "install.sh"
    installer.write_text(INSTALL.read_text())

    env = os.environ.copy()
    env.update(
        {
            "PLUGINFORGE_VST3_DIR": str(tmp_path / "vst3"),
            "PLUGINFORGE_BIN_DIR": str(tmp_path / "bin"),
            "XDG_DATA_HOME": str(tmp_path / "data"),
        }
    )
    result = subprocess.run(
        ["bash", str(installer)], env=env, text=True, capture_output=True, check=True
    )

    assert (tmp_path / "vst3/PluginForge Host.vst3").is_dir()
    assert (tmp_path / "vst3/PluginForge Synth.vst3").is_dir()
    assert (tmp_path / "bin/pluginforge-host").is_file()
    assert (tmp_path / "bin/pluginforge-synth").is_file()
    assert (tmp_path / "data/pluginforge/llm/generate.py").is_file()
    assert "PLUGINFORGE_LLM_SCRIPT" in result.stdout
