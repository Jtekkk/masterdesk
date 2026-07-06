#pragma once

/*  MasterDesk — measurement engine.

    • K-weighted loudness per ITU-R BS.1770-4: pre-filter (high shelf
      +4 dB @ ~1.68 kHz) + RLB high-pass @ ~38 Hz, energy summed across
      channels, -0.691 dB offset.
    • Momentary (400 ms) and Short-term (3 s) LUFS from overlapping windows.
    • Integrated LUFS with the full two-stage gate (absolute -70 LUFS, then
      relative -10 LU) using a bounded histogram — constant memory however
      long the session runs.
    • RMS (300 ms), sample peak and DR (crest-factor based dynamic-range
      estimate over a 3 s window — the needle on the front panel).

    Pure C++, no framework dependencies, 64-bit throughout.
*/

#include "Common.h"
#include <array>

namespace md::dsp
{

//==============================================================================
/** RBJ biquad — only used at control level (metering), Direct Form II t.     */
struct Biquad
{
    void setHighShelf (double sr, double fc, double q, double gainDb) noexcept
    {
        const double A  = std::pow (10.0, gainDb / 40.0);
        const double w  = 2.0 * kPi * fc / sr;
        const double cs = std::cos (w), sn = std::sin (w);
        const double alpha = sn / (2.0 * q);
        const double beta  = 2.0 * std::sqrt (A) * alpha;

        const double a0 = (A + 1.0) - (A - 1.0) * cs + beta;
        b0 = (A * ((A + 1.0) + (A - 1.0) * cs + beta)) / a0;
        b1 = (-2.0 * A * ((A - 1.0) + (A + 1.0) * cs)) / a0;
        b2 = (A * ((A + 1.0) + (A - 1.0) * cs - beta)) / a0;
        a1 = (2.0 * ((A - 1.0) - (A + 1.0) * cs)) / a0;
        a2 = ((A + 1.0) - (A - 1.0) * cs - beta) / a0;
    }

    void setHighPass (double sr, double fc, double q) noexcept
    {
        const double w  = 2.0 * kPi * fc / sr;
        const double cs = std::cos (w), sn = std::sin (w);
        const double alpha = sn / (2.0 * q);

        const double a0 = 1.0 + alpha;
        b0 = ((1.0 + cs) * 0.5) / a0;
        b1 = (-(1.0 + cs)) / a0;
        b2 = ((1.0 + cs) * 0.5) / a0;
        a1 = (-2.0 * cs) / a0;
        a2 = (1.0 - alpha) / a0;
    }

    inline double process (double x) noexcept
    {
        const double y = b0 * x + z1;
        z1 = sanitize (b1 * x - a1 * y + z2);
        z2 = sanitize (b2 * x - a2 * y);
        return y;
    }

    void reset() noexcept { z1 = z2 = 0.0; }

    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
};

//==============================================================================
class LoudnessMeter
{
public:
    static constexpr double absoluteGate = -70.0;

    void prepare (double sampleRate)
    {
        sr = sampleRate;
        for (int ch = 0; ch < 2; ++ch)
        {
            shelf[ch].setHighShelf (sr, 1681.9744509555319, 0.7071752369554196, 3.999843853973347);
            hp[ch].setHighPass    (sr, 38.13547087602444, 0.5003270373238773);
            shelf[ch].reset(); hp[ch].reset();
        }

        hopLen = std::max (1, (int) std::round (sr * 0.100));   // 100 ms hops
        hopAccum = 0.0;
        hopCount = 0;
        hopRing.fill (0.0);
        hopWrite = 0;
        histCount.fill (0);
        histEnergy.fill (0.0);
        momentary = shortTerm = integrated = kSilenceDb;
    }

    /** Feed one stereo sample (post-output). */
    inline void push (double l, double r) noexcept
    {
        const double kl = hp[0].process (shelf[0].process (l));
        const double kr = hp[1].process (shelf[1].process (r));
        hopAccum += kl * kl + kr * kr;

        if (++hopCount >= hopLen)
        {
            commitHop (hopAccum / (double) hopLen);
            hopAccum = 0.0;
            hopCount = 0;
        }
    }

    double momentaryLUFS()  const noexcept { return momentary; }
    double shortTermLUFS()  const noexcept { return shortTerm; }
    double integratedLUFS() const noexcept { return integrated; }

    void resetIntegrated() noexcept
    {
        histCount.fill (0);
        histEnergy.fill (0.0);
        integrated = kSilenceDb;
    }

private:
    static double energyToLUFS (double meanSquare) noexcept
    {
        return -0.691 + 10.0 * std::log10 (std::max (meanSquare, 1.0e-15));
    }

    void commitHop (double meanSquare) noexcept
    {
        hopRing[(size_t) hopWrite] = meanSquare;
        hopWrite = (hopWrite + 1) % (int) hopRing.size();

        // momentary = last 4 hops (400 ms), short-term = last 30 (3 s)
        momentary = energyToLUFS (windowMean (4));
        shortTerm = energyToLUFS (windowMean (30));

        // integrated gating on 400 ms blocks at 100 ms hops
        const double blockMS = windowMean (4);
        const double blockLUFS = energyToLUFS (blockMS);
        if (blockLUFS >= absoluteGate)
        {
            const int bin = binFor (blockLUFS);
            ++histCount[(size_t) bin];
            histEnergy[(size_t) bin] += blockMS;
            if (++recalcCountdown >= 10)   // once a second
            {
                recalcCountdown = 0;
                recomputeIntegrated();
            }
        }
    }

