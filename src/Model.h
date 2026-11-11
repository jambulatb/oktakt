#pragma once

#include <juce_core/juce_core.h>
#include <array>
#include <map>
#include <vector>
#include <cstdint>

namespace ok
{

constexpr int kMaxTracks = 16;
constexpr int kNumParts = 4;
constexpr int kMaxBars = 4;
constexpr int kKeyStride = 128;   // hit key = bar * kKeyStride + step; max 11 * 8 = 88 steps per bar
constexpr int kNumOutputs = 16;   // Main + Out 2 … Out 16 (plugin only)

// File name of a sample path saved on any system: a pattern made on Windows keeps "C:\…\kick.wav",
// which juce::File cannot split on Linux or macOS
inline juce::String sampleFileName (const juce::String& path)
{
    return path.fromLastOccurrenceOf ("/", false, false).fromLastOccurrenceOf ("\\", false, false);
}

//==============================================================================
// Per-hit automation parameters (the rows under an active track)
enum class ParamId : int
{
    Velocity, Micro, Random, Accuracy,
    Stretch, Pitch,
    Cutoff, Resonance, EnvAmount,
    Drive, BitReduction, SRReduction,
    Count
};

constexpr int kNumParams = (int) ParamId::Count;

struct ParamInfo
{
    const char* name;     // stored in pattern files
    const char* label;    // shown in the UI
    const char* group;
    int min, max;
};

const ParamInfo& paramInfo (ParamId id);
ParamId paramFromName (const juce::String& name, bool& ok);

//==============================================================================
enum class PlayMode   { Whole, Slices };
enum class ChopMode   { Transients, Equal, Manual };
enum class SampleMode { KeepPitch, Repitch };
enum class AmpMode    { OneShot, AHD, ADSR };
enum class FilterType { LP12, LP24, HP12, HP24, BP, Notch, Ladder, Morph, Comb, Formant, Count };
enum class StereoMode { Original, Mono, Narrow, Wide, Haas, Alternate, Count };
enum class SwitchMode { Now, BarEnd, PartEnd };

const char* filterName (FilterType);
const char* stereoName (StereoMode);
const char* ampModeName (AmpMode);

struct Slice
{
    int id = 0;
    double start = 0.0;   // fraction of the sample, 0…1
};

struct SoundSettings
{
    PlayMode play = PlayMode::Whole;
    ChopMode chop = ChopMode::Transients;
    int sensitivity = 55;
    int equalParts = 8;
    std::vector<Slice> slices;            // sorted by start, first slice starts at 0
    std::map<int, double> lostSlices;     // removed slice id -> its old start

    SampleMode sampleMode = SampleMode::KeepPitch;
    bool reverse = false, swapLR = false;
    int start = 0, length = 127, stretch = 100;
    int fadeIn = 0, fadeOut = 0;          // One-shot IN / OUT: % of the played part (Start…Length)

    int tune = 0, fine = 0;

    AmpMode amp = AmpMode::OneShot;
    bool cut = false;
    int attack = 0, hold = 12, decay = 48, sustain = 90, release = 30;

    int punch = 0, tail = 0;

    FilterType filter = FilterType::LP24;
    int cutoff = 127, reso = 0, envAmt = 0, envDec = 48, baseHP = 0, baseLP = 127, morph = 0;

    int gain = 0;
    bool autoGain = true;
    int drive = 0, bitRed = 0, srRed = 0;
    StereoMode stereo = StereoMode::Original;
    std::array<int, (int) StereoMode::Count> stereoAmt { 0, 0, 50, 150, 12, 60 };

    double regionStart = 0.0, regionEnd = 1.0;   // used by "Slices → N tracks"

    static int defaultStereoAmt (StereoMode m);
};

//==============================================================================
struct StepSettings
{
    int condN = 1;          // loop cycle 1…8
    uint8_t condMask = 0xff;
    int chance = 100;
    int retrig = 0;         // index into retrig rates, 0 = Off
    int retrigLen = 1;      // 1…16 steps of the track grid
    int fade = 0;           // −100…+100

