# Tracktion Engine Programming Skill

**Progressive Loading Skill for Tracktion Engine Development**

This skill provides contextual information about Tracktion Engine, a powerful audio engine for building DAW-like applications. Information loads progressively based on complexity.

---

## Level 1: Core Concepts & Getting Started

### What is Tracktion Engine?

Tracktion Engine is a high-level data model and framework for building sequence-based audio applications, from simple file players to full DAWs. It's built on JUCE and provides:

- Timeline-based audio/MIDI editing
- Multi-track recording and playback
- Plugin hosting (VST, AU, etc.)
- Automation and modulation
- Real-time audio processing with multi-CPU support
- Cross-platform support (macOS, Windows, Linux, iOS, Android, Raspberry Pi)

**Requirements**: C++20, JUCE

**License**: GPL v3.0 / Commercial

### Core Architecture

```
Engine (Central entry point)
  ├─ Edit (Timeline container - like a project/session)
  │   ├─ Tracks[] (AudioTrack, MidiTrack, FolderTrack, etc.)
  │   ├─ Clips[] (WaveAudioClip, MidiClip, StepClip, etc.)
  │   ├─ TransportControl (Play/stop/record)
  │   └─ TempoSequence (Tempo automation)
  │
  ├─ DeviceManager (Audio/MIDI hardware I/O)
  ├─ PluginManager (Plugin discovery/loading)
  └─ AudioFileManager (File caching)
```

### Basic Workflow

```cpp
// 1. Create Engine
tracktion::engine::Engine engine { "MyApp" };

// 2. Create or load Edit
auto edit = te::createEmptyEdit(engine, editFile);

// 3. Add track
auto track = te::createTrack<te::AudioTrack>(*edit);

// 4. Add audio clip
auto clip = track->insertNewClip(te::TrackItem::Type::wave,
                                 { startTime, duration });
clip->setAudioFile(audioFile);

// 5. Control playback
auto& transport = edit->getTransport();
transport.play();
transport.stop();
```

### Key Namespaces

- `tracktion::engine` or `te` - Main engine classes
- `tracktion::graph` - Audio graph processing
- `tracktion::core` - Utilities and helpers

### Essential Classes

- **Engine** - Central singleton managing all subsystems
- **Edit** - Contains tracks, clips, tempo, markers (like a session)
- **Track** - Container for clips (AudioTrack, MidiTrack, etc.)
- **Clip** - Timed content on tracks (audio, MIDI, etc.)
- **TransportControl** - Playback control
- **Plugin** - Audio/MIDI processing effects

### Module Structure

Tracktion Engine consists of 4 modules:
- `tracktion_engine` - Main DAW engine
- `tracktion_graph` - Real-time audio graph processing
- `tracktion_core` - Utilities and foundation
- `3rd_party` - External libraries (CHOC, libsamplerate, etc.)

### Documentation Resources

- **Official Docs**: https://tracktion.github.io/tracktion_engine/modules.html
- **Tutorials**: `/tutorials/` directory (01-04 numbered tutorials)
- **Examples**: `/examples/DemoRunner/demos/` (many demo files)
- **Key Examples**:
  - `PlaybackDemo.h` - Basic audio playback
  - `MidiPlaybackDemo.h` - MIDI playback
  - `RecordingDemo.h` - Audio recording
  - `PluginDemo.h` - Plugin usage
  - `StepSequencerDemo.h` - Step sequencer

---

## Level 2: Main Components & API

### Track Types

```cpp
// Audio track - plays audio clips
auto audioTrack = te::createTrack<te::AudioTrack>(*edit);

// MIDI track - plays MIDI clips
auto midiTrack = te::createTrack<te::MidiTrack>(*edit);

// Folder track - groups other tracks
auto folder = te::createTrack<te::FolderTrack>(*edit);

// Special tracks
te::TempoTrack    // Tempo automation
te::MarkerTrack   // Markers/cues
te::ChordTrack    // Chord progressions
te::ArrangerTrack // Arrangement sections
```

### Clip Types

