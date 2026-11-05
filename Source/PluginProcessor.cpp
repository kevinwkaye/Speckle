#include "PluginProcessor.h"
#include "PluginEditor.h"

using namespace juce;

// Several Speckle windows can be open at once. Footswitches drive one of them at a time:
// the one whose panel was last touched, or the next one along when C+D are tapped.
namespace
{
SpinLock focusLock;
Array<SpeckleProcessor*> instances;
std::atomic<SpeckleProcessor*> focused { nullptr };

bool hasFocus (SpeckleProcessor* p)
{
    auto* f = focused.load();
    return f == nullptr || f == p;
}

// set while a message from the footswitch device is being handled on the MIDI thread
thread_local bool fromDeviceNow = false;

// footswitch device messages go to the copy that is learning, else the copy in charge
void sendToFocused (const MidiMessage& m)
{
    const SpinLock::ScopedLockType l (focusLock);
    SpeckleProcessor* target = nullptr;
    for (auto* p : instances) if (p->isLearning()) target = p;
    if (target == nullptr) target = focused.load();
    if (target == nullptr && ! instances.isEmpty()) target = instances.getLast();
    if (target != nullptr) target->handleDirectMidi (m);
}
} // namespace

SpeckleProcessor::SpeckleProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", AudioChannelSet::stereo(), true)
                          .withOutput ("Output", AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "SpeckleState", createParameterLayout())
{
    for (auto* p : getParameters())
        if (auto* rp = dynamic_cast<RangedAudioParameter*> (p))
            raw[rp->getParameterID()] = apvts.getRawParameterValue (rp->getParameterID());
    settings.load();
    settingsStamp = PedalSettings::file().getLastModificationTime();
    direct->setSink (&sendToFocused);
    direct->setWanted (settings.footDevice);
    footswitches.onEvent = [this] (int sw, FootswitchLogic::Event e) { onFootEvent (sw, e); };
    {
        const SpinLock::ScopedLockType l (focusLock);
        instances.add (this);
    }
    focused.store (this);
    startTimerHz (100);
}

SpeckleProcessor::~SpeckleProcessor()
{
    stopTimer();
    const SpinLock::ScopedLockType l (focusLock);
    instances.removeAllInstancesOf (this);
    if (focused.load() == this) focused.store (instances.isEmpty() ? nullptr : instances.getLast());
}

void SpeckleProcessor::prepareToPlay (double sampleRate, int)
{
    engine.prepare (sampleRate);
    keeper.prepare (sampleRate);
}

bool SpeckleProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    const auto in = layouts.getMainInputChannelSet();
    if (out != AudioChannelSet::stereo())
        return false;
    return in == AudioChannelSet::stereo() || in == AudioChannelSet::mono();
}

spk::EngineParams SpeckleProcessor::readParams()
{
    spk::EngineParams p;
    p.effect = jlimit (0, spk::kNumEffects - 1, idx (ParamIDs::effect));
    p.variant = jlimit (0, 3, idx (ParamIDs::variant));
    p.reverse = on (ParamIDs::reverse);
    p.activity = f (ParamIDs::activity);
    p.repeats = f (ParamIDs::repeats);
    p.shape = f (ParamIDs::shape);

    // Tempo: Ableton's when synced and playing, otherwise the pedal's own (Tap or the Time knob)
    float bpm = f (ParamIDs::bpm);
    bool synced = false;
    if (on (ParamIDs::sync))
        if (auto* ph = getPlayHead())
            if (auto pos = ph->getPosition())
                if (auto b = pos->getBpm())
                    if (*b > 20.0) { bpm = (float) *b; synced = true; }
    uiBpm.store (bpm);
    uiSynced.store (synced);

    const float fs = (float) getSampleRate();
    p.beatSamples = 60.0f / jmax (20.0f, bpm) * fs;
    const bool tempoMode = idx (ParamIDs::timeMode) == 1;
    p.stepSamples = tempoMode ? p.beatSamples : p.beatSamples * names::subdivBeats[jlimit (0, 8, idx (ParamIDs::subdiv))];

    p.filter = f (ParamIDs::filter);
    p.resonance = f (ParamIDs::resonance);
    p.mix = f (ParamIDs::mix);
    p.volumeDb = f (ParamIDs::volume);
    p.space = f (ParamIDs::space);
    p.reverbMode = jlimit (0, 3, idx (ParamIDs::reverbMode));
    p.modDepth = f (ParamIDs::modDepth);
    p.modRateHz = names::modHz (f (ParamIDs::modRate));
    p.inputMode = idx (ParamIDs::inputMode);
    p.hold = on (ParamIDs::hold);
    p.bypass = on (ParamIDs::bypass);
    p.trails = on (ParamIDs::trails);

    // Loop speed: steps in Subdiv mode, a fluid glide in Tempo mode, as on the pedal
    const float ls = f (ParamIDs::loopSpeed);
    p.loopSpeed = tempoMode ? names::loopSpeedRaw (ls) : names::loopSpeedStepped (ls);
    p.loopLevel = f (ParamIDs::loopLevel);
    p.fadeSeconds = names::fadeSeconds (f (ParamIDs::fadeTime));
    p.fadeMode = idx (ParamIDs::fadeMode);
    p.loopPre = on (ParamIDs::loopPre);
    p.quantise = on (ParamIDs::quantise);
    p.loopReverse = on (ParamIDs::loopReverse);
    p.loopOnly = on (ParamIDs::loopOnly);
    p.recOrder = idx (ParamIDs::recOrder);
    return p;
}

