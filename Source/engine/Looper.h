#pragma once

#include "DspUtil.h"

// The 60 second stereo phrase looper: record, play, overdub, undo, stop and erase,
// with reverse, quarter to four times speed, fades and tempo quantise.
namespace spk
{
class Looper
{
public:
    enum State { Empty = 0, Recording, Playing, Overdubbing, Stopping, Stopped };
    static constexpr float kMaxSeconds = 60.0f;

    struct Settings
    {
        int order = 0;          // 0 Rec > Play > Dub, 1 Rec > Dub > Play
        bool quantise = false;
        float beatSamples = 24000.0f;
        bool reverse = false;
        float speed = 1.0f;
        float fadeSeconds = 0.0f;
        int fadeMode = 0;       // 0 in and out, 1 in only, 2 out only
        float level = 1.0f;
    };

    void prepare (float sampleRate)
    {
        fs = sampleRate;
        cap = (int) (kMaxSeconds * fs);
        xf = (int) (0.012f * fs);
        dubRamp = 1.0f / (0.008f * fs);
        for (int c = 0; c < 2; ++c)
        {
            if ((int) base[c].size() != cap + 8) { base[c].assign ((size_t) cap + 8, 0.0f); dub[c].assign ((size_t) cap + 8, 0.0f); state = Empty; len = 0; }
            tail[c].assign ((size_t) xf + 4, 0.0f);
            aa[c].reset();
        }
        aaSpeed = 1.0f;
        lvl.setTime (0.02f, fs);
        tailValid = false;
    }

    // ---- commands (audio thread) ----
    // A loop shorter than this is never made. Ending a recording sooner than this was the
    // "0.1 second pad" bug: a double press or a quick Burst tap closed a tiny loop.
    static constexpr float kMinSeconds = 0.25f;
    // presses closer together than this are one press (a footswitch that sends twice, a bounce)
    static constexpr float kDebounceSeconds = 0.08f;

    void pressRec()
    {
        if (sinceRec < (int64_t) (kDebounceSeconds * fs)) return;
        sinceRec = 0;
        if (state == Recording && recPos < minLen()) return; // too soon: keep recording
        beforePress = state;
        switch (state)
        {
            case Empty: case Stopped:
                if (state == Stopped && len > 0) { startPlay (true); break; }
                startRecording(); break;
            case Recording: closeLoop (settings.order == 1); break;
            case Playing: state = Overdubbing; break;
            case Overdubbing: state = Playing; break;
            case Stopping: startPlay (true); break;
        }
    }
    void holdRec() { if (len > 0) { clearDub(); if (state == Overdubbing) state = Playing; } }
    void pressStop()
    {
        if (state == Recording && recPos < minLen()) { erase(); return; } // nothing worth keeping yet
        if (state == Recording) { closeLoop (false); }
        if (state == Playing || state == Overdubbing)
        {
            state = Stopping;
            const bool fadeOut = settings.fadeMode == 0 || settings.fadeMode == 2;
            fadeRate = 1.0f / std::max (1.0f, (fadeOut ? std::max (0.008f, settings.fadeSeconds) : 0.008f) * fs);
        }
    }
    void erase() { clearDub(); state = Empty; len = 0; recPos = 0; playPos = 0.0; playGain = 0.0f; tailValid = false; seam = false; }
    void burstDown() { erase(); startRecording(); }
    void burstUp()
    {
        if (state != Recording) return;
        if (recPos < minLen()) erase(); // a tap too short to loop records nothing
        else closeLoop (false);
    }
    int minLen() const { return (int) (kMinSeconds * fs); }

    // Footswitch C acts the moment it goes down, so the loop starts on time. If D joins it a
    // moment later, the pair was a C+D combo, and that first press is taken back.
    void revertPress()
    {
        if (sinceRec > (int64_t) (0.3f * fs)) return;
        if (state == Recording && beforePress != Recording) erase();
        else if (state == Overdubbing && beforePress == Playing) state = Playing;
        else if (state == Playing && (beforePress == Stopped || beforePress == Stopping)) { state = Stopped; playGain = 0.0f; playPos = 0.0; }
        sinceRec = 1 << 30;
    }

