/*
    ,--.                     ,--.     ,--.  ,--.
  ,-'  '-.,--.--.,--,--.,---.|  |,-.,-'  '-.`--' ,---. ,--,--,      Copyright 2018
  '-.  .-'|  .--' ,-.  | .--'|     /'-.  .-',--.| .-. ||      \   Tracktion Software
    |  |  |  |  \ '-'  \ `--.|  \  \  |  |  |  |' '-' '|  ||  |       Corporation
    `---' `--'   `--`--'`---'`--'`--' `---' `--' `---' `--''--'    www.tracktion.com
*/


namespace tracktion { inline namespace engine
{

//==============================================================================
namespace Distortion
{
    // Apply saturation distortion algorithm - creates warm distortion by subtracting scaled square law term
    // Used by distortion() to process individual samples with soft clipping behavior
    inline float saturate (float input, float drive, float lowclip, float highclip)
    {
        input = juce::jlimit (lowclip, highclip, input);
        return input - drive * input * std::fabs (input);
    }

    // Process entire audio buffer through distortion effect with automatic gain compensation
    // Called from FourOscPlugin::applyEffects() when distortion is enabled, applies tube-like saturation
    inline void distortion (float* data, int count, float drive, float lowclip, float highclip)
    {
        if (drive <= 0.f)
            return;

        float gain = drive < 0.5f ? 2.0f * drive + 1.0f : drive * 4.0f;

        while (--count >= 0)
        {
            *data = saturate (*data, drive, lowclip, highclip) * gain;
            data++;
        }
    }
}

// Hard clip audio samples to prevent digital overflow, used between 12dB/24dB filter stages
// Called from FourOscVoice::renderNextBlock() to prevent filter instability with high resonance
inline void clip (float* data, int numSamples)
{
    while (--numSamples >= 0)
    {
        *data = juce::jlimit (-1.0f, 1.0f, *data);
        data++;
    }
}

//==============================================================================
class FODelayLine
{
public:
    // Initialize circular delay buffer for echo/delay effects with specified maximum delay time
    // Used by FODelay and FOChorus classes for their internal delay line processing
    FODelayLine (float maximumDelay = 0.001f, float sr = 44100.0f)
    {
        resize (maximumDelay, sr);
    }

    // Dynamically resize internal delay buffer when sample rate changes or max delay time is updated
    // Called during plugin initialization and when host changes sample rate to maintain timing accuracy
    void resize (float maximumDelay, float sr)
    {
        sampleRate = sr;
        numSamples = (int) std::ceil (maximumDelay * sampleRate);
        sampleBuffer.resize ((size_t) numSamples);
        const auto num = (size_t) numSamples; // Workaround for a GCC warning
        memset (sampleBuffer.data(), 0, sizeof(float) * num);
        currentPos = 0;
    }

    // Clear all stored samples to silence, removing any delay tail or feedback artifacts
    // Called when plugin is reset, transport stops, or user wants to clear delay buffer instantly
    void reset()
    {
        const auto num = (size_t) numSamples; // Workaround for a GCC warning
        memset (sampleBuffer.data(), 0, sizeof(float) * num);
    }

    // Utility function to convert sample count to time duration for delay calculations
    // Used internally by read() method to validate delay time bounds and ensure proper timing
    inline float samplesToSeconds (float numSamplesIn, float sampleRateIn)
    {
        return numSamplesIn / sampleRateIn;
    }

    // Read sample from delay line using linear interpolation for smooth delay time modulation
    // Core delay function called by FODelay and FOChorus for each output sample, supports fractional delays
    inline float read (float atTime)
    {
        jassert (atTime >= 0.0f && atTime < samplesToSeconds (float (numSamples), sampleRate));

        float pos = std::max (1.0f, atTime * (sampleRate - 1));

        int intPos = (int) std::floor (pos);
        float f = pos - intPos;

        int n1 = currentPos - intPos;
        while (n1 < 0)
            n1 += numSamples;
        while (n1 >= numSamples)
            n1 -= numSamples;

        int n2 = n1 - 1;
        if (n2 < 0)
            n2 += numSamples;

        jassert (n1 >= 0 && n1 < numSamples);
        jassert (n2 >= 0 && n2 < numSamples);

        return (1.0f - f) * sampleBuffer[(size_t) n1] + f * sampleBuffer[(size_t) n2];
    }

    // Write new sample to current position and advance write pointer in circular buffer
    // Called for every input sample to maintain continuous delay line operation
    inline void write (const float input)
    {
        sampleBuffer[(size_t) currentPos] = input;
        currentPos++;

        if (currentPos >= numSamples)
            currentPos = 0;
    }

protected:
    int numSamples {0};
    float sampleRate {44100};
    int currentPos {0};
    std::vector<float> sampleBuffer;
};

//==============================================================================
class FODelay
{
public:
    // Main delay processing loop - applies echo with feedback, crossfeed, and wet/dry mixing
    // Called from FourOscPlugin::applyEffects() when delay is enabled, creates stereo delay with ping-pong
    void process (juce::AudioBuffer<float>& buffer, int numSamples)
    {
        float* lOut = buffer.getWritePointer (0);
        float* rOut = buffer.getWritePointer (1);

        AudioScratchBuffer scratchBuffer (buffer);

        float* lWork = scratchBuffer.buffer.getWritePointer (0);
        float* rWork = scratchBuffer.buffer.getWritePointer (1);

        juce::FloatVectorOperations::copy (lWork, lOut, numSamples);
        juce::FloatVectorOperations::copy (rWork, rOut, numSamples);

        for (int i = 0; i < numSamples; i++)
        {
            const float lVal = leftDelay.read  (delay);
            const float rVal = rightDelay.read (delay);

            leftDelay.write  (lWork[i] + (feedback * lVal) + (crossfeed * rVal));
            rightDelay.write (rWork[i] + (feedback * rVal) + (crossfeed * lVal));

            lWork[i] = lVal;
            rWork[i] = rVal;
        }

        // Wet/Dry Mix
        for (int i = 0; i < numSamples; i++)
        {
            AudioFadeCurve::CrossfadeLevels wetDry (mix);

            lOut[i] = (wetDry.gain2 * lOut[i]) + (wetDry.gain1 * lWork[i]);
            rOut[i] = (wetDry.gain2 * rOut[i]) + (wetDry.gain1 * rWork[i]);
        }
    }

    // Update sample rate and resize internal delay lines to maintain maximum 5.1 second delay time
    // Called during plugin initialization to ensure delay timing remains accurate at any sample rate
    void setSampleRate (double sr)
    {
        leftDelay.resize (5.1f, float (sr));
        rightDelay.resize (5.1f, float (sr));
    }

    // Update all delay parameters from plugin controls - time (beats), feedback level, stereo crossfeed, and mix
    // Called from FourOscPlugin::updateParams() to apply user parameter changes during audio processing
    void setParams (float delayIn, float feedbackIn, float crossfeedIn, float mixIn)
    {
        delay = delayIn;
        feedback  = std::min (0.99f, feedbackIn);
        crossfeed = std::min (0.99f, crossfeedIn);
        mix = mixIn;
    }

    // Clear both left and right delay lines to remove all echo tails and feedback artifacts
    // Called when plugin is reset or initialized to ensure clean delay state
    void reset()
    {
        leftDelay.reset();
        rightDelay.reset();
    }

private:
    FODelayLine leftDelay, rightDelay;

    float mix = 0, feedback = 0, delay = 0, crossfeed = 0;
};

//==============================================================================
class FOChorus
{
public:
    // Apply chorus effect using LFO-modulated delay lines to create detuning and stereo width
    // Called from FourOscPlugin::applyEffects() when chorus is enabled, creates classic chorus sound
    void process (juce::AudioBuffer<float>& buffer, int numSamples)
    {
        float ph = 0.0f;
        int bufPos = 0;

        const float delayMs = 20.0f;
        const float minSweepSamples = (float) ((delayMs * sampleRate) / 1000.0);
        const float maxSweepSamples = (float) (((delayMs + depthMs) * sampleRate) / 1000.0);
        const float speed = (float)((juce::MathConstants<double>::pi * 2.0) / (sampleRate / speedHz));
        const int maxLengthMs = 1 + juce::roundToInt (delayMs + depthMs);
        const int lengthInSamples = juce::roundToInt ((maxLengthMs * sampleRate) / 1000.0);

        delayBuffer.ensureMaxBufferSize (lengthInSamples);

        const float lfoFactor = 0.5f * (maxSweepSamples - minSweepSamples);
        const float lfoOffset = minSweepSamples + lfoFactor;

        AudioFadeCurve::CrossfadeLevels wetDry (mix);

        for (int chan = buffer.getNumChannels(); --chan >= 0;)
        {
            float* const d = buffer.getWritePointer (chan, 0);
            float* const buf = (float*) delayBuffer.buffers[chan].getData();

            ph = phase;
            if (chan > 0)
                ph += juce::MathConstants<float>::pi * width;

            bufPos = delayBuffer.bufferPos;

            for (int i = 0; i < numSamples; ++i)
            {
                const float in = d[i];

                const float sweep = lfoOffset + lfoFactor * sinf (ph);
                ph += speed;

                int intSweepPos = juce::roundToInt (sweep);
                const float interp = sweep - intSweepPos;
                intSweepPos = bufPos + lengthInSamples - intSweepPos;

                const float out = buf[(intSweepPos - 1) % lengthInSamples] * interp + buf[intSweepPos % lengthInSamples] * (1.0f - interp);

                float n = in;

                JUCE_UNDENORMALISE (n);

                buf[bufPos] = n;
                d[i] = out * wetDry.gain1 + in * wetDry.gain2;
                bufPos = (bufPos + 1) % lengthInSamples;
            }
        }

        jassert (! hasFloatingPointDenormaliseOccurred());
        zeroDenormalisedValuesIfNeeded (buffer);

        phase = ph;
        if (phase >= juce::MathConstants<float>::pi * 2)
            phase -= juce::MathConstants<float>::pi * 2;

        delayBuffer.bufferPos = bufPos;
    }

