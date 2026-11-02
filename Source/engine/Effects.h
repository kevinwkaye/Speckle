#pragma once

#include "Grains.h"
#include <array>

// The eleven effects, four variations each. Every effect is a scheduler that decides
// when to fire grains, what part of the past they read and how they're shaped.
namespace spk
{
enum Effect
{
    Mosaic = 0, Seq, Glide,          // Micro Loop
    Haze, Tunnel, Strum,             // Granules
    Blocks, Interrupt, Arp,          // Glitch
    Pattern, Warp,                   // Multi Delay
    kNumEffects
};

inline bool isDelayEffect (int e) { return e == Pattern || e == Warp; }

struct FxParams
{
    int effect = Haze, variant = 0;
    float activity = 0.5f, repeats = 0.5f, shape = 0.5f;
    bool reverse = false;
    float stepSamples = 12000.0f;   // the Time subdivision
    float beatSamples = 24000.0f;
};

struct OnsetRing
{
    static constexpr int kSize = 8;
    int64_t pos[kSize] = {};
    float amp[kSize] = {};
    int count = 0, head = 0;
    int64_t latest = -1;
    void add (int64_t p, float a) { pos[head] = p; amp[head] = a; head = (head + 1) % kSize; count = std::min (count + 1, kSize); latest = p; }
    // 0 = most recent
    int64_t get (int back) const { return pos[(head - 1 - back + kSize * 4) % kSize]; }
    void clear() { count = 0; head = 0; latest = -1; }
};

class EffectBank
{
public:
    void prepare (float sampleRate)
    {
        fs = sampleRate;
        pool.prepare (fs);
        margin = (int) (0.004f * fs);
        reset();
    }

    void reset()
    {
        for (auto& v : loops) v = LoopVoice();
        seq = SeqState(); haze = HazeState(); tun = TunnelState(); strum = StrumState();
        blocks = BlocksState(); intr = InterruptState(); arp = ArpState();
        clock = 0;
        lastEffect = -1;
    }

    void clearGrains() { pool.clear(); }

    // Called once per sub-block, after the new audio has gone into the history.
    void schedule (const History& h, const OnsetRing& onsets, const FxParams& p)
    {
        hist = &h;
        now = h.writePos();
        par = p;
        if (p.effect != lastEffect) { enterEffect (p.effect); lastEffect = p.effect; }
        if (onsets.latest != lastOnsetSeen) { newOnset = onsets.latest >= 0; lastOnsetSeen = onsets.latest; }
        else newOnset = false;
        ring = &onsets;

        switch (p.effect)
        {
            case Mosaic: doMosaic(); break;
            case Seq: doSeq(); break;
            case Glide: doGlide(); break;
            case Haze: doHaze(); break;
            case Tunnel: doTunnel(); break;
            case Strum: doStrum(); break;
            case Blocks: doBlocks(); break;
            case Interrupt: doInterrupt(); break;
            case Arp: doArp(); break;
            default: break;
        }
    }

    void render (float* l, float* r, int n) { pool.process (l, r, n); }

    // Interrupt passes the live signal through between its glitches
    float passTarget() const { return lastEffect == Interrupt ? (intr.active ? 0.0f : 1.0f) : 0.0f; }

    // Haze and Tunnel feed a little of their output back into the history
    float feedback() const
    {
        if (lastEffect == Haze) return 0.5f * std::pow (par.repeats, 1.2f);
        return 0.0f;
    }

    int grainCount() const { return pool.count(); }

private:
    // ------------------------------------------------------------------ helpers
    float rnd() { return rng.uni(); }
    float rndPan (float spread) { return (rng.next()) * spread; }
    float cents (float c) { return std::exp2 (c / 1200.0f); }

    // Place a grain reading the audio that started `back` samples ago, keeping every
    // read inside the past whether it plays forwards, faster or backwards.
    void place (GrainSpec& g, double back, bool allowReverse = true)
    {
        const float v = std::max (std::fabs (g.speedA), std::fabs (g.speedB));
        double s = (double) now - back;
        const double room = (double) now - margin;
        const bool rev = allowReverse && par.reverse;
        if (rev)
        {
            s = std::min (s, room - v * g.length);
            g.start = s + v * g.length;
            g.speedA = -std::fabs (g.speedA);
            g.speedB = -std::fabs (g.speedB);
        }
        else
        {
            s = std::min (s, room - std::max (0.0f, v - 1.0f) * g.length);
            g.start = s;
        }
        const double oldest = (double) now - (double) hist->capacity() + 8.0;
        if (g.start - v * g.length < oldest) g.start = oldest + v * g.length;
        g.src = hist;
    }

