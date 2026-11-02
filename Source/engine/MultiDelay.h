#pragma once

#include "DspUtil.h"
#include <array>

// Pattern and Warp: one stereo delay line with up to eight taps.
// Pattern arranges the taps into four rhythms. Warp runs each tap through a filter
// or pitch shifter. Taps glide like tape when the time changes.
namespace spk
{
class MultiDelay
{
public:
    static constexpr int kTaps = 8;

    struct Settings
    {
        bool warp = false;
        int variant = 0;
        float activity = 0.5f, repeats = 0.4f;
        float stepSamples = 12000.0f;
        bool reverse = false;
    };

    void prepare (float sampleRate)
    {
        fs = sampleRate;
        for (auto& l : line) l.init (nextPow2 ((int) (10.0f * fs)));
        for (auto& t : taps) t = Tap();
        for (int k = 0; k < kTaps; ++k)
        {
            taps[(size_t) k].time.setTime (0.35f, fs);
            taps[(size_t) k].mix.setTime (0.08f, fs);
            taps[(size_t) k].lfo = 0.13f * k;
        }
        for (int c = 0; c < 2; ++c) { fbLp[c].setCutoff (7000.0f, fs); fbHp[c].setCutoff (90.0f, fs); fbLp[c].reset(); fbHp[c].reset(); }
        fbSmooth.setTime (0.05f, fs);
        pitchWin = 0.075f * fs;
    }

    void clear() { for (auto& l : line) l.clear(); }

    void process (float inL, float inR, float& outL, float& outR, const Settings& s)
    {
        static const float patterns[4][kTaps] = {
            { 1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 7.0f, 8.0f },              // classic, even repeats
            { 1.0f, 1.5f, 2.5f, 3.0f, 4.0f, 4.5f, 5.5f, 6.0f },              // dotted, syncopated
            { 0.5f, 1.5f, 2.0f, 3.5f, 4.0f, 5.5f, 6.0f, 7.5f },              // off-beat answers
            { 1.0f, 1.333f, 1.667f, 3.0f, 3.333f, 3.667f, 5.0f, 6.0f } };    // triplet bursts
        static const float warpTimes[kTaps] = { 1.0f, 1.75f, 2.5f, 3.25f, 4.0f, 5.0f, 6.0f, 7.0f };
        static const float shifts[kTaps] = { 2.0f, 0.5f, 1.4983f, 2.0f, 0.5f, 4.0f, 1.4983f, 2.0f };

        const int n = 1 + (int) std::lround (clampf (s.activity, 0.0f, 1.0f) * (kTaps - 1));
        const float* times = s.warp ? warpTimes : patterns[s.variant & 3];
        const float maxDelay = (float) line[0].size() * 0.5f - pitchWin - 8.0f;

        float sumL = 0, sumR = 0, lastL = 0, lastR = 0, fbL = 0, fbR = 0;
        for (int k = 0; k < kTaps; ++k)
        {
            Tap& t = taps[(size_t) k];
            const float on = t.mix.next (k < n ? 1.0f : 0.0f);
            const float d = t.time.next (clampf (times[k] * s.stepSamples, 64.0f, maxDelay));
            if (on < 1.0e-4f) continue;

            float l, r;
            if (s.reverse) readReverse (t, d, l, r);
            else { l = line[0].tap (d); r = line[1].tap (d); }

            if (s.warp)
            {
                switch (s.variant & 3)
                {
                    case 0: // envelope filter: each echo opens and closes with its own level
                    {
                        const float lev = std::fabs (l) + std::fabs (r);
                        t.env = lev > t.env ? t.env + (lev - t.env) * 0.02f : t.env * 0.99985f;
                        const float fc = 250.0f + 9000.0f * std::min (1.0f, t.env * 3.0f);
                        if (((++t.count) & 15) == 0) { t.f[0].set (fc, 2.2f, fs); t.f[1].set (fc, 2.2f, fs); }
                        l = t.f[0].lp (l); r = t.f[1].lp (r);
                        break;
                    }
                    case 1: // resonant band-pass, each tap on its own note of the spectrum
                    {
                        if (! t.bpSet) { const float fc = 420.0f * std::pow (2.0f, 0.62f * k); t.f[0].set (fc, 5.0f, fs); t.f[1].set (fc, 5.0f, fs); t.bpSet = true; }
                        l = t.f[0].bpNorm (l) * 2.8f; r = t.f[1].bpNorm (r) * 2.8f;
                        break;
                    }
                    case 2: // pitch shifted taps: octaves and fifths that climb through the feedback
                        readPitched (t, d, shifts[k], l, r);
                        break;
                    default: // taps cross-fade with double speed grains
                    {
                        float pl, pr;
                        readPitched (t, d, 2.0f, pl, pr);
                        t.lfo += (0.11f + 0.04f * k) / fs;
                        if (t.lfo > 1.0f) t.lfo -= 1.0f;
                        const float x = 0.5f - 0.5f * std::cos (kTwoPi * t.lfo);
                        l = l * std::cos (x * kPi * 0.5f) + pl * std::sin (x * kPi * 0.5f);
                        r = r * std::cos (x * kPi * 0.5f) + pr * std::sin (x * kPi * 0.5f);
                        break;
                    }
                }
            }
            if (s.warp && (s.variant & 3) != 1) t.bpSet = false;

            const float gain = on * (1.0f - 0.45f * (float) k / (float) kTaps);
            const float pan = (k & 1) ? 0.55f : -0.55f;
            const float a = (pan + 1.0f) * kPi * 0.25f;
            const float gl = std::cos (a) * 1.41421356f, gr = std::sin (a) * 1.41421356f;
            // keep each tap's own stereo, leaned to its side
            sumL += gain * (l * gl * 0.8f + r * 0.2f);
            sumR += gain * (r * gr * 0.8f + l * 0.2f);
            fbL += l * on; fbR += r * on;
            if (k == n - 1) { lastL = l * on; lastR = r * on; }
        }

        const float norm = 1.0f / std::sqrt ((float) n);
        outL = sumL * norm * 0.95f;
        outR = sumR * norm * 0.95f;

        // feedback: Pattern repeats the whole rhythm from its last tap; Warp feeds every tap back
        const float fbAmt = fbSmooth.next (s.warp ? 0.93f * std::pow (s.repeats, 0.8f) : 0.97f * std::pow (s.repeats, 0.7f));
        float xl, xr;
        if (s.warp) { xl = fbL / n; xr = fbR / n; } // averaged, so stacked taps can't run away
        else { xl = lastL; xr = lastR; }
        xl = fbLp[0].process (xl); xr = fbLp[1].process (xr);
        xl -= fbHp[0].process (xl); xr -= fbHp[1].process (xr);
        line[0].push (softClip (inL + xl * fbAmt));
        line[1].push (softClip (inR + xr * fbAmt));
    }

private:
    struct Tap
    {
        Smoother time, mix;
        Svf f[2];
        float env = 0.0f, lfo = 0.0f, rev = 0.0f, pitch = 0.0f;
        int count = 0;
        bool bpSet = false;
    };