    // Update sample rate and calculate buffer size for chorus delay based on max depth + base delay
    // Called during plugin initialization to ensure chorus timing and modulation remain accurate
    void setSampleRate (double sr)
    {
        sampleRate = sr;

        const float delayMs = 20.0f;
        auto maxLengthMs = 1 + juce::roundToInt (delayMs + depthMs);
        auto bufferSizeSamples = juce::roundToInt ((maxLengthMs * sr) / 1000.0);
        delayBuffer.ensureMaxBufferSize (bufferSizeSamples);
        delayBuffer.clearBuffer();
        phase = 0.0f;
    }

    // Update chorus parameters from plugin controls - LFO rate (Hz), modulation depth (ms), stereo width, and wet/dry mix
    // Called from FourOscPlugin::updateParams() to apply real-time parameter changes during processing
    void setParams (float speedIn, float depthIn, float widthIn, float mixIn)
    {
        speedHz = speedIn;
        depthMs = depthIn;
        width = widthIn;
        mix = mixIn;
    }

    // Clear internal delay buffer to remove any chorus artifacts and reset LFO phase to zero
    // Called when plugin is reset or transport stops to ensure clean chorus state
    void reset()
    {
        delayBuffer.clearBuffer();
    }

private:
    DelayBufferBase delayBuffer;            // Multichannel delay buffer for chorus modulation, handles stereo processing
    double sampleRate = 0;                  // Sample rate for timing calculations and LFO frequency conversion

    float phase = 0;                        // Current LFO phase for chorus modulation, wraps at 2π
    float speedHz = 1.0f;                   // LFO frequency in Hz for chorus rate control
    float depthMs = 3.0f;                   // Modulation depth in milliseconds for chorus intensity
    float width = 0.5f;                     // Stereo width control, adds phase offset between channels
    float mix = 0;                          // Wet/dry mix for chorus effect blending
};

//==============================================================================
class FourOscVoice : public juce::MPESynthesiserVoice
{
public:
    // Initialize voice instance with reference to parent synth plugin for parameter access
    // Each voice handles one note in polyphonic playback, created by MPESynthesiser voice management
    FourOscVoice (FourOscPlugin& s) : synth (s)
    {
        for (auto p : synth.getAutomatableParameters())
            smoothers[p] = {};
    }

    // Handle MIDI note-on events - start envelopes, reset oscillators, and initialize voice for new note
    // Called by JUCE MPESynthesiser when MIDI note is triggered, supports legato mode for smooth transitions
    void noteStarted() override
    {
        if (isPlaying)
        {
            if (synth.isLegato())
            {
                activeNote.setTargetValue (currentlyPlayingNote.initialNote);

                ampAdsr.noteOn();
                filterAdsr.noteOn();
                modAdsr1.noteOn();  // say, this is pitch ADSR
                modAdsr2.noteOn();
            }
            else  // note was playing, but not legato, so just retrigger the same note: isQuickStop + retrigger
            {
                noteStopped (true);  //  ampAdsr.noteOff(); filterAdsr.noteOff(); modAdsr1.noteOff(); modAdsr2.noteOff();
                retrigger = true;
                isQuickStop = true;
            }
        }
        else  // start new note
        {
            activeNote.setCurrentAndTargetValue (currentlyPlayingNote.initialNote);

            isPlaying = true;
            isQuickStop = false;
            retrigger = false;

            ampAdsr.reset();
            filterAdsr.reset();
            modAdsr1.reset();
            modAdsr2.reset();
            lfo1.reset();
            lfo2.reset();

            juce::ScopedValueSetter<bool> svs (snapAllValues, true);
            updateParams (0);  // Update mod values, but numSamples = 0, so do not advance mods

            ampAdsr.noteOn();
            filterAdsr.noteOn();
            modAdsr1.noteOn();
            modAdsr2.noteOn();
            lfo1.reset();
            lfo2.reset();

            filterL1.reset();
            filterR1.reset();
            filterL2.reset();
            filterR2.reset();

            for (auto& o : oscillators)
                o.start();

            filterFrequencySmoother.snapToValue();

            firstBlock = true;
        }
    }

    // Handle MIDI note-off events - trigger envelope release phase or immediate voice cutoff
    // Called by JUCE MPESynthesiser when MIDI note is released, allowTailOff enables natural envelope decay
    void noteStopped (bool allowTailOff) override
    {
        if (allowTailOff)
        {
            ampAdsr.noteOff();
            filterAdsr.noteOff();
            modAdsr1.noteOff();
            modAdsr2.noteOff();
        }
        else
        {
            ampAdsr.reset();
            filterAdsr.reset();
            modAdsr1.reset();
            modAdsr2.reset();
            clearCurrentNote();
            isPlaying = false;
            isQuickStop = false;
        }
    }

    // Update sample rate for all voice components including oscillators, envelopes, LFOs, and filters
    // Called by JUCE framework when host sample rate changes, ensures proper timing for all DSP components
    void setCurrentSampleRate (double newRate) override
    {
        if (newRate > 0)
        {
            MPESynthesiserVoice::setCurrentSampleRate (newRate);

            for (auto& o : oscillators)
                o.setSampleRate (newRate);

            ampAdsr.setSampleRate (newRate);
            filterAdsr.setSampleRate (newRate);
            modAdsr1.setSampleRate (newRate);
            modAdsr2.setSampleRate (newRate);
            lfo1.setSampleRate (newRate);
            lfo2.setSampleRate (newRate);

            lastLegato = paramValue (synth.legato);
            activeNote.reset (newRate, paramValue (synth.legato) / 1000.0f);
            filterFrequencySmoother.reset (newRate, 0.05f);

            for (auto& itr : smoothers)
                itr.second.reset (newRate, 0.01f);
        }
    }

    // Convert MIDI velocity (0-1) to exponential gain curve with adjustable sensitivity
    // Used in renderNextBlock() to apply velocity-sensitive amplitude scaling to voice output
    float velocityToGain (float velocity, float velocitySensitivity = 1.0f)
    {
        float v = velocity * velocitySensitivity + 1.0f - velocitySensitivity;
        return v * std::pow (25.0f, v) * 0.04f;
    }

    using MPESynthesiserVoice::renderNextBlock;
    // Main voice rendering method - generates audio from 4 oscillators, applies filters, envelopes, and adds to output
    // Called by JUCE MPESynthesiser for each active voice during audio processing, handles complete synthesis chain
    void renderNextBlock (juce::AudioBuffer<float>& outputBuffer, int startSample, int numSamples) override
    {
        juce::ScopedValueSetter<bool> svs (snapAllValues, firstBlock || snapAllValues);

        updateParams (numSamples);

        if (firstBlock)
        {
            filterFrequencySmoother.snapToValue();
            firstBlock = false;
        }

        if (numSamples > renderBuffer.getNumSamples())
            renderBuffer.setSize (2, numSamples, false, false, true);

        renderBuffer.clear();

        // Run oscillators
        for (auto& o : oscillators)
            o.process (renderBuffer, 0, numSamples);

        // Apply velocity
        float velocityGain = velocityToGain (currentlyPlayingNote.noteOnVelocity.asUnsignedFloat(), paramValue (synth.ampVelocity) / 100.0f);
        velocityGain = juce::jlimit (0.0f, 1.0f, velocityGain);
        renderBuffer.applyGain (velocityGain);

        // Apply filter
        if (synth.filterTypeValue != 0)
        {
            filterL1.processSamples (renderBuffer.getWritePointer (0), numSamples);
            filterR1.processSamples (renderBuffer.getWritePointer (1), numSamples);

            if (synth.filterSlopeValue == 24)
            {
                clip (renderBuffer.getWritePointer (0), numSamples);
                clip (renderBuffer.getWritePointer (1), numSamples);

                filterL2.processSamples (renderBuffer.getWritePointer (0), numSamples);
                filterR2.processSamples (renderBuffer.getWritePointer (1), numSamples);
            }
        }

        // Apply ADSR
        ampAdsr.applyEnvelopeToBuffer (renderBuffer, 0, numSamples);

        // Add to output
        if (outputBuffer.getNumChannels() == 1)
        {
            outputBuffer.addFrom (0, startSample, renderBuffer, 0, 0, numSamples, 0.5f);
            outputBuffer.addFrom (0, startSample, renderBuffer, 1, 0, numSamples, 0.5f);
        }
        else
        {
            outputBuffer.addFrom (0, startSample, renderBuffer, 0, 0, numSamples);
            outputBuffer.addFrom (1, startSample, renderBuffer, 1, 0, numSamples);
        }

        if (! ampAdsr.isActive())
        {
            isPlaying = false;
            if (retrigger)
            {
                noteStarted();
                retrigger = false;
                isQuickStop = false;
            }
            else
            {
                clearCurrentNote();
            }
        }

        for (auto& itr : smoothers)
            itr.second.process (numSamples);
    }

    // Apply envelope with decibel-based gain scaling to audio buffer for more musical amplitude curves
    // Used for special envelope applications where standard ADSR::applyEnvelopeToBuffer isn't suitable
    void applyEnvelopeToBuffer (juce::ADSR& adsr, juce::AudioBuffer<float>& buffer, int startSample, int numSamples)
    {
        float* l = buffer.getWritePointer (0, startSample);
        float* r = buffer.getWritePointer (1, startSample);

        while (--numSamples >= 0)
        {
            float db = adsr.getNextSample() * 100.0f - 100.0f;
            float gain = juce::Decibels::decibelsToGain (db);

            *l++ *= gain;
            *r++ *= gain;
        }
    }

    // Collect real-time modulated parameter values for GUI visualization of modulation activity
    // Called by plugin GUI to show live modulation on parameter controls, only active for playing voices
    void getLiveModulationPositions (AutomatableParameter::Ptr param, juce::Array<float>& positions)
    {
        if (isActive())
            positions.add (param->valueRange.convertTo0to1 (paramValue (param)));
    }

