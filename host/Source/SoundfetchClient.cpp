#include "SoundfetchClient.h"
#include "PluginConfig.h"   // ADR-032 v1: soundfetch_interpreter_path

SoundfetchClient::SoundfetchClient(juce::File root) : cacheRoot(std::move(root))
{
    cacheRoot.createDirectory();
}

juce::File SoundfetchClient::defaultCacheRoot()
{
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
        .getChildFile("Incant-Audio").getChildFile("Samples");
}

juce::StringArray SoundfetchClient::commandPrefix() const
{
    // SOUNDFETCH_BIN is an escape hatch that bypasses Python resolution
    // entirely -- kept for anyone who already points it at a soundfetch
    // executable directly (e.g. a venv's bin/soundfetch on PATH).
    if (auto configured = juce::SystemStats::getEnvironmentVariable("SOUNDFETCH_BIN", {});
        configured.isNotEmpty())
        return { configured };

    // Resolution order mirrors PromptPanel's interpreter discovery for
    // llm/generate.py (ADR-011, PromptPanel.cpp:462-467): a soundfetch-specific
    // override for an installed/venv layout the system python3 can't import
    // soundfetch from, falling back to the shared PLUGINFORGE_PYTHON override,
    // then (ADR-032 v1) config.json's soundfetch_interpreter_path for a
    // launcher-started DAW that inherits no PLUGINFORGE_* at all, then the bare
    // "python3" PATH lookup. `<python> -m soundfetch` rather than a `soundfetch`
    // binary name removes the PATH-visibility requirement that made every query
    // fail on this machine (soundfetch only ever installed inside venvs, never
    // on PATH).
    auto pythonExe = juce::SystemStats::getEnvironmentVariable(
        "PLUGINFORGE_SOUNDFETCH_PYTHON",
        juce::SystemStats::getEnvironmentVariable("PLUGINFORGE_PYTHON", {}));
    if (pythonExe.isEmpty())
        pythonExe = PluginConfig::load().soundfetchInterpreterPath;
    if (pythonExe.isEmpty())
        pythonExe = "python3";
    return { pythonExe, "-m", "soundfetch" };
}

juce::var SoundfetchClient::run(const juce::StringArray& args, juce::String& error)
{
    juce::ChildProcess process;
    {
        const juce::ScopedLock lock(processLock);
        activeProcess = &process;
    }

    juce::StringArray command = commandPrefix();
    command.addArray(args);

    // wantStdOut only: soundfetch's --json payload is clean on stdout, but its
    // logging (logging.basicConfig(level=INFO), unconditional -- soundfetch's
    // own cli.py has no --quiet flag) and Internet-Archive progress lines
    // ("archive metadata: n/50") go to stderr. juce::ChildProcess::start()'s
    // default streamFlags merge both into one pipe (juce_ChildProcess.h:76),
    // which corrupted every Internet Archive search's JSON -- confirmed by
    // running the exact command this session: stdout alone parses, the merged
    // stream does not. Errors are reported structurally inside the JSON
    // payload itself ({"ok": false, "error": {...}}), so discarding stderr
    // loses no user-facing information.
    if (! process.start(command, juce::ChildProcess::wantStdOut))
    {
        // Only reachable if fork()/pipe() itself failed
        // (juce_SharedCode_posix.h's ActiveProcess ctor) -- NOT for a missing
        // executable. A missing python3 or an uninstalled soundfetch module
        // still makes fork() succeed; execvp fails in the CHILD, which the
        // parent's childPID (already captured before the child could fail)
        // has no way to see. That case surfaces after the process exits,
        // below, not here.
        error = "Soundfetch failed to launch (fork/pipe error).";
        juce::Logger::writeToLog("PluginForge: soundfetch launch failed (fork/pipe) for: "
                                  + command.joinIntoString(" "));
        const juce::ScopedLock lock(processLock);
        activeProcess = nullptr;
        return {};
    }

    if (! process.waitForProcessToFinish(kTimeoutMs))
    {
        process.kill();
        error = "Soundfetch timed out after " + juce::String(kTimeoutMs / 1000) + " seconds.";
    }
    const auto output = process.readAllProcessOutput().trim();
    const auto exitCode = process.getExitCode();
    {
        const juce::ScopedLock lock(processLock);
        activeProcess = nullptr;
    }

    // A launch/install failure produces non-zero + empty stdout. That includes
    // execvp failure (255) and Python finding the interpreter but not the
    // soundfetch module (1, with its diagnostic written only to stderr). Real
    // soundfetch failures emit a structured JSON error object on stdout.
    if (error.isEmpty() && exitCode != 0 && output.isEmpty())
    {
        // This also covers a present Python interpreter whose `-m soundfetch`
        // lookup fails: Python writes that diagnostic to stderr (deliberately
        // not captured because normal soundfetch logging would corrupt JSON),
        // exits non-zero and leaves stdout empty.
        error = "Soundfetch is unavailable or could not start: \""
              + command.joinIntoString(" ")
              + "\" returned no JSON. Install soundfetch 0.4+ so `<python> -m soundfetch` "
                "resolves, then set PLUGINFORGE_SOUNDFETCH_PYTHON (or SOUNDFETCH_BIN) "
                "to point at it.";
        juce::Logger::writeToLog("PluginForge: " + error);
        return {};
    }

    auto parsed = juce::JSON::parse(output);
    if (parsed.isVoid() || parsed.isUndefined())
    {
        if (error.isEmpty()) error = output.isNotEmpty() ? output : "Soundfetch returned no JSON.";
        juce::Logger::writeToLog("PluginForge: soundfetch produced no parseable JSON (exit "
                                  + juce::String(exitCode) + "): " + output);
        return {};
    }
    if (exitCode != 0 && error.isEmpty())
        error = parsed.getProperty("error", juce::var()).getProperty("message", output).toString();
    return parsed;
}

