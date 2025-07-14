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

/** Container for MIDI events (notes, controllers, sysex) with automatic sorting and ValueTree persistence.
    Used by MidiClip for MIDI data storage, editing, and playback sequence generation.
*/
class MidiList
{
public:
    /** Creates empty MIDI list with default settings. */
    MidiList();
    /** Creates MIDI list from saved ValueTree state during project loading. */
    MidiList (const juce::ValueTree&, juce::UndoManager*);
    ~MidiList();

    /** Creates default ValueTree structure for new MIDI lists. */
    static juce::ValueTree createMidiList();

    /** Clears current list and copies all content from another list. Used for take replacement. */
    void copyFrom (const MidiList&, juce::UndoManager*);

    /** Adds copies of events from another list to this one. Used for merging takes. */
    void addFrom (const MidiList&, juce::UndoManager*);

    //==============================================================================
    enum class NoteAutomationType
    {
        none,       /**< No automation, add the sequence as plain MIDI with the channel of the clip. */
        expression  /**< Add the automation as EXP assuming the source sequence is MPE MIDI. */
    };

    //==============================================================================
    /** Returns all notes sorted by beat position. Used by UI and playback systems. */
    const juce::Array<MidiNote*>& getNotes() const;
    /** Returns all controller events sorted by beat position. Used by automation and playback. */
    const juce::Array<MidiControllerEvent*>& getControllerEvents() const;
    /** Returns all sysex events sorted by beat position. Used by device-specific MIDI. */
    const juce::Array<MidiSysexEvent*>& getSysexEvents() const;

    //==============================================================================
    /** Returns true if this list is attached to a MidiClip (has parent in ValueTree). */
    bool isAttachedToClip() const noexcept                          { return ! state.getParent().hasType (IDs::NA); }

    /** Marks this list as a composite (comp) take combining multiple recordings. */
    void setCompList (bool shouldBeComp) noexcept                   { isComp = shouldBeComp; }
    /** Returns true if this is a composite take. */
    bool isCompList() const noexcept                                { return isComp; }

    //==============================================================================
    /** Gets the list's MIDI channel number (1-16). Used during playback sequence generation. */
    MidiChannel getMidiChannel() const                              { return midiChannel; }

    /** Sets MIDI channel for all events in this list. Used when changing clip's MIDI channel. */
    void setMidiChannel (MidiChannel chanNum);

    /** Returns track name from imported MIDI file, used for clip naming. */
    juce::String getImportedMidiTrackName() const noexcept          { return importedName; }

    /** Returns filename of imported MIDI file, used for clip identification. */
    juce::String getImportedFileName() const noexcept               { return importedFileName; }
    /** Sets imported filename to display on clip. */
    void setImportedFileName (const juce::String& n)                { importedFileName = n; }

    //==============================================================================
    /** Returns true if no MIDI events exist in this list. */
    bool isEmpty() const noexcept                                   { return state.getNumChildren() == 0; }

    /** Removes all MIDI events from this list. Used for clearing takes. */
    void clear (juce::UndoManager*);
    /** Removes events outside beat range. Used by MidiClip::trimBeyondEnds(). */
    void trimOutside (BeatPosition firstBeat, BeatPosition lastBeat, juce::UndoManager*);
    /** Shifts all events by time offset. Used when extending clip start or after trimming. */
    void moveAllBeatPositions (BeatDuration deltaBeats, juce::UndoManager*);
    /** Scales all event timing by factor. Used during tempo changes and clip stretching. */
    void rescale (double factor, juce::UndoManager*);

    //==============================================================================
    /** Returns total number of notes in this list. */
    int getNumNotes() const                                         { return getNotes().size(); }
    /** Returns note at index from sorted list. Used by UI and iteration. */
    MidiNote* getNote (int index) const                             { return getNotes()[index]; }
    /** Finds note object for given ValueTree state. Used for state synchronization. */
    MidiNote* getNoteFor (const juce::ValueTree&);

    /** Returns range of note numbers (pitch) in this list. Used for auto-zoom features. */
    juce::Range<int> getNoteNumberRange() const;

    /** Returns beat position of first event. Used for clip boundary calculations. */
    BeatPosition getFirstBeatNumber() const;

    /** Returns beat position of last event. Used for clip boundary calculations. */
    BeatPosition getLastBeatNumber() const;

    /** Adds copy of existing note. Used for copy/paste operations. */
    MidiNote* addNote (const MidiNote&, juce::UndoManager*);
    /** Creates new note with parameters. Used during recording and manual entry. */
    MidiNote* addNote (int pitch, BeatPosition startBeat, BeatDuration lengthInBeats, int velocity, int colourIndex, juce::UndoManager*);
    /** Removes specific note. Used for deletion operations. */
    void removeNote (MidiNote&, juce::UndoManager*);
    /** Removes all notes. Used for clearing note data. */
    void removeAllNotes (juce::UndoManager*);

