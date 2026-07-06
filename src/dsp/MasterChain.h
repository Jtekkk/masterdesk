#pragma once

/*  MasterDesk — the complete mastering chain.

    Signal flow (logical, left to right — exactly what the panel implies):

      IN ─ Volume ─▶ [encode: L/R · M/S] ─▶ Foundation ─▶ Tone ─▶ Compressor
         ─▶ Stereo Enhance ─▶ [decode] ─▶ analog floor (noise/crosstalk)
         ─▶ ▲oversampled THD stage▲ ─▶ Output Trim + AutoGain
         ─▶ True-Peak Limiter ─▶ Wet/Dry (latency-aligned) ─▶ metering ─▶ OUT

    • Internal precision: 64-bit double end to end.
    • Oversampling: off / 2× / 4× / 8× / 16× around the THD stage, with a
      choice of minimum-phase (polyphase IIR, lowest latency) or linear-phase
      (FIR equiripple) anti-alias filtering. Integer-latency mode keeps the
      dry path perfectly phase-aligned for the mix control.
    • The THD stage is instantiated once per oversampling rate so switching
      factors never allocates or re-tunes filters on the audio thread.
    • Every continuous control is ramped (Smoother) — automation is
      zipper-free by construction.
*/

#include <juce_dsp/juce_dsp.h>

#include "Common.h"
#include "AnalogModel.h"
#include "Saturation.h"
#include "Foundation.h"
#include "ToneStack.h"
#include "Compressor.h"
#include "Limiter.h"
#include "Meters.h"

namespace md::dsp
{

class MasterChain
{
public:
    static constexpr int subBlockLen = 64;
    static constexpr int numOsChoices = 5;              // off, 2x, 4x, 8x, 16x

    enum class ChannelMode { stereo = 0, midSide, dualMono };

    struct Params
    {
        double volumeDb        = 0.0;    // -12 .. +12
        double foundation      = 0.0;    // 0 .. 10
        double tonePercent     = 0.0;    // 0 .. 100
        int    toneVoicing     = 0;      // A/B/C/D
        double thdDb           = -90.0;  // -90 .. -24
        double stereoPercent   = 0.0;    // 0 .. 100
        double outputTrimDb    = 0.0;    // -12 .. +12
        int    compMode        = 0;      // 0 soft, 1 hard
        int    channelMode     = 0;      // ChannelMode
        int    osIndex         = 2;      // 0..4 → off,2x,4x,8x,16x
        bool   linearPhase     = false;
        double mixPercent      = 100.0;
        bool   autoGain        = false;
        double analogAmount    = 1.0;    // 0..2
        double temperature     = 0.5;    // 0..1 → 15..45 °C
        bool   truePeak        = true;
        double ceilingDb       = -0.3;
        bool   externalSC      = false;
        double scHpfHz         = 60.0;
        bool   bypassed        = false;
    };

    struct MeterSnapshot
    {
        double dr = 0.0;
        double lufsMomentary = kSilenceDb, lufsShort = kSilenceDb, lufsIntegrated = kSilenceDb;
        double rmsL = kSilenceDb, rmsR = kSilenceDb;
        double peakL = kSilenceDb, peakR = kSilenceDb;
        double truePeakDb = kSilenceDb;
        double compGrDb = 0.0, limGrDb = 0.0;
        double supply = 1.0;
    };

