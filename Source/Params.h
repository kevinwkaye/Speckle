#pragma once

#include "engine/SpeckleEngine.h"
#include <juce_audio_processors/juce_audio_processors.h>

// Every control on the Speckle panel. All are automatable, MIDI-mappable in
// Ableton and saved with the set.
namespace ParamIDs
{
inline constexpr auto effect = "effect";
inline constexpr auto variant = "variant";
inline constexpr auto reverse = "reverse";
inline constexpr auto activity = "activity";
inline constexpr auto repeats = "repeats";
inline constexpr auto shape = "shape";
inline constexpr auto timeMode = "timeMode";
inline constexpr auto subdiv = "subdiv";
inline constexpr auto bpm = "bpm";
inline constexpr auto sync = "sync";
inline constexpr auto filter = "filter";
inline constexpr auto resonance = "resonance";
inline constexpr auto mix = "mix";
inline constexpr auto volume = "volume";
inline constexpr auto space = "space";
inline constexpr auto reverbMode = "reverbMode";
inline constexpr auto modDepth = "modDepth";
inline constexpr auto modRate = "modRate";
inline constexpr auto inputMode = "inputMode";
inline constexpr auto hold = "hold";
inline constexpr auto bypass = "bypass";
inline constexpr auto trails = "trails";
inline constexpr auto loopLevel = "loopLevel";
inline constexpr auto loopSpeed = "loopSpeed";
inline constexpr auto fadeTime = "fadeTime";
inline constexpr auto fadeMode = "fadeMode";
inline constexpr auto loopPre = "loopPre";
inline constexpr auto quantise = "quantise";
inline constexpr auto loopReverse = "loopReverse";
inline constexpr auto burst = "burst";
inline constexpr auto loopOnly = "loopOnly";
inline constexpr auto recOrder = "recOrder";
inline constexpr auto expTarget = "expTarget";
inline constexpr auto keeper = "keeper";
// momentary actions, as parameters so a remote controller (the live rig) can press them
inline constexpr auto actRec = "actRec";
inline constexpr auto actStop = "actStop";
inline constexpr auto actNextFx = "actNextFx";
} // namespace ParamIDs

namespace names
{
inline const juce::StringArray effects { "Mosaic", "Seq", "Glide", "Haze", "Tunnel", "Strum", "Blocks", "Interrupt", "Arp", "Pattern", "Warp" };
inline const juce::StringArray groups { "Micro Loop", "Micro Loop", "Micro Loop", "Granules", "Granules", "Granules", "Glitch", "Glitch", "Glitch", "Multi Delay", "Multi Delay" };
inline const juce::StringArray subdivs { "1 bar", "1/2", "1/4 dot", "1/4", "1/4 trip", "1/8 dot", "1/8", "1/8 trip", "1/16" };
inline constexpr float subdivBeats[9] = { 4.0f, 2.0f, 1.5f, 1.0f, 2.0f / 3.0f, 0.75f, 0.5f, 1.0f / 3.0f, 0.25f };
inline const juce::StringArray reverbs { "Bright Room", "Dark Medium", "Large Hall", "Ambient" };

// what an expression pedal can move: every knob on the panel
struct ExpTarget { const char* id; const char* label; };
inline const std::vector<ExpTarget>& expTargets()
{
    static const std::vector<ExpTarget> t {
        { "activity", "Activity" }, { "repeats", "Repeats" }, { "shape", "Shape" }, { "modDepth", "Mod Depth" },
        { "modRate", "Mod Rate" }, { "filter", "Filter" }, { "resonance", "Resonance" }, { "space", "Space" },
        { "mix", "Mix" }, { "loopLevel", "Loop Level" }, { "loopSpeed", "Loop Speed" }, { "fadeTime", "Fade Time" },
        { "volume", "Volume" },
    };
    return t;
}
inline juce::StringArray expTargetNames()
{
    juce::StringArray s;
    for (const auto& t : expTargets()) s.add (t.label);
    return s;
}

inline float modHz (float v01) { return 0.05f * std::pow (2.0f, v01 * 7.3f); }        // 0.05 .. 8 Hz
inline float loopSpeedRaw (float v01) { return 0.25f * std::pow (16.0f, v01); }       // quarter .. 4x
inline float loopSpeedStepped (float v01)
{
    static const float steps[5] = { 0.25f, 0.5f, 1.0f, 2.0f, 4.0f };
    return steps[juce::jlimit (0, 4, juce::roundToInt (v01 * 4.0f))];
}
inline float fadeSeconds (float v01) { return 10.0f * v01 * v01; }
inline juce::String speedText (float s)
{
    if (s < 0.3f) return "1/4X";
    if (std::abs (s - 0.5f) < 0.01f) return "1/2X";
    return juce::String (s, s < 1.0f || std::abs (s - std::round (s)) > 0.01f ? 2 : 0) + "X";
}
} // namespace names

