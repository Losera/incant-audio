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
  `$HOME/.local/share/pluginforge`

The installer prints the `PLUGINFORGE_LLM_SCRIPT` value that must be present in
the environment used to launch the DAW. Python dependencies are intentionally
not installed automatically; create an environment and install
`runtime/requirements.txt` explicitly.

This is an artifact installer, not host validation. A release still needs a
pluginval scan and a real-DAW smoke test before it can be called verified.