    // A segment already fully in the past, replayed (reversed when Rev is on)
    void placeSegment (GrainSpec& g, double segStart)
    {
        const float v = std::max (std::fabs (g.speedA), std::fabs (g.speedB));
        g.src = hist;
        const double room = (double) now - margin;
        segStart = std::min (segStart, room - 16.0);
        // never let a read overtake the incoming audio: shorten the grain instead
        int maxLen = g.length;
        if (par.reverse) maxLen = (int) ((room - segStart) / v);
        else if (v > 1.0f) maxLen = (int) ((room - segStart) / (v - 1.0f));
        if (maxLen < g.length)
        {
            const float aS = g.attack * g.length, rS = g.release * g.length;
            g.length = std::max (32, maxLen);
            g.attack = std::min (0.5f, aS / g.length);
            g.release = std::min (0.5f, rS / g.length);
        }
        if (par.reverse)
        {
            g.start = segStart + v * g.length;
            g.speedA = -std::fabs (g.speedA);
            g.speedB = -std::fabs (g.speedB);
        }
        else g.start = segStart;
    }

    // rough level of a stretch of the past, to avoid looping silence
    float energy (double start, int len) const
    {
        if (start + len > (double) now) len = (int) std::max (0.0, (double) now - start);
        if (len < 64) return 0.0f;
        float sum = 0.0f;
        for (int k = 0; k < 96; ++k) sum += std::fabs (hist->readMono (History::Full, start + (double) k * len / 96.0));
        return sum / 96.0f;
    }
    static constexpr float kQuiet = 0.004f;

    float stepLen() const { return clampf (par.stepSamples, 0.03f * fs, 6.0f * fs); }
    int samples (float seconds) const { return (int) (seconds * fs); }
    // how many repeats Repeats allows; huge at the top of the knob
    float repeatCount (float lo, float hi) const { return par.repeats > 0.97f ? 1.0e9f : lo + (hi - lo) * par.repeats * par.repeats; }
    float decayPerRepeat (float lo, float hi) const { return par.repeats > 0.97f ? 1.0f : std::pow (0.05f, 1.0f / repeatCount (lo, hi)); }

    void enterEffect (int e)
    {
        for (auto& v : loops) v = LoopVoice();
        seq = SeqState(); haze = HazeState(); tun = TunnelState(); strum = StrumState();
        blocks = BlocksState(); intr = InterruptState(); arp = ArpState();
        (void) e;
    }

    // ================================================================== MOSAIC
    struct LoopVoice
    {
        bool on = false;
        double seg = 0.0;
        int segLen = 0;
        float speed = 1.0f, gain = 1.0f, pan = 0.0f, glidePhase = 0.0f;
        int64_t next = -1;
        float reps = 0.0f;
    };
    std::array<LoopVoice, 6> loops;

    void doMosaic()
    {
        static const float speeds[4][6] = { { 1, 2, 1, 2, 1, 2 }, { 1, 0.5f, 1, 0.5f, 1, 0.5f }, { 2, 2, 2, 2, 2, 2 }, { 0.5f, 1, 2, 4, 1, 2 } };
        static const float mults[6] = { 2.0f, 3.0f, 1.0f, 4.0f, 1.5f, 6.0f };
        static const float pans[6] = { -0.45f, 0.45f, -0.2f, 0.2f, -0.7f, 0.7f };
        const int n = 1 + (int) std::lround (par.activity * 5.0f);
        const float decay = decayPerRepeat (2.0f, 24.0f);
        const float norm = 0.9f / std::sqrt ((float) n);
        const int fade = samples (0.012f);

        for (int i = 0; i < 6; ++i)
        {
            auto& v = loops[(size_t) i];
            if (i >= n) { v.on = false; continue; }
            if (! v.on)
            {
                v = LoopVoice();
                v.on = true;
                v.next = now + (int64_t) (i * stepLen() * 0.5f);
                v.gain = 0.0f; // capture on first emission
            }
            if (now < v.next) continue;

            v.speed = speeds[par.variant & 3][i];
            if (v.gain < 0.05f)
            {
                v.segLen = (int) clampf (stepLen() * mults[i], 0.05f * fs, 8.0f * fs);
                v.seg = (double) now - margin - v.segLen;
                v.gain = energy (v.seg, v.segLen) > kQuiet ? 1.0f : 0.0f;
            }
            const int period = std::max (64, (int) ((float) v.segLen / v.speed));
            GrainSpec g;
            g.speedA = g.speedB = v.speed;
            g.length = period + fade;
            g.attack = g.release = (float) fade / (float) g.length;
            g.equalPower = true;
            g.shape = par.shape;
            g.gain = v.gain * norm;
            g.pan = pans[i];
            placeSegment (g, v.seg);
            if (v.gain > 0.0f) pool.spawn (g);
            v.gain *= decay;
            v.next += period;
            if (v.next < now) v.next = now + period;
        }
    }