SoundfetchClient::SearchResponse SoundfetchClient::search(const juce::String& provider,
                                                           const juce::String& query,
                                                           int maxResults)
{
    SearchResponse response;
    const auto searchDir = cacheRoot.getChildFile("search");
    searchDir.createDirectory();
    juce::String error;
    auto value = run({ provider, "search", query, "--outdir", searchDir.getFullPathName(),
                       "--max-results", juce::String(maxResults), "--json" }, error);
    response.error = error;
    response.ok = value.getProperty("ok", false) && error.isEmpty();
    response.manifestPath = value.getProperty("manifest", {}).toString();
    if (auto* rows = value.getProperty("results", {}).getArray())
        for (const auto& row : *rows)
        {
            const auto metadata = row.getProperty("metadata", {});
            response.results.push_back({ row.getProperty("provider_id", {}).toString(),
                                         row.getProperty("name", {}).toString(),
                                         row.getProperty("provider", provider).toString(),
                                         metadata.getProperty("license", {}).toString(),
                                         static_cast<double>(metadata.getProperty("duration", 0.0)) });
        }
    return response;
}

SoundfetchClient::DownloadResponse SoundfetchClient::download(const juce::String& provider,
                                                               const juce::String& providerId,
                                                               const juce::String& manifestPath)
{
    DownloadResponse response;
    const auto downloadDir = cacheRoot.getChildFile("downloads");
    downloadDir.createDirectory();
    juce::String error;
    auto value = run({ provider, "download", "--manifest", manifestPath,
                       "--provider-id", providerId, "--outdir",
                       downloadDir.getFullPathName(), "--json" }, error);
    response.error = error;
    response.ok = value.getProperty("ok", false) && error.isEmpty();
    if (auto* items = value.getProperty("items", {}).getArray(); items != nullptr && ! items->isEmpty())
        response.localPath = items->getFirst().getProperty("local_path", {}).toString();
    if (response.ok && response.localPath.isEmpty())
    {
        response.ok = false;
        response.error = "Soundfetch completed without returning a local audio path.";
    }
    return response;
}

void SoundfetchClient::cancel()
{
    const juce::ScopedLock lock(processLock);
    if (activeProcess != nullptr)
        activeProcess->kill();
}
