/*
  ==============================================================================

    Yin.h
    Author:  Sami S
    
    This has been extracted from Adamski's implementation of YIN [1] and fitted to our application
    https://github.com/adamski/pitch_detector/blob/time-based/source/modules/PitchYIN.h

    [1]: Cheveigné, A., & Kawahara, H. (2002). Yin, a fundamental frequency estimator for speech and music. 
            The Journal of the Acoustical Society of America. https://doi.org/10.1121/1.1458024 

  ==============================================================================
*/
#pragma once

#include <JuceHeader.h>

class YIN 
{
public:
    YIN() = default;
    ~YIN() = default;

    void yinPrepare(int sampleRate, int size) 
    {
        bufferSize = size;
        yin.setSize(1, size);
        yin.clear();
        isPrepared = true;
        //DBG("sampleRate: " << sampleRate << "| size: " << yin.getNumSamples());
    }

    float yinPitch(const float* inputData, double sampleRate)
    {
        float pitch = 0.0f;
        pitch = calculatePitch(inputData);
        //DBG("DF MIN: " << pitch);

        if (pitch > 0)
        {
            pitch = sampleRate / (pitch + 0.0);
        }
        else
        {
            pitch = 0.0f;
        }
        return pitch;
    }

    int yinMidi(float pitch) {
        int midiPitch = 0;
        if (pitch != 0)
        {
            midiPitch = round(12 * log2((pitch / 440.0f)) + 69);
        }
        else midiPitch = -1;
        return midiPitch;
    }


    float calculatePitch(const float* inputData)
    {
        //read only mono data from channel 0
        float* yinData = yin.getWritePointer(0);
        float difference = 0.0f;
        float sum = 0.0f;

        for (int tau = 1; tau < bufferSize; tau++)
        {
            //difference
            yinData[0] = 1.0f;
            for (int i = 0; i < bufferSize; i++)
            {
                difference += inputData[i] - inputData[i + tau];
                yinData[tau] += (difference * difference);
            }

            //normalize
            if (sum != 0)
            {
                sum += yinData[tau];
            }
            else
            {
                sum = 1.0;
            }
            yinData[tau] = yinData[tau] * tau / (sum);

            int period = tau - 3;

            //if (yinData[period] < threshold) DBG("threshold reached");

            if (tau > 4 && (yinData[period] < threshold) &&
                (yinData[period] < yinData[period + 1]))
            {
                //DBG("return early");
                return quadraticPeakPosition(yin.getReadPointer(0), period);
            }
        }
        return -1.0f;
    }

    //get the peak of the quadratic interpolation
    float quadraticPeakPosition(const float* data, int pos) 
    {
        float s0, s1, s2, xmax;
        unsigned int x0, x2;
        //in case the position is 0 to avoid errors
        if (pos == 0 || pos == bufferSize - 1) return pos;

        //set abscissa of the previous and next points
        x0 = (pos < 1) ? pos : pos - 1;
        x2 = (pos + 1 < bufferSize) ? pos + 1 : pos;

        //return if special cases
        if (x0 == pos) return (data[pos] <= data[x2]) ? pos : x2;
        if (x2 == pos) return (data[pos] <= data[x0]) ? pos : x0;

        //the y values
        s0 = data[x0];
        s1 = data[pos];
        s2 = data[x2];

        //abscissa of the max value
        xmax = pos + 0.5 * (s0 - s2) / (s0 - 2. * s1 + s2);
        return xmax;
    }

    //extract the minimum element from the data buffer (Adamski)
    unsigned int minElement(const float* data) noexcept
    {
        unsigned int j, pos = 0;
        float tmp = data[0];
        for (j = 0; j < bufferSize; j++)
        {
            pos = (tmp < data[j]) ? pos : j;
            tmp = (tmp < data[j]) ? tmp : data[j];
        }
        return pos;
    }

    //function to be called by the parameters...
    void yinUpdateThreshold(float newThreshold)
    {
        threshold = newThreshold;
        yin.clear();
    }

  
    bool isPrepared = false;
    int bufferSize = 1024;
    juce::AudioSampleBuffer yin;
    float threshold = 0.15f;

private:  
};
