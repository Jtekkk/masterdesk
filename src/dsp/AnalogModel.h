#pragma once

/*  MasterDesk — analog behaviour model.

    This is the "circuit floor" shared by the whole chain. It supplies:

      • Randomised component tolerances  — every plugin instance (and every
        channel inside it) gets its own set of small, fixed component
        deviations, like two channels of a real console never matching
        exactly. Deterministic per instance seed.
      • Micro-component drift            — very slow bounded random walks that
        nudge "component values" around their tolerance point, as real parts
        drift with age and temperature.
      • Temperature model                — a virtual operating temperature
        that scales drift depth, bias point and the noise floor.
      • Power-supply sag                 — the virtual supply rail droops
        under sustained programme energy and recovers with a time constant.
        Saturation headroom follows the rail.
      • Hardware-style noise floor       — filtered (pink-ish) noise per
        channel, decorrelated L/R, around -114 dBFS at nominal temperature.
      • Inter-channel crosstalk          — HF-weighted bleed between L and R,
        as found in shared-PSU / shared-ground analog circuitry.

    All of it is scaled by one "analog amount" control and can be fully
    disabled for bit-transparent operation.
*/

#include "Common.h"

namespace md::dsp
{

class AnalogModel
{
public:
    static constexpr int maxToleranceSlots = 16;

    void prepare (double sampleRate, uint64_t instanceSeed)
    {
        sr = sampleRate;

        // Fixed per-instance component tolerances (±0.8 % std-dev, clamped).
        Rng seeded (instanceSeed);
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < maxToleranceSlots; ++i)
                tolerances[ch][i] = clampd (1.0 + 0.008 * seeded.gauss(), 0.975, 1.025);

        for (int ch = 0; ch < 2; ++ch)
        {
            noiseRng[ch]  = Rng (seeded.nextU64());
            driftRng[ch]  = Rng (seeded.nextU64());
            noiseLP[ch].prepare (sr);
            noiseLP[ch].setCutoff (4500.0);   // tame the top — transistor hiss, not white noise
            noiseHP[ch].prepare (sr);
            noiseHP[ch].setCutoff (30.0);
            crossHP[ch].prepare (sr);
            crossHP[ch].setCutoff (2000.0);   // crosstalk rises with frequency
            drift[ch] = 0.0;
        }

        supply     = 1.0;
        supplyEnv  = 0.0;
        // ~40 ms droop, ~400 ms recovery
        sagAttack  = std::exp (-1.0 / (0.040 * sr));
        sagRelease = std::exp (-1.0 / (0.400 * sr));
    }

    /** amount: 0..2 (1 = nominal). temperature: 0..1 mapped to 15..45 °C. */
    void setControls (double amountIn, double temperatureIn) noexcept
    {
        amount      = clampd (amountIn, 0.0, 2.0);
        temperature = clampd (temperatureIn, 0.0, 1.0);

        // Noise rises ~2 dB per +10 °C; nominal floor -114 dBFS.
        const double tempC   = 15.0 + temperature * 30.0;
        const double noiseDb = -114.0 + (tempC - 25.0) * 0.2;
        noiseGain = dbToGain (noiseDb) * amount;

        crosstalkGain = dbToGain (-66.0) * amount;
        driftDepth    = (0.5 + temperature) * amount;   // hotter → more drift
    }

    bool isActive() const noexcept { return amount > 1.0e-6; }

    /** Fixed component tolerance for a channel/slot — multiply nominal
        component values (filter frequencies, gains, thresholds) by this.     */
    double tolerance (int ch, int slot) const noexcept
    {
        return tolerances[ch & 1][slot & (maxToleranceSlots - 1)];
    }

    /** Tolerance combined with the live drift walk for that channel.         */
    double driftedTolerance (int ch, int slot) const noexcept
    {
        return tolerance (ch, slot) * (1.0 + drift[ch & 1] * 0.002 * driftDepth);
    }

    /** Advance the slow drift random walks — call once per block.            */
    void updateDrift() noexcept
    {
        for (int ch = 0; ch < 2; ++ch)
        {
            drift[ch] += driftRng[ch].bipolar() * 0.02;
            drift[ch] *= 0.999;                       // bounded walk
            drift[ch]  = clampd (drift[ch], -1.0, 1.0);
        }
    }

    /** Feed the PSU model with the block's mean-square programme level and
        get the current rail state (0.85 .. 1.0). Saturation stages divide
        their headroom by this.                                               */
    double updateSupply (double blockMeanSquare, int numSamples) noexcept
    {
        // block-rate one-pole using per-sample coefficients raised to n
        const double aAtt = std::pow (sagAttack,  (double) numSamples);
        const double aRel = std::pow (sagRelease, (double) numSamples);
        const double x = clampd (blockMeanSquare * 4.0, 0.0, 1.0);
        const double a = x > supplyEnv ? aAtt : aRel;
        supplyEnv = supplyEnv * a + x * (1.0 - a);
        supply = 1.0 - supplyEnv * 0.06 * amount;     // up to ~0.5 dB of sag
        return supply;
    }

    double currentSupply() const noexcept { return supply; }

    /** Hardware-style noise sample for a channel (already gain-scaled).      */
    inline double noise (int ch) noexcept
    {
        auto& r = noiseRng[ch & 1];
        double n = r.gauss();
        n = noiseLP[ch & 1].processLP (n) * 1.8;      // pink-ish tilt
        n = noiseHP[ch & 1].processHP (n);
        return n * noiseGain;
    }

    /** Apply HF-weighted L↔R crosstalk in place.                             */
    inline void applyCrosstalk (double& l, double& r) noexcept
    {
        if (crosstalkGain <= 0.0) return;
        const double bleedL = crossHP[0].processHP (l);
        const double bleedR = crossHP[1].processHP (r);
        l += bleedR * crosstalkGain;
        r += bleedL * crosstalkGain;
    }

    /** Bias offset for saturators — moves with temperature and drift, giving
        slowly-varying even-harmonic content ("tube bias interaction").       */
    double biasOffset (int ch) const noexcept
    {
        const double tempBias = (temperature - 0.35) * 0.02;
        return (tempBias + drift[ch & 1] * 0.004) * amount;
    }

private:
    double sr = 48000.0;
    double amount = 1.0, temperature = 0.5;
    double tolerances[2][maxToleranceSlots] {};
    double drift[2] {};
    double driftDepth = 1.0;

    Rng noiseRng[2], driftRng[2];
    OnePoleTPT noiseLP[2], noiseHP[2], crossHP[2];
    double noiseGain = 0.0, crosstalkGain = 0.0;

    double supply = 1.0, supplyEnv = 0.0, sagAttack = 0.0, sagRelease = 0.0;
};

} // namespace md::dsp
