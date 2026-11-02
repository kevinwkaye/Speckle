#pragma once

#include "DspUtil.h"
#include <array>

// Stereo reverb: an eight channel diffuser into a modulated feedback delay network.
// Four rooms, matching the pedal's Bright Room, Dark Medium, Large Hall and Ambient.
namespace spk
{
class Reverb
{
public:
    static constexpr int N = 8;
    enum Mode { BrightRoom = 0, DarkMedium, LargeHall, Ambient };

    void prepare (float sampleRate)
    {
        fs = sampleRate;
        for (auto& s : steps)
            for (auto& d : s.d) d.init (nextPow2 ((int) (0.12f * fs)));
        for (auto& d : fdn) d.init (nextPow2 ((int) (0.5f * fs)));
        pre.init (nextPow2 ((int) (0.12f * fs)));
        mode = -1;
        pendingMode = BrightRoom;
        fade = 0.0f;
        fadeStep = 1.0f / (0.04f * fs);
        configure (BrightRoom);
    }

    void setMode (int m) { pendingMode = m; }

    void process (float inL, float inR, float& outL, float& outR)
    {
        // Changing rooms fades the old tail out, rebuilds, and fades back in
        if (pendingMode != mode)
        {
            fade -= fadeStep;
            if (fade <= 0.0f) { configure (pendingMode); fade = 0.0f; }
        }
        else if (fade < 1.0f)
            fade = std::min (1.0f, fade + fadeStep);

        pre.push (0.5f * (inL + inR));
        const float mid = pre.tap (preDelay);
        // keep a little of the stereo image going in
        const float side = 0.5f * (inL - inR);
        pre2L = side;

        float x[N];
        for (int c = 0; c < N; ++c)
            x[c] = (mid + ((c & 1) ? -pre2L : pre2L)) * inGain;

        // diffusion: short random delays, mixed and polarity flipped, four times over
        for (auto& s : steps)
        {
            for (int c = 0; c < N; ++c)
            {
                s.d[c].push (x[c]);
                x[c] = s.d[c].at (s.len[c]) * (s.flip[c] ? -1.0f : 1.0f);
            }
            hadamard (x);
        }
        float early[2] = { 0, 0 };
        for (int c = 0; c < N; ++c) early[c & 1] += x[c];

        // feedback network
        float y[N];
        for (int c = 0; c < N; ++c)
        {
            lfoPh[c] += lfoInc[c];
            if (lfoPh[c] > 1.0f) lfoPh[c] -= 1.0f;
            const float mod = modDepth * std::sin (kTwoPi * lfoPh[c]);
            y[c] = fdn[c].tap (len[c] + mod);
        }
        float late[2] = { 0, 0 };
        for (int c = 0; c < N; ++c) late[c & 1] += y[c] * ((c & 2) ? -1.0f : 1.0f);

        // Householder mix
        float sum = 0.0f;
        for (int c = 0; c < N; ++c) sum += y[c];
        sum *= 2.0f / N;
        for (int c = 0; c < N; ++c)
        {
            float v = (y[c] - sum) * gain[c];
            v = damp[c].process (v);
            v -= lowCut[c].process (v);
            fdn[c].push (x[c] + v);
        }

        const float g = outGain * fade;
        outL = (late[0] + early[0] * earlyMix) * g;
        outR = (late[1] + early[1] * earlyMix) * g;
    }

    void clear()
    {
        for (auto& s : steps) for (auto& d : s.d) d.clear();
        for (auto& d : fdn) d.clear();
        pre.clear();
        for (int c = 0; c < N; ++c) { damp[c].reset(); lowCut[c].reset(); }
    }

private:
    static void hadamard (float* x)
    {
        for (int h = 1; h < N; h <<= 1)
            for (int i = 0; i < N; i += h << 1)
                for (int j = i; j < i + h; ++j)
                {
                    const float a = x[j], b = x[j + h];
                    x[j] = a + b; x[j + h] = a - b;
                }
        const float s = 0.35355339f; // 1/sqrt(8)
        for (int i = 0; i < N; ++i) x[i] *= s;
    }

    void configure (int m)
    {
        mode = m;
        struct Room { float size, rt, dampHz, preMs, modMs, early, out; };
        static const Room rooms[4] = {
            { 0.38f, 0.9f,  11000.0f, 4.0f,  0.25f, 0.55f, 0.6f },   // bright room
            { 0.62f, 2.2f,  3200.0f,  14.0f, 0.45f, 0.35f, 0.62f },  // dark medium
            { 1.00f, 4.2f,  7500.0f,  24.0f, 0.7f,  0.25f, 0.5f },   // large hall
            { 1.35f, 14.0f, 5200.0f,  32.0f, 1.3f,  0.15f, 0.36f },  // ambient wash
        };
        const Room& r = rooms[clampi (m)];
        preDelay = std::max (2.0f, r.preMs * 0.001f * fs);
        modDepth = r.modMs * 0.001f * fs;
        earlyMix = r.early;
        outGain = r.out;
        inGain = 0.6f;

        Rng rng;
        rng.s = 0x51A7C0DEu;
        float stepMs = 6.0f * r.size;
        for (auto& s : steps)
        {
            for (int c = 0; c < N; ++c)
            {
                const float lo = stepMs * (float) c / N, hi = stepMs * (float) (c + 1) / N;
                s.len[c] = std::max (1, (int) ((lo + (hi - lo) * rng.uni()) * 0.001f * fs));
                s.flip[c] = rng.uni() > 0.5f;
            }
            stepMs *= 1.7f;
        }
        for (int c = 0; c < N; ++c)
        {
            // spread the delays evenly in log time between 70 and 160 ms (scaled by room size)
            const float ms = 70.0f * std::pow (160.0f / 70.0f, ((float) c + 0.5f * rng.uni()) / N) * r.size;
            len[c] = ms * 0.001f * fs;
            gain[c] = std::pow (10.0f, -3.0f * (len[c] / fs) / r.rt);
            damp[c].setCutoff (r.dampHz, fs);
            lowCut[c].setCutoff (70.0f, fs);
            lfoInc[c] = (0.07f + 0.11f * (float) c + 0.05f * rng.uni()) / fs;
            lfoPh[c] = rng.uni();
        }
        clear();
    }

    static int clampi (int m) { return m < 0 ? 0 : (m > 3 ? 3 : m); }

    struct Step { std::array<DelayLine, N> d; int len[N] = {}; bool flip[N] = {}; };
    std::array<Step, 4> steps;
    std::array<DelayLine, N> fdn;
    DelayLine pre;
    float fs = 48000.0f, preDelay = 100.0f, modDepth = 10.0f, earlyMix = 0.3f, outGain = 1.0f, inGain = 0.6f, pre2L = 0.0f;
    float len[N] = {}, gain[N] = {}, lfoInc[N] = {}, lfoPh[N] = {};
    OnePole damp[N], lowCut[N];
    int mode = -1, pendingMode = 0;
    float fade = 1.0f, fadeStep = 1.0f / 2000.0f;
};
} // namespace spk
