#pragma once

#include "Detector.h"
#include "Effects.h"
#include "Looper.h"
#include "MultiDelay.h"
#include "Reverb.h"

// The whole Speckle signal path, free of JUCE:
// input -> (hold sampler, pre-FX loop) -> history -> effect -> pitch mod -> filter ->
// delay and reverb -> volume -> mix with dry -> (post-FX loop) -> output
namespace spk
{
struct EngineParams
{
    int effect = Haze, variant = 1;
    bool reverse = false;
    float activity = 0.5f, repeats = 0.5f, shape = 0.5f;
    float stepSamples = 12000.0f, beatSamples = 24000.0f;
    float filter = 1.0f, resonance = 0.2f;
    float mix = 0.5f, volumeDb = 0.0f;
    float space = 0.35f;
    int reverbMode = Reverb::LargeHall;
    float modDepth = 0.0f, modRateHz = 0.5f;
    int inputMode = InputGuitar;
    bool hold = false;
    bool bypass = false, trails = true;
    // looper
    float loopLevel = 0.8f, loopSpeed = 1.0f, fadeSeconds = 0.0f;
    int fadeMode = 0, recOrder = 0;
    bool loopPre = false, quantise = false, loopOnly = false, loopReverse = false;
};

class SpeckleEngine
{
public:
    enum Command { CmdRec = 1, CmdUndo = 2, CmdStop = 4, CmdErase = 8, CmdBurstDown = 16, CmdBurstUp = 32, CmdClearAll = 64 };

    // ---- read by the panel ----
    std::atomic<float> uiWet { 0.0f }, uiInput { 0.0f };
    std::atomic<int> uiGrains { 0 };
    std::atomic<bool> uiHeld { false }, uiOnset { false };

    void prepare (double sampleRate)
    {
        fs = (float) sampleRate;
        detector.prepare (fs);
        hist.prepare (fs, 16.0f);
        fx.prepare (fs);
        delay.prepare (fs);
        reverb.prepare (fs);
        looper.prepare (fs);
        onsets.clear();

        holdRing.init (nextPow2 ((int) (4.0f * fs)));
        holdRingR.init (holdRing.size());
        holdBuf[0].assign ((size_t) (2.5f * fs) + 16, 0.0f);
        holdBuf[1].assign ((size_t) (2.5f * fs) + 16, 0.0f);
        holdLen = 0; holdPos = 0; holdXf = (int) (0.03f * fs);
        wasHeld = false;

        const int modSize = nextPow2 ((int) (0.06f * fs));
        for (auto& d : modLine) d.init (modSize);
        for (auto& d : spaceLine) d.init (nextPow2 ((int) (2.6f * fs)));
        for (int c = 0; c < 2; ++c)
        {
            bassLp[c][0].lowpass (120.0f, 0.7071f, fs); bassLp[c][1].lowpass (120.0f, 0.7071f, fs);
            bassHp[c][0].highpass (120.0f, 0.7071f, fs); bassHp[c][1].highpass (120.0f, 0.7071f, fs);
            for (auto& b : bassLp[c]) b.reset();
            for (auto& b : bassHp[c]) b.reset();
            filt[c].reset();
            dc[c].prepare (fs); dc[c].reset();
            spaceLp[c].setCutoff (4800.0f, fs); spaceLp[c].reset();
        }
        for (auto* s : { &sMix, &sVol, &sSpace, &sModDepth, &sFilter, &sRes, &sPass, &sDelayOut, &sLoopOnly, &sHold, &sBypass, &sTrim })
            s->setTime (0.03f, fs);
        sFilter.setTime (0.05f, fs);
        sSpaceTime.setTime (0.4f, fs);
        modPhase = 0.0f;
        fbL.assign (EffectBank::block, 0.0f); fbR.assign (EffectBank::block, 0.0f);
        lastEffect = -1;
        lastReverbMode = -1;
    }

    void postCommand (int cmd) { commands.fetch_or (cmd); }

