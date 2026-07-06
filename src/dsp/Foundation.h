#pragma once

/*  MasterDesk — Foundation stage (the big right-hand knob).

    Low-end architecture in one control:

      • A ZDF low shelf around 80 Hz plus a gentle resonant bell at ~55 Hz
        build the weight.
      • The boost is *dynamic*: an envelope follower on the low band backs the
        shelf off (up to ~4 dB) when the programme already carries heavy bass,
        so the bottom stays solid instead of bloated — processing that adapts
        to the source material.
      • The boosted band then passes through the hysteretic Transformer model
        for harmonic thickening that rises with the Foundation amount.

    Component values take per-channel analog tolerances, so L/R (or M/S)
    never match with digital perfection when the analog model is active.
*/

#include "Common.h"
#include "AnalogModel.h"
#include "Saturation.h"

namespace md::dsp
{

class Foundation
{
public:
    void prepare (double sampleRate, AnalogModel* modelIn, int channel)
    {
        sr = sampleRate; model = modelIn; ch = channel;

        shelf.prepare (sr);
        bell.prepare (sr);
        lowSense.prepare (sr);
        lowSense.setCutoff (120.0);
        lowEnv.prepare (sr);
        lowEnv.setTimes (30.0, 350.0);
        transformer.prepare (sr, modelIn, channel);
        amountSm.prepare (sr, 40.0);
        update();
    }

    /** amount: 0..10 (front-panel value). */
    void setAmount (double amount) noexcept
    {
        amountSm.setTarget (clampd (amount, 0.0, 10.0) * 0.1);   // → 0..1
    }

    inline double process (double x) noexcept
    {
        const double a = amountSm.next();

        if (controlCountdown-- <= 0)
        {
            controlCountdown = 32;
            update (a);
        }

        // dynamic boost: measure the incoming low band, duck the shelf gain
        const double lowIn = lowSense.processLP (x);
        const double e     = lowEnv.process (std::abs (lowIn));
        const double duck  = clampd ((gainToDb (e + 1.0e-9) + 21.0) * 0.12, 0.0, 1.0);
        const double dynGain = shelfDb * (1.0 - 0.55 * duck);

        shelf.gainLin = dbToGain (dynGain);

        double y = shelf.process (x);
        y = bell.processBell (y, dbToGain (bellDb * (1.0 - 0.4 * duck)));

        transformer.setDrive (a * 0.8);
        return transformer.process (y);
    }

    void reset() noexcept
    {
        shelf.reset(); bell.reset(); lowSense.reset();
        transformer.reset();
    }

private:
    void update (double a = -1.0) noexcept
    {
        if (a < 0.0) a = amountSm.current;

        const double tolF = model != nullptr ? model->driftedTolerance (ch, 0) : 1.0;
        const double tolG = model != nullptr ? model->driftedTolerance (ch, 1) : 1.0;

        shelfDb = a * 6.5 * tolG;                       // up to +6.5 dB
        bellDb  = a * 2.2 * tolG;

        shelf.setup (ShelfTPT::Type::lowShelf, 82.0 * tolF, shelfDb);
        bell.setCutoff (55.0 * tolF, 1.1);
    }

    double sr = 48000.0;
    AnalogModel* model = nullptr;
    int ch = 0;

    ShelfTPT shelf;
    SVFTPT bell;
    OnePoleTPT lowSense;
    EnvFollower lowEnv;
    Transformer transformer;
    Smoother amountSm;

    double shelfDb = 0.0, bellDb = 0.0;
    int controlCountdown = 0;
};

} // namespace md::dsp
