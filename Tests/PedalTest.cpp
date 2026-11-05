// Footswitch and expression test: drives the real plugin with plain MIDI, the way a
// footswitch would, and checks every job in the footswitch table.
//
// It also checks the clash fixed in November 2026: the Microcosm's own CC chart used to answer on
// every channel, so footswitches A to D (CC 20 to 23) changed the reverb, fade and loop direction,
// and the expression pedal (CC 11) moved Repeats.
//
// Usage: speckle_pedal_test

#include "PluginProcessor.h"
#include <cstdio>

using namespace juce;

namespace
{
int failures = 0;

void expect (bool ok, const String& what)
{
    std::printf ("  %s  %s\n", ok ? "ok  " : "FAIL", what.toRawUTF8());
    if (! ok) ++failures;
}

struct Rig
{
    SpeckleProcessor proc;
    AudioBuffer<float> buffer { 2, 128 };

    Rig()
    {
        proc.setPlayConfigDetails (2, 2, 48000.0, 128);
        proc.prepareToPlay (48000.0, 128);
    }

    void cc (int number, int value, int channel = 16, int waitMs = 30)
    {
        MidiBuffer midi;
        midi.addEvent (MidiMessage::controllerEvent (channel, number, value), 0);
        buffer.clear();
        proc.processBlock (buffer, midi);
        run (waitMs);
    }

    // plays some audio through, and lets the panel timers run
    void run (int ms)
    {
        const double end = Time::getMillisecondCounterHiRes() + ms;
        int k = 0;
        while (Time::getMillisecondCounterHiRes() < end)
        {
            for (int i = 0; i < 128; ++i)
            {
                const float v = 0.2f * std::sin ((float) (k++) * 0.05f);
                buffer.setSample (0, i, v);
                buffer.setSample (1, i, v);
            }
            MidiBuffer none;
            proc.processBlock (buffer, none);
            MessageManager::getInstance()->runDispatchLoopUntil (3);
        }
    }

    float value (const char* id) { return proc.apvts.getParameter (id)->convertFrom0to1 (proc.apvts.getParameter (id)->getValue()); }
    int loopState() { return proc.getEngine().getLooper().uiState.load(); }
    void press (int sw, int holdMs = 60) { cc (20 + sw, 127, 16, holdMs); cc (20 + sw, 0); }
};
} // namespace

