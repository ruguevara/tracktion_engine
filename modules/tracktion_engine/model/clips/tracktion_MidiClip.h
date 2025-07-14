/*
    ,--.                     ,--.     ,--.  ,--.
  ,-'  '-.,--.--.,--,--.,---.|  |,-.,-'  '-.`--' ,---. ,--,--,      Copyright 2024
  '-.  .-'|  .--' ,-.  | .--'|     /'-.  .-',--.| .-. ||      \   Tracktion Software
    |  |  |  |  \ '-'  \ `--.|  \  \  |  |  |  |' '-' '|  ||  |       Corporation
    `---' `--'   `--`--'`---'`--'`--' `---' `--' `---' `--''--'    www.tracktion.com

    Tracktion Engine uses a GPL/commercial licence - see LICENCE.md for details.
*/

namespace tracktion { inline namespace engine
{

//==============================================================================
/**
*/
class MidiClip  : public Clip
{
public:
    //==============================================================================
    MidiClip() = delete;
    /** Constructs MidiClip from saved state, used during project loading. */
    MidiClip (const juce::ValueTree&, EditItemID, ClipOwner&);
    ~MidiClip() override;

    using Ptr = juce::ReferenceCountedObjectPtr<MidiClip>;

    /** Copies all properties and MIDI data from another clip, used for clip duplication. */
    void cloneFrom (Clip*) override;

    /** Returns the audio track this MIDI clip is on, cast from ClipOwner. */
    AudioTrack* getAudioTrack() const;

    //==============================================================================
    /** Returns the current take's MIDI sequence for editing and playback. */
    MidiList& getSequence() const noexcept;
    /** Returns cached looped sequence for playback, creates if needed. Used by audio engine. */
    MidiList& getSequenceLooped();
    /** Creates a new looped sequence from source, handling loop boundaries and repetitions. */
    std::unique_ptr<MidiList> createSequenceLooped (MidiList& sourceSequence);

    /** Returns currently selected MIDI events for UI editing operations. */
    const SelectedMidiEvents* getSelectedEvents() const             { return selectedEvents; }

    //==============================================================================
    /** Can be used to disable proxy sequence generation for this clip.
        N.B. If disabled, the audio engine will perform quantisation and groove
        adjustments in real time which may use more CPU.
    */
    void setUsesProxy (bool canUseProxy) noexcept   { proxyAllowed = canUseProxy; }

    /** Retuns true if this clip can use a proxy sequence. */
    bool canUseProxy() const noexcept               { return proxyAllowed; }

    //==============================================================================
    /** @internal */
    std::shared_ptr<LaunchHandle> getLaunchHandle() override;
    /** @internal */
    void setUsesGlobalLaunchQuatisation (bool useGlobal) override           { useClipLaunchQuantisation = ! useGlobal; }
    /** @internal */
    bool usesGlobalLaunchQuatisation() override                             { return ! useClipLaunchQuantisation; }
    /** @internal */
    LaunchQuantisation* getLaunchQuantisation() override;
    /** @internal */
    FollowActions* getFollowActions() override;

    //==============================================================================
    /** Auto-adjusts MIDI editor's vertical zoom to fit all notes in view.
        Uses AudioTrack::setMidiVerticalPos. Called by UI. */
    void scaleVerticallyToFit();

    /** Returns true if clip has MIDI data (at least one take exists). */
    bool hasValidSequence() const noexcept                          { return channelSequence.size() > 0; }

    /** Returns MIDI channel of current take, or default if no sequences exist. */
    MidiChannel getMidiChannel() const                              { return hasValidSequence() ? getSequence().getMidiChannel() : MidiChannel(); }
    /** Sets MIDI channel for current take's sequence. */
    void setMidiChannel (MidiChannel newChannel)                    { getSequence().setMidiChannel (newChannel); }

