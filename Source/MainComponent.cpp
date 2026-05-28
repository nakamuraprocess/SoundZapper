#include "MainComponent.h"

//==============================================================================
MainComponent::MainComponent()
    : state(TransportState::Stopped)
{
    formatManager.registerBasicFormats();

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

    addAndMakeVisible(&currentPositionLabel);
    currentPositionLabel.setText("", juce::dontSendNotification);

    // Interval slider (logarithmic, 50-3000 ms)
    addAndMakeVisible(&timerIntervalSlider);
    {
        juce::NormalisableRange<double> logRange(50.0, 3000.0);
        logRange.setSkewForCentre(500.0);
        timerIntervalSlider.setNormalisableRange(logRange);
    }
    timerIntervalSlider.setValue(500.0);
    timerIntervalSlider.setTextValueSuffix(" ms");
    timerIntervalSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    timerIntervalSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 70, 20);
    timerIntervalSlider.setNumDecimalPlacesToDisplay(0);

    addAndMakeVisible(&timerIntervalLabel);
    timerIntervalLabel.setText("Interval:", juce::dontSendNotification);
    timerIntervalLabel.attachToComponent(&timerIntervalSlider, true);

    // Random order toggle
    addAndMakeVisible(&randomOrderButton);
    randomOrderButton.setButtonText("Random Order");
    randomOrderButton.setToggleState(false, juce::dontSendNotification);
    randomOrderButton.onClick = [this]
        {
            useRandomOrder = randomOrderButton.getToggleState();
        };

    // Pan mode selector
    addAndMakeVisible(&panModeComboBox);
    panModeComboBox.addItem("Left", 1);
    panModeComboBox.addItem("Centre", 2);
    panModeComboBox.addItem("Right", 3);
    panModeComboBox.addItem("Random", 4);
    panModeComboBox.setSelectedId(4, juce::dontSendNotification);

    addAndMakeVisible(&panModeLabel);
    panModeLabel.setText("Pan:", juce::dontSendNotification);
    panModeLabel.attachToComponent(&panModeComboBox, true);

    // Master volume slider
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

    // EQ sliders (±12 dB)
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

    // Reverb range sliders: random value is chosen per channel within [min, max]
    auto setupRevPair = [this](juce::Slider& minS, juce::Label& minL,
        juce::Slider& maxS, juce::Label& maxL,
        double defaultMin, double defaultMax)
        {
            auto init = [this](juce::Slider& s, double val)
                {
                    addAndMakeVisible(s);
                    s.setRange(0.0, 1.0, 0.01);
                    s.setValue(val);
                    s.setSliderStyle(juce::Slider::LinearHorizontal);
                    s.setTextBoxStyle(juce::Slider::TextBoxRight, false, 45, 20);
                };
            init(minS, defaultMin);
            init(maxS, defaultMax);
            // Constrain: min <= max
            minS.onValueChange = [&minS, &maxS] {
                if (minS.getValue() > maxS.getValue())
                    maxS.setValue(minS.getValue(), juce::dontSendNotification);
                };
            maxS.onValueChange = [&minS, &maxS] {
                if (maxS.getValue() < minS.getValue())
                    minS.setValue(maxS.getValue(), juce::dontSendNotification);
                };
            addAndMakeVisible(minL);
            minL.setText("Min", juce::dontSendNotification);
            minL.setJustificationType(juce::Justification::centred);
            addAndMakeVisible(maxL);
            maxL.setText("Max", juce::dontSendNotification);
            maxL.setJustificationType(juce::Justification::centred);
        };

    addAndMakeVisible(reverbRoomSizeLabel);
    reverbRoomSizeLabel.setText("Room:", juce::dontSendNotification);
    setupRevPair(reverbRoomSizeMinSlider, reverbRoomSizeMinLabel,
        reverbRoomSizeMaxSlider, reverbRoomSizeMaxLabel, 0.2, 0.9);

    addAndMakeVisible(reverbWetLabel);
    reverbWetLabel.setText("Wet:", juce::dontSendNotification);
    setupRevPair(reverbWetMinSlider, reverbWetMinLabel,
        reverbWetMaxSlider, reverbWetMaxLabel, 0.1, 0.6);

    // Reverb probability slider (0-100%)
    addAndMakeVisible(reverbProbabilitySlider);
    reverbProbabilitySlider.setRange(0.0, 100.0, 1.0);
    reverbProbabilitySlider.setValue(50.0);
    reverbProbabilitySlider.setTextValueSuffix(" %");
    reverbProbabilitySlider.setSliderStyle(juce::Slider::LinearHorizontal);
    reverbProbabilitySlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 50, 20);
    reverbProbabilitySlider.setNumDecimalPlacesToDisplay(0);

    addAndMakeVisible(reverbProbabilityLabel);
    reverbProbabilityLabel.setText("Rev Prob:", juce::dontSendNotification);
    reverbProbabilityLabel.attachToComponent(&reverbProbabilitySlider, true);

    auto setupSectionTitle = [this](juce::Label& l, const juce::String& text)
        {
            addAndMakeVisible(l);
            l.setText(text, juce::dontSendNotification);
            l.setFont(juce::Font(15.0f, juce::Font::bold));
            l.setColour(juce::Label::textColourId, juce::Colours::lightblue);
        };

    setupSectionTitle(sectionPlaybackTitle, "Playback");
    setupSectionTitle(sectionReverbTitle, "Reverb");
    setupSectionTitle(sectionEqTitle, "Equalizer");

    setAudioChannels(0, 2);
    setSize(400, 650);
}