```cpp
// Audio clip
te::WaveAudioClip   // Audio file playback
te::EditClip        // Nested Edit (like group/bus)

// MIDI clips
te::MidiClip        // Standard MIDI notes/CC
te::StepClip        // Step sequencer pattern
te::ChordClip       // Chord sequences

// Other
te::MarkerClip      // Markers
te::CollectionClip  // Clip collections
te::ContainerClip   // Container for other clips
```

### Creating and Manipulating Clips

```cpp
// Insert audio clip
auto clip = audioTrack->insertWaveClip("My Clip", audioFile,
                                      { {startTime, endTime}, offset });

// Insert MIDI clip
auto midiClip = dynamic_cast<te::MidiClip*>(
    midiTrack->insertNewClip(te::TrackItem::Type::midi,
                            { startTime, duration }, nullptr));

// Add MIDI notes
midiClip->getSequence().addNote(noteNumber, beatNumber,
                                lengthInBeats, velocity,
                                colourIndex, nullptr);

// Modify clip properties
clip->setStart(newStart, true, true);
clip->setEnd(newEnd, true);
clip->setSpeedRatio(1.5); // Pitch/time stretch
clip->setGainDB(6.0);
clip->setFadeIn(1.0);  // 1 second fade
clip->setFadeOut(1.0);
```

### Plugin Management

```cpp
// Get plugin manager
auto& pluginMgr = engine.getPluginManager();

// Scan for plugins
pluginMgr.scanForNewPlugins();

// Get available plugins
auto pluginList = pluginMgr.getKnownPlugins();

// Insert plugin on track
auto plugin = track->pluginList.insertPlugin(
    te::Plugin::create(pluginDescription), index);

// Access plugin parameters
if (auto param = plugin->getAutomatableParameter(0))
{
    param->setParameter(0.5f, juce::NotificationType::sendNotification);
    param->getCurveValue(timeInSeconds); // Get automated value
}
```

### Audio File Handling

```cpp
// Audio file manager handles caching
auto& audioFileMgr = engine.getAudioFileManager();

// Create audio file reference
te::AudioFile audioFile(engine, juce::File("path/to/audio.wav"));

// Get audio properties
auto lengthInSamples = audioFile.getLength();
auto sampleRate = audioFile.getSampleRate();
auto numChannels = audioFile.getNumChannels();

// Read audio data
te::AudioFileInfo info;
audioFile.getInfo(info);

// Create reader
auto reader = audioFile.createReader();
if (reader)
    reader->read(buffer, startSample, numSamples, relativeTo,
                 clearExistingContent);
```

### Transport Control

```cpp
auto& transport = edit->getTransport();

// Playback control
transport.play(autoReturnIfError);
transport.stop(discardRecordings, allowLooping);
transport.setPosition(timeInSeconds);
transport.scrub(scrubSpeedMultiplier);

// Recording
transport.record(retakeMode, autoReturn);

// Loop control
transport.setLoopRange({ loopStart, loopEnd });
transport.looping = true;

// Listen to transport state
struct MyListener : te::TransportControl::Listener
{
    void playbackContextChanged() override {}
    void autoSaveNow() override {}
};
```

### Tempo and Time Signatures

```cpp
auto& tempoSequence = edit->getTempoSequence();

// Set tempo
tempoSequence.setTempo(0.0, 120.0); // 120 BPM at time 0
tempoSequence.setTempoAt(timeInSeconds, newBPM);

// Tempo curves (multiple points)
tempoSequence.insertTempo(timeInSeconds, bpm);

// Time signatures
tempoSequence.getTimeSig(timeInSeconds); // Returns TimeSigSetting
tempoSequence.insertTimeSig(timeInSeconds, numerator, denominator);

// Time conversions
auto beats = tempoSequence.timeToBeats(timeInSeconds);
auto time = tempoSequence.beatsToTime(beatNumber);
```

### Edit Structure

```cpp
// Create new edit
auto edit = te::Edit::createEmpty(engine, te::createEmptyEdit::Options{});

// Load existing edit
auto edit = te::loadEditFromFile(engine, editFile);

// Save edit
edit->saveAs(newFile, saveOptions);
edit->flushState();  // Save without changing file

// Edit properties
edit->getLength();
edit->getTransport();
edit->getTempoSequence();
edit->getMarkerManager();
edit->getPitchSequence();  // Key changes
edit->getAllTracks(te::Edit::TrackType::all);
```