    /** Sets whether the clip should send MPE MIDI rather than single channel. */
    void setMPEMode (bool shouldUseMPE)                             { mpeMode = shouldUseMPE; }
    bool getMPEMode() const noexcept                                { return mpeMode; }

    /** Returns true if this clip represents a rhythm instrument (e.g. MIDI channel 10) */
    bool isRhythm() const noexcept                                  { return getMidiChannel().getChannelNumber() == 10; }

    //==============================================================================
    QuantisationType& getQuantisation() const noexcept              { return *quantisation; }
    /** Sets quantisation type, invalidates cached sequences. Used by quantisation UI. */
    void setQuantisation (const QuantisationType& newType);

    juce::String getGrooveTemplate() const noexcept                 { return grooveTemplate; }
    void setGrooveTemplate (const juce::String& templateName)       { grooveTemplate = templateName; }

    /** Returns true if groove template requires strength parameter. */
    bool usesGrooveStrength() const;

    float getGrooveStrength() const                                 { return grooveStrength; }
    void setGrooveStrength (float g)                                { grooveStrength = juce::jlimit (0.0f, 1.0f, g); }

    //==============================================================================
    /** Merges MIDI data into current take, used during recording and MIDI import. */
    void mergeInMidiSequence (juce::MidiMessageSequence&, MidiList::NoteAutomationType);

    /** Adds new take with MIDI data, used during punch-in recording workflows. */
    void addTake (juce::MidiMessageSequence&, MidiList::NoteAutomationType);

    /** Extends clip start time backwards, moving notes to maintain relative timing. Used when merging MIDI that extends beyond clip boundaries. */
    void extendStart (TimePosition newStartTime);

    /** Removes MIDI events outside clip boundaries, used during clip editing operations. */
    void trimBeyondEnds (bool beyondStart, bool beyondEnd, juce::UndoManager*);

    /** Lengthens or shortens a note to touch the next note (legato style). Used by MIDI editing UI.
        If the note is the last in the sequence, it will use the maxEndBeat as its end.

        @note notesToUse must be in ascending note start order.
    */
    void legatoNote (MidiNote& note, const juce::Array<MidiNote*>& notesToUse,
                     BeatPosition maxEndBeat, juce::UndoManager&);

    //==============================================================================
    float getVolumeDb() const                       { return level->dbGain.get(); }
    void setVolumeDb (float v)                      { level->dbGain = juce::jlimit (-100.0f, 0.0f, v); }

    bool isSendingBankChanges() const noexcept      { return sendBankChange; }
    /** Enables/disables bank change messages before program changes. Used by instrument setup. */
    void setSendingBankChanges (bool sendBank);

    bool isMuted() const override                   { return level->mute.get(); }
    void setMuted (bool m) override                 { level->mute = m; }

    /** Returns current level state for live performance monitoring. */
    LiveClipLevel getLiveClipLevel();

    //==============================================================================
    /** Initializes clip after construction, sets up default sequences. */
    void initialise() override;
    /** Always returns true for MIDI clips. */
    bool isMidi() const override                    { return true; }
    /** Rescales clip timing around pivot point, used by tempo changes. */
    void rescale (TimePosition pivotTimeInEdit, double factor) override;
    /** Returns true if clip can be added to the specified owner track. */
    bool canBeAddedTo (ClipOwner&) override;
    /** Returns description for UI selection lists. */
    juce::String getSelectableDescription() override;
    /** Returns default color for MIDI clips in UI. */
    juce::Colour getDefaultColour() const override;

