/*
    ,--.                     ,--.     ,--.  ,--.
  ,-'  '-.,--.--.,--,--.,---.|  |,-.,-'  '-.`--' ,---. ,--,--,      Copyright 2018
  '-.  .-'|  .--' ,-.  | .--'|     /'-.  .-',--.| .-. ||      \   Tracktion Software
    |  |  |  |  \ '-'  \ `--.|  \  \  |  |  |  |' '-' '|  ||  |       Corporation
    `---' `--'   `--`--'`---'`--'`--' `---' `--' `---' `--''--'    www.tracktion.com
*/


namespace tracktion { inline namespace engine
{

class FODelay;
class FOChorus;

//==============================================================================
/** Smooths a value between 0 and 1 at a constant rate */
template <class T>
class ValueSmoother
{
public:
    void reset (double sr, double time)     { delta = 1.0f / T (sr * time); }
    T getCurrentValue()                     { return currentValue; }
    void setValue (T v)                     { targetValue = v; }
    void snapToValue()                      { currentValue = targetValue; }

    void process (int n)
    {
        if (targetValue != currentValue)
            for (int i = 0; i < n; i++)
                getNextValue();
    }

    T getNextValue()
    {
        if (currentValue < targetValue)
            currentValue = juce::jmin (targetValue, currentValue + delta);
        else if (currentValue > targetValue)
            currentValue = juce::jmax (targetValue, currentValue - delta);
        return currentValue;
    }

    void setValueUnsmoothed (T v)
    {
        targetValue = v;
        currentValue = v;
    }

private:
    T delta = 0;            // Step size per sample for smooth transitions between values
    T targetValue = 0;      // Target value to smooth towards, set by setValue() calls
    T currentValue = 0;     // Current smoothed value, updated each sample until target is reached
};

//==============================================================================
class SimpleLFO
{
public:
    //==============================================================================
    enum WaveShape : int
    {
        none,
        sine,
        triangle,
        sawUp,
        sawDown,
        square,
        random
    };

    //==============================================================================
    struct Parameters
    {
        Parameters() = default;
        Parameters (float frequencyIn, float phaseOffsetIn, float offsetIn, float depthIn,
                    WaveShape waveShapeIn, float pulseWidthIn) :
            waveShape (waveShapeIn), frequency (frequencyIn), phaseOffset (phaseOffsetIn),
            offset (offsetIn), depth (depthIn), pulseWidth (pulseWidthIn) {}

        WaveShape waveShape = sine;
        float frequency = 0, phaseOffset = 0, offset = 0, depth = 0, pulseWidth = 0;
    };

    //==============================================================================
    void setSampleRate (double newSampleRate)       { sampleRate = newSampleRate; }
    void setParameters (Parameters newParameters)   { parameters = newParameters; }
    void reset()                                    { phase = 0; }

    void process (int numSamples)
    {
        double step = 0.0;
        if (parameters.frequency > 0.001f)
            step = parameters.frequency / sampleRate;

        for (int i = 0; i < numSamples; i++)
        {
            phase += step;
            if (phase >= 1.0)
                phase -= 1.0;

            float localPhase = 0.0f;

            if (parameters.phaseOffset != 0)
                localPhase = wrapValue ((float) std::fmod (phase + parameters.phaseOffset, 1.0f), 1.0f);
            else
                localPhase = wrapValue ((float) phase, 1.0f);

            jassert (localPhase >= 0.0f && localPhase <= 1.0f);

            if (parameters.waveShape == random)
            {
                if (localPhase < lastLocalPhase)
                    lastRandomVal = randomSource.nextFloat() * 2.0f - 1.0f;

                lastLocalPhase = localPhase;
            }
        }
    }

