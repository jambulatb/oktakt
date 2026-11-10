#include "Session.h"

namespace ok
{

static constexpr size_t kUndoDepth = 100;

Session::Session (Engine& e, bool plugin) : engine (e), isPlugin (plugin)
{
    auto music = juce::File::getSpecialLocation (juce::File::userMusicDirectory);
    browserRoot = music.isDirectory() ? music : juce::File::getSpecialLocation (juce::File::userHomeDirectory);
    // First run: start in the demo kit. An installer puts it in the shared app data folder
    // (macOS: /Library/Application Support/OKTAKT/Demo Kit); a source build uses the one in resources/
    auto shared = juce::File::getSpecialLocation (juce::File::commonApplicationDataDirectory);
   #if JUCE_MAC
    shared = shared.getChildFile ("Application Support");   // JUCE gives /Library here
   #endif
    juce::File demo = shared.getChildFile ("OKTAKT/Demo Kit");
   #ifdef OKTAKT_DEMO_KIT_DIR
    if (! demo.isDirectory()) demo = juce::File (OKTAKT_DEMO_KIT_DIR);
   #endif
    if (demo.isDirectory()) browserRoot = demo;
    samples.onStretchReady = [this] { publish(); };
    publish();
    startTimerHz (30);
}

Session::~Session()
{
    stopTimer();
}

Hit* Session::hitAt (uint32_t id, int key)
{
    auto& p = curPart();
    auto it = p.tracks.find (id);
    if (it == p.tracks.end()) return nullptr;
    auto h = it->second.hits.find (key);
    return h != it->second.hits.end() ? &h->second : nullptr;
}

//==============================================================================
void Session::checkpoint (const void* gestureKey)
{
    auto now = juce::Time::getMillisecondCounter();
    if (gestureKey != nullptr && gestureKey == lastGesture && now - lastGestureTime < 1200)
    {
        lastGestureTime = now;
        return;
    }
    lastGesture = gestureKey;
    lastGestureTime = now;

    undoStack.push_back (doc);
    if (undoStack.size() > kUndoDepth) undoStack.pop_front();
    redoStack.clear();
}

void Session::changed()
{
    ++revision;
    dirty = true;
    publish();
    sendChangeMessage();
}

void Session::uiChanged()
{
    ++revision;
    sendChangeMessage();
}

void Session::undo()
{
    if (undoStack.empty()) return;
    redoStack.push_back (doc);
    doc = undoStack.back();
    undoStack.pop_back();
    lastGesture = nullptr;
    if (! trackActive()) selected = 0;
    if (! doc.parts[(size_t) part].exists)
        for (int i = 0; i < kNumParts; ++i)
            if (doc.parts[(size_t) i].exists) { part = i; engine.currentPart = i; break; }
    checkMissing();
    toast ("Undone");
    changed();
}

void Session::redo()
{
    if (redoStack.empty()) return;
    undoStack.push_back (doc);
    doc = redoStack.back();
    redoStack.pop_back();
    lastGesture = nullptr;
    if (! trackActive()) selected = 0;
    if (! doc.parts[(size_t) part].exists)
        for (int i = 0; i < kNumParts; ++i)
            if (doc.parts[(size_t) i].exists) { part = i; engine.currentPart = i; break; }
    checkMissing();
    toast ("Redone");
    changed();
}

void Session::toast (const juce::String& text, bool withUndo)
{
    toastText = text;
    toastHasUndo = withUndo;
    toastTime = juce::Time::getMillisecondCounter();
    sendChangeMessage();
}

void Session::publish()
{
    EngineSnapshot::Ptr s = new EngineSnapshot();
    s->doc = doc;
    s->rt.resize (doc.tracks.size());
    std::map<juce::String, std::set<int>> wanted;   // pitch-kept stretches the pattern uses
    for (size_t i = 0; i < doc.tracks.size(); ++i)
    {
        const auto& t = doc.tracks[i];
        auto sample = samples.get (t.samplePath);
        s->rt[i].sample = sample;
        if (sample == nullptr || t.sound.sampleMode != SampleMode::KeepPitch) continue;

        std::set<int> values;
        if (t.sound.stretch != 100) values.insert (t.sound.stretch);
        for (auto& p : doc.parts)
            if (auto it = p.tracks.find (t.id); it != p.tracks.end())
                for (auto& [key, h] : it->second.hits)
                    if (h.has (ParamId::Stretch) && h.p[(size_t) ParamId::Stretch] != 100)
                        values.insert (h.p[(size_t) ParamId::Stretch]);
        if (values.empty()) continue;
        wanted[t.samplePath].insert (values.begin(), values.end());
        for (int v : values)
            if (auto st = samples.getStretched (t.samplePath, v))
            {
                if (v == t.sound.stretch) s->rt[i].stretched = st;
                s->rt[i].perHit.push_back ({ v, st });
            }
    }
    samples.requestStretches (wanted);
    releasePool.push_back (s);
    engine.setSnapshot (s);
}

void Session::checkMissing()
{
    for (auto& t : doc.tracks)
        t.missing = samples.get (t.samplePath) == nullptr;
}

void Session::timerCallback()
{
    const bool playing = isPlaying();
    bool notify = false;

    if (playing)
    {
        int ep = engine.currentPart.load();
        if (ep != part && ep >= 0 && ep < kNumParts)
        {
            part = ep;
            notify = true;
        }
    }
    else if (lastPlaying)
    {
        // On stop a queued part is switched to right away
        int q = engine.queuedPart.exchange (-1);
        if (q >= 0) { part = q; engine.currentPart = q; }
        notify = true;
    }
    lastPlaying = playing;

    int q = engine.queuedPart.load();
    if (q != lastQueued) { lastQueued = q; notify = true; }

    auto now = juce::Time::getMillisecondCounter();
    if (toastText.isNotEmpty() && now - toastTime > (toastHasUndo ? 6000u : 3500u))
    {
        toastText.clear();
        toastHasUndo = false;
        notify = true;
    }

    if (previewPath.isNotEmpty() && ! engine.previewing.load())
    {
        previewPath.clear();
        notify = true;
    }

    if (now - lastPurge > 2000)
    {
        lastPurge = now;
        if (releasePool.size() > 1)
            releasePool.erase (std::remove_if (releasePool.begin(), releasePool.end() - 1,
                                               [] (const EngineSnapshot::Ptr& p) { return p->getReferenceCount() == 1; }),
                               releasePool.end() - 1);
        juce::StringArray keep;
        for (auto& t : doc.tracks) keep.add (t.samplePath);
        if (previewPath.isNotEmpty()) keep.add (previewPath);
        samples.purge (keep);
    }

    if (notify) sendChangeMessage();
}

int Session::colourForFile (const juce::File& f) const
{
    // Colour by folder: the same folder always gets the same colour
    auto folder = f.getParentDirectory().getFileName().toLowerCase();
    if (folder.contains ("kick") || folder.contains ("bd")) return 0;
    if (folder.contains ("snare") || folder.contains ("clap")) return 1;
    if (folder.contains ("hat") || folder.contains ("cymbal")) return 9;
    if (folder.contains ("perc")) return 10;
    if (folder.contains ("break") || folder.contains ("loop")) return 6;
    if (folder.contains ("bass")) return 5;
    if (folder.contains ("vox") || folder.contains ("vocal")) return 2;
    if (folder.contains ("pad") || folder.contains ("texture")) return 4;
    return (int) ((juce::uint32) folder.hashCode() % (juce::uint32) (kPaletteSize - 1));
}

//==============================================================================
bool Session::addTrack (const juce::File& file, int insertIndex)
{
    if ((int) doc.tracks.size() >= kMaxTracks)
    {
        toast ("16 tracks max");
        return false;
    }
    checkpoint();

    Track t;
    t.id = doc.nextTrackId++;
    t.name = file.getFileNameWithoutExtension();
    t.samplePath = file.getFullPathName();
    t.colour = colourForFile (file);
    t.missing = samples.get (t.samplePath) == nullptr;
    rechop (t);

    insertIndex = juce::jlimit (0, (int) doc.tracks.size(), insertIndex < 0 ? (int) doc.tracks.size() : insertIndex);
    doc.tracks.insert (doc.tracks.begin() + insertIndex, t);
    selected = t.id;
    toast ("Track added: " + t.name);
    changed();
    return true;
}

void Session::replaceSample (uint32_t id, const juce::File& file)
{
    auto* t = doc.findTrack (id);
    if (t == nullptr) return;
    checkpoint();
    auto oldName = t->name;
    t->samplePath = file.getFullPathName();
    if (! t->nameManual) t->name = file.getFileNameWithoutExtension();
    t->missing = samples.get (t->samplePath) == nullptr;
    if (t->sound.chop != ChopMode::Manual || t->sound.slices.empty()) rechop (*t);
    // Region from "Slices → tracks" belongs to the old file
    t->sound.regionStart = 0.0;
    t->sound.regionEnd = 1.0;
    selected = id;
    toast (oldName + juce::String::fromUTF8 (" → ") + file.getFileName() + (t->sound.play == PlayMode::Slices ? juce::String::fromUTF8 (" · re-sliced") : ""));
    changed();
}

void Session::moveTrack (uint32_t id, int newIndex)
{
    int from = doc.indexOf (id);
    if (from < 0) return;
    newIndex = juce::jlimit (0, (int) doc.tracks.size(), newIndex);
    if (newIndex == from || newIndex == from + 1) return;
    checkpoint();
    auto t = doc.tracks[(size_t) from];
    doc.tracks.erase (doc.tracks.begin() + from);
    if (newIndex > from) --newIndex;
    doc.tracks.insert (doc.tracks.begin() + newIndex, t);
    toast (t.name + " moved", true);
    changed();
}

void Session::renameTrack (uint32_t id, const juce::String& name)
{
    auto* t = doc.findTrack (id);
    auto n = name.trim();
    if (t == nullptr || n.isEmpty() || n == t->name) return;
    checkpoint();
    t->name = n;
    t->nameManual = true;
    toast ("Renamed to " + n + juce::String::fromUTF8 (" · sample unchanged"));
    changed();
}

void Session::setColour (uint32_t id, int colour)
{
    auto* t = doc.findTrack (id);
    if (t == nullptr || t->colour == colour) return;
    checkpoint();
    t->colour = colour;
    changed();
}

void Session::duplicateTrack (uint32_t id)
{
    int idx = doc.indexOf (id);
    if (idx < 0) return;
    if ((int) doc.tracks.size() >= kMaxTracks) { toast ("16 tracks max"); return; }
    checkpoint();
    // The clone gets the sound and settings of the track, not its hits
    Track copy = doc.tracks[(size_t) idx];
    copy.id = doc.nextTrackId++;
    copy.solo = false;
    doc.tracks.insert (doc.tracks.begin() + idx + 1, copy);
    selected = copy.id;
    toast ("Duplicated " + doc.tracks[(size_t) idx].name, true);
    changed();
}

void Session::clearHits (uint32_t id)
{
    auto* t = doc.findTrack (id);
    if (t == nullptr) return;
    checkpoint();
    curPart().tracks.erase (id);
    toast ("Hits cleared: " + t->name + juce::String::fromUTF8 (" · part ") + juce::String::charToString ((juce::juce_wchar) ('A' + part)), true);
    changed();
}

void Session::deleteTrack (uint32_t id)
{
    int idx = doc.indexOf (id);
    if (idx < 0) return;
    checkpoint();
    auto name = doc.tracks[(size_t) idx].name;
    doc.tracks.erase (doc.tracks.begin() + idx);
    for (auto& p : doc.parts) p.tracks.erase (id);
    if (selected == id) selected = 0;
    toast ("Track deleted: " + name, true);
    changed();
}

void Session::selectTrack (uint32_t id)
{
    selected = (selected == id) ? 0 : id;
    uiChanged();
}

void Session::closeTrack()
{
    selected = 0;
    uiChanged();
}

void Session::remapGrid (Track& t, int oldPer, int newPer)
{
    for (auto& p : doc.parts)
    {
        auto it = p.tracks.find (t.id);
        if (it == p.tracks.end()) continue;
        std::map<int, Hit> out;
        for (auto& [key, h] : it->second.hits)
        {
            int bar = barOfKey (key), st = stepOfKey (key);
            if (st >= oldPer) continue;
            int ns = juce::jmin (newPer - 1, (int) std::lround ((double) st / oldPer * newPer));
            out.emplace (keyOf (bar, ns), h);
        }
        it->second.hits = std::move (out);
    }
}

void Session::setGrid (uint32_t id, int groups, int steps)
{
    auto* t = doc.findTrack (id);
    if (t == nullptr) return;
    groups = juce::jlimit (1, 11, groups);
    steps = juce::jlimit (1, 8, steps);
    if (groups == t->groups && steps == t->steps) return;
    static const int gridGesture = 0;
    checkpoint (&gridGesture);
    int oldPer = t->stepsPerBar();
    t->groups = groups;
    t->steps = steps;
    remapGrid (*t, oldPer, t->stepsPerBar());
    changed();
}

//==============================================================================
void Session::toggleHit (uint32_t id, int bar, int step)
{
    auto* t = doc.findTrack (id);
    if (t == nullptr) return;
    checkpoint();
    auto& hits = trackPart (id).hits;
    int key = keyOf (bar, step);
    auto it = hits.find (key);
    if (it != hits.end())
    {
        hits.erase (it);
    }
    else
    {
        Hit h;
        h.velocity = defaultVelocity (*t, step);
        hits[key] = h;
    }
    changed();
}

void Session::toggleMark (uint32_t id, int key)
{
    if (hitAt (id, key) == nullptr) return;
    checkpoint();
    auto* h = hitAt (id, key);
    h->marked = ! h->marked;
    selected = id;
    changed();
}

void Session::togglePin (uint32_t id, int key)
{
    auto* h = hitAt (id, key);
    if (h == nullptr) return;
    checkpoint();
    h = hitAt (id, key);
    h->pinned = ! h->pinned;
    toast (h->pinned ? juce::String::fromUTF8 ("Pinned \xc2\xb7 Random and Accuracy leave this hit alone") : "Unpinned");
    changed();
}

void Session::clearMarks()
{
    // The active track's marks in this part, or every track's when none is open
    auto& p = curPart();
    auto owns = [this] (uint32_t id) { return ! trackActive() || id == selected; };
    bool any = false;
    for (auto& [id, tp] : p.tracks)
        if (owns (id))
            for (auto& [k, h] : tp.hits) any = any || h.marked;
    if (! any) return;
    checkpoint();
    for (auto& [id, tp] : curPart().tracks)
        if (owns (id))
            for (auto& [k, h] : tp.hits) h.marked = false;
    changed();
}

bool Session::hasMarks() const
{
    return markedCount (selected) > 0;
}

int Session::markedCount (uint32_t id) const
{
    auto* t = doc.findTrack (id);
    if (t == nullptr) return 0;
    const auto& p = doc.parts[(size_t) part];
    auto it = p.tracks.find (id);
    if (it == p.tracks.end()) return 0;
    int n = 0;
    for (auto& [key, h] : it->second.hits)
        if (h.marked && barOfKey (key) < p.bars && stepOfKey (key) < t->stepsPerBar()) ++n;
    return n;
}

int Session::hitCount (uint32_t id) const
{
    auto* t = doc.findTrack (id);
    if (t == nullptr) return 0;
    const auto& p = doc.parts[(size_t) part];
    auto it = p.tracks.find (id);
    if (it == p.tracks.end()) return 0;
    int n = 0;
    for (auto& [key, h] : it->second.hits)
        if (barOfKey (key) < p.bars && stepOfKey (key) < t->stepsPerBar()) ++n;
    return n;
}

int Session::sliceNumberOf (const Track& t, int key)
{
    const auto& s = t.sound;
    if (s.play != PlayMode::Slices || s.slices.empty()) return 0;
    auto& p = curPart();
    auto it = p.tracks.find (t.id);
    if (it == p.tracks.end()) return 0;
    auto h = it->second.hits.find (key);
    if (h == it->second.hits.end()) return 0;
    if (h->second.sliceId >= 0)
    {
        int idx = resolveSliceIndex (s, h->second.sliceId);
        if (idx >= 0) return idx + 1;
    }
    int order = 0;
    const int per = t.stepsPerBar();
    for (auto& [k, hit] : it->second.hits)
    {
        if (barOfKey (k) >= p.bars || stepOfKey (k) >= per) continue;
        if (k == key) break;
        ++order;
    }
    return order % (int) s.slices.size() + 1;
}

void Session::scrollSlice (uint32_t id, int key, int delta)
{
    auto* t = doc.findTrack (id);
    if (t == nullptr) return;
    int n = (int) t->sound.slices.size();
    int cur = sliceNumberOf (*t, key);
    if (cur == 0 || n == 0) return;
    static const int sliceGesture = 0;
    checkpoint (&sliceGesture);
    int next = ((cur - 1 + delta) % n + n) % n;
    if (auto* h = hitAt (id, key)) h->sliceId = t->sound.slices[(size_t) next].id;
    changed();
}

//==============================================================================
void Session::pickPart (int index)
{
    if (index < 0 || index >= kNumParts) return;
    auto& p = doc.parts[(size_t) index];
    if (! p.exists)
    {
        checkpoint();
        p = Part();
        p.exists = true;
        toast ("Part " + juce::String::charToString ((juce::juce_wchar) ('A' + index)) + juce::String::fromUTF8 (" created \xc2\xb7 empty"));
        changed();
    }
    if (index == part)
    {
        engine.queuedPart = -1;
        uiChanged();
        return;
    }
    if (isPlaying())
    {
        engine.queuedPart = index;
    }
    else
    {
        part = index;
        engine.currentPart = index;
        engine.queuedPart = -1;
    }
    uiChanged();
}

void Session::cancelQueue()
{
    engine.queuedPart = -1;
    uiChanged();
}

void Session::setSwitchMode (SwitchMode m)
{
    engine.switchMode = (int) m;
    uiChanged();
}

void Session::setPartBars (int bars)
{
    bars = juce::jlimit (1, kMaxBars, bars);
    auto& p = curPart();
    if (p.bars == bars) return;
    checkpoint();
    if (bars < p.bars)
        for (auto& [id, tp] : p.tracks)
            for (auto it = tp.hits.begin(); it != tp.hits.end();)
                it = barOfKey (it->first) >= bars ? tp.hits.erase (it) : std::next (it);
    p.bars = bars;
    changed();
}

void Session::copyPart (int index)
{
    if (! doc.parts[(size_t) index].exists) return;
    partClip = doc.parts[(size_t) index];
    toast ("Part " + juce::String::charToString ((juce::juce_wchar) ('A' + index)) + " copied");
}

void Session::pastePart (int index)
{
    if (! partClip) return;
    checkpoint();
    auto clip = *partClip;
    // Only tracks that still exist
    for (auto it = clip.tracks.begin(); it != clip.tracks.end();)
        it = doc.findTrack (it->first) == nullptr ? clip.tracks.erase (it) : std::next (it);
    clip.exists = true;
    doc.parts[(size_t) index] = clip;
    toast ("Pasted into part " + juce::String::charToString ((juce::juce_wchar) ('A' + index)));
    changed();
}

void Session::clearPart (int index)
{
    int existing = 0;
    for (auto& p : doc.parts) existing += p.exists ? 1 : 0;
    if (! doc.parts[(size_t) index].exists || existing < 2) return;
    checkpoint();
    doc.parts[(size_t) index] = Part();
    if (index == part)
    {
        for (int i = 0; i < kNumParts; ++i)
            if (doc.parts[(size_t) i].exists) { part = i; engine.currentPart = i; break; }
    }
    if (engine.queuedPart.load() == index) engine.queuedPart = -1;
    toast ("Part " + juce::String::charToString ((juce::juce_wchar) ('A' + index)) + " cleared", true);
    changed();
}

static juce::String partLetter (int i) { return juce::String::charToString ((juce::juce_wchar) ('A' + i)); }

// Moves hits of one bar to another grid, keeping their time in the bar
static void putBarHits (std::map<int, Hit>& dst, int bar, int oldPer, const std::map<int, Hit>& stepHits, int newPer, bool dropSlices)
{
    for (auto it = dst.begin(); it != dst.end();)
        it = barOfKey (it->first) == bar ? dst.erase (it) : std::next (it);
    for (auto& [st, h] : stepHits)
    {
        int ns = juce::jmin (newPer - 1, (int) std::lround ((double) st / oldPer * newPer));
        auto copy = h;
        if (dropSlices) copy.sliceId = -1;
        dst.emplace (keyOf (bar, ns), copy);
    }
}

void Session::copyBar (int bar)
{
    BarClip c;
    c.part = part;
    c.from = bar;
    for (auto& t : doc.tracks)
    {
        auto& entry = c.data[t.id];
        entry.first = t.stepsPerBar();
        auto& p = curPart();
        auto it = p.tracks.find (t.id);
        if (it == p.tracks.end()) continue;
        for (auto& [key, h] : it->second.hits)
            if (barOfKey (key) == bar && stepOfKey (key) < entry.first)
                entry.second[stepOfKey (key)] = h;
    }
    barClip = c;
    toast ("Copied " + barClipDescription());
}

void Session::clearBar (int bar)
{
    checkpoint();
    for (auto& [id, tp] : curPart().tracks)
        for (auto it = tp.hits.begin(); it != tp.hits.end();)
            it = barOfKey (it->first) == bar ? tp.hits.erase (it) : std::next (it);
    toast ("Cleared bar " + juce::String (bar + 1), true);
    changed();
}

bool Session::canPasteBar (int bar) const
{
    return barClip && ! (barClip->part == part && barClip->from == bar);
}

void Session::pasteBar (int bar)
{
    if (! canPasteBar (bar)) return;
    checkpoint();
    for (auto& [id, entry] : barClip->data)
    {
        auto* t = doc.findTrack (id);
        if (t == nullptr) continue;
        putBarHits (trackPart (id).hits, bar, entry.first, entry.second, t->stepsPerBar(), false);
    }
    toast ("Pasted " + barClipDescription() + " into bar " + juce::String (bar + 1), true);
    changed();
}

juce::String Session::barClipDescription() const
{
    if (! barClip) return {};
    return partLetter (barClip->part) + juce::String::fromUTF8 (" \xc2\xb7 bar ") + juce::String (barClip->from + 1);
}

void Session::copyTrack (uint32_t id, int bar)
{
    auto* t = doc.findTrack (id);
    if (t == nullptr) return;
    TrackClip c;
    c.part = part;
    c.from = bar;
    c.track = id;
    c.name = t->name;
    c.per = t->stepsPerBar();
    auto& p = curPart();
    c.bars = p.bars;
    if (auto it = p.tracks.find (id); it != p.tracks.end())
        for (auto& [key, h] : it->second.hits)
        {
            int b = barOfKey (key), st = stepOfKey (key);
            if (st >= c.per || b >= p.bars) continue;
            if (bar < 0) c.hits[key] = h;
            else if (b == bar) c.hits[keyOf (0, st)] = h;
        }
    trackClip = c;
    toast ("Copied " + trackClipDescription());
}

bool Session::canPasteTrack (uint32_t id, int bar) const
{
    if (! trackClip || doc.findTrack (id) == nullptr) return false;
    if (trackClip->part != part || trackClip->track != id) return true;
    return trackClip->from >= 0 && trackClip->from != bar;
}

void Session::pasteTrack (uint32_t id, int bar)
{
    if (! canPasteTrack (id, bar)) return;
    auto* t = doc.findTrack (id);
    checkpoint();
    const bool otherTrack = trackClip->track != id;
    auto& hits = trackPart (id).hits;
    if (trackClip->from < 0)
    {
        // All bars: the part grows to the length of the copied one, other tracks stay as they are
        auto& target = curPart();
        if (target.bars < trackClip->bars) target.bars = juce::jmin (kMaxBars, trackClip->bars);
        for (int b = 0; b < trackClip->bars && b < target.bars; ++b)
        {
            std::map<int, Hit> stepHits;
            for (auto& [key, h] : trackClip->hits)
                if (barOfKey (key) == b) stepHits[stepOfKey (key)] = h;
            putBarHits (hits, b, trackClip->per, stepHits, t->stepsPerBar(), otherTrack);
        }
        toast ("Pasted " + trackClipDescription() + " into " + t->name, true);
    }
    else
    {
        std::map<int, Hit> stepHits;
        for (auto& [key, h] : trackClip->hits) stepHits[stepOfKey (key)] = h;
        putBarHits (hits, bar, trackClip->per, stepHits, t->stepsPerBar(), otherTrack);
        toast ("Pasted " + trackClipDescription() + " into " + t->name + " bar " + juce::String (bar + 1), true);
    }
    changed();
}

void Session::clearTrackBar (uint32_t id, int bar)
{
    auto* t = doc.findTrack (id);
    if (t == nullptr) return;
    checkpoint();
    auto& hits = trackPart (id).hits;
    for (auto it = hits.begin(); it != hits.end();)
        it = barOfKey (it->first) == bar ? hits.erase (it) : std::next (it);
    toast ("Cleared " + t->name + " bar " + juce::String (bar + 1), true);
    changed();
}

juce::String Session::trackClipDescription() const
{
    if (! trackClip) return {};
    auto dot = juce::String::fromUTF8 (" \xc2\xb7 ");
    return trackClip->name + dot + partLetter (trackClip->part) + dot
         + (trackClip->from < 0 ? juce::String ("all bars") : "bar " + juce::String (trackClip->from + 1));
}

//==============================================================================
void Session::setSlicesFromPositions (Track& t, std::vector<double> positions)
{
    positions.push_back (0.0);
    std::sort (positions.begin(), positions.end());
    std::vector<double> clean;
    for (auto p : positions)
    {
        p = juce::jlimit (0.0, 0.999, p);
        if (clean.empty() || p - clean.back() > 0.001) clean.push_back (p);
    }

    auto& s = t.sound;
    auto old = s.slices;
    std::vector<bool> used (old.size(), false);
    std::vector<Slice> out;
    for (size_t i = 0; i < clean.size(); ++i)
    {
        int id = -1;
        if (i == 0 && ! old.empty()) { id = old[0].id; used[0] = true; }
        else
        {
            for (size_t k = 1; k < old.size(); ++k)
                if (! used[k] && std::abs (old[k].start - clean[i]) < 0.003) { id = old[k].id; used[k] = true; break; }
        }
        if (id < 0) id = doc.nextSliceId++;
        out.push_back ({ id, clean[i] });
    }
    for (size_t k = 0; k < old.size(); ++k)
        if (! used[k]) s.lostSlices[old[k].id] = old[k].start;
    for (auto& sl : out) s.lostSlices.erase (sl.id);
    s.slices = out;
}

void Session::rechop (Track& t)
{
    auto sample = samples.get (t.samplePath);
    auto& s = t.sound;
    std::vector<double> pos;
    if (s.chop == ChopMode::Manual)
    {
        if (! s.slices.empty()) return;
        if (sample != nullptr) pos = SampleStore::slicePositionsByHits (*sample, s.sensitivity);
    }
    else if (s.chop == ChopMode::Equal)
    {
        for (int i = 1; i < s.equalParts; ++i) pos.push_back ((double) i / s.equalParts);
    }
    else if (sample != nullptr)
        pos = SampleStore::slicePositionsByHits (*sample, s.sensitivity);
    setSlicesFromPositions (t, pos);
}

void Session::addMarker (uint32_t id, double pos)
{
    auto* t = doc.findTrack (id);
    if (t == nullptr || pos <= 0.002 || pos >= 0.998) return;
    checkpoint();
    auto& s = t->sound;
    s.chop = ChopMode::Manual;
    for (auto& sl : s.slices)
        if (std::abs (sl.start - pos) < 0.002) return;
    s.slices.push_back ({ doc.nextSliceId++, pos });
    std::sort (s.slices.begin(), s.slices.end(), [] (auto& a, auto& b) { return a.start < b.start; });
    changed();
}

void Session::moveMarker (uint32_t id, int sliceIndex, double pos)
{
    auto* t = doc.findTrack (id);
    if (t == nullptr) return;
    auto& s = t->sound;
    if (sliceIndex <= 0 || sliceIndex >= (int) s.slices.size()) return;
    static const int moveGesture = 0;
    checkpoint (&moveGesture);
    s.chop = ChopMode::Manual;
    double lo = s.slices[(size_t) sliceIndex - 1].start + 0.004;
    double hi = sliceIndex + 1 < (int) s.slices.size() ? s.slices[(size_t) sliceIndex + 1].start - 0.004 : 0.996;
    s.slices[(size_t) sliceIndex].start = juce::jlimit (lo, juce::jmax (lo, hi), pos);
    changed();
}

void Session::removeMarker (uint32_t id, int sliceIndex)
{
    auto* t = doc.findTrack (id);
    if (t == nullptr) return;
    auto& s = t->sound;
    if (sliceIndex <= 0 || sliceIndex >= (int) s.slices.size()) return;
    checkpoint();
    s.chop = ChopMode::Manual;
    s.lostSlices[s.slices[(size_t) sliceIndex].id] = s.slices[(size_t) sliceIndex].start;
    s.slices.erase (s.slices.begin() + sliceIndex);
    changed();
}

bool Session::sliceToTrack (uint32_t id, int sliceIndex, int insertIndex)
{
    int idx = doc.indexOf (id);
    if (idx < 0) return false;
    const auto& src = doc.tracks[(size_t) idx];
    const auto& sl = src.sound.slices;
    if (sliceIndex < 0 || sliceIndex >= (int) sl.size()) return false;
    if ((int) doc.tracks.size() >= kMaxTracks) { toast ("16 tracks max"); return false; }
    checkpoint();

    // Same sample and settings, playing just this slice as a whole; no hits
    Track t = src;
    t.id = doc.nextTrackId++;
    t.name = src.name + " " + juce::String (sliceIndex + 1);
    t.nameManual = true;
    t.solo = false;
    t.sound.play = PlayMode::Whole;
    t.sound.regionStart = sl[(size_t) sliceIndex].start;
    t.sound.regionEnd = sliceIndex + 1 < (int) sl.size() ? sl[(size_t) sliceIndex + 1].start : 1.0;
    t.rows.clear();

    insertIndex = juce::jlimit (0, (int) doc.tracks.size(), insertIndex < 0 ? (int) doc.tracks.size() : insertIndex);
    doc.tracks.insert (doc.tracks.begin() + insertIndex, t);
    toast ("Slice " + juce::String (sliceIndex + 1) + juce::String::fromUTF8 (" \xe2\x86\x92 new track ") + t.name, true);
    changed();
    return true;
}

void Session::putSliceHit (uint32_t id, int bar, int step, int sliceIndex)
{
    auto* t = doc.findTrack (id);
    if (t == nullptr || sliceIndex < 0 || sliceIndex >= (int) t->sound.slices.size()) return;
    if (bar >= curPart().bars || step >= t->stepsPerBar()) return;
    checkpoint();
    auto& hits = trackPart (id).hits;
    auto key = keyOf (bar, step);
    auto it = hits.find (key);
    if (it == hits.end())
    {
        Hit h;
        h.velocity = defaultVelocity (*t, step);
        it = hits.emplace (key, h).first;
    }
    it->second.sliceId = t->sound.slices[(size_t) sliceIndex].id;
    toast ("Slice " + juce::String (sliceIndex + 1) + " in bar " + juce::String (bar + 1) + ", step " + juce::String (step + 1));
    changed();
}

//==============================================================================
void Session::togglePlay()
{
    if (isPlugin) return;
    bool now = ! engine.playRequest.load();
    if (now)
    {
        engine.currentPart = part;
        engine.queuedPart = -1;
    }
    engine.playRequest = now;
    uiChanged();
}

void Session::setBpm (double v)
{
    v = juce::jlimit (20.0, 300.0, std::round (v * 10.0) / 10.0);
    engine.bpm = v;
    uiChanged();
}

void Session::tap()
{
    double now = juce::Time::getMillisecondCounterHiRes() / 1000.0;
    if (! taps.empty() && now - taps.back() > 2.0) taps.clear();
    taps.push_back (now);
    if (taps.size() > 5) taps.erase (taps.begin());
    if (taps.size() >= 2)
    {
        double avg = (taps.back() - taps.front()) / (double) (taps.size() - 1);
        if (avg > 0.0) setBpm (60.0 / avg);
    }
}

void Session::setMaster (int v)
{
    master = juce::jlimit (0, 127, v);
    engine.masterGain = master / 100.0f;
    uiChanged();
}

juce::String Session::masterText (int v)
{
    if (v <= 0) return juce::String::fromUTF8 ("\xe2\x88\x92\xe2\x88\x9e dB");
    auto db = 20.0 * std::log10 (v / 100.0);
    if (std::abs (db) < 0.05) return "0.0 dB";
    return (db > 0 ? "+" : juce::String::fromUTF8 ("\xe2\x88\x92")) + juce::String (std::abs (db), 1) + " dB";
}

//==============================================================================
juce::File Session::libraryFolder() const
{
    auto dir = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("OKTAKT").getChildFile ("Patterns");
    dir.createDirectory();
    return dir;
}

juce::Array<juce::File> Session::libraryFiles() const
{
    auto files = libraryFolder().findChildFiles (juce::File::findFiles, false, "*.oktakt");
    std::sort (files.begin(), files.end(), [] (const juce::File& a, const juce::File& b)
               { return a.getLastModificationTime() > b.getLastModificationTime(); });
    return files;
}

static bool writeJson (const juce::File& f, const juce::var& v)
{
    return f.replaceWithText (juce::JSON::toString (v, false));
}

bool Session::save()
{
    if (patternFile == juce::File() || ! patternFile.getParentDirectory().isDirectory()) return false;
    if (! writeJson (patternFile, doc.toVar())) { toast ("Could not save " + patternFile.getFileName()); return true; }
    dirty = false;
    toast (juce::String::fromUTF8 ("Saved \xe2\x80\x9c") + doc.name + juce::String::fromUTF8 ("\xe2\x80\x9d \xc2\xb7 parts and tracks"));
    return true;
}

bool Session::saveAs (const juce::String& name)
{
    auto n = name.trim();
    if (n.isEmpty()) return false;
    doc.name = n;
    patternFile = libraryFolder().getChildFile (juce::File::createLegalFileName (n) + ".oktakt");
    return save();
}

void Session::load (const juce::File& f)
{
    auto v = juce::JSON::parse (f);
    if (! v.isObject() || v.getProperty ("format", "").toString() != "oktakt-pattern")
    {
        toast ("Not a pattern file: " + f.getFileName());
        return;
    }
    checkpoint();
    doc = Document::fromVar (v);
    patternFile = f;
    selected = 0;
    part = 0;
    for (int i = 0; i < kNumParts; ++i)
        if (doc.parts[(size_t) i].exists) { part = i; break; }
    engine.currentPart = part;
    engine.queuedPart = -1;
    checkMissing();
    publish();
    dirty = false;
    int missing = 0;
    for (auto& t : doc.tracks) missing += t.missing ? 1 : 0;
    toast (juce::String::fromUTF8 ("Loaded \xe2\x80\x9c") + doc.name + juce::String::fromUTF8 ("\xe2\x80\x9d")
           + (missing > 0 ? juce::String::fromUTF8 (" \xc2\xb7 ") + juce::String (missing) + (missing == 1 ? " sample" : " samples") + " not found" : juce::String()));
    sendChangeMessage();
}

void Session::newPattern()
{
    checkpoint();
    doc = Document();
    doc.name = "New pattern";
    patternFile = juce::File();
    selected = 0;
    part = 0;
    engine.currentPart = 0;
    engine.queuedPart = -1;
    toast (juce::String::fromUTF8 ("New pattern \xc2\xb7 drop samples to add tracks"), true);
    changed();
    dirty = false;   // nothing to lose yet
}

void Session::setPreview (const juce::String& path)
{
    if (path == previewPath && engine.previewing.load())
    {
        engine.stopPreview();
        previewPath.clear();
    }
    else
    {
        auto s = samples.get (path);
        engine.preview (s);
        previewPath = s != nullptr ? path : juce::String();
    }
    uiChanged();
}

void Session::previewSlice (uint32_t id, int sliceIndex)
{
    auto* t = doc.findTrack (id);
    if (t == nullptr) return;
    auto s = samples.get (t->samplePath);
    if (s == nullptr) return;
    auto& sl = t->sound.slices;
    double a = 0.0, b = 1.0;
    if (sliceIndex >= 0 && sliceIndex < (int) sl.size())
    {
        a = sl[(size_t) sliceIndex].start;
        b = sliceIndex + 1 < (int) sl.size() ? sl[(size_t) sliceIndex + 1].start : 1.0;
    }
    engine.preview (s, a, b);
}

//==============================================================================
juce::var Session::stateToVar() const
{
    auto* o = new juce::DynamicObject();
    o->setProperty ("pattern", doc.toVar());
    auto* s = new juce::DynamicObject();
    s->setProperty ("bpm", engine.bpm.load());
    s->setProperty ("master", master);
    s->setProperty ("metronome", engine.metronomeOn.load());
    s->setProperty ("metronomeVolume", metronomeVolume);
    s->setProperty ("metronomeAccent", metronomeAccent);
    s->setProperty ("metronomeBeats", metronomeBeats);
    s->setProperty ("switchMode", engine.switchMode.load());
    s->setProperty ("part", part);
    s->setProperty ("patternFile", patternFile.getFullPathName());
    s->setProperty ("dirty", dirty);
    s->setProperty ("browserRoot", browserRoot.getFullPathName());
    o->setProperty ("session", juce::var (s));
    return juce::var (o);
}

void Session::stateFromVar (const juce::var& v)
{
    if (! v.isObject()) return;
    doc = Document::fromVar (v.getProperty ("pattern", {}));
    auto s = v.getProperty ("session", {});
    engine.bpm = juce::jlimit (20.0, 300.0, (double) s.getProperty ("bpm", 120.0));
    setMaster ((int) s.getProperty ("master", 100));
    engine.metronomeOn = (bool) s.getProperty ("metronome", false);
    metronomeVolume = juce::jlimit (0, 100, (int) s.getProperty ("metronomeVolume", 70));
    engine.metronomeVolume = metronomeVolume / 100.0f;
    metronomeAccent = (bool) s.getProperty ("metronomeAccent", true);
    engine.metronomeAccent = metronomeAccent;
    metronomeBeats = juce::jlimit (2, 11, (int) s.getProperty ("metronomeBeats", 4));
    engine.metronomeBeats = metronomeBeats;
    engine.switchMode = juce::jlimit (0, 2, (int) s.getProperty ("switchMode", (int) SwitchMode::PartEnd));
    part = juce::jlimit (0, kNumParts - 1, (int) s.getProperty ("part", 0));
    if (! doc.parts[(size_t) part].exists) part = 0;
    engine.currentPart = part;
    engine.queuedPart = -1;
    auto pf = s.getProperty ("patternFile", "").toString();
    patternFile = pf.isNotEmpty() ? juce::File (pf) : juce::File();
    dirty = (bool) s.getProperty ("dirty", false);
    auto br = s.getProperty ("browserRoot", "").toString();
    if (br.isNotEmpty() && juce::File (br).isDirectory()) browserRoot = juce::File (br);
    selected = 0;
    undoStack.clear();
    redoStack.clear();
    checkMissing();
    publish();
    ++revision;
    sendChangeMessage();
}

} // namespace ok
