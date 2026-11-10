#include "SampleStore.h"

#include <signalsmith-stretch/signalsmith-stretch.h>

namespace ok
{

SampleStore::SampleStore() : juce::Thread ("OKTAKT stretch")
{
    formatManager.registerBasicFormats();
    startThread (juce::Thread::Priority::low);
}

SampleStore::~SampleStore()
{
    signalThreadShouldExit();
    notify();
    stopThread (4000);
}

bool SampleStore::isAudioFile (const juce::File& f) const
{
    auto ext = f.getFileExtension().toLowerCase();
    return ext == ".wav" || ext == ".aif" || ext == ".aiff" || ext == ".flac" || ext == ".mp3" || ext == ".ogg";
}

SampleData::Ptr SampleStore::getCached (const juce::String& path) const
{
    auto it = cache.find (path);
    return it != cache.end() ? it->second : nullptr;
}

SampleData::Ptr SampleStore::get (const juce::String& path)
{
    // A path from another system (C:\… on Linux, /home/… on Windows) is simply not found
    if (path.isEmpty() || ! juce::File::isAbsolutePath (path)) return nullptr;
    if (auto c = getCached (path)) return c;

    juce::File f (path);
    if (! f.existsAsFile()) return nullptr;

    std::unique_ptr<juce::AudioFormatReader> reader (formatManager.createReaderFor (f));
    if (reader == nullptr || reader->lengthInSamples <= 0) return nullptr;

    const auto maxLen = (juce::int64) (reader->sampleRate * 600.0);   // 10 minutes is plenty for a sampler
    const int len = (int) juce::jmin (reader->lengthInSamples, maxLen);

    SampleData::Ptr s = new SampleData();
    s->path = path;
    s->sampleRate = reader->sampleRate > 0 ? reader->sampleRate : 44100.0;
    s->isStereo = reader->numChannels > 1;
    s->buffer.setSize (2, len);
    s->buffer.clear();

    if (s->isStereo)
        reader->read (&s->buffer, 0, len, 0, true, true);
    else
    {
        reader->read (&s->buffer, 0, len, 0, true, false);
        s->buffer.copyFrom (1, 0, s->buffer, 0, 0, len);
    }

    analyse (*s);
    cache[path] = s;
    return s;
}

void SampleStore::analyse (SampleData& s)
{
    const int len = s.length();
    const float* l = s.buffer.getReadPointer (0);
    const float* r = s.buffer.getReadPointer (1);

    // Peaks for the waveform
    s.peaks.assign (SampleData::kPeakBins, 0.0f);
    float maxPeak = 0.0f;
    for (int b = 0; b < SampleData::kPeakBins; ++b)
    {
        int a = (int) ((juce::int64) b * len / SampleData::kPeakBins);
        int e = juce::jmax (a + 1, (int) ((juce::int64) (b + 1) * len / SampleData::kPeakBins));
        float m = 0.0f;
        for (int i = a; i < juce::jmin (e, len); ++i)
            m = juce::jmax (m, std::abs (l[i]), std::abs (r[i]));
        s.peaks[(size_t) b] = m;
        maxPeak = juce::jmax (maxPeak, m);
    }
    if (maxPeak > 0.0f)
        for (auto& p : s.peaks) p /= maxPeak;

    // Onsets: rise of pre-emphasised energy in ~5 ms frames
    const int hop = juce::jmax (64, (int) (s.sampleRate * 0.005));
    const int frames = len / hop;
    s.onsets.clear();
    s.onsetStrength.clear();
    if (frames < 4) return;

    std::vector<float> logE ((size_t) frames, -100.0f);
    float prev = 0.0f, maxLog = -100.0f;
    for (int f = 0; f < frames; ++f)
    {
        double e = 0.0;
        for (int i = f * hop; i < (f + 1) * hop; ++i)
        {
            float x = 0.5f * (l[i] + r[i]);
            float d = x - 0.95f * prev;
            prev = x;
            e += (double) d * d + 0.1 * (double) x * x;
        }
        logE[(size_t) f] = (float) (10.0 * std::log10 (e / hop + 1e-12));
        maxLog = juce::jmax (maxLog, logE[(size_t) f]);
    }

    std::vector<float> flux ((size_t) frames, 0.0f);
    for (int f = 1; f < frames; ++f)
    {
        if (logE[(size_t) f] < maxLog - 45.0f) continue;
        float avg = 0.0f;
        int n = 0;
        for (int k = juce::jmax (0, f - 4); k < f; ++k) { avg += logE[(size_t) k]; ++n; }
        avg /= (float) juce::jmax (1, n);
        flux[(size_t) f] = juce::jmax (0.0f, logE[(size_t) f] - avg);
    }

    struct Cand { int frame; float v; };
    std::vector<Cand> cands;
    for (int f = 1; f < frames; ++f)
    {
        float v = flux[(size_t) f];
        if (v < 3.0f) continue;
        bool isMax = true;
        for (int k = juce::jmax (0, f - 3); k <= juce::jmin (frames - 1, f + 3); ++k)
            if (k != f && (flux[(size_t) k] > v || (flux[(size_t) k] == v && k < f))) { isMax = false; break; }
        if (isMax) cands.push_back ({ f, v });
    }

    // Minimum distance of 60 ms, keep the stronger one
    const int minGap = juce::jmax (1, (int) (0.06 * s.sampleRate / hop));
    std::vector<Cand> kept;
    for (auto& c : cands)
    {
        if (! kept.empty() && c.frame - kept.back().frame < minGap)
        {
            if (c.v > kept.back().v) kept.back() = c;
            continue;
        }
        kept.push_back (c);
    }

    float maxFlux = 0.0f;
    for (auto& c : kept) maxFlux = juce::jmax (maxFlux, c.v);
    if (maxFlux <= 0.0f) return;

    for (auto& c : kept)
    {
        // Refine to the first sample of the attack inside the frame window
        int a = juce::jmax (0, (c.frame - 1) * hop), e = juce::jmin (len, (c.frame + 1) * hop);
        float m = 0.0f;
        for (int i = a; i < e; ++i) m = juce::jmax (m, std::abs (0.5f * (l[i] + r[i])));
        int pos = c.frame * hop;
        for (int i = a; i < e; ++i)
            if (std::abs (0.5f * (l[i] + r[i])) >= 0.3f * m) { pos = i; break; }
        pos = juce::jmax (0, pos - 32);

        double frac = (double) pos / len;
        if (frac < 0.004 || frac > 0.998) continue;
        s.onsets.push_back (frac);
        s.onsetStrength.push_back (std::sqrt (c.v / maxFlux));
    }
}

std::vector<double> SampleStore::slicePositionsByHits (const SampleData& s, int sensitivity)
{
    std::vector<double> out;
    const float th = 1.0f - (float) sensitivity / 100.0f;
    for (size_t i = 0; i < s.onsets.size(); ++i)
        if (s.onsetStrength[i] >= th - 1e-6f) out.push_back (s.onsets[i]);
    return out;
}

//==============================================================================
StretchedData::Ptr SampleStore::getStretched (const juce::String& path, int stretchPercent)
{
    auto it = stretched.find ({ path, stretchPercent });
    return it != stretched.end() ? it->second : nullptr;
}

void SampleStore::requestStretches (const std::map<juce::String, std::set<int>>& wanted)
{
    auto isWanted = [&wanted] (const juce::String& path, int v)
    {
        auto w = wanted.find (path);
        return w != wanted.end() && w->second.count (v) != 0;
    };
    bool added = false;
    {
        std::lock_guard<std::mutex> lg (jobLock);
        // Values nobody uses any more (a knob or a hit being dragged past them) are dropped from the queue
        for (auto j = jobs.begin(); j != jobs.end();)
        {
            if (! isWanted (j->first->path, j->second))
            {
                pending.erase ({ j->first->path, j->second });
                j = jobs.erase (j);
            }
            else ++j;
        }
        for (auto& [path, values] : wanted)
        {
            auto src = getCached (path);
            if (src == nullptr) continue;
            for (int v : values)
            {
                StretchKey key { path, v };
                if (stretched.count (key) != 0 || pending.count (key) != 0) continue;
                pending.insert (key);
                jobs.emplace_back (src, v);
                added = true;
            }
        }
    }
    if (added) notify();
}

void SampleStore::run()
{
    while (! threadShouldExit())
    {
        std::pair<SampleData::Ptr, int> job;
        {
            std::lock_guard<std::mutex> lg (jobLock);
            if (! jobs.empty())
            {
                job = jobs.front();
                jobs.pop_front();
            }
        }

        if (job.first == nullptr)
        {
            wait (500);
            continue;
        }

        auto& src = *job.first;
        const int inLen = src.length();
        const int outLen = juce::jmax (1, (int) std::lround ((double) inLen * job.second / 100.0));

        StretchedData::Ptr out = new StretchedData();
        out->stretch = job.second;
        out->buffer.setSize (2, outLen);
        out->buffer.clear();

        bool done = false;
        {
            signalsmith::stretch::SignalsmithStretch<float> st;
            st.presetDefault (2, (float) src.sampleRate);
            if (inLen >= st.outputSeekLength ((float) inLen / (float) outLen) + 16)
            {
                auto* in = src.buffer.getArrayOfReadPointers();
                auto* o = out->buffer.getArrayOfWritePointers();
                done = st.exact (in, inLen, o, outLen);
            }
        }

        if (! done)
        {
            // Too short for the stretcher: plain resampling
            for (int c = 0; c < 2; ++c)
            {
                auto* in = src.buffer.getReadPointer (c);
                auto* o = out->buffer.getWritePointer (c);
                for (int i = 0; i < outLen; ++i)
                {
                    double p = (double) i * inLen / outLen;
                    int i0 = (int) p;
                    double fr = p - i0;
                    float a = in[juce::jmin (i0, inLen - 1)], b = in[juce::jmin (i0 + 1, inLen - 1)];
                    o[i] = (float) (a + (b - a) * fr);
                }
            }
        }

        {
            std::lock_guard<std::mutex> lg (jobLock);
            finished.push_back ({ { src.path, job.second }, out });
        }
        triggerAsyncUpdate();
    }
}

void SampleStore::handleAsyncUpdate()
{
    std::vector<std::pair<StretchKey, StretchedData::Ptr>> done;
    {
        std::lock_guard<std::mutex> lg (jobLock);
        done.swap (finished);
    }
    for (auto& [k, d] : done)
    {
        stretched[k] = d;
        pending.erase (k);
    }
    if (! done.empty() && onStretchReady) onStretchReady();
}

void SampleStore::purge (const juce::StringArray& keepPaths)
{
    for (auto it = cache.begin(); it != cache.end();)
    {
        if (it->second->getReferenceCount() == 1 && ! keepPaths.contains (it->first))
            it = cache.erase (it);
        else ++it;
    }
    for (auto it = stretched.begin(); it != stretched.end();)
    {
        if (it->second->getReferenceCount() == 1 && ! keepPaths.contains (it->first.path))
            it = stretched.erase (it);
        else ++it;
    }
    // Keep at most a few stretched versions per sample that nobody uses
    std::map<juce::String, int> count;
    for (auto it = stretched.rbegin(); it != stretched.rend(); ++it)
        ++count[it->first.path];
    for (auto it = stretched.begin(); it != stretched.end();)
    {
        if (count[it->first.path] > 3 && it->second->getReferenceCount() == 1)
        {
            --count[it->first.path];
            it = stretched.erase (it);
        }
        else ++it;
    }
}

} // namespace ok
