// Self-check: renders the engine offline and saves UI snapshots. No audio device, no windows.
// Usage: OKTAKT_Check <demo-kit folder> <output folder> [path to the built OKTAKT.vst3]

#include "SystemAudio.h"
#include <juce_audio_utils/juce_audio_utils.h>
#include "PluginProcessor.h"
#include "ui/Editor.h"
#include "ui/Widgets.h"
#include "ui/GridView.h"

using namespace ok;

namespace
{

struct HostPlayHead : juce::AudioPlayHead
{
    double ppq = 0.0, bpm = 120.0;
    bool playing = true;
    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo p;
        p.setIsPlaying (playing);
        p.setBpm (bpm);
        p.setPpqPosition (ppq);
        p.setTimeSignature (juce::AudioPlayHead::TimeSignature {});
        return p;
    }
};

struct Runner : juce::Timer
{
    juce::File kit, out, vst3;
    int failures = 0, passes = 0;
    std::unique_ptr<OktaktProcessor> proc, plugin;
    std::unique_ptr<juce::AudioProcessorEditor> editor;
    std::vector<std::function<bool()>> steps;
    size_t stepIndex = 0;
    int waitTicks = 0;
    static constexpr double kSr = 48000.0;

    void check (bool ok, const juce::String& what)
    {
        std::printf ("%s  %s\n", ok ? "PASS" : "FAIL", what.toRawUTF8());
        (ok ? passes : failures)++;
    }

    Session& s() { return *proc->session; }
    Engine& e() { return proc->engine; }

    juce::File sample (const char* rel) { return kit.getChildFile (rel); }

    std::vector<float> render (int n, bool playing = true)
    {
        e().playRequest = playing;
        std::vector<float> mono;
        juce::AudioBuffer<float> buf (2, 512);
        std::vector<float*> outs (kNumOutputs * 2, nullptr);
        Engine::HostInfo host;
        host.isPlugin = false;
        for (int done = 0; done < n; done += 512)
        {
            buf.clear();
            outs[0] = buf.getWritePointer (0);
            outs[1] = buf.getWritePointer (1);
            e().process (outs.data(), 1, 512, host);
            for (int i = 0; i < 512 && done + i < n; ++i)
                mono.push_back (0.5f * (buf.getSample (0, i) + buf.getSample (1, i)));
        }
        return mono;
    }

    // As a plugin: the host plays from ppq and jumps back to it every loopPpq (a DAW loop region)
    // tellLoop: the host reports its loop points (most do); otherwise only the jump back is seen
    std::vector<float> renderHostLoop (int n, double loopPpq, double bpm = 120.0, bool tellLoop = true)
    {
        std::vector<float> mono;
        juce::AudioBuffer<float> buf (2, 512);
        std::vector<float*> outs (kNumOutputs * 2, nullptr);
        Engine::HostInfo host;
        host.isPlugin = true;
        host.playing = true;
        host.bpm = bpm;
        host.looping = tellLoop && loopPpq < 1.0e6;
        host.loopStart = 0.0;
        host.loopEnd = loopPpq;
        const double ppqPerSample = bpm / 60.0 / kSr;
        double ppq = 0.0;
        for (int done = 0; done < n; done += 512)
        {
            buf.clear();
            outs[0] = buf.getWritePointer (0);
            outs[1] = buf.getWritePointer (1);
            host.ppq = ppq;
            e().process (outs.data(), 1, 512, host);
            for (int i = 0; i < 512 && done + i < n; ++i)
                mono.push_back (0.5f * (buf.getSample (0, i) + buf.getSample (1, i)));
            ppq += 512 * ppqPerSample;
            if (ppq >= loopPpq - 1e-9) ppq -= loopPpq;
        }
        Engine::HostInfo stop;
        stop.isPlugin = true;
        for (int done = 0; done < (int) kSr; done += 512)
        {
            buf.clear();
            outs[0] = buf.getWritePointer (0);
            outs[1] = buf.getWritePointer (1);
            e().process (outs.data(), 1, 512, stop);
        }
        return mono;
    }

    void stopAndFlush()
    {
        render (512, false);
        render ((int) kSr, false);
    }

    static std::vector<int> onsets (const std::vector<float>& x, float thr = 0.05f, int quiet = 800)
    {
        // An onset is a rise above thr after at least `quiet` samples below thr / 2
        std::vector<int> out;
        bool armed = true;
        int quietCount = 0;
        for (int i = 0; i < (int) x.size(); ++i)
        {
            float a = std::abs (x[(size_t) i]);
            if (a > thr)
            {
                if (armed) { out.push_back (i); armed = false; }
                quietCount = 0;
            }
            else if (a < thr * 0.5f) { if (++quietCount >= quiet) armed = true; }
            else quietCount = 0;
        }
        return out;
    }

    static bool near (int a, int b, int tol = 96) { return std::abs (a - b) <= tol; }

    Track& track (int i) { return s().doc.tracks[(size_t) i]; }

    void soloOnly (int idx)
    {
        for (int i = 0; i < (int) s().doc.tracks.size(); ++i) track (i).solo = i == idx;
    }

    void clearAll()
    {
        for (auto& p : s().doc.parts) p.tracks.clear();
        s().doc.swing = 50;
    }

    void setHits (int ti, std::initializer_list<int> stepsList)
    {
        auto& hits = s().doc.parts[(size_t) s().part].tracks[track (ti).id].hits;
        hits.clear();
        for (int st : stepsList)
        {
            Hit h;
            h.velocity = 120;
            hits[keyOf (st / 16, st % 16)] = h;
        }
    }

    Hit& hit (int ti, int st) { return s().doc.parts[(size_t) s().part].tracks[track (ti).id].hits[keyOf (st / 16, st % 16)]; }

    juce::String list (const std::vector<int>& v)
    {
        juce::StringArray a;
        for (auto x : v) a.add (juce::String (x));
        return a.joinIntoString (", ");
    }