    //==============================================================================
    /** Returns total number of controller events. */
    int getNumControllerEvents() const                              { return getControllerEvents().size(); }

    /** Returns controller event at index from sorted list. */
    MidiControllerEvent* getControllerEvent (int index) const       { return getControllerEvents()[index]; }
    /** Finds controller event at specific beat and type. Used for automation editing. */
    MidiControllerEvent* getControllerEventAt (BeatPosition, int controllerType) const;

    /** Adds copy of existing controller event. Used for copy/paste operations. */
    MidiControllerEvent* addControllerEvent (const MidiControllerEvent&, juce::UndoManager*);
    /** Creates new controller event. Used during recording and automation. */
    MidiControllerEvent* addControllerEvent (BeatPosition, int controllerType, int controllerValue, juce::UndoManager*);
    /** Creates new controller event with metadata. Used for MPE and advanced automation. */
    MidiControllerEvent* addControllerEvent (BeatPosition, int controllerType, int controllerValue, int metadata, juce::UndoManager*);

    /** Removes specific controller event. Used for deletion operations. */
    void removeControllerEvent (MidiControllerEvent&, juce::UndoManager*);
    /** Removes all controller events. Used for clearing automation data. */
    void removeAllControllers (juce::UndoManager*);

    /** Returns true if any controller events of this type exist. Used for automation lane visibility. */
    bool containsController (int controllerType) const;

    /** Sets controller value at specific beat, creates event if needed. Used by automation editing. */
    void setControllerValueAt (int controllerType, BeatPosition beatNumber, int newValue, juce::UndoManager*);
    /** Removes controller events within beat range. Used for automation editing. */
    void removeControllersBetween (int controllerType, BeatPosition beatNumberStart, BeatPosition beatNumberEnd, juce::UndoManager*);

    /** Creates smooth controller ramp between values. Used for automation curves. */
    void insertRepeatedControllerValue (int type, int startVal, int endVal,
                                        BeatRange rangeBeats,
                                        BeatDuration intervalBeats, juce::UndoManager*);

    //==============================================================================
    /** Returns total number of sysex events. */
    int getNumSysExEvents() const                                   { return getSysexEvents().size(); }

    /** Returns sysex event at index with bounds checking. */
    MidiSysexEvent* getSysexEvent (int index) const                 { return getSysexEvents()[index]; }
    /** Returns sysex event at index without bounds checking. Used for performance-critical code. */
    MidiSysexEvent* getSysexEventUnchecked (int index) const        { return getSysexEvents().getUnchecked (index); }
    /** Finds sysex event for given ValueTree state. Used for state synchronization. */
    MidiSysexEvent* getSysexEventFor (const juce::ValueTree&) const;

    /** Creates new sysex event from MIDI message. Used during recording and import. */
    MidiSysexEvent& addSysExEvent (const juce::MidiMessage&, BeatPosition, juce::UndoManager*);

    /** Removes specific sysex event. Used for deletion operations. */
    void removeSysExEvent (const MidiSysexEvent&, juce::UndoManager*);
    /** Removes all sysex events. Used for clearing system exclusive data. */
    void removeAllSysexes (juce::UndoManager*);

    //==============================================================================
    /** Imports MIDI from sequence, converting timestamps to beats. Used during MIDI file loading and recording. */
    void importMidiSequence (const juce::MidiMessageSequence&, Edit*,
                             TimePosition editTimeOfListTimeZero, juce::UndoManager*);

    /** Imports MIDI sequence with MPE expression mapping. Used for advanced MPE workflows. */
    void importFromEditTimeSequenceWithNoteExpression (const juce::MidiMessageSequence&, Edit*,
                                                       TimePosition editTimeOfListTimeZero, juce::UndoManager*);

    /** Determines MIDI event timing. */
    enum class TimeBase
    {
        seconds,    /** Event times will be in seconds relative to the Edit timeline. */
        beats,      /** Event times will be in beats relative to the Edit timeline. */
        beatsRaw    /** Event times will be in beats with no quantisation or groove. */
    };

    /** Exports to playback sequence for audio engine. Used by audio graph construction.
        @param MidiClip     Clip boundaries and groove template to apply
        @param TimeBase     Output time format (seconds/beats/beatsRaw)
        @param generateMPE  Whether to create MPE or standard MIDI output
    */
    juce::MidiMessageSequence exportToPlaybackMidiSequence (MidiClip&, TimeBase, bool generateMPE) const;

    /** Creates standard playback sequence with default settings. Used by audio engine. */
    static juce::MidiMessageSequence createDefaultPlaybackMidiSequence (const MidiList&, MidiClip&, TimeBase, bool generateMPE);

    //==============================================================================
    /** Analyzes MIDI file to detect MPE data patterns. Used for automatic MPE detection during import. */
    static bool looksLikeMPEData (const juce::File&);