    // Get current filter cutoff frequency including all modulation for GUI spectrum analyzer display
    // Special case for filter frequency visualization that includes envelope, key tracking, and modulation
    void getLiveFilterFrequency (juce::Array<float>& positions)
    {
        if (isActive())
             positions.add ((12.0f * std::log2 (lastFilterFreq / 440.0f) + 69.0f) / 135.076232f);
    }

    // Update all voice parameters with modulation matrix, process envelopes and LFOs for current audio block
    // Called before rendering each audio block to apply real-time parameter changes and modulation sources
    void updateParams (int numSamples)
    {
        // Update mod values
        currentModValue[(int)FourOscPlugin::lfo1] = lfo1.getCurrentValue();
        currentModValue[(int)FourOscPlugin::lfo2] = lfo2.getCurrentValue();
        currentModValue[(int)FourOscPlugin::env1] = modAdsr1.getEnvelopeValue();
        currentModValue[(int)FourOscPlugin::env2] = modAdsr2.getEnvelopeValue();

        currentModValue[FourOscPlugin::mpePressure]   = currentlyPlayingNote.pressure.asUnsignedFloat();
        currentModValue[FourOscPlugin::mpeTimbre]     = currentlyPlayingNote.timbre.asUnsignedFloat();
        currentModValue[FourOscPlugin::midiNoteNum]   = currentlyPlayingNote.initialNote / 127.0f;
        currentModValue[FourOscPlugin::midiVelocity]  = currentlyPlayingNote.noteOnVelocity.asUnsignedFloat();

        for (int i = 0; i <= 127; i++)
            currentModValue[FourOscPlugin::ccBankSelect + i] = synth.controllerValues[i];

        // Flush the LFOs and envelopes
        lfo1.process (numSamples);
        lfo2.process (numSamples);
        for (int i = 0; i < numSamples; i++)
        {
            modAdsr1.getNextSample();
            modAdsr2.getNextSample();
        }

        // Mod
        modAdsr1.setParameters ({
            paramValue (synth.modEnvParams[0]->modAttack),
            paramValue (synth.modEnvParams[0]->modDecay),
            paramValue (synth.modEnvParams[0]->modSustain) / 100.0f,
            paramValue (synth.modEnvParams[0]->modRelease),
        });

        modAdsr2.setParameters ({
            paramValue (synth.modEnvParams[1]->modAttack),
            paramValue (synth.modEnvParams[1]->modDecay),
            paramValue (synth.modEnvParams[1]->modSustain) / 100.0f,
            paramValue (synth.modEnvParams[1]->modRelease),
        });

        float lfoFreq1;
        if (synth.lfoParams[0]->syncValue)
            lfoFreq1 = 1.0f / ((synth.lfoParams[0]->beatValue.get()) / (synth.getCurrentTempo() / 60.0f));
        else
            lfoFreq1 = paramValue (synth.lfoParams[0]->rate);

        lfo1.setParameters ({
            lfoFreq1,
            0,
            0,
            paramValue (synth.lfoParams[0]->depth),
            (SimpleLFO::WaveShape) synth.lfoParams[0]->waveShapeValue.get(),
            0.5f
        });

        float lfoFreq2;
        if (synth.lfoParams[1]->syncValue)
            lfoFreq2 = 1.0f / ((synth.lfoParams[1]->beatValue.get()) / (synth.getCurrentTempo() / 60.0f));
        else
            lfoFreq2 = paramValue (synth.lfoParams[1]->rate);

        lfo2.setParameters ({
            lfoFreq2,
            0,
            0,
            paramValue (synth.lfoParams[1]->depth) / 2,
            (SimpleLFO::WaveShape) synth.lfoParams[1]->waveShapeValue.get(),
            0.5f
        });

        // Amp
        ampAdsr.setAnalog (synth.ampAnalogValue);

        ampAdsr.setParameters ({
            paramValue (synth.ampAttack),
            paramValue (synth.ampDecay),
            paramValue (synth.ampSustain) / 100.0f,
            isQuickStop ? std::min (0.01f, paramValue (synth.ampRelease))
                        : paramValue (synth.ampRelease)
        });

        // Filter
        filterAdsr.setParameters ({
            paramValue (synth.filterAttack),
            paramValue (synth.filterDecay),
            paramValue (synth.filterSustain) / 100.0f,
            paramValue (synth.filterRelease)
        });

        int type = synth.filterTypeValue;
        float filterEnv = filterAdsr.getEnvelopeValue();
        float filterSens = paramValue (synth.filterVelocity) / 100.0f;
        filterSens = currentlyPlayingNote.noteOnVelocity.asUnsignedFloat() * filterSens + 1.0f - filterSens;
        filterEnv *= filterSens;

        for (int i = 0; i < numSamples; i++)
            filterAdsr.getNextSample();

        auto getMidiNoteInHertz = [](float noteNumber)
        {
            return 440.0f * std::pow (2.0f, (noteNumber - 69) / 12.0f);
        };

        float freqNote = paramValue (synth.filterFreq);
        freqNote += (currentlyPlayingNote.initialNote - 60) * paramValue (synth.filterKey) / 100.0f;
        freqNote += filterEnv * (paramValue (synth.filterAmount) * 137);

        filterFrequencySmoother.setValue (freqNote / 135.076232f);
        if (snapAllValues)
            filterFrequencySmoother.snapToValue();

        freqNote = filterFrequencySmoother.getCurrentValue() * 135.076232f;
        filterFrequencySmoother.process (numSamples);

        lastFilterFreq = juce::jlimit (8.0f,
                                       std::min (20000.0f, float (currentSampleRate) / 2.0f),
                                       getMidiNoteInHertz (freqNote));

        float q = 0.70710678118655f / (1.0f - (paramValue (synth.filterResonance) / 100.0f) * 0.99f);

        if (type != 0)
        {
            juce::IIRCoefficients coefs1, coefs2;

            if (type == 1)
            {
                coefs1 = juce::IIRCoefficients::makeLowPass (currentSampleRate, lastFilterFreq, q);
                coefs2 = juce::IIRCoefficients::makeLowPass (currentSampleRate, lastFilterFreq, 0.70710678118655f);
            }
            else if (type == 2)
            {
                coefs1 = juce::IIRCoefficients::makeHighPass (currentSampleRate, lastFilterFreq, q);
                coefs2 = juce::IIRCoefficients::makeHighPass (currentSampleRate, lastFilterFreq, 0.70710678118655f);
            }
            else if (type == 3)
            {
                coefs1 = juce::IIRCoefficients::makeBandPass (currentSampleRate, lastFilterFreq, q);
                coefs2 = juce::IIRCoefficients::makeBandPass (currentSampleRate, lastFilterFreq, 0.70710678118655f);
            }
            else if (type == 4)
            {
                coefs1 = juce::IIRCoefficients::makeNotchFilter (currentSampleRate, lastFilterFreq, q);
                coefs2 = juce::IIRCoefficients::makeNotchFilter (currentSampleRate, lastFilterFreq, 0.70710678118655f);
            }

            filterL1.setCoefficients (coefs1);
            filterR1.setCoefficients (coefs1);

            filterL2.setCoefficients (coefs2);
            filterR2.setCoefficients (coefs2);
        }

        // Oscillators
        double activeNoteSmoothed = activeNote.getNextValue();
        activeNote.skip (numSamples);

        int idx = 0;
        for (auto& o : oscillators)
        {
            double note = activeNoteSmoothed + currentlyPlayingNote.totalPitchbendInSemitones;
            note += juce::roundToInt (paramValue (synth.oscParams[idx]->tune))
                      + paramValue (synth.oscParams[idx]->fineTune) / 100.0;

            o.setNote (float (note));
            o.setGain (juce::Decibels::decibelsToGain (paramValue (synth.oscParams[idx]->level)));
            o.setWave ((Oscillator::Waves)(int (synth.oscParams[idx]->waveShapeValue.get())));
            o.setPulseWidth (paramValue (synth.oscParams[idx]->pulseWidth));
            o.setNumVoices (synth.oscParams[idx]->voicesValue);
            o.setDetune (paramValue (synth.oscParams[idx]->detune));
            o.setSpread (paramValue (synth.oscParams[idx]->spread) / 100.0f);
            o.setPan (paramValue (synth.oscParams[idx]->pan));

            idx++;
        }

        if (lastLegato != paramValue (synth.legato) && ! activeNote.isSmoothing())
        {
            lastLegato = paramValue (synth.legato);
            activeNote.reset (currentSampleRate, lastLegato / 1000.0f);
        }
    }

    // MPE aftertouch/pressure callback - currently unused but required by MPESynthesiserVoice interface
    // Could be implemented to add pressure-sensitive modulation for MPE controllers
    void notePressureChanged() override     {}
    // MPE per-note pitchbend callback - pitch changes handled in updateParams() via currentlyPlayingNote
    // Empty implementation as pitchbend is already processed through JUCE MPE note tracking
    void notePitchbendChanged() override    {}
    // MPE timbre (CC74) callback - timbre modulation handled through modulation matrix instead
    // Empty implementation as timbre is processed as modulation source in updateParams()
    void noteTimbreChanged() override       {}
    // MPE key state callback for note slide/glide - currently unused but required by interface
    // Could be implemented for advanced MPE gestures like note slides between keys
    void noteKeyStateChanged() override     {}

private:
    // Get final parameter value with modulation matrix applied and smoothing for audio-rate changes
    // Core parameter access method used throughout voice rendering to get modulated parameter values
    float paramValue (AutomatableParameter::Ptr param)
    {
        jassert (param != nullptr);
        if (param == nullptr)
            return 0.0f;

        auto smoothItr = smoothers.find (param.get());
        if (smoothItr == smoothers.end())
            return param->getCurrentValue();

        auto modItr = synth.modMatrix.find (param.get());
        if (modItr == synth.modMatrix.end() || ! modItr->second.isModulated())
        {
            smoothItr->second.setValue (param->getCurrentNormalisedValue());

            if (snapAllValues)
                smoothItr->second.snapToValue();

            return param->valueRange.convertFrom0to1 (smoothItr->second.getCurrentValue());
        }
        else
        {
            float val = param->getCurrentNormalisedValue();

            auto& mod = modItr->second;

            for (int i = mod.firstModIndex; i < juce::numElementsInArray (mod.depths) && i <= mod.lastModIndex; i++)
            {
                float d = mod.depths[i];

                if (d > -1000.0f)
                    val += currentModValue[i] * d;
            }

            val = juce::jlimit (0.0f, 1.0f, val);

            smoothItr->second.setValue (val);

            if (snapAllValues)
                smoothItr->second.snapToValue();

            return param->valueRange.convertFrom0to1 (smoothItr->second.getCurrentValue());
        }
    }