    //==========================================================================
    Runner (juce::File k, juce::File o) : kit (k), out (o)
    {
        steps.push_back ([this] { setup(); return true; });
        steps.push_back ([this] { engineTests(); return true; });
        steps.push_back ([this] { fixTests(); return true; });
        steps.push_back ([this] { requestStretch(); return true; });
        steps.push_back ([this] { return waitStretch(); });
        steps.push_back ([this] { dataTests(); return true; });
        steps.push_back ([this] { buildDemoState(); return true; });
        steps.push_back ([this] { return settle (8); });
        steps.push_back ([this] { snapshot ("ui_sequence.png"); return true; });
        steps.push_back ([this] { stepScopeTest(); return true; });
        steps.push_back ([this] { selectPad(); return true; });
        steps.push_back ([this] { return settle (4); });
        steps.push_back ([this] { snapshot ("ui_whole.png"); return true; });
        steps.push_back ([this] { closedWithMarks(); return true; });
        steps.push_back ([this] { return settle (4); });
        steps.push_back ([this] { snapshot ("ui_closed_marks.png"); return true; });
        steps.push_back ([this] { oneShotFades(); return true; });
        steps.push_back ([this] { return settle (4); });
        steps.push_back ([this] { snapshot ("ui_oneshot_fades.png"); return true; });
        steps.push_back ([this] { editor->setSize (1920, 1050); return true; });   // a maximised window on a 1080p screen
        steps.push_back ([this] { return settle (4); });
        steps.push_back ([this] { snapshot ("ui_fullscreen.png"); editor->setSize (ui::Editor::kWidth, ui::Editor::kHeight); return true; });
        steps.push_back ([this] { closeAsk (0); return true; });
        steps.push_back ([this] { return settle (4); });
        steps.push_back ([this] { snapshot ("ui_close_confirm.png"); answerClose (0); return true; });   // Cancel
        steps.push_back ([this] { return settle (4); });
        steps.push_back ([this] { check (closed == 0 && s().dirty, "Cancel keeps the window open and the changes"); closeAsk (2); return true; });
        steps.push_back ([this] { return settle (4); });
        steps.push_back ([this] { answerClose (2); return true; });   // Don't save
        steps.push_back ([this] { return settle (4); });
        steps.push_back ([this] { check (closed == 1, "Don't save closes"); closeClean(); return true; });
        steps.push_back ([this] { sliceDragTest(); return true; });
        steps.push_back ([this] { return settle (4); });
        steps.push_back ([this] { snapshot ("ui_slice_editor.png"); return true; });
        steps.push_back ([this] { emptyPattern(); return true; });
        steps.push_back ([this] { return settle (4); });
        steps.push_back ([this] { snapshot ("ui_empty.png"); return true; });
        steps.push_back ([this] { pluginEditor(); return true; });
        steps.push_back ([this] { return settle (4); });
        steps.push_back ([this] { snapshotPlugin(); return true; });
        steps.push_back ([this] { pluginMarks(); return true; });
        steps.push_back ([this] { return settle (4); });
        steps.push_back ([this] { pluginOutputVisible(); return true; });
        steps.push_back ([this] { hostVst3Load(); return true; });
        steps.push_back ([this] { return settle (15); });
        steps.push_back ([this] { hostVst3Play(); return true; });
        steps.push_back ([this] { finish(); return true; });
        startTimer (20);
    }

    void timerCallback() override
    {
        if (stepIndex >= steps.size()) return;
        if (steps[stepIndex]()) { ++stepIndex; waitTicks = 0; }
    }

    bool settle (int ticks) { return ++waitTicks >= ticks; }

    void setup()
    {
        juce::PluginHostType::jucePlugInClientCurrentWrapperType = juce::AudioProcessor::wrapperType_Standalone;
        proc.reset (dynamic_cast<OktaktProcessor*> (createPluginFilter()));
        proc->prepareToPlay (kSr, 512);
        e().prepare (kSr, 2048);
        e().bpm = 120.0;

        check (s().addTrack (sample ("Drums/Kicks/kick_round.wav"), -1), "add kick track");
        check (s().addTrack (sample ("Drums/Snares & Claps/snare_tight.wav"), -1), "add snare track");
        check (s().addTrack (sample ("Drums/Hats/hat_closed.wav"), -1), "add hat track");
        check (s().addTrack (sample ("Breaks/break_100.wav"), -1), "add break track");
        bool anyMissing = false;
        for (auto& t : s().doc.tracks) anyMissing = anyMissing || t.missing;
        check (! anyMissing && s().doc.tracks.size() == 4, "4 tracks, no missing files");
    }

