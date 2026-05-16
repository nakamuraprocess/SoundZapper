#include "MainComponent.h"

//==============================================================================
MainComponent::MainComponent()
    : state(TransportState::Stopped)
{
    // Register common audio formats such as MP3
    formatManager.registerBasicFormats();

    // Create NUM_CHANNELS transport channels and register change listeners
    for (int i = 0; i < NUM_CHANNELS; ++i)
    {
        auto* ch = channels.add(new PlayChannel());
        ch->transportSource.addChangeListener(this);
    }

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

    // Label showing track progress
    addAndMakeVisible(&currentPositionLabel);
    currentPositionLabel.setText("", juce::dontSendNotification);


    // Slider controlling the delay between tracks (ms) — logarithmic scale
    addAndMakeVisible(&timerIntervalSlider);
    {
        // Logarithmic range: 1 ms to 3000 ms
        // Using NormalisableRange with a skew factor centred at 100 ms
        juce::NormalisableRange<double> logRange(50.0, 3000.0);
        logRange.setSkewForCentre(500.0);   // midpoint of the slider = 500 ms
        timerIntervalSlider.setNormalisableRange(logRange);
    }
    timerIntervalSlider.setValue(500.0);
    timerIntervalSlider.setTextValueSuffix(" ms");
    timerIntervalSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    timerIntervalSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 70, 20);
    timerIntervalSlider.setNumDecimalPlacesToDisplay(0);  // Round to nearest integer

    addAndMakeVisible(&timerIntervalLabel);
    timerIntervalLabel.setText("Interval:", juce::dontSendNotification);
    timerIntervalLabel.attachToComponent(&timerIntervalSlider, true);

    // Toggle between sequential and random track order
    addAndMakeVisible(&randomOrderButton);
    randomOrderButton.setButtonText("Random Order");
    randomOrderButton.setToggleState(false, juce::dontSendNotification);
    randomOrderButton.onClick = [this]
        {
            useRandomOrder = randomOrderButton.getToggleState();
        };

    // Master volume slider (0.0 to 1.0, default 1.0)
    addAndMakeVisible(&masterVolumeSlider);
    masterVolumeSlider.setRange(0.0, 1.0, 0.01);
    masterVolumeSlider.setValue(0.5);
    masterVolumeSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    masterVolumeSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 50, 20);
    masterVolumeSlider.onValueChange = [this]
        {
            masterVolume.store((float)masterVolumeSlider.getValue());
        };

    addAndMakeVisible(&masterVolumeLabel);
    masterVolumeLabel.setText("Volume:", juce::dontSendNotification);
    masterVolumeLabel.attachToComponent(&masterVolumeSlider, true);

    // --- EQ sliders (±12 dB, default 0 dB) ---
    auto setupEqSlider = [this](juce::Slider& s, juce::Label& l, const juce::String& name)
        {
            addAndMakeVisible(s);
            s.setRange(-12.0, 12.0, 0.1);
            s.setValue(0.0);
            s.setTextValueSuffix(" dB");
            s.setSliderStyle(juce::Slider::LinearVertical);
            s.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 60, 28);
            s.onValueChange = [this] { updateEQ(); };

            addAndMakeVisible(l);
            l.setText(name, juce::dontSendNotification);
            l.setJustificationType(juce::Justification::centred);
        };

    setupEqSlider(eqLowSlider, eqLowLabel, "Low\n200Hz");
    setupEqSlider(eqMidSlider, eqMidLabel, "Mid\n1kHz");
    setupEqSlider(eqHighSlider, eqHighLabel, "High\n5kHz");

    // --- Reverb sliders ---
    auto setupReverbSlider = [this](juce::Slider& s, juce::Label& l, const juce::String& name, double defaultVal)
        {
            addAndMakeVisible(s);
            s.setRange(0.0, 1.0, 0.01);
            s.setValue(defaultVal);
            s.setSliderStyle(juce::Slider::LinearHorizontal);
            s.setTextBoxStyle(juce::Slider::TextBoxRight, false, 50, 20);
            s.onValueChange = [this] { updateReverb(); };

            addAndMakeVisible(l);
            l.setText(name, juce::dontSendNotification);
            l.attachToComponent(&s, true);
        };

    setupReverbSlider(reverbRoomSizeSlider, reverbRoomSizeLabel, "Room:", 0.5);
    setupReverbSlider(reverbWetSlider, reverbWetLabel, "Wet:", 0.0);

    // Initialize audio device (stereo output, no input)
    setAudioChannels(0, 2);

    setSize(400, 480);
}

