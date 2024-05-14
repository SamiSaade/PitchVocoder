/*
  ==============================================================================

	Yin.h
	Author:  Sami S

	MidiProcessor just reads midi and outputs it in hertz or note number

  ==============================================================================
*/
#pragma once

#include "JuceHeader.h"

class MidiProcessor
{
public:

	void processMidi(MidiBuffer& midiMessages,const int numSamples)
	{
		//instantiate midi iterator and message
		MidiBuffer::Iterator it(midiMessages);
		MidiMessage currentMessage;
		int samplePosition;

		//in case we want to use on screen keyboard
		keyboardState.processNextMidiBuffer(midiMessages, 0, numSamples, true);

		//iterate through midi events
		while (it.getNextEvent(currentMessage, samplePosition))
		{
			if (currentMessage.isNoteOn())
			{
				//if note on get it and store it in case we want to operate through midi
				auto noteNumber = currentMessage.getNoteNumber();
				this->midiNumber = noteNumber;

				//convert to hertz in case we want to work through frequency
				this->frequency = currentMessage.getMidiNoteInHertz(noteNumber);

				DBG("Note On: " << noteNumber << " " << frequency << " Hz");

			}
		}
	}

	int midiNumber;
	float frequency;
	MidiKeyboardState keyboardState;
};