    // Two heads read backwards over a window as long as the delay, cross-fading as they swap
    void readReverse (Tap& t, float d, float& l, float& r)
    {
        const float w = std::max (256.0f, d);
        t.rev += 1.0f;
        if (t.rev >= w) t.rev -= w;
        l = r = 0.0f;
        for (int h = 0; h < 2; ++h)
        {
            float ph = t.rev + (h ? 0.5f * w : 0.0f);
            if (ph >= w) ph -= w;
            const float back = 2.0f * ph + 4.0f;
            const float g = std::sin (kPi * ph / w);
            l += line[0].tap (back + d * 0.0f) * g * g;
            r += line[1].tap (back) * g * g;
        }
    }

    // Classic two-head pitch shifter riding on the tap's delay
    void readPitched (Tap& t, float d, float ratio, float& l, float& r)
    {
        const float w = pitchWin;
        t.pitch += 1.0f - ratio;
        while (t.pitch < 0.0f) t.pitch += w;
        while (t.pitch >= w) t.pitch -= w;
        l = r = 0.0f;
        for (int h = 0; h < 2; ++h)
        {
            float ph = t.pitch + (h ? 0.5f * w : 0.0f);
            if (ph >= w) ph -= w;
            const float g = std::sin (kPi * ph / w);
            l += line[0].tap (d + ph) * g * g;
            r += line[1].tap (d + ph) * g * g;
        }
    }

    float fs = 48000.0f, pitchWin = 3600.0f;
    std::array<DelayLine, 2> line;
    std::array<Tap, kTaps> taps;
    OnePole fbLp[2], fbHp[2];
    Smoother fbSmooth;
};
} // namespace spk