    void engineTests()
    {
        // Straight quarter notes: 120 BPM, one bar = 96000 samples, a 16th = 6000
        clearAll();
        setHits (0, { 0, 4, 8, 12 });
        soloOnly (0);
        s().changed();
        stopAndFlush();
        auto o = onsets (render (96000));
        check (o.size() == 4 && near (o[0], 0) && near (o[1], 24000) && near (o[2], 48000) && near (o[3], 72000),
               "kick on the beat: onsets " + list (o));

        // Swing 66%: step 1 moves by 0.32 of a step
        clearAll();
        setHits (0, { 1 });
        s().doc.swing = 66;
        s().changed();
        stopAndFlush();
        o = onsets (render (48000));
        check (o.size() == 1 && near (o[0], 6000 + 1920), "swing 66% delays step 2: onset " + list (o));

        // Micro +12 = half a step later
        clearAll();
        setHits (0, { 4 });
        setHitParam (track (0), hit (0, 4), ParamId::Micro, 12);
        s().changed();
        stopAndFlush();
        o = onsets (render (48000));
        check (o.size() == 1 && near (o[0], 27000), "micro +12: onset " + list (o));

        // POSITION Micro +12 moves every hit half a step later, pinned ones too
        clearAll();
        setHits (0, { 0, 8 });
        hit (0, 8).pinned = true;
        track (0).micro = 12;
        s().changed();
        stopAndFlush();
        o = onsets (render (96000));
        check (o.size() == 2 && near (o[0], 3000) && near (o[1], 51000), "POSITION micro +12 shifts every hit: onsets " + list (o));

        // ... and is only the track's value: a hit's own Micro replaces it
        setHitParam (track (0), hit (0, 8), ParamId::Micro, -6);
        s().changed();
        stopAndFlush();
        o = onsets (render (96000));
        check (o.size() == 2 && near (o[0], 3000) && near (o[1], 48000 - 1500), "hit Micro -6 replaces the track's +12: onsets " + list (o));
        track (0).micro = 0;

        // Retrig 1/16 over 4 steps on the hat
        clearAll();
        setHits (2, { 0 });
        soloOnly (2);
        track (2).step.retrig = 3;
        track (2).step.retrigLen = 4;
        s().changed();
        stopAndFlush();
        o = onsets (render (24000));
        check (o.size() == 4 && near (o[1], 6000) && near (o[3], 18000), "retrig 1/16 x4: onsets " + list (o));
        track (2).step = StepSettings();

        // Condition: plays on pass 1 of 2 only
        clearAll();
        setHits (2, { 0 });
        track (2).step.condN = 2;
        track (2).step.condMask = 0b01;
        s().changed();
        stopAndFlush();
        o = onsets (render (96000 * 4));
        check (o.size() == 2 && near (o[0], 0) && near (o[1], 192000), "condition 1 of 2: onsets " + list (o));

        // As a plugin: a DAW loop turn is the next pass, so 1 of 2 plays every other turn
        o = onsets (renderHostLoop (96000 * 6, 4.0));
        check (o.size() == 3 && near (o[0], 0) && near (o[1], 192000) && near (o[2], 384000),
               "condition 1 of 2 in a 1-bar DAW loop: onsets " + list (o));
        o = onsets (renderHostLoop (96000 * 6, 8.0));
        check (o.size() == 3 && near (o[0], 0) && near (o[1], 192000) && near (o[2], 384000),
               "condition 1 of 2 in a 2-bar DAW loop: onsets " + list (o));
        o = onsets (renderHostLoop (96000 * 6, 4.0, 120.0, false));
        check (o.size() == 3 && near (o[0], 0) && near (o[1], 192000, 600) && near (o[2], 384000, 600),
               "condition 1 of 2 in a 1-bar DAW loop the host does not report: onsets " + list (o));
        o = onsets (renderHostLoop (96000 * 4, 1.0e9));
        check (o.size() == 2 && near (o[0], 0) && near (o[1], 192000), "condition 1 of 2 in a DAW without a loop: onsets " + list (o));
        track (2).step = StepSettings();

        // Chance 0
        clearAll();
        setHits (2, { 0, 8 });
        track (2).step.chance = 0;
        s().changed();
        stopAndFlush();
        o = onsets (render (96000 * 2));
        check (o.empty(), "chance 0% never plays");

        // STEP acts on the marked hits only, when some are marked
        hit (2, 8).marked = true;
        s().changed();
        stopAndFlush();
        o = onsets (render (96000));
        check (o.size() == 1 && near (o[0], 0), "STEP on a marked hit leaves the unmarked one alone: onsets " + list (o));
        hit (2, 8).marked = false;
        track (2).step = StepSettings();

        // Random 100%: something changes between passes but pinned hits stay
        clearAll();
        setHits (2, { 0, 4, 8, 12 });
        hit (2, 0).pinned = true;
        track (2).random = 100;
        s().changed();
        stopAndFlush();
        o = onsets (render (96000 * 4));
        int atZero = 0;
        for (auto x : o) for (int p = 0; p < 4; ++p) if (near (x, p * 96000)) ++atZero;
        check (atZero == 4, "random keeps the pinned hit on every pass (" + juce::String ((int) o.size()) + " onsets in 4 bars)");

        // Plugin in a 1-bar DAW loop: each turn of the loop gets new random, not the same bar again
        {
            setHits (2, { 0, 2, 4, 6, 8, 10, 12, 14 });
            s().changed();
            stopAndFlush();
            const int bar = 96000, turns = 6;
            auto x = renderHostLoop (bar * turns, 4.0);
            juce::StringArray seen;
            for (int t = 0; t < turns; ++t)
            {
                std::vector<float> one (x.begin() + t * bar, x.begin() + (t + 1) * bar);
                juce::String pattern;
                for (auto on : onsets (one)) pattern << juce::String (juce::roundToInt (on / 6000.0)) << " ";
                seen.addIfNotAlreadyThere (pattern);
            }
            check (seen.size() >= 3, "random differs between turns of a DAW loop (" + juce::String (seen.size()) + " of " + juce::String (turns) + " turns distinct)");
        }
        track (2).random = 0;

        // Accuracy 0%: unpinned hits move, pinned do not
        clearAll();
        setHits (2, { 0, 8 });
        hit (2, 0).pinned = true;
        track (2).accuracy = 0;
        s().changed();
        stopAndFlush();
        o = onsets (render (96000 * 2));
        bool pinnedOnTime = ! o.empty() && near (o[0], 0);
        bool movedSome = false;
        for (auto x : o) if (! near (x, 0) && ! near (x, 96000) && ! near (x, 48000, 30) && ! near (x, 144000, 30)) movedSome = true;
        check (pinnedOnTime && movedSome, "accuracy 0%: pinned on time, others jitter (" + list (o) + ")");

        // Accuracy also varies the level: compare the peak of the same hit over 8 passes
        clearAll();
        setHits (2, { 0, 8 });
        hit (2, 0).pinned = true;
        s().changed();
        stopAndFlush();
        auto x = render (96000 * 8);
        auto peakNear = [&x] (int at) { float p = 0; for (int i = juce::jmax (0, at - 13000); i < juce::jmin ((int) x.size(), at + 13000); ++i) p = juce::jmax (p, std::abs (x[(size_t) i])); return p; };
        float pinMin = 1e9f, pinMax = 0, freeMin = 1e9f, freeMax = 0;
        for (int p = 0; p < 8; ++p)
        {
            float a = peakNear (p * 96000 + 3000), b = peakNear (p * 96000 + 48000);
            pinMin = juce::jmin (pinMin, a); pinMax = juce::jmax (pinMax, a);
            freeMin = juce::jmin (freeMin, b); freeMax = juce::jmax (freeMax, b);
        }
        check (pinMax - pinMin < 0.01f * pinMax && freeMax > freeMin * 1.15f,
               "accuracy 0% varies the level of free hits (" + juce::String (freeMin, 3) + "-" + juce::String (freeMax, 3)
               + "), pinned stays " + juce::String (pinMin, 3));
        track (2).accuracy = 100;

        // Random and Accuracy per hit: the track plays straight, one hit gets Accuracy 0 / Random 100
        clearAll();
        setHits (2, { 0, 8 });
        setHitParam (track (2), hit (2, 8), ParamId::Accuracy, 0);
        s().changed();
        stopAndFlush();
        o = onsets (render (96000 * 4));
        {
            int straight = 0, moved = 0;
            for (auto x : o)
            {
                bool onBar = false;
                for (int p = 0; p < 4; ++p) onBar = onBar || near (x, p * 96000);
                if (onBar) ++straight;
                else if (! near (x, 48000, 30) && ! near (x, 144000, 30) && ! near (x, 240000, 30) && ! near (x, 336000, 30)) ++moved;
            }
            check (straight == 4 && moved > 0, "hit Accuracy 0 jitters that hit only (" + list (o) + ")");
        }
        clearAll();
        setHits (2, { 0, 8 });
        setHitParam (track (2), hit (2, 8), ParamId::Random, 100);
        s().changed();
        stopAndFlush();
        o = onsets (render (96000 * 8));
        {
            int first = 0, second = 0;
            o.erase (std::remove_if (o.begin(), o.end(), [] (double x) { return x > 96000 * 8 - 1000; }), o.end());   // the next bar's start
            for (auto x : o)
                for (int p = 0; p < 8; ++p)
                {
                    if (near (x, p * 96000)) ++first;
                    if (near (x, p * 96000 + 48000)) ++second;
                }
            check (first == 8 && second < 8 && (int) o.size() == first + second,
                   "hit Random 100 drops that hit on some passes, no extra hits (" + juce::String (first) + " + " + juce::String (second) + ": " + list (o) + ")");
        }
        clearAll();
        track (2).random = 100;
        setHits (2, { 0, 8 });
        setHitParam (track (2), hit (2, 0), ParamId::Random, 0);
        s().changed();
        stopAndFlush();
        o = onsets (render (96000 * 4));
        {
            int first = 0;
            for (auto x : o) for (int p = 0; p < 4; ++p) if (near (x, p * 96000)) ++first;
            check (first == 4, "hit Random 0 plays on every pass in a track at Random 100");
        }
        track (2).random = 0;

        // Parts: A plays step 0, B plays step 8; B queued at part end
        clearAll();
        soloOnly (2);
        stopAndFlush();
        s().pickPart (0);
        setHits (2, { 0 });
        s().pickPart (1);   // creates B from A and switches (stopped)
        check (s().part == 1 && s().doc.parts[1].exists && s().doc.parts[1].tracks.empty() && s().doc.parts[1].bars == 1,
               "a new part B is empty, 1 bar");
        setHits (2, { 8 });
        s().pickPart (0);
        s().changed();
        stopAndFlush();
        s().togglePlay();
        e().queuedPart = -1;
        e().playRequest = true;
        s().pickPart (1);
        check (e().queuedPart.load() == 1, "part B queued while playing");
        o = onsets (render (96000 * 2));
        check (o.size() == 2 && near (o[0], 0) && near (o[1], 96000 + 48000) && e().currentPart.load() == 1,
               "switch at part end: onsets " + list (o));
        e().playRequest = false;
        render (512, false);
        s().part = 0;
        e().currentPart = 0;

        // Every filter / character / stereo setting stays finite
        clearAll();
        setHits (3, { 0, 4, 8, 12 });
        soloOnly (3);
        bool finite = true, audible = true;
        for (int f = 0; f < (int) FilterType::Count; ++f)
        {
            auto& snd = track (3).sound;
            snd.filter = (FilterType) f;
            snd.cutoff = 70; snd.reso = 90; snd.envAmt = 40;
            snd.drive = 80; snd.bitRed = 60; snd.srRed = 40; snd.punch = 60; snd.tail = -40;
            snd.stereo = (StereoMode) (f % (int) StereoMode::Count);
            snd.amp = (AmpMode) (f % 3);
            s().changed();
            stopAndFlush();
            auto x = render (48000);
            float peak = 0;
            for (auto v : x) { if (! std::isfinite (v)) finite = false; peak = juce::jmax (peak, std::abs (v)); }
            if (peak < 0.001f || peak > 20.0f) { audible = false; std::printf ("      filter %s peak %.4f\n", filterName ((FilterType) f), peak); }
        }
        check (finite, "all filters, drive, bits, SR, transient, stereo: output is finite");
        check (audible, "all filter types produce sound in a sane range");
        track (3).sound = SoundSettings();
        s().rechop (track (3));

        // 16 tracks max
        int before = (int) s().doc.tracks.size();
        for (int i = before; i < 17; ++i) s().addTrack (sample ("Drums/Perc/perc_rim.wav"), -1);
        check (s().doc.tracks.size() == 16, "17th track is refused (16 max)");
        while (s().doc.tracks.size() > 4) s().deleteTrack (s().doc.tracks.back().id);

        // Polyphony stress: 16 tracks × 16 hits with long tails
        stopAndFlush();
    }

