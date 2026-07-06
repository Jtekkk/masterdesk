#include "MeterPanel.h"
#include "MasterDeskLookAndFeel.h"

namespace md::ui
{

static juce::Font mpFont (float height, bool bold = false)
{
    return juce::Font (juce::FontOptions (height, bold ? juce::Font::bold : juce::Font::plain));
}

// the printed scale, left to right
static constexpr float scaleMarks[] = { 12.0f, 10.0f, 8.0f, 7.0f, 6.0f, 5.0f, 4.0f, 3.0f };
static constexpr int   numMarks     = 8;
static constexpr float arcHalfAngle = 0.76f;   // radians either side of vertical

MeterPanel::MeterPanel()
{
    window.resize (fftSize);
    for (int i = 0; i < fftSize; ++i)
        window[(size_t) i] = 0.5f * (1.0f - std::cos (2.0f * juce::MathConstants<float>::pi
                                                      * (float) i / (float) (fftSize - 1)));
    fifo.resize (fftSize, 0.0f);
    fftData.resize ((size_t) fftSize * 2, 0.0f);
    displayBins.resize (256, -120.0f);
    setOpaque (false);
}

//==============================================================================
void MeterPanel::update (float dr, float peak, float lufsShortIn, float lufsIntIn,
                         float compGrDb, float limGrDb, float truePeakDb)
{
    drTarget = dr;
    // needle ballistics: ~300 ms rise, ~500 ms fall at 30 fps
    const float coeff = drTarget > drSmoothed ? 0.18f : 0.10f;
    drSmoothed += (drTarget - drSmoothed) * coeff;

    peakDb   = peak;
    lufsS    = lufsShortIn;
    lufsI    = lufsIntIn;
    compGr   = compGrDb;
    limGr    = limGrDb;
    truePeak = truePeakDb;
    repaint();
}

void MeterPanel::setSpectrumMode (bool s)
{
    if (spectrumMode == s)
        return;
    spectrumMode = s;
    if (onSpectrumModeChange)
        onSpectrumModeChange (s);
    repaint();
}

void MeterPanel::mouseUp (const juce::MouseEvent& e)
{
    if (e.mouseWasClicked())
        setSpectrumMode (! spectrumMode);
}

//==============================================================================
void MeterPanel::pushSamples (const float* data, int num)
{
    for (int i = 0; i < num; ++i)
    {
        if (fifoFill >= fftSize)
        {
            runFFT();
            fifoFill = 0;
        }
        fifo[(size_t) fifoFill++] = data[i];
    }
}

void MeterPanel::runFFT()
{
    if (! spectrumMode)
        return;

    for (int i = 0; i < fftSize; ++i)
        fftData[(size_t) i] = fifo[(size_t) i] * window[(size_t) i];
    std::fill (fftData.begin() + fftSize, fftData.end(), 0.0f);

    fft.performFrequencyOnlyForwardTransform (fftData.data());

    // collapse onto log-spaced display bins with peak-hold decay
    const int numBins = (int) displayBins.size();
    const float minF = 20.0f, maxF = 20000.0f;
    for (int b = 0; b < numBins; ++b)
    {
        const float f0 = minF * std::pow (maxF / minF, (float) b / (float) numBins);
        const float f1 = minF * std::pow (maxF / minF, (float) (b + 1) / (float) numBins);
        // assume 48 kHz-ish mapping; the shape is what matters for the display
        const int i0 = juce::jlimit (1, fftSize / 2 - 1, (int) (f0 / 24000.0f * (float) (fftSize / 2)));
        const int i1 = juce::jlimit (i0, fftSize / 2 - 1, (int) (f1 / 24000.0f * (float) (fftSize / 2)));

        float mag = 0.0f;
        for (int i = i0; i <= i1; ++i)
            mag = juce::jmax (mag, fftData[(size_t) i]);

        const float db = juce::Decibels::gainToDecibels (mag / (float) fftSize * 4.0f, -120.0f);
        auto& d = displayBins[(size_t) b];
        d = db > d ? db : d - 1.5f;   // fast attack, smooth decay
    }
}

//==============================================================================
float MeterPanel::angleForValue (float v) const
{
    // piecewise-linear between the printed marks (equal angular spacing)
    v = juce::jlimit (scaleMarks[numMarks - 1], scaleMarks[0], v);
    int seg = 0;
    while (seg < numMarks - 2 && v < scaleMarks[seg + 1])
        ++seg;
    const float t = (scaleMarks[seg] - v) / (scaleMarks[seg] - scaleMarks[seg + 1]);
    const float idx = (float) seg + t;
    return -arcHalfAngle + (idx / (float) (numMarks - 1)) * 2.0f * arcHalfAngle;
}

//==============================================================================
void MeterPanel::paint (juce::Graphics& g)
{
    auto full = getLocalBounds().toFloat();

    // housing
    g.setColour (juce::Colour (0xff060606));
    g.fillRoundedRectangle (full, 6.0f);
    g.setColour (juce::Colour (0xff2e2e2e));
    g.drawRoundedRectangle (full.reduced (0.5f), 6.0f, 1.0f);

    auto inner = full.reduced (8.0f);

    // layout: LED strip right, readout row bottom, face on the rest
    auto ledStrip = inner.removeFromRight (inner.getWidth() * 0.055f);
    auto row      = inner.removeFromBottom (juce::jmax (18.0f, inner.getHeight() * 0.16f));
    inner.removeFromBottom (3.0f);
    auto face     = inner;

    if (spectrumMode)
    {
        drawSpectrum (g, face);
    }
    else
    {
        // cream face
        juce::ColourGradient grad (Palette::meterFace.brighter (0.03f),
                                   face.getX(), face.getY(),
                                   Palette::meterFace.darker (0.06f),
                                   face.getX(), face.getBottom(), false);
        g.setGradientFill (grad);
        g.fillRect (face);
        g.setColour (juce::Colours::black.withAlpha (0.65f));
        g.drawRect (face, 1.2f);

        drawScale (g, face);
        drawNeedle (g, face);
    }

    drawLedLadder (g, ledStrip.reduced (2.0f));
    drawReadoutRow (g, row);
}

void MeterPanel::drawScale (juce::Graphics& g, juce::Rectangle<float> face)
{
    juce::Graphics::ScopedSaveState save (g);
    g.reduceClipRegion (face.toNearestInt());

    const juce::Point<float> pivot (face.getCentreX(), face.getBottom() + face.getHeight() * 0.65f);
    const float rOut = face.getHeight() * 1.22f;
    const float rTick = rOut - face.getHeight() * 0.10f;

    // title
    g.setColour (juce::Colour (0xff141414));
    g.setFont (mpFont (juce::jmax (11.0f, face.getHeight() * 0.13f), true));
    g.drawText ("Dynamic Range",
                juce::Rectangle<float> (face.getX(), face.getY() + face.getHeight() * 0.05f,
                                        face.getWidth(), face.getHeight() * 0.16f),
                juce::Justification::centred, false);

    auto pointAt = [&] (float angle, float radius)
    {
        return juce::Point<float> (pivot.x + radius * std::sin (angle),
                                   pivot.y - radius * std::cos (angle));
    };

    // main arc
    {
        juce::Path arc;
        arc.addCentredArc (pivot.x, pivot.y, rTick, rTick, 0.0f,
                           -arcHalfAngle, arcHalfAngle, true);
        g.setColour (juce::Colour (0xff181818));
        g.strokePath (arc, juce::PathStrokeType (1.6f));
    }

    // green zone (between the 8 and 6 marks) and red zone (4.5 → past 3)
    auto zone = [&] (float vFrom, float vTo, juce::Colour c, float thickness)
    {
        juce::Path p;
        p.addCentredArc (pivot.x, pivot.y, rTick + thickness * 0.9f, rTick + thickness * 0.9f, 0.0f,
                         angleForValue (vFrom), angleForValue (vTo), true);
        g.setColour (c);
        g.strokePath (p, juce::PathStrokeType (thickness));
    };
    const float zoneTh = juce::jmax (2.5f, face.getHeight() * 0.035f);
    zone (7.6f, 5.9f, Palette::meterGreen, zoneTh);
    zone (4.5f, 3.0f, Palette::meterRed,   zoneTh);

    // ticks + numbers
    g.setFont (mpFont (juce::jmax (10.0f, face.getHeight() * 0.115f), true));
    for (int i = 0; i < numMarks; ++i)
    {
        const float v = scaleMarks[i];
        const float a = angleForValue (v);
        const auto p0 = pointAt (a, rTick - face.getHeight() * 0.05f);
        const auto p1 = pointAt (a, rTick + face.getHeight() * 0.045f);
        g.setColour (juce::Colour (0xff181818));
        g.drawLine ({ p0, p1 }, 1.6f);

        const bool green = v <= 7.0f && v >= 6.0f;
        g.setColour (green ? Palette::meterGreen : juce::Colour (0xff181818));
        const auto tp = pointAt (a, rTick + face.getHeight() * 0.17f);
        const float fs = face.getHeight() * 0.16f;
        g.drawText (juce::String ((int) v),
                    juce::Rectangle<float> (tp.x - fs, tp.y - fs * 0.6f, fs * 2.0f, fs * 1.2f),
                    juce::Justification::centred, false);
    }

    // minor ticks between marks
    g.setColour (juce::Colour (0x99181818));
    for (int i = 0; i < numMarks - 1; ++i)
    {
        const float a = (angleForValue (scaleMarks[i]) + angleForValue (scaleMarks[i + 1])) * 0.5f;
        g.drawLine ({ pointAt (a, rTick - face.getHeight() * 0.02f),
                      pointAt (a, rTick + face.getHeight() * 0.02f) }, 1.0f);
    }
}

void MeterPanel::drawNeedle (juce::Graphics& g, juce::Rectangle<float> face)
{
    juce::Graphics::ScopedSaveState save (g);
    g.reduceClipRegion (face.toNearestInt());

    const juce::Point<float> pivot (face.getCentreX(), face.getBottom() + face.getHeight() * 0.65f);
    const float rNeedle = face.getHeight() * 1.32f;

    // silence parks the needle right of the scale, like the reference
    const float value = drSmoothed < 0.05f ? 2.55f : juce::jlimit (2.55f, 12.0f, drSmoothed);
    const float a = drSmoothed < 0.05f ? arcHalfAngle * 1.08f : angleForValue (value);

    const juce::Point<float> tip (pivot.x + rNeedle * std::sin (a),
                                  pivot.y - rNeedle * std::cos (a));

    g.setColour (juce::Colours::black.withAlpha (0.85f));
    g.drawLine ({ pivot, tip }, juce::jmax (1.8f, face.getHeight() * 0.02f));
}

//==============================================================================
void MeterPanel::drawSpectrum (juce::Graphics& g, juce::Rectangle<float> face)
{
    g.setColour (juce::Colour (0xff050505));
    g.fillRect (face);
    g.setColour (juce::Colour (0xff262626));
    g.drawRect (face, 1.0f);

    // grid
    g.setColour (juce::Colours::white.withAlpha (0.05f));
    for (float f : { 100.0f, 1000.0f, 10000.0f })
    {
        const float t = std::log (f / 20.0f) / std::log (1000.0f);
        const float x = face.getX() + t * face.getWidth();
        g.drawVerticalLine ((int) x, face.getY(), face.getBottom());
    }
    for (float db = -20.0f; db > -90.0f; db -= 20.0f)
    {
        const float y = face.getY() + (-db / 100.0f) * face.getHeight();
        g.drawHorizontalLine ((int) y, face.getX(), face.getRight());
    }

    // curve
    juce::Path p;
    const int numBins = (int) displayBins.size();
    bool started = false;
    for (int b = 0; b < numBins; ++b)
    {
        const float x = face.getX() + ((float) b / (float) (numBins - 1)) * face.getWidth();
        const float db = juce::jlimit (-100.0f, 0.0f, displayBins[(size_t) b]);
        const float y = face.getY() + (-db / 100.0f) * face.getHeight();
        if (! started) { p.startNewSubPath (x, y); started = true; }
        else             p.lineTo (x, y);
    }

    juce::Path fill (p);
    fill.lineTo (face.getRight(), face.getBottom());
    fill.lineTo (face.getX(), face.getBottom());
    fill.closeSubPath();

    g.setColour (Palette::meterGreen.withAlpha (0.18f));
    g.fillPath (fill);
    g.setColour (Palette::meterGreen.brighter (0.25f));
    g.strokePath (p, juce::PathStrokeType (1.4f));

    g.setColour (Palette::textDim);
    g.setFont (mpFont (10.0f));
    g.drawText ("SPECTRUM  20 Hz - 20 kHz  ·  click to return",
                face.reduced (6.0f, 4.0f), juce::Justification::topRight, false);
}

//==============================================================================
void MeterPanel::drawLedLadder (juce::Graphics& g, juce::Rectangle<float> strip)
{
    const int numLeds = 14;
    const float gap = 2.0f;
    const float ledH = (strip.getHeight() - gap * (float) (numLeds - 1)) / (float) numLeds;

    // -60 .. 0 dB mapped bottom → top
    const float lit = juce::jlimit (0.0f, 1.0f, (peakDb + 60.0f) / 60.0f);

    for (int i = 0; i < numLeds; ++i)
    {
        const float frac = (float) (i + 1) / (float) numLeds;   // bottom = 1/numLeds
        const float y = strip.getBottom() - (float) (i + 1) * ledH - (float) i * gap;
        juce::Rectangle<float> led (strip.getX(), y, strip.getWidth(), ledH);

        juce::Colour c = frac > 0.92f ? Palette::meterRed
                       : frac > 0.72f ? juce::Colour (0xffd9a02b)
                                      : Palette::meterGreen;
        const bool on = lit >= frac;
        g.setColour (on ? c : c.withAlpha (0.13f));
        g.fillRoundedRectangle (led, 1.5f);
    }
}

void MeterPanel::drawReadoutRow (juce::Graphics& g, juce::Rectangle<float> row)
{
    g.setColour (Palette::textDim);
    g.setFont (mpFont (juce::jmax (10.0f, row.getHeight() * 0.55f)));
    g.drawText ("Analog Mastering System", row.withTrimmedLeft (4.0f),
                juce::Justification::centredLeft, false);

    // DR LCD
    auto lcd = row.removeFromRight (row.getWidth() * 0.34f).reduced (0.0f, 1.0f);
    g.setColour (Palette::lcdBg);
    g.fillRect (lcd);
    g.setColour (juce::Colour (0xff333333));
    g.drawRect (lcd, 1.0f);

    const float drShow = drTarget < 0.05f ? 0.0f : drTarget;
    g.setColour (Palette::lcdText);
    g.setFont (mpFont (juce::jmax (10.0f, lcd.getHeight() * 0.62f), true));
    g.drawText ("DR: " + juce::String (drShow, 1) + "dB",
                lcd.reduced (5.0f, 0.0f), juce::Justification::centred, false);
}

} // namespace md::ui
