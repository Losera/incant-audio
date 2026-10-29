#include "SampleBrowserPanel.h"

SampleBrowserPanel::SampleBrowserPanel(std::function<void(const juce::File&)> callback,
                                       std::function<void(int)> modeCallback)
    : onSampleReady(std::move(callback)), onModeChanged(std::move(modeCallback))
{
    // Openverse is the default (id 3, not 1): it is the only source that is
    // both credential-free (soundfetch providers/openverse/provider.py:134,
    // auth_required: False) and unaffected by PF-056 (docs/BUGS.md:95, the
    // Freesound key is 403-rejected) -- a first run now succeeds with no
    // setup. Existing ids 1/2 are kept unchanged rather than renumbered:
    // beginSearch()'s provider mapping below and EditorSessionTest scenario
    // 38 both reference them.
    provider.addItem("Internet Archive", 1);
    provider.addItem("Freesound", 2);
    provider.addItem("Openverse", 3);
    provider.setSelectedId(3);
    query.setTextToShowWhenEmpty("Search free sounds...", Theme::textSecondary);
    query.setReturnKeyStartsNewLine(false);
    results.setTextWhenNothingSelected("Search results");
    results.setEnabled(false);
    downloadButton.setEnabled(false);
    playbackMode.addItem("Stopped", 1);
    playbackMode.addItem("One-shot", 2);
    playbackMode.addItem("Loop", 3);
    playbackMode.setSelectedId(2);
    setStatusText("Drop an audio file here, or search with Soundfetch.");
    status.setJustificationType(juce::Justification::centredLeft);

    for (auto* component : std::initializer_list<juce::Component*>{ &provider, &query,
             &searchButton, &results, &downloadButton, &localButton, &playbackMode, &status })
        addAndMakeVisible(component);

    searchButton.onClick = [this] { beginSearch(); };
    query.onReturnKey = [this] { beginSearch(); };
    downloadButton.onClick = [this] { beginDownload(); };
    localButton.onClick = [this] { chooseLocalFile(); };
    results.onChange = [this] { downloadButton.setEnabled(results.getSelectedId() > 0); };
    playbackMode.onChange = [this] { onModeChanged(playbackMode.getSelectedId() - 1); };
}

SampleBrowserPanel::~SampleBrowserPanel()
{
    closing.store(true);
    client.cancel();
    if (worker.joinable()) worker.join();
}

void SampleBrowserPanel::resized()
{
    // Row heights tightened 26->20 and a gap added before status (2026-08-13):
    // the original 26+5+26 left only 7px of PluginEditor.h's 64px samplesH
    // band for status -- juce::Label doesn't clip its own bounds, so "Drop an
    // audio file here, or search with Soundfetch." rendered overflowing down
    // into the keyboard band below it, found via EditorSessionTest's own
    // rendered snapshots (session_23_kind_selector_reaches_request.png) during
    // the 2026-08-13 merge with feat/ui-design-system. 20+2+20+2+20 = 64
    // exactly fits the existing band -- no Chrome/samplesH change needed --
    // and 20px matches PromptPanel's own statusH for a single status line.
    auto area = getLocalBounds();
    auto top = area.removeFromTop(20);
    provider.setBounds(top.removeFromLeft(135));
    top.removeFromLeft(6);
    searchButton.setBounds(top.removeFromRight(72));
    top.removeFromRight(6);
    query.setBounds(top);
    area.removeFromTop(2);
    auto middle = area.removeFromTop(20);
    playbackMode.setBounds(middle.removeFromRight(90));
    middle.removeFromRight(6);
    localButton.setBounds(middle.removeFromRight(100));
    middle.removeFromRight(6);
    downloadButton.setBounds(middle.removeFromRight(145));
    middle.removeFromRight(6);
    results.setBounds(middle);
    area.removeFromTop(2);
    status.setBounds(area);
}

void SampleBrowserPanel::setStatusText(const juce::String& text)
{
    status.setText(text, juce::dontSendNotification);
    status.setTooltip(text);
}

bool SampleBrowserPanel::isSupportedAudioFile(const juce::File& file) const
{
    return file.hasFileExtension("wav;wave;aif;aiff;flac;ogg;mp3");
}

bool SampleBrowserPanel::isInterestedInFileDrag(const juce::StringArray& files)
{
    return searchButton.isEnabled() && files.size() == 1
        && isSupportedAudioFile(juce::File(files[0]));
}

void SampleBrowserPanel::filesDropped(const juce::StringArray& files, int, int)
{
    if (isInterestedInFileDrag(files))
        loadSampleAsync(juce::File(files[0]));
}

void SampleBrowserPanel::startWork(std::function<void()> work)
{
    // Re-entrancy guard, not just a courtesy: query.onReturnKey calls
    // beginSearch() unconditionally (SampleBrowserPanel.cpp's ctor), bypassing
    // searchButton's enabled state the way a click on the disabled button
    // would be. Pressing Enter twice before a search returns would otherwise
    // reach worker.join() below while the prior worker is still blocked in a
    // network call that can now legitimately run close to kTimeoutMs (60s,
    // SoundfetchClient.h) -- freezing this whole editor's message thread for
    // however long remains. `working` is only ever touched from the message
    // thread (set here; cleared in finishWork's callAsync, also message-
    // thread-only), so the check needs no lock.
    if (working.load())
        return;
    working.store(true);

    if (worker.joinable()) worker.join();
    searchButton.setEnabled(false);
    downloadButton.setEnabled(false);
    localButton.setEnabled(false);
    worker = std::thread(std::move(work));
}

