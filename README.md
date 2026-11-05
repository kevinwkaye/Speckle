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
| Trails | When on, bypass lets the effect ring out. On the Pedal page, under footswitch A |
| Keep | Logs this moment for your signature suite: every setting plus the last 8 seconds of audio. Right-click for notes and Export |

## Phrase looper

Up to 60 seconds of stereo loop. Rec starts recording, press again to play, again to overdub. Hold Rec to undo the overdub. Stop stops the loop, hold Stop to erase it.

A loop is never shorter than a quarter of a second. Pressing Rec again sooner than that keeps recording, and a quick Stop or Burst tap throws the take away, so a double press can't leave a tiny loop humming like a pad.

Loop speed steps from 1/4x to 4x in Subdiv mode and glides smoothly in Tempo mode. Post FX records what you hear, Pre FX sends the loop through the effect. Quantise trims the loop to the beat. Burst records only while you hold Rec. Loop Only mutes the effect but keeps mod, filter and space. The fade chip sets whether play and stop fade in, out or both, using Fade time.

## Presets

Sixteen slots in red, yellow, green and blue banks. Tap a slot to load it, hold it to save. Presets keep the loop too, with overdubs mixed down, and everything saves with your Ableton set.

## Footswitches and expression (optional)

Everything works from the screen, so you don't need any pedals. If you have a MIDI footswitch:

1. Set its first four switches to send **momentary CC 20, 21, 22 and 23 on MIDI channel 16** (127 when pressed, 0 when released). Or use **Learn** on the Pedal page to use whatever yours sends.
2. Set an expression pedal input to **CC 11 on channel 16**, or click the expression bar and choose Learn.
3. Click the expression bar, choose **Calibrate heel and toe**, then rock the pedal from heel to toe once.

These settings are shared by every Flip Fella plugin on your computer, so you set them up once.

The footswitches follow the Microcosm's own: bypass on the left, the looper on the right.

| Footswitch | Tap | Hold |
| --- | --- | --- |
| A | Bypass | Reverse while held |
| B | Hold on or off | Hold while held |
| C | Loop record, play, overdub | Undo the overdub |
| D | Loop stop, or play from the top | Erase the loop |
| A+B | Next effect | |
| C+D | Hand the footswitches to the next Speckle | Hold a second: save a Keeper |

C acts the moment your foot lands, so loops start and end in time. With Burst on, C records only while you hold it. The A to D squares in the bottom strip do exactly what the footswitches do, and light while their job is on.

The expression pedal moves **Activity** to start with. Click the expression bar to send it to any knob: Repeats, Shape, Mix, Space, Loop Level and the rest.

### Getting footswitch MIDI to the plugin in Ableton

Speckle reads your footswitch straight from its USB cable, so every copy you load works with no MIDI track. The dot next to **FOOTSWITCHES** in the bottom strip turns green when it's connected, and Pebble says FOOTSWITCHES READY.

1. In Ableton's Preferences, Link Tempo MIDI, turn **Track**, **Sync** and **Remote** off for your footswitch's input. Windows lets only one program use a MIDI input at a time, so Ableton has to let go of it.
2. Load Speckle. That's it.

If the dot stays grey, click **FOOTSWITCHES**. The menu says what's wrong, and you can pick your footswitch from the list if it wasn't found automatically.

With several copies loaded, the footswitches drive the one you opened or clicked last. Tap C+D to hand them to the next copy.

Prefer routing through Ableton? Choose **Only from a MIDI track in Ableton** in that menu, then turn **Track** back on for the footswitch, make a MIDI track with **MIDI From** set to the footswitch and **MIDI To** set to Speckle, and arm it. The copy that track feeds takes the footswitches automatically.

### Kevin's setup (M-Vave Chocolate Plus and M-Audio EX-P)

In CubeSuite: footswitches A to D as momentary CC 20 to 23, the pedal jack set to expression input on CC 11. It always sends on channel 1 whatever CubeSuite says; read directly from its cable, Speckle accepts any channel. Put the Chocolate Plus switch on **U** and connect with the USB cable, not Bluetooth. The EX-P needs a 3.5 mm to 6.35 mm TRS adapter and reads backwards on the Chocolate Plus, which Calibrate fixes.

## Original pedal MIDI map

Click the expression bar, open **Original pedal MIDI map** and pick a channel. Speckle then answers the Microcosm's own CC chart and program changes on that channel, so a controller set up for the pedal works here. It is off by default: the chart uses CC 11 and CC 20 to 23, the same numbers as the footswitches and expression pedal.

Program changes 0 to 43 pick the 44 effect variations and 44 to 59 load the user presets. CC 6 Activity, 7 Shape, 8 Filter, 9 Mix, 10 Time, 11 Repeats, 12 Space, 13 Loop level, 14 Mod rate, 15 Resonance, 16 Volume, 17 Loop speed, 19 Mod depth, 20 Reverb, 21 Fade, 23 Loop reverse, 24 Pre FX, 25 Looper only, 26 Burst, 27 Quantise, 28 Rec, 31 Stop, 34 Erase, 35 Undo, 47 Reverse, 48 Hold, 93 Tap, 102 Bypass. Every control can also be MIDI-mapped in Ableton.

## Building

GitHub Actions builds the Windows VST3 on every push to `main` and publishes it to the `builds` branch. To build locally you need CMake 3.22+ and a C++17 compiler; JUCE downloads automatically.

```
cmake -S . -B build
cmake --build build --config Release
```

`speckle_render_test` plays a guitar phrase through every effect, variation, the looper and Hold, and fails on NaNs, clipping, clicks or silence. It also plays soft guitar, a pad with no attack and a phrase that starts after silence through every effect, and fails if any of them goes quiet. It ends with a CPU report at 48 kHz with 64 and 128 sample buffers. Pass a folder to write WAV files.

`speckle_pedal_test` drives the real plugin with footswitch MIDI and checks every job in the footswitch table, the combos, Keeper, the expression pedal, and that the original pedal map stays off the footswitch numbers.

GitHub Actions also builds a Linux ARM64 VST3 for the Raspberry Pi live rig and publishes it to the `builds-linux-arm64` branch.