    // ===================================================================== SEQ
    struct SeqState { bool init = false, quiet = true; double window = 0; int64_t next = 0; int step = 0, barsLeft = 0; };
    SeqState seq;

    void doSeq()
    {
        const float L = stepLen();
        const int steps = 8;
        if (! seq.init) { seq.init = true; seq.next = now; seq.step = 0; seq.barsLeft = 0; seq.window = (double) now - margin - steps * L; }
        if (now < seq.next) return;
        seq.next += (int64_t) L;
        if (seq.next < now) seq.next = now + (int64_t) L;

        // a quiet window is swapped for fresh audio straight away
        if (seq.quiet || (seq.step == 0 && seq.barsLeft <= 0))
        {
            seq.window = (double) now - margin - steps * L;
            seq.barsLeft = par.repeats > 0.97f ? 1 << 30 : 1 + (int) std::lround (par.repeats * 7.0f);
            seq.quiet = energy (seq.window, (int) (steps * L)) < kQuiet;
        }
        if (seq.step == 0) --seq.barsLeft;
        if (seq.quiet) { seq.step = (seq.step + 1) % steps; return; }
        const int v = par.variant & 3;
        const float a = par.activity;
        const int fade = samples (0.004f);

        auto slice = [&] (float speed, int len, float gain, float pan, int which)
        {
            GrainSpec g;
            g.speedA = g.speedB = speed;
            g.length = len + fade;
            g.attack = g.release = (float) fade / (float) g.length;
            g.shape = par.shape;
            g.gain = gain;
            g.pan = pan;
            placeSegment (g, seq.window + which * L);
            return g;
        };

        const int which = (int) (rnd() * steps) % steps;
        const float pan = rndPan (0.35f);
        if (v == 0) // filtered and shuffled
        {
            auto g = slice (1.0f, (int) L, 0.9f, pan, which);
            if (rnd() < a) { g.filter = rnd() < 0.5f ? 1 : 2; g.cutoffA = g.cutoffB = 400.0f * std::pow (14.0f, rnd()); g.q = 0.8f + 3.0f * rnd(); }
            pool.spawn (g);
        }
        else if (v == 1) // bounce between normal and half speed, with a pad underneath at the top
        {
            const bool half = (seq.step & 1) && rnd() < 0.25f + 0.75f * a;
            pool.spawn (slice (half ? 0.5f : 1.0f, (int) L, 0.9f, pan, which));
            if (a > 0.92f && (seq.step % 4) == 0)
            {
                auto g = slice (0.5f, (int) (L * 4.0f), 0.32f, rndPan (0.6f), (int) (rnd() * 4));
                g.attack = 0.4f; g.release = 0.5f; g.equalPower = false; g.shape = 0.5f;
                pool.spawn (g);
            }
        }
        else if (v == 2) // overlapping layers with sweeps
        {
            const int layers = 1 + (int) std::lround (a * 2.0f);
            for (int k = 0; k < layers; ++k)
            {
                auto g = slice (1.0f, (int) (L * 2.0f), 0.62f / std::sqrt ((float) layers), rndPan (0.6f), (which + k * 3) % steps);
                g.attack = 0.3f; g.release = 0.5f;
                g.filter = 1;
                const bool up = rnd() < 0.5f;
                g.cutoffA = up ? 350.0f : 7000.0f; g.cutoffB = up ? 7000.0f : 350.0f; g.q = 1.6f;
                pool.spawn (g);
            }
        }
        else // interlocking layers, crushed, and a crushed sub-octave at the top
        {
            const int layers = 1 + (int) std::lround (a * 2.0f);
            for (int k = 0; k < layers; ++k)
            {
                auto g = slice (1.0f, (int) (L * (k == 0 ? 1.0f : 0.5f)), 0.8f / std::sqrt ((float) layers), rndPan (0.6f), (which + k * 5) % steps);
                if (k > 0) { g.crushBits = 7; g.crushHold = 3; g.filter = 1; g.cutoffA = g.cutoffB = 6500.0f; }
                pool.spawn (g);
            }
            if (a > 0.92f && (seq.step & 1) == 0)
            {
                auto g = slice (0.5f, (int) L, 0.45f, 0.0f, which);
                g.crushBits = 6; g.crushHold = 4; g.filter = 1; g.cutoffA = g.cutoffB = 3000.0f;
                pool.spawn (g);
            }
        }
        seq.step = (seq.step + 1) % steps;
    }