MainComponent::~MainComponent()
{
    stopTimer();

    // Detach all sources from the mixer before shutting down
    mixer.removeAllInputs();
    for (auto* ch : channels)
        ch->transportSource.setSource(nullptr);

    shutdownAudio();
}

//==============================================================================
// Timer callback:
//   Fires 100 ms after a track finishes naturally; advances to the next file.
void MainComponent::timerCallback()
{
    playNext();

    if (currentIndex >= 0 && currentIndex < playlist.size())
    {
        currentPositionLabel.setText(
            juce::String(currentIndex + 1) + " / " + juce::String(playlist.size()),
            juce::dontSendNotification);
    }
}

//==============================================================================
void MainComponent::prepareToPlay(int samplesPerBlockExpected, double sampleRate)
{
    for (auto* ch : channels)
        ch->transportSource.prepareToPlay(samplesPerBlockExpected, sampleRate);

    mixer.prepareToPlay(samplesPerBlockExpected, sampleRate);

    currentSampleRate = sampleRate;

    juce::dsp::ProcessSpec spec;
    spec.sampleRate = sampleRate;
    spec.maximumBlockSize = (juce::uint32)samplesPerBlockExpected;
    spec.numChannels = 1;   // Each chain handles one channel

    eqLeft.prepare(spec);
    eqRight.prepare(spec);

    updateEQ();

    reverb.setSampleRate(sampleRate);
    updateReverb();
}

void MainComponent::getNextAudioBlock(const juce::AudioSourceChannelInfo& bufferToFill)
{
    // Clear first; the mixer will accumulate all active channels into a temp buffer.
    bufferToFill.clearActiveBufferRegion();

    // Process each channel independently so we can apply its own pan.
    juce::AudioBuffer<float> channelBuf(bufferToFill.buffer->getNumChannels(),
        bufferToFill.numSamples);

    for (auto* ch : channels)
    {
        if (ch->readerSource.get() == nullptr)
            continue;

        channelBuf.clear();

        // Fill channelBuf from this transport source
        juce::AudioSourceChannelInfo info(&channelBuf, 0, bufferToFill.numSamples);
        ch->transportSource.getNextAudioBlock(info);

        // --- Equal-power panning for this channel ---
        // pan: -1.0 (full left) .. 0.0 (centre) .. +1.0 (full right)
        const float p = ch->pan.load();
        const float angle = (p + 1.0f) * juce::MathConstants<float>::halfPi / 2.0f;
        const float gainLeft = std::cos(angle);
        const float gainRight = std::sin(angle);

        if (channelBuf.getNumChannels() >= 1)
            channelBuf.applyGain(0, 0, bufferToFill.numSamples, gainLeft);
        if (channelBuf.getNumChannels() >= 2)
            channelBuf.applyGain(1, 0, bufferToFill.numSamples, gainRight);

        // Accumulate into the output buffer
        for (int c = 0; c < bufferToFill.buffer->getNumChannels(); ++c)
        {
            bufferToFill.buffer->addFrom(c, bufferToFill.startSample,
                channelBuf, c, 0,
                bufferToFill.numSamples);
        }
    }

    // Apply master volume to the final mixed output
    bufferToFill.buffer->applyGain(bufferToFill.startSample,
        bufferToFill.numSamples,
        masterVolume.load());

    // Apply 3-band EQ — process left and right channels separately
    {
        auto* buffer = bufferToFill.buffer;
        const int start = bufferToFill.startSample;
        const int num = bufferToFill.numSamples;

        if (buffer->getNumChannels() >= 1)
        {
            juce::dsp::AudioBlock<float> block(buffer->getArrayOfWritePointers(),
                1, (size_t)start, (size_t)num);
            juce::dsp::ProcessContextReplacing<float> ctx(block);
            eqLeft.process(ctx);
        }
        if (buffer->getNumChannels() >= 2)
        {
            auto* rightPtr = buffer->getArrayOfWritePointers() + 1;
            juce::dsp::AudioBlock<float> block(rightPtr, 1, (size_t)start, (size_t)num);
            juce::dsp::ProcessContextReplacing<float> ctx(block);
            eqRight.process(ctx);
        }
    }

    // Apply reverb (stereo) after EQ
    {
        auto* buffer = bufferToFill.buffer;
        const int start = bufferToFill.startSample;
        const int num = bufferToFill.numSamples;

        if (buffer->getNumChannels() >= 2)
            reverb.processStereo(buffer->getWritePointer(0, start),
                buffer->getWritePointer(1, start), num);
        else if (buffer->getNumChannels() == 1)
            reverb.processMono(buffer->getWritePointer(0, start), num);
    }
}