    //==========================================================================
    void prepare (double sampleRate, int maxBlockSize, uint64_t instanceSeed)
    {
        sr = sampleRate;
        analog.prepare (sr, instanceSeed);

        for (int ch = 0; ch < 2; ++ch)
        {
            foundation[ch].prepare (sr, &analog, ch);
            tone[ch].prepare (sr, &analog, ch);
            widthHP[ch].prepare (sr);
        }
        widthHP[0].setCutoff (120.0);
        widthHP[1].setCutoff (120.0);

        comp.prepare (sr, &analog);
        limiter.prepare (sr);
        loudness.prepare (sr);
        dynamics.prepare (sr);
        dynamics.setSampleRate (sr);

        for (int ch = 0; ch < 2; ++ch)
            outTP[ch].prepare();

        // one THD stage set per oversampling rate
        static constexpr int factors[numOsChoices] = { 1, 2, 4, 8, 16 };
        for (int f = 0; f < numOsChoices; ++f)
            for (int ch = 0; ch < 2; ++ch)
                thd[f][ch].prepare (sr * factors[f], &analog, ch);

        // oversamplers: [factorIdx-1][0]=min-phase IIR, [factorIdx-1][1]=linear-phase FIR
        using OS = juce::dsp::Oversampling<double>;
        for (int f = 1; f < numOsChoices; ++f)
        {
            os[f - 1][0] = std::make_unique<OS> (2, (size_t) f, OS::filterHalfBandPolyphaseIIR, true, true);
            os[f - 1][1] = std::make_unique<OS> (2, (size_t) f, OS::filterHalfBandFIREquiripple, true, true);
            os[f - 1][0]->initProcessing ((size_t) std::max (maxBlockSize, subBlockLen));
            os[f - 1][1]->initProcessing ((size_t) std::max (maxBlockSize, subBlockLen));
        }

        volSm.prepare (sr, 30.0);
        trimSm.prepare (sr, 30.0);
        widthSm.prepare (sr, 50.0);
        mixSm.prepare (sr, 60.0);
        agSm.prepare (sr, 800.0);

        volSm.snap (1.0);
        trimSm.snap (1.0);
        widthSm.snap (0.0);
        mixSm.snap (1.0);
        agSm.snap (0.0);

        rmsInEnv = rmsOutEnv = 1.0e-9;
        rmsCoef  = std::exp (-1.0 / (2.0 * sr));       // 2 s loudness match window

        const int maxLatency = limiter.latencySamples() + 64 /* headroom for OS latency */;
        for (int ch = 0; ch < 2; ++ch)
        {
            dryDelay[ch].prepare (maxLatency + 16);
            scDelay[ch].prepare (maxLatency + 16);
        }

        applyParams (params, true);
        updateLatency();
        reset();
    }

    void setParams (const Params& p) noexcept { applyParams (p, false); }

    /** Host-visible latency: oversampler + limiter lookahead. */
    int latencySamples() const noexcept { return totalLatency; }

    //==========================================================================
    /** io: stereo buffer (in place). sc: optional external sidechain, may be
        nullptr. numSamples arbitrary — internally chopped to sub-blocks.     */
    void process (double* const* io, const double* scL, const double* scR, int numSamples) noexcept
    {
        analog.updateDrift();

        int pos = 0;
        while (pos < numSamples)
        {
            const int len = std::min (subBlockLen, numSamples - pos);
            processSubBlock (io[0] + pos, io[1] + pos,
                             scL != nullptr ? scL + pos : nullptr,
                             scR != nullptr ? scR + pos : nullptr,
                             len);
            pos += len;
        }
    }

    MeterSnapshot meters() const noexcept
    {
        MeterSnapshot m;
        m.dr             = dynamics.dr();
        m.lufsMomentary  = loudness.momentaryLUFS();
        m.lufsShort      = loudness.shortTermLUFS();
        m.lufsIntegrated = loudness.integratedLUFS();
        m.rmsL  = dynamics.rms (0);
        m.rmsR  = dynamics.rms (1);
        m.peakL = dynamics.peak (0);
        m.peakR = dynamics.peak (1);
        m.truePeakDb = gainToDb (truePeakHold);
        m.compGrDb = comp.gainReductionDb();
        m.limGrDb  = limiter.gainReductionDb();
        m.supply   = analog.currentSupply();
        return m;
    }

    void resetIntegratedLoudness() noexcept { loudness.resetIntegrated(); }