    // =================================================================== GLIDE
    float glidePhase = 0.0f;

    void doGlide()
    {
        static const float ranges[4][2] = { { 0.5f, 1.0f }, { 2.0f, 0.5f }, { 1.0f, 2.0f }, { 0.5f, 2.0f } };
        const int v = par.variant & 3;
        const float hz = 0.04f + 2.5f * par.activity * par.activity;
        const int G = (int) clampf (stepLen() * 2.0f, 0.12f * fs, 2.0f * fs);
        const int hop = G / 2;
        const float decay = decayPerRepeat (4.0f, 48.0f);
        glidePhase += hz * (float) (now - lastGlideTime) / fs;
        glidePhase -= std::floor (glidePhase);
        lastGlideTime = now;

        for (int i = 0; i < 2; ++i)
        {
            auto& lv = loops[(size_t) i];
            if (! lv.on) { lv = LoopVoice(); lv.on = true; lv.next = now + i * hop / 2; lv.gain = 0.0f; }
            if (now < lv.next) continue;
            const float maxSpeed = std::max (ranges[v][0], ranges[v][1]);
            if (lv.gain < 0.05f)
            {
                lv.segLen = G;
                lv.seg = (double) now - margin - maxSpeed * G - i * stepLen();
                lv.gain = energy (lv.seg, (int) (maxSpeed * G)) > kQuiet ? 1.0f : 0.0f;
            }
            GrainSpec g;
            g.speedA = ranges[v][0];
            g.speedB = ranges[v][1];
            g.glideHz = hz;
            g.glidePhase = glidePhase + (v == 3 && i == 1 ? 0.5f : 0.0f) + (v != 3 ? 0.13f * i : 0.0f);
            g.glideShape = par.shape;
            g.length = G;
            g.attack = g.release = 0.5f;
            g.gain = 0.72f * lv.gain;
            g.pan = i == 0 ? -0.4f : 0.4f;
            placeSegment (g, lv.seg);
            if (lv.gain > 0.0f) pool.spawn (g);
            lv.gain *= decay;
            lv.next += hop;
            if (lv.next < now) lv.next = now + hop;
        }
    }
    int64_t lastGlideTime = 0;

    // ==================================================================== HAZE
    struct HazeState { double acc = 0.0, head = 0.0; };
    HazeState haze;

