#include "Engine.h"

namespace ok
{

using namespace dsp;

struct Engine::Voice
{
    bool active = false;
    uint32_t trackId = 0;
    uint64_t order = 0;

    SampleData::Ptr sampleRef;
    StretchedData::Ptr stretchRef;
    const float* data[2] {};
    int len = 0;
    double pos = 0, lo = 0, hi = 0, inc = 1;
    bool reverse = false, swapLR = false;

    float gainL = 1, gainR = 1;
    int fadeInLeft = 0;
    float fadeInLen = 1, fadeOutLen = 64;
    double userIn = 0, userOut = 0;   // One-shot IN / OUT: fade lengths in samples of the buffer, from lo and to hi

    bool killing = false;
    float killGain = 1, killStep = 0;

    AmpMode amp = AmpMode::OneShot;
    int stage = 0;
    float env = 1, atkInc = 1, decCoef = 0, sus = 1, relCoef = 0;
    int holdLeft = 0, gateLeft = 0;

    FilterType ftype = FilterType::LP24;
    bool filterOn = false;
    float cutHz = 20000, q = 0.7f, reso01 = 0, envSemis = 0, fenv = 0, fenvCoef = 1, morph = 0;
    Svf f1[2], f2[2];
    Ladder lad[2];
    Comb comb[2];
    Formant form[2];
    bool hpOn = false, lpOn = false;
    Svf hp[2], lp[2];
    int sub = 0;

    float pre = 1, drivePre = 1, comp = 1;
    bool driveOn = false;
    bool bitsOn = false;
    float bitStep = 0;
    bool srOn = false;
    float srFactor = 1, srCount = 0, heldL = 0, heldR = 0;

    bool transOn = false;
    float punchDb = 0, tailDb = 0;
    Follower fa, fb, fc, fd;

    StereoMode stereo = StereoMode::Original;
    float width = 1;
    int haas = 0, haasW = 0;
    std::vector<float> haasBuf = std::vector<float> (8192, 0.0f);

    void updateFilter (float sr)
    {
        float hz = cutHz * std::exp2 (envSemis * fenv / 12.0f);
        for (int c = 0; c < 2; ++c)
        {
            switch (ftype)
            {
                case FilterType::LP12: case FilterType::HP12:
                    f1[c].set (hz, q, sr); break;
                case FilterType::LP24: case FilterType::HP24:
                    f1[c].set (hz, 0.54f, sr); f2[c].set (hz, q, sr); break;
                case FilterType::BP: case FilterType::Notch: case FilterType::Morph:
                    f1[c].set (hz, q, sr); break;
                case FilterType::Ladder:
                    lad[c].set (hz, reso01, sr); break;
                case FilterType::Comb:
                    comb[c].set (hz, reso01, sr); break;
                case FilterType::Formant:
                    form[c].set (juce::jlimit (0.0f, 1.0f, (float) (std::log2 (hz / 20.0f) / std::log2 (1000.0f))), reso01, sr); break;
                case FilterType::Count: break;
            }
        }
    }

    inline float filterSample (int c, float x)
    {
        float l, b, h;
        switch (ftype)
        {
            case FilterType::LP12:  f1[c].process (x, l, b, h); return l;
            case FilterType::HP12:  f1[c].process (x, l, b, h); return h;
            case FilterType::LP24:  f1[c].process (x, l, b, h); f2[c].process (l, l, b, h); return l;
            case FilterType::HP24:  f1[c].process (x, l, b, h); f2[c].process (h, l, b, h); return h;
            case FilterType::BP:    f1[c].process (x, l, b, h); return b * f1[c].k;
            case FilterType::Notch: f1[c].process (x, l, b, h); return x - f1[c].k * b;
            case FilterType::Ladder: return lad[c].process (x);
            case FilterType::Morph:
            {
                f1[c].process (x, l, b, h);
                float bn = b * f1[c].k;
                return morph < 0.5f ? l + (bn - l) * (morph * 2.0f) : bn + (h - bn) * (morph * 2.0f - 1.0f);
            }
            case FilterType::Comb:    return comb[c].process (x);
            case FilterType::Formant: return form[c].process (x);
            case FilterType::Count:   break;
        }
        return x;
    }

    inline float read (const float* d, double p) const
    {
        int i1 = (int) std::floor (p);
        float t = (float) (p - i1);
        auto at = [&] (int k) { return d[juce::jlimit (0, len - 1, k)]; };
        float y0 = at (i1 - 1), y1 = at (i1), y2 = at (i1 + 1), y3 = at (i1 + 2);
        float c1 = 0.5f * (y2 - y0);
        float c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
        float c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
        return ((c3 * t + c2) * t + c1) * t + y1;
    }

    void kill (float sr)
    {
        if (! killing) { killing = true; killGain = 1.0f; killStep = 1.0f / (0.003f * sr); }
    }

    void finish()
    {
        active = false;
        sampleRef = nullptr;   // the store keeps another reference, so this never frees on the audio thread
        stretchRef = nullptr;
    }