### Device Management

```cpp
auto& deviceMgr = engine.getDeviceManager();

// Audio devices
auto& audioDevice = deviceMgr.getDefaultWaveOutDevice();
auto& audioInput = deviceMgr.getDefaultWaveInDevice();

// MIDI devices
auto midiInDevices = te::MidiInputDevice::getAvailableDevices(engine);
auto midiOutDevices = te::MidiOutputDevice::getAvailableDevices(engine);

// Enable/disable devices
midiInDevice->setEnabled(true);
midiInDevice->setRecordingEnabled(*track, true);
```

---

## Level 3: Advanced Features

### Automation System

```cpp
// Get automation parameter
auto plugin = track->pluginList[0];
auto param = plugin->getAutomatableParameter(paramIndex);

// Create automation curve
auto& curve = param->getCurve();
curve.addPoint(timeInSeconds, value, curvature);
curve.setPosition(timeInSeconds, newValue);

// Automation recording
param->setAutomationActive(true);
param->midiLearnMode = true;  // Enable MIDI learn
```

### Parameter Modifiers

Modifiers add dynamic modulation to parameters:

```cpp
// LFO modifier
auto lfo = new te::LFOModifier(*param);
lfo->rate = 2.0;  // 2 Hz
lfo->depth = 0.5;
lfo->shape = te::LFOModifier::Shape::sine;

// Envelope follower
auto envFollower = new te::EnvelopeFollowerModifier(*param);
envFollower->attack = 0.01;
envFollower->release = 0.5;

// Breakpoint (curve) modifier
auto breakpoint = new te::BreakpointOscillatorModifier(*param);
breakpoint->addPoint(0.0, 0.0, 0.5);
breakpoint->addPoint(1.0, 1.0, 0.5);

// Step modifier
auto step = new te::StepModifier(*param);
step->setNumSteps(16);
step->setValue(stepIndex, value);

// Random modifier
auto random = new te::RandomModifier(*param);
random->rate = 4.0;
random->depth = 0.3;

// MIDI tracker (note velocity/aftertouch)
auto midiTracker = new te::MIDITrackerModifier(*param);
```

### Macro Parameters

```cpp
// Create macro parameter
auto macro = edit->getMacroParameterList().createMacroParameter();
macro->setParameterName("Cutoff");

// Assign parameter to macro
macro->addParameter(plugin->getAutomatableParameter(0), 0.0, 1.0);
macro->addParameter(plugin2->getAutomatableParameter(2), 0.5, 1.0);

// Control macro
macro->setNormalisedParameter(0.75);
```

### Clip Launching

```cpp
// Get clip slots for track
auto clipSlots = track->getClipSlotList();

// Create clip slot
auto slot = clipSlots->insertNewClip(slotIndex);

// Launch clip
slot->launch();
slot->stop();

// Follow actions
slot->setFollowAction(te::ClipSlot::FollowAction::playNext);
slot->setFollowActionTime(4.0);  // After 4 beats

// Scenes (launch clips across tracks)
auto scene = edit->getClipSlotList().getScene(sceneIndex);
scene->launch();
```

### MIDI Editing

```cpp
auto& midiList = midiClip->getSequence();

// Add notes
midiList.addNote(noteNumber, startBeat, lengthBeats,
                 velocity, colourIndex, undo);

// Remove notes
midiList.removeNote(note, undo);

// Get notes in range
for (auto note : midiList.getNotesInRange({ startBeat, endBeat }))
{
    note->setNoteNumber(newNote, undo);
    note->setVelocity(newVel, undo);
}

// Controllers (CC)
midiList.addControllerEvent(beatNumber, controllerType,
                            value, metadata, undo);

// Quantization
te::Quantisation quant;
quant.setType(te::Quantisation::Type::sixteenth);
midiList.quantise(quant, undo);

// Groove templates
auto groove = te::GrooveTemplate::createGroove(engine, grooveFile);
midiList.applyGrooveTemplate(*groove);
```

### Step Sequencer