    void doHaze()
    {
        const int v = par.variant & 3;
        const float a = par.activity;
        float rate, lenLo, lenHi;
        switch (v)
        {
            case 0: rate = 18.0f + 70.0f * a; lenLo = 0.035f; lenHi = 0.09f; break;
            case 1: rate = 25.0f + 140.0f * a; lenLo = 0.08f; lenHi = 0.32f; break;
            default: rate = 20.0f + 90.0f * a; lenLo = 0.06f; lenHi = 0.22f; break;
        }
        const float spread = (v == 0 ? 0.05f + 0.35f * a : 0.12f + 1.6f * a) * (1.0f + par.repeats);
        const float meanLen = 0.5f * (lenLo + lenHi);
        const float norm = 0.95f / std::sqrt (std::max (1.0f, rate * meanLen * 0.5f));

        // A stretches time: a slow read head trails the input and jumps back to catch up
        haze.head += 0.3 * block;
        if (haze.head < (double) now - (0.25 + 0.6 * a) * fs || haze.head > (double) now - margin) haze.head = (double) now - margin - 0.02 * fs;

        haze.acc += rate * (double) block / fs;
        int spawnNow = (int) haze.acc;
        haze.acc -= spawnNow;
        spawnNow = std::min (spawnNow, 12);
        for (int k = 0; k < spawnNow; ++k)
        {
            GrainSpec g;
            float sp = 1.0f;
            if (v == 2 && rnd() < 0.5f) sp = 2.0f;
            if (v == 3 && rnd() < 0.5f) sp = 0.5f;
            sp *= cents ((rng.next()) * (v == 0 ? 4.0f : 9.0f));
            g.speedA = g.speedB = sp;
            g.length = samples (lenLo + (lenHi - lenLo) * rnd());
            g.attack = g.release = 0.5f;
            g.shape = par.shape;
            g.gain = norm * (0.7f + 0.3f * rnd());
            g.pan = rndPan (0.85f);
            if (v == 0) place (g, (double) now - haze.head + spread * fs * 0.3f * rnd());
            else place (g, margin + spread * fs * rnd());
            pool.spawn (g);
        }
    }

    // ================================================================== TUNNEL
    struct TunnelState
    {
        bool captured = false;
        double seg = 0.0;
        int64_t captureAt = -1, onsetTime = 0, next = 0;
        float gain = 0.0f, filterWalk[3] = { 0.3f, 0.55f, 0.8f };
        int voice = 0;
    };
    TunnelState tun;

    void doTunnel()
    {
        const int v = par.variant & 3;
        const float a = par.activity;
        const float base = clampf (stepLen() * 0.5f, 0.06f * fs, 0.3f * fs);

        if (newOnset) tun.captureAt = ring->latest + samples (0.04f);
        if (! tun.captured && tun.captureAt < 0) tun.captureAt = now; // start droning straight away
        float Lg = base;
        const float t = (float) now / fs;
        if (v == 0) Lg = base * (1.0f + 0.65f * a * std::sin (kTwoPi * 0.17f * t));
        if (v == 3)
        {
            const float since = (float) (now - tun.onsetTime) / fs;
            Lg = base * (1.0f - 0.8f * a * (1.0f - std::exp (-since / 1.2f)));
        }
        Lg = std::max (0.025f * fs, Lg);

        if (tun.captureAt >= 0 && now >= tun.captureAt + (int64_t) (base * 1.5f) + margin)
        {
            tun.seg = (double) tun.captureAt;
            tun.captured = true;
            tun.gain = 1.0f;
            tun.onsetTime = tun.captureAt;
            tun.captureAt = -1;
        }
        if (! tun.captured || now < tun.next) return;

        // decay: Repeats sets how long the drone takes to fade
        const float hop = Lg * 0.25f;
        const float t60 = par.repeats > 0.97f ? 1.0e9f : 0.4f + 22.0f * par.repeats * par.repeats;
        tun.gain *= std::pow (0.001f, (hop / fs) / t60);
        if (tun.gain < 0.003f) return;
        tun.next = now + (int64_t) hop;

        const int voices = v == 1 ? 3 : 2;
        for (int k = 0; k < voices; ++k)
        {
            GrainSpec g;
            const float sp = (v == 1 && k == 2) ? 0.5f : 1.0f;
            g.speedA = g.speedB = sp;
            g.length = (int) Lg;
            g.attack = g.release = 0.5f;
            g.gain = tun.gain * 0.36f;
            g.pan = k == 0 ? -0.55f : (k == 1 ? 0.55f : 0.0f);
            g.shape = 0.5f + (par.shape - 0.5f) * 0.4f;
            const double jitter = (rnd() - 0.5f) * 0.004f * fs + k * 0.003f * fs;
            if (v == 1)
            {
                g.filter = 1;
                const float sweep = 0.5f + 0.5f * std::sin (kTwoPi * 0.09f * t + k);
                g.cutoffA = g.cutoffB = 250.0f + (300.0f + 7000.0f * a) * sweep * sweep;
                g.q = 1.4f;
            }
            else if (v == 2)
            {
                auto& w = tun.filterWalk[k];
                w = clampf (w + rng.next() * 0.01f, 0.05f, 0.95f);
                g.filter = 2;
                g.cutoffA = g.cutoffB = 250.0f * std::pow (14.0f, w);
                g.q = 1.5f + 9.0f * a;
            }
            placeSegment (g, tun.seg + jitter + 0.0005f * fs);
            pool.spawn (g);
        }
    }