    // A 1 s, 220 Hz sine at full level, for pitch and fade checks
    juce::File writeSine()
    {
        auto f = out.getChildFile ("sine_220.wav");
        f.deleteFile();
        juce::AudioBuffer<float> b (1, 48000);
        for (int i = 0; i < b.getNumSamples(); ++i) b.setSample (0, i, 0.8f * (float) std::sin (2.0 * juce::MathConstants<double>::pi * 220.0 * i / 48000.0));
        juce::WavAudioFormat wav;
        if (auto os = std::unique_ptr<juce::OutputStream> (new juce::FileOutputStream (f)); os && static_cast<juce::FileOutputStream*> (os.get())->openedOk())
            if (auto w = std::unique_ptr<juce::AudioFormatWriter> (wav.createWriterFor (os.get(), 48000.0, 1, 24, {}, 0)))
            {
                os.release();
                w->writeFromAudioSampleBuffer (b, 0, b.getNumSamples());
            }
        return f;
    }

    static double freqOf (const std::vector<float>& x, int from, int to)
    {
        int zc = 0;
        for (int i = from + 1; i < to && i < (int) x.size(); ++i)
            if ((x[(size_t) i - 1] < 0.0f) != (x[(size_t) i] < 0.0f)) ++zc;
        return zc * 0.5 * kSr / (to - from);
    }

    static float rms (const std::vector<float>& x, int from, int to)
    {
        double a = 0;
        for (int i = from; i < to && i < (int) x.size(); ++i) a += x[(size_t) i] * x[(size_t) i];
        return (float) std::sqrt (a / juce::jmax (1, to - from));
    }

    void fixTests()
    {
        // Pitch direction: + raises, - lowers
        check (s().addTrack (writeSine(), -1), "add sine track");
        const int si = (int) s().doc.tracks.size() - 1;
        clearAll();
        soloOnly (si);
        track (si).sound.sampleMode = SampleMode::Repitch;
        setHits (si, { 0 });
        double f0, fUp, fDown;
        s().changed(); stopAndFlush(); f0 = freqOf (render (24000), 2000, 20000);
        setHitParam (track (si), hit (si, 0), ParamId::Pitch, 12);
        s().changed(); stopAndFlush(); fUp = freqOf (render (24000), 2000, 20000);
        setHitParam (track (si), hit (si, 0), ParamId::Pitch, -12);
        s().changed(); stopAndFlush(); fDown = freqOf (render (24000), 2000, 20000);
        check (std::abs (f0 - 220) < 5 && std::abs (fUp - 440) < 10 && std::abs (fDown - 110) < 5,
               "hit Pitch +12 / -12 plays an octave up / down: " + juce::String (f0, 1) + " / " + juce::String (fUp, 1) + " / " + juce::String (fDown, 1) + " Hz");
        track (si).sound.tune = -12;
        setHits (si, { 0 });
        s().changed(); stopAndFlush();
        check (std::abs (freqOf (render (24000), 2000, 20000) - 110) < 5, "track Tune -12 plays an octave down");
        track (si).sound.tune = 0;

        // Per-hit Stretch (repitch): 200% plays an octave down
        setHits (si, { 0 });
        setHitParam (track (si), hit (si, 0), ParamId::Stretch, 200);
        s().changed(); stopAndFlush();
        check (std::abs (freqOf (render (24000), 2000, 20000) - 110) < 5, "hit Stretch 200% in Repitch plays an octave down");

        // One-shot IN / OUT (the 2 s note of the stretch check has to end first)
        setHits (si, { 0 });
        track (si).sound.fadeIn = 50;
        track (si).sound.fadeOut = 0;
        s().changed(); stopAndFlush(); render ((int) kSr * 2, false);
        auto x = render (48000);
        float early = rms (x, 1000, 3000), mid = rms (x, 22000, 26000), late = rms (x, 40000, 44000);
        check (early < 0.15f * late && mid > 0.9f * late && mid < 1.05f * late, "In 50%: rises over the first half ("
               + juce::String (early, 3) + " / " + juce::String (mid, 3) + " / " + juce::String (late, 3) + ")");
        track (si).sound.fadeIn = 0;
        track (si).sound.fadeOut = 25;
        s().changed(); stopAndFlush();
        x = render (48000);
        check (rms (x, 30000, 34000) > 0.5f && rms (x, 46000, 47500) < 0.1f, "Out 25%: fades the last quarter");
        track (si).sound.amp = AmpMode::AHD;
        s().changed(); stopAndFlush();
        x = render (48000);
        check (rms (x, 46000, 47500) > 0.2f || rms (x, 30000, 34000) < 0.05f, "In / Out only act in One-shot");
        track (si).sound = SoundSettings();
        s().deleteTrack (track (si).id);

        // Duplicate: same name, same settings, no hits
        clearAll();
        setHits (0, { 0, 4 });
        track (0).sound.cutoff = 77;
        const auto srcName = track (0).name;
        s().duplicateTrack (track (0).id);
        auto& dup = track (1);
        check (dup.name == srcName && dup.sound.cutoff == 77 && s().curPart().tracks[dup.id].hits.empty(),
               "duplicate keeps the name and settings, no hits");
        s().deleteTrack (dup.id);
        track (0).sound.cutoff = 127;

        // STEP is one per track: adding or removing hits does not change it, marks are undoable
        clearAll();
        s().selected = track (2).id;
        track (2).step.retrig = 3;
        s().toggleHit (track (2).id, 0, 0);
        s().toggleHit (track (2).id, 0, 4);
        s().toggleHit (track (2).id, 0, 0);
        check (track (2).step.retrig == 3, "adding and removing hits keeps the track's STEP");
        s().toggleMark (track (2).id, keyOf (0, 4));
        check (s().hasMarks() && s().markedCount (track (2).id) == 1, "shift-click marks a hit");
        s().undo();
        check (! s().hasMarks(), "undo removes the mark");
        track (2).step = StepSettings();
        s().selected = 0;

        // All bars of a track pasted into a 1-bar part: the part grows
        stopAndFlush();   // stopped: picking a part switches to it at once
        clearAll();
        s().pickPart (0);
        s().doc.parts[0].bars = 3;
        setHits (0, { 0, 16, 36 });
        s().copyTrack (track (0).id, -1);
        s().pickPart (3);
        check (s().curPart().bars == 1, "part D starts with 1 bar");
        s().pasteTrack (track (0).id, -1);
        auto& dh = s().curPart().tracks[track (0).id].hits;
        check (s().curPart().bars == 3 && dh.size() == 3 && dh.count (keyOf (2, 4)) && s().curPart().tracks.size() == 1,
               "pasting 3 bars of a track into a 1-bar part makes it 3 bars, only that track gets hits");
        s().clearPart (3);
        s().pickPart (0);
        s().doc.parts[0].bars = 1;
        clearAll();
    }

