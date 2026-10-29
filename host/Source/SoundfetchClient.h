#pragma once

#include <juce_core/juce_core.h>

class SoundfetchClient
{
public:
    struct Result
    {
        juce::String providerId, title, provider, license;
        double durationSeconds = 0.0;
    };

    struct SearchResponse
    {
        bool ok = false;
        juce::String error, manifestPath;
        std::vector<Result> results;
    };

    struct DownloadResponse
    {
        bool ok = false;
        juce::String error, localPath;
    };

    // PF-056 legibility preflight: `<provider> status --json` is a non-network
    // credential doctor (soundfetch cli.py's status_hint/status branch) --
    // it reports whether a provider's credentials are configured without ever
    // printing the credential itself. credentialsConfigured is true both when
    // a provider needs none (archive, openverse -- no "status" key at all in
    // their payload) and when one is present and set; it is false only when a
    // provider that needs a key reports it missing. This cannot see whether a
    // *configured* key is valid -- that only shows up as a search/download 403.
    struct ProviderStatus
    {
        bool ok = false;
        bool credentialsConfigured = true;
        juce::String hint, error;
    };

    explicit SoundfetchClient(juce::File cacheRoot = defaultCacheRoot());
    SearchResponse search(const juce::String& provider, const juce::String& query,
                          int maxResults = kDefaultMaxResults);
    DownloadResponse download(const juce::String& provider,
                              const juce::String& providerId,
                              const juce::String& manifestPath);
    ProviderStatus status(const juce::String& provider);
    void cancel();

    static juce::File defaultCacheRoot();

    // Test-only readback of the resolved argv prefix (SOUNDFETCH_BIN escape
    // hatch, or PLUGINFORGE_SOUNDFETCH_PYTHON/PLUGINFORGE_PYTHON/python3 "-m
    // soundfetch"), without spawning anything. Public per this project's
    // existing ...ForTest() convention (e.g. KeyboardPanel's
    // currentOctaveForTest()).
    juce::StringArray commandPrefixForTest() const { return commandPrefix(); }

    // Lowered from 20 (2026-08-13): a 20-result Internet Archive search was
    // measured at 40.9s wall-clock against kTimeoutMs's 60s budget (each result
    // needs a per-item metadata lookup, cli.py:385-388), and its ~48.5KB of
    // combined stdout+stderr sat close enough to Linux's 64KB default pipe
    // capacity that a slightly larger response could deadlock the child on
    // write() -- waitForProcessToFinish only polls isRunning() and never
    // drains (juce_ChildProcess.cpp). 10 halves both risks without a rewrite
    // to concurrent draining.
    static constexpr int kDefaultMaxResults = 10;

private:
    juce::var run(const juce::StringArray& arguments, juce::String& error);
    juce::StringArray commandPrefix() const;

    juce::File cacheRoot;
    juce::CriticalSection processLock;
    juce::ChildProcess* activeProcess = nullptr;

    static constexpr int kTimeoutMs = 60000;
};