    // in: what gets recorded. out: loop playback added here.
    void process (const float* inL, const float* inR, float* outL, float* outR, int n, const Settings& s)
    {
        settings = s;
        const float speed = clampf (s.speed, 0.25f, 4.0f);
        if (std::fabs (speed - aaSpeed) > 0.01f * aaSpeed)
        {
            aaSpeed = speed;
            for (auto& f : aa) f.lowpass (std::min (0.45f * fs, 0.45f * fs / speed), fs);
        }
        const float step = speed * (s.reverse ? -1.0f : 1.0f);

        sinceRec += n;
        for (int i = 0; i < n; ++i)
        {
            const float level = lvl.next (s.level);
            if (state == Recording)
            {
                if (recPos < cap)
                {
                    base[0][(size_t) recPos] = inL[i];
                    base[1][(size_t) recPos] = inR[i];
                    ++recPos;
                }
                if (recPos >= cap) closeLoop (false);
                continue;
            }
            if (state == Empty || state == Stopped || len <= 0) continue;

            // just after closing, keep capturing a moment more to blend over the seam
            if (tailFill < xf)
            {
                tail[0][(size_t) tailFill] = inL[i];
                tail[1][(size_t) tailFill] = inR[i];
                ++tailFill;
                if (tailFill >= xf) tailValid = true;
            }

            // fades
            if (state == Stopping)
            {
                playGain -= fadeRate;
                if (playGain <= 0.0f) { playGain = 0.0f; state = Stopped; playPos = 0.0; continue; }
            }
            else if (playGain < 1.0f)
                playGain = std::min (1.0f, playGain + fadeRate);

            float l, r;
            readAt (playPos, l, r);
            if (std::fabs (speed - 1.0f) > 0.01f || aaActive)
            {
                aaActive = speed > 1.01f;
                if (aaActive) { l = aa[0].process (l); r = aa[1].process (r); }
            }
            const float g = playGain * playGain * level;
            outL[i] += l * g;
            outR[i] += r * g;

            // overdubs punch in and out with a short ramp so the layer has no hard edges
            dubGain = state == Overdubbing ? std::min (1.0f, dubGain + dubRamp) : std::max (0.0f, dubGain - dubRamp);
            if (dubGain > 0.0f)
            {
                const int w = ((int) playPos) % len;
                dub[0][(size_t) w] += inL[i] * dubGain;
                dub[1][(size_t) w] += inR[i] * dubGain;
            }

            playPos += step;
            if (playPos >= len) playPos -= len;
            if (playPos < 0.0) playPos += len;
        }

        uiState.store ((int) state);
        uiPos.store (len > 0 ? (float) (playPos / len) : (state == Recording ? std::min (1.0f, (float) recPos / cap) : 0.0f));
        uiSeconds.store ((state == Recording ? recPos : len) / fs);
    }

    // ---- presets ----
    int length() const { return len; }
    // overdubs are mixed down into one loop, as the pedal does when saving
    void mixdown (std::vector<float>& l, std::vector<float>& r) const
    {
        l.resize ((size_t) len); r.resize ((size_t) len);
        for (int i = 0; i < len; ++i)
        {
            float a, b;
            readIndex (i, a, b);
            l[(size_t) i] = a; r[(size_t) i] = b;
        }
    }
    void load (const std::vector<float>& l, const std::vector<float>& r, bool play)
    {
        erase();
        len = std::min ((int) std::min (l.size(), r.size()), cap);
        for (int i = 0; i < len; ++i) { base[0][(size_t) i] = l[(size_t) i]; base[1][(size_t) i] = r[(size_t) i]; }
        tailFill = xf; tailValid = false;
        if (len > 0) { if (play) startPlay (false); else state = Stopped; }
    }