    void reset()
    {
        for (int ch = 0; ch < 2; ++ch)
        {
            foundation[ch].reset();
            tone[ch].reset();
            widthHP[ch].reset();
            dryDelay[ch].reset();
            scDelay[ch].reset();
            outTP[ch].reset();
        }
        for (int f = 0; f < numOsChoices; ++f)
            for (int ch = 0; ch < 2; ++ch)
                thd[f][ch].reset();
        for (int f = 0; f < numOsChoices - 1; ++f)
            for (int t = 0; t < 2; ++t)
                if (os[f][t] != nullptr) os[f][t]->reset();

        comp.reset();
        limiter.reset();
        truePeakHold = 0.0;
    }

private:
    //==========================================================================
    void applyParams (const Params& p, bool force)
    {
        params = p;

        volSm.setTarget (dbToGain (p.volumeDb));
        trimSm.setTarget (dbToGain (p.outputTrimDb));
        widthSm.setTarget (clampd (p.stereoPercent, 0.0, 100.0) * 0.01);
        mixSm.setTarget (p.bypassed ? 0.0 : clampd (p.mixPercent, 0.0, 100.0) * 0.01);

        analog.setControls (p.analogAmount, p.temperature);

        for (int ch = 0; ch < 2; ++ch)
        {
            foundation[ch].setAmount (p.foundation);
            tone[ch].setControls ((ToneStack::Voicing) p.toneVoicing, p.tonePercent);
            for (int f = 0; f < numOsChoices; ++f)
                thd[f][ch].setAmount (p.thdDb);
        }

        comp.setMode (p.compMode == 0 ? Compressor::Mode::soft : Compressor::Mode::hard);
        comp.setLink (p.channelMode == (int) ChannelMode::dualMono ? Compressor::Link::independent
                    : p.channelMode == (int) ChannelMode::midSide  ? Compressor::Link::partial
                                                                   : Compressor::Link::linked);
        comp.setSidechainHPF (p.scHpfHz);
        comp.setAutoMakeup (true);

        limiter.setCeiling (p.ceilingDb);
        limiter.setTruePeak (p.truePeak);

        if (force || p.osIndex != activeOsIndex || p.linearPhase != activeLinearPhase)
        {
            activeOsIndex     = clampi (p.osIndex, 0, numOsChoices - 1);
            activeLinearPhase = p.linearPhase;
            updateLatency();
        }
    }

    void updateLatency() noexcept
    {
        osLatency = 0;
        if (activeOsIndex > 0)
        {
            auto& o = os[activeOsIndex - 1][activeLinearPhase ? 1 : 0];
            if (o != nullptr)
                osLatency = (int) std::lround ((double) o->getLatencyInSamples());
        }
        totalLatency = osLatency + limiter.latencySamples();

        for (int ch = 0; ch < 2; ++ch)
        {
            dryDelay[ch].setDelay (totalLatency);
            scDelay[ch].setDelay (0);
        }
    }

    static int clampi (int v, int lo, int hi) noexcept { return v < lo ? lo : (v > hi ? hi : v); }

