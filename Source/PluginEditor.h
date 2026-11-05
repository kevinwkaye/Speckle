#pragma once

#include "PluginProcessor.h"
#include <juce_audio_utils/juce_audio_utils.h>

// Flip Fella palette, shared across all of Kevin's builds.
namespace ff
{
const juce::Colour bg        { 0xff050505 };
const juce::Colour face      { 0xff0d0d0d };
const juce::Colour housing   { 0xff060606 };
const juce::Colour card      { 0xff1b1b1b };
const juce::Colour cardHi    { 0xff252525 };
const juce::Colour cardLow   { 0xff141414 };
const juce::Colour ink       { 0xfff3f0e8 };
const juce::Colour dim       { 0xffa29e95 };
const juce::Colour faint     { 0xff5a5852 };
const juce::Colour line      { 0xff3a3a3a };
const juce::Colour ring      { 0xff1a1a1a };
const juce::Colour pin       { 0xff3c3c3c };
const juce::Colour red       { 0xffe8392f };
const juce::Colour teal      { 0xff3fd6c8 };
const juce::Colour coral     { 0xffff7a59 };
const juce::Colour amber     { 0xfff2c230 };
const juce::Colour blob      { 0xff4d5bff };
const juce::Colour blobShade { 0xff2329a8 };
const juce::Colour eye       { 0xffffffff };
const juce::Colour faceInk   { 0xff0b0b12 };
// the pedal's four preset banks
const juce::Colour bankR     { 0xffe8392f };
const juce::Colour bankY     { 0xfff2c230 };
const juce::Colour bankG     { 0xff4fcf6a };
const juce::Colour bankB     { 0xff4d8dff };

juce::Font font (float height, float kerning = 0.04f);
void label (juce::Graphics&, const juce::String& text, juce::Rectangle<int> r,
            juce::Justification j = juce::Justification::centredLeft, juce::Colour c = dim, float size = 13.0f);
} // namespace ff

namespace ui
{
// A split-flap card that flips like the Karlsson clock when its value changes.
class FlipCard : public juce::Component
{
public:
    void setValue (const juce::String& v);
    void setTextHeight (float h) { textHeight = h; }
    void advance (double dt);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent& e) override { if (onClick) onClick (e); }
    std::function<void (const juce::MouseEvent&)> onClick;

private:
    void drawHalf (juce::Graphics&, juce::Rectangle<float> card, const juce::String& text, bool top, float scale, bool shade);
    juce::String current, previous;
    float progress = 1.0f;
    float textHeight = 34.0f;
};

// Rounded pill buttons bound to a choice parameter.
class Pills : public juce::Component
{
public:
    Pills (juce::AudioProcessorValueTreeState&, const juce::String& paramId, juce::StringArray labels, float textSize = 13.0f);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;

private:
    juce::Rectangle<float> cell (int i) const;
    juce::StringArray labels;
    int selected = 0;
    float textSize;
    std::unique_ptr<juce::ParameterAttachment> attachment;
};

// A single pill that turns ink-white when its switch is on. It can show different words on and off.
class ToggleChip : public juce::Component
{
public:
    ToggleChip (juce::AudioProcessorValueTreeState&, const juce::String& paramId, const juce::String& text,
                const juce::String& onText = {}, bool fillWhenOn = true, float textSize = 13.0f);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    bool isOn() const { return on; }

private:
    juce::String text, onText;
    bool fill;
    float textSize;
    bool on = false;
    std::unique_ptr<juce::ParameterAttachment> attachment;
};

// A pill that steps through a choice parameter's options with each click.
class CycleChip : public juce::Component
{
public:
    CycleChip (juce::AudioProcessorValueTreeState&, const juce::String& paramId, juce::StringArray labels);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;

private:
    juce::StringArray labels;
    int value = 0;
    std::unique_ptr<juce::ParameterAttachment> attachment;
};

// A plain pill button that runs a function.
class ActionChip : public juce::Component
{
public:
    ActionChip (const juce::String& t, std::function<void()> fn, float textSize = 12.0f)
        : text (t), action (std::move (fn)), size (textSize) { setMouseCursor (juce::MouseCursor::PointingHandCursor); }
    void paint (juce::Graphics& g) override;
    void mouseDown (const juce::MouseEvent&) override { if (action) action(); }
    void setLit (bool l) { if (l != lit) { lit = l; repaint(); } }
    void setText (const juce::String& t) { if (t != text) { text = t; repaint(); } }

private:
    juce::String text;
    std::function<void()> action;
    float size;
    bool lit = false;
};

