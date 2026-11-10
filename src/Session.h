#pragma once

#include <juce_events/juce_events.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include "Model.h"
#include "SampleStore.h"
#include "Engine.h"
#include <deque>
#include <set>
#include <optional>

namespace ok
{

// Owns the document and everything the UI does to it. Lives in the processor, message thread only.
class Session : public juce::ChangeBroadcaster, private juce::Timer
{
public:
    Session (Engine& engine, bool isPlugin);
    ~Session() override;

    Engine& engine;
    SampleStore samples;
    const bool isPlugin;

    Document doc;
    juce::uint32 revision = 0;   // bumped on every change that is saved with the state

    //== UI state ==============================================================
    uint32_t selected = 0;      // active track, 0 = none
    int part = 0;               // part shown and edited (follows the engine while playing)
    juce::File patternFile;
    bool dirty = false;
    juce::String toastText;
    bool toastHasUndo = false;
    juce::uint32 toastTime = 0;
    juce::File browserRoot;
    juce::String previewPath;
    int master = 100;           // 0…127, 100 = 0 dB
    int metronomeVolume = 70;
    bool metronomeAccent = true;
    int metronomeBeats = 4;     // time signature: beats per bar, 2…11 (the first one is accented)

    bool trackActive() const { return selected != 0 && doc.findTrack (selected) != nullptr; }
    Track* selectedTrack() { return doc.findTrack (selected); }
    Part& curPart() { return doc.parts[(size_t) part]; }
    TrackPart& trackPart (uint32_t id) { return curPart().tracks[id]; }
    Hit* hitAt (uint32_t id, int key);

    //== Undo ==================================================================
    // Call before changing the document. Repeated calls with the same key within a short time
    // (a knob being dragged or scrolled) produce a single undo step.
    void checkpoint (const void* gestureKey = nullptr);
    void changed();                 // document changed: dirty, republish, notify the UI
    void uiChanged();               // only the UI state changed
    void undo();
    void redo();
    bool canUndo() const { return ! undoStack.empty(); }
    bool canRedo() const { return ! redoStack.empty(); }
    void toast (const juce::String& text, bool withUndo = false);

    //== Tracks ================================================================
    bool addTrack (const juce::File& file, int insertIndex);   // false if 16 tracks
    void replaceSample (uint32_t id, const juce::File& file);
    void moveTrack (uint32_t id, int newIndex);
    void renameTrack (uint32_t id, const juce::String& name);
    void setColour (uint32_t id, int colour);
    void duplicateTrack (uint32_t id);
    void clearHits (uint32_t id);
    void deleteTrack (uint32_t id);
    void selectTrack (uint32_t id);   // click on the name: toggles the active track
    void closeTrack();
    void setGrid (uint32_t id, int groups, int steps);
    // A slice dragged from the slicer: a new track that plays only that slice (false if 16 tracks),
    // or a hit of the slice's own track that plays it
    bool sliceToTrack (uint32_t id, int sliceIndex, int insertIndex);
    void putSliceHit (uint32_t id, int bar, int step, int sliceIndex);

    //== Hits ==================================================================
    void toggleHit (uint32_t id, int bar, int step);
    void toggleMark (uint32_t id, int key);
    void togglePin (uint32_t id, int key);
    void clearMarks();
    void scrollSlice (uint32_t id, int key, int delta);
    int sliceNumberOf (const Track& t, int key);   // 1-based slice shown on a hit, 0 if none

    // STEP of the active track acts on its marked hits in this part, or on all when none is marked
    bool hasMarks() const;
    int markedCount (uint32_t id) const;
    int hitCount (uint32_t id) const;

    //== Parts and bars ========================================================
    void pickPart (int index);
    void setPartBars (int bars);
    void copyPart (int index);
    void pastePart (int index);
    void clearPart (int index);
    bool hasPartClip() const { return partClip.has_value(); }
    void setSwitchMode (SwitchMode m);
    SwitchMode switchMode() const { return (SwitchMode) engine.switchMode.load(); }
    int queuedPart() const { return engine.queuedPart.load(); }
    void cancelQueue();

    // One bar of every track (the ⋯ menu above a bar). The clip can be pasted into any part.
    void copyBar (int bar);
    void pasteBar (int bar);
    void clearBar (int bar);
    bool hasBarClip() const { return barClip.has_value(); }
    bool canPasteBar (int bar) const;
    juce::String barClipDescription() const;

    // One track (the ⋯ menu of a track): a single bar, or every bar of the part when bar = -1.
    // Pastes into any track and any part; steps follow the target track's grid.
    void copyTrack (uint32_t id, int bar);
    void pasteTrack (uint32_t id, int bar);
    void clearTrackBar (uint32_t id, int bar);
    bool hasTrackClip() const { return trackClip.has_value(); }
    bool trackClipIsWhole() const { return trackClip && trackClip->from < 0; }
    bool canPasteTrack (uint32_t id, int bar) const;
    juce::String trackClipDescription() const;

    //== Slices ================================================================
    void rechop (Track& t);                         // recompute slices for By hits / Equal parts
    void setSlicesFromPositions (Track& t, std::vector<double> positions);
    void addMarker (uint32_t id, double pos);
    void moveMarker (uint32_t id, int sliceIndex, double pos);
    void removeMarker (uint32_t id, int sliceIndex);

    //== Transport =============================================================
    void togglePlay();
    // Standalone: the request counts at once, before the audio thread has seen it
    bool isPlaying() const { return isPlugin ? engine.isPlaying.load() : engine.playRequest.load(); }
    void setBpm (double bpm);
    double bpm() const { return engine.bpm.load(); }
    void tap();
    void setMaster (int v);
    static juce::String masterText (int v);

    //== Files =================================================================
    juce::File libraryFolder() const;
    juce::Array<juce::File> libraryFiles() const;
    bool save();                                  // false: needs a name (Save as…)
    bool saveAs (const juce::String& name);
    void load (const juce::File& f);
    void newPattern();
    void setPreview (const juce::String& path);   // toggles preview of a browser file
    void previewSlice (uint32_t id, int sliceIndex);

    juce::var stateToVar() const;
    void stateFromVar (const juce::var& v);

    void publish();

private:
    void timerCallback() override;
    void checkMissing();
    int colourForFile (const juce::File& f) const;
    void remapGrid (Track& t, int oldPer, int newPer);

    std::deque<Document> undoStack, redoStack;
    const void* lastGesture = nullptr;
    juce::uint32 lastGestureTime = 0;
    std::vector<EngineSnapshot::Ptr> releasePool;
    juce::uint32 lastPurge = 0;
    int lastEnginePart = -1, lastQueued = -2;
    bool lastPlaying = false;
    std::vector<double> taps;

    struct BarClip
    {
        int part = 0, from = 0;
        // per track: steps per bar and the hits by step
        std::map<uint32_t, std::pair<int, std::map<int, Hit>>> data;
    };
    struct TrackClip
    {
        int part = 0, from = 0;      // from = -1: every bar
        int bars = 1;                // bars of the part it came from
        uint32_t track = 0;
        juce::String name;
        int per = 16;
        std::map<int, Hit> hits;     // key: bar (relative to `from`) and step
    };
    std::optional<BarClip> barClip;
    std::optional<TrackClip> trackClip;
    std::optional<Part> partClip;
};

} // namespace ok