MainComponent::~MainComponent()
{
    stopTimer();

    {
        juce::ScopedLock sl(channelsLock);
        mixer.removeAllInputs();
        for (auto* ch : channels)
            ch->transportSource.setSource(nullptr);
    }

    shutdownAudio();
}

//==============================================================================
PlayChannel* MainComponent::createChannel()
{
    juce::ScopedLock sl(channelsLock);

    if (channels.size() >= MAX_CHANNELS)
    {
        DBG("createChannel: MAX_CHANNELS reached (" + juce::String(MAX_CHANNELS) + ")");
        return nullptr;
    }

    auto* ch = channels.add(new PlayChannel());
    ch->transportSource.addChangeListener(this);
    ch->transportSource.prepareToPlay(currentBlockSize, currentSampleRate);
    ch->reverb.setSampleRate(currentSampleRate);
    mixer.addInputSource(&ch->transportSource, false);

    DBG("createChannel: active=" + juce::String(channels.size()));
    return ch;
}

void MainComponent::removeFinishedChannels()
{
    juce::ScopedLock sl(channelsLock);

    for (int i = channels.size() - 1; i >= 0; --i)
    {
        auto* ch = channels[i];

        if (ch->tailMode)
        {
            // Audio thread signals completion via tailFinished
            if (ch->tailFinished.load())
            {
                mixer.removeInputSource(&ch->transportSource);
                ch->transportSource.setSource(nullptr);
                channels.remove(i, true);
                DBG("tail finished, channel removed");
            }
        }
        else if (!ch->transportSource.isPlaying())
        {
            if (ch->tailSamplesRemaining > 0)
            {
                // File finished — enter tail mode to let reverb decay
                ch->tailMode = true;
                DBG("tail mode: " + juce::String(ch->tailSamplesRemaining / (int)currentSampleRate) + "s remaining");
            }
            else
            {
                // No reverb tail needed — remove immediately
                mixer.removeInputSource(&ch->transportSource);
                ch->transportSource.setSource(nullptr);
                channels.remove(i, true);
            }
        }
    }

    DBG("removeFinishedChannels: active=" + juce::String(channels.size()));
}

//==============================================================================
void MainComponent::timerCallback()
{
    removeFinishedChannels();

    playNext();

    if (currentIndex >= 0 && currentIndex < playlist.size())
    {
        currentPositionLabel.setText(
            juce::String(currentIndex + 1) + " / " + juce::String(playlist.size())
            + "  " + playlist[currentIndex].getFileNameWithoutExtension(),
            juce::dontSendNotification);
    }
}

//==============================================================================
void MainComponent::prepareToPlay(int samplesPerBlockExpected, double sampleRate)
{
    currentSampleRate = sampleRate;
    currentBlockSize = samplesPerBlockExpected;

    mixer.prepareToPlay(samplesPerBlockExpected, sampleRate);

    juce::dsp::ProcessSpec spec;
    spec.sampleRate = sampleRate;
    spec.maximumBlockSize = (juce::uint32)samplesPerBlockExpected;
    spec.numChannels = 1;

    eqLeft.prepare(spec);
    eqRight.prepare(spec);
    updateEQ();
}

