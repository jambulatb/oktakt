#include "Model.h"
#include <set>

namespace ok
{

static const ParamInfo kParams[kNumParams] = {
    { "Velocity",      "Velocity", "HIT",       0, 127 },
    { "Micro",         "Micro",    "HIT",       -23, 23 },
    { "Stretch",       "Stretch",  "SAMPLE",    25, 400 },
    { "Pitch",         "Pitch",    "PITCH",     -24, 24 },
    { "Cutoff",        "Cutoff",   "FILTER",    0, 127 },
    { "Resonance",     "Reso",     "FILTER",    0, 127 },
    { "Env amount",    "Env amt",  "FILTER",    -64, 63 },
    { "Drive",         "Drive",    "CHARACTER", 0, 127 },
    { "Bit reduction", "Bit red.", "CHARACTER", 0, 127 },
    { "SR reduction",  "SR red.",  "CHARACTER", 0, 127 },
};

const ParamInfo& paramInfo (ParamId id) { return kParams[(int) id]; }

ParamId paramFromName (const juce::String& name, bool& ok)
{
    for (int i = 0; i < kNumParams; ++i)
        if (name == kParams[i].name) { ok = true; return (ParamId) i; }
    ok = false;
    return ParamId::Velocity;
}

const char* filterName (FilterType f)
{
    static const char* n[] = { "Lowpass 12", "Lowpass 24", "Highpass 12", "Highpass 24", "Bandpass",
                               "Notch", "Ladder", "Morph", "Comb", "Formant" };
    return n[(int) f];
}

const char* stereoName (StereoMode m)
{
    static const char* n[] = { "Original", "Mono", "Narrow", "Wide", "Haas", "Alternate" };
    return n[(int) m];
}

const char* ampModeName (AmpMode m)
{
    static const char* n[] = { "One-shot", "AHD", "ADSR" };
    return n[(int) m];
}

int SoundSettings::defaultStereoAmt (StereoMode m)
{
    static const int d[] = { 0, 0, 50, 150, 12, 60 };
    return d[(int) m];
}

const char* retrigName (int index)
{
    static const char* n[] = { "Off", "1/8", "1/8T", "1/16", "1/16T", "1/32", "1/32T", "1/64", "1/64T" };
    return n[juce::jlimit (0, kNumRetrigRates - 1, index)];
}

double retrigPpq (int index)
{
    static const double r[] = { 0.0, 0.5, 1.0 / 3.0, 0.25, 1.0 / 6.0, 0.125, 1.0 / 12.0, 1.0 / 16.0, 1.0 / 24.0 };
    return r[juce::jlimit (0, kNumRetrigRates - 1, index)];
}

static const juce::uint32 kPalette[kPaletteSize] = {
    0xffF2A23A, 0xffFF7A6B, 0xffE86A8E, 0xffC77DFF, 0xff9C8CFF, 0xff6FA8FF,
    0xff5CC6B8, 0xff7BD88F, 0xffC6D85C, 0xffF5D547, 0xffD9A46C, 0xffA6A8AE
};

juce::uint32 paletteArgb (int index) { return kPalette[juce::jlimit (0, kPaletteSize - 1, index)]; }

juce::String paletteName (int index)
{
    static const char* n[] = { "Amber", "Coral", "Pink", "Violet", "Lavender", "Blue", "Teal", "Green", "Lime", "Yellow", "Tan", "Grey" };
    return n[juce::jlimit (0, kPaletteSize - 1, index)];
}

//==============================================================================
Document::Document()
{
    parts[0].exists = true;
}

Track* Document::findTrack (uint32_t id)
{
    for (auto& t : tracks)
        if (t.id == id) return &t;
    return nullptr;
}

const Track* Document::findTrack (uint32_t id) const
{
    for (auto& t : tracks)
        if (t.id == id) return &t;
    return nullptr;
}

int Document::indexOf (uint32_t id) const
{
    for (size_t i = 0; i < tracks.size(); ++i)
        if (tracks[i].id == id) return (int) i;
    return -1;
}

//==============================================================================
int baseValue (const Track& t, ParamId id)
{
    const auto& s = t.sound;
    switch (id)
    {
        case ParamId::Velocity:     return 100;
        case ParamId::Micro:        return 0;
        case ParamId::Stretch:      return s.stretch;
        case ParamId::Pitch:        return s.tune;
        case ParamId::Cutoff:       return s.cutoff;
        case ParamId::Resonance:    return s.reso;
        case ParamId::EnvAmount:    return s.envAmt;
        case ParamId::Drive:        return s.drive;
        case ParamId::BitReduction: return s.bitRed;
        case ParamId::SRReduction:  return s.srRed;
        case ParamId::Count:        break;
    }
    return 0;
}

int effectiveValue (const Track& t, const Hit& h, ParamId id)
{
    if (id == ParamId::Velocity) return h.velocity;
    if (h.has (id)) return h.p[(size_t) id];
    return baseValue (t, id);
}

void setHitParam (const Track& t, Hit& h, ParamId id, int value)
{
    auto& info = paramInfo (id);
    value = juce::jlimit (info.min, info.max, value);
    if (id == ParamId::Velocity) { h.velocity = value; return; }
    if (value == baseValue (t, id)) h.p[(size_t) id] = Hit::kUnset;
    else h.p[(size_t) id] = (int16_t) value;
}

int defaultVelocity (const Track& t, int stepInBar)
{
    return (stepInBar % juce::jmax (1, t.steps)) == 0 ? 120 : 96;
}

int sliceIndexOf (const SoundSettings& s, int sliceId)
{
    for (size_t i = 0; i < s.slices.size(); ++i)
        if (s.slices[i].id == sliceId) return (int) i;
    return -1;
}

int resolveSliceIndex (const SoundSettings& s, int sliceId)
{
    if (s.slices.empty()) return -1;
    auto idx = sliceIndexOf (s, sliceId);
    if (idx >= 0) return idx;

    auto lost = s.lostSlices.find (sliceId);
    if (lost == s.lostSlices.end()) return -1;

    int found = 0;
    for (size_t i = 0; i < s.slices.size(); ++i)
        if (s.slices[i].start <= lost->second + 1e-9) found = (int) i;
    return found;
}

//==============================================================================
// JSON
static juce::var stepToVar (const StepSettings& s)
{
    auto* o = new juce::DynamicObject();
    o->setProperty ("condN", s.condN);
    o->setProperty ("condMask", (int) s.condMask);
    o->setProperty ("chance", s.chance);
    o->setProperty ("retrig", juce::String (retrigName (s.retrig)));
    o->setProperty ("retrigLen", s.retrigLen);
    o->setProperty ("fade", s.fade);
    return juce::var (o);
}

static StepSettings stepFromVar (const juce::var& v)
{
    StepSettings s;
    if (! v.isObject()) return s;
    s.condN = juce::jlimit (1, 8, (int) v.getProperty ("condN", 1));
    s.condMask = (uint8_t) (int) v.getProperty ("condMask", 255);
    s.chance = juce::jlimit (0, 100, (int) v.getProperty ("chance", 100));
    auto r = v.getProperty ("retrig", "Off").toString();
    for (int i = 0; i < kNumRetrigRates; ++i)
        if (r == retrigName (i)) s.retrig = i;
    s.retrigLen = juce::jlimit (1, 16, (int) v.getProperty ("retrigLen", 1));
    s.fade = juce::jlimit (-100, 100, (int) v.getProperty ("fade", 0));
    return s;
}

static juce::var soundToVar (const SoundSettings& s)
{
    auto* o = new juce::DynamicObject();
    o->setProperty ("play", s.play == PlayMode::Slices ? "Slices" : "Whole");
    o->setProperty ("chop", s.chop == ChopMode::Equal ? "Equal" : (s.chop == ChopMode::Manual ? "Manual" : "Transients"));
    o->setProperty ("sensitivity", s.sensitivity);
    o->setProperty ("equalParts", s.equalParts);

    juce::Array<juce::var> sl;
    for (auto& x : s.slices)
    {
        auto* so = new juce::DynamicObject();
        so->setProperty ("id", x.id);
        so->setProperty ("start", x.start);
        sl.add (juce::var (so));
    }
    o->setProperty ("slices", sl);

    juce::Array<juce::var> lost;
    for (auto& [id, start] : s.lostSlices)
    {
        auto* so = new juce::DynamicObject();
        so->setProperty ("id", id);
        so->setProperty ("start", start);
        lost.add (juce::var (so));
    }
    o->setProperty ("lostSlices", lost);

    o->setProperty ("sampleMode", s.sampleMode == SampleMode::Repitch ? "Repitch" : "Keep pitch");
    o->setProperty ("reverse", s.reverse);
    o->setProperty ("swapLR", s.swapLR);
    o->setProperty ("start", s.start);
    o->setProperty ("length", s.length);
    o->setProperty ("stretch", s.stretch);
    o->setProperty ("fadeIn", s.fadeIn);
    o->setProperty ("fadeOut", s.fadeOut);
    o->setProperty ("tune", s.tune);
    o->setProperty ("fine", s.fine);
    o->setProperty ("amp", juce::String (ampModeName (s.amp)));
    o->setProperty ("cut", s.cut);
    o->setProperty ("attack", s.attack);
    o->setProperty ("hold", s.hold);
    o->setProperty ("decay", s.decay);
    o->setProperty ("sustain", s.sustain);
    o->setProperty ("release", s.release);
    o->setProperty ("punch", s.punch);
    o->setProperty ("tail", s.tail);
    o->setProperty ("filter", juce::String (filterName (s.filter)));
    o->setProperty ("cutoff", s.cutoff);
    o->setProperty ("reso", s.reso);
    o->setProperty ("envAmt", s.envAmt);
    o->setProperty ("envDec", s.envDec);
    o->setProperty ("baseHP", s.baseHP);
    o->setProperty ("baseLP", s.baseLP);
    o->setProperty ("morph", s.morph);
    o->setProperty ("gain", s.gain);
    o->setProperty ("autoGain", s.autoGain);
    o->setProperty ("drive", s.drive);
    o->setProperty ("bitRed", s.bitRed);
    o->setProperty ("srRed", s.srRed);
    o->setProperty ("stereo", juce::String (stereoName (s.stereo)));
    juce::Array<juce::var> amts;
    for (auto a : s.stereoAmt) amts.add (a);
    o->setProperty ("stereoAmt", amts);
    o->setProperty ("regionStart", s.regionStart);
    o->setProperty ("regionEnd", s.regionEnd);
    return juce::var (o);
}

template <typename E>
static E enumByName (const juce::String& s, int count, const char* (*nameFn) (E), E fallback)
{
    for (int i = 0; i < count; ++i)
        if (s == nameFn ((E) i)) return (E) i;
    return fallback;
}

static SoundSettings soundFromVar (const juce::var& v)
{
    SoundSettings s;
    if (! v.isObject()) return s;
    auto gi = [&] (const char* k, int def, int lo, int hi) { return juce::jlimit (lo, hi, (int) v.getProperty (k, def)); };

    s.play = v.getProperty ("play", "Whole").toString() == "Slices" ? PlayMode::Slices : PlayMode::Whole;
    auto chop = v.getProperty ("chop", "Transients").toString();
    s.chop = chop == "Equal" ? ChopMode::Equal : (chop == "Manual" ? ChopMode::Manual : ChopMode::Transients);
    s.sensitivity = gi ("sensitivity", 55, 0, 100);
    s.equalParts = gi ("equalParts", 8, 2, 20);

    if (auto* arr = v.getProperty ("slices", {}).getArray())
        for (auto& x : *arr)
            s.slices.push_back ({ (int) x.getProperty ("id", 0), (double) x.getProperty ("start", 0.0) });
    std::sort (s.slices.begin(), s.slices.end(), [] (auto& a, auto& b) { return a.start < b.start; });

    if (auto* arr = v.getProperty ("lostSlices", {}).getArray())
        for (auto& x : *arr)
            s.lostSlices[(int) x.getProperty ("id", 0)] = (double) x.getProperty ("start", 0.0);

    s.sampleMode = v.getProperty ("sampleMode", "Keep pitch").toString() == "Repitch" ? SampleMode::Repitch : SampleMode::KeepPitch;
    s.reverse = (bool) v.getProperty ("reverse", false);
    s.swapLR = (bool) v.getProperty ("swapLR", false);
    s.start = gi ("start", 0, 0, 127);
    s.length = gi ("length", 127, 1, 127);
    s.stretch = gi ("stretch", 100, 25, 400);
    s.fadeIn = gi ("fadeIn", 0, 0, 100);
    s.fadeOut = gi ("fadeOut", 0, 0, 100);
    s.tune = gi ("tune", 0, -24, 24);
    s.fine = gi ("fine", 0, -50, 50);
    s.amp = enumByName<AmpMode> (v.getProperty ("amp", "One-shot").toString(), 3, ampModeName, AmpMode::OneShot);
    s.cut = (bool) v.getProperty ("cut", false);
    s.attack = gi ("attack", 0, 0, 127);
    s.hold = gi ("hold", 12, 0, 127);
    s.decay = gi ("decay", 48, 0, 127);
    s.sustain = gi ("sustain", 90, 0, 127);
    s.release = gi ("release", 30, 0, 127);
    s.punch = gi ("punch", 0, -100, 100);
    s.tail = gi ("tail", 0, -100, 100);
    s.filter = enumByName<FilterType> (v.getProperty ("filter", "Lowpass 24").toString(), (int) FilterType::Count, filterName, FilterType::LP24);
    s.cutoff = gi ("cutoff", 127, 0, 127);
    s.reso = gi ("reso", 0, 0, 127);
    s.envAmt = gi ("envAmt", 0, -64, 63);
    s.envDec = gi ("envDec", 48, 0, 127);
    s.baseHP = gi ("baseHP", 0, 0, 127);
    s.baseLP = gi ("baseLP", 127, 0, 127);
    s.morph = gi ("morph", 0, 0, 127);
    s.gain = gi ("gain", 0, -12, 24);
    s.autoGain = (bool) v.getProperty ("autoGain", true);
    s.drive = gi ("drive", 0, 0, 127);
    s.bitRed = gi ("bitRed", 0, 0, 127);
    s.srRed = gi ("srRed", 0, 0, 127);
    s.stereo = enumByName<StereoMode> (v.getProperty ("stereo", "Original").toString(), (int) StereoMode::Count, stereoName, StereoMode::Original);
    if (auto* arr = v.getProperty ("stereoAmt", {}).getArray())
        for (int i = 0; i < juce::jmin ((int) s.stereoAmt.size(), arr->size()); ++i)
            s.stereoAmt[(size_t) i] = (int) (*arr)[i];
    s.regionStart = juce::jlimit (0.0, 1.0, (double) v.getProperty ("regionStart", 0.0));
    s.regionEnd = juce::jlimit (0.0, 1.0, (double) v.getProperty ("regionEnd", 1.0));
    if (s.regionEnd <= s.regionStart) { s.regionStart = 0.0; s.regionEnd = 1.0; }
    return s;
}

juce::var Document::toVar() const
{
    auto* root = new juce::DynamicObject();
    root->setProperty ("format", "oktakt-pattern");
    root->setProperty ("version", version);
    root->setProperty ("name", name);
    root->setProperty ("swing", swing);
    root->setProperty ("nextTrackId", (int) nextTrackId);
    root->setProperty ("nextSliceId", nextSliceId);

    juce::Array<juce::var> tr;
    for (auto& t : tracks)
    {
        auto* o = new juce::DynamicObject();
        o->setProperty ("id", (int) t.id);
        o->setProperty ("name", t.name);
        o->setProperty ("nameManual", t.nameManual);
        o->setProperty ("color", t.colour);
        o->setProperty ("sample", t.samplePath);
        o->setProperty ("groups", t.groups);
        o->setProperty ("steps", t.steps);
        o->setProperty ("volume", t.volume);
        o->setProperty ("pan", t.pan);
        o->setProperty ("mute", t.mute);
        o->setProperty ("solo", t.solo);
        o->setProperty ("output", t.output);
        o->setProperty ("random", t.random);
        o->setProperty ("accuracy", t.accuracy);
        juce::Array<juce::var> rows;
        for (auto r : t.rows) rows.add (juce::String (paramInfo (r).name));
        o->setProperty ("rows", rows);
        o->setProperty ("sound", soundToVar (t.sound));
        o->setProperty ("step", stepToVar (t.step));
        if (t.micro != 0) o->setProperty ("micro", t.micro);
        tr.add (juce::var (o));
    }
    root->setProperty ("tracks", tr);

    juce::Array<juce::var> ps;
    for (auto& p : parts)
    {
        auto* po = new juce::DynamicObject();
        po->setProperty ("exists", p.exists);
        po->setProperty ("bars", p.bars);
        auto* tp = new juce::DynamicObject();
        for (auto& [tid, part] : p.tracks)
        {
            juce::Array<juce::var> hits;
            for (auto& [key, h] : part.hits)
            {
                auto* ho = new juce::DynamicObject();
                ho->setProperty ("bar", barOfKey (key));
                ho->setProperty ("step", stepOfKey (key));
                ho->setProperty ("vel", h.velocity);
                auto* pp = new juce::DynamicObject();
                for (int i = 0; i < kNumParams; ++i)
                    if (h.p[(size_t) i] != Hit::kUnset)
                        pp->setProperty (kParams[i].name, (int) h.p[(size_t) i]);
                ho->setProperty ("params", juce::var (pp));
                if (h.marked) ho->setProperty ("marked", true);
                if (h.pinned) ho->setProperty ("pinned", true);
                if (h.sliceId >= 0) ho->setProperty ("slice", h.sliceId);
                hits.add (juce::var (ho));
            }
            tp->setProperty (juce::String ((int) tid), hits);
        }
        po->setProperty ("tracks", juce::var (tp));
        ps.add (juce::var (po));
    }
    root->setProperty ("parts", ps);
    return juce::var (root);
}

Document Document::fromVar (const juce::var& v)
{
    Document d;
    d.version = (int) v.getProperty ("version", 1);
    d.name = v.getProperty ("name", "Pattern").toString();
    d.swing = juce::jlimit (50, 75, (int) v.getProperty ("swing", 50));
    d.nextTrackId = (uint32_t) (int) v.getProperty ("nextTrackId", 1);
    d.nextSliceId = (int) v.getProperty ("nextSliceId", 1);
    std::set<uint32_t> oldStepTracks;

    if (auto* arr = v.getProperty ("tracks", {}).getArray())
    {
        for (auto& o : *arr)
        {
            if ((int) d.tracks.size() >= kMaxTracks) break;
            Track t;
            t.id = (uint32_t) (int) o.getProperty ("id", 0);
            t.name = o.getProperty ("name", "Track").toString();
            t.nameManual = (bool) o.getProperty ("nameManual", false);
            t.colour = juce::jlimit (0, kPaletteSize - 1, (int) o.getProperty ("color", 0));
            t.samplePath = o.getProperty ("sample", "").toString();
            t.groups = juce::jlimit (1, 11, (int) o.getProperty ("groups", 4));
            t.steps = juce::jlimit (1, 8, (int) o.getProperty ("steps", 4));
            t.volume = juce::jlimit (0, 127, (int) o.getProperty ("volume", 100));
            t.pan = juce::jlimit (-64, 63, (int) o.getProperty ("pan", 0));
            t.mute = (bool) o.getProperty ("mute", false);
            t.solo = (bool) o.getProperty ("solo", false);
            t.output = juce::jlimit (0, kNumOutputs - 1, (int) o.getProperty ("output", 0));
            t.random = juce::jlimit (0, 100, (int) o.getProperty ("random", 0));
            t.accuracy = juce::jlimit (0, 100, (int) o.getProperty ("accuracy", 100));
            if (auto* rows = o.getProperty ("rows", {}).getArray())
                for (auto& r : *rows)
                {
                    bool ok = false;
                    auto id = paramFromName (r.toString(), ok);
                    if (ok && id != ParamId::Velocity) t.rows.push_back (id);
                }
            t.sound = soundFromVar (o.getProperty ("sound", {}));
            t.step = stepFromVar (o.getProperty ("step", {}));
            t.micro = juce::jlimit (-23, 23, (int) o.getProperty ("micro", o.getProperty ("stepMicro", 0)));
            if (! o.hasProperty ("step")) oldStepTracks.insert (t.id);
            if (t.id == 0 || d.findTrack (t.id) != nullptr) t.id = d.nextTrackId;
            d.nextTrackId = juce::jmax (d.nextTrackId, t.id + 1);
            for (auto& sl : t.sound.slices) d.nextSliceId = juce::jmax (d.nextSliceId, sl.id + 1);
            for (auto& [id, st] : t.sound.lostSlices) d.nextSliceId = juce::jmax (d.nextSliceId, id + 1);
            d.tracks.push_back (std::move (t));
        }
    }

    if (auto* arr = v.getProperty ("parts", {}).getArray())
    {
        for (int pi = 0; pi < juce::jmin (kNumParts, arr->size()); ++pi)
        {
            const auto po = (*arr)[pi];
            auto& p = d.parts[(size_t) pi];
            p.exists = (bool) po.getProperty ("exists", pi == 0);
            p.bars = juce::jlimit (1, kMaxBars, (int) po.getProperty ("bars", 1));
            if (auto* obj = po.getProperty ("tracks", {}).getDynamicObject())
            {
                for (auto& prop : obj->getProperties())
                {
                    auto tid = (uint32_t) prop.name.toString().getIntValue();
                    if (d.findTrack (tid) == nullptr) continue;
                    auto& tp = p.tracks[tid];
                    if (auto* hits = prop.value.getArray())
                    {
                        for (auto& ho : *hits)
                        {
                            Hit h;
                            int bar = juce::jlimit (0, kMaxBars - 1, (int) ho.getProperty ("bar", 0));
                            int step = juce::jlimit (0, kKeyStride - 1, (int) ho.getProperty ("step", 0));
                            h.velocity = juce::jlimit (0, 127, (int) ho.getProperty ("vel", 100));
                            if (auto* pp = ho.getProperty ("params", {}).getDynamicObject())
                                for (auto& pr : pp->getProperties())
                                {
                                    bool ok = false;
                                    auto id = paramFromName (pr.name.toString(), ok);
                                    if (ok && id != ParamId::Velocity)
                                        h.p[(size_t) id] = (int16_t) juce::jlimit (paramInfo (id).min, paramInfo (id).max, (int) pr.value);
                                }
                            h.marked = (bool) ho.getProperty ("marked", false);
                            // Older files kept STEP per hit: the first one set becomes the track's
                            if (oldStepTracks.count (tid) != 0 && ho.hasProperty ("step_"))
                            {
                                auto st = stepFromVar (ho.getProperty ("step_", {}));
                                if (st != StepSettings())
                                {
                                    d.findTrack (tid)->step = st;
                                    oldStepTracks.erase (tid);
                                }
                            }
                            h.pinned = (bool) ho.getProperty ("pinned", false);
                            h.sliceId = (int) ho.getProperty ("slice", -1);
                            tp.hits[keyOf (bar, step)] = h;
                        }
                    }
                }
            }
        }
    }

    bool any = false;
    for (auto& p : d.parts) any = any || p.exists;
    if (! any) d.parts[0].exists = true;
    return d;
}

} // namespace ok