    void process (float* L, float* R, int n, const EngineParams& p)
    {
        // looper commands from the panel
        const int cmd = commands.exchange (0);
        if (cmd & CmdClearAll) { looper.erase(); }
        if (cmd & CmdBurstDown) looper.burstDown();
        if (cmd & CmdBurstUp) looper.burstUp();
        if (cmd & CmdRec) looper.pressRec();
        if (cmd & CmdUndo) looper.holdRec();
        if (cmd & CmdStop) looper.pressStop();
        if (cmd & CmdErase) looper.erase();

        if (p.reverbMode != lastReverbMode) { reverb.setMode (p.reverbMode); lastReverbMode = p.reverbMode; }
        detector.setMode (p.inputMode);
        detector.setSensitivity (p.inputMode == InputSynth ? 0.55f : 0.62f);

        for (int start = 0; start < n; start += EffectBank::block)
            processBlock (L + start, R + start, std::min (EffectBank::block, n - start), p);
    }

    // ---- presets: the loop travels with a saved preset ----
    Looper& getLooper() { return looper; }
    const Looper& getLooper() const { return looper; }
    float sampleRate() const { return fs; }

private:
    void processBlock (float* L, float* R, int n, const EngineParams& p)
    {
        float effL[EffectBank::block], effR[EffectBank::block];
        float wetL[EffectBank::block] = {}, wetR[EffectBank::block] = {};
        float lowL[EffectBank::block], lowR[EffectBank::block];
        float preL[EffectBank::block] = {}, preR[EffectBank::block] = {};
        float dryL[EffectBank::block], dryR[EffectBank::block];

        Looper::Settings ls;
        ls.order = p.recOrder; ls.quantise = p.quantise; ls.beatSamples = p.beatSamples;
        ls.reverse = p.loopReverse; ls.speed = p.loopSpeed; ls.fadeSeconds = p.fadeSeconds;
        ls.fadeMode = p.fadeMode; ls.level = p.loopLevel;

        for (int i = 0; i < n; ++i) { dryL[i] = L[i]; dryR[i] = R[i]; }

        // pre-FX loop: records the input and plays back into the effects
        if (p.loopPre) looper.process (dryL, dryR, preL, preR, n, ls);

        const bool delayFx = isDelayEffect (p.effect);
        const float bypassGate = p.bypass ? 0.0f : 1.0f;
        float level = 0.0f;
        for (int i = 0; i < n; ++i)
        {
            float l = dryL[i], r = dryR[i];
            if (p.inputMode == InputBass)
            {
                // everything under 120 Hz stays dry and tight
                lowL[i] = bassLp[0][1].process (bassLp[0][0].process (l));
                lowR[i] = bassLp[1][1].process (bassLp[1][0].process (r));
                l = bassHp[0][1].process (bassHp[0][0].process (l));
                r = bassHp[1][1].process (bassHp[1][0].process (r));
            }
            else lowL[i] = lowR[i] = 0.0f;

            // hold sampler: keep a few seconds of input, freeze the last beat on demand
            holdRing.push (l); holdRingR.push (r);
            const float h = sHold.next (p.hold ? 1.0f : 0.0f);
            if (p.hold && ! wasHeld) captureHold (p.beatSamples);
            wasHeld = p.hold;
            if (h > 1.0e-4f && holdLen > 0)
            {
                float hl, hr;
                readHold (hl, hr);
                const float a = std::sin (h * kPi * 0.5f), b = std::cos (h * kPi * 0.5f);
                l = l * b + hl * a; r = r * b + hr * a;
            }

            l = (l + preL[i]) * bypassGate;
            r = (r + preR[i]) * bypassGate;
            effL[i] = l; effR[i] = r;
            level = std::max (level, std::fabs (l) + std::fabs (r));

            const float on = detector.process (0.5f * (l + r));
            if (on > 0.0f) { onsets.add (hist.writePos(), on); uiOnset.store (true); }

            const float fb = fx.feedback();
            hist.push (softClip (l + fbL[(size_t) i] * fb), softClip (r + fbR[(size_t) i] * fb));
        }
        uiInput.store (level * 0.5f);

        // granular effects
        FxParams fp;
        fp.effect = p.effect; fp.variant = p.variant; fp.activity = p.activity; fp.repeats = p.repeats;
        fp.shape = p.shape; fp.reverse = p.reverse; fp.stepSamples = p.stepSamples; fp.beatSamples = p.beatSamples;
        if (p.effect != lastEffect) lastEffect = p.effect;
        if (! delayFx) fx.schedule (hist, onsets, fp);
        fx.render (wetL, wetR, n);
        uiGrains.store (fx.grainCount());

        // multi delay keeps running so its echoes ring out after switching away
        MultiDelay::Settings ds;
        ds.warp = p.effect == Warp; ds.variant = p.variant; ds.activity = p.activity; ds.repeats = p.repeats;
        ds.stepSamples = p.stepSamples; ds.reverse = p.reverse;
        // level matched so every effect sits about where your dry signal does
        static constexpr float trims[kNumEffects] = { 1.7f, 2.4f, 1.7f, 1.15f, 1.5f, 1.1f, 1.45f, 1.0f, 0.8f, 0.72f, 1.0f };
        float wetPeak = 0.0f;
        for (int i = 0; i < n; ++i)
        {
            const float dOut = sDelayOut.next (delayFx ? 1.0f : 0.0f);
            if (dOut > 1.0e-4f || delayActive)
            {
                // Shape: a volume contour on what goes into the echoes, from plucked to swelling
                const float since = onsets.latest < 0 ? 1.0f : clampf ((float) (hist.writePos() - n + i - onsets.latest) / (2.0f * p.beatSamples), 0.0f, 1.0f);
                const float c = delayFx ? contour (since, p.shape) : 0.0f;
                float dl, dr;
                delay.process (effL[i] * c, effR[i] * c, dl, dr, ds);
                wetL[i] += dl * dOut; wetR[i] += dr * dOut;
                delayActive = dOut > 1.0e-4f || std::fabs (dl) + std::fabs (dr) > 1.0e-5f;
            }

            // Interrupt lets the live signal through between glitches
            const float pass = sPass.next (fx.passTarget());
            const float trim = sTrim.next (trims[p.effect]);
            float wl = wetL[i] * trim + effL[i] * pass;
            float wr = wetR[i] * trim + effR[i] * pass;

            // Looper Only mutes the effects; mod, filter and space stay on
            const float lo = sLoopOnly.next (p.loopOnly ? 1.0f : 0.0f);
            wl = wl * (1.0f - lo) + effL[i] * lo;
            wr = wr * (1.0f - lo) + effR[i] * lo;

            fbL[(size_t) i] = wl; fbR[(size_t) i] = wr;
            wetPeak = std::max (wetPeak, std::fabs (wl) + std::fabs (wr));

            postChain (wl, wr, p);

            const float mix = sMix.next (p.mix);
            const float dryG = std::min (1.0f, 2.0f * (1.0f - mix));
            const float wetG = std::min (1.0f, 2.0f * mix);
            const float bp = sBypass.next (p.bypass ? 1.0f : 0.0f);
            const float wetKeep = p.trails ? 1.0f : 1.0f - bp;
            const float dryNow = lerpf (dryG, 1.0f, bp);
            float ol = dryL[i] * dryNow + wl * wetG * wetKeep;
            float orr = dryR[i] * dryNow + wr * wetG * wetKeep;
            if (p.inputMode == InputBass) { ol += lowL[i] * wetG * (1.0f - bp); orr += lowR[i] * wetG * (1.0f - bp); }
            L[i] = ol; R[i] = orr;
        }
        uiWet.store (wetPeak * 0.5f);
        uiHeld.store (p.hold);

        // post-FX loop: records what you hear, plays back on top
        if (! p.loopPre)
        {
            float recL[EffectBank::block], recR[EffectBank::block];
            for (int i = 0; i < n; ++i) { recL[i] = L[i]; recR[i] = R[i]; }
            looper.process (recL, recR, L, R, n, ls);
        }
        for (int i = 0; i < n; ++i)
        {
            L[i] = softClip (L[i]);
            R[i] = softClip (R[i]);
        }
    }