void SpeckleProcessor::processBlock (AudioBuffer<float>& buffer, MidiBuffer& midi)
{
    ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    if (n == 0 || getTotalNumOutputChannels() < 2)
        return;

    if (getTotalNumInputChannels() == 1) // a mono input feeds both sides
        buffer.copyFrom (1, 0, buffer, 0, 0, n);

    if (loadReady.load())
    {
        engine.getLooper().load (loadL, loadR, loadPlay);
        loadReady.store (false);
    }

    for (const auto meta : midi)
        handleMidi (meta.getMessage(), false);

    float* L = buffer.getWritePointer (0);
    float* R = buffer.getWritePointer (1);
    keeper.pushInput (L, R, n);
    engine.process (L, R, n, readParams());
    keeper.pushOutput (L, R, n);
}

// =====================================================================================
// MIDI: footswitches, expression, and the original pedal's own MIDI map
// =====================================================================================
void SpeckleProcessor::queue (int kind, int a, float v)
{
    auto& fifoRef = fromDeviceNow ? directFifo : fifo;
    auto& dest = fromDeviceNow ? directPending : pending;
    const auto scope = fifoRef.write (1);
    if (scope.blockSize1 > 0) dest[(size_t) scope.startIndex1] = { kind, a, v, nullptr };
}

void SpeckleProcessor::queueParam (int kind, const char* id, float v)
{
    auto& fifoRef = fromDeviceNow ? directFifo : fifo;
    auto& dest = fromDeviceNow ? directPending : pending;
    const auto scope = fifoRef.write (1);
    if (scope.blockSize1 > 0) dest[(size_t) scope.startIndex1] = { kind, 0, v, id };
}

void SpeckleProcessor::handleDirectMidi (const MidiMessage& m)
{
    // several MIDI devices can call in on their own threads
    const SpinLock::ScopedLockType l (directWriteLock);
    fromDeviceNow = true;
    handleMidi (m, true);
    fromDeviceNow = false;
}

void SpeckleProcessor::setFootDevice (const String& device)
{
    settings.footDevice = device;
    settings.save();
    settingsStamp = PedalSettings::file().getLastModificationTime();
    direct->setWanted (device);
}