    /** Removes all takes except the current one, used for take management cleanup. */
    void clearTakes() override;
    /** Returns true if clip has multiple takes (more than one sequence). */
    bool hasAnyTakes() const override               { return channelSequence.size() > 1; }
    /** Returns total number of takes, optionally including composite takes. */
    int getNumTakes (bool includeComps) override;
    /** Returns user-friendly descriptions of all takes for UI display. */
    juce::StringArray getTakeDescriptions() const override;
    /** Switches to specified take index, used by take selection UI. */
    void setCurrentTake (int takeIndex) override;
    /** Returns index of currently active take. */
    int getCurrentTake() const override             { return currentTake; }
    /** Returns true if current take is a composite (comp) take. */
    bool isCurrentTakeComp() override;
    /** Shows confirmation dialog and deletes unused takes to save memory. */
    void deleteAllUnusedTakesConfirmingWithUser();
    /** Splits takes into separate clips, optionally on new tracks. */
    Clip::Array unpackTakes (bool toNewTracks) override;
    /** Returns MIDI sequence for specified take index, used for take management. */
    MidiList* getTakeSequence (int takeIndex) const { return channelSequence[takeIndex]; }

    /** MIDI clips always support looping. */
    bool canLoop() const override                   { return true; }
    /** Returns true if clip has loop length set. */
    bool isLooping() const override                 { return loopLengthBeats > BeatDuration(); }
    /** MIDI clips use beat-based looping when looping is enabled. */
    bool beatBasedLooping() const override          { return isLooping(); }
    /** Sets number of loop repetitions, used by loop editing UI. */
    void setNumberOfLoops (int) override;
    /** Disables looping for this clip, used by loop editing UI. */
    void disableLooping() override;
    /** Sets loop range in time units, used by loop editing UI. */
    void setLoopRange (TimeRange) override;
    /** Sets loop range in beat units, used by loop editing UI. */
    void setLoopRangeBeats (BeatRange) override;
    /** Returns loop start position in beats. */
    BeatPosition getLoopStartBeats() const override       { return loopStartBeats; }
    /** Returns loop length in beats. */
    BeatDuration getLoopLengthBeats() const override      { return loopLengthBeats; }
    /** Returns loop start position in time units. */
    TimePosition getLoopStart() const override;
    /** Returns loop length in time units. */
    TimeDuration getLoopLength() const override;

    enum class LoopedSequenceType : int
    {
        loopRangeDefinesAllRepetitions          = 0,    /**< The looped sequence is the same for all repetitions including the first. */
        loopRangeDefinesSubsequentRepetitions   = 1     /**< The first section is the whole sequence, subsequent repitions are determined by the loop range. */
    };

    juce::CachedValue<LoopedSequenceType> loopedSequenceType;

    /** Returns comp manager for creating composite takes from multiple recordings. */
    MidiCompManager& getCompManager();

    //==============================================================================
    /** Returns pattern generator for algorithmic MIDI generation, if available. */
    PatternGenerator* getPatternGenerator() override;
    /** Called when tempo/pitch automation changes to update cached sequences. */
    void pitchTempoTrackChanged() override;

    //==============================================================================
    /** RAII helper to temporarily limit MIDI operations to selected events only. Used by editing UI. */
    struct ScopedEventsList
    {
        /** Sets selectedEvents for the duration of this object's lifetime. */
        ScopedEventsList (MidiClip&, SelectedMidiEvents*);
        /** Restores previous selectedEvents state. */
        ~ScopedEventsList();

    private:
        MidiClip& clip;
        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ScopedEventsList)
    };

protected:
    /** Handles property changes in clip state, invalidates caches as needed. */
    void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&) override;
    /** Handles addition of child elements (takes, automation, etc.). */
    void valueTreeChildAdded (juce::ValueTree&, juce::ValueTree&) override;
    /** Handles removal of child elements, cleans up references. */
    void valueTreeChildRemoved (juce::ValueTree&, juce::ValueTree&, int) override;

private:
    //==============================================================================
    friend class MidiNote;