    // pitch modulation -> resonant low-pass -> delay and reverb -> volume
    void postChain (float& l, float& r, const EngineParams& p)
    {
        l = dc[0].process (l); r = dc[1].process (r);

        // pitch modulation: a gentle chorus at low depth, a seasick warble at the top
        modPhase += p.modRateHz / fs;
        if (modPhase >= 1.0f) modPhase -= 1.0f;
        const float depth = sModDepth.next (p.modDepth);
        modLine[0].push (l); modLine[1].push (r);
        const float centre = 0.0065f * fs, swing = depth * depth * 0.006f * fs;
        l = modLine[0].tap (centre + swing * std::sin (kTwoPi * modPhase));
        r = modLine[1].tap (centre + swing * std::sin (kTwoPi * modPhase + 1.5708f));

        // filter: fully clockwise is open, fully anticlockwise kills the effect
        const float f = sFilter.next (p.filter);
        const float q = 0.6f + 11.0f * std::pow (sRes.next (p.resonance), 1.6f);
        if ((++filterTick & 7) == 0)
        {
            const float fc = 30.0f * std::pow (20000.0f / 30.0f, f);
            filt[0].set (fc, q, fs); filt[1].set (fc, q, fs);
        }
        const float fl = filt[0].lp (l), fr = filt[1].lp (r);
        const float open = clampf ((f - 0.97f) / 0.03f, 0.0f, 1.0f);
        const float kill = clampf (f / 0.04f, 0.0f, 1.0f);
        const float resComp = 1.0f / (1.0f + 0.08f * (q - 0.6f));
        l = (fl * resComp * (1.0f - open) + l * open) * kill;
        r = (fr * resComp * (1.0f - open) + r * open) * kill;

        // space: a dotted echo and the reverb, all wet at the top of the knob
        const float s = sSpace.next (p.space);
        if (s > 1.0e-3f || spaceTail > 0)
        {
            const float dt = sSpaceTime.next (clampf (0.75f * p.beatSamples, 0.08f * fs, 2.4f * fs));
            const float el = spaceLine[0].tap (dt), er = spaceLine[1].tap (dt);
            spaceLine[0].push (l + spaceLp[1].process (er) * 0.42f);  // ping-pong
            spaceLine[1].push (r + spaceLp[0].process (el) * 0.42f);
            float rl, rr;
            reverb.process (l + el * 0.6f, r + er * 0.6f, rl, rr);
            const float dryPart = std::cos (s * kPi * 0.5f);
            const float wetPart = std::sin (s * kPi * 0.5f);
            l = l * dryPart + (rl + el * 0.45f) * wetPart;
            r = r * dryPart + (rr + er * 0.45f) * wetPart;
            spaceTail = s > 1.0e-3f ? (int) (20.0f * fs) : spaceTail - 1;
        }

        const float vol = sVol.next (dbToGain (p.volumeDb));
        l *= vol; r *= vol;
    }