void SpeckleProcessor::handleMidi (const MidiMessage& m, bool fromDevice)
{
    const int ch = m.getChannel();
    if (m.isProgramChange())
    {
        if (settings.pedalMapChannel == 0 || ch != settings.pedalMapChannel || ! (fromDevice || hasFocus (this))) return;
        // the pedal's program changes: 0 to 43 pick an effect and variation, 44 to 59 the user presets
        static const int order[11] = { spk::Arp, spk::Interrupt, spk::Blocks, spk::Glide, spk::Seq, spk::Mosaic,
                                       spk::Haze, spk::Tunnel, spk::Strum, spk::Pattern, spk::Warp };
        const int pc = m.getProgramChangeNumber();
        if (pc < 44)
        {
            queueParam (SetPlain, ParamIDs::effect, (float) order[pc / 4]);
            queueParam (SetPlain, ParamIDs::variant, (float) (pc % 4));
        }
        else if (pc < 44 + kPresets)
            queue (LoadPreset, pc - 44, 0.0f);
        return;
    }
    if (! m.isController()) return;
    const int cc = m.getControllerNumber(), val = m.getControllerValue();

    if (learnTarget.load() != LearnNone)
    {
        if (val > 0) queue (Learned, ch * 128 + cc, 0.0f);
        return;
    }

    // Straight from the footswitch device, any channel will do: it's the footswitch's own cable,
    // and some footswitches ignore their channel setting. Through Ableton, the channel must match.
    // When the device is read directly, the same messages arriving through Ableton are ignored,
    // so nothing happens twice.
    if (fromDevice || ! pedalsDirect())
    {
        int foot = -1;
        for (int k = 0; k < 4 && foot < 0; ++k)
            if ((fromDevice || ch == settings.fsChannel[(size_t) k]) && cc == settings.fsCC[(size_t) k]) foot = k;
        const bool keep = foot < 0 && settings.keeperCC >= 0 && (fromDevice || ch == settings.keeperChannel) && cc == settings.keeperCC;
        const bool expression = foot < 0 && ! keep && (fromDevice || ch == settings.expChannel) && cc == settings.expCC;
        if (foot >= 0 || keep || expression)
        {
            if (! fromDevice)
            {
                // The copy Ableton sends footswitch MIDI to takes charge, unless the copy in
                // charge is fed too. A newly loaded copy with no MIDI track can't hold the pedals.
                hostFeedsPedals.store (true);
                const SpinLock::ScopedLockType l (focusLock);
                auto* f = focused.load();
                if (f != this && (f == nullptr || ! f->hostFeedsPedals.load())) focused.store (this);
                if (focused.load() != this) return;
            }
            if (foot >= 0) queue (val >= 64 ? FootPress : FootRelease, foot, 0.0f);
            else if (keep) { if (val >= 64) queue (KeepCC, 0, 0.0f); }
            else queue (Expression, val, 0.0f);
            return;
        }
    }

    // The Microcosm's own CC chart. Off unless a channel is chosen for it on the Pedal page:
    // left listening on every channel it took the footswitch and expression numbers
    // (CC 11 moved Repeats, CC 20 to 23 changed the reverb, fade and loop direction).
    if (settings.pedalMapChannel == 0 || ch != settings.pedalMapChannel) return;
    const float v = (float) val / 127.0f;
    const bool down = val >= 64;
    using namespace ParamIDs;
    switch (cc)
    {
        case 5:  queueParam (SetNorm, subdiv, v); break;
        case 6:  queueParam (SetNorm, activity, v); break;
        case 7:  queueParam (SetNorm, shape, v); break;
        case 8:  queueParam (SetNorm, filter, v); break;
        case 9:  queueParam (SetNorm, mix, v); break;
        case 10: queueParam (SetNorm, idx (timeMode) == 1 ? bpm : subdiv, v); break;
        case 11: queueParam (SetNorm, repeats, v); break;
        case 12: queueParam (SetNorm, space, v); break;
        case 13: queueParam (SetNorm, loopLevel, v); break;
        case 14: queueParam (SetNorm, modRate, v); break;
        case 15: queueParam (SetNorm, resonance, v); break;
        case 16: queueParam (SetNorm, volume, v); break;
        case 17: queueParam (SetNorm, loopSpeed, v); break;
        case 19: queueParam (SetNorm, modDepth, v); break;
        case 20: queueParam (SetPlain, reverbMode, (float) jlimit (0, 3, val / 32)); break;
        case 21: queueParam (SetNorm, fadeTime, v); break;
        case 23: queueParam (SetPlain, loopReverse, down ? 1.0f : 0.0f); break;
        case 24: queueParam (SetPlain, loopPre, down ? 1.0f : 0.0f); break;
        case 25: queueParam (SetPlain, loopOnly, down ? 1.0f : 0.0f); break;
        case 26: queueParam (SetPlain, burst, down ? 1.0f : 0.0f); break;
        case 27: queueParam (SetPlain, quantise, down ? 1.0f : 0.0f); break;
        case 28: case 29: case 30: if (down) queue (LoopCmd, spk::SpeckleEngine::CmdRec, 0.0f); break;
        case 31: if (down) queue (LoopCmd, spk::SpeckleEngine::CmdStop, 0.0f); break;
        case 34: if (down) queue (LoopCmd, spk::SpeckleEngine::CmdErase, 0.0f); break;
        case 35: if (down) queue (LoopCmd, spk::SpeckleEngine::CmdUndo, 0.0f); break;
        case 47: queueParam (SetPlain, reverse, down ? 1.0f : 0.0f); break;
        case 48: queueParam (SetPlain, hold, down ? 1.0f : 0.0f); break;
        case 93: if (down) queue (Tap, 0, 0.0f); break;
        case 102: queueParam (SetPlain, bypass, down ? 0.0f : 1.0f); break; // 64 to 127 engages, as on the pedal
        default: break;
    }
}

void SpeckleProcessor::setNow (const String& id, float plainValue)
{
    if (auto* p = apvts.getParameter (id))
    {
        p->beginChangeGesture();
        p->setValueNotifyingHost (p->convertTo0to1 (plainValue));
        p->endChangeGesture();
    }
}

void SpeckleProcessor::setNorm (const String& id, float v01)
{
    if (auto* p = apvts.getParameter (id))
    {
        p->beginChangeGesture();
        p->setValueNotifyingHost (jlimit (0.0f, 1.0f, v01));
        p->endChangeGesture();
    }
}

void SpeckleProcessor::say (const String& text)
{
    uiMessage = text;
    uiMessageAt = Time::getMillisecondCounterHiRes() * 0.001;
}

