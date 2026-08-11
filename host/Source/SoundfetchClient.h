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

    explicit SoundfetchClient(juce::File cacheRoot = defaultCacheRoot());
    SearchResponse search(const juce::String& provider, const juce::String& query,
                          int maxResults = 20);
    DownloadResponse download(const juce::String& provider,
                              const juce::String& providerId,
                              const juce::String& manifestPath);
    void cancel();

    static juce::File defaultCacheRoot();

private:
    juce::var run(const juce::StringArray& arguments, juce::String& error);
    juce::String executable() const;

    juce::File cacheRoot;
    juce::CriticalSection processLock;
    juce::ChildProcess* activeProcess = nullptr;
};
