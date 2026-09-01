#pragma once
#include <juce_core/juce_core.h>

// ---------------------------------------------------------------------------
// PluginConfig — ADR-032 v1's small, versioned, NON-SECRET preferences file.
//
// Path: $XDG_CONFIG_HOME/pluginforge/config.json, else
//       ~/.config/pluginforge/config.json.
//
// WHY THIS EXISTS: the plugin owned none of its configuration. Which provider,
// which model, where llm/generate.py lives, and which interpreter runs
// Soundfetch were all read from whatever environment the DAW process happened
// to inherit. A DAW started from a desktop launcher inherits none of the
// PLUGINFORGE_* variables and no .env — PF-071 (a launcher-started REAPER/Carla
// falling through to a stale XDG runtime that defaults to the paid provider)
// and PF-065 (installed VST3 can't find generate.py) are the observed failures.
// This file is the one configuration source a user can actually fix by hand
// when it is wrong, unlike an upward directory walk or a silent XDG fallback.
//
// WHAT THIS IS NOT: it is not .env and it carries NO credentials. A provider
// name and two file paths are preferences, not secrets — the line ADR-032 §4.4
// draws is at storing *credentials* outside a real vault, not at a non-secret
// preferences file. The moment this struct is tempted to hold an API key, that
// is the v2 ADR, not a field here.
//
// FALLBACK CONTRACT: an absent or malformed file yields a PluginConfig whose
// every field is empty, which is exactly today's environment-only behaviour.
// Callers treat an empty field as "not configured — use the existing
// resolution" and never as an explicit empty value.
// ---------------------------------------------------------------------------
struct PluginConfig
{
    // "" => the Python side's DEFAULT_PROVIDER (providers.resolve_provider()).
    juce::String activeProvider;
    // "" => providers.resolve_model() picks the selected provider's default.
    juce::String activeModel;
    // "" => resolveGenerateScript() falls through to the parent-dir walk / XDG.
    juce::String generateScriptPath;
    // "" => SoundfetchClient uses PLUGINFORGE_SOUNDFETCH_PYTHON / PLUGINFORGE_PYTHON
    //       / "python3", exactly as before.
    juce::String soundfetchInterpreterPath;

    // The on-disk schema version this struct was loaded from (or would be
    // written as). v1 is the only version; a higher number on disk is still
    // read leniently — unknown keys are ignored, known keys are taken — so a
    // newer plugin's file does not brick an older one.
    int schema = 1;

    static constexpr int kSchemaVersion = 1;

    // Resolve the config path. Both inputs are injectable so a test can point
    // them at scratch directories rather than the real user environment.
    static juce::File configFile(
        const juce::String& xdgConfigHome
            = juce::SystemStats::getEnvironmentVariable("XDG_CONFIG_HOME", ""),
        const juce::File& homeDir
            = juce::File::getSpecialLocation(juce::File::userHomeDirectory))
    {
        const juce::File base = xdgConfigHome.isNotEmpty()
            ? juce::File(xdgConfigHome)
            : homeDir.getChildFile(".config");
        return base.getChildFile("pluginforge").getChildFile("config.json");
    }

    // Absent file, unreadable file, non-JSON text, or a JSON value that is not
    // an object all return a default-constructed PluginConfig (every field
    // empty). This is deliberate: the fallback contract above says a broken
    // file must degrade to environment-only behaviour, never to an error.
    static PluginConfig load(const juce::File& file = configFile())
    {
        PluginConfig cfg;
        if (! file.existsAsFile())
            return cfg;

        const juce::var parsed = juce::JSON::parse(file.loadFileAsString());
        if (! parsed.isObject())
            return cfg;

        // getProperty(id, "") returns a var; .toString() on an absent or
        // non-string value gives "", which is the "not configured" sentinel.
        cfg.activeProvider           = parsed.getProperty("active_provider", "").toString().trim();
        cfg.activeModel              = parsed.getProperty("active_model", "").toString().trim();
        cfg.generateScriptPath       = parsed.getProperty("generate_script_path", "").toString().trim();
        cfg.soundfetchInterpreterPath = parsed.getProperty("soundfetch_interpreter_path", "").toString().trim();
        cfg.schema = static_cast<int>(parsed.getProperty("schema", kSchemaVersion));
        return cfg;
    }

    // Write the file, creating ~/.config/pluginforge/ if needed. Only non-empty
    // fields are emitted, so a round-trip never invents an explicit "" that
    // load() would then have to special-case. Returns false on any I/O failure
    // (the caller keeps running on environment resolution — a preferences write
    // that failed is not fatal).
    //
    // "\n" line ending, matching every other write in host/Source/ — see
    // PromptPanel.cpp's replaceWithText note on juce_File.h's "\r\n" default.
    bool writeTo(const juce::File& file = configFile()) const
    {
        if (! file.getParentDirectory().createDirectory())
            return false;

        auto* obj = new juce::DynamicObject();
        obj->setProperty("schema", kSchemaVersion);
        if (activeProvider.isNotEmpty())            obj->setProperty("active_provider", activeProvider);
        if (activeModel.isNotEmpty())               obj->setProperty("active_model", activeModel);
        if (generateScriptPath.isNotEmpty())        obj->setProperty("generate_script_path", generateScriptPath);
        if (soundfetchInterpreterPath.isNotEmpty()) obj->setProperty("soundfetch_interpreter_path", soundfetchInterpreterPath);

        return file.replaceWithText(
            juce::JSON::toString(juce::var(obj), /* allOnOneLine */ false),
            false, false, "\n");
    }
};
