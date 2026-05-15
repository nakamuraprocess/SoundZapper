#include "MainComponent.h"

//==============================================================================
MainComponent::MainComponent()
    : state(TransportState::Stopped)
{
    // Register common audio formats such as MP3
    formatManager.registerBasicFormats();
    transportSource.addChangeListener(this);

    // [Open Folder] button
    addAndMakeVisible(&openButton);
    openButton.setButtonText("Open Folder...");
    openButton.onClick = [this] { selectFolder(); };

    // [Play/Stop] button
    addAndMakeVisible(&playButton);
    playButton.setButtonText("Play");
    playButton.onClick = [this] { playButtonClicked(); };
    playButton.setColour(juce::TextButton::buttonColourId, juce::Colours::green);
    playButton.setEnabled(false);

    // Label showing the current file name
    addAndMakeVisible(&currentPositionLabel);
    currentPositionLabel.setText("", juce::dontSendNotification);

    // Initialize audio device (stereo output, no input)
    setAudioChannels(0, 2);

    setSize(400, 200);
}

MainComponent::~MainComponent()
{
    stopTimer();
    shutdownAudio();
}

//==============================================================================
// Timer callback:
//   Called when the transport stops naturally (track finished) to load and start the next file.
void MainComponent::timerCallback()
{
    //stopTimer();

    // Advance to the next file
    playNextFile();

    if (currentIndex >= 0 && currentIndex < playlist.size())
    {
        // Update the label
        currentPositionLabel.setText(
            juce::String(currentIndex + 1) + " / " + juce::String(playlist.size()),
            juce::dontSendNotification);

        // Rewind to the beginning and start playback
        transportSource.setPosition(0.0);
        changeState(TransportState::Starting);
    }
}

//==============================================================================
void MainComponent::prepareToPlay(int samplesPerBlockExpected, double sampleRate)
{
    transportSource.prepareToPlay(samplesPerBlockExpected, sampleRate);
}

void MainComponent::getNextAudioBlock(const juce::AudioSourceChannelInfo& bufferToFill)
{
    if (readerSource.get() == nullptr)
    {
        bufferToFill.clearActiveBufferRegion();
        return;
    }
    transportSource.getNextAudioBlock(bufferToFill);
}

void MainComponent::releaseResources()
{
    transportSource.releaseResources();
}

//==============================================================================
void MainComponent::paint(juce::Graphics& g)
{
    g.fillAll(getLookAndFeel().findColour(juce::ResizableWindow::backgroundColourId));
}

void MainComponent::resized()
{
    openButton.setBounds(10, 10, getWidth() - 20, 30);
    playButton.setBounds(10, 50, getWidth() - 20, 30);
    currentPositionLabel.setBounds(10, 100, getWidth() - 20, 25);
}

//==============================================================================
void MainComponent::changeListenerCallback(juce::ChangeBroadcaster* source)
{
    if (source == &transportSource)
    {
        if (transportSource.isPlaying())
        {
            changeState(TransportState::Playing);
        }
        else
        {
            if (state == TransportState::Playing)
            {
                // Track finished naturally -> move to Stopped, then let the Timer advance to the next track
                changeState(TransportState::Stopped);
                startTimer(250); // timerCallback fires after 200 ms to play the next track
            }
            else if (state == TransportState::Stopping)
            {
                // User pressed Stop -> just halt, do not start the timer
                changeState(TransportState::Stopped);
            }
        }
    }
}

//==============================================================================
void MainComponent::changeState(TransportState newState)
{
    if (state != newState)
    {
        state = newState;

        switch (state)
        {
        case TransportState::Stopped:
            playButton.setButtonText("Play");
            playButton.setEnabled(true);
            break;

        case TransportState::Starting:
            transportSource.start();
            break;

        case TransportState::Playing:
            playButton.setButtonText("Stop");
            break;

        case TransportState::Stopping:
            transportSource.stop();
            break;
        }
    }
}

//==============================================================================
void MainComponent::selectFolder()
{
    chooser = std::make_unique<juce::FileChooser>(
        "Select a folder containing MP3 files...",
        juce::File::getSpecialLocation(juce::File::userDesktopDirectory),
        ""
    );

    auto chooserFlags = juce::FileBrowserComponent::openMode
        | juce::FileBrowserComponent::canSelectDirectories;

    chooser->launchAsync(chooserFlags, [this](const juce::FileChooser& fc)
        {
            auto folder = fc.getResult();

            if (folder != juce::File{} && folder.isDirectory())
            {
                currentFolder = folder;
                loadPlaylistFromFolder(currentFolder);

                if (!playlist.isEmpty())
                {
                    currentIndex = 0;
                    playCurrentFile(); // Load the first file (playback starts when Play is pressed)
                    currentPositionLabel.setText(
                        "1 / " + juce::String(playlist.size()),
                        juce::dontSendNotification);
                }
            }
        });
}

void MainComponent::loadPlaylistFromFolder(const juce::File& folder)
{
    playlist.clear();

    for (auto& file : folder.findChildFiles(juce::File::findFiles, false, "*.mp3"))
        playlist.add(file);

    // Sort files numerically by filename (ascending)
    std::sort(playlist.begin(), playlist.end(),
        [](const juce::File& a, const juce::File& b)
        {
            return a.getFileNameWithoutExtension().getIntValue()
                < b.getFileNameWithoutExtension().getIntValue();
        });

    for (const auto& file : playlist)
        DBG(file.getFileName());

    if (playlist.isEmpty())
        currentIndex = -1;
    else
        playButton.setEnabled(true);
}

void MainComponent::playCurrentFile()
{
    if (currentIndex < 0 || currentIndex >= playlist.size())
        return;

    auto file = playlist[currentIndex];
    auto* reader = formatManager.createReaderFor(file);

    if (reader != nullptr)
    {
        auto newSource = std::make_unique<juce::AudioFormatReaderSource>(reader, true);
        transportSource.setSource(newSource.get(), 0, nullptr, reader->sampleRate);
        readerSource = std::move(newSource);
    }
}

void MainComponent::playNextFile()
{
    if (playlist.isEmpty())
        return;

    int nextIndex = currentIndex + 1;
    if (nextIndex >= playlist.size())
        nextIndex = 0; // Wrap around to the first track

    currentIndex = nextIndex;
    playCurrentFile();
}

void MainComponent::playButtonClicked()
{
    if (state == TransportState::Stopped)
    {
        changeState(TransportState::Starting);
    }
    else
    {
        stopTimer(); // Also cancel any pending auto-advance timer
        changeState(TransportState::Stopping);
    }
}