// ---- tap tempo ----
void SpeckleProcessor::tap()
{
    const double t = Time::getMillisecondCounterHiRes() * 0.001;
    if (! taps.empty() && t - taps.back() > 2.0) taps.clear();
    taps.push_back (t);
    if (taps.size() > 5) taps.erase (taps.begin());
    if (taps.size() >= 2)
    {
        const double avg = (taps.back() - taps.front()) / (double) (taps.size() - 1);
        setNow (ParamIDs::bpm, (float) jlimit (40.0, 240.0, 60.0 / avg));
        setNow (ParamIDs::sync, 0.0f); // tapping takes over from Ableton's tempo
    }
}

// ---- looper footswitches ----
void SpeckleProcessor::recDown()
{
    recDownAt = Time::getMillisecondCounterHiRes() * 0.001;
    recHeldFired = false;
    engine.postCommand (on (ParamIDs::burst) ? spk::SpeckleEngine::CmdBurstDown : spk::SpeckleEngine::CmdRec);
}

void SpeckleProcessor::recUp()
{
    if (on (ParamIDs::burst)) engine.postCommand (spk::SpeckleEngine::CmdBurstUp);
    recDownAt = -1.0;
}

void SpeckleProcessor::stopDown()
{
    stopDownAt = Time::getMillisecondCounterHiRes() * 0.001;
    stopHeldFired = false;
    engine.postCommand (spk::SpeckleEngine::CmdStop);
}

void SpeckleProcessor::stopUp() { stopDownAt = -1.0; }

void SpeckleProcessor::nextEffect (int direction)
{
    const int fx = (idx (ParamIDs::effect) + direction + spk::kNumEffects) % spk::kNumEffects;
    setNow (ParamIDs::effect, (float) fx);
    say (names::effects[fx].toUpperCase());
}

void SpeckleProcessor::timerCallback()
{
    const double now = Time::getMillisecondCounterHiRes() * 0.001;

    // settings changed by another copy, or another Flip Fella plugin: pick them up
    if (now >= nextSettingsCheck)
    {
        nextSettingsCheck = now + 1.0;
        const auto stamp = PedalSettings::file().getLastModificationTime();
        if (stamp != settingsStamp && learnTarget.load() == LearnNone && ! calibrating)
        {
            settingsStamp = stamp;
            settings.load();
            direct->setWanted (settings.footDevice);
        }
        const bool d = pedalsDirect();
        if (d != wasDirect)
        {
            wasDirect = d;
            if (d && hasFocus (this)) say ("FOOTSWITCHES READY");
        }
    }

    auto apply = [&] (const std::array<Pending, 512>& from, int start, int size)
    {
        for (int i = start; i < start + size; ++i)
        {
            const auto& q = from[(size_t) i];
            switch (q.kind)
            {
                case FootPress: footswitches.press (q.a, now); break;
                case FootRelease: footswitches.release (q.a, now); break;
                case SetNorm: setNorm (q.id, q.v); break;
                case SetPlain: setNow (q.id, q.v); break;
                case LoadPreset: loadPreset (q.a); break;
                case Tap: tap(); break;
                case KeepCC: keepNow(); break;
                case LoopCmd: engine.postCommand (q.a); break;
                case Expression:
                {
                    const int cc = q.a;
                    expMovedAt = now;
                    if (calibrating)
                    {
                        if (calFirst < 0) calFirst = cc;
                        calLo = jmin (calLo, cc);
                        calHi = jmax (calHi, cc);
                        uiExp.store ((float) cc / 127.0f);
                        break;
                    }
                    const float v = settings.expressionValue (cc);
                    uiExp.store (v);
                    const auto& targets = names::expTargets();
                    const int t = jlimit (0, (int) targets.size() - 1, idx (ParamIDs::expTarget));
                    setNorm (targets[(size_t) t].id, v);
                    break;
                }
                case Learned:
                {
                    const int ch = q.a / 128, cc = q.a % 128;
                    const int what = learnTarget.exchange (LearnNone);
                    if (what >= 0 && what < 4)
                    {
                        settings.fsChannel[(size_t) what] = ch;
                        settings.fsCC[(size_t) what] = cc;
                        say ("FOOTSWITCH " + String::charToString ((juce_wchar) ('A' + what)) + " IS CC " + String (cc));
                    }
                    else if (what == LearnKeeper)
                    {
                        settings.keeperChannel = ch;
                        settings.keeperCC = cc;
                        say ("KEEPER IS CC " + String (cc));
                    }
                    else if (what == LearnExpression)
                    {
                        settings.expChannel = ch;
                        settings.expCC = cc;
                        say ("EXPRESSION IS CC " + String (cc));
                    }
                    settings.save();
                    settingsStamp = PedalSettings::file().getLastModificationTime();
                    break;
                }
                default: break;
            }
        }
    };
    {
        const auto scope = fifo.read (fifo.getNumReady());
        apply (pending, scope.startIndex1, scope.blockSize1);
        apply (pending, scope.startIndex2, scope.blockSize2);
    }
    {
        const auto scope = directFifo.read (directFifo.getNumReady());
        apply (directPending, scope.startIndex1, scope.blockSize1);
        apply (directPending, scope.startIndex2, scope.blockSize2);
    }

    // actions a remote controller can press: each is a switch that springs back
    if (on (ParamIDs::keeper)) { setNow (ParamIDs::keeper, 0.0f); keepNow(); }
    if (on (ParamIDs::actRec)) { setNow (ParamIDs::actRec, 0.0f); engine.postCommand (spk::SpeckleEngine::CmdRec); }
    if (on (ParamIDs::actStop)) { setNow (ParamIDs::actStop, 0.0f); engine.postCommand (spk::SpeckleEngine::CmdStop); }
    if (on (ParamIDs::actNextFx)) { setNow (ParamIDs::actNextFx, 0.0f); nextEffect (1); }

    footswitches.tick (now);
    int mask = 0;
    for (int k = 0; k < 4; ++k) if (footswitches.isDown (k)) mask |= 1 << k;
    uiFootDown.store (mask);

    // long presses on the on-screen looper buttons
    if (recDownAt > 0.0 && ! recHeldFired && ! on (ParamIDs::burst) && now - recDownAt > 0.8)
    {
        engine.postCommand (spk::SpeckleEngine::CmdUndo);
        recHeldFired = true;
        say ("OVERDUB UNDONE");
    }
    if (stopDownAt > 0.0 && ! stopHeldFired && now - stopDownAt > 0.8)
    {
        engine.postCommand (spk::SpeckleEngine::CmdErase);
        stopHeldFired = true;
        say ("LOOP ERASED");
    }

    if (calibrating && now > calibrateUntil)
    {
        calibrating = false;
        if (calHi - calLo > 20)
        {
            // whichever end the pedal started at is the heel
            const bool startedLow = std::abs (calFirst - calLo) < std::abs (calFirst - calHi);
            settings.heel = startedLow ? calLo : calHi;
            settings.toe = startedLow ? calHi : calLo;
            settings.save();
            settingsStamp = PedalSettings::file().getLastModificationTime();
            say ("EXPRESSION CALIBRATED");
        }
        else
            say ("NO MOVEMENT, TRY AGAIN");
    }
}