    /** Default MPE parameter values for initialization. */
    static constexpr const double defaultInitialTimbreValue = 0.5;
    static constexpr const double defaultInitialPitchBendValue = 0;
    static constexpr const double defaultInitialPressureValue = 0;

    /** Checks if MIDI file contains tempo changes. Used to determine import strategy. */
    static bool fileHasTempoChanges (const juce::File&);

    /** Reads MIDI file into separate track lists with tempo/time signature data. Primary MIDI import function. */
    static bool readSeparateTracksFromFile (const juce::File&,
                                            juce::OwnedArray<MidiList>& lists,
                                            juce::Array<BeatPosition>& tempoChangeBeatNumbers,
                                            juce::Array<double>& bpms,
                                            juce::Array<int>& numerators,
                                            juce::Array<int>& denominators,
                                            BeatDuration& songLength,
                                            bool importAsNoteExpression);

    //==============================================================================
    /** Sorts MIDI events by beat position. Used internally by EventList for automatic ordering. */
    template <typename Type>
    static void sortMidiEventsByTime (juce::Array<Type>& notes)
    {
        std::sort (notes.begin(), notes.end(),
                   [] (const Type& a, const Type& b) { return a->getBeatPosition() < b->getBeatPosition(); });
    }

    /** Sorts MIDI events by note number (pitch). Used for chord analysis and display. */
    template <typename Type>
    static void sortMidiEventsByNoteNumber (juce::Array<Type>& notes)
    {
        std::sort (notes.begin(), notes.end(),
                   [] (const Type& a, const Type& b) { return a->getNoteNumber() < b->getNoteNumber(); });
    }

    //==============================================================================
    /** ValueTree state for persistence and undo/redo support. */
    juce::ValueTree state;

private:
    //==============================================================================
    /** MIDI channel (1-16) for all events in this list. */
    juce::CachedValue<MidiChannel> midiChannel;
    /** Whether this list represents a composite take. */
    juce::CachedValue<bool> isComp;

    /** Filename from imported MIDI file. */
    juce::String importedFileName;
    /** Track name from imported MIDI file. */
    juce::String importedName;

    /** Initializes event lists and default values. Called during construction. */
    void initialise (juce::UndoManager*);

    template<typename EventType>
    struct EventDelegate
    {
        static bool isSuitableType (const juce::ValueTree&);
        /** Return true if the order may have changed. */
        static bool updateObject (EventType&, const juce::Identifier&);
        static void removeFromSelection (EventType*);
    };

    template<typename EventType>
    struct EventList : public ValueTreeObjectList<EventType>
    {
        EventList (const juce::ValueTree& v)
            : ValueTreeObjectList<EventType> (v)
        {
            ValueTreeObjectList<EventType>::rebuildObjects();
        }

        ~EventList() override
        {
            ValueTreeObjectList<EventType>::freeObjects();
        }

        EventType* getEventFor (const juce::ValueTree& v)
        {
            for (auto m : ValueTreeObjectList<EventType>::objects)
                if (m->state == v)
                    return m;

            return {};
        }

        bool isSuitableType (const juce::ValueTree& v) const override   { return EventDelegate<EventType>::isSuitableType (v); }
        EventType* createNewObject (const juce::ValueTree& v) override  { return new EventType (v); }
        void deleteObject (EventType* m) override                       { delete m; }
        void newObjectAdded (EventType*) override                       { triggerSort(); }
        void objectRemoved (EventType* m) override                      { EventDelegate<EventType>::removeFromSelection (m); triggerSort(); }
        void objectOrderChanged() override                              { triggerSort(); }

        void valueTreePropertyChanged (juce::ValueTree& v, const juce::Identifier& i) override
        {
            if (auto e = getEventFor (v))
                if (EventDelegate<EventType>::updateObject (*e, i))
                    triggerSort();
        }

        void triggerSort()
        {
            const juce::ScopedLock sl (lock);
            needsSorting = true;
        }

        const juce::Array<EventType*>& getSortedList()
        {
            TRACKTION_ASSERT_MESSAGE_THREAD

            const juce::ScopedLock sl (lock);

            if (needsSorting)
            {
                needsSorting = false;
                sortedEvents = ValueTreeObjectList<EventType>::objects;
                sortMidiEventsByTime (sortedEvents);
            }

            return sortedEvents;
        }

        bool needsSorting = true;
        juce::Array<EventType*> sortedEvents;
        juce::CriticalSection lock;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EventList)
    };

    /** Automatically sorted list of MIDI notes. */
    std::unique_ptr<EventList<MidiNote>> noteList;
    /** Automatically sorted list of controller events. */
    std::unique_ptr<EventList<MidiControllerEvent>> controllerList;
    /** Automatically sorted list of sysex events. */
    std::unique_ptr<EventList<MidiSysexEvent>> sysexList;

    //==============================================================================
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MidiList)
};

}} // namespace tracktion { inline namespace engine