    void requestStretch()
    {
        auto& t = track (0);
        t.sound.stretch = 200;
        t.sound.sampleMode = SampleMode::KeepPitch;
        s().changed();
    }

    bool waitStretch()
    {
        auto st = s().samples.getStretched (track (0).samplePath, 200);
        if (st == nullptr && ++waitTicks < 300) return false;
        auto src = s().samples.getCached (track (0).samplePath);
        check (st != nullptr && src != nullptr && std::abs (st->buffer.getNumSamples() - 2 * src->length()) < 4,
               "Keep pitch stretch 200% renders a buffer twice as long");
        track (0).sound.stretch = 100;
        return true;
    }

    void dataTests()
    {
        // Slicing
        auto& br = track (3);
        br.sound.play = PlayMode::Slices;
        br.sound.chop = ChopMode::Transients;
        br.sound.sensitivity = 60;
        s().rechop (br);
        int byHits = (int) br.sound.slices.size();
        check (byHits >= 6, "break sliced by hits into " + juce::String (byHits) + " slices");
        br.sound.chop = ChopMode::Equal;
        br.sound.equalParts = 8;
        s().rechop (br);
        check (br.sound.slices.size() == 8, "equal parts: 8 slices");

        // Slice reference survives a marker move and resolves after removal
        int id5 = br.sound.slices[4].id;
        clearAll();
        setHits (3, { 0 });
        hit (3, 0).sliceId = id5;
        s().moveMarker (br.id, 4, br.sound.slices[4].start + 0.01);
        check (resolveSliceIndex (br.sound, id5) == 4, "hit keeps its slice after the marker moves");
        double st5 = br.sound.slices[4].start;
        s().removeMarker (br.id, 4);
        int idx = resolveSliceIndex (br.sound, id5);
        check (idx == 3 && br.sound.slices[3].start <= st5, "removed slice falls back to the slice holding its start");

        // Undo / redo
        int hitsBefore = (int) s().curPart().tracks[track (0).id].hits.size();
        s().toggleHit (track (0).id, 0, 5);
        s().undo();
        int hitsAfter = (int) s().curPart().tracks[track (0).id].hits.size();
        check (hitsBefore == hitsAfter, "undo restores the hits");
        s().redo();
        check ((int) s().curPart().tracks[track (0).id].hits.size() == hitsBefore + 1, "redo applies the hit again");

        // Grid change keeps hits by time
        clearAll();
        setHits (1, { 0, 8 });
        s().setGrid (track (1).id, 4, 3);
        auto& h = s().curPart().tracks[track (1).id].hits;
        check (h.count (keyOf (0, 0)) && h.count (keyOf (0, 6)), "grid 4x4 -> 4x3 keeps hits at the same time");
        s().setGrid (track (1).id, 4, 4);

        // Copy a bar of all tracks from part A into part C
        clearAll();
        s().pickPart (0);
        s().doc.parts[0].bars = 2;
        setHits (0, { 0, 4, 16 });
        s().copyBar (0);
        check (! s().canPasteBar (0) && s().canPasteBar (1), "bar clip: not into itself, yes into another bar");
        s().pickPart (2);
        check (s().part == 2 && s().curPart().tracks.empty(), "part C created empty");
        check (s().canPasteBar (0), "bar clip pastes into the same bar number of another part");
        s().pasteBar (0);
        auto& cHits = s().curPart().tracks[track (0).id].hits;
        check (cHits.size() == 2 && cHits.count (keyOf (0, 0)) && cHits.count (keyOf (0, 4)), "bar 1 of part A copied into part C");

        // Copy one bar of a track into another track with another grid
        s().setGrid (track (1).id, 4, 3);
        s().copyTrack (track (0).id, 0);
        s().pasteTrack (track (1).id, 0);
        auto& snareHits = s().curPart().tracks[track (1).id].hits;
        check (snareHits.size() == 2 && snareHits.count (keyOf (0, 0)) && snareHits.count (keyOf (0, 3)),
               "track bar pasted into a 4x3 track lands on the same beats");
        s().setGrid (track (1).id, 4, 4);

        // Copy all bars of a track from part A into part C
        s().pickPart (0);
        s().copyTrack (track (0).id, -1);
        check (! s().canPasteTrack (track (0).id, -1), "whole-track clip does not paste onto itself");
        s().pickPart (2);
        s().curPart().bars = 2;
        s().pasteTrack (track (0).id, -1);
        check (s().curPart().tracks[track (0).id].hits.count (keyOf (1, 0)) == 1, "all bars of the track pasted into part C");
        s().undo();
        check (s().curPart().tracks[track (0).id].hits.count (keyOf (1, 0)) == 0, "undo reverts the paste");
        s().pickPart (0);

        // JSON round trip
        clearAll();
        setHits (0, { 0, 4, 8, 12 });
        track (0).step.chance = 50;
        hit (0, 4).marked = true;
        hit (0, 4).pinned = true;
        setHitParam (track (0), hit (0, 4), ParamId::Cutoff, 40);
        s().doc.swing = 57;
        s().doc.parts[0].bars = 2;
        auto json = juce::JSON::toString (s().stateToVar());
        auto d2 = Document::fromVar (juce::JSON::parse (json).getProperty ("pattern", {}));
        auto& h2 = d2.parts[0].tracks[d2.tracks[0].id].hits[keyOf (0, 4)];
        check (d2.tracks.size() == s().doc.tracks.size() && d2.swing == 57 && d2.parts[0].bars == 2
               && d2.tracks[0].step.chance == 50 && h2.marked && h2.pinned && h2.p[(size_t) ParamId::Cutoff] == 40
               && d2.tracks[3].sound.slices.size() == track (3).sound.slices.size(),
               "pattern JSON round trip keeps tracks, hits, STEP, pins, params and slices");

        track (0).step = StepSettings();
        hit (0, 4).marked = false;

        // Version 1 files: the track's Micro was added to the hit's own and skipped pinned hits
        {
            auto old = d2.toVar();
            old.getDynamicObject()->setProperty ("version", 1);
            auto* t0 = old["tracks"][0].getDynamicObject();
            t0->setProperty ("micro", 10);
            auto hits = old["parts"][0]["tracks"][juce::Identifier (juce::String ((int) d2.tracks[0].id))];
            for (auto& ho : *hits.getArray())
            {
                const int st = ho["step"];
                if (st == 0) ho["params"].getDynamicObject()->setProperty ("Micro", 5);
                if (st == 8) ho.getDynamicObject()->setProperty ("pinned", true);
            }
            auto d4 = Document::fromVar (old);
            const auto& t4 = d4.tracks[0];
            auto& h4 = d4.parts[0].tracks[t4.id].hits;
            const int m0 = effectiveValue (t4, h4[keyOf (0, 0)], ParamId::Micro);
            const int m4 = effectiveValue (t4, h4[keyOf (0, 4)], ParamId::Micro);
            const int m8 = effectiveValue (t4, h4[keyOf (0, 8)], ParamId::Micro);
            const int m12 = effectiveValue (t4, h4[keyOf (0, 12)], ParamId::Micro);
            check (d4.version == 2 && t4.micro == 10 && m0 == 15 && m4 == 0 && m8 == 0 && m12 == 10
                   && ! h4[keyOf (0, 12)].has (ParamId::Micro),
                   "version 1 Micro converted: " + juce::String (m0) + " " + juce::String (m4) + " " + juce::String (m8) + " " + juce::String (m12));
        }

        // Missing file is flagged on load, data kept
        auto v = juce::JSON::parse (json);
        auto doc3 = Document::fromVar (v.getProperty ("pattern", {}));
        doc3.tracks[1].samplePath = "/nonexistent/snare.wav";
        auto* o = new juce::DynamicObject();
        o->setProperty ("pattern", doc3.toVar());
        o->setProperty ("session", v.getProperty ("session", {}));
        s().stateFromVar (juce::var (o));
        check (track (1).missing && track (1).name == "snare_tight", "missing file: flagged, track kept");

        // A pattern saved on Windows opened here: the sample is not found, its name still shows
        doc3.tracks[1].samplePath = "C:\\Users\\ann\\Samples\\snare_tight.wav";
        auto* o2 = new juce::DynamicObject();
        o2->setProperty ("pattern", doc3.toVar());
        o2->setProperty ("session", v.getProperty ("session", {}));
        s().stateFromVar (juce::var (o2));
        check (track (1).missing && sampleFileName (track (1).samplePath) == "snare_tight.wav",
               "Windows sample path: flagged missing, file name shown");
        check (sampleFileName ("/home/ann/kick.wav") == "kick.wav", "file name of a POSIX path");

        // Metronome time signature is kept with the session state
        s().metronomeBeats = 7;
        auto st7 = s().stateToVar();
        s().metronomeBeats = 4;
        s().stateFromVar (st7);
        check (s().metronomeBeats == 7 && s().engine.metronomeBeats.load() == 7, "metronome 7/4 restored");
        s().metronomeBeats = 4;
        s().engine.metronomeBeats = 4;
    }