// =====================================================================================
// Footswitches, laid out after the Microcosm's own:
//   A  tap: bypass                  hold: reverse while held
//   B  tap: Hold sampler on/off     hold: Hold while held
//   C  tap: loop record / overdub   hold: undo
//   D  tap: loop stop / play        hold: erase
//   A+B  next effect                C+D  hand the footswitches on, hold a second for Keeper
// =====================================================================================
void SpeckleProcessor::onFootEvent (int sw, FootswitchLogic::Event e)
{
    using E = FootswitchLogic;
    using C = spk::SpeckleEngine;
    const double now = Time::getMillisecondCounterHiRes() * 0.001;
    if (e == E::Down || e == E::ComboAB || e == E::ComboCD) uiFootPulse.fetch_add (1);

    if (e == E::ComboHoldCD)
    {
        focused.store (this); // Keeper, on the plugin you were playing
        keepNow();
        return;
    }
    if (e == E::ComboCD)
    {
        // C acted the moment it went down; as half of a pair, take that back
        if (recFromFoot && now - recFootAt < 0.3) engine.postCommand (on (ParamIDs::burst) ? C::CmdErase : C::CmdRevert);
        recFromFoot = false;
        const SpinLock::ScopedLockType l (focusLock);
        const int i = instances.indexOf (this);
        auto* next = instances.isEmpty() ? nullptr : instances[(i + 1) % instances.size()];
        focused.store (next);
        if (next != nullptr) next->say ("FOOTSWITCHES HERE");
        return;
    }
    if (e == E::ComboAB) { nextEffect (1); return; }
    if (e == E::ComboHoldAB) return; // reserved

    const int st = engine.getLooper().uiState.load();
    using L = spk::Looper;
    switch (sw)
    {
        case 0:
            if (e == E::Tap) { setNow (ParamIDs::bypass, on (ParamIDs::bypass) ? 0.0f : 1.0f); say (on (ParamIDs::bypass) ? "BYPASSED" : "ON"); }
            if (e == E::HoldStart) { reverseBeforeHold = on (ParamIDs::reverse); setNow (ParamIDs::reverse, reverseBeforeHold ? 0.0f : 1.0f); }
            if (e == E::HoldEnd) setNow (ParamIDs::reverse, reverseBeforeHold ? 1.0f : 0.0f);
            break;
        case 1:
            if (e == E::Tap) { setNow (ParamIDs::hold, on (ParamIDs::hold) ? 0.0f : 1.0f); say (on (ParamIDs::hold) ? "HELD" : "LISTENING"); }
            if (e == E::HoldStart) setNow (ParamIDs::hold, 1.0f);
            if (e == E::HoldEnd) setNow (ParamIDs::hold, 0.0f);
            break;
        case 2:
            // acts on the press, so loops start and end exactly when your foot lands
            if (e == E::Down)
            {
                engine.postCommand (on (ParamIDs::burst) ? C::CmdBurstDown : C::CmdRec);
                recFromFoot = true;
                recFootAt = now;
            }
            if (e == E::Tap || e == E::HoldEnd) { if (on (ParamIDs::burst)) engine.postCommand (C::CmdBurstUp); recFromFoot = false; }
            if (e == E::HoldStart && ! on (ParamIDs::burst) && st != L::Recording && st != L::Empty)
            {
                engine.postCommand (C::CmdUndo);
                say ("OVERDUB UNDONE");
            }
            break;
        case 3:
            if (e == E::Tap)
            {
                if (st == L::Stopped || st == L::Stopping) engine.postCommand (C::CmdRec); // plays from the top
                else if (st != L::Empty) engine.postCommand (C::CmdStop);
            }
            if (e == E::HoldStart && st != L::Empty) { engine.postCommand (C::CmdErase); say ("LOOP ERASED"); }
            break;
        default: break;
    }
}