    // =================================================================== STRUM
    struct StrumState { int64_t next = 0; float gain = 0.0f; int chain = 0, beat = 0; double copyAcc = 0; };
    StrumState strum;

    int snippetLen() const { return (int) clampf (stepLen() * 0.9f, 0.06f * fs, 0.4f * fs); }
    bool onsetReady (int back) const { return ring->count > back && now - ring->get (back) > snippetLen() * 2 + margin; }

    GrainSpec note (int back, float speed, int len, float gain, float pan)
    {
        GrainSpec g;
        g.speedA = g.speedB = speed;
        g.length = len;
        g.attack = std::min (0.5f, samples (0.003f) / (float) len);
        g.release = 0.45f;
        g.shape = par.shape;
        g.gain = gain;
        g.pan = pan;
        placeSegment (g, (double) ring->get (back) - samples (0.002f));
        return g;
    }

    void doStrum()
    {
        const int v = par.variant & 3;
        const float a = par.activity;
        if (newOnset) { strum.gain = 1.0f; strum.chain = 0; }
        if (! onsetReady (0)) return;
        const int S = snippetLen();

        if (v == 1) // overlapping copies of the last note, drifting into phase
        {
            const float rate = 8.0f + 60.0f * a;
            strum.copyAcc += rate * block / fs;
            const float fadeT = par.repeats > 0.97f ? 1.0e9f : 0.3f + 10.0f * par.repeats * par.repeats;
            strum.gain *= std::pow (0.001f, (block / fs) / fadeT);
            while (strum.copyAcc >= 1.0)
            {
                strum.copyAcc -= 1.0;
                if (strum.gain < 0.004f) continue;
                auto g = note (0, cents (rng.next() * 14.0f), S, strum.gain * 0.9f / std::sqrt (1.0f + rate * S / fs), rndPan (0.8f));
                pool.spawn (g);
            }
            return;
        }

        const int div = v == 0 ? 1 + (int) std::lround (a * 3.0f) : 1;
        const int64_t period = std::max<int64_t> (64, (int64_t) (stepLen() / div));
        if (now < strum.next) return;
        strum.next = now + period;
        if (strum.gain < 0.01f) return;

        if (v == 0) // the latest note, again and again
        {
            pool.spawn (note (0, 1.0f, std::min (S, (int) period * 2), strum.gain * 0.9f, (strum.beat & 1) ? 0.3f : -0.3f));
            strum.gain *= decayPerRepeat (2.0f, 40.0f);
        }
        else // cascading chains through recent notes; D adds double speed grains
        {
            const int m = std::min (ring->count, 2 + (int) std::lround (a * 6.0f));
            int back = strum.chain % std::max (1, m);
            if (! onsetReady (back)) back = 0;
            pool.spawn (note (back, 1.0f, S, strum.gain * 0.85f, rndPan (0.6f)));
            if (v == 3 && (strum.beat & 1)) pool.spawn (note (back, 2.0f, S / 2, strum.gain * 0.5f, rndPan (0.8f)));
            if (++strum.chain >= m) { strum.chain = 0; strum.gain *= decayPerRepeat (1.0f, 20.0f); }
        }
        ++strum.beat;
    }

    // ================================================================== BLOCKS
    struct BlocksState { int stepsLeft = 0, step = 0; double window = 0; int64_t next = 0; };
    BlocksState blocks;

