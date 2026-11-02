// Offline render test for the Speckle engine (no JUCE needed).
// Plays a guitar-like phrase through every effect and variation, the looper and the
// hold sampler, then checks for NaNs, runaway levels, clicks and silence.
// Pass a folder to also write WAV files.
//
// Usage: speckle_render_test [output_folder]

#include "SpeckleEngine.h"
#include <chrono>
#if defined(__SSE__) || defined(_M_X64)
#include <xmmintrin.h>
#endif
#include <cstdio>
#include <functional>
#include <memory>
#include <string>

using namespace spk;

static constexpr float kFs = 48000.0f;

// Plucked notes, a chord and a held synth-like note, then silence
static void makeInput (std::vector<float>& l, std::vector<float>& r, int n, float lowest = 196.0f)
{
    l.assign ((size_t) n, 0.0f);
    r.assign ((size_t) n, 0.0f);
    const float ratios[8] = { 1.0f, 1.26f, 1.498f, 2.0f, 1.498f, 1.26f, 1.122f, 1.0f };
    for (int k = 0; k < 8; ++k)
    {
        const int s0 = (int) ((0.15f + k * 0.62f) * kFs);
        const bool chord = k == 3 || k == 7;
        for (int i = 0; i < (int) (1.8f * kFs) && s0 + i < n; ++i)
        {
            const float t = (float) i / kFs;
            const float e = std::exp (-t * 2.2f) * std::min (1.0f, t * 400.0f);
            float v = 0.0f;
            for (int c = 0; c < (chord ? 3 : 1); ++c)
            {
                const float f = lowest * ratios[k] * (c == 0 ? 1.0f : (c == 1 ? 1.26f : 1.498f));
                v += 0.2f * e * (std::sin (kTwoPi * f * t) + 0.5f * std::sin (kTwoPi * 2 * f * t + 0.3f) + 0.25f * std::sin (kTwoPi * 3 * f * t) + 0.1f * std::sin (kTwoPi * 5 * f * t));
            }
            l[(size_t) (s0 + i)] += v;
            r[(size_t) (s0 + i)] += v * 0.97f;
        }
    }
}

static void writeWav (const std::string& path, const std::vector<float>& l, const std::vector<float>& r)
{
    FILE* f = std::fopen (path.c_str(), "wb");
    if (! f) return;
    const uint32_t n = (uint32_t) l.size(), dataBytes = n * 4, sr = (uint32_t) kFs;
    auto u32 = [&] (uint32_t v) { std::fwrite (&v, 4, 1, f); };
    auto u16 = [&] (uint16_t v) { std::fwrite (&v, 2, 1, f); };
    std::fwrite ("RIFF", 1, 4, f); u32 (36 + dataBytes); std::fwrite ("WAVE", 1, 4, f);
    std::fwrite ("fmt ", 1, 4, f); u32 (16); u16 (1); u16 (2); u32 (sr); u32 (sr * 4); u16 (4); u16 (16);
    std::fwrite ("data", 1, 4, f); u32 (dataBytes);
    for (uint32_t i = 0; i < n; ++i)
    {
        int16_t a = (int16_t) (clampf (l[i], -1, 1) * 32767.0f);
        int16_t b = (int16_t) (clampf (r[i], -1, 1) * 32767.0f);
        std::fwrite (&a, 2, 1, f); std::fwrite (&b, 2, 1, f);
    }
    std::fclose (f);
}

struct Result { float peak = 0, rms = 0, tailRms = 0, maxJump = 0, side = 0, ms = 0, jumpAt = 0; bool nan = false; };

using Script = std::function<void (float t, EngineParams&, SpeckleEngine&)>;