void SpeckleProcessor::footDown (int sw)
{
    focused.store (this);
    footswitches.press (sw, Time::getMillisecondCounterHiRes() * 0.001);
}

void SpeckleProcessor::footUp (int sw) { footswitches.release (sw, Time::getMillisecondCounterHiRes() * 0.001); }

// =====================================================================================
// Footswitch learn and expression calibration
// =====================================================================================
void SpeckleProcessor::learnFootswitch (int sw)
{
    learnTarget.store (sw);
    say ("PRESS FOOTSWITCH " + String::charToString ((juce_wchar) ('A' + sw)));
}

void SpeckleProcessor::learnExpression()
{
    learnTarget.store (LearnExpression);
    say ("MOVE THE EXPRESSION PEDAL");
}

void SpeckleProcessor::calibrateExpression()
{
    calibrating = true;
    calibrateUntil = Time::getMillisecondCounterHiRes() * 0.001 + 6.0;
    calLo = 127; calHi = 0; calFirst = -1;
    say ("HEEL DOWN, THEN ROCK TO TOE");
}

void SpeckleProcessor::resetFootswitches()
{
    settings.resetFootswitches();
    settings.save();
    settingsStamp = PedalSettings::file().getLastModificationTime();
    say ("FOOTSWITCH DEFAULTS RESTORED");
}

// =====================================================================================
// Keeper
// =====================================================================================
void SpeckleProcessor::keepNow()
{
    DynamicObject::Ptr e = new DynamicObject();
    e->setProperty ("plugin", "Speckle");
   #ifdef JucePlugin_VersionString
    e->setProperty ("version", JucePlugin_VersionString);
   #endif
    e->setProperty ("recreates", "Hologram Electronics Microcosm");
    e->setProperty ("time", Time::getCurrentTime().toISO8601 (true));
    e->setProperty ("context", settings.keeperLive ? "Live" : "Studio");
    static const char* const banks[4] = { "Red", "Yellow", "Green", "Blue" };
    e->setProperty ("preset", selected < 0 ? String ("none") : String (banks[selected / 4]) + " " + String (selected % 4 + 1));
    e->setProperty ("tempo", uiBpm.load());
    e->setProperty ("tempoFromHost", uiSynced.load());
    const int fx = jlimit (0, spk::kNumEffects - 1, idx (ParamIDs::effect));
    e->setProperty ("effect", names::effects[fx] + " " + String::charToString ((juce_wchar) ('A' + jlimit (0, 3, idx (ParamIDs::variant)))));
    e->setProperty ("group", names::groups[fx]);

    DynamicObject::Ptr exp = new DynamicObject();
    const auto& targets = names::expTargets();
    exp->setProperty ("target", targets[(size_t) jlimit (0, (int) targets.size() - 1, idx (ParamIDs::expTarget))].label);
    exp->setProperty ("position", uiExp.load());
    exp->setProperty ("movedSecondsAgo", Time::getMillisecondCounterHiRes() * 0.001 - expMovedAt);
    e->setProperty ("expression", var (exp.get()));

    // every parameter, hidden ones included, as value and as shown on the panel
    static const StringArray skip { ParamIDs::keeper, ParamIDs::actRec, ParamIDs::actStop, ParamIDs::actNextFx };
    DynamicObject::Ptr params = new DynamicObject();
    for (auto* prm : getParameters())
        if (auto* rp = dynamic_cast<RangedAudioParameter*> (prm))
        {
            if (skip.contains (rp->getParameterID())) continue;
            DynamicObject::Ptr pv = new DynamicObject();
            pv->setProperty ("value", rp->convertFrom0to1 (rp->getValue()));
            pv->setProperty ("shown", rp->getCurrentValueAsText());
            params->setProperty (rp->getParameterID(), var (pv.get()));
        }
    e->setProperty ("parameters", var (params.get()));

    const auto& lp = engine.getLooper();
    static const char* const loopStates[6] = { "empty", "recording", "playing", "overdubbing", "stopping", "stopped" };
    e->setProperty ("loopState", loopStates[jlimit (0, 5, lp.uiState.load())]);
    e->setProperty ("loopSeconds", lp.uiSeconds.load());

    keeper.keep (e);
    uiKeepPulse.fetch_add (1);
    say ("KEPT");
}

