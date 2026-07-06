#pragma once

/*  MasterDesk — mastering compressor.

    bx_masterdesk-style operation: the threshold is fixed and the VOLUME knob
    drives the programme into it, so "more volume" = "more glue" with one
    gesture. Character:

      • SOFT — 2:1, wide 12 dB knee, slower — invisible glue.
        HARD — 4:1, 5 dB knee, faster — modern punch.
      • Feedback-flavoured detector: the sidechain listens to a blend of the
        stage input and the *previous output* sample, which is what gives
        vintage feedback compressors their forgiving curve (nonlinear
        feedback around the gain element).
      • Programme-dependent timing: attack shortens on transient-dense
        material; release runs on two coupled envelopes (fast + slow) whose
        balance follows how long gain reduction has been held — fast recovery
        on peaks, slow crawl on sustained density. Zipper-free by
        construction (everything is a one-pole in the log domain).
      • Sidechain: internal (post-Volume programme) or external bus, both
        through a variable high-pass so bass doesn't pump the master.
      • Link modes: fully linked stereo, dual mono (independent L/R), or
        M/S with 50 % partial linking.
      • Auto make-up derived from a slowly smoothed average of the actual
        gain reduction — intelligent gain compensation without pumping.
*/

#include "Common.h"
#include "AnalogModel.h"

namespace md::dsp
{

class Compressor
{
public:
    enum class Mode { soft = 0, hard };
    enum class Link { linked = 0, independent, partial };

    void prepare (double sampleRate, AnalogModel* modelIn)
    {
        sr = sampleRate; model = modelIn;

        for (int ch = 0; ch < 2; ++ch)
        {
            scHpf[ch].prepare (sr);
            scHpf[ch].setCutoff (60.0);
            detEnv[ch] = 0.0;
            grFast[ch] = grSlow[ch] = 0.0;
            prevOut[ch] = 0.0;
        }
        holdEnv = 0.0;
        makeupSm.prepare (sr, 400.0);
        makeupSm.snap (0.0);
        setMode (Mode::soft);
        updateCoeffs();
    }

    void setMode (Mode m) noexcept
    {
        mode = m;
        if (mode == Mode::soft) { ratio = 2.0; kneeDb = 12.0; baseAttackMs = 25.0; }
        else                    { ratio = 4.0; kneeDb = 5.0;  baseAttackMs = 8.0;  }
        updateCoeffs();
    }

    void setLink (Link l) noexcept                  { link = l; }
    void setSidechainHPF (double hz) noexcept       { scHpf[0].setCutoff (hz); scHpf[1].setCutoff (hz); }
    void setAutoMakeup (bool on) noexcept           { autoMakeup = on; }

    /** Threshold is nominally fixed; analog tolerances skew it slightly. */
    void setThreshold (double db) noexcept          { thresholdDb = db; }

    /** l/r: in/out audio. kl/kr: sidechain key (pass the same as l/r for the
        internal chain, or the external bus samples). */
    inline void process (double& l, double& r, double kl, double kr) noexcept
    {
        // --- detector -------------------------------------------------------
        // feedback blend: 70 % key, 30 % previous output
        double dl = scHpf[0].processHP (kl * 0.7 + prevOut[0] * 0.3);
        double dr = scHpf[1].processHP (kr * 0.7 + prevOut[1] * 0.3);

        const double lvlL = detectorLevel (0, dl);
        const double lvlR = detectorLevel (1, dr);

        double grL = computeGR (lvlL, 0);
        double grR = computeGR (lvlR, 1);

        switch (link)
        {
            case Link::linked:      grL = grR = std::max (grL, grR); break;
            case Link::independent: break;
            case Link::partial:
            {
                const double mx = std::max (grL, grR);
                grL = grL * 0.5 + mx * 0.5;
                grR = grR * 0.5 + mx * 0.5;
                break;
            }
        }

        const double smL = smoothGR (0, grL);
        const double smR = smoothGR (1, grR);

        currentGrDb = std::max (smL, smR);

        // --- auto make-up ----------------------------------------------------
        makeupSm.setTarget (autoMakeup ? currentGrDb * 0.7 : 0.0);
        const double mk = dbToGain (makeupSm.next());

        l *= dbToGain (-smL) * mk;
        r *= dbToGain (-smR) * mk;

        prevOut[0] = sanitize (l);
        prevOut[1] = sanitize (r);
    }

