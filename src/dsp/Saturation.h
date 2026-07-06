#pragma once

/*  MasterDesk — nonlinear stages.

    THDStage
    --------
    The main harmonic generator behind the THD knob. Runs inside the
    oversampled section of the chain. Features:

      • Frequency-dependent saturation: a gentle low tilt (pre-emphasis) into
        the shaper and its exact inverse afterwards means low frequencies
        drive the nonlinearity harder than highs — transformer/console
        behaviour rather than fuzz.
      • Dynamic harmonics: drive is modulated by a programme envelope, so the
        harmonic content breathes with the material instead of being static.
      • Bias interaction: the shaper's operating point moves slowly with the
        programme envelope and the analog model's temperature/drift bias,
        producing level-dependent even harmonics like a hot valve stage.
      • Memory / hysteresis: the shaper input includes a small amount of the
        previous shaper *output* (nonlinear feedback), which gives the
        transfer curve a rate-dependent, hysteretic character.
      • PSU sag: the clip point follows the analog model's supply rail.
      • Coupling caps: a charge-modelled DC blocker removes the bias offset.

    Transformer
    -----------
    A low-frequency saturation model used by the Foundation stage: the band
    below ~150 Hz passes through a hysteretic magnetic-core shaper (simplified
    Jiles-Atherton flavour: the core "remembers" recent flux) while the rest
    of the spectrum passes clean.
*/

#include "Common.h"
#include "AnalogModel.h"

namespace md::dsp
{

//==============================================================================
class THDStage
{
public:
    void prepare (double sampleRate, AnalogModel* modelIn, int channel)
    {
        sr    = sampleRate;
        model = modelIn;
        ch    = channel;

        preTilt.prepare (sr);
        postTilt.prepare (sr);
        dcBlock.prepare (sr);
        dcBlock.setCutoff (5.0);
        env.prepare (sr);
        env.setTimes (5.0, 120.0);
        slowEnv.prepare (sr);
        slowEnv.setTimes (400.0, 1200.0);
        memory = 0.0;
        updateTilt();
    }

    /** thdDb: target harmonic level in dB (-90 .. -24). */
    void setAmount (double thdDb) noexcept
    {
        // -90 dB → nearly clean, -24 dB → hot. Exponential mapping.
        drive = std::pow (10.0, (thdDb + 24.0) / 40.0) * 1.10;
        drive = clampd (drive, 0.0, 1.4);
        makeup = 1.0 / (1.0 + drive * 0.22);          // keep unity-ish gain
    }

    inline double process (double x) noexcept
    {
        const double ax  = std::abs (x);
        const double e   = env.process (ax);          // fast programme envelope
        const double eS  = slowEnv.process (ax);      // slow bias envelope

        // Dynamic drive: harmonics rise faster than level (like real iron/valves)
        const double dNow = drive * (0.55 + 0.9 * clampd (e * 1.8, 0.0, 1.0));

        // Bias point: static analog bias + programme-dependent shift
        const double bias = (model != nullptr ? model->biasOffset (ch) : 0.0)
                            + eS * 0.035 * drive;

        // Supply rail scales the headroom (PSU sag)
        const double rail = model != nullptr ? model->currentSupply() : 1.0;

        // Frequency tilt into the shaper (LF driven harder)
        double v = preTilt.process (x);

        // Hysteresis: tiny nonlinear feedback of the previous output
        v += memory * 0.06 * dNow;

        // The shaper itself — soft, symmetric core + bias for even harmonics
        const double headroom = rail;
        const double t = (v * dNow + bias) / headroom;
        double y = headroom * (std::tanh (t) - std::tanh (bias / headroom));

        // Blend: keep the linear core dominant so it stays a mastering tool
        const double wet = clampd (dNow * 0.85, 0.0, 1.0);
        y = x + (y / std::max (dNow, 1.0e-9) - x) * wet;

        memory = sanitize (std::tanh (y));

        // Coupling capacitor
        y = dcBlock.process (y);

        // Inverse tilt restores the macro frequency balance
        y = postTilt.process (y);

        return y * makeup;
    }

    void reset() noexcept
    {
        preTilt.reset(); postTilt.reset();
        dcBlock.prepare (sr);
        memory = 0.0;
    }

private:
    void updateTilt() noexcept
    {
        const double tol = model != nullptr ? model->tolerance (ch, 3) : 1.0;
        // +2.5 dB low shelf in, -2.5 dB out at ~250 Hz → LF-weighted drive
        preTilt.setup  (ShelfTPT::Type::lowShelf, 250.0 * tol,  2.5);
        postTilt.setup (ShelfTPT::Type::lowShelf, 250.0 * tol, -2.5);
    }

    double sr = 48000.0;
    AnalogModel* model = nullptr;
    int ch = 0;

    ShelfTPT preTilt, postTilt;
    DCBlocker dcBlock;
    EnvFollower env, slowEnv;

    double drive = 0.1, makeup = 1.0, memory = 0.0;
};

//==============================================================================
/** Magnetic-core low-band saturator with hysteresis (Foundation stage).      */
class Transformer
{
public:
    void prepare (double sampleRate, AnalogModel* modelIn, int channel)
    {
        sr = sampleRate; model = modelIn; ch = channel;

        const double tol = model != nullptr ? model->tolerance (ch, 5) : 1.0;
        split.prepare (sr);
        split.setCutoff (150.0 * tol);
        coupling.prepare (sr);
        coupling.setCutoff (18.0 * tol);   // output transformer LF roll
        flux = 0.0;
    }

    void setDrive (double d) noexcept { drive = clampd (d, 0.0, 1.5); }

    inline double process (double x) noexcept
    {
        const double lo = split.processLP (x);
        const double hi = x - lo;

        // Core hysteresis: magnetisation lags the applied signal. The state
        // 'flux' relaxes towards the shaped input; the output is drawn from
        // the state, not the instantaneous input → rate-dependent loop.
        const double rail  = model != nullptr ? model->currentSupply() : 1.0;
        const double d     = 1.0 + drive * 3.0;
        const double target = std::tanh (lo * d / rail) * rail;
        flux += (target - flux) * 0.62;                // lag = hysteresis width
        flux = sanitize (flux);

        const double sat = flux / d;
        const double lowOut = lo + (sat - lo) * clampd (drive * 1.2, 0.0, 1.0);

        return coupling.processHP (lowOut) + coupling2State (hi);
    }

    void reset() noexcept { split.reset(); coupling.reset(); flux = 0.0; }

private:
    // the high band bypasses the core but shares the output coupling character
    inline double coupling2State (double hi) noexcept { return hi; }

    double sr = 48000.0;
    AnalogModel* model = nullptr;
    int ch = 0;

    OnePoleTPT split, coupling;
    double drive = 0.0, flux = 0.0;
};

} // namespace md::dsp
