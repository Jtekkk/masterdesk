#pragma once

/*  MasterDesk — Tone stage (the small "3. Tone" knob + A/B/C/D voicings).

    Four fixed EQ "voicings" built from ZDF shelves/bells; the Tone knob
    morphs continuously from flat (0 %) to the full voicing (100 %), so a
    single gesture reshapes the master without hunting for frequencies.

      A — Modern:   tight low cut sub-20, +air shelf 11 kHz, hint of 3 kHz presence
      B — Presence: forward mids, +2.5 dB @ 3 kHz bell, slight 200 Hz dip
      C — Air:      wide +3.5 dB shelf @ 10 kHz only — pure sheen
      D — Warm:     +1.5 dB low shelf 150 Hz, -2 dB high shelf 8 kHz — tape-ish tilt

    All corner frequencies and gains take analog tolerances per channel.
*/

#include "Common.h"
#include "AnalogModel.h"

namespace md::dsp
{

class ToneStack
{
public:
    enum class Voicing { A = 0, B, C, D };

    void prepare (double sampleRate, AnalogModel* modelIn, int channel)
    {
        sr = sampleRate; model = modelIn; ch = channel;
        low.prepare (sr);
        high.prepare (sr);
        bell.prepare (sr);
        subCut.prepare (sr);
        subCut.setCutoff (18.0);
        amountSm.prepare (sr, 40.0);
        update();
    }

    void setControls (Voicing v, double amountPercent) noexcept
    {
        voicing = v;
        amountSm.setTarget (clampd (amountPercent, 0.0, 100.0) * 0.01);
    }

    inline double process (double x) noexcept
    {
        const double a = amountSm.next();

        if (controlCountdown-- <= 0)
        {
            controlCountdown = 32;
            update (a);
        }

        double y = x;
        if (useSubCut)
            y = subCut.processHP (y);

        y = low.process (y);
        y = bell.processBell (y, bellGainLin);
        y = high.process (y);
        return y;
    }

    void reset() noexcept { low.reset(); high.reset(); bell.reset(); subCut.reset(); }

private:
    void update (double a = -1.0) noexcept
    {
        if (a < 0.0) a = amountSm.current;

        const double tf = model != nullptr ? model->driftedTolerance (ch, 8) : 1.0;
        const double tg = model != nullptr ? model->driftedTolerance (ch, 9) : 1.0;

        double lowHz = 150.0,  lowDb = 0.0;
        double highHz = 9000.0, highDb = 0.0;
        double bellHz = 3000.0, bellDb = 0.0, bellQ = 0.8;
        useSubCut = false;

        switch (voicing)
        {
            case Voicing::A:  // Modern
                useSubCut = true;
                highHz = 11000.0; highDb =  2.8;
                bellHz = 3200.0;  bellDb =  0.8;
                break;
            case Voicing::B:  // Presence
                bellHz = 3000.0;  bellDb =  2.5; bellQ = 0.9;
                lowHz  = 200.0;   lowDb  = -1.2;
                break;
            case Voicing::C:  // Air
                highHz = 10000.0; highDb =  3.5;
                break;
            case Voicing::D:  // Warm
                lowHz  = 150.0;   lowDb  =  1.5;
                highHz = 8000.0;  highDb = -2.0;
                break;
        }

        low.setup  (ShelfTPT::Type::lowShelf,  lowHz  * tf, lowDb  * a * tg);
        high.setup (ShelfTPT::Type::highShelf, highHz * tf, highDb * a * tg);
        bell.setCutoff (bellHz * tf, bellQ);
        bellGainLin = dbToGain (bellDb * a * tg);
    }

    double sr = 48000.0;
    AnalogModel* model = nullptr;
    int ch = 0;

    ShelfTPT low, high;
    SVFTPT bell;
    OnePoleTPT subCut;
    Smoother amountSm;

    Voicing voicing = Voicing::A;
    double bellGainLin = 1.0;
    bool useSubCut = false;
    int controlCountdown = 0;
};

} // namespace md::dsp