    std::atomic<int> uiState { Empty };
    std::atomic<float> uiPos { 0.0f }, uiSeconds { 0.0f };

private:
    void startRecording()
    {
        clearDub();
        state = Recording;
        recPos = 0;
        len = 0;
        tailValid = false;
        seam = false;
    }

    void closeLoop (bool thenDub)
    {
        int n = recPos;
        if (settings.quantise && settings.beatSamples > 100.0f)
        {
            const int beats = std::max (1, (int) std::lround ((float) n / settings.beatSamples));
            n = std::min (cap, (int) std::lround (beats * settings.beatSamples));
            for (int i = recPos; i < n; ++i) { base[0][(size_t) i] = 0.0f; base[1][(size_t) i] = 0.0f; }
        }
        len = std::max (n, std::max (minLen(), xf * 2 + 4));
        for (int i = recPos; i < len; ++i) { base[0][(size_t) i] = 0.0f; base[1][(size_t) i] = 0.0f; }
        playPos = settings.reverse ? (double) (len - 1) : 0.0;
        tailFill = 0;
        tailValid = false;
        seam = true;
        playGain = 1.0f;
        state = thenDub ? Overdubbing : Playing;
    }

    void startPlay (bool fromStart)
    {
        if (fromStart) playPos = settings.reverse ? (double) (len - 1) : 0.0;
        state = Playing;
        const bool fadeIn = settings.fadeMode == 0 || settings.fadeMode == 1;
        fadeRate = 1.0f / std::max (1.0f, (fadeIn ? std::max (0.008f, settings.fadeSeconds) : 0.008f) * fs);
        playGain = 0.0f;
    }

    void clearDub()
    {
        for (int c = 0; c < 2; ++c) std::fill (dub[c].begin(), dub[c].begin() + std::max (len, recPos) + 1, 0.0f);
    }

    // One loop sample, with the seam blended into the audio that followed the loop point
    void readIndex (int i, float& l, float& r) const
    {
        l = base[0][(size_t) i] + dub[0][(size_t) i];
        r = base[1][(size_t) i] + dub[1][(size_t) i];
        if (seam && i < xf && (tailValid || i < tailFill))
        {
            const float x = (float) i / (float) xf;
            const float fin = std::sin (x * kPi * 0.5f), fout = std::cos (x * kPi * 0.5f);
            l = base[0][(size_t) i] * fin + tail[0][(size_t) i] * fout + dub[0][(size_t) i];
            r = base[1][(size_t) i] * fin + tail[1][(size_t) i] * fout + dub[1][(size_t) i];
        }
    }

    void readAt (double pos, float& l, float& r) const
    {
        const int i1 = (int) pos;
        const float f = (float) (pos - i1);
        float y[4][2];
        for (int k = 0; k < 4; ++k)
        {
            int j = i1 - 1 + k;
            while (j < 0) j += len;
            while (j >= len) j -= len;
            readIndex (j, y[k][0], y[k][1]);
        }
        float o[2];
        for (int c = 0; c < 2; ++c)
        {
            const float c1 = 0.5f * (y[2][c] - y[0][c]);
            const float c2 = y[0][c] - 2.5f * y[1][c] + 2.0f * y[2][c] - 0.5f * y[3][c];
            const float c3 = 0.5f * (y[3][c] - y[0][c]) + 1.5f * (y[1][c] - y[2][c]);
            o[c] = ((c3 * f + c2) * f + c1) * f + y[1][c];
        }
        l = o[0]; r = o[1];
    }

    float fs = 48000.0f;
    int cap = 0, xf = 512, len = 0, recPos = 0, tailFill = 0;
    int64_t sinceRec = 1 << 30;
    bool tailValid = false, aaActive = false, seam = false;
    double playPos = 0.0;
    float playGain = 0.0f, fadeRate = 0.01f, aaSpeed = 1.0f, dubGain = 0.0f, dubRamp = 0.002f;
    State state = Empty, beforePress = Empty;
    Settings settings;
    std::vector<float> base[2], dub[2], tail[2];
    Butter4 aa[2];
    Smoother lvl;
};
} // namespace spk