    void doBlocks()
    {
        const int v = par.variant & 3;
        const float a = par.activity;
        const float L = stepLen() * 0.5f;
        if (newOnset && blocks.stepsLeft <= 0)
        {
            blocks.stepsLeft = par.repeats > 0.97f ? 1 << 30 : 2 + (int) std::lround (par.repeats * par.repeats * 30.0f);
            blocks.step = 0;
            blocks.window = (double) now - margin - 8.0 * L;
            blocks.next = now;
        }
        if (blocks.stepsLeft <= 0 || now < blocks.next) return;
        blocks.next = now + (int64_t) L;
        --blocks.stepsLeft;
        if (par.repeats > 0.97f && (blocks.step % 16) == 15) blocks.window = (double) now - margin - 8.0 * L;

        // little variation: a predictable backwards walk. More: random slices and changes
        const int which = rnd() < a ? (int) (rnd() * 8) % 8 : 7 - (blocks.step % 8);
        const int fade = samples (0.003f);
        GrainSpec g;
        g.length = (int) L + fade;
        g.attack = g.release = (float) fade / g.length;
        g.shape = par.shape;
        g.gain = 0.9f;
        g.pan = rndPan (0.5f);
        float sp = 1.0f;

        if (v == 0 && rnd() < a * 0.5f && onsetReady (0)) // a run of recent notes
        {
            const int runLen = (int) (L / 4);
            for (int k = 0; k < 4; ++k)
            {
                const int back = ring->count > 1 ? k % std::min (ring->count, 4) : 0;
                if (! onsetReady (back)) continue;
                runQueue[k] = note (back, 1.0f, runLen, 0.55f, rndPan (0.6f));
            }
            runQueued = 4; runEvery = runLen; runNext = now;
        }
        else
        {
            if ((v == 1 || v == 3) && rnd() < 0.3f + 0.7f * a)
            {
                static const float steps[5] = { 0.5f, 2.0f, 1.4983f, 0.75f, 1.0f };
                sp = steps[(int) (rnd() * 5) % 5];
            }
            g.speedA = g.speedB = sp;
            if (v == 2)
            {
                g.length = (int) (L * 2.0f);
                g.attack = 0.25f; g.release = 0.6f;
                g.filter = 1; g.cutoffA = 900.0f + 2600.0f * rnd(); g.cutoffB = g.cutoffA * 0.5f; g.q = 0.9f;
                g.gain = 0.65f;
            }
            if (v == 3) { g.crushBits = 6 + (int) ((1.0f - a) * 3.0f); g.crushHold = 2 + (int) (a * 4.0f); }
            placeSegment (g, blocks.window + which * L);
            pool.spawn (g);
        }
        ++blocks.step;
        fireRuns();
    }

    GrainSpec runQueue[4];
    int runQueued = 0, runEvery = 0;
    int64_t runNext = 0;
    void fireRuns()
    {
        while (runQueued > 0 && now >= runNext)
        {
            pool.spawn (runQueue[4 - runQueued]);
            --runQueued;
            runNext += runEvery;
        }
    }

    // =============================================================== INTERRUPT
    struct InterruptState { bool active = false; int64_t next = 0, end = 0, sub = 0; double window = 0; int subLen = 0; int echoes = 0; };
    InterruptState intr;

    void doInterrupt()
    {
        const int v = par.variant & 3;
        const float a = par.activity;
        const float L = stepLen();
        if (intr.next == 0) intr.next = now + (int64_t) (L * 2.0f);

        if (! intr.active && now >= intr.next)
        {
            intr.active = true;
            const int lenSteps = rnd() < 0.5f ? 1 : 2;
            intr.end = now + (int64_t) (L * lenSteps);
            intr.window = (double) now - margin - 2.0 * L;
            intr.subLen = (int) (L / (rnd() < 0.5f ? 4.0f : (a > 0.6f ? 8.0f : 2.0f)));
            intr.sub = now;
        }
        if (intr.active)
        {
            if (now >= intr.end)
            {
                intr.active = false;
                // Repeats: how often glitches come, from every four bars to every step
                const float steps = 32.0f * std::pow (1.0f / 32.0f, par.repeats);
                intr.next = now + (int64_t) (L * steps * (0.6f + 0.8f * rnd()));
            }
            else if (now >= intr.sub)
            {
                intr.sub += intr.subLen;
                const int fade = samples (0.002f);
                GrainSpec g;
                g.length = intr.subLen + fade;
                g.attack = g.release = (float) fade / g.length;
                g.shape = par.shape;
                g.gain = 0.95f;
                g.pan = rndPan (0.3f + 0.4f * a);
                float sp = 1.0f;
                const double off = rnd() < a ? rnd() * std::max (0.0, 2.0 * L - 4.0 * intr.subLen) : 0.0;
                if (v == 1 && rnd() < 0.4f + 0.6f * a) { static const float s[4] = { 2.0f, 0.5f, 1.4983f, 0.75f }; sp = s[(int) (rnd() * 4) % 4]; }
                if (v == 3)
                {
                    if (rnd() < a) sp = rnd() < 0.5f ? 4.0f : 0.25f;
                    g.crushBits = 5 + (int) (rnd() * 3); g.crushHold = 2 + (int) (rnd() * 3);
                }
                g.speedA = g.speedB = sp;
                if (v == 2) { g.filter = 1; g.cutoffA = 9000.0f; g.cutoffB = 300.0f + 1200.0f * rnd(); g.q = 2.0f; }
                GrainSpec seg = g;
                const bool flip = v == 3 && rnd() < a * 0.5f;
                if (flip) par.reverse = ! par.reverse; // drastic: this slice plays the other way
                placeSegment (seg, intr.window + off);
                if (flip) par.reverse = ! par.reverse;
                pool.spawn (seg);
                if (v == 2) // the glitch echoes on, dotted sixteenth apart
                    for (int e = 1; e <= 2; ++e)
                    {
                        echo[e - 1] = seg;
                        echo[e - 1].gain *= std::pow (0.5f, (float) e);
                        echoAt[e - 1] = now + (int64_t) (e * L * 0.75f);
                    }
            }
        }
        for (int e = 0; e < 2; ++e)
            if (echoAt[e] > 0 && now >= echoAt[e]) { pool.spawn (echo[e]); echoAt[e] = 0; }
    }
    GrainSpec echo[2];
    int64_t echoAt[2] = { 0, 0 };