    double gainReductionDb() const noexcept { return currentGrDb; }

    void reset() noexcept
    {
        for (int ch = 0; ch < 2; ++ch)
        {
            scHpf[ch].reset();
            detEnv[ch] = grFast[ch] = grSlow[ch] = prevOut[ch] = 0.0;
        }
        holdEnv = 0.0;
    }

private:
    void updateCoeffs() noexcept
    {
        aAttBase  = std::exp (-1.0 / (0.001 * baseAttackMs * sr));
        aRelFast  = std::exp (-1.0 / (0.120 * sr));
        aRelSlow  = std::exp (-1.0 / (0.900 * sr));
        aDetAtt   = std::exp (-1.0 / (0.0008 * sr));
        aDetRel   = std::exp (-1.0 / (0.060  * sr));
        aHold     = std::exp (-1.0 / (0.500  * sr));
    }

    inline double detectorLevel (int ch, double d) noexcept
    {
        const double rect = std::abs (d);
        const double a = rect > detEnv[ch] ? aDetAtt : aDetRel;
        detEnv[ch] = sanitize (detEnv[ch] * a + rect * (1.0 - a));
        return gainToDb (detEnv[ch]);
    }

    inline double computeGR (double levelDb, int ch) const noexcept
    {
        const double tol = model != nullptr ? model->tolerance (ch, 12) : 1.0;
        const double th  = thresholdDb * tol;
        const double over = levelDb - th;
        const double slope = 1.0 - 1.0 / ratio;

        if (2.0 * over <= -kneeDb) return 0.0;
        if (2.0 * over >=  kneeDb) return slope * over;
        const double t = over + kneeDb * 0.5;
        return slope * t * t / (2.0 * kneeDb);
    }

    inline double smoothGR (int ch, double target) noexcept
    {
        // programme-dependent attack: transient density shortens it
        const double transientFactor = clampd (target - grFast[ch], 0.0, 12.0) / 12.0;
        const double aAtt = aAttBase * (1.0 - 0.6 * transientFactor);

        // dual-envelope release, balance follows sustained-GR "hold" state
        holdEnv = sanitize (holdEnv * aHold + target * (1.0 - aHold));
        const double sustain = clampd (holdEnv / 6.0, 0.0, 1.0);

        auto step = [] (double env, double tgt, double aAttack, double aRelease)
        {
            const double a = tgt > env ? aAttack : aRelease;
            return sanitize (env * a + tgt * (1.0 - a));
        };

        grFast[ch] = step (grFast[ch], target, aAtt, aRelFast);
        grSlow[ch] = step (grSlow[ch], target, aAtt, aRelSlow);

        return grFast[ch] * (1.0 - sustain) + grSlow[ch] * sustain;
    }

    double sr = 48000.0;
    AnalogModel* model = nullptr;

    Mode mode = Mode::soft;
    Link link = Link::linked;
    double ratio = 2.0, kneeDb = 12.0, thresholdDb = -16.0;
    double baseAttackMs = 25.0;
    bool autoMakeup = true;

    OnePoleTPT scHpf[2];
    double detEnv[2] {}, grFast[2] {}, grSlow[2] {}, prevOut[2] {};
    double holdEnv = 0.0;
    double aAttBase = 0, aRelFast = 0, aRelSlow = 0, aDetAtt = 0, aDetRel = 0, aHold = 0;

    Smoother makeupSm;
    double currentGrDb = 0.0;
};

} // namespace md::dsp
