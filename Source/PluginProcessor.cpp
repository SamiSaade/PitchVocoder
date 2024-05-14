#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "PluginParameter.h"


//==============================================================================

HarmonizerAudioProcessor::HarmonizerAudioProcessor():
#ifndef JucePlugin_PreferredChannelConfigurations
    AudioProcessor (BusesProperties()
                    #if ! JucePlugin_IsMidiEffect
                     #if ! JucePlugin_IsSynth
                      .withInput  ("Input",  AudioChannelSet::stereo(), true)
                     #endif
                      .withOutput ("Output", AudioChannelSet::stereo(), true)
                    #endif
                   ),
#endif
    parameters (*this)
    //, paramShift (parameters, "Shift", " Semitone(s)", -12.0f, 12.0f, 0.0f,
    //              [this](float value){ return powf (2.0f, value / 12.0f); })
    , paramThreshold (parameters, "Threshold", "", 0.1f, 0.4f, 0.1f,
                    [this](float value) {return value; })
    , paramFftSize (parameters, "FFT size", fftSizeItemsUI, fftSize512,
                    [this](float value){
                        const ScopedLock sl (lock);
                        value = (float)(1 << ((int)value + 5));
                        paramFftSize.setCurrentAndTargetValue (value);
                        updateFftSize();
                        updateHopSize();
                        updateAnalysisWindow();
                        updateWindowScaleFactor();
                        return value;
                    })
    , paramHopSize (parameters, "Hop size", hopSizeItemsUI, hopSize8,
                    [this](float value){
                        const ScopedLock sl (lock);
                        value = (float)(1 << ((int)value + 1));
                        paramHopSize.setCurrentAndTargetValue (value);
                        updateFftSize();
                        updateHopSize();
                        updateAnalysisWindow();
                        updateWindowScaleFactor();
                        return value;
                    })
    , paramWindowType (parameters, "Window type", windowTypeItemsUI, windowTypeHann,
                       [this](float value){
                           const ScopedLock sl (lock);
                           paramWindowType.setCurrentAndTargetValue (value);
                           updateFftSize();
                           updateHopSize();
                           updateAnalysisWindow();
                           updateWindowScaleFactor();
                           return value;
                       })
{
    parameters.apvts.state = ValueTree (Identifier (getName().removeCharacters ("- ")));
}

HarmonizerAudioProcessor::~HarmonizerAudioProcessor()
{
}

//==============================================================================

void HarmonizerAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    //Phase vocoder setup
    const double smoothTime = 1e-3;
    //paramShift.reset (sampleRate, smoothTime);
    paramThreshold.reset(sampleRate, smoothTime);
    paramFftSize.reset (sampleRate, smoothTime);
    paramHopSize.reset (sampleRate, smoothTime);
    paramWindowType.reset (sampleRate, smoothTime);

    needToResetPhases = true;
    needToUpdateThreshold = true;
    
    //yin setup
    yin.yinPrepare(sampleRate, samplesPerBlock);
}

void HarmonizerAudioProcessor::releaseResources()
{
}