    FourOscPlugin& synth;                   // Reference to parent plugin for parameter access and settings

    juce::AudioBuffer<float> renderBuffer {2, 512};    // Temporary stereo buffer for voice audio rendering before mixing to output
    MultiVoiceOscillator oscillators[4];               // Four main oscillators with unison/detune capabilities
    ExpEnvelope ampAdsr;                               // Exponential amplitude envelope for musical amplitude curves
    LinEnvelope filterAdsr, modAdsr1, modAdsr2;       // Linear envelopes: filter cutoff, mod envelope 1 & 2 for modulation sources
    SimpleLFO lfo1, lfo2;                             // Two LFOs for modulation matrix sources, tempo-syncable
    juce::IIRFilter filterL1, filterR1, filterL2, filterR2;  // Stereo IIR filters: first and second stages for 12dB/24dB slopes

    ValueSmoother<float> filterFrequencySmoother;     // Smooths filter frequency changes to prevent audio glitches

    bool retrigger = false;                           // Flag for retriggering voice after quick stop in non-legato mode
    bool isPlaying = false;                           // Voice activity state, true when note is active
    bool isQuickStop = false;                         // Flag for immediate envelope stop during note retriggering
    bool snapAllValues = false;                       // Force immediate parameter changes without smoothing (first block)
    bool firstBlock = false;                          // Flag indicating first audio block after note start for initialization
    juce::LinearSmoothedValue<float> activeNote;      // Smoothed MIDI note number for legato transitions and portamento
    float lastLegato = -1.0f;                         // Previous legato time setting for detecting parameter changes
    float lastFilterFreq = 0;                         // Last calculated filter frequency for GUI visualization

    float currentModValue[FourOscPlugin::numModSources] = {0};  // Current values of all modulation sources (LFOs, envelopes, MIDI, etc.)