    bool operator== (const StepSettings& o) const
    {
        return condN == o.condN && (condMask & mask (condN)) == (o.condMask & mask (o.condN))
            && chance == o.chance && retrig == o.retrig && retrigLen == o.retrigLen && fade == o.fade;
    }
    bool operator!= (const StepSettings& o) const { return ! (*this == o); }
    static uint8_t mask (int n) { return (uint8_t) ((1 << n) - 1); }
    bool playsOnPass (int pass) const { return ((condMask >> (pass % condN)) & 1) != 0; }
};

constexpr int kNumRetrigRates = 9;
const char* retrigName (int index);
double retrigPpq (int index);   // length of one repeat in quarter notes

struct Hit
{
    static constexpr int16_t kUnset = INT16_MIN;

    int velocity = 100;
    std::array<int16_t, kNumParams> p;
    bool marked = false;   // marked for STEP: when a track has marked hits in a part, STEP acts on them only
    bool pinned = false;
    int sliceId = -1;     // -1 = slices play in hit order

    Hit() { p.fill (kUnset); }
    bool has (ParamId id) const { return p[(size_t) id] != kUnset; }
};

struct TrackPart
{
    std::map<int, Hit> hits;   // key = bar * kKeyStride + step
};

struct Part
{
    bool exists = false;
    int bars = 1;
    std::map<uint32_t, TrackPart> tracks;
};

struct Track
{
    uint32_t id = 0;
    juce::String name;
    bool nameManual = false;
    int colour = 0;               // palette index
    juce::String samplePath;
    bool missing = false;

    int groups = 4, steps = 4;
    int volume = 100, pan = 0;
    bool mute = false, solo = false;
    int output = 0;               // 0 = Main, n = Out n+1
    int random = 0, accuracy = 100;   // base values of the per-hit Random / Accuracy rows

    SoundSettings sound;
    std::vector<ParamId> rows;    // automation rows besides Velocity

    // STEP: one per track. Acts on the marked hits of a part, or on all its hits when none is marked
    StepSettings step;

    // POSITION: the track's Micro, the base value of the per-hit Micro row like SOUND is for the others
    int micro = 0;

    int stepsPerBar() const { return groups * steps; }
};

struct Document
{
    int version = 2;   // 2: a hit's Micro replaces the track's instead of adding to it
    juce::String name { "New pattern" };
    int swing = 50;
    std::vector<Track> tracks;
    std::array<Part, kNumParts> parts;
    uint32_t nextTrackId = 1;
    int nextSliceId = 1;

    Document();

    Track* findTrack (uint32_t id);
    const Track* findTrack (uint32_t id) const;
    int indexOf (uint32_t id) const;

    juce::var toVar() const;
    static Document fromVar (const juce::var& v);
};

//==============================================================================
// Helpers shared by UI and engine
int baseValue (const Track& t, ParamId id);           // the track's value for a per-hit parameter
int effectiveValue (const Track& t, const Hit& h, ParamId id);
void setHitParam (const Track& t, Hit& h, ParamId id, int value);   // stores only if different
int defaultVelocity (const Track& t, int stepInBar);

inline int keyOf (int bar, int step) { return bar * kKeyStride + step; }
inline int barOfKey (int key)        { return key / kKeyStride; }
inline int stepOfKey (int key)       { return key % kKeyStride; }

// Slices of a track, ordered: returns index of slice id, or -1
int sliceIndexOf (const SoundSettings& s, int sliceId);
int resolveSliceIndex (const SoundSettings& s, int sliceId);   // follows lost slices to the slice that now holds their start

juce::String paletteName (int index);
juce::uint32 paletteArgb (int index);
constexpr int kPaletteSize = 12;

} // namespace ok
