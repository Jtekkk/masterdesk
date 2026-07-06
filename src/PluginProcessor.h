#pragma once

/*  MasterDesk — plugin processor.

    Hosts the 64-bit MasterChain, exposes:
      • native double-precision processing (float hosts get a lossless
        promote/demote at the boundary),
      • an optional external stereo sidechain bus,
      • undo/redo (UndoManager-backed parameter history),
      • A/B compare + presets + parameter locking,
      • lock-free meter publication and a spectrum FIFO for the editor,
      • host-reported latency that tracks oversampling/limiter settings.
*/

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_dsp/juce_dsp.h>

#include "Parameters.h"
#include "PresetManager.h"
#include "dsp/MasterChain.h"

//==============================================================================
class MasterDeskProcessor : public juce::AudioProcessor,
                            private juce::AsyncUpdater
{
public:
    MasterDeskProcessor();
    ~MasterDeskProcessor() override;

    //==========================================================================
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;

    void processBlock (juce::AudioBuffer<float>&,  juce::MidiBuffer&) override;
    void processBlock (juce::AudioBuffer<double>&, juce::MidiBuffer&) override;
    bool supportsDoublePrecisionProcessing() const override   { return true; }

    //==========================================================================
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override                           { return true; }

    const juce::String getName() const override               { return "MasterDesk"; }
    bool acceptsMidi() const override                         { return false; }
    bool producesMidi() const override                        { return false; }
    double getTailLengthSeconds() const override              { return 0.1; }

    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram (int) override;
    const juce::String getProgramName (int) override;
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    juce::AudioProcessorParameter* getBypassParameter() const override;

    //==========================================================================
    juce::AudioProcessorValueTreeState apvts;
    juce::UndoManager undoManager;
    std::unique_ptr<PresetManager> presets;

    //==========================================================================
    // Parameter locking (kept while browsing presets)
    bool isParamLocked (const juce::String& id) const;
    void setParamLocked (const juce::String& id, bool locked);
    void toggleParamLock (const juce::String& id)             { setParamLocked (id, ! isParamLocked (id)); }

    //==========================================================================
    // Meters (written on the audio thread, read by the editor)
    struct Meters
    {
        std::atomic<float> dr             { 0.0f };
        std::atomic<float> lufsMomentary  { -120.0f };
        std::atomic<float> lufsShort      { -120.0f };
        std::atomic<float> lufsIntegrated { -120.0f };
        std::atomic<float> rmsL  { -120.0f }, rmsR  { -120.0f };
        std::atomic<float> peakL { -120.0f }, peakR { -120.0f };
        std::atomic<float> truePeak { -120.0f };
        std::atomic<float> compGr { 0.0f }, limGr { 0.0f };
        std::atomic<float> supply { 1.0f };
    };
    Meters meters;

    void resetIntegratedLoudness()                            { chain.resetIntegratedLoudness(); }

    //==========================================================================
    // Spectrum tap (mono, post-output) for the analyser view
    static constexpr int spectrumFifoSize = 1 << 14;
    int  readSpectrum (float* dest, int maxSamples);          // returns count

    uint64_t getAnalogSeed() const                            { return analogSeed; }

private:
    void handleAsyncUpdate() override;                        // latency notify
    void pushChainParams();
    void processInternal (juce::AudioBuffer<double>& mainBus,
                          const double* scL, const double* scR, int numSamples);
    void pushSpectrum (const double* l, const double* r, int n);

    md::dsp::MasterChain chain;
    uint64_t analogSeed = 0;

    juce::AudioBuffer<double> doubleScratch;                  // float-host promote buffer
    juce::AudioBuffer<double> scScratch;

    std::atomic<int> latencyToReport { 0 };

    juce::AbstractFifo spectrumFifo { spectrumFifoSize };
    std::vector<float> spectrumBuf;

    juce::CriticalSection lockSection;
    juce::StringArray lockedParams;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MasterDeskProcessor)
};
