#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
MasterDeskProcessor::MasterDeskProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",     juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output",    juce::AudioChannelSet::stereo(), true)
                          .withInput  ("Sidechain", juce::AudioChannelSet::stereo(), false)),
      apvts (*this, &undoManager, "MasterDesk", md::param::createLayout())
{
    // every unit off the line gets its own component tolerances
    analogSeed = (uint64_t) juce::Random::getSystemRandom().nextInt64();
    if (analogSeed == 0) analogSeed = 0x4D44534Bull; // "MDSK"

    presets = std::make_unique<PresetManager> (apvts,
        [this] (const juce::String& id) { return isParamLocked (id); });

    spectrumBuf.resize ((size_t) spectrumFifoSize, 0.0f);
}

MasterDeskProcessor::~MasterDeskProcessor() = default;

//==============================================================================
void MasterDeskProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    chain.prepare (sampleRate, samplesPerBlock, analogSeed);
    pushChainParams();

    doubleScratch.setSize (2, samplesPerBlock, false, false, true);
    scScratch.setSize (2, samplesPerBlock, false, false, true);

    setLatencySamples (chain.latencySamples());
    latencyToReport.store (chain.latencySamples());
}

void MasterDeskProcessor::releaseResources() {}

bool MasterDeskProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto mainIn  = layouts.getMainInputChannelSet();
    const auto mainOut = layouts.getMainOutputChannelSet();

    if (mainIn != mainOut)
        return false;
    if (mainIn != juce::AudioChannelSet::stereo() && mainIn != juce::AudioChannelSet::mono())
        return false;

    // sidechain: disabled, mono or stereo
    if (layouts.inputBuses.size() > 1)
    {
        const auto sc = layouts.getChannelSet (true, 1);
        if (! sc.isDisabled() && sc != juce::AudioChannelSet::mono()
                              && sc != juce::AudioChannelSet::stereo())
            return false;
    }
    return true;
}

//==============================================================================
void MasterDeskProcessor::pushChainParams()
{
    using namespace md::param;
    auto get = [this] (const char* id) { return (double) apvts.getRawParameterValue (id)->load(); };

    md::dsp::MasterChain::Params p;
    p.volumeDb      = get (volume);
    p.foundation    = get (foundation);
    p.tonePercent   = get (tone);
    p.toneVoicing   = (int) get (toneStyle);
    p.thdDb         = get (thd);
    p.stereoPercent = get (stereoEnh);
    p.outputTrimDb  = get (outputTrim);
    p.compMode      = (int) get (compMode);
    p.channelMode   = (int) get (channelMode);
    p.osIndex       = (int) get (oversampling);
    p.linearPhase   = get (phaseMode) > 0.5;
    p.mixPercent    = get (mix);
    p.autoGain      = get (autoGain) > 0.5;
    p.analogAmount  = get (analog) * 0.01;
    p.temperature   = (get (temperature) - 15.0) / 30.0;
    p.truePeak      = get (truePeak) > 0.5;
    p.ceilingDb     = get (ceiling);
    p.externalSC    = get (extSidechain) > 0.5;
    p.scHpfHz       = get (scHpf);
    p.bypassed      = get (bypass) > 0.5;

    chain.setParams (p);

    const int lat = chain.latencySamples();
    if (lat != latencyToReport.exchange (lat))
        triggerAsyncUpdate();
}

void MasterDeskProcessor::handleAsyncUpdate()
{
    setLatencySamples (latencyToReport.load());
}

