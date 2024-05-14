#pragma once
#define _USE_MATH_DEFINES
#include <cmath>
#include "../JuceLibraryCode/JuceHeader.h"
#include "PluginParameter.h"
#include "MidiProcessor.h"
#include "Yin.h"

class HarmonizerAudioProcessor : public AudioProcessor
{
public:

    HarmonizerAudioProcessor();
    ~HarmonizerAudioProcessor();

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    void processBlock (AudioSampleBuffer&, MidiBuffer&) override;
    void getStateInformation (MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;

   #ifndef JucePlugin_PreferredChannelConfigurations
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
   #endif

    const String getName() const override;

    bool acceptsMidi() const override;
    bool producesMidi() const override;
    bool isMidiEffect () const override;
    double getTailLengthSeconds() const override;

    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram (int index) override;
    const String getProgramName (int index) override;
    void changeProgramName (int index, const String& newName) override;

    //==============================================================================
    /*class Harmonizer : public YIN {};*/

    //==============================================================================
    //Param UI shenanigans
    StringArray fftSizeItemsUI = {
        "32",
        "64",
        "128",
        "256",
        "512",
        "1024",
        "2048",
        "4096",
        "8192"
    };

    enum fftSizeIndex {
        fftSize32 = 0,
        fftSize64,
        fftSize128,
        fftSize256,
        fftSize512,
        fftSize1024,
        fftSize2048,
        fftSize4096,
        fftSize8192,
    };

    StringArray hopSizeItemsUI = {
        "1/2 Window",
        "1/4 Window",
        "1/8 Window",
    };

    enum hopSizeIndex {
        hopSize2 = 0,
        hopSize4,
        hopSize8,
    };

    StringArray windowTypeItemsUI = {
        "Bartlett",
        "Hann",
        "Hamming",
    };

    enum windowTypeIndex {
        windowTypeBartlett = 0,
        windowTypeHann,
        windowTypeHamming,
    };

    //helper functions
    void updateFftSize();
    void updateHopSize();
    void updateAnalysisWindow();
    void updateWindow (const HeapBlock<float>& window, const int windowLength);
    void updateWindowScaleFactor();

    float princArg (const float phase);

    //======================================
    //fft buffers and varibales
    CriticalSection lock;

    int fftSize;
    std::unique_ptr<dsp::FFT> fft;

    int inputBufferLength;
    int inputBufferWritePosition;
    AudioSampleBuffer inputBuffer;

    int outputBufferLength;
    int outputBufferWritePosition;
    int outputBufferReadPosition;
    AudioSampleBuffer outputBuffer;

    HeapBlock<float> fftWindow;
    HeapBlock<dsp::Complex<float>> fftTimeDomain;
    HeapBlock<dsp::Complex<float>> fftFrequencyDomain;

    int samplesSinceLastFFT;

    int overlap;
    int hopSize;
    float windowScaleFactor;

    //======================================
    //Phase buffers and variables
    HeapBlock<float> omega;
    AudioSampleBuffer inputPhase;
    AudioSampleBuffer outputPhase;
    bool needToResetPhases;
    bool needToUpdateThreshold;

    //======================================
    //Params
    PluginParametersManager parameters;

    //PluginParameterLinSlider paramShift;
    PluginParameterLinSlider paramThreshold;
    PluginParameterComboBox paramFftSize;
    PluginParameterComboBox paramHopSize;
    PluginParameterComboBox paramWindowType;

    //======================================
    YIN yin;
    MidiProcessor midi;
    MidiKeyboardState keyboardState;

private:
    int midiVoiceCurrent = 69;
    int midiPlayedCurrent = 69;
    float shift = 0.0f;
    



    //==============================================================================

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (HarmonizerAudioProcessor)
};