void HarmonizerAudioProcessor::processBlock (AudioSampleBuffer& buffer, MidiBuffer& midiMessages)
{
    const ScopedLock sl (lock);

    ScopedNoDenormals noDenormals;

    //midiProcessor.processMidi(midiMessages);


    //initializations
    const int numInputChannels = getTotalNumInputChannels();
    const int numOutputChannels = getTotalNumOutputChannels();
    const int numSamples = buffer.getNumSamples();
    const int sampleRate = getSampleRate();

    //initial buffer write and read positions for FFTs
    int currentInputBufferWritePosition;
    int currentOutputBufferWritePosition;
    int currentOutputBufferReadPosition;
    int currentSamplesSinceLastFFT;

    //YIN f_0 tracking
    float frequency = yin.yinPitch(buffer.getReadPointer(0),sampleRate);
    int midiVoice = yin.yinMidi(frequency);

    //Midi
    midi.processMidi(midiMessages, numSamples);
    float targetFrequency = midi.frequency;
    int midiPlayed = midi.midiNumber;
    //DBG("targetFrequency: "<<targetFrequency<<"| tracked frequency: "<<frequency);
 
    //calculate shift using pitch fore finer grain control (suffers from the pitch tracker's volatility and still requires quantization)
    //float shiftCurrent = targetFrequency / frequency;

    //calc shift using midi for a quick and dirty quantization to the 12 tone western scale
    float delta = midiPlayed - midiVoice;
    float shiftCurrent = pow(2.0f, (delta) / 12.0f);

    //very primitive safeguard for shifting
    if (abs(shiftCurrent - shift) < 12) shift = shiftCurrent;

    //shift using ui (ui element is all commented out)
    //feature to enable --> choose from fixed interval pitch shift (default) or automatic (with pitch track and midi))
    //float shift = paramShift.getNextValue();

    //Handle threshold paramter smoothing
    float newThreshold = paramThreshold.getNextValue();
    if (paramThreshold.isSmoothing())
        needToUpdateThreshold = true;
    if (newThreshold == paramThreshold.getTargetValue() && needToUpdateThreshold)
    {
        yin.yinUpdateThreshold(newThreshold);
        needToUpdateThreshold = false;
    }

    if (midiPlayedCurrent != midiPlayed || midiVoiceCurrent != midiVoice)
    {
        midiPlayedCurrent = midiPlayed;
        midiVoiceCurrent = midiVoice;
        needToResetPhases = true;
    }

    float ratio = roundf (shift * (float)hopSize) / (float)hopSize;
    int resampledLength = floorf ((float)fftSize / ratio);

    DBG("midiPlayed: " << midiPlayed << "| midiVoice: " << midiVoice << "| tracked frequency: " << frequency <<"| Shift: " << shift);

    HeapBlock<float> resampledOutput (resampledLength, true);
    HeapBlock<float> synthesisWindow (resampledLength, true);
    updateWindow (synthesisWindow, resampledLength);

    //sample processing loop
    for (int channel = 0; channel < numInputChannels; ++channel) {
        float* channelData = buffer.getWritePointer (channel);

        //init current buffer positions
        currentInputBufferWritePosition = inputBufferWritePosition;
        currentOutputBufferWritePosition = outputBufferWritePosition;
        currentOutputBufferReadPosition = outputBufferReadPosition;
        currentSamplesSinceLastFFT = samplesSinceLastFFT;

        for (int sample = 0; sample < numSamples; ++sample) {
            //get input
            const float in = channelData[sample];
            //store output
            channelData[sample] = outputBuffer.getSample (channel, currentOutputBufferReadPosition);

            //synthesis stage
            // 
            //zero the output and store the input in buffer. reset read positions if needed
            outputBuffer.setSample (channel, currentOutputBufferReadPosition, 0.0f);
            if (++currentOutputBufferReadPosition >= outputBufferLength)
                currentOutputBufferReadPosition = 0;

            inputBuffer.setSample (channel, currentInputBufferWritePosition, in);
            if (++currentInputBufferWritePosition >= inputBufferLength)
                currentInputBufferWritePosition = 0;

            //check if enough samples have come in according to hopsize
            if (++currentSamplesSinceLastFFT >= hopSize) {
                currentSamplesSinceLastFFT = 0;

                //apply window on input and store in fft time domain buffer (imag is 0.0 since real signal)
                int inputBufferIndex = currentInputBufferWritePosition;
                for (int index = 0; index < fftSize; ++index) {
                    fftTimeDomain[index].real (sqrtf (fftWindow[index]) * inputBuffer.getSample (channel, inputBufferIndex));
                    fftTimeDomain[index].imag (0.0f);

                    if (++inputBufferIndex >= inputBufferLength)
                        inputBufferIndex = 0;
                }
            
                //perform fft on the time domain buffer and store in freq domain buffer
                fft->perform (fftTimeDomain, fftFrequencyDomain, false);

                //phase reset when changing the parameter due to smoothing
                //if (paramShift.isSmoothing())
                //    needToResetPhases = true;
                //if (shift == paramShift.getTargetValue() && needToResetPhases) {
                //    inputPhase.clear();
                //    outputPhase.clear();
                //    needToResetPhases = false;
                //}

                if (needToResetPhases)
                {
                    inputPhase.clear();
                    outputPhase.clear();
                    needToResetPhases = false;
                }

                //modification stage
                // 
                //modify stft and apply effect
                for (int index = 0; index < fftSize; ++index) {

                    //initialize magnitude and phase
                    float magnitude = abs (fftFrequencyDomain[index]);
                    float phase = arg (fftFrequencyDomain[index]);

                    //calculate needed phase shift according to deltaPhi and ratio
                    float phaseDeviation = phase - inputPhase.getSample (channel, index) - omega[index] * (float)hopSize;
                    float deltaPhi = omega[index] * hopSize + princArg (phaseDeviation);
                    float newPhase = princArg (outputPhase.getSample (channel, index) + deltaPhi * ratio);

                    //store phases in buffers to keep track of phase
                    inputPhase.setSample (channel, index, phase);
                    outputPhase.setSample (channel, index, newPhase);
                    
                    //store
                    fftFrequencyDomain[index] = std::polar (magnitude, newPhase);
                }

                //synthesis stage
                //
                //inverse fft from freq domain buffer to time domain buffer
                fft->perform (fftFrequencyDomain, fftTimeDomain, true);

                for (int index = 0; index < resampledLength; ++index) {
                    //reconstruct signal
                    float x = (float)index * (float)fftSize / (float)resampledLength;
                    int ix = (int)floorf (x);
                    float dx = x - (float)ix;

                    float sample1 = fftTimeDomain[ix].real();
                    float sample2 = fftTimeDomain[(ix + 1) % fftSize].real();
                    resampledOutput[index] = sample1 + dx * (sample2 - sample1);
                    resampledOutput[index] *= sqrtf (synthesisWindow[index]);
                }

                //store resampled ouput signal in system output buffer and scale according to ratio
                int outputBufferIndex = currentOutputBufferWritePosition;
                for (int index = 0; index < resampledLength; ++index) {
                    float out = outputBuffer.getSample (channel, outputBufferIndex);
                    out += resampledOutput[index] * windowScaleFactor;
                    outputBuffer.setSample (channel, outputBufferIndex, out);

                    if (++outputBufferIndex >= outputBufferLength)
                        outputBufferIndex = 0;
                }

                //move write buffer by hop increments
                currentOutputBufferWritePosition += hopSize;
                if (currentOutputBufferWritePosition >= outputBufferLength)
                    currentOutputBufferWritePosition = 0;
            }
        }
    }

    //set buffer position values
    inputBufferWritePosition = currentInputBufferWritePosition;
    outputBufferWritePosition = currentOutputBufferWritePosition;
    outputBufferReadPosition = currentOutputBufferReadPosition;
    samplesSinceLastFFT = currentSamplesSinceLastFFT;

    //sanity clear extra channel data if needed
    for (int channel = numInputChannels; channel < numOutputChannels; ++channel)
        buffer.clear (channel, 0, numSamples);
}