    float getCurrentValue()
    {
        float val = 0.0f, localPhase = 0.0f;

        if (parameters.phaseOffset != 0)
            localPhase = wrapValue ((float) std::fmod (phase + parameters.phaseOffset, 1.0f), 1.0f);
        else
            localPhase = wrapValue ((float) phase, 1.0f);

        jassert (localPhase >= 0.0f && localPhase <= 1.0f);

        switch (parameters.waveShape)
        {
            case none:      val = 0; break;
            case sine:      val = std::sin (localPhase * juce::MathConstants<float>::pi * 2); break;
            case triangle:  val = (localPhase < 0.5f) ? (4.0f * localPhase - 1.0f) : (-4.0f * localPhase + 3.0f); break;
            case sawUp:     val = localPhase * 2.0f - 1.0f; break;
            case sawDown:   val = (1.0f - localPhase) * 2.0f - 1.0f; break;
            case square:    val = (localPhase < parameters.pulseWidth) ? 1.0f : -1.0f; break;
            case random:    val = lastRandomVal; break;
        }

        return (val * parameters.depth + parameters.offset);
    }

private:
    Parameters parameters;          // Current LFO settings including waveform, frequency, depth, etc.

    double phase = 0;               // Main LFO phase accumulator (0-1), incremented each sample
    double lastLocalPhase = 1;      // Previous phase value for detecting random waveform triggers
    double sampleRate = 0;          // Sample rate used for frequency to phase increment conversion
    float lastRandomVal = 0;        // Last random value generated, held until next trigger

    juce::Random randomSource {1};  // Random number generator with fixed seed for reproducible randomness

