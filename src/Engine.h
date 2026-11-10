#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include "Model.h"
#include "SampleStore.h"
#include "Dsp.h"
#include <atomic>

namespace ok
{

struct TrackRuntime
{
    SampleData::Ptr sample;
    StretchedData::Ptr stretched;   // pitch-kept stretch of the track, if ready
    std::vector<std::pair<int, StretchedData::Ptr>> perHit;   // every ready stretch by percent (hits can set their own)
};

// Immutable copy of the document handed to the audio thread
struct EngineSnapshot : juce::ReferenceCountedObject
{
    using Ptr = juce::ReferenceCountedObjectPtr<EngineSnapshot>;
    Document doc;
    std::vector<TrackRuntime> rt;   // parallel to doc.tracks
};

class Engine
{
public:
    static constexpr int kMaxVoices = 128;

    Engine();
    ~Engine();

    void prepare (double sampleRate, int maxBlockSize);

    // message thread
    void setSnapshot (EngineSnapshot::Ptr s);
    void preview (SampleData::Ptr s, double startFrac = 0.0, double endFrac = 1.0);
    void stopPreview();

    struct HostInfo
    {
        bool isPlugin = false;
        bool playing = false;
        double bpm = 120.0;
        double ppq = 0.0;
        bool looping = false;               // the host's loop (cycle) is on and its points are known
        double loopStart = 0.0, loopEnd = 0.0;
    };

    // outs: 2 * numBuses channel pointers (already cleared); a null pair means the bus is inactive
    void process (float* const* outs, int numBuses, int numSamples, const HostInfo& host);

    // Transport, written by the UI
    std::atomic<bool>  playRequest { false };
    std::atomic<double> bpm { 120.0 };
    std::atomic<bool>  metronomeOn { false };
    std::atomic<float> metronomeVolume { 0.7f };
    std::atomic<bool>  metronomeAccent { true };
    std::atomic<int>   metronomeBeats { 4 };
    std::atomic<float> masterGain { 1.0f };
    std::atomic<int>   switchMode { (int) SwitchMode::PartEnd };
    std::atomic<int>   queuedPart { -1 };
    std::atomic<int>   currentPart { 0 };

    // Read by the UI
    std::atomic<bool>   isPlaying { false };
    std::atomic<double> posInPart { 0.0 };   // quarter notes from the start of the current pass
    std::atomic<double> currentBpm { 120.0 };
    std::atomic<bool>   previewing { false };

private:
    struct Event
    {
        int offset = 0;
        int track = 0;
        const Hit* hit = nullptr;
        float velocity = 100.0f;
        int sliceIdx = -1;
    };

    struct Voice;
    void processSegment (float* const* outs, int numBuses, int numSamples, const HostInfo& host);
    void collectEvents (const EngineSnapshot& snap, int partIdx, double partStart,
                        double w0, double w1, double blockStart, double ppqPerSample, int numSamples);
    void startVoice (const EngineSnapshot& snap, const Event& e, double bpmNow);
    Voice* allocVoice();
    void renderVoices (int from, int to);
    void renderMetronome (float* l, float* r, int from, int to);
    void renderPreview (float* l, float* r, int n);

    double sr = 48000.0;
    int maxBlock = 512;

    juce::SpinLock snapLock;
    EngineSnapshot::Ptr pending, current;

    std::vector<std::unique_ptr<Voice>> voices;
    uint64_t voiceCounter = 0;

    juce::AudioBuffer<float> trackBuf;   // 2 channels per track
    std::array<float, kMaxTracks> trackGain {};
    float masterSmoothed = 1.0f;

    std::vector<Event> events;
    struct HitRef { double nominal; const Hit* hit; int key; int order; };
    std::vector<HitRef> scratch;

    // transport
    bool wasPlaying = false;
    double ppq = 0.0;            // standalone position
    double expectedHostPpq = 0.0;
    double startPpq = 0.0;       // where play started, bars count from here
    double partStartPpq = 0.0;   // where the current part's pass 0 started
    int passOffset = 0;          // passes already played before partStartPpq (a DAW loop restarts the part, the count goes on)
    uint64_t playSeed = 1;
    std::array<int, kMaxTracks> altCounter {};

    // metronome
    struct Click { bool active = false; float phase = 0, inc = 0, amp = 0, decay = 0; int offset = 0; } click;
    std::vector<std::pair<int, bool>> beatEvents;

    // preview
    juce::SpinLock previewLock;
    SampleData::Ptr previewPending, previewSample;
    bool previewChanged = false;
    double previewPos = 0, previewEnd = 0, previewInc = 1;
    float previewFade = 1.0f;
};

} // namespace ok