//==============================================================================


//update fft size depending on params
void HarmonizerAudioProcessor::updateFftSize()
{
    //get fft size from params
    fftSize = (int)paramFftSize.getTargetValue();
    //init fft instance
    fft = std::make_unique<dsp::FFT>(log2 (fftSize));

    //init the buffer and its params and update size
    inputBufferLength = fftSize;
    inputBufferWritePosition = 0;
    inputBuffer.clear();
    inputBuffer.setSize (getTotalNumInputChannels(), inputBufferLength);

    //Same for output buffer and its params
    float maxRatio = powf (2.0f, -12.0f / 12.0f);
    outputBufferLength = (int)floorf ((float)fftSize / maxRatio);
    outputBufferWritePosition = 0;
    outputBufferReadPosition = 0;
    outputBuffer.clear();
    outputBuffer.setSize (getTotalNumInputChannels(), outputBufferLength);

    //reallocate values for the windows and analysis and synthesis buffers (since from heap then you need to use realloc)
    fftWindow.realloc (fftSize);
    fftWindow.clear (fftSize);

    fftTimeDomain.realloc (fftSize);
    fftTimeDomain.clear (fftSize);

    fftFrequencyDomain.realloc (fftSize);
    fftFrequencyDomain.clear (fftSize);

    //reset samples since last fft
    samplesSinceLastFFT = 0;


    omega.realloc (fftSize);
    for (int index = 0; index < fftSize; ++index) omega[index] = 2.0f * M_PI * index / (float)fftSize;

    inputPhase.clear();
    inputPhase.setSize (getTotalNumInputChannels(), outputBufferLength);

    outputPhase.clear();
    outputPhase.setSize (getTotalNumInputChannels(), outputBufferLength);
}

//update hop size according to params
void HarmonizerAudioProcessor::updateHopSize()
{
    overlap = (int)paramHopSize.getTargetValue();
    if (overlap != 0) {
        hopSize = fftSize / overlap;
        outputBufferWritePosition = hopSize % outputBufferLength;
    }
}

//call update on analysis window
void HarmonizerAudioProcessor::updateAnalysisWindow()
{
    updateWindow (fftWindow, fftSize);
}

