#pragma once

#include <JuceHeader.h>

//==============================================================================
struct PlayChannel
{
    std::unique_ptr<juce::AudioFormatReaderSource> readerSource;
    juce::AudioTransportSource                     transportSource;
    std::atomic<float>                             pan{ 0.0f };
    juce::Reverb                                   reverb;

    // Reverb tail state:
    //   false = file is still playing normally
    //   true  = file finished, counting down tailSamplesRemaining
    bool tailMode{ false };
    int  tailSamplesRemaining{ 0 };
    std::atomic<bool> tailFinished{ false };  // set by audio thread when tail is done

    PlayChannel() = default;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PlayChannel)
};

//==============================================================================
class MainComponent : public juce::AudioAppComponent,
    public juce::ChangeListener,
    private juce::Timer
{
public:
    // Maximum number of simultaneously active channels
    static constexpr int MAX_CHANNELS = 24;

    //==========================================================================
    MainComponent();
    ~MainComponent() override;

    //==========================================================================
    void prepareToPlay(int samplesPerBlockExpected, double sampleRate) override;
    void getNextAudioBlock(const juce::AudioSourceChannelInfo& bufferToFill) override;
    void releaseResources() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

    void changeListenerCallback(juce::ChangeBroadcaster* source) override;
    void timerCallback() override;

private:
    //==========================================================================
    enum class TransportState { Stopped, Playing, Stopping };

    void changeState(TransportState newState);
    void selectFolder();
    void loadPlaylistFromFolder(const juce::File& folder);
    void play();
    void playNext();
    void playButtonClicked();
    void randomisePan(PlayChannel* ch);
    void randomiseReverb(PlayChannel* ch);

    void updateEQ();

    // Create a new PlayChannel, prepare it, and add it to the active list.
    // Returns nullptr if MAX_CHANNELS is already reached.
    PlayChannel* createChannel();

    // Remove all channels that have finished playing (called on message thread).
    void removeFinishedChannels();

    //==========================================================================
    // --- UI ---
    juce::TextButton openButton;
    juce::TextButton playButton;
    juce::Label      currentPositionLabel;

    juce::Slider       timerIntervalSlider;
    juce::Label        timerIntervalLabel;

    juce::ToggleButton randomOrderButton;
    bool               useRandomOrder{ false };

    juce::ComboBox  panModeComboBox;   // Left / Centre / Right / Random
    juce::Label     panModeLabel;

    juce::Slider       masterVolumeSlider;
    juce::Label        masterVolumeLabel;

    juce::Label   sectionPlaybackTitle;
    juce::Label   sectionReverbTitle;
    juce::Label   sectionEqTitle;

    juce::Slider  eqLowSlider;
    juce::Slider  eqMidSlider;
    juce::Slider  eqHighSlider;
    juce::Label   eqLowLabel;
    juce::Label   eqMidLabel;
    juce::Label   eqHighLabel;

    juce::Slider  reverbRoomSizeMinSlider;
    juce::Slider  reverbRoomSizeMaxSlider;
    juce::Slider  reverbWetMinSlider;
    juce::Slider  reverbWetMaxSlider;
    juce::Label   reverbRoomSizeLabel;
    juce::Label   reverbWetLabel;
    juce::Label   reverbRoomSizeMinLabel;
    juce::Label   reverbRoomSizeMaxLabel;
    juce::Label   reverbWetMinLabel;
    juce::Label   reverbWetMaxLabel;

    juce::Slider  reverbProbabilitySlider;
    juce::Label   reverbProbabilityLabel;

    std::unique_ptr<juce::FileChooser> chooser;

    //==========================================================================
    // --- Audio pipeline ---
    juce::AudioFormatManager formatManager;
    juce::MixerAudioSource   mixer;

    // Active channels — created on play(), destroyed when transport finishes
    juce::OwnedArray<PlayChannel> channels;
    juce::CriticalSection         channelsLock;

    double currentSampleRate{ 44100.0 };
    int    currentBlockSize{ 512 };

    //==========================================================================
    // --- Playback state ---
    TransportState state{ TransportState::Stopped };

    juce::File              currentFolder;
    juce::Array<juce::File> playlist;
    int                     currentIndex{ -1 };

    juce::Random random;

    std::atomic<float> masterVolume{ 0.5f };

    //==========================================================================
    // --- 3-band EQ ---
    using Filter = juce::dsp::IIR::Filter<float>;
    using FilterCoefs = juce::dsp::IIR::Coefficients<float>;
    using EqChain = juce::dsp::ProcessorChain<Filter, Filter, Filter>;

    EqChain eqLeft;
    EqChain eqRight;

    // Reverb is per-channel inside PlayChannel

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainComponent)
};