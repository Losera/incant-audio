#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "SoundfetchClient.h"

class SampleBrowserPanel : public juce::Component,
                           public juce::FileDragAndDropTarget
{
public:
    explicit SampleBrowserPanel(std::function<void(const juce::File&)> onSampleReady,
                                std::function<void(int)> onModeChanged);
    ~SampleBrowserPanel() override;

    void resized() override;
    bool isInterestedInFileDrag(const juce::StringArray& files) override;
    void filesDropped(const juce::StringArray& files, int, int) override;

private:
    void beginSearch();
    void beginDownload();
    void chooseLocalFile();
    void loadSampleAsync(const juce::File&);
    void startWork(std::function<void()> work);
    void finishWork(std::function<void()> messageThreadWork);
    bool isSupportedAudioFile(const juce::File&) const;

    std::function<void(const juce::File&)> onSampleReady;
    std::function<void(int)> onModeChanged;
    SoundfetchClient client;
    std::thread worker;
    std::atomic<bool> closing { false };
    std::atomic<bool> working { false };

    juce::ComboBox provider;
    juce::TextEditor query;
    juce::TextButton searchButton { "Search" };
    juce::ComboBox results;
    juce::TextButton downloadButton { "Download + audition" };
    juce::TextButton localButton { "Local file..." };
    juce::ComboBox playbackMode;
    juce::Label status;
    std::unique_ptr<juce::FileChooser> chooser;

    juce::String manifestPath;
    std::vector<SoundfetchClient::Result> currentResults;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SampleBrowserPanel)
};