//update window according to chosen param
void HarmonizerAudioProcessor::updateWindow (const HeapBlock<float>& window, const int windowLength)
{
    switch ((int)paramWindowType.getTargetValue()) {
        case windowTypeBartlett: {
            for (int sample = 0; sample < windowLength; ++sample)
                window[sample] = 1.0f - fabs (2.0f * (float)sample / (float)(windowLength - 1) - 1.0f);
            break;
        }
        case windowTypeHann: {
            for (int sample = 0; sample < windowLength; ++sample)
                window[sample] = 0.5f - 0.5f * cosf (2.0f * M_PI * (float)sample / (float)(windowLength - 1));
            break;
        }
        case windowTypeHamming: {
            for (int sample = 0; sample < windowLength; ++sample)
                window[sample] = 0.54f - 0.46f * cosf (2.0f * M_PI * (float)sample / (float)(windowLength - 1));
            break;
        }
    }
}

//updating window scale factor depending on overlap and fftSize
void HarmonizerAudioProcessor::updateWindowScaleFactor()
{
    float windowSum = 0.0f;
    for (int sample = 0; sample < fftSize; ++sample) windowSum += fftWindow[sample];

    windowScaleFactor = 0.0f;
    if (overlap != 0 && windowSum != 0.0f)
        windowScaleFactor = 1.0f / (float)overlap / windowSum * (float)fftSize;
}

//phase wrapping
float HarmonizerAudioProcessor::princArg (const float phase)
{
    if (phase >= 0.0f) return fmod (phase + M_PI,  2.0f * M_PI) - M_PI;
    else return fmod (phase + M_PI, -2.0f * M_PI) + M_PI;
}

void HarmonizerAudioProcessor::getStateInformation (MemoryBlock& destData)
{
    auto state = parameters.apvts.copyState();
    std::unique_ptr<XmlElement> xml (state.createXml());
    copyXmlToBinary (*xml, destData);
}

void HarmonizerAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    std::unique_ptr<XmlElement> xmlState (getXmlFromBinary (data, sizeInBytes));

    if (xmlState.get() != nullptr)
        if (xmlState->hasTagName (parameters.apvts.state.getType()))
            parameters.apvts.replaceState (ValueTree::fromXml (*xmlState));
}

AudioProcessorEditor* HarmonizerAudioProcessor::createEditor()
{
    return new PitchShiftAudioProcessorEditor (*this);
}

bool HarmonizerAudioProcessor::hasEditor() const
{
    return true; // (change this to false if you choose to not supply an editor)
}
#ifndef JucePlugin_PreferredChannelConfigurations
bool HarmonizerAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
  #if JucePlugin_IsMidiEffect
    ignoreUnused (layouts);
    return true;
  #else
    // This is the place where you check if the layout is supported.
    // In this template code we only support mono or stereo.
    if (layouts.getMainOutputChannelSet() != AudioChannelSet::mono()
     && layouts.getMainOutputChannelSet() != AudioChannelSet::stereo())
        return false;

    // This checks if the input layout matches the output layout
   #if ! JucePlugin_IsSynth
    if (layouts.getMainOutputChannelSet() != layouts.getMainInputChannelSet())
        return false;
   #endif

    return true;
  #endif
}
#endif

const String HarmonizerAudioProcessor::getName() const
{
    return JucePlugin_Name;
}

bool HarmonizerAudioProcessor::acceptsMidi() const
{
   #if JucePlugin_WantsMidiInput
    return true;
   #else
    return false;
   #endif
}

bool HarmonizerAudioProcessor::producesMidi() const
{
   #if JucePlugin_ProducesMidiOutput
    return true;
   #else
    return false;
   #endif
}

bool HarmonizerAudioProcessor::isMidiEffect() const
{
   #if JucePlugin_IsMidiEffect
    return true;
   #else
    return false;
   #endif
}

double HarmonizerAudioProcessor::getTailLengthSeconds() const
{
    return 0.0;
}

int HarmonizerAudioProcessor::getNumPrograms()
{
    return 1;   
}

int HarmonizerAudioProcessor::getCurrentProgram()
{
    return 0;
}

void HarmonizerAudioProcessor::setCurrentProgram (int index)
{
}

const String HarmonizerAudioProcessor::getProgramName (int index)
{
    return {};
}

void HarmonizerAudioProcessor::changeProgramName (int index, const String& newName)
{
}

// This creates new instances of the plugin..
AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new HarmonizerAudioProcessor();
}