//==============================================================================
void MasterDeskProcessor::processInternal (juce::AudioBuffer<double>& mainBus,
                                           const double* scL, const double* scR,
                                           int numSamples)
{
    const int numCh = mainBus.getNumChannels();

    double* chans[2] = { nullptr, nullptr };
    if (numCh >= 2)
    {
        chans[0] = mainBus.getWritePointer (0);
        chans[1] = mainBus.getWritePointer (1);
    }
    else
    {
        // mono host bus → run the chain on a duplicated channel pair
        doubleScratch.copyFrom (0, 0, mainBus, 0, 0, numSamples);
        doubleScratch.copyFrom (1, 0, mainBus, 0, 0, numSamples);
        chans[0] = doubleScratch.getWritePointer (0);
        chans[1] = doubleScratch.getWritePointer (1);
    }

    chain.process (chans, scL, scR, numSamples);

    if (numCh < 2)
    {
        auto* dst = mainBus.getWritePointer (0);
        for (int i = 0; i < numSamples; ++i)
            dst[i] = (chans[0][i] + chans[1][i]) * 0.5;
    }

    pushSpectrum (chans[0], chans[1], numSamples);

    const auto m = chain.meters();
    meters.dr.store             ((float) m.dr);
    meters.lufsMomentary.store  ((float) m.lufsMomentary);
    meters.lufsShort.store      ((float) m.lufsShort);
    meters.lufsIntegrated.store ((float) m.lufsIntegrated);
    meters.rmsL.store  ((float) m.rmsL);
    meters.rmsR.store  ((float) m.rmsR);
    meters.peakL.store ((float) m.peakL);
    meters.peakR.store ((float) m.peakR);
    meters.truePeak.store ((float) m.truePeakDb);
    meters.compGr.store ((float) m.compGrDb);
    meters.limGr.store  ((float) m.limGrDb);
    meters.supply.store ((float) m.supply);
}

void MasterDeskProcessor::processBlock (juce::AudioBuffer<double>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    pushChainParams();

    auto mainBus = getBusBuffer (buffer, true, 0);

    const double* scL = nullptr;
    const double* scR = nullptr;
    if (getBusCount (true) > 1)
    {
        auto scBus = getBusBuffer (buffer, true, 1);
        if (scBus.getNumChannels() > 0)
        {
            scL = scBus.getReadPointer (0);
            scR = scBus.getReadPointer (scBus.getNumChannels() > 1 ? 1 : 0);
        }
    }

    processInternal (mainBus, scL, scR, buffer.getNumSamples());
}

void MasterDeskProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    pushChainParams();

    const int n = buffer.getNumSamples();
    auto mainBus = getBusBuffer (buffer, true, 0);
    const int numCh = juce::jmin (2, mainBus.getNumChannels());

    // promote to 64-bit
    for (int ch = 0; ch < numCh; ++ch)
    {
        auto* src = mainBus.getReadPointer (ch);
        auto* dst = doubleScratch.getWritePointer (ch);
        for (int i = 0; i < n; ++i)
            dst[i] = (double) src[i];
    }
    if (numCh == 1)
        doubleScratch.copyFrom (1, 0, doubleScratch, 0, 0, n);

    const double* scL = nullptr;
    const double* scR = nullptr;
    if (getBusCount (true) > 1)
    {
        auto scBus = getBusBuffer (buffer, true, 1);
        if (scBus.getNumChannels() > 0)
        {
            for (int ch = 0; ch < juce::jmin (2, scBus.getNumChannels()); ++ch)
            {
                auto* src = scBus.getReadPointer (ch);
                auto* dst = scScratch.getWritePointer (ch);
                for (int i = 0; i < n; ++i)
                    dst[i] = (double) src[i];
            }
            scL = scScratch.getReadPointer (0);
            scR = scScratch.getReadPointer (scBus.getNumChannels() > 1 ? 1 : 0);
        }
    }

    double* chans[2] = { doubleScratch.getWritePointer (0), doubleScratch.getWritePointer (1) };
    chain.process (chans, scL, scR, n);
    pushSpectrum (chans[0], chans[1], n);

    // demote
    for (int ch = 0; ch < numCh; ++ch)
    {
        auto* dst = mainBus.getWritePointer (ch);
        const double* src = numCh == 1
            ? nullptr : chans[ch];
        if (numCh == 1)
        {
            for (int i = 0; i < n; ++i)
                dst[i] = (float) ((chans[0][i] + chans[1][i]) * 0.5);
        }
        else
        {
            for (int i = 0; i < n; ++i)
                dst[i] = (float) src[i];
        }
    }

    const auto m = chain.meters();
    meters.dr.store             ((float) m.dr);
    meters.lufsMomentary.store  ((float) m.lufsMomentary);
    meters.lufsShort.store      ((float) m.lufsShort);
    meters.lufsIntegrated.store ((float) m.lufsIntegrated);
    meters.rmsL.store  ((float) m.rmsL);
    meters.rmsR.store  ((float) m.rmsR);
    meters.peakL.store ((float) m.peakL);
    meters.peakR.store ((float) m.peakR);
    meters.truePeak.store ((float) m.truePeakDb);
    meters.compGr.store ((float) m.compGrDb);
    meters.limGr.store  ((float) m.limGrDb);
    meters.supply.store ((float) m.supply);
}