File SpeckleProcessor::exportKeepers()
{
    const auto file = keeper.exportZip();
    if (file.existsAsFile()) { say ("EXPORTED TO DOCUMENTS"); file.revealToUser(); }
    else say ("NOTHING KEPT YET");
    return file;
}

void SpeckleProcessor::openKeepersFolder()
{
    Keeper::folder().createDirectory();
    Keeper::folder().startAsProcess();
}

void SpeckleProcessor::learnKeeper()
{
    learnTarget.store (LearnKeeper);
    say ("PRESS YOUR KEEPER SWITCH");
}

void SpeckleProcessor::setKeeperLive (bool live)
{
    settings.keeperLive = live;
    settings.save();
    settingsStamp = PedalSettings::file().getLastModificationTime();
}

// ---- user presets: settings and the loop, sixteen slots in four colour banks ----
bool SpeckleProcessor::inSnapshot (const String& id)
{
    static const StringArray excluded { ParamIDs::bypass, ParamIDs::hold, ParamIDs::sync, ParamIDs::trails,
                                        ParamIDs::keeper, ParamIDs::actRec, ParamIDs::actStop, ParamIDs::actNextFx };
    return ! excluded.contains (id);
}

ValueTree SpeckleProcessor::snapshotOf() const
{
    ValueTree t ("Snapshot");
    for (auto* p : getParameters())
        if (auto* rp = dynamic_cast<RangedAudioParameter*> (p))
            if (inSnapshot (rp->getParameterID()))
                t.setProperty (Identifier (rp->getParameterID()), rp->convertFrom0to1 (rp->getValue()), nullptr);
    return t;
}

void SpeckleProcessor::applySnapshot (const ValueTree& t)
{
    for (auto* p : getParameters())
        if (auto* rp = dynamic_cast<RangedAudioParameter*> (p))
        {
            const Identifier id (rp->getParameterID());
            if (inSnapshot (rp->getParameterID()) && t.hasProperty (id))
                setNow (rp->getParameterID(), (float) t.getProperty (id));
        }
}

String SpeckleProcessor::encodeLoop (const std::vector<float>& l, const std::vector<float>& r)
{
    if (l.empty()) return {};
    MemoryOutputStream packed;
    {
        GZIPCompressorOutputStream zip (packed, 6);
        for (size_t i = 0; i < l.size(); ++i)
        {
            zip.writeShort ((short) jlimit (-32767, 32767, roundToInt (l[i] * 32767.0f)));
            zip.writeShort ((short) jlimit (-32767, 32767, roundToInt (r[i] * 32767.0f)));
        }
    }
    return packed.getMemoryBlock().toBase64Encoding();
}

bool SpeckleProcessor::decodeLoop (const String& s, std::vector<float>& l, std::vector<float>& r)
{
    l.clear(); r.clear();
    if (s.isEmpty()) return false;
    MemoryBlock packed;
    if (! packed.fromBase64Encoding (s)) return false;
    MemoryInputStream in (packed, false);
    GZIPDecompressorInputStream unzip (in);
    MemoryBlock data;
    unzip.readIntoMemoryBlock (data);
    const size_t frames = data.getSize() / 4;
    MemoryInputStream pcm (data, false);
    l.resize (frames); r.resize (frames);
    for (size_t i = 0; i < frames; ++i)
    {
        l[i] = (float) pcm.readShort() / 32767.0f;
        r[i] = (float) pcm.readShort() / 32767.0f;
    }
    return frames > 0;
}