    inline float wrapValue (float v, float range)
    {
        while (v >= range) v -= range;
        while (v <  0)     v += range;
        return v;
    }
};

//==============================================================================
class FourOscPlugin  : public Plugin,
                       private juce::MPESynthesiser,
                       private juce::AsyncUpdater,
                       private LevelMeasurer::Client
{
public:
    FourOscPlugin (PluginCreationInfo);
    ~FourOscPlugin() override;

    bool isMono() const                                 { return voiceModeValue.get() == 0; }
    bool isLegato() const                               { return voiceModeValue.get() == 1; }
    bool isPoly() const                                 { return voiceModeValue.get() == 2; }

    //==============================================================================
    static const char* getPluginName()                  { return NEEDS_TRANS("4OSC"); }
    static const char* xmlTypeName;

    juce::String getName() const override               { return TRANS("4OSC"); }
    juce::String getPluginType() override               { return xmlTypeName; }
    juce::String getShortName (int) override            { return "4OSC"; }
    juce::String getSelectableDescription() override    { return TRANS("4OSC Plugin"); }

    int getNumOutputChannelsGivenInputs (int numInputChannels) override { return juce::jmin (numInputChannels, 2); }

    void initialise (const PluginInitialisationInfo&) override;
    void deinitialise() override;

    void reset() override;
    void midiPanic() override;

    void applyToBuffer (const PluginRenderContext&) override;

    //==============================================================================
    bool takesMidiInput() override                      { return true; }
    bool takesAudioInput() override                     { return false; }
    bool isSynth() override                             { return true; }
    bool producesAudioWhenNoAudioInput() override       { return true; }
    double getTailLength() const override               { return ampRelease->getCurrentValue(); }

    void restorePluginStateFromValueTree (const juce::ValueTree&) override;

    float getCurrentTempo()                             { return currentTempo; }

private:
    std::unordered_map<juce::String, juce::String> labels;  // Parameter unit labels (Hz, dB, %, ms, etc.) for display

public:
    //==============================================================================
    struct OscParams
    {
        OscParams (FourOscPlugin& plugin, int oscNum);
        void attach();
        void detach();

        juce::CachedValue<int> waveShapeValue, voicesValue;     // Cached waveform type and voice count for unison
        juce::CachedValue<float> tuneValue, fineTuneValue;      // Cached coarse/fine pitch adjustment values
        juce::CachedValue<float> levelValue, pulseWidthValue;   // Cached amplitude and pulse width settings
        juce::CachedValue<float> detuneValue, spreadValue, panValue;  // Cached unison detune, spread, and pan position

        AutomatableParameter::Ptr tune, fineTune, level, pulseWidth, detune, spread, pan;  // Automatable parameter objects for DAW integration

        void restorePluginStateFromValueTree (const juce::ValueTree& v)
        {
            copyPropertiesToCachedValues (v, tuneValue, fineTuneValue, levelValue, pulseWidthValue,
                                          detuneValue, spreadValue, panValue, waveShapeValue, voicesValue);
        }
    };

    juce::OwnedArray<OscParams> oscParams;      // Parameter sets for all four oscillators, indexed 0-3

    //==============================================================================
    struct LFOParams
    {
        LFOParams (FourOscPlugin& plugin, int lfoNum);
        void attach();
        void detach();

        juce::CachedValue<bool> syncValue;                      // Whether LFO is tempo-synced to host
        juce::CachedValue<int> waveShapeValue;                  // LFO waveform type (sine, triangle, etc.)
        juce::CachedValue<float> rateValue, beatValue;          // LFO rate in Hz and beat divisions
        juce::CachedValue<float> depthValue;                    // LFO modulation depth amount

        AutomatableParameter::Ptr rate, depth;                  // Automatable LFO parameters for DAW control

        void restorePluginStateFromValueTree (const juce::ValueTree& v)
        {
            copyPropertiesToCachedValues (v, rateValue, beatValue, depthValue, waveShapeValue, syncValue);
        }
    };

    juce::OwnedArray<LFOParams> lfoParams;      // Parameter sets for both modulation LFOs (LFO1, LFO2)

    //==============================================================================
    struct MODEnvParams
    {
        MODEnvParams (FourOscPlugin& plugin, int envNum);
        void attach();
        void detach();

        juce::CachedValue<float> modAttackValue, modDecayValue, modSustainValue, modReleaseValue;  // Cached ADSR values for modulation envelopes
        AutomatableParameter::Ptr modAttack, modDecay, modSustain, modRelease;                    // Automatable envelope parameters for DAW integration

        void restorePluginStateFromValueTree (const juce::ValueTree& v)
        {
            copyPropertiesToCachedValues (v, modAttackValue, modDecayValue, modSustainValue, modReleaseValue);
        }
    };

    juce::OwnedArray<MODEnvParams> modEnvParams; // Parameter sets for both modulation envelopes (ENV1, ENV2)

    //==============================================================================
    // Amplitude envelope cached values - attack, decay, sustain, release times and velocity sensitivity
    juce::CachedValue<float> ampAttackValue, ampDecayValue, ampSustainValue, ampReleaseValue, ampVelocityValue;
    
    // Filter envelope and parameters - ADSR times, cutoff frequency, resonance, envelope amount, key tracking, velocity sensitivity
    juce::CachedValue<float> filterAttackValue, filterDecayValue, filterSustainValue, filterReleaseValue, filterFreqValue,
                             filterResonanceValue, filterAmountValue, filterKeyValue, filterVelocityValue;
    
    juce::CachedValue<int> filterTypeValue, filterSlopeValue;   // Filter type (LP/HP/BP/Notch) and slope (12/24 dB/oct)
    juce::CachedValue<bool> ampAnalogValue;                     // Whether to use analog-modeled envelope curves

    AutomatableParameter::Ptr ampAttack, ampDecay, ampSustain, ampRelease, ampVelocity;          // Amplitude envelope automatable parameters
    AutomatableParameter::Ptr filterAttack, filterDecay, filterSustain, filterRelease;           // Filter envelope automatable parameters
    AutomatableParameter::Ptr filterFreq, filterResonance, filterAmount, filterKey, filterVelocity; // Filter control automatable parameters

    juce::CachedValue<bool> distortionOnValue, reverbOnValue, delayOnValue, chorusOnValue; // Effect enable/disable states

    juce::CachedValue<float> distortionValue;   // Cached distortion drive amount
    AutomatableParameter::Ptr distortion;       // Automatable distortion parameter for DAW control

    juce::CachedValue<float> reverbSizeValue, reverbDampingValue, reverbWidthValue, reverbMixValue; // Cached reverb parameters
    AutomatableParameter::Ptr reverbSize, reverbDamping, reverbWidth, reverbMix;                    // Automatable reverb controls

    juce::CachedValue<float> delayValue, delayFeedbackValue, delayCrossfeedValue, delayMixValue; // Cached delay parameters including time, feedback, crossfeed, mix
    AutomatableParameter::Ptr delayFeedback, delayCrossfeed, delayMix;                          // Automatable delay controls (time is tempo-synced)

    juce::CachedValue<float> chorusSpeedValue, chorusDepthValue, chorusWidthValue, chorusMixValue; // Cached chorus LFO speed, depth, stereo width, mix
    AutomatableParameter::Ptr chorusSpeed, chorusDepth, chorusWidth, chorusMix;                    // Automatable chorus effect controls

    juce::CachedValue<int> voiceModeValue, voicesValue;         // Voice mode (mono/legato/poly) and polyphony count
    juce::CachedValue<float> legatoValue, masterLevelValue;     // Legato portamento time and master output level
    AutomatableParameter::Ptr legato, masterLevel;             // Automatable voice and master level controls

    //==============================================================================

    enum ModSource : int
    {
        none = -1,
        lfo1 = 0,
        lfo2,
        env1,
        env2,
        mpePressure,
        mpeTimbre,
        midiNoteNum,
        midiVelocity,
        ccBankSelect,
        ccPolyMode = ccBankSelect + 127,
        numModSources
    };

    juce::String modulationSourceToName (ModSource src);
    juce::String modulationSourceToID (ModSource src);
    ModSource idToModulationSource (juce::String idStr);
    bool isModulated (AutomatableParameter::Ptr param);
    juce::Array<float> getLiveModulationPositions (AutomatableParameter::Ptr param);
    juce::Array<ModSource> getModulationSources (AutomatableParameter::Ptr param);
    float getModulationDepth (ModSource src, AutomatableParameter::Ptr param);
    void setModulationDepth (ModSource src, AutomatableParameter::Ptr param, float depth);
    void clearModulation (ModSource src, AutomatableParameter::Ptr param);

    struct ModAssign
    {
    public:
        ModAssign()
        {
            for (auto& d : depths)
                d = -1000.0f;
        }

        inline void updateCachedInfo()
        {
            int f = -1, l = -1;
            for (int i = 0; i < juce::numElementsInArray (depths); i++)
            {
                if (depths[i] >= -1.0f && f == -1)  f = i;
                if (depths[i] >= -1.0)              l = i;
            }

            firstModIndex = f;
            lastModIndex  = l;
        }

        inline bool isModulated()
        {
            return firstModIndex != -1 && lastModIndex != -1;
        }

        int firstModIndex = -1, lastModIndex = -1; // Optimization indices for active modulation range
        float depths[numModSources] = {};           // Modulation depth values for each source (-1000 = inactive)
    };

    std::unordered_map<AutomatableParameter*, ModAssign> modMatrix;  // Complete modulation matrix mapping parameters to modulation sources
    float controllerValues[128] = {0};                                // Normalized MIDI CC values (0-1) for modulation sources

    float getLevel (int channel);

private:
    //==============================================================================
    void valueTreeChanged() override;
    void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&) override;
    void valueTreeChildAdded (juce::ValueTree&, juce::ValueTree&) override;
    void valueTreeChildRemoved (juce::ValueTree&, juce::ValueTree&, int) override;
    void handleAsyncUpdate() override;
    void handleController (int midiChannel, int controllerNumber, int controllerValue) override;