//==============================================================================
void MasterDeskProcessor::pushSpectrum (const double* l, const double* r, int n)
{
    int start1, size1, start2, size2;
    spectrumFifo.prepareToWrite (n, start1, size1, start2, size2);
    int i = 0;
    for (int k = 0; k < size1; ++k, ++i) spectrumBuf[(size_t) (start1 + k)] = (float) ((l[i] + r[i]) * 0.5);
    for (int k = 0; k < size2; ++k, ++i) spectrumBuf[(size_t) (start2 + k)] = (float) ((l[i] + r[i]) * 0.5);
    spectrumFifo.finishedWrite (size1 + size2);
}

int MasterDeskProcessor::readSpectrum (float* dest, int maxSamples)
{
    int start1, size1, start2, size2;
    spectrumFifo.prepareToRead (maxSamples, start1, size1, start2, size2);
    int i = 0;
    for (int k = 0; k < size1; ++k, ++i) dest[i] = spectrumBuf[(size_t) (start1 + k)];
    for (int k = 0; k < size2; ++k, ++i) dest[i] = spectrumBuf[(size_t) (start2 + k)];
    spectrumFifo.finishedRead (size1 + size2);
    return size1 + size2;
}

//==============================================================================
bool MasterDeskProcessor::isParamLocked (const juce::String& id) const
{
    const juce::ScopedLock sl (lockSection);
    return lockedParams.contains (id);
}

void MasterDeskProcessor::setParamLocked (const juce::String& id, bool locked)
{
    const juce::ScopedLock sl (lockSection);
    if (locked)
        lockedParams.addIfNotAlreadyThere (id);
    else
        lockedParams.removeString (id);
}

//==============================================================================
int MasterDeskProcessor::getNumPrograms()             { return presets->getNumFactoryPresets(); }
int MasterDeskProcessor::getCurrentProgram()          { return juce::jmax (0, presets->getCurrentPresetIndex()); }
void MasterDeskProcessor::setCurrentProgram (int i)   { presets->applyPreset (i); }
const juce::String MasterDeskProcessor::getProgramName (int i)
{
    auto names = presets->getPresetNames();
    return i >= 0 && i < names.size() ? names[i] : juce::String();
}

juce::AudioProcessorParameter* MasterDeskProcessor::getBypassParameter() const
{
    return apvts.getParameter (md::param::bypass);
}

//==============================================================================
void MasterDeskProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    {
        const juce::ScopedLock sl (lockSection);
        state.setProperty ("lockedParams", lockedParams.joinIntoString (";"), nullptr);
    }
    state.setProperty ("analogSeed", juce::String (analogSeed), nullptr);
    state.setProperty ("presetName", presets->getCurrentPresetName(), nullptr);

    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void MasterDeskProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
    {
        auto state = juce::ValueTree::fromXml (*xml);
        if (! state.isValid())
            return;

        {
            const juce::ScopedLock sl (lockSection);
            lockedParams.clear();
            lockedParams.addTokens (state.getProperty ("lockedParams").toString(), ";", "");
            lockedParams.removeEmptyStrings();
        }

        const auto seedStr = state.getProperty ("analogSeed").toString();
        if (seedStr.isNotEmpty())
            analogSeed = (uint64_t) seedStr.getLargeIntValue();

        apvts.replaceState (state);
    }
}

//==============================================================================
juce::AudioProcessorEditor* MasterDeskProcessor::createEditor()
{
    return new MasterDeskEditor (*this);
}

// This creates new instances of the plugin
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new MasterDeskProcessor();
}