```cpp
auto stepClip = dynamic_cast<te::StepClip*>(clip);

// Create pattern
te::Pattern pattern;
pattern.setNumNotes(16);  // 16 steps

// Set step data
for (int i = 0; i < 16; ++i)
{
    pattern.setNote(i, noteNumber, velocity);
    pattern.setNoteValue(i, te::Pattern::NoteValue::gate, gate);
    pattern.setNoteValue(i, te::Pattern::NoteValue::probability, prob);
}

stepClip->setPattern(channelIndex, pattern, undo);
```

### MIDI Pattern Generation

```cpp
// Generate bass pattern
auto bassPattern = te::generateBassPattern(
    edit, scale, rootNote, numBars, complexity);

// Generate melody
auto melody = te::generateMelodyPattern(
    edit, scale, rootNote, numBars, complexity);

// Generate chord progression
auto chords = te::generateChordPattern(
    edit, scale, rootNote, numBars, complexity);

// Generate arpeggio
auto arp = te::generateArpPattern(
    edit, scale, rootNote, numBars, arpType);
```

### Rendering/Export

```cpp
// Create renderer
te::Renderer::Parameters params(*edit);
params.destFile = outputFile;
params.audioFormat = engine.getAudioFileFormatManager().getNamedFormat("WAV");
params.sampleRate = 44100.0;
params.bitDepth = 16;

// Render specific selection
params.time = { startTime, endTime };
params.tracksToDo.add(track);

// Render settings
params.normalise = true;
params.trimSilence = true;
params.createMidiFile = false;

// Perform render
te::Renderer renderer(params);
while (renderer.isRendering())
{
    auto progress = renderer.getProgress();
    // Update UI...
}

auto success = renderer.getResult();
```

### Audio Recording

```cpp
// Start recording on track
auto& transport = edit->getTransport();
auto inputDevice = deviceMgr.getDefaultWaveInDevice();

// Enable recording for track
track->setRecordingActive(true, inputDevice);

// Start recording
transport.record(false);  // false = don't auto-return

// Stop recording
transport.stop(false, false);

// Get recorded clip
auto newClip = track->getClips().getLast();
```

### Freeze/Bounce

```cpp
// Freeze track (render to audio)
if (track->canContainPlugin())
{
    auto freezePoint = track->insertNewPlugin("freezePoint", index);
    track->freezeTrackUpTo(freezePoint, groupFreeze, allowAsync);
}

// Unfreeze
track->unFreezeTrack();

// Check if frozen
bool isFrozen = track->isFrozen();
```

### Click Track

```cpp
auto& clickTrack = edit->getClickTrack();

// Enable click
clickTrack.setEnabled(true);

// Custom click samples
clickTrack.setAccentedBeatFile(accentFile);
clickTrack.setUnaccentedBeatFile(beatFile);

// Click properties
clickTrack.setLevel(0.75f);
clickTrack.setOnlyDuringRecording(true);
```

### Markers and Regions

```cpp
auto& markerMgr = edit->getMarkerManager();

// Add marker
auto marker = markerMgr.createMarker(-1, position, length, undo);
marker->name = "Verse 1";
marker->colour = juce::Colours::blue;

// Get markers
auto markers = markerMgr.getMarkers();

// Navigate markers
markerMgr.getNextMarker(currentTime);
markerMgr.getPrevMarker(currentTime);
```

---

## Level 4: Expert Topics

### Threading Model

**Thread Types:**
- **Message Thread**: UI updates, Edit modifications, non-real-time ops
- **Audio Thread**: Real-time processing (callback), lock-free only
- **Recording Thread**: Disk I/O for recording
- **Background Threads**: Asset generation, rendering, plugin scanning

**Thread Safety Rules:**
1. Never block the audio thread
2. Use lock-free data structures for audio/message thread communication
3. Modify Edit state only on message thread
4. Use `callBlocking()` to safely query from audio thread
5. Recording uses lock-free FIFO to audio thread

```cpp
// Safe cross-thread communication
juce::AbstractFifo fifo(bufferSize);

// Audio thread writes
fifo.prepareToWrite(numToWrite).forEach([&](int idx) {
    buffer[idx] = audioData[idx];
});

// Message thread reads
fifo.prepareToRead(numReady).forEach([&](int idx) {
    processData(buffer[idx]);
});
```

