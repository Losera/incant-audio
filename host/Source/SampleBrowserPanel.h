#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "SoundfetchClient.h"
#include "Theme.h"

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

    // Test-only: proves setStatusText's actual contract below (text and
    // tooltip always carry the same string), not just that it compiles.
    // getTooltip() is NOT const in JUCE's own SettableTooltipClient
    // (juce_TooltipClient.h:80 -- `String getTooltip() override` on a
    // non-const member), so this accessor can't be const either.
    juce::String statusTextForTest() const { return status.getText(); }
    juce::String statusTooltipForTest()    { return status.getTooltip(); }

private:
    void beginSearch();
    void beginDownload();
    void chooseLocalFile();
    void loadSampleAsync(const juce::File&);
    void startWork(std::function<void()> work);
    void finishWork(std::function<void()> messageThreadWork);
    bool isSupportedAudioFile(const juce::File&) const;

    // T5: the soundfetch-missing message alone runs to ~230 characters
    // ("Soundfetch is unavailable or could not start: ..." plus the repair
    // instructions), routed into `status`, a single-line 20px juce::Label --
    // the whole samplesH band is fixed at 64px total across three rows
    // (PluginEditor.h's Chrome), so there is no room to make this label
    // multi-line without a layout change bigger than this fix. JUCE's default
    // LookAndFeel already ellipsizes an overflowing single-line Label rather
    // than clipping it silently -- what was actually missing is any way to
    // read the rest. Every status update goes through this instead of a bare
    // status.setText() so the untruncated string is always ALSO the tooltip
    // (rendered by the TooltipWindow PluginEditor now owns).
    void setStatusText(const juce::String& text);

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