class FlipLookAndFeel : public juce::LookAndFeel_V4
{
public:
    void drawLinearSlider (juce::Graphics&, int x, int y, int w, int h, float pos, float, float,
                           juce::Slider::SliderStyle, juce::Slider&) override;
    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos, float start, float end, juce::Slider&) override;
};

// A knob drawn as a small clock face, with its name above and value below.
class Dial : public juce::Component
{
public:
    Dial (juce::AudioProcessorValueTreeState&, const juce::String& paramId, const juce::String& caption, bool redHand = false, bool secondary = false);
    void bindTo (const juce::String& paramId); // the Time knob swaps between subdivision and tempo
    void setValueText (const juce::String& t) { if (t != valueText) { valueText = t; repaint(); } }
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    juce::AudioProcessorValueTreeState& state;
    juce::Slider slider;
    juce::RangedAudioParameter* param = nullptr;
    juce::String caption, valueText;
    bool secondary = false;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
};

// A slim slider with its name on the left and value on the right.
class LabelledSlider : public juce::Component
{
public:
    LabelledSlider (juce::AudioProcessorValueTreeState&, const juce::String& paramId, const juce::String& caption);
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    juce::Slider slider;
    juce::RangedAudioParameter* param = nullptr;
    juce::String caption;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
};

// The hero control: all eleven effects round one Karlsson face, in four groups.
class EffectDial : public juce::Component
{
public:
    explicit EffectDial (SpeckleProcessor&);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    int current() const;

private:
    juce::Point<float> centre() const;
    float scale() const;
    int stopAt (juce::Point<float>) const;
    void pick (int stop);
    SpeckleProcessor& proc;
    juce::RangedAudioParameter* param = nullptr;
    int dragStart = 0;
};

// Rec / Dub and Stop. Tap to act; hold Rec to undo the overdub, hold Stop to erase.
class LoopButton : public juce::Component
{
public:
    LoopButton (SpeckleProcessor&, bool isRec);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    SpeckleProcessor& proc;
    bool rec;
    bool pressed = false;
};

// Where the loop's playhead is, coloured by what the looper is doing.
class LoopBar : public juce::Component
{
public:
    explicit LoopBar (SpeckleProcessor& p) : proc (p) {}
    void paint (juce::Graphics&) override;

private:
    SpeckleProcessor& proc;
};

class TapButton : public juce::Component
{
public:
    explicit TapButton (SpeckleProcessor& p) : proc (p) { setMouseCursor (juce::MouseCursor::PointingHandCursor); }
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;

private:
    SpeckleProcessor& proc;
    double flashUntil = 0;
};

// The big Hold and Bypass footswitches.
class FootSwitch : public juce::Component
{
public:
    FootSwitch (SpeckleProcessor&, const juce::String& paramId, bool isBypass);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;

private:
    SpeckleProcessor& proc;
    bool bypassStyle;
    bool on = false;
    std::unique_ptr<juce::ParameterAttachment> attachment;
};

// Sixteen user presets in the pedal's red, yellow, green and blue banks.
class PresetBanks : public juce::Component, private juce::Timer
{
public:
    explicit PresetBanks (SpeckleProcessor& p) : proc (p) { setMouseCursor (juce::MouseCursor::PointingHandCursor); }
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    void timerCallback() override;
    juce::Rectangle<float> cell (int i) const;
    SpeckleProcessor& proc;
    int pressed = -1, flashSlot = -1;
    double downAt = 0, flashUntil = 0;
    bool saved = false;
};

enum class View { Main, Pedal };

// One footswitch on the Pedal page: its jobs, what it listens for, Learn and Defaults.
class FootswitchCard : public juce::Component
{
public:
    FootswitchCard (SpeckleProcessor&, int sw);
    void paint (juce::Graphics&) override;
    void resized() override;
    void tick();

private:
    SpeckleProcessor& proc;
    int sw;
    FlipCard letter;
    ActionChip learn, reset;
    std::unique_ptr<ToggleChip> trails;
    juce::String sends;
};