### Audio Graph Architecture

The `tracktion_graph` module provides node-based processing:

```cpp
// Nodes represent processing units
class MyNode : public tracktion::graph::Node
{
    tracktion::graph::NodeProperties getNodeProperties() override
    {
        return { numberOfChannels, latencyNumSamples, hasMidi };
    }

    void prepareToPlay(const PlaybackInitialisationInfo& info) override
    {
        sampleRate = info.sampleRate;
    }

    void process(ProcessContext& pc) override
    {
        auto inputBuffers = pc.buffers;
        auto outputBuffers = pc.buffers;

        // Process audio...
    }
};

// Build graph
std::vector<std::unique_ptr<Node>> nodes;
nodes.push_back(std::make_unique<MyNode>());

auto graph = tracktion::graph::createNode(std::move(nodes));
```

### Custom Plugin Development

```cpp
// Create custom plugin type
class MyPlugin : public te::Plugin
{
public:
    MyPlugin(te::PluginCreationInfo info) : Plugin(info) {}

    static const char* xmlTypeName;

    juce::String getName() const override { return "My Plugin"; }

    void initialise(const PluginInitialisationInfo& info) override
    {
        // Setup DSP
    }

    void applyToBuffer(const PluginRenderContext& prc) override
    {
        // Process audio
        auto& destBuffer = prc.destBuffer;
        // ... DSP code ...
    }

    // Add automatable parameters
    juce::ReferenceCountedArray<te::AutomatableParameter> getAutomatableParameters() override
    {
        return params;
    }
};

// Register plugin type
te::Plugin::registerPluginType<MyPlugin>();
```

### Performance Optimization

**Audio File Caching:**
```cpp
// Configure cache size
engine.getAudioFileManager().setCacheSize(256 * 1024 * 1024); // 256MB

// Proxy files for faster loading
audioFile.createProxyIfNeeded();
```

**Multi-CPU Utilization:**
```cpp
// Configure thread pool
auto& editPlaybackContext = edit->getCurrentPlaybackContext();
editPlaybackContext->setNumThreads(numCPUs);

// Choose threading algorithm
tracktion::graph::PlayHeadState::PlayHeadPositionUpdaterType::singleThreaded;
tracktion::graph::PlayHeadState::PlayHeadPositionUpdaterType::multiThreaded;
```

**Plugin Latency Compensation:**
```cpp
// Report plugin latency
int getLatencySamples() override { return latency; }

// Engine automatically compensates across all tracks
```

### ValueTree State Management

All Tracktion Engine objects use JUCE's ValueTree:

```cpp
// Access underlying state
auto& state = edit->state;
auto& trackState = track->state;

// Listen to changes
struct StateListener : juce::ValueTree::Listener
{
    void valueTreePropertyChanged(juce::ValueTree& tree,
                                 const juce::Identifier& prop) override
    {
        // React to changes
    }
};

// Modify state directly (advanced)
track->state.setProperty(te::IDs::colour,
                        juce::Colour(0xff0000ff).toString(),
                        &edit->getUndoManager());
```

### Engine Customization

```cpp
// Custom UIBehaviour
struct MyUIBehaviour : te::UIBehaviour
{
    bool shouldGenerateLiveWaveformsWhenRecording() override { return true; }
    int getTimecodeFormat() override { return te::TimecodeDisplayFormat::barsBeats; }
    // ... override other methods ...
};

engine.setUIBehaviour(std::make_unique<MyUIBehaviour>());

// Custom EngineBehaviour
struct MyEngineBehaviour : te::EngineBehaviour
{
    bool autoInitialiseDevices() override { return false; }
    double getDefaultLoopLength() override { return 8.0; }
    // ... override other methods ...
};

engine.setEngineBehaviour(std::make_unique<MyEngineBehaviour>());
```

### Undo/Redo

```cpp
auto& undoManager = edit->getUndoManager();

// Perform undoable action
undoManager.beginNewTransaction();
track->insertNewClip(/*...*/);
undoManager.beginNewTransaction();

// Undo/redo
undoManager.undo();
undoManager.redo();

// Named transactions
undoManager.beginNewTransaction("Add Track");
```

