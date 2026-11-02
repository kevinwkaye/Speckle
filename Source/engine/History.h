#pragma once

#include "DspUtil.h"

// A stereo record of the recent past that grains, loops and delay taps read from.
// It keeps three copies: full range, and two band-limited ones for reads at double
// and quadruple speed, so octave-up grains stay clean instead of aliasing.
namespace spk
{
class History
{
public:
    enum Band { Full = 0, Half, Quarter, kBands };

    void prepare (float sampleRate, float seconds)
    {
        fs = sampleRate;
        const int n = nextPow2 ((int) (seconds * fs));
        mask = n - 1;
        for (auto& band : buf)
            for (auto& ch : band)
                ch.assign ((size_t) n, 0.0f);
        for (int c = 0; c < 2; ++c)
        {
            lpHalf[c].lowpass (0.235f * fs, fs);
            lpQuarter[c].lowpass (0.115f * fs, fs);
        }
        clear();
    }

    void clear()
    {
        for (auto& band : buf)
            for (auto& ch : band)
                std::fill (ch.begin(), ch.end(), 0.0f);
        for (int c = 0; c < 2; ++c) { lpHalf[c].reset(); lpQuarter[c].reset(); }
        now = 0;
    }

    void push (float l, float r)
    {
        const size_t i = (size_t) (now & mask);
        const float in[2] = { l, r };
        for (int c = 0; c < 2; ++c)
        {
            buf[Full][c][i] = in[c];
            buf[Half][c][i] = lpHalf[c].process (in[c]);
            buf[Quarter][c][i] = lpQuarter[c].process (in[c]);
        }
        ++now;
    }

    // The band a read at this speed should use
    static int bandFor (float absSpeed) { return absSpeed > 2.6f ? Quarter : (absSpeed > 1.3f ? Half : Full); }

    // Absolute position (samples since prepare). pos must lie in the past.
    void read (int band, double pos, float& l, float& r) const
    {
        l = ringRead (buf[band][0].data(), mask, pos);
        r = ringRead (buf[band][1].data(), mask, pos);
    }

    float readMono (int band, double pos) const
    {
        return 0.5f * (ringRead (buf[band][0].data(), mask, pos) + ringRead (buf[band][1].data(), mask, pos));
    }

    int64_t writePos() const { return now; }        // index the next sample will take
    int64_t capacity() const { return mask + 1; }
    float sampleRate() const { return fs; }

private:
    float fs = 48000.0f;
    int64_t mask = 0, now = 0;
    std::vector<float> buf[kBands][2];
    Butter4 lpHalf[2], lpQuarter[2];
};
} // namespace spk
