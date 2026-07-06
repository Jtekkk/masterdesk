#pragma once

/*  MasterDesk — common DSP building blocks.

    Everything here runs in double precision (64-bit) and is allocation-free
    on the audio thread once prepare() has been called.
*/

#include <cmath>
#include <vector>
#include <algorithm>
#include <cstdint>

namespace md::dsp
{

constexpr double kSilenceDb = -120.0;
constexpr double kPi        = 3.141592653589793238462643383279502884;

inline double dbToGain (double db) noexcept   { return std::pow (10.0, db * 0.05); }
inline double gainToDb (double g)  noexcept   { return 20.0 * std::log10 (std::max (g, 1.0e-12)); }
inline double clampd   (double v, double lo, double hi) noexcept { return v < lo ? lo : (v > hi ? hi : v); }

/** Flush denormals / NaNs coming out of feedback structures. */
inline double sanitize (double v) noexcept
{
    if (! std::isfinite (v) || std::abs (v) < 1.0e-30)
        return 0.0;
    return v;
}

//==============================================================================
/** Zero-delay-feedback (topology-preserving transform) one-pole.
    process() returns the low-pass output; highpass = input - lowpass.        */
struct OnePoleTPT
{
    void prepare (double sr) noexcept          { sampleRate = sr; setCutoff (cutoffHz); reset(); }
    void reset() noexcept                      { s = 0.0; }

    void setCutoff (double hz) noexcept
    {
        cutoffHz = clampd (hz, 1.0, sampleRate * 0.49);
        const double g = std::tan (kPi * cutoffHz / sampleRate);
        G = g / (1.0 + g);
    }

    inline double processLP (double x) noexcept
    {
        const double v = G * (x - s);
        const double y = v + s;
        s = sanitize (y + v);
        return y;
    }

    inline double processHP (double x) noexcept { return x - processLP (x); }

    double sampleRate = 48000.0, cutoffHz = 1000.0, G = 0.0, s = 0.0;
};

//==============================================================================
/** First-order ZDF shelving filter (low or high). Gain is set as linear.     */
struct ShelfTPT
{
    enum class Type { lowShelf, highShelf };

    void prepare (double sr) noexcept   { pole.prepare (sr); }
    void reset() noexcept               { pole.reset(); }
    void setup (Type t, double hz, double gainDb) noexcept
    {
        type = t;
        pole.setCutoff (hz);
        gainLin = dbToGain (gainDb);
    }

    inline double process (double x) noexcept
    {
        const double lp = pole.processLP (x);
        const double band = (type == Type::lowShelf ? lp : x - lp);
        return x + (gainLin - 1.0) * band;
    }

    OnePoleTPT pole;
    Type   type    = Type::lowShelf;
    double gainLin = 1.0;
};

//==============================================================================
/** ZDF state-variable filter (Zavalishin TPT form). Provides LP/BP/HP and a
    constant-skirt peaking mode used by the Foundation resonance.             */
struct SVFTPT
{
    void prepare (double sr) noexcept  { sampleRate = sr; update(); reset(); }
    void reset() noexcept              { ic1 = ic2 = 0.0; }

    void setCutoff (double hz, double q) noexcept
    {
        cutoffHz = clampd (hz, 5.0, sampleRate * 0.45);
        Q = clampd (q, 0.05, 40.0);
        update();
    }

    inline void processAll (double x, double& lp, double& bp, double& hp) noexcept
    {
        hp = (x - g1 * ic1 - ic2) * d;
        bp = g * hp + ic1;
        lp = g * bp + ic2;
        ic1 = sanitize (g * hp + bp);
        ic2 = sanitize (g * bp + lp);
    }

    inline double processLP (double x) noexcept { double lp, bp, hp; processAll (x, lp, bp, hp); return lp; }
    inline double processBP (double x) noexcept { double lp, bp, hp; processAll (x, lp, bp, hp); return bp; }
    inline double processHP (double x) noexcept { double lp, bp, hp; processAll (x, lp, bp, hp); return hp; }

    /** Peaking bell: unity + gain * band-pass (constant-Q character). */
    inline double processBell (double x, double bellGainLin) noexcept
    {
        double lp, bp, hp; processAll (x, lp, bp, hp);
        return x + (bellGainLin - 1.0) * (bp / Q);
    }

private:
    void update() noexcept
    {
        g  = std::tan (kPi * cutoffHz / sampleRate);
        k  = 1.0 / Q;
        g1 = g + k;
        d  = 1.0 / (1.0 + g * g1);
    }

    double sampleRate = 48000.0, cutoffHz = 1000.0, Q = 0.7071;
    double g = 0, k = 1.4142, g1 = 0, d = 1;
    double ic1 = 0, ic2 = 0;
};

//==============================================================================
/** DC blocker whose pole can be modulated — models coupling-capacitor
    charge/discharge (cutoff creeps up slightly as the cap charges under
    sustained asymmetric signal).                                             */
struct DCBlocker
{
    void prepare (double sr) noexcept
    {
        sampleRate = sr;
        setCutoff (baseHz);
        x1 = y1 = charge = 0.0;
    }

    void setCutoff (double hz) noexcept
    {
        baseHz = hz;
        R = 1.0 - (2.0 * kPi * hz / sampleRate);
        R = clampd (R, 0.9, 0.999999);
    }

