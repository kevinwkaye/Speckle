# Speckle

A granular looper VST3 for Ableton Live, recreating the Hologram Electronics Microcosm pedal in the Flip Fella style, tuned to sound lush and clean.

## Install

1. Open the **builds** branch on GitHub and download `Speckle-Windows.zip`.
2. Unzip it and copy `Speckle.vst3` into `C:\Program Files\Common Files\VST3`.
3. In Ableton, open Preferences > Plug-Ins, turn on "Use VST3 Plug-In System Folders" and press Rescan.

`Speckle-standalone.exe` in the same zip runs without Ableton if you want to try it on its own.

## The effects

Turn the effect dial to pick one of eleven effects, then A to D for its variation. Fwd/Rev flips the playback direction.

| Group | Effect | A | B | C | D |
| --- | --- | --- | --- | --- | --- |
| Micro Loop | Mosaic | normal and double speed loops | normal and half speed | all double speed | half, normal, double and quad |
| | Seq | filtered, shuffled slices | normal and half speed bounce, pad at full Activity | overlapping layers with sweeps | crushed layers, crushed sub octave at full Activity |
| | Glide | half to normal | double to half | normal to double | both directions at once |
| Granules | Haze | short, stretched wash | many random grains | normal and double speed grains | normal and half speed grains |
| | Tunnel | drone that stretches and squeezes | sub octave drone with sweeps | resonant band-pass drone | drone that tightens after each note |
| | Strum | the last note, repeated | overlapping copies that phase | cascading recent notes | cascades with double speed grains |
| Glitch | Blocks | rearranged with bursts of runs | rearranged and pitch shifted | soft, filtered glitches | pitch shifted and crushed |
| | Interrupt | rearranged interruptions | pitch shifted | filter sweeps and echoes | crushed and drastic |
| | Arp | basic arpeggios of your notes | pitched patterns | a random filter per note | crushed |
| Multi Delay | Pattern | classic, even repeats | dotted | off-beat | triplet bursts |
| | Warp | envelope filter on each tap | resonant band-pass taps | octave and fifth shifted taps | taps fade into double speed grains |

## Controls

| Control | What it does |
| --- | --- |
| Activity | How much of the effect: loopers, grains, layers, taps or steps |
| Repeats | How long it carries on. At the top of the knob it never stops |
| Shape | A contour on every grain, from plucked to swelling. Glide uses it for the shape of the glide |
| Time | The subdivision, or the tempo itself in Tempo mode |
| Mod depth, Mod rate | Pitch modulation, from a gentle chorus to a seasick warble |
| Filter, Resonance | Resonant low-pass on the effect. Fully left mutes the effect, fully right is open |
| Space | The dotted echo and reverb, all wet at the top |
| Reverb | Bright Room, Dark Medium, Large Hall or Ambient |
| Mix | Dry and effect. Both are at full level at noon |
| Volume | The effect's level |
| Input | Guitar, Synth or Bass. Bass keeps everything under 120 Hz dry and tight |
| Tap, Sync | Tap a tempo, or follow Ableton's. Tapping switches Sync off |
| Hold | Freezes the last beat and keeps feeding it through the effect |
| Trails | When on, bypass lets the effect ring out |

## Phrase looper

Up to 60 seconds of stereo loop. Rec starts recording, press again to play, again to overdub. Hold Rec to undo the overdub. Stop stops the loop, hold Stop to erase it.

Loop speed steps from 1/4x to 4x in Subdiv mode and glides smoothly in Tempo mode. Post FX records what you hear, Pre FX sends the loop through the effect. Quantise trims the loop to the beat. Burst records only while you hold Rec. Loop Only mutes the effect but keeps mod, filter and space. The fade chip sets whether play and stop fade in, out or both, using Fade time.

## Presets

Sixteen slots in red, yellow, green and blue banks. Tap a slot to load it, hold it to save. Presets keep the loop too, with overdubs mixed down, and everything saves with your Ableton set.

## MIDI

Speckle follows the pedal's MIDI map, so a controller set up for a Microcosm works here. Program changes 0 to 43 pick the 44 effect variations and 44 to 59 load the user presets. CC 6 Activity, 7 Shape, 8 Filter, 9 Mix, 10 Time, 11 Repeats, 12 Space, 13 Loop level, 14 Mod rate, 15 Resonance, 16 Volume, 17 Loop speed, 19 Mod depth, 20 Reverb, 28 Rec, 31 Stop, 34 Erase, 35 Undo, 47 Reverse, 48 Hold, 93 Tap, 102 Bypass. Every control can also be MIDI-mapped in Ableton.

## Building

GitHub Actions builds the Windows VST3 on every push to `main` and publishes it to the `builds` branch. To build locally you need CMake 3.22+ and a C++17 compiler; JUCE downloads automatically.

```
cmake -S . -B build
cmake --build build --config Release
```

`speckle_render_test` plays a guitar phrase through every effect, variation, the looper and Hold, and fails on NaNs, clipping, clicks or silence. Pass a folder to write WAV files.
