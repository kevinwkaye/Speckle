#pragma once

#include "History.h"
#include <array>

// The grain engine at the heart of Speckle. Every micro loop, cloud, glitch and
// arpeggio note is a grain: a short read from a History with its own speed, window,
// pan, filter and optional bit crush.
namespace spk
{
// Shape knob: a volume contour laid over every grain.
// Left of centre the sound plucks and decays, centre is neutral, right swells in.
inline float contour (float t, float shape)
{
    if (shape < 0.47f)
    {
        const float k = (0.47f - shape) / 0.47f;
        return std::exp (-t * 7.0f * k);
    }
    if (shape > 0.53f)
    {
        const float k = (shape - 0.53f) / 0.47f;
        return 1.0f - k + k * std::pow (t, 1.3f);
    }
    return 1.0f;
}

struct GrainSpec
{
    const History* src = nullptr;
    double start = 0.0;       // absolute read position
    float speedA = 1.0f;      // playback rate, negative plays backwards
    float speedB = 1.0f;      // glide target (equal to speedA for a steady pitch)
    float glideHz = 0.0f;     // how fast the speed moves between A and B
    float glidePhase = 0.0f;  // 0..1 starting phase of the glide
    float glideShape = 0.5f;  // 0 ramp up, 0.5 smooth, 1 ramp down
    int length = 4800;        // in output samples
    float attack = 0.5f, release = 0.5f; // window fades as fractions of the length
    bool equalPower = false;  // for loops, where neighbours carry unrelated audio
    float shape = 0.5f;       // contour, see above
    float gain = 1.0f, pan = 0.0f;
    int filter = 0;           // 0 none, 1 low-pass, 2 band-pass
    float cutoffA = 20000.0f, cutoffB = 20000.0f, q = 0.707f;
    int crushBits = 0, crushHold = 1;
};

class GrainPool
{
public:
    static constexpr int kMax = 160;

    void prepare (float sampleRate) { fs = sampleRate; clear(); }
    void clear() { for (auto& g : grains) g.active = false; activeCount = 0; }
    int count() const { return activeCount; }

    bool spawn (const GrainSpec& s)
    {
        if (s.src == nullptr || s.length < 16) return false;
        Grain* slot = nullptr;
        for (auto& g : grains)
            if (! g.active) { slot = &g; break; }
        if (slot == nullptr) // steal the grain nearest its end
        {
            float best = -1.0f;
            for (auto& g : grains)
            {
                const float done = (float) g.age / (float) g.spec.length;
                if (done > best) { best = done; slot = &g; }
            }
        }
        Grain& g = *slot;
        g.spec = s;
        g.active = true;
        g.age = 0;
        g.pos = s.start;
        g.band = History::bandFor (std::max (std::fabs (s.speedA), std::fabs (s.speedB)));
        g.phase = s.glidePhase;
        g.glideInc = s.glideHz / fs;
        const float a = (clampf (s.pan, -1.0f, 1.0f) + 1.0f) * (kPi * 0.25f);
        g.gL = s.gain * std::cos (a) * 1.41421356f;
        g.gR = s.gain * std::sin (a) * 1.41421356f;
        g.f[0].reset(); g.f[1].reset();
        g.held[0] = g.held[1] = 0.0f;
        g.holdCount = 0;
        g.stealFade = 1.0f;
        // crushed grains get a soft low-pass so the grit stays musical
        if (g.spec.crushBits > 0 && g.spec.filter == 0) { g.spec.filter = 1; g.spec.cutoffA = g.spec.cutoffB = 7500.0f; g.spec.q = 0.707f; }
        if (g.spec.filter != 0) g.updateFilter (0.0f, fs);
        return true;
    }

    void process (float* outL, float* outR, int n)
    {
        activeCount = 0;
        for (auto& g : grains)
        {
            if (! g.active) continue;
            ++activeCount;
            g.render (outL, outR, n, fs);
        }
    }

private:
    struct Grain
    {
        GrainSpec spec;
        bool active = false;
        int age = 0, band = 0, holdCount = 0;
        double pos = 0.0;
        float phase = 0.0f, glideInc = 0.0f, gL = 1.0f, gR = 1.0f, stealFade = 1.0f;
        float held[2] = { 0, 0 };
        Svf f[2];

        void updateFilter (float t, float fs)
        {
            const float fc = spec.cutoffA * std::pow (spec.cutoffB / std::max (1.0f, spec.cutoffA), t);
            f[0].set (fc, spec.q, fs);
            f[1].set (fc, spec.q, fs);
        }

        float speedNow() const
        {
            if (spec.speedA == spec.speedB) return spec.speedA;
            // shape morphs the glide from a rising ramp through a smooth swing to a falling ramp
            const float ph = phase - std::floor (phase);
            const float smooth = 0.5f - 0.5f * std::cos (kTwoPi * ph);
            float u;
            if (spec.glideShape < 0.5f) u = lerpf (ph, smooth, spec.glideShape * 2.0f);
            else u = lerpf (smooth, 1.0f - ph, (spec.glideShape - 0.5f) * 2.0f);
            // move in pitch, not in rate, so glides sound even
            const float la = std::log2 (std::fabs (spec.speedA)), lb = std::log2 (std::fabs (spec.speedB));
            const float sign = spec.speedA < 0.0f ? -1.0f : 1.0f;
            return sign * std::exp2 (lerpf (la, lb, u));
        }

        float window (float t) const
        {
            float w = 1.0f;
            if (t < spec.attack)
            {
                const float x = t / std::max (1.0e-4f, spec.attack);
                w = spec.equalPower ? std::sin (x * kPi * 0.5f) : 0.5f - 0.5f * std::cos (x * kPi);
            }
            else if (t > 1.0f - spec.release)
            {
                const float x = (1.0f - t) / std::max (1.0e-4f, spec.release);
                w = spec.equalPower ? std::sin (x * kPi * 0.5f) : 0.5f - 0.5f * std::cos (x * kPi);
            }
            return w * contour (t, spec.shape);
        }

        void render (float* outL, float* outR, int n, float fs)
        {
            const float invLen = 1.0f / (float) spec.length;
            const bool glide = spec.speedA != spec.speedB;
            float speed = speedNow();
            for (int i = 0; i < n; ++i)
            {
                if (age >= spec.length) { active = false; return; }
                const float t = (float) age * invLen;
                if (glide && (age & 15) == 0) speed = speedNow();
                if (spec.filter != 0 && (age & 31) == 0 && spec.cutoffA != spec.cutoffB) updateFilter (t, fs);

                float l, r;
                spec.src->read (band, pos, l, r);

                if (spec.crushBits > 0)
                {
                    if (holdCount <= 0)
                    {
                        const float steps = (float) (1 << spec.crushBits);
                        held[0] = std::round (l * steps) / steps;
                        held[1] = std::round (r * steps) / steps;
                        holdCount = spec.crushHold;
                    }
                    --holdCount;
                    l = held[0]; r = held[1];
                }
                if (spec.filter == 1) { l = f[0].lp (l); r = f[1].lp (r); }
                else if (spec.filter == 2) { l = f[0].bpNorm (l) * 1.8f; r = f[1].bpNorm (r) * 1.8f; }

                const float w = window (t);
                outL[i] += l * w * gL;
                outR[i] += r * w * gR;

                pos += speed;
                phase += glideInc;
                ++age;
            }
        }
    };

    float fs = 48000.0f;
    std::array<Grain, kMax> grains;
    int activeCount = 0;
};
} // namespace spk
