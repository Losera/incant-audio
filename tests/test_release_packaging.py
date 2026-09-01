import json
import os
import subprocess
import tarfile
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
PACKAGE = ROOT / "tools" / "package_release.sh"
INSTALL = ROOT / "tools" / "install_release.sh"


def _fake_package(tmp_path: Path) -> Path:
    """A minimal extracted release tree the installer can run against offline."""
    package = tmp_path / "package"
    (package / "plugins/PluginForge Host.vst3").mkdir(parents=True)
    (package / "plugins/PluginForge Synth.vst3").mkdir(parents=True)
    (package / "standalone").mkdir()
    (package / "runtime/llm").mkdir(parents=True)
    (package / "standalone/PluginForge Host").write_text("host")
    (package / "standalone/PluginForge Synth").write_text("synth")
    (package / "runtime/llm/generate.py").write_text("# runtime")
    # Empty so `pip install -r` is an instant no-op with the venv's bundled pip
    # (no network); a real release ships the four real deps here.
    (package / "runtime/requirements.txt").write_text("")
    (package / "runtime/.env.example").write_text("PLUGINFORGE_PROVIDER=gemini\n")
    (package / "install.sh").write_text(INSTALL.read_text())
    return package


def _install_env(tmp_path: Path) -> dict:
    env = os.environ.copy()
    env.pop("PLUGINFORGE_PYTHON", None)
    env.update(
        {
            "PLUGINFORGE_VST3_DIR": str(tmp_path / "vst3"),
            "PLUGINFORGE_BIN_DIR": str(tmp_path / "bin"),
            "XDG_DATA_HOME": str(tmp_path / "data"),
            "XDG_CONFIG_HOME": str(tmp_path / "config"),
        }
    )
    return env


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
    package = _fake_package(tmp_path)
    env = _install_env(tmp_path)
    subprocess.run(
        ["bash", str(package / "install.sh")], env=env, text=True,
        capture_output=True, check=True,
    )

    assert (tmp_path / "vst3/PluginForge Host.vst3").is_dir()
    assert (tmp_path / "vst3/PluginForge Synth.vst3").is_dir()
    assert (tmp_path / "bin/pluginforge-host").is_file()
    assert (tmp_path / "bin/pluginforge-synth").is_file()
    assert (tmp_path / "data/pluginforge/llm/generate.py").is_file()


def test_installer_writes_a_config_the_plugin_can_read(tmp_path):
    """PF-065: the installer's job is a config.json the plugin finds with no
    PLUGINFORGE_* env var — generate_script_path + a venv interpreter."""
    package = _fake_package(tmp_path)
    env = _install_env(tmp_path)
    subprocess.run(
        ["bash", str(package / "install.sh")], env=env, text=True,
        capture_output=True, check=True,
    )

    venv_python = tmp_path / "data/pluginforge/venv/bin/python3"
    assert venv_python.is_file(), "the installer creates a dedicated venv"

    config = json.loads((tmp_path / "config/pluginforge/config.json").read_text())
    assert config["generate_script_path"] == str(tmp_path / "data/pluginforge/llm/generate.py")
    assert config["python_path"] == str(venv_python)
    assert config["schema"] == 1

    # .env is seeded from the example, not left absent
    assert (tmp_path / "data/pluginforge/.env").read_text() == "PLUGINFORGE_PROVIDER=gemini\n"


def test_installer_preserves_picker_written_config_keys(tmp_path):
    """A user who set a provider in the in-plugin picker before (re-)installing
    must not lose it — the installer merges, it does not overwrite."""
    package = _fake_package(tmp_path)
    env = _install_env(tmp_path)

    config_path = tmp_path / "config/pluginforge/config.json"
    config_path.parent.mkdir(parents=True)
    config_path.write_text(json.dumps({
        "schema": 1,
        "active_provider": "groq",
        "active_model": "openai/gpt-oss-120b",
        "soundfetch_interpreter_path": "/home/u/soundfetch/.venv/bin/python3",
        "generate_script_path": "/stale/path/generate.py",
    }) + "\n")

    subprocess.run(
        ["bash", str(package / "install.sh")], env=env, text=True,
        capture_output=True, check=True,
    )

    config = json.loads(config_path.read_text())
    assert config["active_provider"] == "groq"
    assert config["active_model"] == "openai/gpt-oss-120b"
    assert config["soundfetch_interpreter_path"] == "/home/u/soundfetch/.venv/bin/python3"
    # the paths the installer owns are refreshed
    assert config["generate_script_path"] == str(tmp_path / "data/pluginforge/llm/generate.py")
    assert config["python_path"] == str(tmp_path / "data/pluginforge/venv/bin/python3")


def test_installer_records_system_python_when_venv_unavailable(tmp_path, monkeypatch):
    """If `python3 -m venv` can't run, the installer still records a usable
    interpreter (the system python3) rather than leaving python_path unset."""
    package = _fake_package(tmp_path)
    env = _install_env(tmp_path)

    # A shim `python3` whose `-m venv --help` fails, everything else passes through.
    shim_dir = tmp_path / "shim"
    shim_dir.mkdir()
    real_python = subprocess.run(
        ["bash", "-c", "command -v python3"], text=True, capture_output=True, check=True
    ).stdout.strip()
    (shim_dir / "python3").write_text(
        f'#!/usr/bin/env bash\n'
        f'if [ "$1" = "-m" ] && [ "$2" = "venv" ]; then exit 1; fi\n'
        f'exec {real_python} "$@"\n'
    )
    (shim_dir / "python3").chmod(0o755)
    env["PATH"] = f"{shim_dir}:{env['PATH']}"

    subprocess.run(
        ["bash", str(package / "install.sh")], env=env, text=True,
        capture_output=True, check=True,
    )

    config = json.loads((tmp_path / "config/pluginforge/config.json").read_text())
    assert config["python_path"] == str(shim_dir / "python3")
    assert not (tmp_path / "data/pluginforge/venv").exists()