    double windowMean (int hops) const noexcept
    {
        double sum = 0.0;
        int idx = hopWrite;
        for (int i = 0; i < hops; ++i)
        {
            idx = (idx + (int) hopRing.size() - 1) % (int) hopRing.size();
            sum += hopRing[(size_t) idx];
        }
        return sum / (double) hops;
    }

    static int binFor (double lufs) noexcept
    {
        const int bin = (int) ((lufs - absoluteGate) * 10.0);   // 0.1 LU bins
        return bin < 0 ? 0 : (bin >= numBins ? numBins - 1 : bin);
    }

    void recomputeIntegrated() noexcept
    {
        // stage 1: mean of everything above the absolute gate
        double e = 0.0; int64_t n = 0;
        for (int i = 0; i < numBins; ++i) { e += histEnergy[(size_t) i]; n += histCount[(size_t) i]; }
        if (n == 0) { integrated = kSilenceDb; return; }
        const double relThreshold = energyToLUFS (e / (double) n) - 10.0;

        // stage 2: mean of blocks above the relative gate
        const int startBin = binFor (relThreshold);
        e = 0.0; n = 0;
        for (int i = startBin; i < numBins; ++i) { e += histEnergy[(size_t) i]; n += histCount[(size_t) i]; }
        integrated = n == 0 ? kSilenceDb : energyToLUFS (e / (double) n);
    }

    static constexpr int numBins = 701;   // -70 .. 0 LUFS, 0.1 LU each

    double sr = 48000.0;
    Biquad shelf[2], hp[2];

    int hopLen = 4800, hopCount = 0, hopWrite = 0, recalcCountdown = 0;
    double hopAccum = 0.0;
    std::array<double, 32> hopRing {};

    std::array<int64_t, numBins> histCount {};
    std::array<double,  numBins> histEnergy {};

    double momentary = kSilenceDb, shortTerm = kSilenceDb, integrated = kSilenceDb;
};

//==============================================================================
/** RMS / peak / crest-factor DR over a 3 s window, plus per-channel level.   */
class DynamicsMeter
{
public:
    void prepare (double sampleRate)
    {
        sr = sampleRate;
        hopLen = std::max (1, (int) std::round (sr * 0.100));
        hopCount = 0;
        hopEnergy[0] = hopEnergy[1] = 0.0;
        hopPeak = 0.0;
        ringE.fill (0.0);
        ringP.fill (0.0);
        ringWrite = 0;
        rmsDb[0] = rmsDb[1] = kSilenceDb;
        peakHold[0] = peakHold[1] = 0.0;
        drValue = 0.0;
    }

    inline void push (double l, double r) noexcept
    {
        hopEnergy[0] += l * l;
        hopEnergy[1] += r * r;
        const double p = std::max (std::abs (l), std::abs (r));
        hopPeak = std::max (hopPeak, p);
        peakHold[0] = std::max (peakHold[0] * peakDecay, std::abs (l));
        peakHold[1] = std::max (peakHold[1] * peakDecay, std::abs (r));

        if (++hopCount >= hopLen)
        {
            const double msL = hopEnergy[0] / (double) hopLen;
            const double msR = hopEnergy[1] / (double) hopLen;

            // 300 ms RMS ≈ 3-hop smoothing
            rmsSmooth[0] = rmsSmooth[0] * 0.67 + msL * 0.33;
            rmsSmooth[1] = rmsSmooth[1] * 0.67 + msR * 0.33;
            rmsDb[0] = 10.0 * std::log10 (std::max (rmsSmooth[0], 1.0e-15));
            rmsDb[1] = 10.0 * std::log10 (std::max (rmsSmooth[1], 1.0e-15));

            ringE[(size_t) ringWrite] = (msL + msR) * 0.5;
            ringP[(size_t) ringWrite] = hopPeak;
            ringWrite = (ringWrite + 1) % (int) ringE.size();

            double e = 0.0, pk = 0.0;
            for (size_t i = 0; i < ringE.size(); ++i)
            {
                e += ringE[i];
                pk = std::max (pk, ringP[i]);
            }
            e /= (double) ringE.size();

            const double rmsDbW  = 10.0 * std::log10 (std::max (e, 1.0e-15));
            const double peakDbW = gainToDb (pk);
            drValue = (peakDbW < -60.0) ? 0.0 : clampd (peakDbW - rmsDbW, 0.0, 30.0);

            hopEnergy[0] = hopEnergy[1] = 0.0;
            hopPeak = 0.0;
            hopCount = 0;
        }
    }

    void setSampleRate (double s) noexcept { sr = s; peakDecay = std::exp (-1.0 / (1.5 * sr)); }

    double dr()            const noexcept { return drValue; }
    double rms (int ch)    const noexcept { return rmsDb[ch & 1]; }
    double peak (int ch)   const noexcept { return gainToDb (peakHold[ch & 1]); }

private:
    double sr = 48000.0;
    int hopLen = 4800, hopCount = 0, ringWrite = 0;
    double hopEnergy[2] {}, hopPeak = 0.0;
    double rmsSmooth[2] {}, rmsDb[2] {};
    double peakHold[2] {};
    double peakDecay = 0.99999;
    std::array<double, 30> ringE {}, ringP {};
    double drValue = 0.0;
};

} // namespace md::dsp