void MainComponent::getNextAudioBlock(const juce::AudioSourceChannelInfo& bufferToFill)
{
    bufferToFill.clearActiveBufferRegion();

    juce::AudioBuffer<float> channelBuf(bufferToFill.buffer->getNumChannels(),
        bufferToFill.numSamples);
    {
        juce::ScopedLock sl(channelsLock);

        for (auto* ch : channels)
        {
            // Skip channels with no source, unless in tail mode (reverb decay)
            if (ch->readerSource.get() == nullptr && !ch->tailMode)
                continue;

            channelBuf.clear();

            if (!ch->tailMode)
            {
                // Normal playback
                juce::AudioSourceChannelInfo info(&channelBuf, 0, bufferToFill.numSamples);
                ch->transportSource.getNextAudioBlock(info);
            }
            else
            {
                // Tail mode: feed silence so reverb can decay naturally
                ch->tailSamplesRemaining -= bufferToFill.numSamples;
                if (ch->tailSamplesRemaining <= 0)
                {
                    ch->tailFinished.store(true);
                    continue; // Skip further processing for this channel
                }
            }

            // Per-channel reverb
            if (channelBuf.getNumChannels() >= 2)
                ch->reverb.processStereo(channelBuf.getWritePointer(0),
                    channelBuf.getWritePointer(1),
                    bufferToFill.numSamples);
            else if (channelBuf.getNumChannels() == 1)
                ch->reverb.processMono(channelBuf.getWritePointer(0),
                    bufferToFill.numSamples);

            // Equal-power panning
            const float p = ch->pan.load();
            const float angle = (p + 1.0f) * juce::MathConstants<float>::halfPi / 2.0f;
            const float gainLeft = std::cos(angle);
            const float gainRight = std::sin(angle);

            if (channelBuf.getNumChannels() >= 1)
                channelBuf.applyGain(0, 0, bufferToFill.numSamples, gainLeft);
            if (channelBuf.getNumChannels() >= 2)
                channelBuf.applyGain(1, 0, bufferToFill.numSamples, gainRight);

            for (int c = 0; c < bufferToFill.buffer->getNumChannels(); ++c)
                bufferToFill.buffer->addFrom(c, bufferToFill.startSample,
                    channelBuf, c, 0,
                    bufferToFill.numSamples);
        }
    }

    // Master volume
    bufferToFill.buffer->applyGain(bufferToFill.startSample,
        bufferToFill.numSamples,
        masterVolume.load());

    // 3-band EQ
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
}

void MainComponent::releaseResources()
{
    juce::ScopedLock sl(channelsLock);
    mixer.releaseResources();
    for (auto* ch : channels)
        ch->transportSource.releaseResources();

    eqLeft.reset();
    eqRight.reset();
}

//==============================================================================
void MainComponent::paint(juce::Graphics& g)
{
    g.fillAll(getLookAndFeel().findColour(juce::ResizableWindow::backgroundColourId));

    g.setColour(juce::Colours::grey.withAlpha(0.6f));
    auto drawSep = [&](const juce::Label& titleLabel)
        {
            int y = titleLabel.getY() - 5;
            g.drawHorizontalLine(y, 10.0f, (float)(getWidth() - 10));
        };
    drawSep(sectionReverbTitle);
    drawSep(sectionEqTitle);
}

