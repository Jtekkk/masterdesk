#pragma once

/*  MasterDesk — the central "Dynamic Range" meter.

    A vector recreation of the reference hardware meter:
      • cream VU face, scale 12…3 dB with green comfort zone and red
        over-compression zone, ballistic needle,
      • "Analog Mastering System" legend and a DR LCD readout,
      • an LED ladder on the right showing output level,
      • click the face (or the SPEC button) to flip the face into a
        log-frequency spectrum analyser (4096-point FFT, peak-hold decay).

    All drawing is resolution-independent.
*/

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_dsp/juce_dsp.h>

namespace md::ui
{

class MeterPanel : public juce::Component
{
public:
    MeterPanel();

    /** Called from the editor's timer. Values in dB. */
    void update (float dr, float peakDb, float lufsShort, float lufsIntegrated,
                 float compGrDb, float limGrDb, float truePeakDb);

    /** Feed post-output mono samples for the analyser. */
    void pushSamples (const float* data, int num);

    void setSpectrumMode (bool shouldShowSpectrum);
    bool isSpectrumMode() const noexcept        { return spectrumMode; }

    void paint (juce::Graphics&) override;
    void mouseUp (const juce::MouseEvent&) override;

    std::function<void (bool)> onSpectrumModeChange;

private:
    void drawScale (juce::Graphics&, juce::Rectangle<float> face);
    void drawNeedle (juce::Graphics&, juce::Rectangle<float> face);
    void drawSpectrum (juce::Graphics&, juce::Rectangle<float> face);
    void drawLedLadder (juce::Graphics&, juce::Rectangle<float> strip);
    void drawReadoutRow (juce::Graphics&, juce::Rectangle<float> row);

    float angleForValue (float v) const;
    void runFFT();

    // meter state (UI-side ballistics)
    float drTarget = 0.0f, drSmoothed = 0.0f;
    float peakDb = -120.0f, lufsS = -120.0f, lufsI = -120.0f;
    float compGr = 0.0f, limGr = 0.0f, truePeak = -120.0f;

    bool spectrumMode = false;

    // analyser
    static constexpr int fftOrder = 12;
    static constexpr int fftSize  = 1 << fftOrder;
    juce::dsp::FFT fft { fftOrder };
    std::vector<float> window, fifo, fftData, displayBins;
    int fifoFill = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MeterPanel)
};

} // namespace md::ui
