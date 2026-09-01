# Distribution

PluginForge currently packages Linux x86-64 VST3 and Standalone builds. AU,
Windows, signing, notarization, and a system-wide installer are not implemented.
PluginForge itself is proprietary. Creating an archive does not establish that
its third-party dependencies are cleared for public or commercial distribution;
review the JUCE, Faust, LLVM, and other applicable terms before publishing one.

## Build and package

Configure and build the four Release targets described in the root README, then run:

```bash
tools/package_release.sh
```

The script refuses to package a partial build. It writes a versioned `.tar.gz`
and matching SHA-256 file under `dist/`. The archive contains both VST3 bundles,
both Standalone executables, the Python generation runtime, requirements, and
license/readme files.

Verify the archive from its output directory:

```bash
cd dist
sha256sum -c pluginforge-*.tar.gz.sha256
```

## Local install

Extract the archive and run its installer:

```bash
tar -xzf pluginforge-*.tar.gz
cd pluginforge-*-linux-x86_64
./install.sh
```

The default user-local destinations are:

- VST3: `$HOME/.vst3`
- Standalone launchers: `$HOME/.local/bin`
- Python generation runtime: `$XDG_DATA_HOME/pluginforge` or
  `$HOME/.local/share/pluginforge`, with a dedicated venv under `.../venv`
- Config: `$XDG_CONFIG_HOME/pluginforge/config.json` or
  `$HOME/.config/pluginforge/config.json`

The installer creates a venv, installs `runtime/requirements.txt` into it, seeds
`.env` from `.env.example` if absent, and writes `config.json` with
`generate_script_path` and `python_path` pointing at what it just installed. It
**merges** into an existing `config.json` — a provider or model set from the
in-plugin picker survives a reinstall. No `PLUGINFORGE_*` environment variable is
needed afterward, including for a DAW started from a desktop launcher (PF-065 /
PF-071). If `python3 -m venv` is unavailable it records the system `python3` and
prints the `pip install` command to finish by hand.

This is an artifact installer, not host validation. A release still needs a
pluginval scan and a real-DAW smoke test before it can be called verified.