void MainComponent::releaseResources()
{
    mixer.releaseResources();
    for (auto* ch : channels)
        ch->transportSource.releaseResources();

    eqLeft.reset();
    eqRight.reset();
    reverb.reset();
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
    // Labels are attached via attachToComponent; only lay out the sliders
    timerIntervalSlider.setBounds(70, 135, getWidth() - 80, 25);
    randomOrderButton.setBounds(10, 170, getWidth() - 20, 25);
    masterVolumeSlider.setBounds(70, 205, getWidth() - 80, 25);

    // EQ sliders arranged side by side with labels above
    const int eqTop = 245;
    const int eqH = 120;
    const int labelH = 32;
    const int totalW = getWidth() - 20;
    const int bandW = totalW / 3;

    eqLowLabel.setBounds(10, eqTop, bandW, labelH);
    eqMidLabel.setBounds(10 + bandW, eqTop, bandW, labelH);
    eqHighLabel.setBounds(10 + bandW * 2, eqTop, bandW, labelH);

    eqLowSlider.setBounds(10, eqTop + labelH, bandW, eqH);
    eqMidSlider.setBounds(10 + bandW, eqTop + labelH, bandW, eqH);
    eqHighSlider.setBounds(10 + bandW * 2, eqTop + labelH, bandW, eqH);

    // Reverb sliders below EQ
    const int revTop = eqTop + labelH + eqH + 10;
    reverbRoomSizeSlider.setBounds(70, revTop, getWidth() - 80, 25);
    reverbWetSlider.setBounds(70, revTop + 35, getWidth() - 80, 25);
}

//==============================================================================
void MainComponent::changeListenerCallback(juce::ChangeBroadcaster* source)
{
    for (auto* ch : channels)
    {
        if (source != &ch->transportSource)
            continue;

        if (ch->transportSource.isPlaying())
        {
            changeState(TransportState::Playing);
        }
        else
        {
            if (state == TransportState::Playing)
            {
                // This channel finished naturally -> schedule next file
                startTimer((int)timerIntervalSlider.getValue());
            }
            else if (state == TransportState::Stopping)
            {
                // User pressed Stop
                changeState(TransportState::Stopped);
            }
        }
        break;
    }
}

//==============================================================================
void MainComponent::changeState(TransportState newState)
{
    if (state == newState)
        return;

    state = newState;

    switch (state)
    {
    case TransportState::Stopped:
        playButton.setButtonText("Play");
        playButton.setEnabled(true);
        break;

    case TransportState::Starting:
        // Start the channel that was just loaded in play()
        channels[nextChannel == 0 ? NUM_CHANNELS - 1 : nextChannel - 1]
            ->transportSource.start();
        break;

    case TransportState::Playing:
        playButton.setButtonText("Stop");
        break;

    case TransportState::Stopping:
        for (auto* ch : channels)
            ch->transportSource.stop();
        break;
    }
}