    //==========================================================================
    void processSubBlock (double* l, double* r,
                          const double* scL, const double* scR, int len) noexcept
    {
        const bool ms = params.channelMode == (int) ChannelMode::midSide;
        double meanSq = 0.0;

        // ---- stage A: base-rate front half --------------------------------
        for (int i = 0; i < len; ++i)
        {
            double L = l[i], R = r[i];

            // latency-aligned dry copy
            dryBuf[0][i] = dryDelay[0].process (L);
            dryBuf[1][i] = dryDelay[1].process (R);

            // input loudness estimate (for auto-gain matching)
            rmsInEnv = rmsInEnv * rmsCoef + (L * L + R * R) * 0.5 * (1.0 - rmsCoef);

            const double v = volSm.next();
            L *= v; R *= v;

            // processing domain
            double a = L, b = R;
            if (ms) { a = (L + R) * 0.5; b = (L - R) * 0.5; }

            a = foundation[0].process (a);
            b = foundation[1].process (b);
            a = tone[0].process (a);
            b = tone[1].process (b);

            // sidechain key (external bus enters the same domain)
            double ka = a, kb = b;
            if (params.externalSC && scL != nullptr)
            {
                const double eL = scL[i];
                const double eR = scR != nullptr ? scR[i] : eL;
                if (ms) { ka = (eL + eR) * 0.5; kb = (eL - eR) * 0.5; }
                else    { ka = eL; kb = eR; }
            }

            comp.process (a, b, ka, kb);

            // stereo enhance: operate on the side signal (bass stays mono)
            const double w = widthSm.next();
            {
                double m = a, s = b;
                if (! ms) { m = (a + b) * 0.5; s = (a - b) * 0.5; }
                const double sHi = widthHP[0].processHP (s);
                s += sHi * (w * 1.0);                 // up to +6 dB of HF side
                if (! ms) { a = m + s; b = m - s; }
                else      { b = s; }
            }

            // back to L/R
            if (ms) { L = a + b; R = a - b; }
            else    { L = a;     R = b;     }

            if (analog.isActive())
            {
                analog.applyCrosstalk (L, R);
                L += analog.noise (0);
                R += analog.noise (1);
            }

            work[0][i] = L;
            work[1][i] = R;
            meanSq += (L * L + R * R) * 0.5;
        }

        analog.updateSupply (meanSq / (double) len, len);

        // ---- stage B: oversampled THD -------------------------------------
        if (activeOsIndex == 0)
        {
            for (int i = 0; i < len; ++i)
            {
                work[0][i] = thd[0][0].process (work[0][i]);
                work[1][i] = thd[0][1].process (work[1][i]);
            }
        }
        else
        {
            auto& o = *os[activeOsIndex - 1][activeLinearPhase ? 1 : 0];
            double* chans[2] = { work[0], work[1] };
            juce::dsp::AudioBlock<double> block (chans, 2, (size_t) len);
            auto up = o.processSamplesUp (block);

            auto* u0 = up.getChannelPointer (0);
            auto* u1 = up.getChannelPointer (1);
            const int upLen = (int) up.getNumSamples();
            auto& t0 = thd[activeOsIndex][0];
            auto& t1 = thd[activeOsIndex][1];
            for (int i = 0; i < upLen; ++i)
            {
                u0[i] = t0.process (u0[i]);
                u1[i] = t1.process (u1[i]);
            }
            o.processSamplesDown (block);
        }

        // ---- stage C: trim, auto-gain, limit, mix, meter -------------------
        for (int i = 0; i < len; ++i)
        {
            double L = work[0][i], R = work[1][i];

            // measured loudness match (feed-forward: measured pre-correction)
            rmsOutEnv = rmsOutEnv * rmsCoef + (L * L + R * R) * 0.5 * (1.0 - rmsCoef);

            double agDb = 0.0;
            if (params.autoGain)
            {
                agDb = 5.0 * (std::log10 (rmsInEnv + 1.0e-15) - std::log10 (rmsOutEnv + 1.0e-15));
                agDb = clampd (agDb, -12.0, 12.0);
            }
            agSm.setTarget (agDb);
            const double g = trimSm.next() * dbToGain (agSm.next());
            L *= g; R *= g;

            limiter.process (L, R);

            const double m = mixSm.next();
            L = dryBuf[0][i] * (1.0 - m) + L * m;
            R = dryBuf[1][i] * (1.0 - m) + R * m;

            loudness.push (L, R);
            dynamics.push (L, R);
            const double tp = std::max (outTP[0].process (L), outTP[1].process (R));
            truePeakHold = std::max (truePeakHold * 0.99995, tp);

            l[i] = L;
            r[i] = R;
        }
    }

    //==========================================================================
    double sr = 48000.0;
    Params params;

    AnalogModel analog;
    Foundation  foundation[2];
    ToneStack   tone[2];
    THDStage    thd[numOsChoices][2];
    Compressor  comp;
    Limiter     limiter;
    OnePoleTPT  widthHP[2];

    std::unique_ptr<juce::dsp::Oversampling<double>> os[numOsChoices - 1][2];
    int  activeOsIndex = 0;
    bool activeLinearPhase = false;
    int  osLatency = 0, totalLatency = 0;

    Smoother volSm, trimSm, widthSm, mixSm, agSm;

    Delay dryDelay[2], scDelay[2];
    double dryBuf[2][subBlockLen] {};
    double work[2][subBlockLen] {};

    double rmsInEnv = 0.0, rmsOutEnv = 0.0, rmsCoef = 0.9999;

    LoudnessMeter   loudness;
    DynamicsMeter   dynamics;
    TruePeakDetector outTP[2];
    double truePeakHold = 0.0;
};

} // namespace md::dsp