    void flushPluginStateToValueTree() override;

    void loadModMatrix();
    void setupTextFunctions();
    AutomatableParameter* addParam (const juce::String& paramID, const juce::String& name, juce::NormalisableRange<float> valueRange, juce::String label = {});

    void applyToBuffer (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi);
    void updateParams (juce::AudioBuffer<float>& buffer);
    void applyEffects (juce::AudioBuffer<float>& buffer);
    float paramValue (AutomatableParameter::Ptr param);

    tempo::Sequence::Position currentPos { createPosition (edit.tempoSequence) }; // Current timeline position for tempo-synced effects
    juce::Reverb reverb;                                    // Built-in JUCE reverb processor for reverb effect
    std::unique_ptr<FODelay> delay;                         // Custom stereo delay effect with ping-pong and crossfeed
    std::unique_ptr<FOChorus> chorus;                       // Custom chorus effect with LFO modulation
    std::unordered_map<AutomatableParameter*, ValueSmoother<float>> smoothers; // Parameter smoothers for effect controls

    bool flushingState = false;                             // Flag preventing recursive updates during state save/load
    float currentTempo = 0.0f;                              // Current host tempo in BPM for delay sync calculations
    LevelMeasurer levelMeasurer;                            // Audio level analyzer for GUI meters
    DbTimePair levels[2];                                   // Peak level storage for left/right channels with time stamps

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FourOscPlugin)
};

}} // namespace tracktion { inline namespace engine