static void resampleTo (std::vector<float>& v, double from, double to)
{
    if (v.empty() || from <= 0.0 || std::abs (from - to) < 1.0) return;
    const double ratio = from / to;
    std::vector<float> out ((size_t) ((double) v.size() / ratio));
    for (size_t i = 0; i < out.size(); ++i)
    {
        const double p = (double) i * ratio;
        const size_t a = (size_t) p;
        const float fr = (float) (p - (double) a);
        out[i] = v[a] + (v[std::min (a + 1, v.size() - 1)] - v[a]) * fr;
    }
    v.swap (out);
}

void SpeckleProcessor::savePreset (int slot)
{
    if (slot < 0 || slot >= kPresets) return;
    auto& s = slots[(size_t) slot];
    s.params = snapshotOf();
    std::vector<float> l, r;
    engine.getLooper().mixdown (l, r); // overdubs are mixed down, as on the pedal
    s.loop = encodeLoop (l, r);
    s.params.setProperty ("loopRate", engine.sampleRate(), nullptr);
    selected = slot;
}

void SpeckleProcessor::loadPreset (int slot)
{
    if (! hasPreset (slot) || loadReady.load()) return;
    const auto& s = slots[(size_t) slot];
    applySnapshot (s.params);
    std::vector<float> l, r;
    decodeLoop (s.loop, l, r);
    resampleTo (l, (double) s.params.getProperty ("loopRate", 48000.0), engine.sampleRate());
    resampleTo (r, (double) s.params.getProperty ("loopRate", 48000.0), engine.sampleRate());
    loadL.swap (l);
    loadR.swap (r);
    loadPlay = ! loadL.empty();
    loadReady.store (true);
    selected = slot;
}

bool SpeckleProcessor::hasPreset (int slot) const
{
    return slot >= 0 && slot < kPresets && slots[(size_t) slot].params.isValid();
}

bool SpeckleProcessor::presetHasLoop (int slot) const
{
    return hasPreset (slot) && slots[(size_t) slot].loop.isNotEmpty();
}

AudioProcessorEditor* SpeckleProcessor::createEditor()
{
    focused.store (this); // opening a copy's window gives it the footswitches
    return new SpeckleEditor (*this);
}

// ---- saving: parameters, the 16 presets and the current loop ----
void SpeckleProcessor::getStateInformation (MemoryBlock& destData)
{
    auto state = apvts.copyState();
    for (int i = state.getNumChildren(); --i >= 0;)
    {
        const auto type = state.getChild (i).getType().toString();
        if (type == "Presets" || type == "Loop")
            state.removeChild (i, nullptr);
    }

    ValueTree presets ("Presets");
    presets.setProperty ("selected", selected, nullptr);
    for (int i = 0; i < kPresets; ++i)
    {
        ValueTree slot ("Slot");
        const auto& s = slots[(size_t) i];
        if (s.params.isValid())
        {
            slot.appendChild (s.params.createCopy(), nullptr);
            slot.setProperty ("loop", s.loop, nullptr);
        }
        presets.appendChild (slot, nullptr);
    }
    state.appendChild (presets, nullptr);

    ValueTree loop ("Loop");
    std::vector<float> l, r;
    engine.getLooper().mixdown (l, r);
    loop.setProperty ("rate", engine.sampleRate(), nullptr);
    loop.setProperty ("data", encodeLoop (l, r), nullptr);
    state.appendChild (loop, nullptr);

    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void SpeckleProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary (data, sizeInBytes);
    if (xml == nullptr || ! xml->hasTagName (apvts.state.getType()))
        return;
    auto tree = ValueTree::fromXml (*xml);

    const auto presets = tree.getChildWithName ("Presets");
    if (presets.isValid())
    {
        selected = (int) presets.getProperty ("selected", -1);
        for (int i = 0; i < kPresets && i < presets.getNumChildren(); ++i)
        {
            const auto slot = presets.getChild (i);
            auto& s = slots[(size_t) i];
            s.params = slot.getNumChildren() > 0 ? slot.getChild (0).createCopy() : ValueTree();
            s.loop = slot.getProperty ("loop").toString();
        }
        tree.removeChild (presets, nullptr);
    }

    const auto loop = tree.getChildWithName ("Loop");
    if (loop.isValid())
    {
        std::vector<float> l, r;
        if (decodeLoop (loop.getProperty ("data").toString(), l, r) && ! loadReady.load())
        {
            const double rate = (double) loop.getProperty ("rate", 48000.0);
            const double now = getSampleRate() > 0.0 ? getSampleRate() : rate;
            resampleTo (l, rate, now);
            resampleTo (r, rate, now);
            loadL.swap (l);
            loadR.swap (r);
            loadPlay = false; // a reopened set brings its loop back stopped, ready to play
            loadReady.store (true);
        }
        tree.removeChild (loop, nullptr);
    }

    apvts.replaceState (tree);
}

AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new SpeckleProcessor();
}