    //==========================================================================
    void buildDemoState()
    {
        // A pattern close to the mockup, for the screenshot
        auto st = juce::JSON::parse ("{}");
        s().newPattern();
        s().doc.name = "Intro groove";
        s().browserRoot = kit;
        auto add = [this] (const char* f) { s().addTrack (sample (f), -1); };
        add ("Drums/Kicks/kick_round.wav");
        add ("Drums/Snares & Claps/snare_tight.wav");
        add ("Drums/Hats/hat_closed.wav");
        add ("Drums/Perc/perc_rim.wav");
        add ("Breaks/break_100.wav");
        add ("Loops/Bass/bass_loop_120.wav");
        add ("Loops/Pads/pad_wide.wav");
        add ("Drums/Snares & Claps/clap_stack.wav");
        track (0).name = "Kick"; track (1).name = "Snare"; track (2).name = "Hats"; track (3).name = "Perc";
        track (4).name = "Break"; track (5).name = "Bass loop"; track (6).name = "Pad"; track (7).name = "Vox chop";
        track (7).missing = true;
        track (6).mute = true;
        track (2).volume = 84; track (2).pan = 20; track (3).pan = -24;
        track (3).groups = 4; track (3).steps = 3;
        s().doc.parts[0].bars = 2;
        s().doc.swing = 54;
        s().doc.parts[1].exists = true;
        s().doc.parts[1].bars = 4;

        auto put = [this] (int ti, std::initializer_list<int> keys)
        {
            auto& hits = s().doc.parts[0].tracks[track (ti).id].hits;
            for (int k : keys)
            {
                Hit h;
                h.velocity = defaultVelocity (track (ti), k % track (ti).stepsPerBar());
                hits[keyOf (k / track (ti).stepsPerBar(), k % track (ti).stepsPerBar())] = h;
            }
        };
        put (0, { 0, 4, 8, 12, 16, 20, 24, 28 });
        put (1, { 4, 12, 20, 28, 31 });
        put (2, { 0, 2, 4, 6, 8, 10, 12, 14, 16, 18, 20, 22, 24, 26, 28, 30 });
        put (3, { 0, 5, 9, 11, 12, 17, 21, 23 });
        put (4, { 0, 3, 6, 8, 10, 14, 16, 22, 24, 27 });
        put (5, { 0, 8, 16, 24 });
        put (6, { 0, 16 });
        put (7, { 6, 14, 22, 30 });
        auto& br = track (4);
        br.sound.play = PlayMode::Slices;
        br.sound.sensitivity = 60;
        s().rechop (br);
        br.rows = { ParamId::Pitch, ParamId::Cutoff, ParamId::Random };
        br.random = 20;
        auto& bh = s().doc.parts[0].tracks[br.id].hits;
        setHitParam (br, bh[keyOf (0, 6)], ParamId::Pitch, 3);
        setHitParam (br, bh[keyOf (1, 2)], ParamId::Pitch, -2);
        setHitParam (br, bh[keyOf (0, 3)], ParamId::Cutoff, 110);
        setHitParam (br, bh[keyOf (0, 10)], ParamId::Cutoff, 45);
        setHitParam (br, bh[keyOf (0, 8)], ParamId::Random, 60);   // pinned: drawn greyed out
        setHitParam (br, bh[keyOf (1, 0)], ParamId::Random, 60);
        bh[keyOf (0, 0)].pinned = true;
        bh[keyOf (0, 8)].pinned = true;
        bh[keyOf (0, 14)].velocity = 70;
        br.sound.amp = AmpMode::AHD;
        br.sound.filter = FilterType::LP24;
        br.sound.cutoff = 82; br.sound.reso = 34; br.sound.drive = 22;
        s().selected = br.id;
        bh[keyOf (0, 6)].marked = true;
        bh[keyOf (0, 10)].marked = true;
        s().toast ("Track added: break_100");
        s().dirty = true;
        s().changed();

        editor.reset (proc->createEditor());
        editor->setSize (ui::Editor::kWidth, ui::Editor::kHeight);
        (void) st;
    }

    int closed = 0;

    template <typename T> static T* findChild (juce::Component& c)
    {
        if (auto* x = dynamic_cast<T*> (&c)) return x;
        for (auto* ch : c.getChildren())
            if (auto* x = findChild<T> (*ch)) return x;
        return nullptr;
    }

