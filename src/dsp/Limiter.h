#pragma once

/*  MasterDesk — true-peak lookahead limiter (the output safety stage).

    • 2 ms lookahead: the audio is delayed while a sliding-window maximum
      (monotonic wedge, O(1)/sample, allocation-free) sees the future and the
      gain computer ramps *ahead* of transients — no overshoot, no clicks,
      transient shape preserved as long as possible.
    • True-peak detection: the detector path is upsampled 4× through a
      polyphase windowed-sinc interpolator (BS.1770 style), so inter-sample
      peaks are caught before they become D/A overs. Switchable.
    • Programme-dependent release: dual time constants blended by how long
      limiting has been sustained — fast on isolated peaks, slow on density,
      which keeps distortion low without pumping.
    • Final ceiling guard: an exact soft-knee clip at the ceiling catches the
      residual (< 0.1 dB) that any smoothed limiter lets through.
*/

#include "Common.h"

namespace md::dsp
{

//==============================================================================
/** 4× polyphase true-peak upsampling detector (per channel).                 */
class TruePeakDetector
{
public:
    static constexpr int phases = 4;
    static constexpr int tapsPerPhase = 12;

    void prepare()
    {
        // windowed-sinc lowpass at 0.45·fs, 48 taps, split into 4 phases
        constexpr int total = phases * tapsPerPhase;
        for (int i = 0; i < total; ++i)
        {
            const double n = (double) i - (double) (total - 1) / 2.0;
            const double x = n / (double) phases;
            const double sinc = x == 0.0 ? 1.0 : std::sin (kPi * 0.9 * x) / (kPi * x);
            // Blackman-Harris window
            const double w = 0.35875
                           - 0.48829 * std::cos (2.0 * kPi * i / (total - 1))
                           + 0.14128 * std::cos (4.0 * kPi * i / (total - 1))
                           - 0.01168 * std::cos (6.0 * kPi * i / (total - 1));
            coeff[i % phases][i / phases] = sinc * w * 0.9;
        }
        std::fill (history, history + tapsPerPhase, 0.0);
        writeIdx = 0;
    }

    /** Push one sample, get the maximum absolute interpolated value around it. */
    inline double process (double x) noexcept
    {
        history[writeIdx] = x;
        writeIdx = (writeIdx + 1) % tapsPerPhase;

        double peak = std::abs (x);
        for (int p = 1; p < phases; ++p)   // phase 0 ≈ the sample itself
        {
            double acc = 0.0;
            int idx = writeIdx;
            for (int t = 0; t < tapsPerPhase; ++t)
            {
                acc += coeff[p][t] * history[idx];
                idx = (idx + 1) % tapsPerPhase;
            }
            peak = std::max (peak, std::abs (acc));
        }
        return peak;
    }

    void reset() noexcept { std::fill (history, history + tapsPerPhase, 0.0); }

private:
    double coeff[phases][tapsPerPhase] {};
    double history[tapsPerPhase] {};
    int writeIdx = 0;
};

//==============================================================================
class Limiter
{
public:
    void prepare (double sampleRate)
    {
        sr = sampleRate;
        lookaheadSamples = std::max (16, (int) std::round (sr * 0.002));   // 2 ms

        for (int ch = 0; ch < 2; ++ch)
        {
            delay[ch].prepare (lookaheadSamples + 8);
            delay[ch].setDelay (lookaheadSamples);
            delay[ch].reset();
            tpDetector[ch].prepare();
        }
        windowMax.prepare (lookaheadSamples);

        // attack smoothing shorter than the lookahead → gain lands early
        aAtt      = std::exp (-1.0 / (0.0006 * sr));
        aRelFast  = std::exp (-1.0 / (0.060 * sr));
        aRelSlow  = std::exp (-1.0 / (0.600 * sr));
        aHold     = std::exp (-1.0 / (0.800 * sr));

        gain = 1.0;
        gain2 = 1.0;
        holdEnv = 0.0;
    }

    void setCeiling (double dbTP) noexcept   { ceiling = dbToGain (clampd (dbTP, -6.0, 0.0)); }
    void setTruePeak (bool on) noexcept      { truePeak = on; }

    int latencySamples() const noexcept      { return lookaheadSamples; }

    inline void process (double& l, double& r) noexcept
    {
        // ---- detection (pre-delay) ----
        double peak = std::max (std::abs (l), std::abs (r));
        if (truePeak)
            peak = std::max (tpDetector[0].process (l), tpDetector[1].process (r));

        const double futureMax = windowMax.push (peak);
        const double target = futureMax > ceiling ? ceiling / futureMax : 1.0;

        // ---- programme-dependent smoothing ----
        const double grNow = 1.0 - target;
        holdEnv = sanitize (holdEnv * aHold + grNow * (1.0 - aHold));
        const double sustain = clampd (holdEnv * 12.0, 0.0, 1.0);
        const double aRel = aRelFast * (1.0 - sustain) + aRelSlow * sustain;

        auto step = [] (double g, double t, double aA, double aR)
        {
            const double a = t < g ? aA : aR;
            return sanitize (g * a + t * (1.0 - a));
        };
        gain  = step (gain,  target, aAtt, aRel);
        gain2 = step (gain2, gain,   aAtt, aRel);      // cascaded → smoother knee

        currentGrDb = -gainToDb (std::max (gain2, 1.0e-6));

        // ---- delayed audio × gain ----
        double dl = delay[0].process (l) * gain2;
        double dr = delay[1].process (r) * gain2;

        // ---- exact ceiling guard (soft into hard at the ceiling) ----
        l = ceilingClip (dl);
        r = ceilingClip (dr);
    }

    double gainReductionDb() const noexcept { return currentGrDb; }

    void reset() noexcept
    {
        for (int ch = 0; ch < 2; ++ch) { delay[ch].reset(); tpDetector[ch].reset(); }
        windowMax.prepare (lookaheadSamples);
        gain = gain2 = 1.0;
        holdEnv = 0.0;
    }

private:
    inline double ceilingClip (double x) const noexcept
    {
        const double c = ceiling;
        const double knee = c * 0.05;                 // last 0.4 dB rounded
        const double a = std::abs (x);
        if (a <= c - knee) return x;
        const double sign = x < 0.0 ? -1.0 : 1.0;
        const double t = (a - (c - knee)) / knee;     // 0..∞
        const double soft = (c - knee) + knee * std::tanh (t);
        return sign * std::min (soft, c);
    }

    double sr = 48000.0;
    int lookaheadSamples = 96;
    double ceiling = dbToGain (-0.3);
    bool truePeak = true;

    Delay delay[2];
    TruePeakDetector tpDetector[2];
    SlidingMax windowMax;

    double aAtt = 0, aRelFast = 0, aRelSlow = 0, aHold = 0;
    double gain = 1.0, gain2 = 1.0, holdEnv = 0.0;
    double currentGrDb = 0.0;
};

} // namespace md::dsp