    void captureHold (float beatSamples)
    {
        holdLen = (int) clampf (beatSamples, 0.35f * fs, 2.0f * fs);
        const int total = holdLen + holdXf;
        for (int k = 0; k < total; ++k)
        {
            holdBuf[0][(size_t) k] = holdRing.at (total - k);
            holdBuf[1][(size_t) k] = holdRingR.at (total - k);
        }
        holdPos = 0;
    }

    // loop body is holdBuf[xf .. xf+len); the audio before it blends over the seam
    void readHold (float& l, float& r)
    {
        const int p = holdPos;
        l = holdBuf[0][(size_t) (holdXf + p)];
        r = holdBuf[1][(size_t) (holdXf + p)];
        const int fadeFrom = holdLen - holdXf;
        if (p >= fadeFrom)
        {
            const int k = p - fadeFrom;
            const float x = (float) k / (float) holdXf;
            const float a = std::cos (x * kPi * 0.5f), b = std::sin (x * kPi * 0.5f);
            l = l * a + holdBuf[0][(size_t) k] * b;
            r = r * a + holdBuf[1][(size_t) k] * b;
        }
        if (++holdPos >= holdLen) holdPos = 0;
    }

    float fs = 48000.0f;
    Detector detector;
    History hist;
    OnsetRing onsets;
    EffectBank fx;
    MultiDelay delay;
    Reverb reverb;
    Looper looper;
    std::atomic<int> commands { 0 };

    DelayLine holdRing, holdRingR;
    std::vector<float> holdBuf[2];
    int holdLen = 0, holdPos = 0, holdXf = 1440;
    bool wasHeld = false;

    DelayLine modLine[2], spaceLine[2];
    float modPhase = 0.0f;
    Svf filt[2];
    int filterTick = 0, spaceTail = 0;
    OnePole spaceLp[2];
    Biquad bassLp[2][2], bassHp[2][2];
    DcBlock dc[2];
    Smoother sMix, sVol, sSpace, sModDepth, sFilter, sRes, sPass, sDelayOut, sLoopOnly, sHold, sBypass, sTrim, sSpaceTime;
    std::vector<float> fbL, fbR;
    int lastEffect = -1, lastReverbMode = -1;
    bool delayActive = false;
};
} // namespace spk