inline juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout()
{
    using namespace juce;
    using P = AudioParameterFloat;
    AudioProcessorValueTreeState::ParameterLayout layout;

    auto pctText = AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return String (roundToInt (v * 100.0f)) + "%"; });
    auto knob = [&] (const char* id, const char* name, float def)
    {
        layout.add (std::make_unique<P> (ParameterID { id, 1 }, name, NormalisableRange<float> (0.0f, 1.0f), def, pctText));
    };
    auto knobWith = [&] (const char* id, const char* name, float def, std::function<String (float, int)> text)
    {
        layout.add (std::make_unique<P> (ParameterID { id, 1 }, name, NormalisableRange<float> (0.0f, 1.0f), def,
                                         AudioParameterFloatAttributes().withStringFromValueFunction (std::move (text))));
    };
    auto toggle = [&] (const char* id, const char* name, bool def) { layout.add (std::make_unique<AudioParameterBool> (ParameterID { id, 1 }, name, def)); };
    auto choice = [&] (const char* id, const char* name, StringArray items, int def) { layout.add (std::make_unique<AudioParameterChoice> (ParameterID { id, 1 }, name, items, def)); };

    // ---- effect ----
    choice (ParamIDs::effect, "Effect", names::effects, spk::Haze);
    choice (ParamIDs::variant, "Variation", { "A", "B", "C", "D" }, 1);
    toggle (ParamIDs::reverse, "Reverse", false);

    // ---- shape ----
    knob (ParamIDs::activity, "Activity", 0.55f);
    knob (ParamIDs::repeats, "Repeats", 0.5f);
    knobWith (ParamIDs::shape, "Shape", 0.5f, [] (float v, int)
    {
        if (v < 0.45f) return String ("Pluck");
        if (v > 0.55f) return String ("Swell");
        return String ("Even");
    });
    choice (ParamIDs::timeMode, "Time Mode", { "Subdiv", "Tempo" }, 0);
    choice (ParamIDs::subdiv, "Subdivision", names::subdivs, 6);
    layout.add (std::make_unique<P> (ParameterID { ParamIDs::bpm, 1 }, "Tempo", NormalisableRange<float> (40.0f, 240.0f, 0.1f), 100.0f,
                                     AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return String (roundToInt (v)) + " BPM"; })));
    toggle (ParamIDs::sync, "Sync to Ableton", true);
    knobWith (ParamIDs::modDepth, "Mod Depth", 0.12f, [] (float v, int) { return String (roundToInt (v * 100.0f)) + "%"; });
    knobWith (ParamIDs::modRate, "Mod Rate", 0.45f, [] (float v, int)
    {
        const float hz = names::modHz (v);
        return hz < 1.0f ? String (hz, 2) + " Hz" : String (hz, 1) + " Hz";
    });

    // ---- tone and space ----
    knobWith (ParamIDs::filter, "Filter", 1.0f, [] (float v, int)
    {
        if (v >= 0.97f) return String ("Open");
        if (v <= 0.04f) return String ("Shut");
        const float hz = 30.0f * std::pow (20000.0f / 30.0f, v);
        return hz < 1000.0f ? String (roundToInt (hz)) + " Hz" : String (hz / 1000.0f, 1) + " kHz";
    });
    knob (ParamIDs::resonance, "Resonance", 0.2f);
    knob (ParamIDs::space, "Space", 0.4f);
    knob (ParamIDs::mix, "Mix", 0.5f);
    choice (ParamIDs::reverbMode, "Reverb", names::reverbs, 3);

    // ---- looper ----
    knob (ParamIDs::loopLevel, "Loop Level", 0.8f);
    knobWith (ParamIDs::loopSpeed, "Loop Speed", 0.5f, [] (float v, int) { return names::speedText (names::loopSpeedRaw (v)); });
    knobWith (ParamIDs::fadeTime, "Fade Time", 0.1f, [] (float v, int)
    {
        const float s = names::fadeSeconds (v);
        return s < 0.05f ? String ("Cut") : (s < 1.0f ? String (roundToInt (s * 1000.0f)) + " ms" : String (s, 1) + " s");
    });
    layout.add (std::make_unique<P> (ParameterID { ParamIDs::volume, 1 }, "Volume", NormalisableRange<float> (-24.0f, 6.0f), 0.0f,
                                     AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int)
                                     {
                                         return std::abs (v) < 0.05f ? String ("0 dB") : String (v, 1) + " dB";
                                     })));
    choice (ParamIDs::fadeMode, "Loop Fade", { "In and Out", "In Only", "Out Only" }, 0);
    toggle (ParamIDs::loopPre, "Loop Before Effects", false);
    toggle (ParamIDs::quantise, "Quantise Loop", false);
    toggle (ParamIDs::loopReverse, "Reverse Loop", false);
    toggle (ParamIDs::burst, "Burst", false);
    toggle (ParamIDs::loopOnly, "Looper Only", false);
    choice (ParamIDs::recOrder, "Looper Order", { "Rec Play Dub", "Rec Dub Play" }, 0);

    // ---- input and switches ----
    choice (ParamIDs::inputMode, "Input", { "Guitar", "Synth", "Bass" }, 0);
    toggle (ParamIDs::hold, "Hold", false);
    toggle (ParamIDs::bypass, "Bypass", false);
    toggle (ParamIDs::trails, "Trails", true);

    // ---- expression, Keeper and the footswitch actions ----
    choice (ParamIDs::expTarget, "Expression Target", names::expTargetNames(), 0);
    toggle (ParamIDs::keeper, "Keeper", false);
    toggle (ParamIDs::actRec, "Loop Rec / Dub", false);
    toggle (ParamIDs::actStop, "Loop Stop / Play", false);
    toggle (ParamIDs::actNextFx, "Next Effect", false);

    return layout;
}