    inline double process (double x) noexcept
    {
        // capacitor "charge" follows the rectified DC offset estimate
        charge = sanitize (charge * 0.9999 + x * 0.0001);
        const double rMod = clampd (R - std::abs (charge) * 0.002, 0.9, 0.999999);
        const double y = x - x1 + rMod * y1;
        x1 = x;
        y1 = sanitize (y);
        return y;
    }

    double sampleRate = 48000.0, baseHz = 6.0, R = 0.9995;
    double x1 = 0, y1 = 0, charge = 0;
};

//==============================================================================
/** Peak/RMS envelope follower with independent attack and release.          */
struct EnvFollower
{
    void prepare (double sr) noexcept  { sampleRate = sr; setTimes (attackMs, releaseMs); env = 0.0; }

    void setTimes (double attMs, double relMs) noexcept
    {
        attackMs = attMs; releaseMs = relMs;
        aAtt = coefFor (attMs);
        aRel = coefFor (relMs);
    }

    inline double process (double rectified) noexcept
    {
        const double a = rectified > env ? aAtt : aRel;
        env = sanitize (env + (rectified - env) * (1.0 - a));
        return env;
    }

    double coefFor (double ms) const noexcept
    {
        if (ms <= 0.0) return 0.0;
        return std::exp (-1.0 / (0.001 * ms * sampleRate));
    }

    double sampleRate = 48000.0, attackMs = 10.0, releaseMs = 100.0;
    double aAtt = 0, aRel = 0, env = 0;
};

//==============================================================================
/** Linear parameter smoother — zipper-noise-free, sample-accurate ramps.     */
struct Smoother
{
    void prepare (double sr, double rampMs) noexcept
    {
        stepsPerRamp = std::max (1, (int) (sr * rampMs * 0.001));
        current = target;
        stepsLeft = 0;
    }

    void setTarget (double t) noexcept
    {
        if (std::abs (t - target) < 1.0e-12) return;
        target = t;
        step = (target - current) / (double) stepsPerRamp;
        stepsLeft = stepsPerRamp;
    }

    void snap (double t) noexcept { target = current = t; stepsLeft = 0; }

    inline double next() noexcept
    {
        if (stepsLeft > 0)
        {
            --stepsLeft;
            current += step;
            if (stepsLeft == 0) current = target;
        }
        return current;
    }

    bool isSmoothing() const noexcept { return stepsLeft > 0; }

    double current = 0, target = 0, step = 0;
    int stepsPerRamp = 64, stepsLeft = 0;
};

//==============================================================================
/** Fixed-capacity sliding-window maximum (monotonic wedge). O(1) amortised,
    zero allocation after prepare(). Used by the limiter's lookahead.         */
struct SlidingMax
{
    void prepare (int windowLen) noexcept
    {
        window = std::max (1, windowLen);
        values.assign ((size_t) window + 1, 0.0);
        indices.assign ((size_t) window + 1, 0);
        head = tail = 0;
        n = 0;
    }

    inline double push (double v) noexcept
    {
        // drop expired front element
        if (head != tail && indices[(size_t) head] <= n - window)
            head = (head + 1) % (int) values.size();

        // pop smaller elements from the back
        while (head != tail)
        {
            const int back = (tail + (int) values.size() - 1) % (int) values.size();
            if (values[(size_t) back] <= v)
                tail = back;
            else
                break;
        }

        values[(size_t) tail]  = v;
        indices[(size_t) tail] = n;
        tail = (tail + 1) % (int) values.size();
        ++n;
        return values[(size_t) head];
    }

    std::vector<double> values;
    std::vector<int64_t> indices;
    int window = 1, head = 0, tail = 0;
    int64_t n = 0;
};

//==============================================================================
/** Simple preallocated integer-delay line (per channel).                     */
struct Delay
{
    void prepare (int maxSamples) noexcept
    {
        buf.assign ((size_t) std::max (1, maxSamples), 0.0);
        writePos = 0;
        delay = std::min (delay, (int) buf.size() - 1);
    }

    void setDelay (int samples) noexcept   { delay = clampi (samples, 0, (int) buf.size() - 1); }
    void reset() noexcept                  { std::fill (buf.begin(), buf.end(), 0.0); }

    inline double process (double x) noexcept
    {
        buf[(size_t) writePos] = x;
        int readPos = writePos - delay;
        if (readPos < 0) readPos += (int) buf.size();
        writePos = (writePos + 1) % (int) buf.size();
        return buf[(size_t) readPos];
    }

    static int clampi (int v, int lo, int hi) noexcept { return v < lo ? lo : (v > hi ? hi : v); }

    std::vector<double> buf;
    int writePos = 0, delay = 0;
};

//==============================================================================
/** Deterministic, fast xorshift RNG — used for analog tolerances, drift and
    the modelled noise floor. Never calls into the system RNG on the audio
    thread.                                                                   */
struct Rng
{
    explicit Rng (uint64_t seed = 0x9E3779B97F4A7C15ull) : state (seed | 1ull) {}

    inline uint64_t nextU64() noexcept
    {
        uint64_t x = state;
        x ^= x << 13; x ^= x >> 7; x ^= x << 17;
        state = x;
        return x;
    }

    /** Uniform in [-1, 1). */
    inline double bipolar() noexcept
    {
        return ((double) (nextU64() >> 11) / 4503599627370496.0) * 2.0 - 1.0;
    }

    /** Approximately normal (sum of uniforms), mean 0, std ~1. */
    inline double gauss() noexcept
    {
        return (bipolar() + bipolar() + bipolar()) * 0.7071;
    }

    uint64_t state;
};

} // namespace md::dsp