### Selection Management

```cpp
auto& selectionMgr = edit->getSelectionManager();

// Select objects
selectionMgr.selectOnly(clip);
selectionMgr.addToSelection(clip2);
selectionMgr.deselectAll();

// Query selection
auto selectedClips = selectionMgr.getItemsOfType<te::Clip>();
auto selectedTracks = selectionMgr.getItemsOfType<te::Track>();
```

### ARA (Audio Random Access) Support

```cpp
// Enable ARA for compatible plugins
#define TRACKTION_ENABLE_ARA 1

// ARA plugins can access Edit structure
// Provides better integration with hosts like Studio One
```

### ReWire Support

```cpp
#define TRACKTION_ENABLE_REWIRE 1

// ReWire devices
auto rewireDevices = te::ReWirePlugin::getAvailableDevices();
auto plugin = track->pluginList.insertPlugin(
    te::ReWirePlugin::create(rewireDevice), index);
```

### MPE (MIDI Polyphonic Expression)

```cpp
// Enable MPE for MIDI clip
midiClip->setMPEMode(true);

// Note expression data
auto note = midiClip->getSequence().getNote(index);
note->setPitchBend(pitchBendValue);
note->setPressure(pressureValue);
note->setTimbre(timbreValue);

// Export to MPE MIDI
edit->exportToMPEMIDI(outputFile);
```

### Time Stretching Algorithms

```cpp
// Configure time stretch engine
#define TRACKTION_ENABLE_TIMESTRETCH_SOUNDTOUCH 1
#define TRACKTION_ENABLE_TIMESTRETCH_ELASTIQUE 1

// Set algorithm per clip
audioClip->setTimeStretchMode(te::TimeStretcher::elastiqueTransient);
audioClip->setSpeedRatio(1.5);  // 1.5x speed
audioClip->setPitchChange(2.0); // +2 semitones
```

### Ableton Link Synchronization

```cpp
#define TRACKTION_ENABLE_ABLETON_LINK 1

// Enable Link
transport.enableAbletonLink(true);

// Link settings
transport.setAbletonLinkStart(enabled);
transport.setNumPeers(numPeers);
```

### Control Surface API

```cpp
// Get available control surfaces
auto surfaces = te::ExternalControllerManager::getControllerDevices(engine);

// Enable control surface
auto mcu = engine.getExternalControllerManager().createControllerDevice(
    te::MackeControlUnit::deviceType);
mcu->enable();

// Custom control surface
class MyControlSurface : public te::ExternalController
{
    void moveFaders(float* positions, int numFaders) override {}
    void movePanPots(float* positions, int numPots) override {}
    void updateSoloAndMute() override {}
    // ... implement other methods ...
};
```

### Background Rendering

```cpp
// Render in background thread
te::Renderer renderer(params);
renderer.renderInBackground();

// Monitor progress
renderer.addListener(this);

struct RendererListener : te::Renderer::Listener
{
    void renderProgress(float progress) override
    {
        // Update progress bar
    }

    void renderComplete() override
    {
        // Handle completion
    }
};
```

### Memory Management Best Practices

1. **Use Engine's allocators** for real-time audio buffers
2. **Avoid allocations** in audio thread
3. **Pool temporary buffers** via `engine.getAudioFileManager()`
4. **Use scratch buffers** from `tracktion::engine::ScratchBuffer`
5. **Configure rpmalloc** via `TRACKTION_ENABLE_RPMALLOC`

```cpp
// Get scratch buffer (pre-allocated)
auto scratch = engine.getAudioFileManager().getScratchBuffer(numChannels, numSamples);

// Use it without allocation
scratch->clear();
// ... use buffer ...
```

### Debugging and Profiling

```cpp
// Enable crash tracing
te::CrashTracer::Ptr tracer(new te::CrashTracer("MyFunction"));

// Benchmark code
TRACKTION_BENCHMARK("MyOperation")
{
    // Code to benchmark
}

// Enable unit tests
#define TRACKTION_UNIT_TESTS 1
```

---

## Common Patterns & Idioms

### Pattern: Safe Plugin Casting