    void render (float* L, float* R, int n, float sr)
    {
        for (int i = 0; i < n; ++i)
        {
            double remaining = reverse ? (pos - lo) / -inc : (hi - pos) / inc;
            if (remaining <= 0.0) { finish(); return; }

            float sl = read (data[0], pos), sr_ = read (data[1], pos);
            if (swapLR) std::swap (sl, sr_);
            pos += inc;

            float edge = 1.0f;
            if (remaining < fadeOutLen) edge = (float) remaining / fadeOutLen;
            if (fadeInLeft > 0) { edge *= 1.0f - (float) fadeInLeft / fadeInLen; --fadeInLeft; }
            if (userIn > 0.0 && pos - lo < userIn) edge *= (float) juce::jmax (0.0, (pos - lo) / userIn);
            if (userOut > 0.0 && hi - pos < userOut) edge *= (float) juce::jmax (0.0, (hi - pos) / userOut);

            if (srOn)
            {
                srCount += 1.0f;
                if (srCount >= srFactor) { srCount -= srFactor; heldL = sl; heldR = sr_; }
                sl = heldL; sr_ = heldR;
            }
            if (bitsOn)
            {
                sl = std::round (sl / bitStep) * bitStep;
                sr_ = std::round (sr_ / bitStep) * bitStep;
            }
            sl *= pre; sr_ *= pre;
            if (driveOn) { sl = fastTanh (sl * drivePre); sr_ = fastTanh (sr_ * drivePre); }
            sl *= comp; sr_ *= comp;

            if (filterOn)
            {
                if (sub-- <= 0)
                {
                    sub = 15;
                    if (envSemis != 0.0f) { updateFilter (sr); fenv *= fenvCoef; }
                }
                sl = filterSample (0, sl);
                sr_ = filterSample (1, sr_);
            }
            if (hpOn) { float l, b, h; hp[0].process (sl, l, b, h); sl = h; hp[1].process (sr_, l, b, h); sr_ = h; }
            if (lpOn) { float l, b, h; lp[0].process (sl, l, b, h); sl = l; lp[1].process (sr_, l, b, h); sr_ = l; }

            if (transOn)
            {
                float m = std::abs (sl) + std::abs (sr_);
                float a1 = fa.process (m), a2 = fb.process (m), s1 = fc.process (m), s2 = fd.process (m);
                float att = juce::jmax (0.0f, a1 - a2) / (a1 + 1e-6f);
                float tl = juce::jmax (0.0f, s2 - s1) / (s2 + 1e-6f);
                float g = juce::Decibels::decibelsToGain (punchDb * att + tailDb * tl);
                sl *= g; sr_ *= g;
            }

            // amp envelope
            switch (amp)
            {
                case AmpMode::OneShot: break;
                case AmpMode::AHD:
                    if (stage == 0) { env += atkInc; if (env >= 1.0f) { env = 1.0f; stage = 1; } }
                    else if (stage == 1) { if (--holdLeft <= 0) stage = 2; }
                    else { env *= decCoef; if (env < 1e-4f) { finish(); return; } }
                    break;
                case AmpMode::ADSR:
                    if (stage < 3 && --gateLeft <= 0) stage = 3;
                    if (stage == 0) { env += atkInc; if (env >= 1.0f) { env = 1.0f; stage = 1; } }
                    else if (stage == 1) { env = sus + (env - sus) * decCoef; }
                    else if (stage == 3) { env *= relCoef; if (env < 1e-4f) { finish(); return; } }
                    break;
            }
            float g = env * edge;
            if (killing)
            {
                killGain -= killStep;
                if (killGain <= 0.0f) { finish(); return; }
                g *= killGain;
            }
            sl *= g; sr_ *= g;

            switch (stereo)
            {
                case StereoMode::Original: break;
                case StereoMode::Mono: { float m = 0.5f * (sl + sr_); sl = sr_ = m; break; }
                case StereoMode::Narrow:
                case StereoMode::Wide:
                {
                    float m = 0.5f * (sl + sr_), s = 0.5f * (sl - sr_) * width;
                    sl = m + s; sr_ = m - s;
                    break;
                }
                case StereoMode::Haas:
                {
                    haasBuf[(size_t) haasW] = sr_;
                    sr_ = haasBuf[(size_t) ((haasW - haas) & 8191)];
                    haasW = (haasW + 1) & 8191;
                    break;
                }
                case StereoMode::Alternate: case StereoMode::Count: break;
            }

            L[i] += sl * gainL;
            R[i] += sr_ * gainR;
        }
    }
};

//==============================================================================
Engine::Engine()
{
    for (int i = 0; i < kMaxVoices; ++i)
        voices.push_back (std::make_unique<Voice>());
    events.reserve (4096);
    scratch.reserve (kMaxBars * kKeyStride);
    beatEvents.reserve (64);
}

Engine::~Engine() = default;

void Engine::prepare (double sampleRate, int maxBlockSize)
{
    sr = sampleRate > 0 ? sampleRate : 48000.0;
    maxBlock = juce::jmax (16, maxBlockSize);
    trackBuf.setSize (kMaxTracks * 2, maxBlock, false, true, true);
    for (auto& v : voices) v->finish();
    trackGain.fill (1.0f);
}

void Engine::setSnapshot (EngineSnapshot::Ptr s)
{
    const juce::SpinLock::ScopedLockType sl (snapLock);
    pending = s;
}

void Engine::preview (SampleData::Ptr s, double startFrac, double endFrac)
{
    const juce::SpinLock::ScopedLockType sl (previewLock);
    previewPending = s;
    previewChanged = true;
    previewPos = startFrac;
    previewEnd = endFrac;
    previewing = s != nullptr;
}

void Engine::stopPreview() { preview (nullptr); }

Engine::Voice* Engine::allocVoice()
{
    Voice* oldest = nullptr;
    for (auto& v : voices)
    {
        if (! v->active) return v.get();
        if (oldest == nullptr || v->order < oldest->order) oldest = v.get();
    }
    oldest->finish();
    return oldest;
}

//==============================================================================
void Engine::collectEvents (const EngineSnapshot& snap, int partIdx, double ps,
                            double w0, double w1, double blockStart, double ppqPerSample, int numSamples)
{
    const auto& doc = snap.doc;
    const auto& part = doc.parts[(size_t) partIdx];
    const double L = part.bars * 4.0;
    const double rel0 = w0 - ps, rel1 = w1 - ps;
    if (rel1 <= rel0) return;

    static const Hit randomHit;

    auto emit = [&] (double relTime, int ti, const Hit* h, float vel, int slice)
    {
        if (relTime < rel0 || relTime >= rel1) return;
        int off = (int) std::floor ((ps + relTime - blockStart) / ppqPerSample);
        off = juce::jlimit (0, numSamples - 1, off);
        if (events.size() < events.capacity())
            events.push_back ({ off, ti, h, vel, slice });
    };

    for (int ti = 0; ti < (int) doc.tracks.size(); ++ti)
    {
        const auto& tr = doc.tracks[(size_t) ti];
        const auto& rt = snap.rt[(size_t) ti];
        if (rt.sample == nullptr) continue;

        const int per = tr.stepsPerBar();
        const double stepLen = 4.0 / per;
        const double swingOff = (doc.swing / 100.0 - 0.5) * 2.0 * stepLen;
        const bool slices = tr.sound.play == PlayMode::Slices && ! tr.sound.slices.empty();
        const int nSlices = (int) tr.sound.slices.size();

        scratch.clear();
        auto it = part.tracks.find (tr.id);
        bool anyMarked = false;
        if (it != part.tracks.end())
            for (auto& [key, h] : it->second.hits)
                anyMarked = anyMarked || (h.marked && barOfKey (key) < part.bars && stepOfKey (key) < per);
        static const StepSettings noStep;
        if (it != part.tracks.end())
        {
            int order = 0;
            for (auto& [key, h] : it->second.hits)
            {
                int bar = barOfKey (key), st = stepOfKey (key);
                if (bar >= part.bars || st >= per) continue;
                double nominal = bar * 4.0 + st * stepLen;
                scratch.push_back ({ nominal, &h, key, order++ });
            }
        }

        // The track's Random / Accuracy; a hit may set its own in the automation rows
        const double rnd = tr.random / 100.0;
        const double inacc = (100 - tr.accuracy) / 100.0;

        int pFirst = juce::jmax (0, (int) std::floor (rel0 / L) - 1);
        int pLast = (int) std::floor (rel1 / L) + 1;

        for (int pass = pFirst; pass <= pLast; ++pass)
        {
            const double base = pass * L;
            const int passNo = passOffset + pass;   // counts on across DAW loop turns
            const uint64_t seed = playSeed + (uint64_t) passNo * 7919ull;

            for (size_t j = 0; j < scratch.size(); ++j)
            {
                const auto& ref = scratch[j];
                const Hit& h = *ref.hit;
                const int st = stepOfKey (ref.key);
                // The track's STEP: on the marked hits, or on all when none is marked
                const bool inStep = ! anyMarked || h.marked;
                const StepSettings& step = inStep ? tr.step : noStep;

                if (! step.playsOnPass (passNo)) continue;
                if (step.chance < 100 && hash01 (seed, tr.id, (uint64_t) ref.key, 1) * 100.0 >= step.chance) continue;

                const double hRnd = h.pinned ? 0.0 : effectiveValue (tr, h, ParamId::Random) / 100.0;
                const double hInacc = h.pinned ? 0.0 : (100 - effectiveValue (tr, h, ParamId::Accuracy)) / 100.0;

                float vel = (float) h.velocity;
                if (hRnd > 0.0)
                {
                    if (hash01 (seed, tr.id, (uint64_t) ref.key, 2) < hRnd * 0.5) continue;
                    vel *= (float) (1.0 + (hash01 (seed, tr.id, (uint64_t) ref.key, 3) * 2.0 - 1.0) * hRnd * 0.5);
                    vel = juce::jlimit (1.0f, 127.0f, vel);
                }

                double t = ref.nominal;
                if ((st % tr.steps) % 2 == 1) t += swingOff;
                t += effectiveValue (tr, h, ParamId::Micro) / 24.0 * stepLen;
                if (hInacc > 0.0)
                {
                    // Accuracy loosens both timing and level
                    t += (hash01 (seed, tr.id, (uint64_t) ref.key, 4) * 2.0 - 1.0) * hInacc * 0.5 * stepLen;
                    vel *= (float) (1.0 + (hash01 (seed, tr.id, (uint64_t) ref.key, 8) * 2.0 - 1.0) * hInacc * 0.5);
                    vel = juce::jlimit (1.0f, 127.0f, vel);
                }

                int slice = -1;
                if (slices)
                {
                    slice = h.sliceId >= 0 ? resolveSliceIndex (tr.sound, h.sliceId) : -1;
                    if (slice < 0) slice = ref.order % nSlices;
                }

                if (step.retrig > 0)
                {
                    const double rate = retrigPpq (step.retrig);
                    const double dur = step.retrigLen * stepLen;
                    const int n = juce::jmax (1, (int) std::floor (dur / rate + 1e-6));
                    const double next = j + 1 < scratch.size() ? scratch[j + 1].nominal
                                                               : (scratch.empty() ? L : L + scratch[0].nominal);
                    for (int r = 0; r < n; ++r)
                    {
                        double tt = t + r * rate;
                        if (r > 0 && tt >= next - 1e-9) break;
                        float frac = n > 1 ? (float) r / (float) (n - 1) : 0.0f;
                        float g = step.fade < 0 ? 1.0f + step.fade / 100.0f * frac
                                                : 1.0f - step.fade / 100.0f * (1.0f - frac);
                        emit (base + tt, ti, &h, vel * juce::jmax (0.0f, g), slice);
                    }
                }
                else
                    emit (base + t, ti, &h, vel, slice);
            }

            // The track's Random also adds quieter hits to empty cells
            if (rnd > 0.0)
            {
                int ref = 0;
                for (int bar = 0; bar < part.bars; ++bar)
                {
                    for (int st = 0; st < per; ++st, ++ref)
                    {
                        int key = keyOf (bar, st);
                        if (it != part.tracks.end() && it->second.hits.count (key) != 0) continue;
                        if (hash01 (seed, tr.id, (uint64_t) key, 5) >= rnd * 0.25) continue;
                        double t = bar * 4.0 + st * stepLen;
                        if ((st % tr.steps) % 2 == 1) t += swingOff;
                        t += tr.micro / 24.0 * stepLen;
                        float vel = (float) (127.0 * (0.35 + 0.25 * hash01 (seed, tr.id, (uint64_t) key, 7)));
                        if (inacc > 0.0)
                        {
                            t += (hash01 (seed, tr.id, (uint64_t) key, 6) * 2.0 - 1.0) * inacc * 0.5 * stepLen;
                            vel *= (float) (1.0 + (hash01 (seed, tr.id, (uint64_t) key, 9) * 2.0 - 1.0) * inacc * 0.5);
                            vel = juce::jlimit (1.0f, 127.0f, vel);
                        }
                        emit (base + t, ti, &randomHit, vel, slices ? ref % nSlices : -1);
                    }
                }
            }
        }
    }
}

void Engine::startVoice (const EngineSnapshot& snap, const Event& e, double bpmNow)
{
    const auto& tr = snap.doc.tracks[(size_t) e.track];
    const auto& rt = snap.rt[(size_t) e.track];
    const auto& s = tr.sound;
    if (rt.sample == nullptr) return;
    const Hit& h = *e.hit;
    const float fsr = (float) sr;

    if (s.cut)
        for (auto& v : voices)
            if (v->active && v->trackId == tr.id) v->kill (fsr);

    double a = s.regionStart, b = s.regionEnd;
    if (s.play == PlayMode::Slices && e.sliceIdx >= 0 && e.sliceIdx < (int) s.slices.size())
    {
        a = s.slices[(size_t) e.sliceIdx].start;
        b = e.sliceIdx + 1 < (int) s.slices.size() ? s.slices[(size_t) e.sliceIdx + 1].start : 1.0;
    }
    const double stF = s.start / 127.0;
    const double lnF = s.length / 127.0;
    double s0 = a + (b - a) * stF;
    double e0 = juce::jmin (b, s0 + (b - a) * lnF);
    if (e0 <= s0) return;

    auto* v = allocVoice();
    v->order = ++voiceCounter;
    v->trackId = tr.id;
    v->sampleRef = rt.sample;

    // Stretch of this hit: pitch kept uses a stretched copy (plays unstretched until it is ready), repitch changes the rate
    const int stretch = effectiveValue (tr, h, ParamId::Stretch);
    const StretchedData* st = nullptr;
    if (s.sampleMode == SampleMode::KeepPitch && stretch != 100)
        for (auto& [pct, data] : rt.perHit)
            if (pct == stretch) { st = data.get(); break; }
    const juce::AudioBuffer<float>& buf = st != nullptr ? st->buffer : rt.sample->buffer;
    v->stretchRef = const_cast<StretchedData*> (st);
    v->data[0] = buf.getReadPointer (0);
    v->data[1] = buf.getReadPointer (1);
    v->len = buf.getNumSamples();

    const float pitch = (float) effectiveValue (tr, h, ParamId::Pitch) + s.fine / 100.0f;
    double rate = rt.sample->sampleRate / sr * std::exp2 (pitch / 12.0);
    if (s.sampleMode == SampleMode::Repitch) rate *= 100.0 / stretch;

    v->lo = s0 * v->len;
    v->hi = e0 * v->len;
    v->reverse = s.reverse;
    v->inc = s.reverse ? -rate : rate;
    v->pos = s.reverse ? v->hi - 1.0 : v->lo;
    v->swapLR = s.swapLR;
    v->fadeOutLen = juce::jmax (1.0f, 0.002f * fsr);
    v->fadeInLen = juce::jmax (1.0f, 0.0007f * fsr);
    v->fadeInLeft = (s.reverse ? e0 < 0.9999 : s0 > 0.0001) ? (int) v->fadeInLen : 0;
    v->userIn = s.amp == AmpMode::OneShot ? (v->hi - v->lo) * s.fadeIn / 100.0 : 0.0;
    v->userOut = s.amp == AmpMode::OneShot ? (v->hi - v->lo) * s.fadeOut / 100.0 : 0.0;
    v->killing = false;
    v->killGain = 1.0f;

    // Level and pan
    const float velGain = std::pow (juce::jlimit (0.0f, 127.0f, e.velocity) / 127.0f, 1.5f);
    const float lvl = tr.volume / 100.0f;
    const float gain = velGain * lvl * lvl;
    float pan = tr.pan / 64.0f;
    if (s.stereo == StereoMode::Alternate)
    {
        int idx = juce::jlimit (0, kMaxTracks - 1, e.track);
        float side = (altCounter[(size_t) idx]++ % 2 == 0) ? -1.0f : 1.0f;
        pan += side * s.stereoAmt[(size_t) StereoMode::Alternate] / 100.0f;
    }
    pan = juce::jlimit (-1.0f, 1.0f, pan);
    v->gainL = gain * (pan > 0 ? 1.0f - pan : 1.0f);
    v->gainR = gain * (pan < 0 ? 1.0f + pan : 1.0f);

    // Amp envelope
    v->amp = s.amp;
    v->stage = 0;
    auto secs = [] (int k) { return k == 0 ? 0.0001f : knobToSeconds (k); };
    if (s.amp == AmpMode::OneShot) v->env = 1.0f;
    else
    {
        v->env = 0.0f;
        v->atkInc = 1.0f / juce::jmax (1.0f, secs (s.attack) * fsr);
        float dec = secs (s.decay);
        if (s.amp == AmpMode::AHD)
        {
            v->holdLeft = juce::jmax (1, (int) (knobToSeconds (s.hold) * fsr * (s.hold == 0 ? 0.0f : 1.0f)));
            v->decCoef = std::exp (std::log (1e-4f) / (dec * fsr));
        }
        else
        {
            v->decCoef = std::exp (std::log (0.01f) / (dec * fsr));
            v->sus = s.sustain / 127.0f;
            v->relCoef = std::exp (std::log (1e-4f) / (secs (s.release) * fsr));
            const double gatePpq = 1.0;   // the note is held for one beat
            v->gateLeft = juce::jmax (1, (int) (gatePpq * 60.0 / juce::jmax (1.0, bpmNow) * sr));
        }
    }

    // Character
    const int g = s.gain;
    const int drive = effectiveValue (tr, h, ParamId::Drive);
    const int bits = effectiveValue (tr, h, ParamId::BitReduction);
    v->pre = juce::Decibels::decibelsToGain ((float) g);
    v->driveOn = drive > 0;
    v->drivePre = 1.0f + std::pow (drive / 127.0f, 2.0f) * 24.0f;
    v->comp = 1.0f;
    if (s.autoGain)
    {
        float m = v->pre * (v->driveOn ? std::tanh (v->drivePre * 0.5f) / 0.5f : 1.0f);
        v->comp = 1.0f / juce::jmax (0.05f, m);
    }
    v->bitsOn = bits > 0;
    v->bitStep = std::pow (2.0f, 1.0f - (16.0f - bits / 127.0f * 14.0f));
    const int srRed = effectiveValue (tr, h, ParamId::SRReduction);
    v->srOn = srRed > 0;
    v->srFactor = 1.0f + std::pow (srRed / 127.0f, 2.0f) * 63.0f;
    v->srCount = v->srFactor;

    // Filter
    v->ftype = s.filter;
    const int cutoff = effectiveValue (tr, h, ParamId::Cutoff);
    const int reso = effectiveValue (tr, h, ParamId::Resonance);
    const int envAmt = effectiveValue (tr, h, ParamId::EnvAmount);
    v->cutHz = knobToHz ((float) cutoff);
    v->reso01 = reso / 127.0f;
    if (s.filter == FilterType::BP || s.filter == FilterType::Notch)
        v->q = 0.4f + std::pow (1.0f - v->reso01, 2.0f) * 10.0f;
    else
        v->q = 0.707f + v->reso01 * v->reso01 * 15.0f;
    v->envSemis = envAmt / 64.0f * 60.0f;
    v->fenv = 1.0f;
    v->fenvCoef = std::exp (std::log (1e-3f) / (0.005f * std::pow (1000.0f, s.envDec / 127.0f) * fsr / 16.0f));
    v->morph = s.morph / 127.0f;
    switch (s.filter)
    {
        case FilterType::LP12: case FilterType::LP24: case FilterType::Ladder:
            v->filterOn = ! (cutoff >= 127 && envAmt <= 0); break;
        case FilterType::HP12: case FilterType::HP24:
            v->filterOn = ! (cutoff <= 0 && envAmt >= 0); break;
        default: v->filterOn = true; break;
    }
    if (v->filterOn)
    {
        for (int c = 0; c < 2; ++c)
        {
            v->f1[c].reset(); v->f2[c].reset(); v->lad[c].reset(); v->form[c].reset();
            if (s.filter == FilterType::Comb) v->comb[c].reset();
        }
        v->updateFilter (fsr);
        v->sub = 15;
    }
    v->hpOn = s.baseHP > 0 && s.filter != FilterType::Morph;
    v->lpOn = s.baseLP < 127;
    for (int c = 0; c < 2; ++c)
    {
        v->hp[c].reset(); v->lp[c].reset();
        if (v->hpOn) v->hp[c].set (knobToHz ((float) s.baseHP), 0.707f, fsr);
        if (v->lpOn) v->lp[c].set (knobToHz ((float) s.baseLP), 0.707f, fsr);
    }

    // Transient
    const int punch = s.punch, tail = s.tail;
    v->transOn = punch != 0 || tail != 0;
    v->punchDb = punch / 100.0f * 15.0f;
    v->tailDb = tail / 100.0f * 15.0f;
    v->fa.env = v->fb.env = v->fc.env = v->fd.env = 0.0f;
    v->fa.set (0.0005f, 0.04f, fsr);
    v->fb.set (0.015f, 0.04f, fsr);
    v->fc.set (0.0005f, 0.03f, fsr);
    v->fd.set (0.0005f, 0.4f, fsr);

    // Stereo
    v->stereo = s.stereo;
    v->width = 1.0f;
    if (s.stereo == StereoMode::Narrow) v->width = s.stereoAmt[(size_t) StereoMode::Narrow] / 100.0f;
    if (s.stereo == StereoMode::Wide)   v->width = rt.sample->isStereo ? s.stereoAmt[(size_t) StereoMode::Wide] / 100.0f : 1.0f;
    if (s.stereo == StereoMode::Haas)
    {
        v->haas = juce::jlimit (1, 8000, (int) (s.stereoAmt[(size_t) StereoMode::Haas] * 0.001f * fsr));
        std::fill (v->haasBuf.begin(), v->haasBuf.end(), 0.0f);
        v->haasW = 0;
    }

    v->active = true;
}

void Engine::renderVoices (int from, int to)
{
    if (to <= from || current == nullptr) return;
    const auto& tracks = current->doc.tracks;
    for (auto& v : voices)
    {
        if (! v->active) continue;
        int idx = -1;
        for (int i = 0; i < (int) tracks.size(); ++i)
            if (tracks[(size_t) i].id == v->trackId) { idx = i; break; }
        if (idx < 0) { v->finish(); continue; }
        v->render (trackBuf.getWritePointer (idx * 2) + from, trackBuf.getWritePointer (idx * 2 + 1) + from, to - from, (float) sr);
    }
}

void Engine::renderMetronome (float* l, float* r, int from, int to)
{
    for (int i = from; i < to; ++i)
    {
        for (auto& be : beatEvents)
        {
            if (be.first == i)
            {
                click.active = true;
                click.phase = 0.0f;
                click.inc = juce::MathConstants<float>::twoPi * (be.second ? 2400.0f : 1600.0f) / (float) sr;
                click.amp = metronomeVolume.load() * (be.second ? 0.9f : 0.5f);
                click.decay = std::exp (std::log (1e-3f) / (0.035f * (float) sr));
            }
        }
        if (! click.active) continue;
        float s = std::sin (click.phase) * click.amp;
        click.phase += click.inc;
        click.amp *= click.decay;
        if (click.amp < 1e-4f) click.active = false;
        l[i] += s;
        r[i] += s;
    }
}

void Engine::renderPreview (float* l, float* r, int n)
{
    {
        const juce::SpinLock::ScopedTryLockType sl (previewLock);
        if (sl.isLocked() && previewChanged)
        {
            previewChanged = false;
            previewSample = previewPending;
            previewPending = nullptr;
            if (previewSample != nullptr)
            {
                previewInc = previewSample->sampleRate / sr;
                const double len = previewSample->length();
                previewPos = juce::jlimit (0.0, len, previewPos * len);
                previewEnd = juce::jlimit (0.0, len, previewEnd * len);
                previewFade = 1.0f;
            }
        }
    }
    if (previewSample == nullptr) return;

    auto* d0 = previewSample->buffer.getReadPointer (0);
    auto* d1 = previewSample->buffer.getReadPointer (1);
    for (int i = 0; i < n; ++i)
    {
        if (previewPos >= previewEnd - 1.0)
        {
            previewSample = nullptr;
            previewing = false;
            return;
        }
        int i0 = (int) previewPos;
        float fr = (float) (previewPos - i0);
        float a = d0[i0] + (d0[i0 + 1] - d0[i0]) * fr;
        float b = d1[i0] + (d1[i0 + 1] - d1[i0]) * fr;
        float rem = (float) ((previewEnd - previewPos) / (0.003 * sr));
        float g = juce::jmin (1.0f, rem) * 0.8f;
        l[i] += a * g;
        r[i] += b * g;
        previewPos += previewInc;
    }
}

//==============================================================================
void Engine::process (float* const* outs, int numBuses, int numSamples, const HostInfo& host)
{
    // A host loop that ends inside this block (Live, for one, does not split the block there):
    // play up to the loop end, then go on from the loop start, as if the host had split it
    if (host.isPlugin && host.playing && host.looping && host.loopEnd > host.loopStart + 1e-6 && numBuses <= kNumOutputs)
    {
        const double ppqPerSample = host.bpm / 60.0 / sr;
        const double end = host.ppq + numSamples * ppqPerSample;
        if (host.ppq < host.loopEnd - 1e-9 && end > host.loopEnd + 1e-9)
        {
            // the first part stops before the loop end, so a hit right on it plays after the wrap
            const int n1 = (int) std::floor ((host.loopEnd - host.ppq) / ppqPerSample + 1e-9);
            if (n1 >= 0 && n1 < numSamples)
            {
                if (n1 > 0) processSegment (outs, numBuses, n1, host);
                std::array<float*, 2 * kNumOutputs> rest {};
                for (int i = 0; i < 2 * numBuses; ++i)
                    rest[(size_t) i] = outs[i] != nullptr ? outs[i] + n1 : nullptr;
                HostInfo wrapped = host;
                wrapped.ppq = host.loopStart + (host.ppq + n1 * ppqPerSample - host.loopEnd);
                processSegment (rest.data(), numBuses, numSamples - n1, wrapped);
                return;
            }
        }
    }
    processSegment (outs, numBuses, numSamples, host);
}

void Engine::processSegment (float* const* outs, int numBuses, int numSamples, const HostInfo& host)
{
    {
        const juce::SpinLock::ScopedTryLockType sl (snapLock);
        if (sl.isLocked() && pending != nullptr)
        {
            current = pending;   // the session keeps every snapshot alive until we let go of it
            pending = nullptr;
        }
    }

    trackBuf.clear (0, numSamples);
    events.clear();
    beatEvents.clear();

    const bool playing = host.isPlugin ? host.playing : playRequest.load();
    const double bpmNow = host.isPlugin ? host.bpm : bpm.load();
    currentBpm = bpmNow;
    const double ppqPerSample = bpmNow / 60.0 / sr;

    double blockStart = host.isPlugin ? host.ppq : ppq;

    if (playing && ! wasPlaying)
    {
        if (! host.isPlugin) { ppq = 0.0; blockStart = 0.0; }
        startPpq = partStartPpq = blockStart;
        passOffset = 0;
        playSeed = (uint64_t) juce::Random::getSystemRandom().nextInt64();
        altCounter.fill (0);
    }
    else if (playing && host.isPlugin && std::abs (host.ppq - expectedHostPpq) > 0.02)
    {
        // The host jumped (loop or relocate): restart the current part from here. The pass count goes
        // on, so a DAW loop turn is the next pass for CONDITION (1:2 alternates), and the randomness
        // is fresh, or every turn would replay the same Random / Chance / Accuracy
        if (current != nullptr)
        {
            const int part = juce::jlimit (0, kNumParts - 1, currentPart.load());
            const double L = current->doc.parts[(size_t) part].bars * 4.0;
            // passes played since the part started; a host that ran a little past its loop end
            // (up to a 16th) has not begun another one
            passOffset += juce::jmax (1, (int) std::ceil ((expectedHostPpq - partStartPpq - 0.25) / L));
        }
        startPpq = partStartPpq = blockStart;
        playSeed = (uint64_t) juce::Random::getSystemRandom().nextInt64();
    }
    wasPlaying = playing;
    isPlaying = playing;

    if (playing && current != nullptr)
    {
        double blockEnd = blockStart + numSamples * ppqPerSample;
        // A block that ends on the host's loop end must not catch the hit right on it (rounding):
        // that one belongs to the next turn, after the jump back
        if (host.isPlugin && host.looping && blockStart < host.loopEnd && blockEnd > host.loopEnd - 1e-6)
            blockEnd = juce::jmax (blockStart, juce::jmin (blockEnd, host.loopEnd - 1e-7));
        double segStart = blockStart;

        while (segStart < blockEnd)
        {
            int part = juce::jlimit (0, kNumParts - 1, currentPart.load());
            const double L = current->doc.parts[(size_t) part].bars * 4.0;
            const int q = queuedPart.load();
            double boundary = blockEnd + 1.0;
            if (q >= 0 && q < kNumParts)
            {
                const auto mode = (SwitchMode) switchMode.load();
                // The next bar / part boundary; one we are standing on counts only if something was played before it
                auto next = [segStart] (double origin, double len)
                {
                    double n = std::ceil ((segStart - origin) / len - 1e-9);
                    if (n <= 0.0 && segStart <= origin + 1e-9) n = 1.0;
                    return origin + len * n;
                };
                if (mode == SwitchMode::Now) boundary = segStart;
                else if (mode == SwitchMode::BarEnd) boundary = next (startPpq, 4.0);
                else boundary = next (partStartPpq, L);
            }

            if (boundary <= segStart)
            {
                currentPart = q;
                queuedPart = -1;
                partStartPpq = segStart;
                passOffset = 0;
                continue;
            }

            const double segEnd = juce::jmin (blockEnd, boundary);
            collectEvents (*current, part, partStartPpq, segStart, segEnd, blockStart, ppqPerSample, numSamples);
            if (segEnd < boundary) break;

            currentPart = q;
            queuedPart = -1;
            partStartPpq = boundary;
            passOffset = 0;
            segStart = boundary;
        }

        // Metronome on every beat, counted from the start
        if (! host.isPlugin && metronomeOn.load())
        {
            double k0 = std::ceil (blockStart - startPpq - 1e-9);
            for (double k = k0; startPpq + k < blockEnd; k += 1.0)
            {
                int off = juce::jlimit (0, numSamples - 1, (int) std::floor ((startPpq + k - blockStart) / ppqPerSample));
                bool accent = metronomeAccent.load() && ((juce::int64) std::llround (k)) % juce::jlimit (2, 11, metronomeBeats.load()) == 0;
                beatEvents.push_back ({ off, accent });
            }
        }

        const int part = juce::jlimit (0, kNumParts - 1, currentPart.load());
        const double L = current->doc.parts[(size_t) part].bars * 4.0;
        posInPart = std::fmod (juce::jmax (0.0, blockEnd - partStartPpq), L);

        if (! host.isPlugin) ppq = blockEnd;
        expectedHostPpq = blockEnd;
    }

    // Render voices, starting new ones at their sample offsets
    // std::sort does not allocate (stable_sort may), so keep the audio thread allocation-free
    std::sort (events.begin(), events.end(), [] (const Event& a, const Event& b)
               { return a.offset != b.offset ? a.offset < b.offset : a.track < b.track; });
    int cursor = 0;
    for (auto& e : events)
    {
        if (e.offset > cursor) { renderVoices (cursor, e.offset); cursor = e.offset; }
        startVoice (*current, e, bpmNow);
    }
    renderVoices (cursor, numSamples);

    // Mix tracks into their outputs
    const float master = masterGain.load();
    if (current != nullptr)
    {
        const auto& tracks = current->doc.tracks;
        bool anySolo = false;
        for (auto& t : tracks) anySolo = anySolo || t.solo;

        for (int ti = 0; ti < (int) tracks.size(); ++ti)
        {
            const auto& t = tracks[(size_t) ti];
            const float target = (t.mute || (anySolo && ! t.solo)) ? 0.0f : 1.0f;
            const float g0 = trackGain[(size_t) ti];
            trackGain[(size_t) ti] = target;

            int bus = host.isPlugin ? juce::jlimit (0, kNumOutputs - 1, t.output) : 0;
            if (bus >= numBuses || outs[bus * 2] == nullptr) continue;

            for (int c = 0; c < 2; ++c)
            {
                auto* dst = outs[bus * 2 + c];
                auto* src = trackBuf.getReadPointer (ti * 2 + c);
                const float step = (target - g0) / (float) numSamples;
                float g = g0;
                for (int i = 0; i < numSamples; ++i, g += step)
                    dst[i] += src[i] * g * master;
            }
        }
    }

    if (numBuses > 0 && outs[0] != nullptr)
    {
        if (! beatEvents.empty() || click.active)
            renderMetronome (outs[0], outs[1], 0, numSamples);
        renderPreview (outs[0], outs[1], numSamples);
    }
}

} // namespace ok
