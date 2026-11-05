#include "PluginEditor.h"
#include "BinaryData.h"

using namespace juce;

// =====================================================================================
// Palette helpers
// =====================================================================================
namespace ff
{
Font font (float height, float kerning)
{
    static Typeface::Ptr typeface = Typeface::createSystemTypefaceFor (BinaryData::RobotoCondensedBold_ttf,
                                                                       BinaryData::RobotoCondensedBold_ttfSize);
    return Font (FontOptions (typeface).withHeight (height).withKerningFactor (kerning));
}

void label (Graphics& g, const String& text, Rectangle<int> r, Justification j, Colour c, float size)
{
    g.setColour (c);
    g.setFont (font (size, 0.08f));
    g.drawText (text, r, j, false);
}
} // namespace ff

namespace ui
{
static double nowSeconds() { return Time::getMillisecondCounterHiRes() * 0.001; }

static void setParam (RangedAudioParameter* p, float plain)
{
    if (p == nullptr) return;
    p->beginChangeGesture();
    p->setValueNotifyingHost (p->convertTo0to1 (plain));
    p->endChangeGesture();
}

static float plainOf (const RangedAudioParameter* p) { return p->convertFrom0to1 (p->getValue()); }

static void pill (Graphics& g, Rectangle<float> b, const String& text, bool on, float size, Colour onColour = ff::ink)
{
    const float r = b.getHeight() * 0.5f;
    if (on)
    {
        g.setColour (onColour);
        g.fillRoundedRectangle (b, r);
    }
    else
    {
        g.setColour (ff::line);
        g.drawRoundedRectangle (b.reduced (1.0f), r, 2.0f);
    }
    g.setColour (on ? ff::bg : ff::ink);
    g.setFont (ff::font (size, 0.06f));
    g.drawText (text, b, Justification::centred, false);
}

// =====================================================================================
// FlipCard
// =====================================================================================
void FlipCard::setValue (const String& v)
{
    if (v == current) return;
    previous = current;
    current = v;
    progress = previous.isEmpty() ? 1.0f : 0.0f;
    repaint();
}

void FlipCard::advance (double dt)
{
    if (progress < 1.0f)
    {
        progress = jmin (1.0f, progress + (float) (dt / 0.26));
        repaint();
    }
}

void FlipCard::drawHalf (Graphics& g, Rectangle<float> card, const String& text, bool top, float scale, bool shade)
{
    const float hinge = card.getCentreY();
    const float halfH = card.getHeight() * 0.5f;
    if (scale <= 0.001f) return;

    Graphics::ScopedSaveState save (g);
    auto clip = top ? Rectangle<float> (card.getX(), hinge - halfH * scale, card.getWidth(), halfH * scale)
                    : Rectangle<float> (card.getX(), hinge, card.getWidth(), halfH * scale);
    g.reduceClipRegion (clip.getSmallestIntegerContainer());
    g.addTransform (AffineTransform::scale (1.0f, scale, 0.0f, hinge));

    if (top) g.setGradientFill (ColourGradient (ff::cardHi, 0, card.getY(), ff::card, 0, hinge, false));
    else     g.setGradientFill (ColourGradient (ff::card, 0, hinge, ff::cardLow, 0, card.getBottom(), false));
    g.fillRoundedRectangle (card, 7.0f);
    g.setColour (ff::ink);
    g.setFont (ff::font (textHeight, 0.02f));
    g.drawFittedText (text, card.reduced (6.0f, 0.0f).toNearestInt(), Justification::centred, 1, 0.7f);

    if (shade)
    {
        g.setColour (Colours::black.withAlpha (0.45f * (1.0f - scale)));
        g.fillRoundedRectangle (card, 7.0f);
    }
}

void FlipCard::paint (Graphics& g)
{
    auto outer = getLocalBounds().toFloat();
    g.setColour (ff::housing);
    g.fillRoundedRectangle (outer, 10.0f);
    auto card = outer.reduced (6.0f);

    if (progress >= 1.0f)
    {
        drawHalf (g, card, current, true, 1.0f, false);
        drawHalf (g, card, current, false, 1.0f, false);
    }
    else
    {
        drawHalf (g, card, current, true, 1.0f, false);
        drawHalf (g, card, previous, false, 1.0f, false);
        const float eased = 1.0f - std::pow (1.0f - progress, 2.0f);
        if (eased < 0.5f) drawHalf (g, card, previous, true, 1.0f - eased * 2.0f, true);
        else              drawHalf (g, card, current, false, (eased - 0.5f) * 2.0f, true);
    }

    g.setColour (Colours::black);
    g.fillRect (card.getX(), card.getCentreY() - 1.5f, card.getWidth(), 3.0f);
    g.setColour (ff::pin);
    g.fillRoundedRectangle (outer.getX() + 1.0f, card.getCentreY() - 6.0f, 3.0f, 12.0f, 1.0f);
    g.fillRoundedRectangle (outer.getRight() - 4.0f, card.getCentreY() - 6.0f, 3.0f, 12.0f, 1.0f);
}

// =====================================================================================
// Pills and chips
// =====================================================================================
Pills::Pills (AudioProcessorValueTreeState& s, const String& paramId, StringArray l, float size)
    : labels (std::move (l)), textSize (size)
{
    setMouseCursor (MouseCursor::PointingHandCursor);
    attachment = std::make_unique<ParameterAttachment> (*s.getParameter (paramId), [this] (float v)
    {
        selected = (int) std::lround (v);
        repaint();
    });
    attachment->sendInitialUpdate();
}

Rectangle<float> Pills::cell (int i) const
{
    const float gap = 8.0f;
    const float w = ((float) getWidth() - gap * (float) (labels.size() - 1)) / (float) labels.size();
    return { (float) i * (w + gap), 0.0f, w, (float) getHeight() };
}

void Pills::paint (Graphics& g)
{
    for (int i = 0; i < labels.size(); ++i)
        pill (g, cell (i).reduced (1.0f), labels[i], i == selected, textSize);
}

void Pills::mouseDown (const MouseEvent& e)
{
    for (int i = 0; i < labels.size(); ++i)
        if (cell (i).contains (e.position))
        {
            attachment->setValueAsCompleteGesture ((float) i);
            return;
        }
}

ToggleChip::ToggleChip (AudioProcessorValueTreeState& s, const String& paramId, const String& t, const String& tOn, bool fillOn, float size)
    : text (t), onText (tOn.isEmpty() ? t : tOn), fill (fillOn), textSize (size)
{
    setMouseCursor (MouseCursor::PointingHandCursor);
    attachment = std::make_unique<ParameterAttachment> (*s.getParameter (paramId), [this] (float v) { on = v > 0.5f; repaint(); });
    attachment->sendInitialUpdate();
}

void ToggleChip::paint (Graphics& g)
{
    pill (g, getLocalBounds().toFloat().reduced (1.0f), on ? onText : text, on && fill, textSize);
}

void ToggleChip::mouseDown (const MouseEvent&)
{
    attachment->setValueAsCompleteGesture (on ? 0.0f : 1.0f);
}

CycleChip::CycleChip (AudioProcessorValueTreeState& s, const String& paramId, StringArray l) : labels (std::move (l))
{
    setMouseCursor (MouseCursor::PointingHandCursor);
    attachment = std::make_unique<ParameterAttachment> (*s.getParameter (paramId), [this] (float v) { value = (int) std::lround (v); repaint(); });
    attachment->sendInitialUpdate();
}

void CycleChip::paint (Graphics& g)
{
    pill (g, getLocalBounds().toFloat().reduced (1.0f), labels[value], false, 12.0f);
}

void CycleChip::mouseDown (const MouseEvent&)
{
    attachment->setValueAsCompleteGesture ((float) ((value + 1) % labels.size()));
}

// =====================================================================================
// Look and feel
// =====================================================================================
void FlipLookAndFeel::drawLinearSlider (Graphics& g, int x, int y, int w, int h, float pos, float, float,
                                        Slider::SliderStyle, Slider& s)
{
    const float cy = (float) y + (float) h * 0.5f;
    const float left = (float) x + 7.0f, right = (float) (x + w) - 7.0f;
    g.setColour (ff::ring);
    g.fillRoundedRectangle (left, cy - 3.0f, right - left, 6.0f, 3.0f);
    g.setColour (ff::ink.withAlpha (s.isMouseOverOrDragging() ? 1.0f : 0.85f));
    g.fillRoundedRectangle (left, cy - 3.0f, jmax (0.0f, pos - left), 6.0f, 3.0f);
    g.setColour (ff::ink);
    g.fillEllipse (pos - 7.0f, cy - 7.0f, 14.0f, 14.0f);
}

void FlipLookAndFeel::drawRotarySlider (Graphics& g, int x, int y, int w, int h, float pos, float start, float end, Slider& s)
{
    const float size = (float) jmin (w, h);
    const Point<float> c ((float) x + (float) w * 0.5f, (float) y + (float) h * 0.5f);
    const float r = size * 0.5f - 1.0f;

    g.setColour (ff::ring);
    g.fillEllipse (c.x - r, c.y - r, r * 2, r * 2);
    g.setColour (ff::bg);
    g.fillEllipse (c.x - r * 0.9f, c.y - r * 0.9f, r * 1.8f, r * 1.8f);

    g.setColour (ff::ink);
    for (int i = 0; i <= 10; ++i)
    {
        const float a = start + (end - start) * (float) i / 10.0f;
        const Point<float> p1 (c.x + std::sin (a) * r * 0.80f, c.y - std::cos (a) * r * 0.80f);
        const Point<float> p2 (c.x + std::sin (a) * r * 0.92f, c.y - std::cos (a) * r * 0.92f);
        g.drawLine ({ p1, p2 }, 2.0f);
    }

    const bool red = s.getProperties()["red"];
    g.setColour (red ? ff::red : ff::ink);
    const float a = start + pos * (end - start);
    Path hand;
    hand.addRoundedRectangle (-3.0f, -r * 0.68f, 6.0f, r * 0.68f + r * 0.15f, 2.0f);
    g.fillPath (hand, AffineTransform::rotation (a).translated (c));
    g.fillEllipse (c.x - 5.0f, c.y - 5.0f, 10.0f, 10.0f);
    g.setColour (ff::bg);
    g.fillEllipse (c.x - 1.8f, c.y - 1.8f, 3.6f, 3.6f);
}

// =====================================================================================
// Dial and slider
// =====================================================================================
Dial::Dial (AudioProcessorValueTreeState& s, const String& paramId, const String& c, bool redHand, bool sec)
    : state (s), caption (c), secondary (sec)
{
    slider.setSliderStyle (Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle (Slider::NoTextBox, false, 0, 0);
    slider.setRotaryParameters (MathConstants<float>::pi * 1.25f, MathConstants<float>::pi * 2.75f, true);
    slider.setMouseCursor (MouseCursor::PointingHandCursor);
    slider.getProperties().set ("red", redHand);
    slider.onValueChange = [this] { repaint(); };
    addAndMakeVisible (slider);
    bindTo (paramId);
}

void Dial::bindTo (const String& paramId)
{
    attachment.reset();
    param = state.getParameter (paramId);
    attachment = std::make_unique<AudioProcessorValueTreeState::SliderAttachment> (state, paramId, slider);
    slider.setDoubleClickReturnValue (true, (double) param->convertFrom0to1 (param->getDefaultValue()));
    repaint();
}

void Dial::resized()
{
    slider.setBounds (getLocalBounds().withTrimmedTop (20).withTrimmedBottom (24).withSizeKeepingCentre (80, 80));
}

void Dial::paint (Graphics& g)
{
    auto r = getLocalBounds();
    ff::label (g, caption.toUpperCase(), r.removeFromTop (18), Justification::centred, secondary ? ff::faint.brighter (0.4f) : ff::dim);
    const String value = valueText.isNotEmpty() ? valueText : param->getCurrentValueAsText();
    ff::label (g, value.toUpperCase(), r.removeFromBottom (20), Justification::centred, ff::ink, 15.0f);
}

LabelledSlider::LabelledSlider (AudioProcessorValueTreeState& s, const String& paramId, const String& c) : caption (c)
{
    param = s.getParameter (paramId);
    slider.setSliderStyle (Slider::LinearHorizontal);
    slider.setTextBoxStyle (Slider::NoTextBox, false, 0, 0);
    slider.setMouseCursor (MouseCursor::PointingHandCursor);
    slider.setDoubleClickReturnValue (true, (double) param->convertFrom0to1 (param->getDefaultValue()));
    addAndMakeVisible (slider);
    attachment = std::make_unique<AudioProcessorValueTreeState::SliderAttachment> (s, paramId, slider);
    slider.onValueChange = [this] { repaint(); };
}

void LabelledSlider::resized()
{
    slider.setBounds (getLocalBounds().withTrimmedTop (16));
}

void LabelledSlider::paint (Graphics& g)
{
    auto top = getLocalBounds().removeFromTop (16);
    ff::label (g, caption.toUpperCase(), top, Justification::centredLeft, ff::dim, 11.0f);
    ff::label (g, param->getCurrentValueAsText().toUpperCase(), top, Justification::centredRight, ff::ink, 11.0f);
}

// =====================================================================================
// EffectDial: eleven stops round a Karlsson face, grouped as on the pedal
// =====================================================================================
static constexpr float kStep = 270.0f / 10.0f;
static float stopDegrees (int i) { return -135.0f + kStep * (float) i; }

EffectDial::EffectDial (SpeckleProcessor& p) : proc (p)
{
    param = p.apvts.getParameter (ParamIDs::effect);
    setMouseCursor (MouseCursor::PointingHandCursor);
}

Point<float> EffectDial::centre() const { return { getWidth() * 0.5f, getHeight() * 140.0f / 252.0f }; }
float EffectDial::scale() const { return jmin ((float) getWidth() / 368.0f, (float) getHeight() / 252.0f); }
int EffectDial::current() const { return jlimit (0, spk::kNumEffects - 1, roundToInt (plainOf (param))); }

void EffectDial::paint (Graphics& g)
{
    const float s = scale();
    const auto c = centre();
    auto at = [&] (float deg, float r) { const float a = degreesToRadians (deg); return Point<float> (c.x + std::sin (a) * r * s, c.y - std::cos (a) * r * s); };
    const int sel = current();

    struct Arc { const char* name; int a, b; Justification j; };
    const Arc arcs[4] = { { "MICRO LOOP", 0, 2, Justification::centredRight }, { "GRANULES", 3, 5, Justification::centredRight },
                          { "GLITCH", 6, 8, Justification::centredLeft }, { "MULTI DELAY", 9, 10, Justification::centredLeft } };
    for (const auto& arc : arcs)
    {
        const bool lit = sel >= arc.a && sel <= arc.b;
        Path p;
        const float a0 = degreesToRadians (stopDegrees (arc.a) - 9.0f), a1 = degreesToRadians (stopDegrees (arc.b) + 9.0f);
        p.addCentredArc (c.x, c.y, 114.0f * s, 114.0f * s, 0.0f, a0, a1, true);
        g.setColour (lit ? ff::ink : ff::line);
        g.strokePath (p, PathStrokeType (3.0f * s, PathStrokeType::curved, PathStrokeType::rounded));

        const float mid = 0.5f * (stopDegrees (arc.a) + stopDegrees (arc.b));
        const auto lp = at (mid, 128.0f);
        Rectangle<float> box (lp.x - 45.0f * s, lp.y - 9.0f * s, 90.0f * s, 18.0f * s);
        if (arc.j == Justification::centredRight) box = box.withX (lp.x - 88.0f * s);
        if (arc.j == Justification::centredLeft) box = box.withX (lp.x - 2.0f * s);
        g.setColour (lit ? ff::ink : ff::dim);
        g.setFont (ff::font (12.0f * s, 0.08f));
        g.drawText (arc.name, box, arc.j, false);
    }

    g.setColour (ff::ring);
    g.fillEllipse (c.x - 106 * s, c.y - 106 * s, 212 * s, 212 * s);
    g.setColour (ff::bg);
    g.fillEllipse (c.x - 100 * s, c.y - 100 * s, 200 * s, 200 * s);

    static const char* stopNames[11] = { "MOSAIC", "SEQ", "GLIDE", "HAZE", "TUNNEL", "STRUM", "BLOCKS", "INTRPT", "ARP", "PATTN", "WARP" };
    for (int i = 0; i < spk::kNumEffects; ++i)
    {
        const float deg = stopDegrees (i);
        g.setColour (ff::ink);
        g.drawLine (Line<float> (at (deg, 92), at (deg, 102)), 4.0f * s);
        const auto lp = at (deg, 73);
        g.setColour (i == sel ? ff::ink : ff::dim);
        g.setFont (ff::font (11.0f * s, 0.02f));
        g.drawText (stopNames[i], Rectangle<float> (lp.x - 22 * s, lp.y - 8 * s, 44 * s, 16 * s), Justification::centred, false);
    }

    Path needle;
    needle.addRectangle (-1.4f * s, -58.0f * s, 2.8f * s, 76.0f * s);
    g.setColour (ff::red);
    g.fillPath (needle, AffineTransform::rotation (degreesToRadians (stopDegrees (sel))).translated (c));
    g.fillEllipse (c.x - 7 * s, c.y - 7 * s, 14 * s, 14 * s);
    g.setColour (ff::bg);
    g.fillEllipse (c.x - 2.6f * s, c.y - 2.6f * s, 5.2f * s, 5.2f * s);
}

int EffectDial::stopAt (Point<float> p) const
{
    const auto c = centre();
    const float dist = p.getDistanceFrom (c) / scale();
    if (dist < 30.0f || dist > 140.0f) return -1;
    const float deg = radiansToDegrees (std::atan2 (p.x - c.x, c.y - p.y));
    const int i = roundToInt ((deg + 135.0f) / kStep);
    return (i >= 0 && i < spk::kNumEffects) ? i : -1;
}

void EffectDial::pick (int stop)
{
    if (stop < 0 || stop == current()) return;
    setParam (param, (float) stop);
    repaint();
}

void EffectDial::mouseDown (const MouseEvent& e) { dragStart = current(); pick (stopAt (e.position)); }

void EffectDial::mouseDrag (const MouseEvent& e)
{
    if (std::abs (e.getDistanceFromDragStartY()) > 6)
        pick (jlimit (0, spk::kNumEffects - 1, roundToInt ((float) dragStart - (float) e.getDistanceFromDragStartY() / 22.0f)));
}

void EffectDial::mouseWheelMove (const MouseEvent&, const MouseWheelDetails& w)
{
    pick (jlimit (0, spk::kNumEffects - 1, current() + (w.deltaY > 0 ? 1 : -1)));
}

// =====================================================================================
// Looper controls
// =====================================================================================
LoopButton::LoopButton (SpeckleProcessor& p, bool isRec) : proc (p), rec (isRec)
{
    setMouseCursor (MouseCursor::PointingHandCursor);
}

void LoopButton::mouseDown (const MouseEvent&)
{
    pressed = true;
    if (rec) proc.recDown(); else proc.stopDown();
    repaint();
}

void LoopButton::mouseUp (const MouseEvent&)
{
    pressed = false;
    if (rec) proc.recUp(); else proc.stopUp();
    repaint();
}

void LoopButton::paint (Graphics& g)
{
    auto b = getLocalBounds().toFloat().reduced (1.0f);
    const float r = b.getHeight() * 0.5f;
    const int st = proc.getEngine().getLooper().uiState.load();
    using L = spk::Looper;
    Colour accent = ff::line;
    String text = rec ? "REC" : "STOP";
    if (rec)
    {
        if (st == L::Recording) { accent = ff::red; text = "RECORDING"; }
        else if (st == L::Overdubbing) { accent = ff::amber; text = "DUBBING"; }
        else if (st == L::Playing) { accent = ff::teal; text = "DUB"; }
        else if (st == L::Stopped || st == L::Stopping) text = "PLAY";
    }
    else if (st == L::Stopped) text = "HOLD: ERASE";

    g.setColour (pressed ? ff::card : ff::bg);
    g.fillRoundedRectangle (b, r);
    g.setColour (accent == ff::line ? (pressed ? ff::ink : ff::line) : accent);
    g.drawRoundedRectangle (b.reduced (1.0f), r, 2.0f);
    if (rec)
    {
        const float pulse = st == L::Recording ? 0.5f + 0.5f * (float) std::sin (nowSeconds() * 8.0) : 1.0f;
        g.setColour ((st == L::Empty || st == L::Stopped ? ff::red : accent).withAlpha (pulse));
        g.fillEllipse (b.getX() + 14.0f, b.getCentreY() - 4.0f, 8.0f, 8.0f);
    }
    else
    {
        g.setColour (ff::ink);
        g.fillRect (b.getX() + 14.0f, b.getCentreY() - 4.0f, 8.0f, 8.0f);
    }
    g.setColour (ff::ink);
    g.setFont (ff::font (13.0f, 0.06f));
    g.drawText (text, b.withTrimmedLeft (16.0f), Justification::centred, false);
}

void LoopBar::paint (Graphics& g)
{
    auto b = getLocalBounds().toFloat();
    const auto& lp = proc.getEngine().getLooper();
    const int st = lp.uiState.load();
    const float pos = jlimit (0.0f, 1.0f, lp.uiPos.load());
    using L = spk::Looper;
    g.setColour (ff::ring);
    g.fillRoundedRectangle (b, b.getHeight() * 0.5f);
    Colour c = ff::teal;
    if (st == L::Recording) c = ff::red;
    else if (st == L::Overdubbing) c = ff::amber;
    else if (st == L::Stopped || st == L::Empty) c = ff::faint;
    if (st != L::Empty)
    {
        g.setColour (c);
        g.fillRoundedRectangle (b.withWidth (jmax (b.getHeight(), b.getWidth() * pos)), b.getHeight() * 0.5f);
    }
}

void TapButton::mouseDown (const MouseEvent&)
{
    proc.tap();
    flashUntil = nowSeconds() + 0.12;
    repaint();
}

void TapButton::paint (Graphics& g)
{
    // the button pulses on the beat
    const float bpm = proc.currentBpm();
    const double beat = 60.0 / jmax (20.0f, bpm);
    const bool lit = nowSeconds() < flashUntil || std::fmod (nowSeconds(), beat) < 0.08;
    auto b = getLocalBounds().toFloat().reduced (1.0f);
    g.setColour (ff::line);
    g.drawRoundedRectangle (b.reduced (1.0f), b.getHeight() * 0.5f, 2.0f);
    g.setColour (lit ? ff::red : ff::red.withAlpha (0.25f));
    g.fillEllipse (b.getX() + 12.0f, b.getCentreY() - 4.0f, 8.0f, 8.0f);
    g.setColour (ff::ink);
    g.setFont (ff::font (13.0f, 0.06f));
    g.drawText ("TAP", b.withTrimmedLeft (14.0f), Justification::centred, false);
}

// =====================================================================================
// Footswitches
// =====================================================================================
FootSwitch::FootSwitch (SpeckleProcessor& p, const String& paramId, bool isBypass) : proc (p), bypassStyle (isBypass)
{
    setMouseCursor (MouseCursor::PointingHandCursor);
    attachment = std::make_unique<ParameterAttachment> (*p.apvts.getParameter (paramId), [this] (float v) { on = v > 0.5f; repaint(); });
    attachment->sendInitialUpdate();
}

void FootSwitch::mouseDown (const MouseEvent&) { attachment->setValueAsCompleteGesture (on ? 0.0f : 1.0f); }

void FootSwitch::paint (Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (1.0f);
    const bool lit = bypassStyle ? ! on : on;
    const Colour led = bypassStyle ? ff::red : ff::coral;
    g.setColour (ff::housing);
    g.fillRoundedRectangle (r, 12.0f);
    g.setColour (lit ? (bypassStyle ? ff::ink : ff::coral) : ff::line);
    g.drawRoundedRectangle (r.reduced (1.0f), 12.0f, 2.0f);

    const Point<float> p (r.getX() + 24.0f, r.getCentreY());
    if (lit)
    {
        g.setColour (led.withAlpha (0.22f));
        g.fillEllipse (p.x - 12.0f, p.y - 12.0f, 24.0f, 24.0f);
    }
    g.setColour (lit ? led : ff::pin);
    g.fillEllipse (p.x - 7.0f, p.y - 7.0f, 14.0f, 14.0f);

    auto text = r.withTrimmedLeft (44.0f);
    g.setColour (lit ? ff::ink : ff::dim);
    g.setFont (ff::font (19.0f, 0.08f));
    const String big = bypassStyle ? (on ? "BYPASSED" : "ON") : (on ? "HELD" : "HOLD");
    g.drawText (big, text.removeFromTop (r.getHeight() * 0.6f).withTrimmedTop (6.0f), Justification::bottomLeft, false);
    const bool tails = proc.apvts.getRawParameterValue (ParamIDs::trails)->load() > 0.5f;
    const String small = bypassStyle ? (tails ? "TRAILS ON" : "TRAILS OFF") : (on ? "TAP TO RELEASE" : "FREEZE A BEAT");
    ff::label (g, small, text.toNearestInt(), Justification::topLeft, ff::dim, 11.0f);
    // which footswitch does the same job
    ff::label (g, bypassStyle ? "A" : "B", r.removeFromRight (14.0f).removeFromTop (18.0f).translated (-6.0f, 4.0f).toNearestInt(),
               Justification::centred, ff::faint.brighter (0.3f), 11.0f);
}

// =====================================================================================
// Presets
// =====================================================================================
Rectangle<float> PresetBanks::cell (int i) const
{
    const int bank = i / 4, k = i % 4;
    return { (float) (3 + bank * 112 + k * 26), 3.0f, 22.0f, 22.0f };
}

void PresetBanks::paint (Graphics& g)
{
    static const Colour banks[4] = { ff::bankR, ff::bankY, ff::bankG, ff::bankB };
    const int sel = proc.selectedPreset();
    for (int i = 0; i < SpeckleProcessor::kPresets; ++i)
    {
        auto c = cell (i);
        const Colour col = banks[i / 4];
        const bool flash = i == flashSlot && nowSeconds() < flashUntil;
        if (proc.hasPreset (i) || flash)
        {
            g.setColour (flash ? ff::ink : col);
            g.fillRoundedRectangle (c, 6.0f);
            if (proc.presetHasLoop (i) && ! flash)
            {
                g.setColour (ff::bg.withAlpha (0.55f));
                g.fillEllipse (c.getCentreX() - 3.0f, c.getCentreY() - 3.0f, 6.0f, 6.0f);
            }
        }
        else
        {
            g.setColour (col.withAlpha (0.45f));
            g.drawRoundedRectangle (c.reduced (1.0f), 6.0f, 2.0f);
        }
        if (i == sel)
        {
            g.setColour (ff::ink);
            g.drawRoundedRectangle (c.expanded (2.5f), 8.0f, 1.5f);
        }
    }
    ff::label (g, flashUntil > nowSeconds() ? String ("SAVED") : String::fromUTF8 ("TAP TO LOAD · HOLD TO SAVE"), { 0, 32, 440, 16 }, Justification::centredLeft, ff::faint.brighter (0.4f), 11.0f);
}

void PresetBanks::mouseDown (const MouseEvent& e)
{
    pressed = -1;
    for (int i = 0; i < SpeckleProcessor::kPresets; ++i)
        if (cell (i).expanded (3.0f).contains (e.position)) pressed = i;
    downAt = nowSeconds();
    saved = false;
    startTimer (40);
}

void PresetBanks::timerCallback()
{
    // hold to save, like holding the pedal's selector
    if (pressed >= 0 && ! saved && nowSeconds() - downAt > 0.7)
    {
        proc.savePreset (pressed);
        saved = true;
        flashSlot = pressed;
        flashUntil = nowSeconds() + 0.9;
    }
    if (pressed < 0 && nowSeconds() > flashUntil)
        stopTimer();
    repaint();
}

void PresetBanks::mouseUp (const MouseEvent&)
{
    if (pressed >= 0 && ! saved) proc.loadPreset (pressed);
    pressed = -1;
    repaint();
}

// =====================================================================================
// Footswitches and expression
// =====================================================================================
void ActionChip::paint (Graphics& g) { pill (g, getLocalBounds().toFloat().reduced (1.0f), text, lit, size); }

FootswitchCard::FootswitchCard (SpeckleProcessor& p, int s)
    : proc (p), sw (s),
      learn ("LEARN", [this] { proc.learnFootswitch (sw); }),
      reset ("DEFAULTS", [this] { proc.resetFootswitches(); })
{
    letter.setTextHeight (54.0f);
    letter.setValue (String::charToString ((juce_wchar) ('A' + s)));
    addAndMakeVisible (letter);
    addAndMakeVisible (learn);
    addAndMakeVisible (reset);
    if (s == 0)
    {
        trails = std::make_unique<ToggleChip> (p.apvts, ParamIDs::trails, "TRAILS OFF", "TRAILS ON", true, 12.0f);
        addAndMakeVisible (*trails);
    }
}

void FootswitchCard::resized()
{
    letter.setBounds (16, 42, 258, 96);
    if (trails) trails->setBounds (16, 304, 124, 28);
    learn.setBounds (16, 390, 124, 34);
    reset.setBounds (150, 390, 124, 34);
}

void FootswitchCard::paint (Graphics& g)
{
    g.setColour (ff::face);
    g.fillRoundedRectangle (getLocalBounds().toFloat(), 12.0f);
    ff::label (g, "FOOTSWITCH", { 16, 14, 258, 18 });

    const bool burst = proc.apvts.getRawParameterValue (ParamIDs::burst)->load() > 0.5f;
    static const char* const tapJobs[4] = { "BYPASS", "HOLD ON / OFF", "REC / PLAY / DUB", "STOP / PLAY" };
    static const char* const holdJobs[4] = { "REVERSE WHILE HELD", "HOLD WHILE HELD", "UNDO OVERDUB", "ERASE LOOP" };
    static const char* const notes[4] = { "BYPASS LETS THE EFFECT RING OUT", "FREEZES THE LAST BEAT", "ACTS THE MOMENT YOUR FOOT LANDS", "STOPPED: TAP TO PLAY FROM THE TOP" };
    static const char* const combos[4] = { "A+B  NEXT EFFECT", "A+B  NEXT EFFECT", "C+D  NEXT SPECKLE, HOLD 1 S TO KEEP", "C+D  NEXT SPECKLE, HOLD 1 S TO KEEP" };
    struct Row { const char* k; String v; };
    const Row rows[3] = { { "TAP", sw == 2 && burst ? String ("RECORD WHILE HELD") : String (tapJobs[sw]) },
                          { "HOLD", sw == 2 && burst ? String ("BURST IS ON") : String (holdJobs[sw]) },
                          { "LISTENS FOR", sends } };
    int y = 150;
    for (const auto& r : rows)
    {
        g.setColour (ff::ring);
        g.fillRect (16, y, 258, 2);
        ff::label (g, r.k, { 16, y + 4, 120, 30 }, Justification::centredLeft, ff::dim, 13.0f);
        ff::label (g, r.v, { 110, y + 4, 164, 30 }, Justification::centredRight, ff::ink, 14.0f);
        y += 38;
    }
    g.setColour (ff::ring);
    g.fillRect (16, y, 258, 2);
    ff::label (g, notes[sw], { 16, y + 8, 258, 18 }, Justification::centredLeft, ff::dim, 11.0f);
    ff::label (g, combos[sw], { 16, y + (sw == 0 ? 70 : 32), 258, 18 }, Justification::centredLeft, ff::dim, 11.0f);
}

void FootswitchCard::tick()
{
    const auto& st = proc.pedals();
    const String t = proc.pedalsDirect() ? "CC " + String (st.fsCC[(size_t) sw]) + String::fromUTF8 (" · ANY CH")
                                         : "CH " + String (st.fsChannel[(size_t) sw]) + String::fromUTF8 (" · CC ") + String (st.fsCC[(size_t) sw]);
    if (t != sends) { sends = t; repaint(); }
    learn.setLit (proc.learningWhat() == sw);
    learn.setText (proc.learningWhat() == sw ? "PRESS IT NOW" : "LEARN");
    const bool down = (proc.uiFootDown.load() >> sw) & 1;
    letter.setAlpha (down ? 0.6f : 1.0f);
}

void BigButton::set (const String& m, const String& sm, Colour c)
{
    if (m == mainText && sm == smallText && c == borderColour) return;
    mainText = m; smallText = sm; borderColour = c;
    repaint();
}

void BigButton::paint (Graphics& g)
{
    auto b = getLocalBounds().toFloat().reduced (1.0f);
    g.setColour (pressed ? ff::ring : ff::bg);
    g.fillRoundedRectangle (b, b.getHeight() * 0.5f);
    g.setColour (borderColour);
    g.drawRoundedRectangle (b, b.getHeight() * 0.5f, 2.0f);
    const float big = 13.0f, small = 9.5f;
    const float top = b.getCentreY() - (big + small + 1.0f) * 0.5f;
    ff::label (g, mainText, Rectangle<float> (b.getX(), top, b.getWidth(), big).toNearestInt(), Justification::centred, ff::ink, big);
    ff::label (g, smallText, Rectangle<float> (b.getX(), top + big + 1.0f, b.getWidth(), small).toNearestInt(), Justification::centred, ff::dim, small);
}

void BigButton::mouseDown (const MouseEvent& e)
{
    if (e.mods.isPopupMenu()) { if (onRightClick) onRightClick(); return; }
    pressed = true; repaint();
    if (onDown) onDown();
}

void MiniFoot::paint (Graphics& g)
{
    const bool down = (proc.uiFootDown.load() >> sw) & 1;
    auto b = getLocalBounds().toFloat().reduced (1.0f);
    g.setColour (down ? ff::ink : ff::bg);
    g.fillRoundedRectangle (b, 7.0f);
    // lit while its job is on: A the effect, B Hold, C a recording or overdub, D a loop playing
    const auto& lp = proc.getEngine().getLooper();
    const int st = lp.uiState.load();
    using L = spk::Looper;
    bool lit = false;
    Colour col = ff::line;
    if (sw == 0) { lit = proc.apvts.getRawParameterValue (ParamIDs::bypass)->load() < 0.5f; col = ff::red; }
    if (sw == 1) { lit = proc.apvts.getRawParameterValue (ParamIDs::hold)->load() > 0.5f; col = ff::coral; }
    if (sw == 2) { lit = st == L::Recording || st == L::Overdubbing; col = st == L::Recording ? ff::red : ff::amber; }
    if (sw == 3) { lit = st == L::Playing || st == L::Overdubbing; col = ff::teal; }
    g.setColour (down ? ff::ink : (lit ? col : ff::line));
    g.drawRoundedRectangle (b.reduced (lit ? 1.0f : 0.0f), 7.0f, lit ? 3.0f : 2.0f);
    ff::label (g, String::charToString ((juce_wchar) ('A' + sw)), getLocalBounds(), Justification::centred, down ? ff::bg : ff::ink, 14.0f);
}

void FootInputLabel::paint (Graphics& g)
{
    // a dot that lights when the footswitch is plugged in and heard directly
    g.setColour (proc.pedalsDirect() ? ff::bankG : ff::ring);
    g.fillEllipse (0.0f, (float) getHeight() * 0.5f - 4.0f, 8.0f, 8.0f);
    ff::label (g, "FOOTSWITCHES  v", getLocalBounds().withTrimmedLeft (14), Justification::centredLeft, ff::dim, 12.0f);
}

void FootInputLabel::mouseDown (const MouseEvent&)
{
    PopupMenu m;
    m.addSectionHeader ("Footswitch input");
    m.addItem (-1, proc.footInputStatus(), false, false);
    const auto chosen = proc.footDevice();
    m.addItem (10, "Find my footswitch automatically", true, chosen.isEmpty());
    const auto devices = DirectMidiInput::deviceNames();
    for (int i = 0; i < devices.size() && i < 40; ++i)
        m.addItem (100 + i, "Use " + devices[i], true, chosen == devices[i]);
    m.addItem (11, "Only from a MIDI track in Ableton", true, chosen == "none");
    m.showMenuAsync (PopupMenu::Options().withTargetComponent (this), [this, devices] (int r)
    {
        if (r == 10) proc.setFootDevice ({});
        else if (r == 11) proc.setFootDevice ("none");
        else if (r >= 100 && r - 100 < devices.size()) proc.setFootDevice (devices[r - 100]);
        repaint();
    });
}

void ExpressionMeter::paint (Graphics& g)
{
    const auto& t = names::expTargets();
    const int target = jlimit (0, (int) t.size() - 1, roundToInt (proc.apvts.getRawParameterValue (ParamIDs::expTarget)->load()));
    String text = String::fromUTF8 ("EXP · ") + String (t[(size_t) target].label).toUpperCase() + "  v";
    if (proc.isCalibrating()) text = "ROCK HEEL TO TOE";
    else if (proc.learningWhat() == SpeckleProcessor::LearnExpression) text = "MOVE THE PEDAL";
    ff::label (g, text, { 0, 0, getWidth(), 16 }, Justification::centredLeft, proc.isCalibrating() ? ff::ink : ff::dim, 11.0f);
    const float v = proc.expressionLevel();
    const float x0 = 8.0f, w = (float) getWidth() - 16.0f, y = 24.0f;
    g.setColour (ff::ring);
    g.fillRoundedRectangle (x0, y, w, 8.0f, 4.0f);
    const bool fresh = nowSeconds() - proc.lastExpressionMove() < 1.5;
    g.setColour (fresh ? ff::ink : ff::dim);
    g.fillRoundedRectangle (x0, y, w * v, 8.0f, 4.0f);
    g.fillEllipse (x0 + w * v - 8.0f, y - 4.0f, 16.0f, 16.0f);
}

void ExpressionMeter::mouseDown (const MouseEvent&)
{
    PopupMenu targets;
    const auto& t = names::expTargets();
    const int target = roundToInt (proc.apvts.getRawParameterValue (ParamIDs::expTarget)->load());
    for (int i = 0; i < (int) t.size(); ++i) targets.addItem (100 + i, t[(size_t) i].label, true, i == target);
    PopupMenu curves;
    const char* const curveNames[3] = { "Linear", "Slow start", "Fast start" };
    for (int i = 0; i < 3; ++i) curves.addItem (200 + i, curveNames[i], true, proc.pedals().curve == i);
    PopupMenu map;
    map.addItem (300, "Off", true, proc.pedals().pedalMapChannel == 0);
    for (int ch = 1; ch <= 16; ++ch) map.addItem (300 + ch, "Channel " + String (ch), true, proc.pedals().pedalMapChannel == ch);

    PopupMenu m;
    m.addSubMenu ("Expression moves", targets);
    m.addItem (1, "Calibrate heel and toe");
    m.addItem (2, "Learn expression pedal");
    m.addSubMenu ("Curve", curves);
    m.addSeparator();
    m.addSubMenu ("Original pedal MIDI map", map);
    m.showMenuAsync (PopupMenu::Options().withTargetComponent (this), [this] (int r)
    {
        if (r == 1) proc.calibrateExpression();
        else if (r == 2) proc.learnExpression();
        else if (r >= 100 && r < 200) setParam (proc.apvts.getParameter (ParamIDs::expTarget), (float) (r - 100));
        else if (r >= 200 && r < 300) { proc.pedals().curve = r - 200; proc.pedals().save(); }
        else if (r >= 300) { proc.pedals().pedalMapChannel = r - 300; proc.pedals().save(); }
        repaint();
    });
}

// =====================================================================================
// Pebble
// =====================================================================================
void Pebble::update (double t, double dt)
{
    now = t;
    auto& eng = proc.getEngine();
    level = eng.uiInput.load();
    wet = eng.uiWet.load();
    held = proc.apvts.getRawParameterValue (ParamIDs::hold)->load() > 0.5f;
    const bool bypassed = proc.apvts.getRawParameterValue (ParamIDs::bypass)->load() > 0.5f;
    if (level > 0.004f || wet > 0.01f) lastSound = t;
    asleep = bypassed || t - lastSound > 6.0;

    if (eng.uiOnset.load())
    {
        eng.uiOnset.store (false);
        hop = jmax (hop, 0.5f);
    }
    // taps its foot whenever a footswitch goes down
    const int pulse = proc.uiFootPulse.load();
    if (pulse != lastPulse) { lastPulse = pulse; hop = 1.0f; }
    hop *= (float) std::exp (-dt / 0.12);
    nodding *= (float) std::exp (-dt / 0.35);
    // leans with the expression pedal while it moves
    const bool expFresh = t - proc.lastExpressionMove() < 2.0;
    const float leanTarget = expFresh ? (proc.expressionLevel() - 0.5f) * 16.0f : 0.0f;
    lean += (leanTarget - lean) * (float) (1.0 - std::exp (-dt / 0.1));
    // eyes drift slowly side to side, following the grains
    const float lookTarget = (float) std::sin (t * 0.7) * jlimit (0.0f, 1.0f, wet * 4.0f);
    look += (lookTarget - look) * (float) (1.0 - std::exp (-dt / 0.2));
    const float mouthTarget = jlimit (0.0f, 1.0f, (wet - 0.02f) * 4.0f);
    mouth += (mouthTarget - mouth) * (float) (1.0 - std::exp (-dt / 0.06));

    if (t > nextBlink)
    {
        blinkUntil = t + 0.13;
        nextBlink = t + 2.5 + Random::getSystemRandom().nextDouble() * 3.0;
    }
    blinking = t < blinkUntil;
    repaint();
}

void Pebble::paint (Graphics& g)
{
    // Glows teal while the effect sounds, coral while Hold has frozen a beat, dozes when bypassed.
    auto a = getLocalBounds().toFloat();
    const float s = a.getWidth() / 140.0f;
    const float bob = asleep ? 0.0f : 1.5f * (float) std::sin (now * 2.4);
    const float breathe = asleep ? 1.0f + 0.025f * (float) std::sin (now * 1.6) : 1.0f;
    const float dy = bob - hop * 10.0f + 8.0f * nodding * (float) std::sin (nodding * 12.0f); // a happy nod for KEPT
    const float tilt = degreesToRadians (lean);
    const auto tf = AffineTransform::translation (-100.0f, -100.0f).scaled (s * breathe, s / breathe)
                        .rotated (tilt, 0.0f, 50.0f * s).translated (a.getCentreX(), a.getCentreY() + 6.0f * s);
    auto ell = [&] (float cx, float cy, float rx, float ry, Colour col)
    {
        Path p; p.addEllipse (cx - rx, cy - ry, rx * 2, ry * 2);
        g.setColour (col); g.fillPath (p, tf);
    };
    auto stroke = [&] (Path& p, float w, Colour col = ff::faceInk)
    {
        g.setColour (col);
        g.strokePath (p, PathStrokeType (w, PathStrokeType::curved, PathStrokeType::rounded), tf);
    };

    const float glow = held ? 1.0f : jlimit (0.0f, 1.0f, wet * 3.0f);
    if (glow > 0.02f && ! asleep)
    {
        Path ringPath; ringPath.addEllipse (100 - 66, 100 - 56 + dy, 132, 112);
        stroke (ringPath, 5.0f, (held ? ff::coral : ff::teal).withAlpha (0.65f * glow));
    }

    ell (107, 109 + dy, 58, 48, ff::blobShade);
    ell (100, 100 + dy, 58, 48, ff::blob);

    if (asleep || blinking)
    {
        for (float ex : { 80.0f, 120.0f })
        {
            Path lid; lid.startNewSubPath (ex - 12, 94 + dy); lid.quadraticTo (ex, 102 + dy, ex + 12, 94 + dy);
            stroke (lid, 6.0f);
        }
    }
    else
    {
        for (float ex : { 80.0f, 120.0f })
        {
            ell (ex, 92 + dy, 14.0f, 14.0f, ff::eye);
            ell (ex + look * 5.0f, 94 + dy, 7.0f, 7.0f, ff::faceInk);
        }
    }

    if (asleep)
    {
        Path m; m.startNewSubPath (94, 124 + dy); m.lineTo (106, 124 + dy);
        stroke (m, 6.0f);
    }
    else if (held) // holding its breath
        ell (100, 123 + dy, 6.0f, 6.0f, ff::faceInk);
    else if (mouth > 0.15f)
        ell (100, 124 + dy, 7.0f + 3.0f * mouth, 4.0f + 8.0f * mouth, ff::faceInk);
    else
    {
        Path m; m.startNewSubPath (90, 120 + dy); m.quadraticTo (100, 130 + dy, 110, 120 + dy);
        stroke (m, 6.0f);
    }

    if (asleep)
    {
        g.setFont (ff::font (15.0f, 0.0f));
        for (int k = 0; k < 3; ++k)
        {
            const float ph = (float) std::fmod (now * 0.5 + k / 3.0, 1.0);
            g.setColour (ff::ink.withAlpha (1.0f - ph));
            g.drawText ("z", Rectangle<float> (a.getRight() - 16.0f + ph * 8.0f, a.getY() + 8.0f - ph * 14.0f, 16.0f, 16.0f),
                        Justification::centred, false);
        }
    }
}
} // namespace ui

// =====================================================================================
// Editor
// =====================================================================================
using namespace ui;

SpeckleEditor::SpeckleEditor (SpeckleProcessor& p)
    : AudioProcessorEditor (&p), proc (p),
      inputMode (p.apvts, ParamIDs::inputMode, { "GUITAR", "SYNTH", "BASS" }),
      tapButton (p),
      sync (p.apvts, ParamIDs::sync, "SYNC"),
      viewMain ("MAIN", [this] { setView (View::Main); }, 13.0f),
      viewPedal ("PEDAL", [this] { setView (View::Pedal); }, 13.0f),
      effectDial (p),
      variant (p.apvts, ParamIDs::variant, { "A", "B", "C", "D" }, 15.0f),
      reverse (p.apvts, ParamIDs::reverse, "FWD", "REV", true, 15.0f),
      activity (p.apvts, ParamIDs::activity, "Activity"),
      repeats (p.apvts, ParamIDs::repeats, "Repeats"),
      shape (p.apvts, ParamIDs::shape, "Shape"),
      time (p.apvts, ParamIDs::subdiv, "Time"),
      modDepth (p.apvts, ParamIDs::modDepth, "Mod depth"),
      modRate (p.apvts, ParamIDs::modRate, "Mod rate"),
      timeMode (p.apvts, ParamIDs::timeMode, { "SUBDIV", "TEMPO" }),
      filter (p.apvts, ParamIDs::filter, "Filter"),
      resonance (p.apvts, ParamIDs::resonance, "Resonance", false, true),
      space (p.apvts, ParamIDs::space, "Space"),
      mix (p.apvts, ParamIDs::mix, "Mix", true),
      reverbMode (p.apvts, ParamIDs::reverbMode, { "ROOM", "DARK", "HALL", "AMB" }),
      loopLevel (p.apvts, ParamIDs::loopLevel, "Loop level"),
      loopSpeed (p.apvts, ParamIDs::loopSpeed, "Loop speed", false, true),
      fadeTime (p.apvts, ParamIDs::fadeTime, "Fade time"),
      volume (p.apvts, ParamIDs::volume, "Volume", false, true),
      loopBar (p),
      loopPre (p.apvts, ParamIDs::loopPre, "POST FX", "PRE FX", false, 12.0f),
      quantise (p.apvts, ParamIDs::quantise, "QUANTISE", {}, true, 12.0f),
      loopReverse (p.apvts, ParamIDs::loopReverse, "REVERSE", {}, true, 12.0f),
      burst (p.apvts, ParamIDs::burst, "BURST", {}, true, 12.0f),
      loopOnly (p.apvts, ParamIDs::loopOnly, "LOOP ONLY", {}, true, 12.0f),
      fadeMode (p.apvts, ParamIDs::fadeMode, { "FADE I/O", "FADE IN", "FADE OUT" }),
      recButton (p, true), stopButton (p, false),
      pebble (p),
      hold (p, ParamIDs::hold, false),
      bypass (p, ParamIDs::bypass, true),
      banks (p),
      footInput (p),
      expression (p)
{
    setLookAndFeel (&lnf);
    mainPage = { &groupCard, &effectCard, &effectDial, &variant, &reverse,
                 &activity, &repeats, &shape, &time, &modDepth, &modRate, &timeMode,
                 &filter, &resonance, &space, &mix, &reverbMode,
                 &loopLevel, &loopSpeed, &fadeTime, &volume, &loopBar, &loopPre, &quantise, &loopReverse, &burst, &loopOnly, &fadeMode,
                 &recButton, &stopButton };
    for (auto* c : mainPage) addAndMakeVisible (c);
    for (auto* c : std::initializer_list<Component*> { &inputMode, &tempoCard, &tapButton, &sync, &viewMain, &viewPedal,
                                                       &pebble, &hold, &bypass, &banks, &footInput, &expression, &keepButton, &contextCard })
        addAndMakeVisible (c);
    for (int k = 0; k < 4; ++k)
    {
        footCards[(size_t) k] = std::make_unique<FootswitchCard> (p, k);
        addChildComponent (*footCards[(size_t) k]);
        minis[(size_t) k] = std::make_unique<MiniFoot> (p, k);
        addAndMakeVisible (*minis[(size_t) k]);
    }

    tempoCard.setTextHeight (22.0f);
    tempoCard.setMouseCursor (MouseCursor::PointingHandCursor);
    tempoCard.onClick = [this] (const MouseEvent&) { proc.tap(); };
    groupCard.setTextHeight (32.0f);
    effectCard.setTextHeight (30.0f);

    // Keeper: click to log the moment, right-click for notes, export and the folder
    keepButton.set ("KEEP", "HOLD C+D", ff::line);
    keepButton.onDown = [this] { proc.keepNow(); };
    keepButton.onRightClick = [this] { keeperMenu(); };
    contextCard.setTextHeight (12.0f);
    contextCard.setMouseCursor (MouseCursor::PointingHandCursor);
    contextCard.setValue (proc.keeperLive() ? "LIVE" : "STUDIO");
    contextCard.onClick = [this] (const MouseEvent&)
    {
        proc.setKeeperLive (! proc.keeperLive());
        contextCard.setValue (proc.keeperLive() ? "LIVE" : "STUDIO");
        proc.say (proc.keeperLive() ? "LIVE" : "STUDIO");
    };
    addChildComponent (noteEditor);
    noteEditor.setTextToShowWhenEmpty ("Optional, Enter to save", ff::faint);
    noteEditor.setFont (FontOptions (13.0f));
    noteEditor.setIndents (10, 7);
    noteEditor.setColour (TextEditor::backgroundColourId, ff::bg);
    noteEditor.setColour (TextEditor::textColourId, ff::ink);
    noteEditor.setColour (TextEditor::outlineColourId, ff::line);
    noteEditor.setColour (TextEditor::focusedOutlineColourId, ff::ink);
    noteEditor.setColour (CaretComponent::caretColourId, ff::ink);
    noteEditor.setInputRestrictions (200);
    noteEditor.onReturnKey = [this] { closeNote (true); };
    noteEditor.onEscapeKey = [this] { closeNote (false); };
    noteEditor.onTextChange = [this] { noteUntil = Time::getMillisecondCounterHiRes() * 0.001 + 20.0; };

    setView (View::Main);
    setSize (1260, 660);
    lastTick = Time::getMillisecondCounterHiRes() * 0.001;
    startTimerHz (60);
}

SpeckleEditor::~SpeckleEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void SpeckleEditor::setView (View v)
{
    view = v;
    for (auto* c : mainPage) c->setVisible (v == View::Main);
    for (auto& c : footCards) c->setVisible (v == View::Pedal);
    viewMain.setLit (v == View::Main);
    viewPedal.setLit (v == View::Pedal);
    repaint();
}

void SpeckleEditor::keeperMenu()
{
    PopupMenu m;
    m.addItem (1, "Add a note to Keeper No. " + String (proc.lastKeeper()), proc.lastKeeper() > 0);
    m.addItem (2, "Export for Claude");
    m.addItem (3, "Open the Keepers folder");
    m.addSeparator();
    m.addItem (4, "Learn a Keeper footswitch");
    m.showMenuAsync (PopupMenu::Options().withTargetComponent (&keepButton), [this] (int r)
    {
        if (r == 1)
        {
            noteNumber = proc.lastKeeper();
            noteEditor.clear();
            noteEditor.setVisible (true);
            noteEditor.grabKeyboardFocus();
            noteUntil = Time::getMillisecondCounterHiRes() * 0.001 + 20.0;
            repaint (136, 540, 190, 100);
        }
        else if (r == 2) proc.exportKeepers();
        else if (r == 3) proc.openKeepersFolder();
        else if (r == 4) proc.learnKeeper();
    });
}

void SpeckleEditor::closeNote (bool save)
{
    if (save) proc.addKeeperNote (noteNumber, noteEditor.getText());
    if (save && noteEditor.getText().trim().isNotEmpty()) proc.say ("NOTED");
    noteEditor.setVisible (false);
    noteEditor.clear();
    noteUntil = 0.0;
    repaint (136, 540, 190, 100);
}

void SpeckleEditor::section (Graphics& g, Rectangle<int> r) const
{
    g.setColour (ff::face);
    g.fillRoundedRectangle (r.toFloat(), 12.0f);
}

void SpeckleEditor::paint (Graphics& g)
{
    g.fillAll (ff::bg);
    g.setColour (ff::ink);
    g.setFont (ff::font (26.0f, 0.04f));
    g.drawText ("SPECKLE", 20, 20, 130, 40, Justification::centredLeft, false);
    ff::label (g, "SCATTERED SAMPLES, SPARKLING SPACE", { 146, 20, 300, 40 });
    ff::label (g, "INPUT", { 456, 20, 50, 40 });
    ff::label (g, "TEMPO", { 748, 20, 56, 40 });

    if (view == View::Main)
    {
        section (g, { 20, 76, 400, 440 });
        section (g, { 440, 76, 250, 440 });
        section (g, { 710, 76, 250, 440 });
        section (g, { 980, 76, 260, 440 });

        ff::label (g, "EFFECT", { 36, 92, 200, 16 });
        ff::label (g, "SHAPE", { 456, 92, 200, 16 });
        ff::label (g, "TONE + SPACE", { 726, 92, 200, 16 });
        ff::label (g, "PHRASE LOOPER", { 996, 92, 120, 16 });
        ff::label (g, loopStatus, { 1104, 92, 120, 16 }, Justification::centredRight, ff::ink);

        ff::label (g, timeHint, { 456, 472, 218, 24 }, Justification::centred, ff::faint.brighter (0.4f), 12.0f);
        ff::label (g, "REVERB", { 726, 386, 120, 18 });
        ff::label (g, String::fromUTF8 ("MODE · ") + reverbName, { 726, 452, 218, 20 }, Justification::centredLeft, ff::dim, 12.0f);
    }

    section (g, { 20, 532, 1220, 112 });
    if (noteEditor.isVisible())
        ff::label (g, "KEPT No. " + String (noteNumber) + String::fromUTF8 (" · ADD A NOTE?"), { 140, 546, 180, 16 }, Justification::centredLeft, ff::ink, 12.0f);
    else
    {
        ff::label (g, "PEBBLE", { 140, 542, 180, 16 });
        ff::label (g, mood, { 140, 558, 180, 28 }, Justification::centredLeft, ff::ink, 21.0f);
        ff::label (g, moodSub, { 140, 584, 180, 16 }, Justification::centredLeft, ff::dim, 11.0f);
    }
    ff::label (g, String::fromUTF8 ("USER PRESETS · LOOPS SAVE WITH THEM"), { 496, 546, 440, 18 });
}

void SpeckleEditor::resized()
{
    // header
    inputMode.setBounds (506, 24, 226, 32);
    tempoCard.setBounds (804, 20, 84, 40);
    tapButton.setBounds (898, 24, 62, 32);
    sync.setBounds (968, 24, 66, 32);
    viewMain.setBounds (1094, 24, 70, 32);
    viewPedal.setBounds (1170, 24, 70, 32);

    // effect
    groupCard.setBounds (36, 120, 226, 72);
    effectCard.setBounds (270, 120, 134, 72);
    effectDial.setBounds (36, 196, 368, 252);
    variant.setBounds (36, 458, 250, 36);
    reverse.setBounds (298, 458, 106, 36);

    // shape
    activity.setBounds (456, 116, 110, 128);
    repeats.setBounds (570, 116, 110, 128);
    shape.setBounds (456, 252, 110, 128);
    time.setBounds (570, 252, 110, 128);
    modDepth.setBounds (456, 386, 104, 34);
    modRate.setBounds (572, 386, 104, 34);
    timeMode.setBounds (456, 432, 218, 32);

    // tone and space
    filter.setBounds (726, 116, 110, 128);
    resonance.setBounds (840, 116, 110, 128);
    space.setBounds (726, 252, 110, 128);
    mix.setBounds (840, 252, 110, 128);
    reverbMode.setBounds (726, 410, 218, 32);

    // looper
    loopLevel.setBounds (996, 116, 110, 128);
    loopSpeed.setBounds (1110, 116, 110, 128);
    fadeTime.setBounds (996, 252, 110, 128);
    volume.setBounds (1110, 252, 110, 128);
    loopBar.setBounds (996, 388, 228, 10);
    const int cw = 72, ch = 28;
    loopPre.setBounds (996, 406, cw, ch);
    quantise.setBounds (996 + cw + 6, 406, cw, ch);
    loopReverse.setBounds (996 + 2 * (cw + 6), 406, cw, ch);
    burst.setBounds (996, 440, cw, ch);
    loopOnly.setBounds (996 + cw + 6, 440, cw, ch);
    fadeMode.setBounds (996 + 2 * (cw + 6), 440, cw, ch);
    recButton.setBounds (996, 476, 110, 34);
    stopButton.setBounds (1114, 476, 110, 34);

    // pedal page: one card per footswitch
    for (int k = 0; k < 4; ++k) footCards[(size_t) k]->setBounds (20 + k * 310, 76, 290, 440);

    // the strip, left to right: Pebble and Keeper, Hold, presets, footswitches, bypass
    pebble.setBounds (32, 540, 96, 98);
    noteEditor.setBounds (140, 566, 180, 30);
    keepButton.setBounds (140, 604, 96, 32);
    contextCard.setBounds (244, 604, 76, 32);
    hold.setBounds (332, 556, 148, 64);
    banks.setBounds (493, 568, 440, 50);
    footInput.setBounds (948, 542, 142, 20);
    for (int k = 0; k < 4; ++k) minis[(size_t) k]->setBounds (948 + k * 37, 566, 31, 30);
    expression.setBounds (948, 602, 142, 36);
    bypass.setBounds (1102, 556, 122, 64);
}

void SpeckleEditor::timerCallback()
{
    const double now = Time::getMillisecondCounterHiRes() * 0.001;
    const double dt = jlimit (0.0, 0.1, now - lastTick);
    lastTick = now;
    auto& s = proc.apvts;

    for (auto* c : { &tempoCard, &groupCard, &effectCard, &contextCard })
        c->advance (dt);
    for (auto& c : footCards) c->tick();

    // flip cards: group, effect and variation, tempo
    const int fx = jlimit (0, spk::kNumEffects - 1, roundToInt (s.getRawParameterValue (ParamIDs::effect)->load()));
    const int var = jlimit (0, 3, roundToInt (s.getRawParameterValue (ParamIDs::variant)->load()));
    static const char* shortNames[11] = { "MOSAIC", "SEQ", "GLIDE", "HAZE", "TUNNEL", "STRUM", "BLOCKS", "INTRPT", "ARP", "PATTN", "WARP" };
    groupCard.setValue (names::groups[fx].toUpperCase());
    effectCard.setValue (String (shortNames[fx]) + " " + String::charToString ((juce_wchar) ('A' + var)));
    tempoCard.setValue (String (roundToInt (proc.currentBpm())));
    sync.setAlpha (proc.hostSynced() || s.getRawParameterValue (ParamIDs::sync)->load() < 0.5f ? 1.0f : 0.6f);
    if (lastEffect >= 0 && fx != lastEffect) proc.say (names::effects[fx].toUpperCase());
    lastEffect = fx;

    // the Time knob sets the subdivision, or the tempo itself in Tempo mode
    const int tm = roundToInt (s.getRawParameterValue (ParamIDs::timeMode)->load());
    if (tm != boundTimeMode)
    {
        time.bindTo (tm == 1 ? ParamIDs::bpm : ParamIDs::subdiv);
        boundTimeMode = tm;
    }
    const String hint = tm == 1 ? (proc.hostSynced() ? "ABLETON SETS THE TEMPO" : "TIME SETS THE TEMPO")
                                : "TIME SETS " + names::subdivs[roundToInt (s.getRawParameterValue (ParamIDs::subdiv)->load())].toUpperCase();
    const String rv = names::reverbs[roundToInt (s.getRawParameterValue (ParamIDs::reverbMode)->load())].toUpperCase();

    // looper status
    const auto& lp = proc.getEngine().getLooper();
    using L = spk::Looper;
    const int st = lp.uiState.load();
    const float secs = lp.uiSeconds.load();
    String ls;
    switch (st)
    {
        case L::Recording: ls = "REC " + String (secs, 1) + " S"; break;
        case L::Playing: ls = "PLAYING " + String (secs, 1) + " S"; break;
        case L::Overdubbing: ls = "DUBBING"; break;
        case L::Stopping: case L::Stopped: ls = "STOPPED"; break;
        default: ls = "EMPTY"; break;
    }

    // Pebble's status lines
    String m, sub;
    const bool held = s.getRawParameterValue (ParamIDs::hold)->load() > 0.5f;
    if (now - proc.uiMessageAt < 2.5 && proc.uiMessage.isNotEmpty()) { m = proc.uiMessage; sub = "PEBBLE IS ON IT"; }
    else if (s.getRawParameterValue (ParamIDs::bypass)->load() > 0.5f) { m = "NAPPING"; sub = "BYPASSED"; }
    else if (held) { m = "HOLDING"; sub = "A BEAT IS FROZEN"; }
    else if (st == L::Recording) { m = "RECORDING"; sub = "TAP REC TO LOOP IT"; }
    else if (st == L::Overdubbing) { m = "LAYERING"; sub = "OVERDUBBING THE LOOP"; }
    else if (st == L::Playing) { m = "LOOPING"; sub = String (secs, 1) + " S PHRASE"; }
    else { m = "LISTENING"; sub = names::effects[fx].toUpperCase() + " " + String::charToString ((juce_wchar) ('A' + var)); }

    if (hint != timeHint || rv != reverbName || ls != loopStatus || m != mood || sub != moodSub)
    {
        timeHint = hint; reverbName = rv; loopStatus = ls; mood = m; moodSub = sub;
        repaint();
    }

    // Keeper: flip to KEPT, Pebble nods, offer a note for ten seconds without taking the keyboard
    const int kp = proc.uiKeepPulse.load();
    if (kp != lastKeepPulse)
    {
        lastKeepPulse = kp;
        keptUntil = now + 1.8;
        contextCard.setValue ("KEPT");
        pebble.nod();
        noteNumber = 0;
        noteEditor.clear();
        noteEditor.setVisible (true);
        noteUntil = now + 10.0;
        repaint (136, 540, 190, 100);
    }
    if (noteNumber == 0 && proc.lastKeeper() > 0 && noteEditor.isVisible()) { noteNumber = proc.lastKeeper(); repaint (136, 540, 190, 100); }
    if (keptUntil > 0.0 && now > keptUntil) { keptUntil = 0.0; contextCard.setValue (proc.keeperLive() ? "LIVE" : "STUDIO"); }
    if (noteEditor.isVisible() && now > noteUntil && ! noteEditor.hasKeyboardFocus (true)) closeNote (false);
    const bool learningKeep = proc.learningWhat() == SpeckleProcessor::LearnKeeper;
    keepButton.set (learningKeep ? "PRESS IT" : "KEEP", learningKeep ? "YOUR SWITCH" : "HOLD C+D", learningKeep ? ff::ink : ff::line);

    pebble.update (now, dt);
    loopBar.repaint();
    recButton.repaint();
    stopButton.repaint();
    tapButton.repaint();
    effectDial.repaint();
    banks.repaint();
    hold.repaint();
    bypass.repaint();
    expression.repaint();
    footInput.repaint();
    for (auto& mf : minis) mf->repaint();
}