    // ===================================================================== ARP
    struct ArpState { int64_t next = 0; int step = 0; float cut[8] = {}; bool cutSet = false; };
    ArpState arp;

    void doArp()
    {
        const int v = par.variant & 3;
        const float a = par.activity;
        if (! onsetReady (0)) return;
        const float L = stepLen();
        if (now < arp.next) return;
        arp.next = now + (int64_t) L;
        if (! arp.cutSet) { for (auto& c : arp.cut) c = 300.0f * std::pow (20.0f, rnd()); arp.cutSet = true; }

        const int steps = 2 + (int) std::lround (a * 6.0f);
        const int pos = arp.step % steps;
        const int avail = std::min (ring->count, 4);
        // up and down through the recent notes
        int idx = pos % (2 * std::max (1, avail - 1));
        if (idx >= avail) idx = 2 * (avail - 1) - idx;
        int back = std::max (0, avail - 1 - idx);
        if (! onsetReady (back)) back = 0;

        static const float ladder[8] = { 1.0f, 1.4983f, 2.0f, 1.0f, 0.5f, 1.4983f, 2.0f, 2.9966f };
        const float sp = v == 1 ? ladder[pos % 8] : 1.0f;
        const int len = (int) clampf (L * (0.5f + 2.5f * par.repeats), 0.04f * fs, 3.0f * fs);
        // keeps going while you play, then fades over a few bars once you stop
        const float idle = (float) (now - ring->latest) / std::max (1.0f, par.beatSamples) - 4.0f;
        const float fade = par.repeats > 0.97f ? 1.0f : std::exp (-std::max (0.0f, idle) / 4.0f);
        if (fade < 0.01f) return;
        auto g = note (back, sp, len, 0.7f * fade, ((pos & 1) ? 0.45f : -0.45f));
        if (v == 2) { g.filter = rnd() < 0.5f ? 1 : 2; g.cutoffA = g.cutoffB = arp.cut[pos % 8]; g.q = 1.2f + 2.5f * rnd(); arp.cut[pos % 8] = 300.0f * std::pow (20.0f, rnd()); }
        if (v == 3) { g.crushBits = 6; g.crushHold = 3; g.filter = 1; g.cutoffA = g.cutoffB = 7000.0f; }
        pool.spawn (g);
        ++arp.step;
    }

    // ------------------------------------------------------------------ state
public:
    static constexpr int block = 32;

private:
    float fs = 48000.0f;
    int margin = 192;
    GrainPool pool;
    Rng rng;
    const History* hist = nullptr;
    const OnsetRing* ring = nullptr;
    FxParams par;
    int64_t now = 0, clock = 0, lastOnsetSeen = -1;
    bool newOnset = false;
    int lastEffect = -1;
};
} // namespace spk