// The Keep button: two lines, centred, and a right-click menu.
class BigButton : public juce::Component
{
public:
    std::function<void()> onDown, onRightClick;
    void set (const juce::String& main, const juce::String& small, juce::Colour border);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override { pressed = false; repaint(); }

private:
    juce::String mainText, smallText;
    juce::Colour borderColour = ff::line;
    bool pressed = false;
};

// Small A to D squares that act exactly like the footswitches, and light when one is pressed.
class MiniFoot : public juce::Component
{
public:
    MiniFoot (SpeckleProcessor& p, int s) : proc (p), sw (s) { setMouseCursor (juce::MouseCursor::PointingHandCursor); }
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override { proc.footDown (sw); }
    void mouseUp (const juce::MouseEvent&) override { proc.footUp (sw); }

private:
    SpeckleProcessor& proc;
    int sw;
};

// "FOOTSWITCHES": a dot that lights when the footswitch is heard, and a menu for where it's read from.
class FootInputLabel : public juce::Component
{
public:
    explicit FootInputLabel (SpeckleProcessor& p) : proc (p) { setMouseCursor (juce::MouseCursor::PointingHandCursor); }
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;

private:
    SpeckleProcessor& proc;
};

// The expression pedal's live position. Click for its target, Learn, Calibrate and the pedal's MIDI map.
class ExpressionMeter : public juce::Component
{
public:
    explicit ExpressionMeter (SpeckleProcessor& p) : proc (p) { setMouseCursor (juce::MouseCursor::PointingHandCursor); }
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;

private:
    SpeckleProcessor& proc;
};

// Pebble from Flip Fella, reacting to the effect, the footswitches and the expression pedal.
class Pebble : public juce::Component
{
public:
    explicit Pebble (SpeckleProcessor& p) : proc (p) {}
    void update (double now, double dt);
    void paint (juce::Graphics&) override;
    void nod() { nodding = 1.0f; }

private:
    SpeckleProcessor& proc;
    float level = 0, wet = 0, look = 0, mouth = 0, hop = 0, lean = 0, nodding = 0;
    bool asleep = false, blinking = false, held = false;
    int lastPulse = 0;
    double lastSound = 0, nextBlink = 2.0, blinkUntil = 0, now = 0;
};
} // namespace ui

class SpeckleEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit SpeckleEditor (SpeckleProcessor&);
    ~SpeckleEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void section (juce::Graphics&, juce::Rectangle<int>) const;
    void setView (ui::View);
    void keeperMenu();
    void closeNote (bool save);

    SpeckleProcessor& proc;
    ui::FlipLookAndFeel lnf;
    double lastTick = 0;
    ui::View view = ui::View::Main;
    std::vector<juce::Component*> mainPage;

    // header
    ui::Pills inputMode;
    ui::FlipCard tempoCard;
    ui::TapButton tapButton;
    ui::ToggleChip sync;
    ui::ActionChip viewMain, viewPedal;

    // pedal page
    std::array<std::unique_ptr<ui::FootswitchCard>, 4> footCards;

    // effect
    ui::FlipCard groupCard, effectCard;
    ui::EffectDial effectDial;
    ui::Pills variant;
    ui::ToggleChip reverse;

    // shape
    ui::Dial activity, repeats, shape, time;
    ui::LabelledSlider modDepth, modRate;
    ui::Pills timeMode;

    // tone and space
    ui::Dial filter, resonance, space, mix;
    ui::Pills reverbMode;

    // looper
    ui::Dial loopLevel, loopSpeed, fadeTime, volume;
    ui::LoopBar loopBar;
    ui::ToggleChip loopPre, quantise, loopReverse, burst, loopOnly;
    ui::CycleChip fadeMode;
    ui::LoopButton recButton, stopButton;

    // strip
    ui::Pebble pebble;
    ui::FootSwitch hold, bypass;
    ui::PresetBanks banks;
    std::array<std::unique_ptr<ui::MiniFoot>, 4> minis;
    ui::FootInputLabel footInput;
    ui::ExpressionMeter expression;

    // Keeper
    ui::BigButton keepButton;
    ui::FlipCard contextCard;
    juce::TextEditor noteEditor;
    int noteNumber = 0, lastKeepPulse = 0, lastEffect = -1;
    double keptUntil = 0.0, noteUntil = 0.0;

    int boundTimeMode = -1;
    juce::String timeHint, reverbName, loopStatus, mood, moodSub;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SpeckleEditor)
};