    //==============================================================================
    /** Array of MIDI sequences, one per take/comp. A comp is a composite take combining best parts from multiple takes. */
    juce::OwnedArray<MidiList> channelSequence;
    /** Shared level control for volume and mute state. */
    std::shared_ptr<ClipLevel> level { std::make_shared<ClipLevel>() };
    /** Handle for live performance launch control. */
    std::shared_ptr<LaunchHandle> launchHandle;
    /** Whether to use clip-specific launch quantisation instead of global. */
    juce::CachedValue<bool> useClipLaunchQuantisation;
    /** Clip-specific launch quantisation settings. */
    std::unique_ptr<LaunchQuantisation> launchQuantisation;
    /** Actions to perform after clip finishes playing. */
    std::unique_ptr<FollowActions> followActions;

    /** Whether proxy sequence generation is allowed for performance optimization. */
    juce::CachedValue<int> proxyAllowed;
    /** Index of the currently active take in channelSequence. */
    juce::CachedValue<int> currentTake;
    /** Strength of groove template application (0.0-1.0). */
    juce::CachedValue<float> grooveStrength;
    /** Start position of loop in beats. */
    juce::CachedValue<BeatPosition> loopStartBeats;
    /** Length of loop in beats. */
    juce::CachedValue<BeatDuration> loopLengthBeats;
    /** Original clip length before looping was applied. */
    juce::CachedValue<BeatDuration> originalLength;
    /** Quantisation settings for MIDI notes. */
    std::unique_ptr<QuantisationType> quantisation;
    /** Whether to send program change messages when clip starts playing (switches instrument/sound). */
    juce::CachedValue<bool> sendPatch;
    /** Whether to send bank change messages before program changes (accesses extended sound banks). */
    juce::CachedValue<bool> sendBankChange;
    /** Whether to use MPE (MIDI Polyphonic Expression) mode. */
    juce::CachedValue<bool> mpeMode;
    /** Name of the groove template to apply. */
    juce::CachedValue<juce::String> grooveTemplate;

    /** Flag to warn users when clip uses multiple MIDI channels (may cause confusion). */
    bool shouldWarnAboutMultiChannel = false;
    /** Pointer to currently selected MIDI events for editing. */
    SelectedMidiEvents* selectedEvents = nullptr;

    /** Cached looped sequence, generated on-demand for performance. */
    mutable std::unique_ptr<MidiList> cachedLoopedSequence;
    /** Manager for MIDI comps (composite takes). */
    MidiCompManager::Ptr midiCompManager;

    //==============================================================================
    /** Sets currently selected MIDI events for UI editing, used by ScopedEventsList. */
    void setSelectedEvents (SelectedMidiEvents* events)     { selectedEvents = events; }

    //==============================================================================
    /** Finds MidiList corresponding to ValueTree state, used for take management. */
    MidiList* getMidiListForState (const juce::ValueTree&);
    /** Invalidates cached looped sequence when clip properties change, ensures performance optimization. */
    void clearCachedLoopSequence();

    //==============================================================================
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MidiClip)
};


//==============================================================================
//==============================================================================
/** Copies a zero-time origin based MIDI sequence in to a MidiClip.
    This zill extend the start and end of the clip to fit the whole sequence.
    @param MidiClip             The destination clip
    @param MidiMessageSequence  The zero-based MIDI sequence
    @param offsetToApply        An offset to apply to all MIDI message timestamps
    @param NoteAutomationType   Whether to use standard MIDI or MPE
*/
void mergeInMidiSequence (MidiClip&, juce::MidiMessageSequence, TimeDuration offsetToApply,
                          MidiList::NoteAutomationType);


}} // namespace tracktion { inline namespace engine

namespace juce
{
    template <>
    struct VariantConverter<tracktion::engine::MidiClip::LoopedSequenceType>
    {
        static tracktion::engine::MidiClip::LoopedSequenceType fromVar (const var& v)   { return (tracktion::engine::MidiClip::LoopedSequenceType) static_cast<int> (v); }
        static var toVar (tracktion::engine::MidiClip::LoopedSequenceType v)            { return static_cast<int> (v); }
    };
}