int main()
{
    ScopedJuceInitialiser_GUI juce;
    // test the defaults whatever this computer has saved, and put its settings back afterwards
    const auto settingsFile = PedalSettings::file();
    const bool hadSettings = settingsFile.existsAsFile();
    const String savedSettings = hadSettings ? settingsFile.loadFileAsString() : String();
    settingsFile.deleteFile();
    using L = spk::Looper;
    auto rig = std::make_unique<Rig>();
    auto& r = *rig;

    std::printf ("Clash test, original pedal map off\n");
    const float reverb = r.value (ParamIDs::reverbMode), fade = r.value (ParamIDs::fadeTime), repeats = r.value (ParamIDs::repeats);
    for (int ch : { 1, 5, 15 })
        for (int n : { 11, 20, 21, 22, 23 })
        {
            r.cc (n, 127, ch, 10);
            r.cc (n, 0, ch, 10);
        }
    expect (r.value (ParamIDs::reverbMode) == reverb, "CC 20 to 23 off the footswitch channel leave the reverb alone");
    expect (r.value (ParamIDs::fadeTime) == fade, "and the fade time");
    expect (r.value (ParamIDs::loopReverse) < 0.5f, "and the loop direction");
    expect (r.value (ParamIDs::repeats) == repeats, "CC 11 off the expression channel leaves Repeats alone");
    expect (r.value (ParamIDs::bypass) < 0.5f && r.loopState() == L::Empty, "nothing bypassed or recorded");

    std::printf ("Footswitch A\n");
    r.press (0);
    expect (r.value (ParamIDs::bypass) > 0.5f, "tap A bypasses");
    expect (r.value (ParamIDs::reverbMode) == reverb, "and doesn't touch the reverb (CC 20 used to)");
    r.press (0);
    expect (r.value (ParamIDs::bypass) < 0.5f, "tap A again switches the effect back on");
    r.cc (20, 127, 16, 700);
    expect (r.value (ParamIDs::reverse) > 0.5f, "hold A reverses while held");
    r.cc (20, 0);
    expect (r.value (ParamIDs::reverse) < 0.5f && r.value (ParamIDs::bypass) < 0.5f, "letting go turns reverse back off, no bypass");

    std::printf ("Footswitch B\n");
    r.press (1);
    expect (r.value (ParamIDs::hold) > 0.5f, "tap B freezes a beat");
    r.press (1);
    expect (r.value (ParamIDs::hold) < 0.5f, "tap B again lets go");
    r.cc (21, 127, 16, 700);
    expect (r.value (ParamIDs::hold) > 0.5f, "hold B holds while held");
    r.cc (21, 0);
    expect (r.value (ParamIDs::hold) < 0.5f, "and lets go with your foot");

    std::printf ("Footswitches C and D, the looper\n");
    r.press (2);
    expect (r.loopState() == L::Recording, "tap C starts recording");
    r.run (1000);
    r.press (2);
    expect (r.loopState() == L::Playing, "tap C again plays the loop");
    const float secs = (float) r.proc.getEngine().getLooper().length() / 48000.0f;
    expect (secs > 0.8f && secs < 1.4f, "the loop is as long as you played (" + String (secs, 2) + " s)");
    r.run (300);
    r.press (2);
    expect (r.loopState() == L::Overdubbing, "tap C overdubs");
    r.run (500);
    r.press (2);
    expect (r.loopState() == L::Playing, "tap C ends the overdub");
    r.press (3);
    r.run (100);
    expect (r.loopState() == L::Stopped || r.loopState() == L::Stopping, "tap D stops");
    r.press (3);
    expect (r.loopState() == L::Playing, "tap D again plays from the top");
    r.cc (23, 127, 16, 700);
    r.cc (23, 0);
    expect (r.loopState() == L::Empty, "hold D erases");
    r.cc (22, 127, 16, 40);
    r.cc (22, 0, 16, 40);
    r.cc (22, 127, 16, 40);
    r.cc (22, 0, 16, 40);
    expect (r.loopState() == L::Recording, "two quick taps don't close a tiny loop");
    r.cc (23, 127, 16, 700);
    r.cc (23, 0);

    std::printf ("Combos\n");
    const float fx = r.value (ParamIDs::effect);
    {
        MidiBuffer midi;
        midi.addEvent (MidiMessage::controllerEvent (16, 20, 127), 0);
        midi.addEvent (MidiMessage::controllerEvent (16, 21, 127), 1);
        r.buffer.clear();
        r.proc.processBlock (r.buffer, midi);
        r.run (60);
        r.cc (20, 0); r.cc (21, 0);
    }
    expect (r.value (ParamIDs::effect) == std::fmod (fx + 1.0f, (float) spk::kNumEffects), "A+B steps to the next effect");
    expect (r.value (ParamIDs::bypass) < 0.5f && r.value (ParamIDs::hold) < 0.5f, "without bypassing or holding");

    const int keptBefore = r.proc.lastKeeper();
    {
        MidiBuffer midi;
        midi.addEvent (MidiMessage::controllerEvent (16, 22, 127), 0);
        r.buffer.clear();
        r.proc.processBlock (r.buffer, midi);
        r.run (40);
        r.cc (23, 127, 16, 1300);
        r.cc (22, 0); r.cc (23, 0, 16, 600);
    }
    expect (r.proc.lastKeeper() > keptBefore, "hold C+D for a second saves a Keeper");
    expect (r.loopState() == L::Empty, "and C's press is taken back: no recording, no erase");

    std::printf ("Expression\n");
    r.cc (11, 0);
    expect (r.value (ParamIDs::activity) < 0.02f, "heel down: Activity at the bottom");
    r.cc (11, 127);
    expect (r.value (ParamIDs::activity) > 0.98f, "toe down: Activity at the top");
    expect (r.value (ParamIDs::repeats) == repeats, "Repeats untouched (CC 11 used to move it)");

    std::printf ("Original pedal MIDI map on channel 5\n");
    r.proc.pedals().pedalMapChannel = 5;
    r.cc (20, 127, 5);
    expect (r.value (ParamIDs::reverbMode) == 3.0f, "CC 20 on channel 5 picks the reverb, as on the pedal");
    r.cc (20, 0, 16);
    r.cc (20, 127, 16); r.cc (20, 0, 16);
    expect (r.value (ParamIDs::reverbMode) == 3.0f, "CC 20 on the footswitch channel is still footswitch A");
    expect (r.value (ParamIDs::bypass) > 0.5f, "and bypasses");
    r.cc (102, 127, 5);
    expect (r.value (ParamIDs::bypass) < 0.5f, "CC 102 on channel 5 engages the effect");

    rig.reset();
    if (hadSettings) settingsFile.replaceWithText (savedSettings);
    else settingsFile.deleteFile();
    std::printf (failures == 0 ? "All footswitch checks passed\n" : "%d footswitch check(s) failed\n", failures);
    return failures == 0 ? 0 : 1;
}
