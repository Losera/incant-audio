#include "SampleBrowserPanel.h"

SampleBrowserPanel::SampleBrowserPanel(std::function<void(const juce::File&)> callback,
                                       std::function<void(int)> modeCallback)
    : onSampleReady(std::move(callback)), onModeChanged(std::move(modeCallback))
{
    provider.addItem("Internet Archive", 1);
    provider.addItem("Freesound", 2);
    provider.setSelectedId(1);
    query.setTextToShowWhenEmpty("Search free sounds...", juce::Colours::grey);
    query.setReturnKeyStartsNewLine(false);
    results.setTextWhenNothingSelected("Search results");
    results.setEnabled(false);
    downloadButton.setEnabled(false);
    playbackMode.addItem("Stopped", 1);
    playbackMode.addItem("One-shot", 2);
    playbackMode.addItem("Loop", 3);
    playbackMode.setSelectedId(2);
    status.setText("Drop an audio file here, or search with Soundfetch.", juce::dontSendNotification);
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
    if (worker.joinable()) worker.join();
    searchButton.setEnabled(false);
    downloadButton.setEnabled(false);
    localButton.setEnabled(false);
    worker = std::thread(std::move(work));
}

void SampleBrowserPanel::loadSampleAsync(const juce::File& file)
{
    const int mode = playbackMode.getSelectedId() - 1;
    status.setText("Loading sample...", juce::dontSendNotification);
    startWork([this, file, mode]
    {
        onSampleReady(file); // decode and buffer swap stay off the message thread
        onModeChanged(mode);
        finishWork([this, name = file.getFileName()]
        {
            status.setText("Loaded: " + name, juce::dontSendNotification);
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
        safe->searchButton.setEnabled(true);
        safe->localButton.setEnabled(true);
    });
}

void SampleBrowserPanel::beginSearch()
{
    const auto text = query.getText().trim();
    if (text.isEmpty()) return;
    const auto providerId = provider.getSelectedId() == 2 ? juce::String("freesound")
                                                          : juce::String("archive");
    status.setText("Searching...", juce::dontSendNotification);
    startWork([this, providerId, text]
    {
        auto response = client.search(providerId, text);
        finishWork([this, response = std::move(response)]() mutable
        {
            results.clear();
            currentResults = std::move(response.results);
            manifestPath = response.manifestPath;
            if (! response.ok)
            {
                status.setText(response.error, juce::dontSendNotification);
                results.setEnabled(false);
                return;
            }
            int id = 1;
            for (const auto& item : currentResults)
                results.addItem(item.title.isNotEmpty() ? item.title : item.providerId, id++);
            results.setEnabled(! currentResults.empty());
            status.setText(juce::String(currentResults.size()) + " results. Select one to download.",
                           juce::dontSendNotification);
        });
    });
}

void SampleBrowserPanel::beginDownload()
{
    const int index = results.getSelectedId() - 1;
    if (index < 0 || index >= static_cast<int>(currentResults.size())) return;
    const auto item = currentResults[static_cast<size_t>(index)];
    const int mode = playbackMode.getSelectedId() - 1;
    status.setText("Downloading...", juce::dontSendNotification);
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
                status.setText(response.error, juce::dontSendNotification);
                downloadButton.setEnabled(true);
                return;
            }
            status.setText("Downloaded and loaded: " + juce::File(response.localPath).getFileName(),
                           juce::dontSendNotification);
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