    // Slices dragged from the slicer into the grid
    void sliceDragTest()
    {
        auto& br = track (4);
        br.sound.amp = AmpMode::AHD;
        s().selected = br.id;
        s().changed();
        if (auto* ed = dynamic_cast<ui::Editor*> (editor.get())) ed->refresh();
        auto* rows = findChild<ui::GridRows> (*editor);
        check (rows != nullptr, "grid found");
        if (rows == nullptr) return;
        auto bars = rows->barRanges();
        const int y = 4 * (ui::GridRows::kRowH + ui::GridRows::kGap) + ui::GridRows::kRowH / 2;   // the Break row
        const int x = (int) (bars[0].getStart() + bars[0].getLength() * 1.5f / 16.0f);         // its step 2

        juce::DragAndDropTarget::SourceDetails onCell ("slice:" + juce::String ((int) br.id) + ":4", rows, { x, y });
        rows->itemDropped (onCell);
        auto* h = s().hitAt (br.id, keyOf (0, 1));
        check (h != nullptr && h->sliceId == br.sound.slices[4].id, "slice 5 dropped on a cell of its track: a hit plays it");

        const int before = (int) s().doc.tracks.size();
        const double a2 = br.sound.slices[2].start, a3 = br.sound.slices[3].start;   // br moves when a track is added
        juce::DragAndDropTarget::SourceDetails onEmpty ("slice:" + juce::String ((int) br.id) + ":2", rows, { x, rows->getHeight() - 10 });
        rows->itemDropped (onEmpty);
        auto& nt = s().doc.tracks.back();
        check ((int) s().doc.tracks.size() == before + 1 && nt.sound.play == PlayMode::Whole
               && std::abs (nt.sound.regionStart - a2) < 1e-9
               && std::abs (nt.sound.regionEnd - a3) < 1e-9 && s().curPart().tracks[nt.id].hits.empty(),
               "slice 3 dropped below the tracks: a new track plays only that slice");
        s().undo();
        s().undo();

        // Editor open: no Slices -> tracks / Export slices buttons
        s().selected = track (4).id;
        s().changed();
        if (auto* ed = dynamic_cast<ui::Editor*> (editor.get())) ed->refresh();
        std::function<ui::Btn* (juce::Component&)> findEdit = [&] (juce::Component& c) -> ui::Btn*
        {
            if (auto* b = dynamic_cast<ui::Btn*> (&c); b != nullptr && b->text == "Edit slices" && b->isVisible()) return b;
            for (auto* ch : c.getChildren()) if (auto* b = findEdit (*ch)) return b;
            return nullptr;
        };
        if (auto* eb = findEdit (*editor)) eb->onClick();
        std::function<bool (juce::Component&)> hasOld = [&] (juce::Component& c) -> bool
        {
            if (auto* b = dynamic_cast<ui::Btn*> (&c); b != nullptr && (b->text.startsWith ("Slices") || b->text == "Export slices")) return true;
            for (auto* ch : c.getChildren()) if (hasOld (*ch)) return true;
            return false;
        };
        check (! hasOld (*editor), "the slice editor has no Slices -> tracks / Export slices buttons");
    }

    void closeAsk (int)
    {
        s().dirty = true;
        if (auto* ed = dynamic_cast<ui::Editor*> (editor.get()))
            ed->confirmClose ([this] { ++closed; });
        check (closed == 0, "closing with unsaved changes asks first");
    }

    static juce::AlertWindow* findAlert (juce::Component& c)
    {
        if (auto* a = dynamic_cast<juce::AlertWindow*> (&c)) return a;
        for (auto* ch : c.getChildren())
            if (auto* a = findAlert (*ch)) return a;
        return nullptr;
    }

    void answerClose (int button)
    {
        auto* a = findAlert (*editor);
        check (a != nullptr && a->isVisible(), "the Save changes? dialog is shown");
        if (a != nullptr) a->exitModalState (button);
    }

    void closeClean()
    {
        s().dirty = false;
        if (auto* ed = dynamic_cast<ui::Editor*> (editor.get()))
            ed->confirmClose ([this] { ++closed; });
        check (closed == 2, "closing without changes does not ask");
    }

    ui::Knob* findKnob (juce::Component& c, const juce::String& label)
    {
        if (auto* k = dynamic_cast<ui::Knob*> (&c); k != nullptr && k->label == label) return k;
        for (auto* ch : c.getChildren())
            if (auto* k = findKnob (*ch, label)) return k;
        return nullptr;
    }

    // STEP through the panel edits the track's one STEP, whatever is marked
    void stepScopeTest()
    {
        auto& br = track (4);
        auto* chance = findKnob (*editor, "Chance");
        check (chance != nullptr, "STEP Chance knob found");
        if (chance == nullptr) return;
        chance->set (40);
        check (br.step.chance == 40 && chance->get() == 40, "the STEP panel sets the track's STEP");
        br.step = StepSettings();
        s().changed();
    }

    void selectPad()
    {
        for (auto& [k, h] : s().doc.parts[0].tracks[track (4).id].hits) h.marked = false;
        s().selected = track (5).id;
        track (5).sound.amp = AmpMode::ADSR;
        track (5).sound.stretch = 150;
        track (5).sound.stereo = StereoMode::Haas;
        s().changed();
    }

    void closedWithMarks()
    {
        auto& bh = s().doc.parts[0].tracks[track (4).id].hits;
        bh[keyOf (0, 6)].marked = true;
        bh[keyOf (0, 10)].marked = true;
        s().selected = 0;
        s().changed();
    }

    void oneShotFades()
    {
        for (auto& [k, h] : s().doc.parts[0].tracks[track (4).id].hits) h.marked = false;
        auto& br = track (4);
        br.sound.amp = AmpMode::OneShot;
        br.sound.fadeIn = 20;
        br.sound.fadeOut = 45;
        br.sound.length = 110;
        s().selected = br.id;
        s().changed();
    }

    void emptyPattern()
    {
        s().newPattern();
        s().changed();
    }

    void snapshot (const juce::String& name)
    {
        if (auto* ed = dynamic_cast<ui::Editor*> (editor.get())) ed->refresh();
        auto img = editor->createComponentSnapshot (editor->getLocalBounds(), true, 1.0f);
        auto f = out.getChildFile (name);
        f.deleteFile();
        juce::FileOutputStream os (f);
        juce::PNGImageFormat png;
        check (os.openedOk() && png.writeImageToStream (img, os), "snapshot " + name);
    }

    void pluginEditor()
    {
        editor = nullptr;
        juce::PluginHostType::jucePlugInClientCurrentWrapperType = juce::AudioProcessor::wrapperType_VST3;
        plugin.reset (dynamic_cast<OktaktProcessor*> (createPluginFilter()));
        check (plugin->getBusCount (false) == kNumOutputs, "plugin has 16 stereo outputs");
        auto& ps = *plugin->session;
        ps.addTrack (sample ("Drums/Kicks/kick_round.wav"), -1);
        ps.addTrack (sample ("Breaks/break_100.wav"), -1);
        ps.doc.tracks[1].output = 3;
        ps.selected = ps.doc.tracks[1].id;
        Hit h;
        ps.doc.parts[0].tracks[ps.doc.tracks[0].id].hits[keyOf (0, 0)] = h;
        ps.doc.parts[0].tracks[ps.doc.tracks[1].id].hits[keyOf (0, 4)] = h;
        ps.changed();
        editor.reset (plugin->createEditor());
        editor->setSize (ui::Editor::kWidth, ui::Editor::kHeight);
    }

    void snapshotPlugin() { snapshot ("ui_plugin.png"); }