void MainComponent::resized()
{
    const int titleH = 22;
    const int margin = 10;

    // --- Playback section ---
    sectionPlaybackTitle.setBounds(margin, 10, getWidth() - margin * 2, titleH);
    openButton.setBounds(margin, 37, getWidth() - margin * 2, 30);
    playButton.setBounds(margin, 77, getWidth() - margin * 2, 30);
    currentPositionLabel.setBounds(margin, 117, getWidth() - margin * 2, 25);
    timerIntervalSlider.setBounds(70, 152, getWidth() - 80, 25);
    randomOrderButton.setBounds(margin, 187, getWidth() - margin * 2, 25);
    panModeComboBox.setBounds(70, 222, getWidth() - 80, 25);
    masterVolumeSlider.setBounds(70, 257, getWidth() - 80, 25);

    // --- Reverb section ---
    sectionReverbTitle.setBounds(margin, 292, getWidth() - margin * 2, titleH);

    const int revTop = 319;
    const int halfW = (getWidth() - 80) / 2;
    const int sliderX = 55;
    const int tbW = 45;
    const int subLH = 18;
    const int rowStep = subLH + 25 + 8;

    reverbRoomSizeLabel.setBounds(margin, revTop + subLH, 45, 25);
    reverbRoomSizeMinLabel.setBounds(sliderX + halfW - tbW, revTop, tbW, subLH);
    reverbRoomSizeMaxLabel.setBounds(sliderX + halfW * 2 - tbW, revTop, tbW, subLH);
    reverbRoomSizeMinSlider.setBounds(sliderX, revTop + subLH, halfW, 25);
    reverbRoomSizeMaxSlider.setBounds(sliderX + halfW, revTop + subLH, halfW, 25);

    reverbWetLabel.setBounds(margin, revTop + rowStep + subLH, 45, 25);
    reverbWetMinLabel.setBounds(sliderX + halfW - tbW, revTop + rowStep, tbW, subLH);
    reverbWetMaxLabel.setBounds(sliderX + halfW * 2 - tbW, revTop + rowStep, tbW, subLH);
    reverbWetMinSlider.setBounds(sliderX, revTop + rowStep + subLH, halfW, 25);
    reverbWetMaxSlider.setBounds(sliderX + halfW, revTop + rowStep + subLH, halfW, 25);

    reverbProbabilitySlider.setBounds(70, revTop + rowStep * 2, getWidth() - 80, 25);

    // --- Equalizer section ---
    const int revBottom = revTop + rowStep * 2 + 25;
    sectionEqTitle.setBounds(margin, revBottom + 14, getWidth() - margin * 2, titleH);

    const int eqTop = revBottom + 14 + titleH + 5;
    const int eqH = 120;
    const int labelH = 32;
    const int bandW = (getWidth() - margin * 2) / 3;

    eqLowLabel.setBounds(margin, eqTop, bandW, labelH);
    eqMidLabel.setBounds(margin + bandW, eqTop, bandW, labelH);
    eqHighLabel.setBounds(margin + bandW * 2, eqTop, bandW, labelH);

    eqLowSlider.setBounds(margin, eqTop + labelH, bandW, eqH);
    eqMidSlider.setBounds(margin + bandW, eqTop + labelH, bandW, eqH);
    eqHighSlider.setBounds(margin + bandW * 2, eqTop + labelH, bandW, eqH);
}

//==============================================================================
void MainComponent::changeListenerCallback(juce::ChangeBroadcaster* source)
{
    for (auto* ch : channels)
    {
        if (source != &ch->transportSource)
            continue;

        if (!ch->transportSource.isPlaying())
        {
            if (state == TransportState::Playing)
                startTimer((int)timerIntervalSlider.getValue());
            else if (state == TransportState::Stopping)
                changeState(TransportState::Stopped);
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
        stopTimer();
        playButton.setButtonText("Play");
        playButton.setEnabled(true);
        break;

    case TransportState::Playing:
        playButton.setButtonText("Stop");
        break;

    case TransportState::Stopping:
    {
        juce::ScopedLock sl(channelsLock);
        mixer.removeAllInputs();
        for (auto* ch : channels)
            ch->transportSource.setSource(nullptr);
    }
    juce::MessageManager::callAsync([this]
        {
            juce::ScopedLock sl(channelsLock);
            channels.clear(true);
        });
    changeState(TransportState::Stopped);
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
                    "1 / " + juce::String(playlist.size())
                    + "  " + playlist[0].getFileNameWithoutExtension(),
                    juce::dontSendNotification);
            }
        });
}

void MainComponent::loadPlaylistFromFolder(const juce::File& folder)
{
    playlist.clear();

    for (auto& file : folder.findChildFiles(juce::File::findFiles, false, "*.wav"))
        playlist.add(file);

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

    auto* ch = createChannel();
    if (ch == nullptr)
        return;

    randomisePan(ch);
    randomiseReverb(ch);

    auto file = playlist[currentIndex];
    auto* reader = formatManager.createReaderFor(file);
    if (reader == nullptr)
        return;

    auto newSource = std::make_unique<juce::AudioFormatReaderSource>(reader, true);
    ch->transportSource.setSource(newSource.get(), 0, nullptr, reader->sampleRate);
    ch->readerSource = std::move(newSource);
    ch->transportSource.setPosition(0.0);
    ch->transportSource.start();

    DBG("play(): track=" + juce::String(currentIndex + 1)
        + "  pan=" + juce::String(ch->pan.load(), 2)
        + "  active channels=" + juce::String(channels.size()));
}

