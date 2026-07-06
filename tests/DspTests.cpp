/*  MasterDesk — offline DSP verification.

    Runs the full MasterChain headless and asserts the properties the design
    promises: finite/stable output, ceiling compliance, correct latency
    alignment of the dry path, level-dependent harmonic generation, and a
    noise floor at spec. Exits non-zero on any failure (CTest target).
*/

#include <cstdio>
#include <cmath>
#include <vector>

#include "../src/dsp/MasterChain.h"

using md::dsp::MasterChain;

static int failures = 0;

static void check (bool ok, const char* what)
{
    std::printf ("[%s] %s\n", ok ? "PASS" : "FAIL", what);
    if (! ok)
        ++failures;
}

// single-bin DFT magnitude (relative)
static double goertzel (const std::vector<double>& x, double freq, double sr)
{
    const double w = 2.0 * md::dsp::kPi * freq / sr;
    const double c = 2.0 * std::cos (w);
    double s0 = 0, s1 = 0, s2 = 0;
    for (double v : x)
    {
        s0 = v + c * s1 - s2;
        s2 = s1;
        s1 = s0;
    }
    const double power = s1 * s1 + s2 * s2 - c * s1 * s2;
    return std::sqrt (std::max (power, 0.0)) / ((double) x.size() * 0.5);
}

struct Runner
{
    explicit Runner (const MasterChain::Params& p, double sampleRate = 48000.0)
        : sr (sampleRate)
    {
        chain.prepare (sr, block, 0xC0FFEEull);
        chain.setParams (p);
    }

    // returns interleaved-free stereo output for `seconds` of a sine at freq/ampDb
    void runSine (double freq, double ampDb, double seconds,
                  std::vector<double>& outL, std::vector<double>& outR,
                  std::vector<double>* inL = nullptr)
    {
        const int total = (int) (sr * seconds);
        const double amp = md::dsp::dbToGain (ampDb);
        std::vector<double> l (block), r (block);
        double phase = 0.0;
        const double inc = 2.0 * md::dsp::kPi * freq / sr;

        for (int done = 0; done < total; done += block)
        {
            const int n = std::min (block, total - done);
            for (int i = 0; i < n; ++i)
            {
                const double v = amp * std::sin (phase);
                phase += inc;
                l[(size_t) i] = r[(size_t) i] = v;
                if (inL) inL->push_back (v);
            }
            double* chans[2] = { l.data(), r.data() };
            chain.process (chans, nullptr, nullptr, n);
            for (int i = 0; i < n; ++i)
            {
                outL.push_back (l[(size_t) i]);
                outR.push_back (r[(size_t) i]);
            }
        }
    }

    double sr;
    static constexpr int block = 512;
    MasterChain chain;
};

