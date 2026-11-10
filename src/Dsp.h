#pragma once

#include <cmath>
#include <algorithm>
#include <juce_core/juce_core.h>

namespace ok::dsp
{

inline float fastTanh (float x)
{
    x = juce::jlimit (-4.5f, 4.5f, x);
    const float x2 = x * x;
    return x * (27.0f + x2) / (27.0f + 9.0f * x2);
}

// Knob 0…127 → frequency 20 Hz…20 kHz
inline float knobToHz (float v) { return 20.0f * std::pow (1000.0f, juce::jlimit (0.0f, 127.0f, v) / 127.0f); }

// Knob 0…127 → time in seconds for envelopes (≈2 ms … 6 s)
inline float knobToSeconds (int v)
{
    float x = juce::jlimit (0, 127, v) / 127.0f;
    return 0.002f + 6.0f * x * x * x;
}

// Topology-preserving state variable filter (Zavalishin)
struct Svf
{
    float ic1 = 0, ic2 = 0;
    float g = 0, k = 1, a1 = 0, a2 = 0, a3 = 0;

    void set (float hz, float q, float sr)
    {
        hz = juce::jlimit (10.0f, sr * 0.49f, hz);
        g = std::tan (juce::MathConstants<float>::pi * hz / sr);
        k = 1.0f / juce::jmax (0.05f, q);
        a1 = 1.0f / (1.0f + g * (g + k));
        a2 = g * a1;
        a3 = g * a2;
    }
    void reset() { ic1 = ic2 = 0; }

    // returns lp, bp, hp
    inline void process (float x, float& lp, float& bp, float& hp)
    {
        float v3 = x - ic2;
        float v1 = a1 * ic1 + a2 * v3;
        float v2 = ic2 + a2 * ic1 + a3 * v3;
        ic1 = 2 * v1 - ic1;
        ic2 = 2 * v2 - ic2;
        lp = v2;
        bp = v1;
        hp = x - k * v1 - v2;
    }
};

// 4-pole ladder with a soft-clipped feedback path
struct Ladder
{
    float s[4] {}, g = 0, res = 0;

    void set (float hz, float reso01, float sr)
    {
        hz = juce::jlimit (10.0f, sr * 0.45f, hz);
        float wc = std::tan (juce::MathConstants<float>::pi * hz / sr);
        g = wc / (1.0f + wc);
        res = reso01 * 3.8f;
    }
    void reset() { std::fill (std::begin (s), std::end (s), 0.0f); }

    inline float process (float x)
    {
        float in = fastTanh (x - res * s[3]) ;
        for (int i = 0; i < 4; ++i)
        {
            float v = g * (in - s[i]);
            float y = v + s[i];
            s[i] = y + v;
            in = y;
        }
        return in * (1.0f + res * 0.45f);
    }
};

// Feedback comb with fractional delay
struct Comb
{
    static constexpr int kSize = 8192;
    float buf[kSize] {};
    int w = 0;
    float delay = 100.0f, fb = 0.5f;

    void set (float hz, float feedback01, float sr)
    {
        delay = juce::jlimit (2.0f, (float) kSize - 4.0f, sr / juce::jmax (20.0f, hz));
        fb = feedback01 * 0.96f;
    }
    void reset() { std::fill (std::begin (buf), std::end (buf), 0.0f); w = 0; }

    inline float process (float x)
    {
        float rp = (float) w - delay;
        if (rp < 0) rp += kSize;
        int i0 = (int) rp;
        float fr = rp - (float) i0;
        float a = buf[i0 & (kSize - 1)], b = buf[(i0 + 1) & (kSize - 1)];
        float d = a + (b - a) * fr;
        float y = x + fb * d;
        buf[w] = fastTanh (y * 0.7f) / 0.7f;
        w = (w + 1) & (kSize - 1);
        return y * (1.0f - fb * 0.5f);
    }
};

// Three parallel band-passes at vowel formants
struct Formant
{
    Svf f[3];
    float gains[3] { 1, 0.6f, 0.3f };

    void set (float vowel01, float sharp01, float sr)
    {
        // A E I O U (F1, F2, F3)
        static const float F[5][3] = { { 800, 1150, 2900 }, { 400, 1700, 2600 }, { 280, 2250, 2900 }, { 450, 800, 2830 }, { 325, 700, 2530 } };
        float p = juce::jlimit (0.0f, 1.0f, vowel01) * 4.0f;
        int i0 = juce::jmin (3, (int) p);
        float fr = p - (float) i0;
        float q = 2.0f + sharp01 * 18.0f;
        for (int k = 0; k < 3; ++k)
            f[k].set (F[i0][k] + (F[i0 + 1][k] - F[i0][k]) * fr, q, sr);
    }
    void reset() { for (auto& x : f) x.reset(); }

    inline float process (float x)
    {
        float y = 0, lp, bp, hp;
        for (int k = 0; k < 3; ++k)
        {
            f[k].process (x, lp, bp, hp);
            y += bp * gains[k];
        }
        return y * 1.6f;
    }
};

// One-pole envelope follower with separate attack and release
struct Follower
{
    float env = 0, a = 0, r = 0;
    void set (float attackSec, float releaseSec, float sr)
    {
        a = std::exp (-1.0f / (attackSec * sr));
        r = std::exp (-1.0f / (releaseSec * sr));
    }
    inline float process (float x)
    {
        float c = x > env ? a : r;
        env = x + c * (env - x);
        return env;
    }
};

// Deterministic hash → [0, 1)
inline double hash01 (uint64_t a, uint64_t b, uint64_t c, uint64_t d)
{
    uint64_t x = a * 0x9E3779B97F4A7C15ull ^ (b + 0x632BE59BD9B4E019ull) * 0xBF58476D1CE4E5B9ull
               ^ (c + 0x8CB92BA72F3D8DD7ull) * 0x94D049BB133111EBull ^ (d + 0x2545F4914F6CDD1Dull);
    x ^= x >> 30; x *= 0xBF58476D1CE4E5B9ull;
    x ^= x >> 27; x *= 0x94D049BB133111EBull;
    x ^= x >> 31;
    return (double) (x >> 11) * (1.0 / 9007199254740992.0);
}

} // namespace ok::dsp
