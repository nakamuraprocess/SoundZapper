#pragma once

#include <JuceHeader.h>

//==============================================================================
class MainComponent : public juce::AudioAppComponent,
    public juce::ChangeListener,
    private juce::Timer
{
public:
    //==========================================================================
    MainComponent();
    ~MainComponent() override;

    //==========================================================================
    // AudioAppComponent overrides
    void prepareToPlay(int samplesPerBlockExpected, double sampleRate) override;
    void getNextAudioBlock(const juce::AudioSourceChannelInfo& bufferToFill) override;
    void releaseResources() override;

    //==========================================================================
    // Component overrides
    void paint(juce::Graphics& g) override;
    void resized() override;

    //==========================================================================
    // ChangeListener override
    void changeListenerCallback(juce::ChangeBroadcaster* source) override;

    // Timer override
    void timerCallback() override;

private:
    //==========================================================================
    enum class TransportState { Stopped, Starting, Playing, Stopping };

    void changeState(TransportState newState);
    void selectFolder();
    void loadPlaylistFromFolder(const juce::File& folder);
    void play();
    void playNext();
    void playButtonClicked();

    //==========================================================================
    juce::TextButton openButton;
    juce::TextButton playButton;
    juce::Label      currentPositionLabel;

    std::unique_ptr<juce::FileChooser> chooser;

    juce::AudioFormatManager                       formatManager;
    std::unique_ptr<juce::AudioFormatReaderSource> readerSource;
    juce::AudioTransportSource                     transportSource;
    TransportState                                 state{ TransportState::Stopped };

    juce::File              currentFolder;
    juce::Array<juce::File> playlist;
    int                     currentIndex{ -1 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainComponent)
};