void MainComponent::randomisePan(PlayChannel* ch)
{
    const int mode = panModeComboBox.getSelectedId();
    float pan = 0.0f;

    switch (mode)
    {
    case 1: pan = -1.0f; break;                            // Left
    case 2: pan = 0.0f; break;                            // Centre
    case 3: pan = 1.0f; break;                            // Right
    case 4: pan = random.nextFloat() * 2.0f - 1.0f; break; // Random
    default: pan = 0.0f; break;
    }

    ch->pan.store(pan);
}

void MainComponent::playNext()
{
    if (playlist.isEmpty())
        return;

    int nextIndex;
    if (useRandomOrder)
    {
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
            nextIndex = 0;
    }

    currentIndex = nextIndex;
    play();
}

//==============================================================================
void MainComponent::updateEQ()
{
    const double sr = currentSampleRate;

    *eqLeft.get<0>().coefficients =
        *FilterCoefs::makeLowShelf(sr, 200.0, 0.707, juce::Decibels::decibelsToGain((float)eqLowSlider.getValue()));
    *eqRight.get<0>().coefficients =
        *FilterCoefs::makeLowShelf(sr, 200.0, 0.707, juce::Decibels::decibelsToGain((float)eqLowSlider.getValue()));

    *eqLeft.get<1>().coefficients =
        *FilterCoefs::makePeakFilter(sr, 1000.0, 0.707, juce::Decibels::decibelsToGain((float)eqMidSlider.getValue()));
    *eqRight.get<1>().coefficients =
        *FilterCoefs::makePeakFilter(sr, 1000.0, 0.707, juce::Decibels::decibelsToGain((float)eqMidSlider.getValue()));

    *eqLeft.get<2>().coefficients =
        *FilterCoefs::makeHighShelf(sr, 5000.0, 0.707, juce::Decibels::decibelsToGain((float)eqHighSlider.getValue()));
    *eqRight.get<2>().coefficients =
        *FilterCoefs::makeHighShelf(sr, 5000.0, 0.707, juce::Decibels::decibelsToGain((float)eqHighSlider.getValue()));
}

void MainComponent::randomiseReverb(PlayChannel* ch)
{
    const float probability = (float)reverbProbabilitySlider.getValue() / 100.0f;
    if (random.nextFloat() > probability)
    {
        juce::Reverb::Parameters dry;
        dry.wetLevel = 0.0f;
        dry.dryLevel = 1.0f;
        ch->reverb.setParameters(dry);
        ch->reverb.reset();
        ch->tailSamplesRemaining = 0;  // No tail needed
        return;
    }

    const float roomMin = (float)std::min(reverbRoomSizeMinSlider.getValue(), reverbRoomSizeMaxSlider.getValue());
    const float roomMax = (float)std::max(reverbRoomSizeMinSlider.getValue(), reverbRoomSizeMaxSlider.getValue());
    const float wetMin = (float)std::min(reverbWetMinSlider.getValue(), reverbWetMaxSlider.getValue());
    const float wetMax = (float)std::max(reverbWetMinSlider.getValue(), reverbWetMaxSlider.getValue());

    juce::Reverb::Parameters params;
    params.roomSize = roomMin + random.nextFloat() * (roomMax - roomMin);
    params.wetLevel = wetMin + random.nextFloat() * (wetMax - wetMin);
    params.dryLevel = 1.0f - params.wetLevel;
    params.damping = 0.5f;
    params.width = 1.0f;
    params.freezeMode = 0.0f;
    ch->reverb.setParameters(params);
    ch->reverb.reset();

    // Estimate tail duration: roomSize * 6 seconds (max ~6 s for roomSize=1.0)
    const float tailSeconds = params.roomSize * 6.0f;
    ch->tailSamplesRemaining = (int)(tailSeconds * (float)currentSampleRate);
}

void MainComponent::playButtonClicked()
{
    if (state == TransportState::Stopped)
    {
        play();
        changeState(TransportState::Playing);
    }
    else
    {
        changeState(TransportState::Stopping);
    }
}