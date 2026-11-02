#pragma once

#include "Params.h"
#include <juce_audio_processors/juce_audio_processors.h>
#include <map>

class SpeckleProcessor : public juce::AudioProcessor, private juce::Timer
{
public:
    static constexpr int kPresets = 16;

    SpeckleProcessor();
    ~SpeckleProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Speckle"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 20.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState apvts;
    spk::SpeckleEngine& getEngine() { return engine; }
    const spk::SpeckleEngine& getEngine() const { return engine; }

    // ---- called from the panel (message thread) ----
    void tap();
    float currentBpm() const { return uiBpm.load(); }
    bool hostSynced() const { return uiSynced.load(); }

    // Looper footswitches. Tap acts at once; holding Rec undoes the overdub, holding Stop erases.
    void recDown();
    void recUp();
    void stopDown();
    void stopUp();

    void savePreset (int slot);
    void loadPreset (int slot);
    bool hasPreset (int slot) const;
    bool presetHasLoop (int slot) const;
    int selectedPreset() const { return selected; }

private:
    void timerCallback() override;
    spk::EngineParams readParams();
    void handleMidi (const juce::MidiMessage&);
    void setNow (const juce::String& id, float plainValue);
    void setNorm (const juce::String& id, float v01);
    juce::ValueTree snapshotOf() const;
    void applySnapshot (const juce::ValueTree&);
    static bool inSnapshot (const juce::String& id);
    static juce::String encodeLoop (const std::vector<float>& l, const std::vector<float>& r);
    static bool decodeLoop (const juce::String&, std::vector<float>& l, std::vector<float>& r);

    spk::SpeckleEngine engine;

    std::map<juce::String, std::atomic<float>*> raw;
    float f (const char* id) const { return raw.at (id)->load (std::memory_order_relaxed); }
    bool on (const char* id) const { return f (id) > 0.5f; }
    int idx (const char* id) const { return (int) std::lround (f (id)); }

    // MIDI changes are made on the message thread
    struct Pending { int kind = 0; const char* id = nullptr; float value = 0; };
    enum PendingKind { SetNorm = 0, SetPlain, Tap, LoadPreset };
    juce::AbstractFifo fifo { 256 };
    std::array<Pending, 256> pending;
    void queue (int kind, const char* id, float v);

    // a preset's loop handed to the audio thread
    std::vector<float> loadL, loadR;
    std::atomic<bool> loadReady { false };
    bool loadPlay = true;

    struct Slot { juce::ValueTree params; juce::String loop; };  // params also carry the loop's sample rate
    std::array<Slot, kPresets> slots;
    int selected = -1;

    std::atomic<float> uiBpm { 100.0f };
    std::atomic<bool> uiSynced { false };
    std::vector<double> taps;
    double recDownAt = -1.0, stopDownAt = -1.0;
    bool recHeldFired = false, stopHeldFired = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SpeckleProcessor)
};