static Result render (const std::string& name, EngineParams p, const Script& script, const std::string& outDir, float seconds = 9.0f, float lowest = 196.0f)
{
    static std::unique_ptr<SpeckleEngine> eng;
    if (! eng) eng = std::make_unique<SpeckleEngine>();
    eng->prepare (kFs);
    eng->postCommand (SpeckleEngine::CmdClearAll);
    const int n = (int) (seconds * kFs);
    std::vector<float> l, r;
    makeInput (l, r, n, lowest);

    const int block = 256;
    const auto t0 = std::chrono::steady_clock::now();
    for (int s = 0; s < n; s += block)
    {
        const int len = std::min (block, n - s);
        if (script) script ((float) s / kFs, p, *eng);
        eng->process (l.data() + s, r.data() + s, len, p);
    }
    const double secs = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();

    Result res;
    res.ms = (float) (secs * 1000.0);
    double sum = 0, tail = 0, side = 0;
    const int tailStart = (int) ((seconds - 2.0f) * kFs);
    for (int i = 0; i < n; ++i)
    {
        const float a = l[(size_t) i], b = r[(size_t) i];
        if (! std::isfinite (a) || ! std::isfinite (b)) res.nan = true;
        res.peak = std::max (res.peak, std::max (std::fabs (a), std::fabs (b)));
        sum += (double) a * a + (double) b * b;
        side += (double) (a - b) * (a - b);
        if (i >= tailStart) tail += (double) a * a + (double) b * b;
        if (i > 1)
        {
            const float d2 = std::fabs (a - 2 * l[(size_t) i - 1] + l[(size_t) i - 2]);
            if (d2 > res.maxJump) { res.maxJump = d2; res.jumpAt = (float) i / kFs; }
        }
    }
    res.rms = (float) std::sqrt (sum / (2.0 * n));
    res.side = (float) std::sqrt (side / n);
    res.tailRms = (float) std::sqrt (tail / (2.0 * std::max (1, n - tailStart)));
    std::printf ("%-22s peak %.3f  rms %.4f  tail %.4f  side %.4f  jump %.3f@%.2f  cpu %4.0f%%%s\n", name.c_str(), res.peak, res.rms, res.tailRms, res.side, res.maxJump, res.jumpAt,
                 100.0 * secs / seconds, res.nan ? "  NaN!" : "");
    if (! outDir.empty()) writeWav (outDir + "/" + name + ".wav", l, r);
    return res;
}