//==============================================================================
void MainComponent::selectFolder()
{
    chooser = std::make_unique<juce::FileChooser>(
        "Select a folder containing MP3 files...",
        juce::File("E:\\Sounds\\SoundZapper"),
        ""
    );

    auto chooserFlags = juce::FileBrowserComponent::openMode
        | juce::FileBrowserComponent::canSelectDirectories;

    chooser->launchAsync(chooserFlags, [this](const juce::FileChooser& fc)
        {
            auto folder = fc.getResult();
            if (folder == juce::File{} || !folder.isDirectory())
                return;

            currentFolder = folder;
            loadPlaylistFromFolder(currentFolder);

            if (!playlist.isEmpty())
            {
                currentIndex = 0;
                currentPositionLabel.setText(
                    "1 / " + juce::String(playlist.size()),
                    juce::dontSendNotification);
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

void MainComponent::play()
{
    startTimer((int)timerIntervalSlider.getValue());
    if (currentIndex < 0 || currentIndex >= playlist.size())
        return;

    // Pick the next channel in round-robin order
    const int targetCh = nextChannel;
    nextChannel = (nextChannel + 1) % NUM_CHANNELS;

    auto* ch = channels[targetCh];

    // Randomise pan for this channel
    randomisePan(targetCh);

    auto file = playlist[currentIndex];
    auto* reader = formatManager.createReaderFor(file);
    if (reader == nullptr)
        return;

    ch->transportSource.setSource(nullptr);   // detach old source first
    auto newSource = std::make_unique<juce::AudioFormatReaderSource>(reader, true);
    ch->transportSource.setSource(newSource.get(), 0, nullptr, reader->sampleRate);
    ch->readerSource = std::move(newSource);

    ch->transportSource.setPosition(0.0);
    ch->transportSource.start();

    DBG("play(): track " + juce::String(currentIndex + 1)
        + " -> Ch" + juce::String(targetCh + 1)
        + "  pan=" + juce::String(ch->pan.load(), 2));
}

void MainComponent::randomisePan(int ch)
{
    const float newPan = random.nextFloat() * 2.0f - 1.0f; // [-1.0, +1.0]
    channels[ch]->pan.store(newPan);

    // Rebuild the pan label showing all channels
    juce::String text;
    for (int i = 0; i < NUM_CHANNELS; ++i)
    {
        const float p = channels[i]->pan.load();
        juce::String side = (p < -0.05f) ? "L" : (p > 0.05f) ? "R" : "C";
        text += "Ch" + juce::String(i + 1) + ":" + side + juce::String(p, 2);
        if (i < NUM_CHANNELS - 1) text += "  ";
    }
}

void MainComponent::playNext()
{
    if (playlist.isEmpty())
        return;

    int nextIndex;
    if (useRandomOrder)
    {
        // Pick a random track (avoid repeating the current one if possible)
        if (playlist.size() > 1)
        {
            do { nextIndex = random.nextInt(playlist.size()); } while (nextIndex == currentIndex);
        }
        else
        {
            nextIndex = 0;
        }
    }
    else
    {
        nextIndex = currentIndex + 1;
        if (nextIndex >= playlist.size())
            nextIndex = 0; // Wrap around to the first track
    }

    currentIndex = nextIndex;
    play();
}

//==============================================================================
void MainComponent::updateEQ()
{
    // Called on the message thread; coefficients are swapped atomically by JUCE
    const double sr = currentSampleRate;

    // Low shelf  ~200 Hz
    *eqLeft.get<0>().coefficients =
        *FilterCoefs::makeLowShelf(sr, 200.0, 0.707, juce::Decibels::decibelsToGain((float)eqLowSlider.getValue()));
    *eqRight.get<0>().coefficients =
        *FilterCoefs::makeLowShelf(sr, 200.0, 0.707, juce::Decibels::decibelsToGain((float)eqLowSlider.getValue()));

    // Mid peak  ~1 kHz
    *eqLeft.get<1>().coefficients =
        *FilterCoefs::makePeakFilter(sr, 1000.0, 0.707, juce::Decibels::decibelsToGain((float)eqMidSlider.getValue()));
    *eqRight.get<1>().coefficients =
        *FilterCoefs::makePeakFilter(sr, 1000.0, 0.707, juce::Decibels::decibelsToGain((float)eqMidSlider.getValue()));

    // High shelf ~5 kHz
    *eqLeft.get<2>().coefficients =
        *FilterCoefs::makeHighShelf(sr, 5000.0, 0.707, juce::Decibels::decibelsToGain((float)eqHighSlider.getValue()));
    *eqRight.get<2>().coefficients =
        *FilterCoefs::makeHighShelf(sr, 5000.0, 0.707, juce::Decibels::decibelsToGain((float)eqHighSlider.getValue()));
}

//==============================================================================
void MainComponent::updateReverb()
{
    juce::Reverb::Parameters params;
    params.roomSize = (float)reverbRoomSizeSlider.getValue();
    params.wetLevel = (float)reverbWetSlider.getValue();
    params.dryLevel = 1.0f - params.wetLevel;  // Dry fades as wet increases
    params.damping = 0.5f;
    params.width = 1.0f;
    params.freezeMode = 0.0f;
    reverb.setParameters(params);
}

void MainComponent::playButtonClicked()
{
    if (state == TransportState::Stopped)
    {
        play();
        changeState(TransportState::Playing); // already started in play()
    }
    else
    {
        stopTimer();
        changeState(TransportState::Stopping);
    }
}