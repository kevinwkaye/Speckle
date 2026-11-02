#include "PluginProcessor.h"
#include "PluginEditor.h"

using namespace juce;

SpeckleProcessor::SpeckleProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", AudioChannelSet::stereo(), true)
                          .withOutput ("Output", AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "SpeckleState", createParameterLayout())
{
    for (auto* p : getParameters())
        if (auto* rp = dynamic_cast<RangedAudioParameter*> (p))
            raw[rp->getParameterID()] = apvts.getRawParameterValue (rp->getParameterID());
    startTimerHz (60);
}

SpeckleProcessor::~SpeckleProcessor()
{
    stopTimer();
}

void SpeckleProcessor::prepareToPlay (double sampleRate, int)
{
    engine.prepare (sampleRate);
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
        handleMidi (meta.getMessage());

    engine.process (buffer.getWritePointer (0), buffer.getWritePointer (1), n, readParams());
}

// The pedal's own MIDI map, so a controller set up for a Microcosm works here too
void SpeckleProcessor::handleMidi (const MidiMessage& m)
{
    if (m.isProgramChange())
    {
        static const int order[11] = { spk::Arp, spk::Interrupt, spk::Blocks, spk::Glide, spk::Seq, spk::Mosaic,
                                       spk::Haze, spk::Tunnel, spk::Strum, spk::Pattern, spk::Warp };
        const int pc = m.getProgramChangeNumber();
        if (pc < 44)
        {
            queue (SetPlain, ParamIDs::effect, (float) order[pc / 4]);
            queue (SetPlain, ParamIDs::variant, (float) (pc % 4));
        }
        else if (pc < 44 + kPresets)
            queue (LoadPreset, nullptr, (float) (pc - 44));
        return;
    }
    if (! m.isController())
        return;

    const float v = (float) m.getControllerValue() / 127.0f;
    const bool down = m.getControllerValue() >= 64;
    using namespace ParamIDs;
    switch (m.getControllerNumber())
    {
        case 5:  queue (SetNorm, subdiv, v); break;
        case 6:  queue (SetNorm, activity, v); break;
        case 7:  queue (SetNorm, shape, v); break;
        case 8:  queue (SetNorm, filter, v); break;
        case 9:  queue (SetNorm, mix, v); break;
        case 10: queue (SetNorm, idx (timeMode) == 1 ? bpm : subdiv, v); break;
        case 11: queue (SetNorm, repeats, v); break;
        case 12: queue (SetNorm, space, v); break;
        case 13: queue (SetNorm, loopLevel, v); break;
        case 14: queue (SetNorm, modRate, v); break;
        case 15: queue (SetNorm, resonance, v); break;
        case 16: queue (SetNorm, volume, v); break;
        case 17: queue (SetNorm, loopSpeed, v); break;
        case 19: queue (SetNorm, modDepth, v); break;
        case 20: queue (SetPlain, reverbMode, (float) jlimit (0, 3, m.getControllerValue() / 32)); break;
        case 21: queue (SetNorm, fadeTime, v); break;
        case 23: queue (SetPlain, loopReverse, down ? 1.0f : 0.0f); break;
        case 24: queue (SetPlain, loopPre, down ? 1.0f : 0.0f); break;
        case 25: queue (SetPlain, loopOnly, down ? 1.0f : 0.0f); break;
        case 26: queue (SetPlain, burst, down ? 1.0f : 0.0f); break;
        case 27: queue (SetPlain, quantise, down ? 1.0f : 0.0f); break;
        case 28: case 29: case 30: if (down) engine.postCommand (spk::SpeckleEngine::CmdRec); break;
        case 31: if (down) engine.postCommand (spk::SpeckleEngine::CmdStop); break;
        case 34: if (down) engine.postCommand (spk::SpeckleEngine::CmdErase); break;
        case 35: if (down) engine.postCommand (spk::SpeckleEngine::CmdUndo); break;
        case 47: queue (SetPlain, reverse, down ? 1.0f : 0.0f); break;
        case 48: queue (SetPlain, hold, down ? 1.0f : 0.0f); break;
        case 93: if (down) queue (Tap, nullptr, 0.0f); break;
        case 102: queue (SetPlain, bypass, down ? 0.0f : 1.0f); break; // 64-127 engages
        default: break;
    }
}

void SpeckleProcessor::queue (int kind, const char* id, float v)
{
    const auto scope = fifo.write (1);
    if (scope.blockSize1 > 0)
        pending[(size_t) scope.startIndex1] = { kind, id, v };
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

void SpeckleProcessor::timerCallback()
{
    // MIDI changes
    const auto scope = fifo.read (fifo.getNumReady());
    auto apply = [this] (int start, int size)
    {
        for (int i = start; i < start + size; ++i)
        {
            const auto& p = pending[(size_t) i];
            switch (p.kind)
            {
                case SetNorm: setNorm (p.id, p.value); break;
                case SetPlain: setNow (p.id, p.value); break;
                case Tap: tap(); break;
                case LoadPreset: loadPreset ((int) p.value); break;
                default: break;
            }
        }
    };
    apply (scope.startIndex1, scope.blockSize1);
    apply (scope.startIndex2, scope.blockSize2);

    // long presses
    const double t = Time::getMillisecondCounterHiRes() * 0.001;
    if (recDownAt > 0.0 && ! recHeldFired && ! on (ParamIDs::burst) && t - recDownAt > 0.8)
    {
        engine.postCommand (spk::SpeckleEngine::CmdUndo);
        recHeldFired = true;
    }
    if (stopDownAt > 0.0 && ! stopHeldFired && t - stopDownAt > 0.8)
    {
        engine.postCommand (spk::SpeckleEngine::CmdErase);
        stopHeldFired = true;
    }
}

// ---- user presets: settings and the loop, sixteen slots in four colour banks ----
bool SpeckleProcessor::inSnapshot (const String& id)
{
    static const StringArray excluded { ParamIDs::bypass, ParamIDs::hold, ParamIDs::sync, ParamIDs::trails };
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
