#pragma once

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_events/juce_events.h>
#include <map>
#include <set>
#include <mutex>
#include <deque>

namespace ok
{

// Decoded audio of one file. Shared between the message thread and the audio thread;
// only the SampleStore ever drops the last reference, so deletion never happens on the audio thread.
struct SampleData : juce::ReferenceCountedObject
{
    using Ptr = juce::ReferenceCountedObjectPtr<SampleData>;

    juce::String path;
    juce::AudioBuffer<float> buffer;   // always 2 channels (mono files are duplicated)
    bool isStereo = false;
    double sampleRate = 44100.0;

    std::vector<float> peaks;          // max |x| per bin, kPeakBins bins
    std::vector<double> onsets;        // positions (fraction 0…1), sorted
    std::vector<float> onsetStrength;  // 0…1 for each onset

    static constexpr int kPeakBins = 2000;

    int length() const { return buffer.getNumSamples(); }
    double seconds() const { return length() / sampleRate; }
};

struct StretchedData : juce::ReferenceCountedObject
{
    using Ptr = juce::ReferenceCountedObjectPtr<StretchedData>;
    juce::AudioBuffer<float> buffer;
    int stretch = 100;
};

class SampleStore : private juce::Thread, private juce::AsyncUpdater
{
public:
    SampleStore();
    ~SampleStore() override;

    // Loads synchronously on first use. Returns nullptr if the file is missing or unreadable.
    SampleData::Ptr get (const juce::String& path);
    SampleData::Ptr getCached (const juce::String& path) const;

    // Pitch-preserving stretch, computed in the background. Returns nullptr until ready.
    StretchedData::Ptr getStretched (const juce::String& path, int stretchPercent);
    // Queues every stretch the pattern needs (path -> percents); queued ones it no longer needs are dropped
    void requestStretches (const std::map<juce::String, std::set<int>>& wanted);

    // Drops cached entries nobody else references and that are not in keepPaths
    void purge (const juce::StringArray& keepPaths);

    std::function<void()> onStretchReady;   // called on the message thread

    juce::AudioFormatManager& formats() { return formatManager; }
    bool isAudioFile (const juce::File& f) const;

    static std::vector<double> slicePositionsByHits (const SampleData& s, int sensitivity);


private:
    void run() override;
    void handleAsyncUpdate() override;
    static void analyse (SampleData& s);

    juce::AudioFormatManager formatManager;
    std::map<juce::String, SampleData::Ptr> cache;

    struct StretchKey
    {
        juce::String path;
        int stretch;
        bool operator< (const StretchKey& o) const { return path != o.path ? path < o.path : stretch < o.stretch; }
    };
    std::map<StretchKey, StretchedData::Ptr> stretched;   // message thread only

    std::mutex jobLock;
    std::deque<std::pair<SampleData::Ptr, int>> jobs;
    std::vector<std::pair<StretchKey, StretchedData::Ptr>> finished;   // guarded by jobLock
    std::set<StretchKey> pending;                                     // message thread only
};

} // namespace ok