    // With marks the STEP block is at its tallest: the track's DAW output must still be fully on screen
    void pluginMarks()
    {
        auto& ps = *plugin->session;
        ps.toggleMark (ps.doc.tracks[1].id, keyOf (0, 4));
        ps.changed();
    }

    void pluginOutputVisible()
    {
        snapshot ("ui_plugin_marks.png");
        juce::ComboBox* box = nullptr;
        std::function<void (juce::Component&)> find = [&] (juce::Component& c)
        {
            if (auto* cb = dynamic_cast<juce::ComboBox*> (&c); cb != nullptr && cb->getNumItems() == kNumOutputs) box = cb;
            for (auto* ch : c.getChildren()) find (*ch);
        };
        find (*editor);
        check (box != nullptr && box->isVisible() && box->getParentComponent()->isVisible(), "plugin: track OUTPUT menu shown");
        if (box == nullptr) return;
        auto* side = box->getParentComponent();
        check (side->getLocalBounds().contains (box->getBounds()), "plugin: OUTPUT fits in the side panel with marks");
        check (box->getText() == "Out 4", "plugin: OUTPUT shows the track's output");
    }

    // Loads the built VST3 the way a DAW does and plays a pattern from the host transport
   #if JUCE_PLUGINHOST_VST3
    std::unique_ptr<juce::AudioPluginInstance> inst;
   #endif

    void hostVst3Load()
    {
       #if JUCE_PLUGINHOST_VST3
        if (vst3 == juce::File()) return;
        editor = nullptr;
        juce::VST3PluginFormat fmt;
        juce::OwnedArray<juce::PluginDescription> descs;
        fmt.findAllTypesForFile (descs, vst3.getFullPathName());
        check (descs.size() == 1, "VST3 is found by a host scan");
        if (descs.isEmpty()) return;
        juce::String err;
        inst = fmt.createInstanceFromDescription (*descs[0], kSr, 512, err);
        check (inst != nullptr, "VST3 instantiates " + err);
        if (inst == nullptr) return;
        check (inst->getBusCount (false) == kNumOutputs, "VST3 exposes 16 stereo outputs");

        Document d;
        Track t;
        t.id = 1;
        t.name = "Kick";
        t.samplePath = sample ("Drums/Kicks/kick_round.wav").getFullPathName();
        d.tracks.push_back (t);
        d.nextTrackId = 2;
        for (int st : { 0, 4, 8, 12 }) d.parts[0].tracks[1].hits[keyOf (0, st)] = Hit();
        auto* o = new juce::DynamicObject();
        o->setProperty ("pattern", d.toVar());
        o->setProperty ("session", juce::var (new juce::DynamicObject()));
        auto json = juce::JSON::toString (juce::var (o));
        // The JUCE VST3 host wraps the plugin's own state in XML, as a DAW project would hold it
        juce::XmlElement wrapper ("VST3PluginState");
        wrapper.createNewChildElement ("IComponent")->addTextElement (juce::MemoryBlock (json.toRawUTF8(), json.getNumBytesAsUTF8()).toBase64Encoding());
        juce::MemoryBlock packed;
        juce::AudioProcessor::copyXmlToBinary (wrapper, packed);
        inst->setStateInformation (packed.getData(), (int) packed.getSize());
       #endif
    }

    void hostVst3Play()
    {
       #if JUCE_PLUGINHOST_VST3
        if (inst == nullptr) return;
        HostPlayHead ph;
        inst->setPlayHead (&ph);
        inst->prepareToPlay (kSr, 512);
        juce::AudioBuffer<float> buf (juce::jmax (2, inst->getTotalNumOutputChannels()), 512);
        juce::MidiBuffer midi;
        std::vector<float> mono;
        for (int done = 0; done < 96000; done += 512)
        {
            buf.clear();
            inst->processBlock (buf, midi);
            for (int i = 0; i < 512; ++i) mono.push_back (0.5f * (buf.getSample (0, i) + buf.getSample (1, i)));
            ph.ppq += 512 * ph.bpm / 60.0 / kSr;
        }
        auto o2 = onsets (mono, 0.3f, 400);
        check (o2.size() >= 4 && near (o2[0], 0) && near (o2[1], 24000) && near (o2[3], 72000),
               "VST3 plays the restored pattern on the host beat: onsets " + list (o2));

        juce::MemoryBlock state, component;
        inst->getStateInformation (state);
        if (auto xml = juce::AudioProcessor::getXmlFromBinary (state.getData(), (int) state.getSize()))
            if (auto* c = xml->getChildByName ("IComponent"))
                component.fromBase64Encoding (c->getAllSubText());
        check (component.toString().contains ("kick_round.wav"), "VST3 state saves the pattern for the DAW project");
        inst->releaseResources();
        inst->setPlayHead (nullptr);
        inst = nullptr;
       #endif
    }

    void finish()
    {
        stopTimer();
        editor = nullptr;
        plugin = nullptr;
        proc = nullptr;
        std::printf ("\n%d passed, %d failed\n", passes, failures);
        juce::MessageManager::getInstance()->stopDispatchLoop();
    }
};

} // namespace

int main (int argc, char** argv)
{
    if (argc < 3)
    {
        std::printf ("usage: OKTAKT_Check <demo-kit folder> <output folder> [OKTAKT.vst3]\n");
        return 2;
    }
    juce::ScopedJuceInitialiser_GUI init;
    if (juce::String (argv[1]) == "--outputs")
    {
        // Desktop sound outputs offered in Settings; with an id, opens the ALSA "default" device routed
        // there for 3 s of silence (watch it with `pactl list sink-inputs`)
        for (auto& o : ok::systemaudio::listOutputs())
            std::printf ("%s\t%s\n", o.id.toRawUTF8(), o.description.toRawUTF8());
        std::fflush (stdout);
        if (juce::String (argv[2]) == "-") return 0;
        ok::systemaudio::routeTo (argv[2]);
        juce::AudioDeviceManager dm;
        dm.getAvailableDeviceTypes();
        dm.setCurrentAudioDeviceType ("ALSA", true);
        auto* type = dm.getCurrentDeviceTypeObject();
        if (type == nullptr) { std::printf ("no ALSA\n"); return 1; }
        type->scanForDevices();
        auto setup = dm.getAudioDeviceSetup();
        setup.outputDeviceName = type->getDeviceNames (false)[type->getDefaultDeviceIndex (false)];
        setup.inputDeviceName = {};
        auto err = dm.initialise (0, 2, nullptr, false, {}, &setup);
        std::printf ("opened '%s' %s\n", setup.outputDeviceName.toRawUTF8(), err.isEmpty() ? "ok" : err.toRawUTF8());
        std::fflush (stdout);
        juce::Thread::sleep (3000);
        return err.isEmpty() ? 0 : 1;
    }
    juce::File kit (juce::File::getCurrentWorkingDirectory().getChildFile (argv[1]));
    juce::File out (juce::File::getCurrentWorkingDirectory().getChildFile (argv[2]));
    out.createDirectory();
    int failures = 0;
    {
        Runner r (kit, out);
        if (argc > 3) r.vst3 = juce::File::getCurrentWorkingDirectory().getChildFile (argv[3]);
        juce::MessageManager::getInstance()->runDispatchLoop();
        failures = r.failures;
    }
    return failures == 0 ? 0 : 1;
}