void SampleBrowserPanel::loadSampleAsync(const juce::File& file)
{
    const int mode = playbackMode.getSelectedId() - 1;
    setStatusText("Loading sample...");
    startWork([this, file, mode]
    {
        onSampleReady(file); // decode and buffer swap stay off the message thread
        onModeChanged(mode);
        finishWork([this, name = file.getFileName()]
        {
            setStatusText("Loaded: " + name);
        });
    });
}

void SampleBrowserPanel::finishWork(std::function<void()> action)
{
    if (closing.load()) return;
    juce::Component::SafePointer<SampleBrowserPanel> safe(this);
    juce::MessageManager::callAsync([safe, action = std::move(action)]
    {
        if (safe == nullptr) return;
        action();
        safe->working.store(false);
        safe->searchButton.setEnabled(true);
        safe->localButton.setEnabled(true);
    });
}

void SampleBrowserPanel::beginSearch()
{
    const auto text = query.getText().trim();
    if (text.isEmpty()) return;
    const int selectedId = provider.getSelectedId();
    const auto providerId = selectedId == 2 ? juce::String("freesound")
                           : selectedId == 3 ? juce::String("openverse")
                                             : juce::String("archive");
    setStatusText("Searching...");
    startWork([this, providerId, text]
    {
        // PF-056 preflight (docs/BUGS.md:95): a missing Freesound key and a
        // 403-rejected one both used to surface only as "no JSON" -- neither
        // named the provider nor said why. This distinguishes them before
        // spending a network round trip on a search that cannot succeed.
        // archive/openverse always report credentialsConfigured == true
        // (SoundfetchClient::status), so this adds no round trip for them.
        if (providerId == "freesound")
        {
            auto probe = client.status(providerId);
            if (probe.ok && ! probe.credentialsConfigured)
            {
                finishWork([this]
                {
                    setStatusText("Freesound needs an API key. Set FREESOUND_API_KEY in "
                                  "the environment PluginForge was launched from, or use "
                                  "Openverse / Internet Archive instead.");
                    results.setEnabled(false);
                });
                return;
            }
        }
        auto response = client.search(providerId, text);
        finishWork([this, providerId, response = std::move(response)]() mutable
        {
            results.clear();
            currentResults = std::move(response.results);
            manifestPath = response.manifestPath;
            if (! response.ok)
            {
                // PF-056's remaining half: a configured-but-rejected Freesound
                // key surfaces from SoundfetchClient only as {"error":
                // {"message": "HTTP 403 <detail> (<url>)"}} (soundfetch
                // net.py:17,46-78 -- 403 is not in RETRYABLE_STATUS, so it
                // raises HttpError immediately) with no provider name
                // attached. Name it here instead of showing the raw message
                // as if it were self-explanatory -- but only for an actual
                // 403; a timeout or a malformed-JSON error is not a key
                // problem and must not be relabelled as one.
                setStatusText(providerId == "freesound" && response.error.contains("403")
                    ? "Freesound rejected the configured API key (" + response.error
                      + "). The key is set but not accepted -- replace it at freesound.org."
                    : response.error);
                results.setEnabled(false);
                return;
            }
            int id = 1;
            for (const auto& item : currentResults)
                results.addItem(item.title.isNotEmpty() ? item.title : item.providerId, id++);
            results.setEnabled(! currentResults.empty());
            setStatusText(juce::String(currentResults.size()) + " results. Select one to download.");
        });
    });
}

void SampleBrowserPanel::beginDownload()
{
    const int index = results.getSelectedId() - 1;
    if (index < 0 || index >= static_cast<int>(currentResults.size())) return;
    const auto item = currentResults[static_cast<size_t>(index)];
    const int mode = playbackMode.getSelectedId() - 1;
    setStatusText("Downloading...");
    startWork([this, item, mode]
    {
        auto response = client.download(item.provider, item.providerId, manifestPath);
        if (response.ok)
        {
            onSampleReady(juce::File(response.localPath));
            onModeChanged(mode);
        }
        finishWork([this, response]
        {
            if (! response.ok)
            {
                setStatusText(response.error);
                downloadButton.setEnabled(true);
                return;
            }
            setStatusText("Downloaded and loaded: " + juce::File(response.localPath).getFileName());
            downloadButton.setEnabled(true);
        });
    });
}

void SampleBrowserPanel::chooseLocalFile()
{
    chooser = std::make_unique<juce::FileChooser>("Choose an audition sample", juce::File {},
                                                   "*.wav;*.wave;*.aif;*.aiff;*.flac;*.ogg;*.mp3");
    chooser->launchAsync(juce::FileBrowserComponent::openMode
                         | juce::FileBrowserComponent::canSelectFiles,
                         [safe = juce::Component::SafePointer<SampleBrowserPanel>(this)]
    (const juce::FileChooser& fc)
    {
        if (safe == nullptr) return;
        auto file = fc.getResult();
        if (file.existsAsFile())
            safe->loadSampleAsync(file);
    });
}
