#pragma once

#include <JuceHeader.h>

//==============================================================================
// Holds one independent transport pipeline with its own pan value.
struct PlayChannel
{
    std::unique_ptr<juce::AudioFormatReaderSource> readerSource;
    juce::AudioTransportSource                     transportSource;

    // Pan: -1.0 (full left) .. 0.0 (centre) .. +1.0 (full right)
    // std::atomic so getNextAudioBlock() can read safely from the audio thread.
    std::atomic<float> pan{ 0.0f };

    PlayChannel() = default;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PlayChannel)
};

//==============================================================================
class MainComponent : public juce::AudioAppComponent,
    public juce::ChangeListener,
    private juce::Timer
{
public:
    // Number of simultaneous transport channels
    static constexpr int NUM_CHANNELS = 24;

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
    // ChangeListener override — receives state changes from all transport sources
    void changeListenerCallback(juce::ChangeBroadcaster* source) override;

    // Timer override — advances to the next file after a track finishes
    void timerCallback() override;

private:
    //==========================================================================
    enum class TransportState { Stopped, Starting, Playing, Stopping };

    void changeState(TransportState newState);
    void selectFolder();
    void loadPlaylistFromFolder(const juce::File& folder);
    void play();       // Load currentIndex into the next round-robin channel and start
    void playNext();   // Advance currentIndex then call play()
    void playButtonClicked();
    void randomisePan(int ch);   // Assign a random pan to one channel

    //==========================================================================
    // --- UI ---
    juce::TextButton openButton;
    juce::TextButton playButton;
    juce::Label      currentPositionLabel;  // "track N / total"

    juce::Slider        timerIntervalSlider;    // Controls the delay between tracks (ms)
    juce::Label         timerIntervalLabel;     // Shows the current interval value

    juce::ToggleButton  randomOrderButton;      // Switch between sequential and random order
    bool                useRandomOrder{ false };

    juce::Slider        masterVolumeSlider;     // Master volume (0.0 to 1.0)
    juce::Label         masterVolumeLabel;      // "Volume:" label

    std::unique_ptr<juce::FileChooser> chooser;

    // --- Audio pipeline ---
    juce::AudioFormatManager    formatManager;
    juce::MixerAudioSource      mixer;
    juce::OwnedArray<PlayChannel> channels; // NUM_CHANNELS entries

    // Round-robin counter: which channel receives the next play() call
    int nextChannel{ 0 };

    // --- Playback state (single playlist, single Play/Stop button) ---
    TransportState state{ TransportState::Stopped };

    // --- Playlist ---
    juce::File              currentFolder;
    juce::Array<juce::File> playlist;
    int                     currentIndex{ -1 };

    juce::Random random;

    // Master volume applied in getNextAudioBlock (atomic for thread safety)
    std::atomic<float> masterVolume{ 1.0f };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainComponent)
};