```cpp
if (auto volumePlugin = dynamic_cast<te::VolumeAndPanPlugin*>(plugin))
{
    volumePlugin->setVolumeDb(6.0);
    volumePlugin->setPan(0.5);
}
```

### Pattern: Iterating Clips in Time Range

```cpp
for (auto clip : track->getClips())
{
    if (clip->getPosition().overlaps({ startTime, endTime }))
    {
        // Process clip
    }
}
```

### Pattern: Finding Plugin by Name

```cpp
auto findPlugin = [](te::Track& track, const juce::String& name)
{
    for (auto plugin : track->pluginList)
        if (plugin->getName() == name)
            return plugin;
    return nullptr;
};
```

### Pattern: Convert Time Formats

```cpp
auto& tempo = edit->getTempoSequence();

// Seconds to beats
auto beats = tempo.timeToBeats(timeInSeconds);

// Beats to seconds
auto time = tempo.beatsToTime(beatNumber);

// Beats to bars:beats
auto barBeat = tempo.toBarBeatFraction(beatNumber);

// Timecode string
auto timecode = te::TimecodeDisplayFormat::formatTimecode(
    timeInSeconds, tempo, te::TimecodeDisplayFormat::barsBeats);
```

---

## Key Configuration Flags

```cpp
// Feature flags (set before including headers)
TRACKTION_ENABLE_ARA                // Audio Random Access
TRACKTION_ENABLE_CMAJOR             // Cmajor plugins
TRACKTION_ENABLE_REWIRE             // ReWire support
TRACKTION_ENABLE_AUTOMAP            // Novation Automap
TRACKTION_ENABLE_VIDEO              // Video support
TRACKTION_ENABLE_REX                // REX audio format
TRACKTION_ENABLE_CONTROL_SURFACES   // External controllers
TRACKTION_ENABLE_TIMESTRETCH_*      // Various timestretch engines
TRACKTION_ENABLE_ABLETON_LINK       // Ableton Link
TRACKTION_AIR_WINDOWS               // AirWindows effects
TRACKTION_UNIT_TESTS                // Unit tests
TRACKTION_BENCHMARKS                // Benchmarks
```

---

## Important File Locations

- **Core headers**: `/modules/tracktion_engine/tracktion_engine.h`
- **Examples**: `/examples/DemoRunner/demos/*.h`
- **Tutorials**: `/tutorials/*.md`
- **Tests**: `/modules/tracktion_engine/tracktion_engine_tests.cpp`
- **Graph module**: `/modules/tracktion_graph/`
- **Utilities**: `/modules/tracktion_engine/utilities/`

---

## Quick Reference: Common Tasks

**Load Edit**: `te::loadEditFromFile(engine, file)`
**Create Edit**: `te::Edit::createEmpty(engine, options)`
**Add Track**: `te::createTrack<te::AudioTrack>(*edit)`
**Add Clip**: `track->insertNewClip(type, position, sourceMedia)`
**Play**: `edit->getTransport().play(true)`
**Record**: `edit->getTransport().record(false)`
**Add Plugin**: `track->pluginList.insertPlugin(plugin, index)`
**Set Tempo**: `edit->getTempoSequence().setTempo(time, bpm)`
**Render**: `te::Renderer renderer(params); renderer.render()`
**Add Marker**: `edit->getMarkerManager().createMarker(idx, pos, len, undo)`

---

## Resources & Links

- **GitHub**: https://github.com/Tracktion/tracktion_engine
- **API Docs**: https://tracktion.github.io/tracktion_engine/modules.html
- **Forum**: https://forum.juce.com/c/tracktion-engine
- **Company**: https://www.tracktion.com/develop/tracktion-engine
- **Example App**: [Tracktion Waveform Free](https://www.tracktion.com/products/waveform-free)

---

## Version Information

- **Current Version**: 3.1.0
- **Minimum C++**: C++20
- **Platforms**: macOS, Windows, Linux, iOS, Android, Raspberry Pi
- **Dependencies**: JUCE (audio_basics, audio_devices, audio_formats, audio_utils, dsp, gui_extra, osc)

---

*This skill progressively loads based on context. For JUCE-specific information, use the separate JUCE skill.*
