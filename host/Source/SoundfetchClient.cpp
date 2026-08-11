#include "SoundfetchClient.h"

SoundfetchClient::SoundfetchClient(juce::File root) : cacheRoot(std::move(root))
{
    cacheRoot.createDirectory();
}

juce::File SoundfetchClient::defaultCacheRoot()
{
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
        .getChildFile("Incant-Audio").getChildFile("Samples");
}

juce::String SoundfetchClient::executable() const
{
    if (auto configured = juce::SystemStats::getEnvironmentVariable("SOUNDFETCH_BIN", {});
        configured.isNotEmpty())
        return configured;
    return "soundfetch";
}

juce::var SoundfetchClient::run(const juce::StringArray& args, juce::String& error)
{
    juce::ChildProcess process;
    {
        const juce::ScopedLock lock(processLock);
        activeProcess = &process;
    }

    juce::StringArray command { executable() };
    command.addArray(args);
    if (! process.start(command))
    {
        error = "Soundfetch is unavailable. Install soundfetch 0.4+ or set SOUNDFETCH_BIN.";
        const juce::ScopedLock lock(processLock);
        activeProcess = nullptr;
        return {};
    }

    if (! process.waitForProcessToFinish(60000))
    {
        process.kill();
        error = "Soundfetch timed out after 60 seconds.";
    }
    const auto output = process.readAllProcessOutput().trim();
    const auto exitCode = process.getExitCode();
    {
        const juce::ScopedLock lock(processLock);
        activeProcess = nullptr;
    }
    auto parsed = juce::JSON::parse(output);
    if (parsed.isVoid() || parsed.isUndefined())
    {
        if (error.isEmpty()) error = output.isNotEmpty() ? output : "Soundfetch returned no JSON.";
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