//==============================================================================
int main()
{
    std::printf ("MasterDesk DSP verification\n===========================\n");

    // ---- 1. stability & ceiling under hot drive --------------------------
    {
        MasterChain::Params p;
        p.volumeDb = 9.0;
        p.foundation = 6.0;
        p.tonePercent = 40.0;
        p.thdDb = -30.0;
        p.stereoPercent = 20.0;
        p.osIndex = 2;              // 4x
        p.truePeak = true;
        p.ceilingDb = -1.0;
        p.analogAmount = 1.0;

        Runner run (p);
        std::vector<double> L, R;
        run.runSine (95.0, -6.0, 4.0, L, R);

        bool finite = true;
        double maxAbs = 0.0;
        double sumSq = 0.0;
        const size_t warm = (size_t) (run.sr * 1.0);
        for (size_t i = 0; i < L.size(); ++i)
        {
            if (! std::isfinite (L[i]) || ! std::isfinite (R[i])) { finite = false; break; }
            if (i >= warm)
            {
                maxAbs = std::max ({ maxAbs, std::abs (L[i]), std::abs (R[i]) });
                sumSq += L[i] * L[i];
            }
        }
        const double rmsDb = 10.0 * std::log10 (sumSq / (double) (L.size() - warm) + 1e-20);

        check (finite, "hot drive: output finite everywhere");
        check (maxAbs <= md::dsp::dbToGain (-1.0) + 0.02,
               "hot drive: sample peaks respect the -1 dBTP ceiling");
        check (rmsDb > -30.0, "hot drive: output carries signal (not muted)");
        check (run.chain.latencySamples() > 0, "hot drive: latency reported (lookahead + OS)");
        std::printf ("       peak %.3f dBFS, rms %.1f dB, latency %d smp\n",
                     md::dsp::gainToDb (maxAbs), rmsDb, run.chain.latencySamples());
    }

    // ---- 2. dry path latency alignment (mix = 0 == delayed input) --------
    {
        MasterChain::Params p;
        p.mixPercent = 0.0;
        p.analogAmount = 0.0;       // bit-exact dry requires the analog floor off
        p.osIndex = 3;              // 8x — exercises a long AA latency
        p.linearPhase = true;

        Runner run (p);
        std::vector<double> L, R, in;
        run.runSine (997.0, -12.0, 1.0, L, R, &in);

        const int lat = run.chain.latencySamples();
        double maxErr = 0.0;
        for (size_t i = (size_t) lat + 4800; i < L.size(); ++i)
            maxErr = std::max (maxErr, std::abs (L[i] - in[i - (size_t) lat]));

        check (maxErr < 1.0e-9, "mix=0: output is the bit-exact latency-aligned dry signal");
        std::printf ("       latency %d smp, max dry error %.3g\n", lat, maxErr);
    }

    // ---- 3. THD control generates level-dependent harmonics --------------
    {
        auto harmonicAt = [] (double thdDb)
        {
            MasterChain::Params p;
            p.thdDb = thdDb;
            p.volumeDb = 0.0;
            p.foundation = 0.0;
            p.tonePercent = 0.0;
            p.stereoPercent = 0.0;
            p.analogAmount = 0.0;
            p.autoGain = false;
            p.osIndex = 2;
            p.ceilingDb = 0.0;

            Runner run (p);
            std::vector<double> L, R;
            run.runSine (1000.0, -8.0, 2.0, L, R);

            // analyse the last second
            std::vector<double> tail (L.end() - (long) run.sr, L.end());
            const double fund = goertzel (tail, 1000.0, run.sr);
            const double h3   = goertzel (tail, 3000.0, run.sr);
            return 20.0 * std::log10 (h3 / std::max (fund, 1e-15) + 1e-15);
        };

        const double clean = harmonicAt (-90.0);
        const double hot   = harmonicAt (-30.0);
        check (hot > clean + 12.0, "THD knob: 3rd harmonic rises with the control");
        check (clean < -70.0,      "THD at minimum: essentially clean (H3 < -70 dBc)");
        std::printf ("       H3 @ THD -90: %.1f dBc, @ THD -30: %.1f dBc\n", clean, hot);
    }

    // ---- 4. noise floor at spec when idle --------------------------------
    {
        MasterChain::Params p;
        p.analogAmount = 1.0;
        p.thdDb = -52.0;
        p.osIndex = 1;

        Runner run (p);
        std::vector<double> L, R;
        run.runSine (1000.0, -200.0, 2.0, L, R);   // effectively silence in

        double sumSq = 0.0;
        const size_t warm = (size_t) run.sr;
        for (size_t i = warm; i < L.size(); ++i)
            sumSq += L[i] * L[i];
        const double noiseDb = 10.0 * std::log10 (sumSq / (double) (L.size() - warm) + 1e-30);

        check (noiseDb < -100.0, "analog floor: modelled noise stays below -100 dBFS");
        check (noiseDb > -160.0, "analog floor: noise is present when the model is on");
        std::printf ("       idle noise floor %.1f dBFS\n", noiseDb);
    }

    // ---- 5. compressor engages progressively with Volume drive -----------
    {
        auto grFor = [] (double volumeDb)
        {
            MasterChain::Params p;
            p.volumeDb = volumeDb;
            p.thdDb = -90.0;
            p.analogAmount = 0.0;
            p.osIndex = 0;
            p.autoGain = false;
            p.ceilingDb = 0.0;

            Runner run (p);
            std::vector<double> L, R;
            run.runSine (220.0, -8.0, 3.0, L, R);
            return run.chain.meters().compGrDb;
        };

        const double gentle = grFor (-8.0);   // sits under the threshold
        const double driven = grFor (10.0);   // slams into it
        check (gentle < 1.0,          "compressor: quiet drive stays essentially untouched");
        check (driven > gentle + 2.0, "compressor: hot drive produces gain reduction");
        std::printf ("       GR @ -8 dB drive: %.2f dB, @ +10 dB drive: %.2f dB\n",
                     gentle, driven);
    }

    std::printf ("===========================\n%s (%d failure%s)\n",
                 failures == 0 ? "ALL TESTS PASSED" : "TESTS FAILED",
                 failures, failures == 1 ? "" : "s");
    return failures == 0 ? 0 : 1;
}