    std::map<AutomatableParameter*, ValueSmoother<float>> smoothers;  // Per-parameter smoothers for glitch-free modulation
};

//==============================================================================
// Initialize parameter objects for one of the four oscillators with value tree bindings
// Creates automatable parameters and connects them to plugin state, called during plugin construction
FourOscPlugin::OscParams::OscParams (FourOscPlugin& plugin, int oscNum)
{
    auto um = plugin.getUndoManager();

    auto oscID = [] (juce::Identifier i, int num)
    {
        return juce::Identifier (i.toString() + juce::String (num));
    };

    waveShapeValue.referTo (plugin.state, oscID (IDs::waveShape, oscNum), um, oscNum == 1 ? 1 : 0);
    tuneValue.referTo (plugin.state, oscID (IDs::tune, oscNum), um, 0);
    fineTuneValue.referTo (plugin.state, oscID (IDs::fineTune, oscNum), um, 0);
    levelValue.referTo (plugin.state, oscID (IDs::level, oscNum), um, 0);
    pulseWidthValue.referTo (plugin.state, oscID (IDs::pulseWidth, oscNum), um, 0.5);
    voicesValue.referTo (plugin.state, oscID (IDs::voices, oscNum), um, 1);
    detuneValue.referTo (plugin.state, oscID (IDs::detune, oscNum), um, 0);
    spreadValue.referTo (plugin.state, oscID (IDs::spread, oscNum), um, 0);
    panValue.referTo (plugin.state, oscID (IDs::pan, oscNum), um, 0);

    auto paramID = [] (juce::Identifier i, int num)
    {
        return juce::Identifier (i.toString() + juce::String (num)).toString();
    };

    tune        = plugin.addParam (paramID (IDs::tune, oscNum), TRANS("Tune") + " " + juce::String (oscNum), {-36.0f, 36.0f, 1.0f}, "st");
    fineTune    = plugin.addParam (paramID (IDs::fineTune, oscNum), TRANS("Fine Tune") + " " + juce::String (oscNum), {-100.0f, 100.0f});
    level       = plugin.addParam (paramID (IDs::level, oscNum), TRANS("Level") + " " + juce::String (oscNum), {-100.0f, 0.0f, 0.0f, 4.0f}, "dB");
    pulseWidth  = plugin.addParam (paramID (IDs::pulseWidth, oscNum), TRANS("Pulse Width") + " " + juce::String (oscNum), {0.01f, 0.99f});
    detune      = plugin.addParam (paramID (IDs::detune, oscNum), TRANS("Detune") + " " + juce::String (oscNum), {0.0f, 0.5f});
    spread      = plugin.addParam (paramID (IDs::spread, oscNum), TRANS("Spread") + " " + juce::String (oscNum), {-100.0f, 100.0f}, "%");
    pan         = plugin.addParam (paramID (IDs::pan, oscNum), TRANS("Pan") + " " + juce::String (oscNum), {-1.0f, 1.0f});
}

// Connect automatable parameters to their cached value objects for real-time access
// Called after plugin construction to establish parameter-to-value bindings for audio thread
void FourOscPlugin::OscParams::attach()
{
    tune->attachToCurrentValue (tuneValue);
    fineTune->attachToCurrentValue (fineTuneValue);
    level->attachToCurrentValue (levelValue);
    pulseWidth->attachToCurrentValue (pulseWidthValue);
    detune->attachToCurrentValue (detuneValue);
    spread->attachToCurrentValue (spreadValue);
    pan->attachToCurrentValue (panValue);
}

// Disconnect parameters from cached values during plugin destruction to prevent access violations
// Called in plugin destructor to safely clean up parameter connections
void FourOscPlugin::OscParams::detach()
{
    tune->detachFromCurrentValue();
    fineTune->detachFromCurrentValue();
    level->detachFromCurrentValue();
    pulseWidth->detachFromCurrentValue();
    detune->detachFromCurrentValue();
    spread->detachFromCurrentValue();
    pan->detachFromCurrentValue();
}

//==============================================================================
// Initialize parameter objects for one of the two LFOs including sync and tempo-based controls
// Creates rate, depth, sync, and beat division parameters, called during plugin construction
FourOscPlugin::LFOParams::LFOParams (FourOscPlugin& plugin, int lfoNum)
{
    auto um = plugin.getUndoManager();

    auto lfoID = [] (juce::Identifier i, int num)
    {
        return juce::Identifier (i.toString() + juce::String (num));
    };

    waveShapeValue.referTo (plugin.state, lfoID (IDs::lfoWaveShape, lfoNum), um, lfoNum == 1 ? 1 : 0);
    syncValue.referTo (plugin.state, lfoID (IDs::lfoSync, lfoNum), um, false);
    rateValue.referTo (plugin.state, lfoID (IDs::lfoRate, lfoNum), um, 1);
    depthValue.referTo (plugin.state, lfoID (IDs::lfoDepth, lfoNum), um, 1.0f);
    beatValue.referTo (plugin.state, lfoID (IDs::lfoBeat, lfoNum), um, 1);

    auto paramID = [] (juce::Identifier i, int num)
    {
        return juce::Identifier (i.toString() + juce::String (num)).toString();
    };

    rate        = plugin.addParam (paramID (IDs::lfoRate, lfoNum),  TRANS("Rate") + " " + juce::String (lfoNum), {0.0f, 500.0f, 0.0f, 0.3f}, "Hz");
    depth       = plugin.addParam (paramID (IDs::lfoDepth, lfoNum), TRANS("Depth") + " " + juce::String (lfoNum), {0.0f, 1.0f});
}

// Connect LFO automatable parameters to cached value objects for real-time audio processing
// Establishes parameter bindings after plugin construction for thread-safe access
void FourOscPlugin::LFOParams::attach()
{
    depth->attachToCurrentValue (depthValue);
    rate->attachToCurrentValue (rateValue);
}

// Safely disconnect LFO parameters from cached values during plugin cleanup
// Called in destructor to prevent dangling references and access violations
void FourOscPlugin::LFOParams::detach()
{
    depth->detachFromCurrentValue();
    rate->detachFromCurrentValue();
}

//==============================================================================
// Initialize ADSR envelope parameters for one of the two modulation envelopes used by mod matrix
// Creates attack, decay, sustain, release parameters for modulation sources, called during construction
FourOscPlugin::MODEnvParams::MODEnvParams (FourOscPlugin& plugin, int modNum)
{
    auto um = plugin.getUndoManager();

    auto modID = [] (juce::Identifier i, int num)
    {
        return juce::Identifier (i.toString() + juce::String (num));
    };

    modAttackValue.referTo (plugin.state, modID (IDs::modAttack, modNum), um, 0.1f);
    modDecayValue.referTo (plugin.state, modID (IDs::modDecay, modNum), um, 0.1f);
    modSustainValue.referTo (plugin.state, modID (IDs::modSustain, modNum), um, 80.0f);
    modReleaseValue.referTo (plugin.state, modID (IDs::modRelease, modNum), um, 0.1f);

    auto paramID = [] (juce::Identifier i, int num)
    {
        return juce::Identifier (i.toString() + juce::String (num)).toString();
    };

    modAttack   = plugin.addParam (paramID (IDs::modAttack, modNum),  TRANS("Mod Attack")  + " " + juce::String (modNum), {0.0f, 60.0f, 0.0f, 0.2f});
    modDecay    = plugin.addParam (paramID (IDs::modDecay, modNum),   TRANS("Mod Decay")   + " " + juce::String (modNum), {0.0f, 60.0f, 0.0f, 0.2f});
    modSustain  = plugin.addParam (paramID (IDs::modSustain, modNum), TRANS("Mod Sustain") + " " + juce::String (modNum), {0.0f, 100.0f}, "%");
    modRelease  = plugin.addParam (paramID (IDs::modRelease, modNum), TRANS("Mod Release") + " " + juce::String (modNum), {0.001f, 60.0f, 0.0f, 0.2f});
}

// Connect modulation envelope parameters to cached values for real-time envelope processing
// Enables thread-safe access to envelope parameters during voice rendering
void FourOscPlugin::MODEnvParams::attach()
{
    modAttack->attachToCurrentValue (modAttackValue);
    modDecay->attachToCurrentValue (modDecayValue);
    modSustain->attachToCurrentValue (modSustainValue);
    modRelease->attachToCurrentValue (modReleaseValue);
}

// Safely disconnect envelope parameters during plugin destruction to prevent memory access issues
// Called in destructor as part of parameter cleanup sequence
void FourOscPlugin::MODEnvParams::detach()
{
    modAttack->detachFromCurrentValue();
    modDecay->detachFromCurrentValue();
    modSustain->detachFromCurrentValue();
    modRelease->detachFromCurrentValue();
}

//==============================================================================
// Main plugin constructor - initializes all oscillators, effects, envelopes, and modulation matrix
// Sets up complete synthesizer with 4 oscillators, filters, effects chain, and MPE support
FourOscPlugin::FourOscPlugin (PluginCreationInfo info)  : Plugin (info)
{
    auto um = getUndoManager();

    levelMeasurer.addClient (*this);

    instrument.enableLegacyMode();
    setPitchbendTrackingMode (juce::MPEInstrument::allNotesOnChannel);

    setVoiceStealingEnabled (true);

    delay  = std::make_unique<FODelay>();
    chorus = std::make_unique<FOChorus>();

    for (int i = 0; i < 4; i++) oscParams.add (new OscParams (*this, i + 1));
    for (int i = 0; i < 2; i++) lfoParams.add (new LFOParams (*this, i + 1));
    for (int i = 0; i < 2; i++) modEnvParams.add (new MODEnvParams (*this, i + 1));

    // Amp
    ampAttackValue.referTo (state, IDs::ampAttack, um, 0.1f);
    ampDecayValue.referTo (state, IDs::ampDecay, um, 0.1f);
    ampSustainValue.referTo (state, IDs::ampSustain, um, 80.0f);
    ampReleaseValue.referTo (state, IDs::ampRelease, um, 0.1f);
    ampVelocityValue.referTo (state, IDs::ampVelocity, um, 100.0f);
    ampAnalogValue.referTo (state, IDs::ampAnalog, um, true);

    ampAttack   = addParam ("ampAttack",   TRANS("Amp Attack"),   {0.001f, 60.0f, 0.0f, 0.2f});
    ampDecay    = addParam ("ampDecay",    TRANS("Amp Decay"),    {0.001f, 60.0f, 0.0f, 0.2f});
    ampSustain  = addParam ("ampSustain",  TRANS("Amp Sustain"),  {0.0f,   100.0f}, "%");
    ampRelease  = addParam ("ampRelease",  TRANS("Amp Release"),  {0.001f, 60.0f, 0.0f, 0.2f});
    ampVelocity = addParam ("ampVelocity", TRANS("Amp Velocity"), {0.0f, 100.0f}, "%");

    ampAttack->attachToCurrentValue (ampAttackValue);
    ampDecay->attachToCurrentValue (ampDecayValue);
    ampSustain->attachToCurrentValue (ampSustainValue);
    ampRelease->attachToCurrentValue (ampReleaseValue);
    ampVelocity->attachToCurrentValue (ampVelocityValue);

    // Filter
    filterAttackValue.referTo (state, IDs::filterAttack, um, 0.1f);
    filterDecayValue.referTo (state, IDs::filterDecay, um, 0.1f);
    filterSustainValue.referTo (state, IDs::filterSustain, um, 80.0f);
    filterReleaseValue.referTo (state, IDs::filterRelease, um, 0.1f);
    filterFreqValue.referTo (state, IDs::filterFreq, um, 69.0f);
    filterResonanceValue.referTo (state, IDs::filterResonance, um, 0.5f);
    filterAmountValue.referTo (state, IDs::filterAmount, um, 0.0f);
    filterKeyValue.referTo (state, IDs::filterKey, um, 0.0f);
    filterVelocityValue.referTo (state, IDs::filterVelocity, um, 0.0f);
    filterTypeValue.referTo (state, IDs::filterType, um, 0);
    filterSlopeValue.referTo (state, IDs::filterSlope, um, 12);

    filterAttack    = addParam ("filterAttack",     TRANS("Filter Attack"),     {0.0f, 60.0f, 0.0f, 0.2f});
    filterDecay     = addParam ("filterDecay",      TRANS("Filter Decay"),      {0.0f, 60.0f, 0.0f, 0.2f});
    filterSustain   = addParam ("filterSustain",    TRANS("Filter Sustain"),    {0.0f, 100.0f}, "%");
    filterRelease   = addParam ("filterRelease",    TRANS("Filter Release"),    {0.0f, 60.0f, 0.0f, 0.2f});
    filterFreq      = addParam ("filterFreq",       TRANS("Filter Freq"),       {0.0f, 135.076232f});
    filterResonance = addParam ("filterResonance",  TRANS("Filter Resonance"),  {0.0f, 100.0f}, "%");
    filterAmount    = addParam ("filterAmount",     TRANS("Filter Amount"),     {-1.0f, 1.0f});
    filterKey       = addParam ("filterKey",        TRANS("Filter Key"),        {0.0f, 100.0f}, "%");
    filterVelocity  = addParam ("filterVelocity",   TRANS("Filter Velocity"),   {0.0f, 100.0f}, "%");

    filterAttack->attachToCurrentValue (filterAttackValue);
    filterDecay->attachToCurrentValue (filterDecayValue);
    filterSustain->attachToCurrentValue (filterSustainValue);
    filterRelease->attachToCurrentValue (filterReleaseValue);
    filterFreq->attachToCurrentValue (filterFreqValue);
    filterResonance->attachToCurrentValue (filterResonanceValue);
    filterAmount->attachToCurrentValue (filterAmountValue);
    filterKey->attachToCurrentValue (filterKeyValue);
    filterVelocity->attachToCurrentValue (filterVelocityValue);

    // Build the mod matrix before we add any global params
    for (auto p : getAutomatableParameters())
        modMatrix[p] = ModAssign();

    // Effects: Distortion
    distortionOnValue.referTo (state, IDs::distortionOn, um);
    distortionValue.referTo (state, IDs::distortion, um, 0.0f);

    distortion = addParam ("distortion", TRANS("Distortion"), {0.0f, 1.0f});

    distortion->attachToCurrentValue (distortionValue);

    // Effects: Reverb
    reverbOnValue.referTo (state, IDs::reverbOn, um);
    reverbSizeValue.referTo (state, IDs::reverbSize, um, 0.0f);
    reverbDampingValue.referTo (state, IDs::reverbDamping, um, 0.0f);
    reverbWidthValue.referTo (state, IDs::reverbWidth, um, 0.0f);
    reverbMixValue.referTo (state, IDs::reverbMix, um, 0.0);

    reverbSize      = addParam ("reverbSize",     TRANS("Size"),    {0.0f, 1.0f});
    reverbDamping   = addParam ("reverbDamping",  TRANS("Damping"), {0.0f, 1.0f});
    reverbWidth     = addParam ("reverbWidth",    TRANS("Width"),   {0.0f, 1.0f});
    reverbMix       = addParam ("reverbMix",      TRANS("Mix"),     {0.0f, 1.0f});

    reverbSize->attachToCurrentValue (reverbSizeValue);
    reverbDamping->attachToCurrentValue (reverbDampingValue);
    reverbWidth->attachToCurrentValue (reverbWidthValue);
    reverbMix->attachToCurrentValue (reverbMixValue);

    // Effects: Delay
    delayOnValue.referTo (state, IDs::delayOn, um);
    delayFeedbackValue.referTo (state, IDs::delayFeedback, um, -10.0f);
    delayCrossfeedValue.referTo (state, IDs::delayCrossfeed, um, -100.0f);
    delayMixValue.referTo (state, IDs::delayMix, um, 0.0f);
    delayValue.referTo (state, IDs::delay, um, 1.0f);

    delayFeedback   = addParam ("delayFeedback",    TRANS("Feedback"),  {-100.0f, 0.0f, 0.0f, 4.0f}, "dB");
    delayCrossfeed  = addParam ("delayCrossfeed",   TRANS("Crossfeed"), {-100.0f, 0.0f, 0.0f, 4.0f}, "dB");
    delayMix        = addParam ("delayMix",         TRANS("Mix"),       {0.0f, 1.0f});

    delayFeedback->attachToCurrentValue (delayFeedbackValue);
    delayCrossfeed->attachToCurrentValue (delayCrossfeedValue);
    delayMix->attachToCurrentValue (delayMixValue);

    // Effects: Chorus
    chorusOnValue.referTo (state, IDs::chorusOn, um);
    chorusSpeedValue.referTo (state, IDs::chosusSpeed, um, 1.0f);
    chorusDepthValue.referTo (state, IDs::chorusDepth, um, 3.0f);
    chorusWidthValue.referTo (state, IDs::chrousWidth, um, 0.5f);
    chorusMixValue.referTo (state, IDs::chorusMix, um, 0.0f);

    chorusSpeed    = addParam ("chorusSpeed",      TRANS("Speed"),       {0.1f, 10.0f}, "Hz");
    chorusDepth    = addParam ("chorusDepth",      TRANS("Depth"),       {0.1f, 20.0f}, "ms");
    chorusWidth    = addParam ("chorusWidth",      TRANS("Width"),       {0.0f, 1.0f});
    chorusMix      = addParam ("chorusMix",        TRANS("Mix"),         {0.0f, 1.0f});

    chorusSpeed->attachToCurrentValue (chorusSpeedValue);
    chorusDepth->attachToCurrentValue (chorusDepthValue);
    chorusWidth->attachToCurrentValue (chorusWidthValue);
    chorusMix->attachToCurrentValue (chorusMixValue);

    // Master
    voiceModeValue.referTo (state, IDs::voiceMode, um, 2);
    voicesValue.referTo (state, IDs::voices, um, 32);
    legatoValue.referTo (state, IDs::legato, um, 0);
    masterLevelValue.referTo (state, IDs::masterLevel, um, 0);

    legato          = addParam ("legato",           TRANS("Legato"),    {0.0f, 500.0f}, "ms");
    masterLevel     = addParam ("masterLevel",      TRANS("Level"),     {-100.0f, 0.0f, 0.0f, 4.0f});

    legato->attachToCurrentValue (legatoValue);
    masterLevel->attachToCurrentValue (masterLevelValue);

    // Oscillators
    for (auto o : oscParams)
        o->attach();

    // Mod
    for (auto l : lfoParams)
        l->attach();

    for (auto e : modEnvParams)
        e->attach();

    for (auto p : getAutomatableParameters())
        smoothers[p] = {};

    // Setup text functions
    setupTextFunctions();

    valueTreePropertyChanged (state, IDs::voiceMode);
    valueTreePropertyChanged (state, IDs::mpe);

    loadModMatrix();
}

// Plugin destructor - safely detaches all parameters and cleans up resources
// Ensures proper cleanup of parameter bindings, voices, and effect instances
FourOscPlugin::~FourOscPlugin()
{
    notifyListenersOfDeletion();

    // Oscillators
    for (auto o : oscParams)
        o->detach();

    // Mod
    for (auto l : lfoParams)
        l->detach();

    for (auto e : modEnvParams)
        e->detach();

    // Amp
    ampAttack->detachFromCurrentValue();
    ampDecay->detachFromCurrentValue();
    ampSustain->detachFromCurrentValue();
    ampRelease->detachFromCurrentValue();
    ampVelocity->detachFromCurrentValue();

    // Filter
    filterAttack->detachFromCurrentValue();
    filterDecay->detachFromCurrentValue();
    filterSustain->detachFromCurrentValue();
    filterRelease->detachFromCurrentValue();
    filterFreq->detachFromCurrentValue();
    filterResonance->detachFromCurrentValue();
    filterAmount->detachFromCurrentValue();
    filterKey->detachFromCurrentValue();
    filterVelocity->detachFromCurrentValue();

    // Effects: Distortion
    distortion->detachFromCurrentValue();

    // Effects: Reverb
    reverbSize->detachFromCurrentValue();
    reverbDamping->detachFromCurrentValue();
    reverbWidth->detachFromCurrentValue();
    reverbMix->detachFromCurrentValue();

    // Effects: Delay
    delayFeedback->detachFromCurrentValue();
    delayCrossfeed->detachFromCurrentValue();
    delayMix->detachFromCurrentValue();

    // Effects: Chorus
    chorusSpeed->detachFromCurrentValue();
    chorusDepth->detachFromCurrentValue();
    chorusWidth->detachFromCurrentValue();
    chorusMix->detachFromCurrentValue();

    // Voices
    legato->detachFromCurrentValue();
    masterLevel->detachFromCurrentValue();
}

const char* FourOscPlugin::xmlTypeName = "4osc";

// Create and register new automatable parameter with DAW automation support and optional unit label
// Used throughout construction to create all plugin parameters with proper DAW integration
AutomatableParameter* FourOscPlugin::addParam (const juce::String& paramID, const juce::String& name, juce::NormalisableRange<float> valueRange, juce::String label)
{
    auto p = Plugin::addParam (paramID, name, valueRange);

    if (label.isNotEmpty())
        labels[paramID] = label;

    return p;
}

// Configure parameter display formatting (Hz, dB, %, ms, etc.) for GUI and DAW parameter display
// Called during construction to set up proper parameter value formatting for user interface
void FourOscPlugin::setupTextFunctions()
{
    // Add a default function that does number of decimal places nicely and adds a labels
    for (auto p : getAutomatableParameters())
    {
        juce::String label;
        auto itr = labels.find (p->paramID);
        if (itr != labels.end())
            label = itr->second;

        auto basicValueToTextFunction = [label] (float value)
        {
            juce::String text;
            float v = std::abs (value);
            if (v > 100)
                text = juce::String (juce::roundToInt (value));
            else if (v > 10)
                text = juce::String (value, 1);
            else if (v > 1)
                text = juce::String (value, 2);
            else
                text = juce::String (value, 3);

            if (label.isNotEmpty())
                text += label;

            return text;
        };

        p->valueToStringFunction = basicValueToTextFunction;
    }

    auto timeValueToTextFunction = [] (float value)
    {
        if (value < 1.0f)
            return juce::String (juce::roundToInt (value * 1000)) + "ms";
        return juce::String (value, 2) + "s";
    };

    auto panValueToTextFunction = [] (float value)
    {
        if (value < 0.0f)
            return juce::String (juce::roundToInt (-value * 100)) + "L";
        return juce::String (juce::roundToInt (value * 100)) + "R";
    };

    auto percentValueToTextFunction = [] (float value)
    {
        return juce::String (juce::roundToInt (value * 100)) + "%";
    };

    auto tuneValueToTextFunction = [] (float value)
    {
        return juce::String (juce::roundToInt (value)) + "st";
    };

    auto freqValueToTextFunction = [] (float value)
    {
        float freq = 440.0f * std::pow (2.0f, (value - 69) / 12.0f);
        return juce::String (juce::roundToInt (freq)) + "Hz";
    };

    auto textToFreqValueFunction = [] (juce::String text)
    {
        float freq = text.getFloatValue();
        return 12.0f * std::log2 (freq / 440.0f) + 69.0f;
    };

    auto textToTimeValueFunction = [] (juce::String text)
    {
        float time = text.getFloatValue();
        return (text.contains ("ms") || time > 10.0f) ? (time / 1000.0f) : time;
    };

    for (auto p : oscParams)
    {
        p->pulseWidth->valueToStringFunction = percentValueToTextFunction;
        p->tune->valueToStringFunction = tuneValueToTextFunction;
        p->detune->valueToStringFunction = percentValueToTextFunction;
        p->pan->valueToStringFunction = panValueToTextFunction;
    }

    for (auto p : lfoParams)
    {
        p->depth->valueToStringFunction = percentValueToTextFunction;
    }

    for (auto p : modEnvParams)
    {
        p->modAttack->valueToStringFunction = timeValueToTextFunction;
        p->modAttack->stringToValueFunction = textToTimeValueFunction;
        p->modDecay->valueToStringFunction = timeValueToTextFunction;
        p->modDecay->stringToValueFunction = textToTimeValueFunction;
        p->modRelease->valueToStringFunction = timeValueToTextFunction;
        p->modRelease->stringToValueFunction = textToTimeValueFunction;
    }

    ampAttack->valueToStringFunction = timeValueToTextFunction;
    ampAttack->stringToValueFunction = textToTimeValueFunction;
    ampDecay->valueToStringFunction = timeValueToTextFunction;
    ampDecay->stringToValueFunction = textToTimeValueFunction;
    ampRelease->valueToStringFunction = timeValueToTextFunction;
    ampRelease->stringToValueFunction = textToTimeValueFunction;

    filterAttack->valueToStringFunction = timeValueToTextFunction;
    filterAttack->stringToValueFunction = textToTimeValueFunction;
    filterDecay->valueToStringFunction = timeValueToTextFunction;
    filterDecay->stringToValueFunction = textToTimeValueFunction;
    filterRelease->valueToStringFunction = timeValueToTextFunction;
    filterRelease->stringToValueFunction = textToTimeValueFunction;
    filterFreq->valueToStringFunction = freqValueToTextFunction;
    filterFreq->stringToValueFunction = textToFreqValueFunction;
    filterAmount->valueToStringFunction = percentValueToTextFunction;

    distortion->valueToStringFunction = percentValueToTextFunction;

    delayMix->valueToStringFunction = percentValueToTextFunction;

    chorusWidth->valueToStringFunction = percentValueToTextFunction;
    chorusMix->valueToStringFunction = percentValueToTextFunction;

    reverbSize->valueToStringFunction = percentValueToTextFunction;
    reverbWidth->valueToStringFunction = percentValueToTextFunction;
    reverbDamping->valueToStringFunction = percentValueToTextFunction;
    reverbMix->valueToStringFunction = percentValueToTextFunction;
}

// Get current output level with decay for GUI level meters, implements peak-hold with 48dB/sec decay
// Called by GUI components to display real-time output levels with proper meter ballistics
float FourOscPlugin::getLevel (int channel)
{
    auto& peak = levels[channel];

    auto elapsedMilliseconds = std::max (0, int (juce::Time::getApproximateMillisecondCounter() - peak.time) - 50);
    float currentLevel = peak.dB - (48.0f * elapsedMilliseconds / 1000.0f);

    auto latest = getAndClearAudioLevel (channel);

    if (latest.dB > currentLevel)
    {
        peak = latest;
        return juce::jlimit (-100.0f, 0.0f, peak.dB);
    }

    return juce::jlimit (-100.0f, 0.0f, currentLevel);
}

// Handle global value tree changes and forward to base Plugin class for state management
// Called when plugin state is modified to ensure proper state synchronization
void FourOscPlugin::valueTreeChanged()
{
    Plugin::valueTreeChanged();
}

// Handle specific property changes like voice mode, MPE settings, and modulation matrix updates
// Responds to parameter changes by updating voice allocation, MPE zones, and modulation routing
void FourOscPlugin::valueTreePropertyChanged (juce::ValueTree& v, const juce::Identifier& i)
{
    Plugin::valueTreePropertyChanged (v, i);

    if (v.hasType (IDs::PLUGIN))
    {
        if (i == IDs::voiceMode
            || i == IDs::voices)
        {
            juce::ScopedLock sl (voicesLock);
            if (voiceModeValue == 2)
            {
                reduceNumVoices (voicesValue.get());
                while (getNumVoices() < voicesValue.get())
                    addVoice (new FourOscVoice (*this));
            }
            else
            {
                while (getNumVoices() < 1)
                    addVoice (new FourOscVoice (*this));

                reduceNumVoices (1);
            }
        }
        else if (i == IDs::mpe)
        {
            if ((bool) state[IDs::mpe])
            {
                juce::MPEZoneLayout zones;
                zones.setLowerZone (15);
                instrument.setZoneLayout (zones);
                setPitchbendTrackingMode (juce::MPEInstrument::lastNotePlayedOnChannel);
            }
            else
            {
                instrument.enableLegacyMode();
                setPitchbendTrackingMode (juce::MPEInstrument::allNotesOnChannel);
            }
        }
    }
    else if (v.hasType (IDs::MODMATRIX) || v.hasType (IDs::MODMATRIXITEM))
    {
        if (! flushingState)
            triggerAsyncUpdate();
    }
}

// Handle addition of child nodes like modulation matrix items, triggers async modulation update
// Called when new modulation assignments are created through GUI or preset loading
void FourOscPlugin::valueTreeChildAdded (juce::ValueTree& v, juce::ValueTree& c)
{
    Plugin::valueTreeChildAdded (v, c);

    if (c.hasType (IDs::MODMATRIX) || c.hasType (IDs::MODMATRIXITEM))
        if (! flushingState)
            triggerAsyncUpdate();
}

// Handle removal of child nodes like modulation assignments, triggers modulation matrix reload
// Called when modulation assignments are deleted or presets with different modulation are loaded
void FourOscPlugin::valueTreeChildRemoved (juce::ValueTree& v, juce::ValueTree& c, int i)
{
    Plugin::valueTreeChildRemoved (v, c, i);

    if (c.hasType (IDs::MODMATRIX) || c.hasType (IDs::MODMATRIXITEM))
        if (! flushingState)
            triggerAsyncUpdate();
}

// Process MIDI CC messages and store normalized values for modulation matrix CC sources
// Enables any CC to be used as modulation source, called by JUCE MIDI handling system
void FourOscPlugin::handleController (int, int controllerNumber, int controllerValue)
{
    controllerValues[controllerNumber] = controllerValue / 127.0f;
}

// Process modulation matrix changes on message thread to avoid real-time thread blocking
// Called asynchronously when modulation assignments change to reload modulation routing safely
void FourOscPlugin::handleAsyncUpdate()
{
    loadModMatrix();
}

// Parse modulation matrix from plugin state and configure internal modulation routing tables
// Builds efficient lookup tables for real-time modulation processing from saved state data
void FourOscPlugin::loadModMatrix()
{
    // Disable all modulation
    for (auto& itr : modMatrix)
        for (int s = lfo1; s < numModSources; s++)
            itr.second.depths[s] = -1000.0f;

    // Read modulation from state ValueTree
    auto mm = state.getChildWithName (IDs::MODMATRIX);
    if (! mm.isValid())
        return;

    for (auto mmi : mm)
    {
        auto paramId = mmi.getProperty (IDs::modParam).toString();
        auto src     = idToModulationSource (mmi.getProperty (IDs::modItem).toString());
        float depth  = (float) mmi.getProperty (IDs::modDepth);

        if (src != none)
        {
            if (auto p = getAutomatableParameterByID (paramId))
            {
                auto itr = modMatrix.find (p.get());
                if (itr != modMatrix.end())
                    itr->second.depths[src] = depth;
                else
                    jassertfalse;
            }
        }
    }

    // Update cached lookup info
    for (auto& itr : modMatrix)
        itr.second.updateCachedInfo();
}

// Save complete plugin state including modulation matrix to value tree for preset/project storage
// Called when creating presets or saving projects to ensure all modulation assignments are preserved
void FourOscPlugin::flushPluginStateToValueTree()
{
    juce::ScopedValueSetter<bool> svs (flushingState, true);

    auto um = getUndoManager();

    auto vt = state.getChildWithName (IDs::MODMATRIX);
    if (vt.isValid())
        state.removeChild (vt, um);

    auto mm = juce::ValueTree (IDs::MODMATRIX);

    for (const auto& itr : modMatrix)
    {
        for (int s = lfo1; s < numModSources; s++)
        {
            if (itr.second.depths[s] >= -1.0f)
            {
                auto mmi = juce::ValueTree (IDs::MODMATRIXITEM);
                mmi.setProperty (IDs::modParam, itr.first->paramID, um);
                mmi.setProperty (IDs::modItem, modulationSourceToID ((ModSource)s), um);
                mmi.setProperty (IDs::modDepth, itr.second.depths[s], um);

                mm.addChild (mmi, -1, um);
            }
        }
    }

    state.addChild (mm, -1, um);

    Plugin::flushPluginStateToValueTree(); // Add any parameter values that are being modified
}

// Initialize plugin for audio processing - set sample rate, prepare effects, and reset all DSP components
// Called by host when plugin is loaded or sample rate changes, prepares all effects for processing
void FourOscPlugin::initialise (const PluginInitialisationInfo& info)
{
    setCurrentPlaybackSampleRate (info.sampleRate);

    reverb.setSampleRate (info.sampleRate);
    delay->setSampleRate (info.sampleRate);
    chorus->setSampleRate (info.sampleRate);

    reverb.reset();
    delay->reset();
    chorus->reset();

    for (auto& itr : smoothers)
        itr.second.reset (info.sampleRate, 0.01f);
}

// Clean up plugin resources when unloaded or deactivated by host
// Currently empty but available for future resource cleanup if needed
void FourOscPlugin::deinitialise()
{
}

//==============================================================================
// Reset plugin to initial state - stop all voices immediately without release tails
// Called by host when transport stops or user requests reset, ensures immediate silence
void FourOscPlugin::reset()
{
    turnOffAllVoices (false);
}

// Emergency stop all voices without release phase - implements MIDI panic/all notes off
// Called by host or user to immediately silence all voices in case of stuck notes
void FourOscPlugin::midiPanic()
{
    turnOffAllVoices (false);
}

// Primary audio processing entry point - handles MIDI, processes voices, and applies effects chain
// Called by host for each audio block, manages tempo sync, voice processing, and level metering
void FourOscPlugin::applyToBuffer (const PluginRenderContext& fc)
{
    juce::ScopedLock sl (voicesLock);

    if (fc.destBuffer != nullptr)
    {
        SCOPED_REALTIME_CHECK

        // find the tempo
        currentPos.set (fc.editTime.getStart());
        currentTempo = float (currentPos.getTempo());

        // Handle all notes off first
        if (fc.bufferForMidiMessages != nullptr)
            if (fc.bufferForMidiMessages->isAllNotesOff)
                turnOffAllVoices (true);

        // Chop the buffer in 32 sample blocks so modulation is smooth
        int todo = fc.bufferNumSamples;
        int pos  = fc.bufferStartSample;

        while (todo > 0)
        {
            int thisBlock = std::min (32, todo);

            AudioScratchBuffer workBuffer (2, thisBlock);
            workBuffer.buffer.clear();

            juce::MidiBuffer midi;
            if (fc.bufferForMidiMessages != nullptr)
            {
                for (auto m : *fc.bufferForMidiMessages)
                {
                    int midiPos = juce::roundToInt (m.getTimeStamp() * getSampleRate());
                    if (midiPos >= pos && midiPos < pos + thisBlock)
                        midi.addEvent (m, midiPos - pos);
                }
            }

            applyToBuffer (workBuffer.buffer, midi);

            if (fc.destBuffer->getNumChannels() == 1)
            {
                fc.destBuffer->copyFrom (0, pos, workBuffer.buffer, 0, 0, thisBlock);
            }
            else
            {
                fc.destBuffer->copyFrom (0, pos, workBuffer.buffer, 0, 0, thisBlock);
                fc.destBuffer->copyFrom (1, pos, workBuffer.buffer, 1, 0, thisBlock);
            }

            levelMeasurer.processBuffer (workBuffer.buffer, 0, thisBlock);

            todo -= thisBlock;
            pos += thisBlock;
        }

        for (int ch = 2; ch < fc.destBuffer->getNumChannels(); ch++)
            fc.destBuffer->clear (ch, fc.bufferStartSample, fc.bufferNumSamples);
    }
}

// Internal audio processing method - updates parameters, renders voices, and applies effect chain
// Called from main applyToBuffer in 32-sample blocks for smooth modulation and parameter changes
void FourOscPlugin::applyToBuffer (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    updateParams (buffer);
    renderNextBlock (buffer, midi, 0, buffer.getNumSamples());
    applyEffects (buffer);

    for (auto& itr : smoothers)
        itr.second.process (buffer.getNumSamples());
}

// Apply effect chain in order: distortion → chorus → delay → reverb, then master level
// Called after voice rendering to process synthesized audio through effects with bypass checking
void FourOscPlugin::applyEffects (juce::AudioBuffer<float>& buffer)
{
    int numSamples = buffer.getNumSamples();

    // Apply Distortion
    if (distortionOnValue)
    {
        float drive = paramValue (distortion);
        float clip = 1.0f / (2.0f * drive);
        Distortion::distortion (buffer.getWritePointer (0), numSamples, drive, -clip, clip);
        Distortion::distortion (buffer.getWritePointer (1), numSamples, drive, -clip, clip);
    }

    // Apply Chorus
    if (chorusOnValue)
        chorus->process (buffer, numSamples);

    // Apply Delay
    if (delayOnValue)
        delay->process (buffer, numSamples);

    // Apply Reverb
    if (reverbOnValue)
        reverb.processStereo (buffer.getWritePointer (0), buffer.getWritePointer (1), numSamples);

    // Apply master level
    buffer.applyGain (juce::Decibels::decibelsToGain (paramValue (masterLevel)));
}

// Update effect parameters including tempo-synced delay and all effect settings before processing
// Called before effect processing to ensure parameters are current, handles tempo sync calculations
void FourOscPlugin::updateParams (juce::AudioBuffer<float>& buffer)
{
    ignoreUnused (buffer);

    // Reverb
    AudioFadeCurve::CrossfadeLevels wetDry (paramValue (reverbMix));

    juce::Reverb::Parameters params;
    params.roomSize = paramValue (reverbSize);
    params.damping = paramValue (reverbDamping);
    params.width = paramValue (reverbWidth);
    params.wetLevel = wetDry.gain1;
    params.dryLevel = wetDry.gain2;
    params.freezeMode = 0;

    reverb.setParameters (params);

    // Delay
    float delayTime = (delayValue.get()) / (currentTempo / 60.0f);
    delay->setParams (delayTime,
                      juce::Decibels::decibelsToGain (paramValue (delayFeedback)),
                      juce::Decibels::decibelsToGain (paramValue (delayCrossfeed)),
                      paramValue (delayMix));

    // Chorus
    chorus->setParams (paramValue (chorusSpeed),
                       paramValue (chorusDepth),
                       paramValue (chorusWidth),
                       paramValue (chorusMix));
}

//==============================================================================
// Restore complete plugin state from preset or project data including all parameters and modulation
// Called when loading presets or projects, rebuilds entire plugin state including modulation matrix
void FourOscPlugin::restorePluginStateFromValueTree (const juce::ValueTree& v)
{
    copyPropertiesToCachedValues (v, ampAttackValue, ampDecayValue, ampSustainValue, ampReleaseValue, ampVelocityValue, filterAttackValue,
                                  filterDecayValue, filterSustainValue, filterReleaseValue, filterFreqValue, filterResonanceValue,
                                  filterAmountValue, filterKeyValue, filterVelocityValue, distortionValue, reverbSizeValue,
                                  reverbDampingValue, reverbWidthValue, reverbMixValue, delayValue, delayFeedbackValue, delayCrossfeedValue,
                                  delayMixValue, chorusSpeedValue, chorusDepthValue, chorusWidthValue, chorusMixValue, legatoValue,
                                  masterLevelValue, voiceModeValue, voicesValue, filterTypeValue, filterSlopeValue,
                                  ampAnalogValue, distortionOnValue, reverbOnValue, delayOnValue, chorusOnValue);

    auto um = getUndoManager();

    for (auto p : oscParams)
        p->restorePluginStateFromValueTree (v);

    for (auto p : lfoParams)
        p->restorePluginStateFromValueTree (v);

    for (auto p : modEnvParams)
        p->restorePluginStateFromValueTree (v);

    auto mm = state.getChildWithName (IDs::MODMATRIX);
    if (mm.isValid())
        state.removeChild (mm, um);

    mm = v.getChildWithName (IDs::MODMATRIX);

    if (mm.isValid())
        state.addChild (mm.createCopy(), -1, um);

    valueTreePropertyChanged (state, IDs::voiceMode);

    for (auto p : getAutomatableParameters())
        p->updateFromAttachedValue();
}

// Convert modulation source identifier to human-readable name for GUI modulation source lists
// Used by GUI components to display modulation source names in dropdowns and modulation displays
juce::String FourOscPlugin::modulationSourceToName (ModSource src)
{
    switch (src)
    {
        case lfo1:          return TRANS("LFO 1");
        case lfo2:          return TRANS("LFO 2");
        case env1:          return TRANS("Envelope 1");
        case env2:          return TRANS("Envelope 2");
        case mpePressure:   return TRANS("MPE Pressure");
        case mpeTimbre:     return TRANS("MPE Timbre");
        case midiNoteNum:   return TRANS("MIDI Note Number");
        case midiVelocity:  return TRANS("MIDI Velocity");
        case none:
        case ccBankSelect:
        case ccPolyMode:
        case numModSources:
        default:
        {
            if (src >= ccBankSelect && src <= ccPolyMode)
            {
                auto prefix = juce::String ("CC#") + juce::String ((int)(src - ccBankSelect));
                auto name = juce::String (juce::MidiMessage::getControllerName (src - ccBankSelect));

                if (name.isEmpty())
                    return prefix;

                return prefix + " " + name;
            }

            jassertfalse;
            return {};
        }
    }
}

// Convert modulation source enum to string identifier for value tree storage and preset saving
// Used when saving modulation assignments to ensure proper serialization and deserialization
juce::String FourOscPlugin::modulationSourceToID (FourOscPlugin::ModSource src)
{
    switch (src)
    {
        case lfo1:          return "lfo1";
        case lfo2:          return "lfo2";
        case env1:          return "env1";
        case env2:          return "env2";
        case mpePressure:   return "mpePressure";
        case mpeTimbre:     return "mpeTimbre";
        case midiNoteNum:   return "midiNote";
        case midiVelocity:  return "midiVelocity";
        case none:
        case ccBankSelect:
        case ccPolyMode:
        case numModSources:
        default:
        {
            if (src >= ccBankSelect && src <= ccPolyMode)
                return "cc" + juce::String (int (src - ccBankSelect));

            jassertfalse;
            return {};
        }
    }
}

// Convert string identifier back to modulation source enum when loading presets or projects
// Used during preset loading and state restoration to rebuild modulation matrix from saved data
FourOscPlugin::ModSource FourOscPlugin::idToModulationSource (juce::String idStr)
{
    if (idStr == "lfo1")            return lfo1;
    if (idStr == "lfo2")            return lfo2;
    if (idStr == "env1")            return env1;
    if (idStr == "env2")            return env2;
    if (idStr == "mpePressure")     return mpePressure;
    if (idStr == "mpeTimbre")       return mpeTimbre;
    if (idStr == "midiNote")        return midiNoteNum;
    if (idStr == "midiVelocity")    return midiVelocity;

    if (idStr.startsWith ("cc"))
        return ModSource (ccBankSelect + idStr.getTrailingIntValue());

    return none;
}

// Collect real-time modulated parameter values from all active voices for GUI modulation visualization
// Used by GUI controls to show live modulation activity, aggregates values from all playing voices
juce::Array<float> FourOscPlugin::getLiveModulationPositions (AutomatableParameter::Ptr param)
{
    juce::Array<float> positions;

    // Filter frequency is a special case, not only do we want to show modulation,
    // also want to show effect of key tracking and filter envelope
    if (param->paramID == "filterFreq" && isModulated (param))
    {
        for (int i = 0; i < getNumVoices(); i++)
            if (auto fov = dynamic_cast<FourOscVoice*> (getVoice (i)))
                fov->getLiveFilterFrequency (positions);
    }
    else if (isModulated (param))
    {
        for (int i = 0; i < getNumVoices(); i++)
            if (auto fov = dynamic_cast<FourOscVoice*> (getVoice (i)))
                fov->getLiveModulationPositions (param, positions);
    }
    return positions;
}

// Check if parameter has any active modulation sources or special modulation (filter key tracking)
// Used by GUI to determine whether to show modulation indicators on parameter controls
bool FourOscPlugin::isModulated (AutomatableParameter::Ptr param)
{
    if (param->paramID == "filterFreq" && (filterKeyValue.get() != 0 || filterAmountValue.get() != 0 ))
        return true;

    auto itr = modMatrix.find (param.get());
    if (itr != modMatrix.end())
    {
        for (auto d : itr->second.depths)
            if (d >= -1.0f)
                return true;

        return false;
    }
    jassertfalse;
    return false;
}

// Get list of all modulation sources currently assigned to a parameter for GUI modulation editing
// Used by modulation matrix GUI to show which sources are modulating each parameter
juce::Array<FourOscPlugin::ModSource> FourOscPlugin::getModulationSources (AutomatableParameter::Ptr param)
{
    auto itr = modMatrix.find (param.get());
    if (itr != modMatrix.end())
    {
        juce::Array<ModSource> res;
        for (int s = lfo1; s < numModSources; s++)
            if (itr->second.depths[s] >= -1.0f)
                res.add ((ModSource) s);

        return res;
    }
    jassertfalse;
    return {};
}

// Get modulation depth value for specific source-parameter combination in modulation matrix
// Used by GUI modulation controls to display and edit current modulation depth settings
float FourOscPlugin::getModulationDepth (FourOscPlugin::ModSource src, AutomatableParameter::Ptr param)
{
    auto itr = modMatrix.find (param.get());
    if (itr != modMatrix.end())
        return itr->second.depths[src];
    jassertfalse;
    return -1000;
}

// Set modulation depth for source-parameter pair, updates internal modulation matrix and cached info
// Called by GUI modulation controls when user adjusts modulation amounts, updates real-time processing
void FourOscPlugin::setModulationDepth (FourOscPlugin::ModSource src, AutomatableParameter::Ptr param, float depth)
{
    auto itr = modMatrix.find (param.get());
    if (itr != modMatrix.end())
    {
        itr->second.depths[src] = depth;
        itr->second.updateCachedInfo();
        return;
    }
    jassertfalse;
}

// Remove modulation assignment between source and parameter, clears from modulation matrix
// Called by GUI when user removes modulation assignments or resets modulation routing
void FourOscPlugin::clearModulation (ModSource src, AutomatableParameter::Ptr param)
{
    auto itr = modMatrix.find (param.get());
    if (itr != modMatrix.end())
    {
        itr->second.depths[src] = -1000.0f;
        itr->second.updateCachedInfo();
        return;
    }
    jassertfalse;
}

// Get parameter value with smoothing applied for glitch-free audio processing at plugin level
// Used for effect parameters that need smoothing but don't use the voice-level modulation system
float FourOscPlugin::paramValue (AutomatableParameter::Ptr param)
{
    jassert (param != nullptr);
    if (param == nullptr)
        return 0.0f;

    auto smoothItr = smoothers.find (param.get());
    if (smoothItr == smoothers.end())
        return param->getCurrentValue();

    float val = param->getCurrentNormalisedValue();
    smoothItr->second.setValue (val);
    return param->valueRange.convertFrom0to1 (smoothItr->second.getCurrentValue());
}

}} // namespace tracktion { inline namespace engine