int main (int argc, char** argv)
{
    const std::string out = argc > 1 ? argv[1] : "";
#if defined(__SSE__) || defined(_M_X64)
    _mm_setcsr (_mm_getcsr() | 0x8040); // flush denormals, as the plugin does
#endif
    int failures = 0;
    auto expect = [&] (bool ok, const char* what, const std::string& name)
    {
        if (! ok) { std::printf ("  FAIL %s: %s\n", name.c_str(), what); ++failures; }
    };
    auto check = [&] (const std::string& name, const Result& r, float jumpLimit = 0.3f)
    {
        expect (! r.nan, "NaN in output", name);
        expect (r.peak <= 1.0f, "output over full scale", name);
        expect (r.rms > 0.006f, "no effect heard", name);
        expect (r.maxJump < jumpLimit, "click (sudden jump)", name);
    };

    EngineParams base;
    base.mix = 1.0f;     // effect only, so silence means the effect did nothing
    base.space = 0.0f;
    base.stepSamples = 0.5f * 60.0f / 100.0f * kFs; // 1/8 at 100 bpm
    base.beatSamples = 60.0f / 100.0f * kFs;

    static const char* fxNames[kNumEffects] = { "mosaic", "seq", "glide", "haze", "tunnel", "strum", "blocks", "interrupt", "arp", "pattern", "warp" };
    const char letters[4] = { 'a', 'b', 'c', 'd' };
    for (int e = 0; e < kNumEffects; ++e)
        for (int v = 0; v < 4; ++v)
        {
            auto p = base;
            p.effect = e; p.variant = v;
            const bool crushed = (e == Seq && v == 3) || (e == Blocks && v == 3) || (e == Interrupt && v == 3) || (e == Arp && v == 3);
            const std::string name = std::string (fxNames[e]) + "-" + letters[v];
            check (name, render (name, p, nullptr, out), crushed ? 0.9f : 0.3f);
        }

    // extremes: everything up, reversed, full activity and endless repeats
    for (int e = 0; e < kNumEffects; ++e)
    {
        auto p = base;
        p.effect = e; p.variant = 3; p.activity = 1.0f; p.repeats = 1.0f; p.reverse = true; p.shape = 0.9f;
        const std::string name = std::string (fxNames[e]) + "-max-rev";
        check (name, render (name, p, nullptr, out), 0.9f);
    }

    // a lush default: Haze B in the ambient reverb with some modulation
    {
        auto p = base; p.effect = Haze; p.variant = 1; p.mix = 0.55f; p.space = 0.6f; p.reverbMode = Reverb::Ambient; p.modDepth = 0.3f;
        auto r = render ("lush-haze-ambient", p, nullptr, out, 12.0f);
        check ("lush-haze-ambient", r);
        expect (r.side > 0.004f, "no stereo width", "lush-haze-ambient");
    }
    for (int m = 0; m < 4; ++m)
    {
        auto p = base; p.effect = Pattern; p.variant = 0; p.mix = 0.5f; p.space = 1.0f; p.reverbMode = m;
        const std::string name = std::string ("reverb-") + letters[m];
        check (name, render (name, p, nullptr, out, 10.0f));
    }
    {
        auto p = base; p.effect = Mosaic; p.modDepth = 1.0f; p.modRateHz = 6.0f; p.filter = 0.45f; p.resonance = 0.9f;
        check ("mod-filter-extreme", render ("mod-filter-extreme", p, nullptr, out));
    }
    {
        // knobs swept while playing: nothing may click
        auto p = base; p.effect = Glide; p.space = 0.5f;
        check ("sweeps", render ("sweeps", p, [] (float t, EngineParams& q, SpeckleEngine&)
        {
            q.filter = 0.55f + 0.45f * std::sin (t * 1.3f);
            q.space = 0.5f + 0.5f * std::sin (t * 0.7f);
            q.mix = 0.5f + 0.5f * std::sin (t * 0.9f);
            q.stepSamples = (0.35f + 0.25f * std::sin (t * 0.5f)) * kFs;
            q.reverbMode = (int) (t / 2.5f) % 4;
        }, out));
    }
    {
        // switching effects every bar
        auto p = base;
        check ("effect-hopping", render ("effect-hopping", p, [] (float t, EngineParams& q, SpeckleEngine&) { q.effect = (int) (t / 0.8f) % kNumEffects; q.variant = (int) (t / 1.7f) % 4; }, out, 12.0f), 0.9f);
    }
    {
        auto p = base; p.effect = Haze; p.inputMode = InputBass;
        check ("bass-haze", render ("bass-haze", p, nullptr, out, 9.0f, 55.0f));
    }
    {
        // hold freezes a beat and the effect keeps working on it after the input stops
        auto p = base; p.effect = Mosaic;
        auto r = render ("hold", p, [] (float t, EngineParams& q, SpeckleEngine&) { q.hold = t > 1.1f; }, out, 10.0f);
        check ("hold", r);
        expect (r.tailRms > 0.01f, "held sound died away", "hold");
    }
    {
        // looper: record two seconds, play, overdub, then stop with a fade
        auto p = base; p.effect = Haze; p.mix = 0.5f; p.fadeSeconds = 1.0f;
        auto r = render ("looper", p, [&] (float t, EngineParams&, SpeckleEngine& e)
        {
            static int phase = 0;
            if (t < 0.01f) phase = 0;
            if (phase == 0 && t > 0.15f) { e.postCommand (SpeckleEngine::CmdRec); phase = 1; }
            if (phase == 1 && t > 2.25f) { e.postCommand (SpeckleEngine::CmdRec); phase = 2; }
            if (phase == 2 && t > 4.0f) { e.postCommand (SpeckleEngine::CmdRec); phase = 3; }
            if (phase == 3 && t > 5.0f) { e.postCommand (SpeckleEngine::CmdRec); phase = 4; }
            if (phase == 4 && t > 8.5f) { e.postCommand (SpeckleEngine::CmdStop); phase = 5; }
        }, out, 11.0f);
        check ("looper", r);
    }
    {
        // looper at double speed, reversed, quantised to the beat
        auto p = base; p.effect = Pattern; p.mix = 0.4f; p.loopSpeed = 2.0f; p.loopReverse = true; p.quantise = true;
        auto r = render ("looper-fast-rev", p, [&] (float t, EngineParams&, SpeckleEngine& e)
        {
            static int phase = 0;
            if (t < 0.01f) phase = 0;
            if (phase == 0 && t > 0.15f) { e.postCommand (SpeckleEngine::CmdRec); phase = 1; }
            if (phase == 1 && t > 2.6f) { e.postCommand (SpeckleEngine::CmdRec); phase = 2; }
        }, out);
        check ("looper-fast-rev", r);
    }
    {
        // bypass with trails: dry returns at unity
        auto p = base; p.effect = Warp; p.variant = 2; p.mix = 0.5f; p.space = 0.5f;
        check ("bypass-trails", render ("bypass-trails", p, [] (float t, EngineParams& q, SpeckleEngine&) { q.bypass = t > 4.0f; }, out));
    }

    std::printf (failures == 0 ? "All scenarios passed\n" : "%d check(s) failed\n", failures);
    return failures == 0 ? 0 : 1;
}
