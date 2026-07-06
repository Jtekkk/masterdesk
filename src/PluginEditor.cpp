#include "PluginEditor.h"

using namespace md::ui;

static juce::Font edFont (float height, bool bold = false)
{
    return juce::Font (juce::FontOptions (height, bold ? juce::Font::bold : juce::Font::plain));
}

//==============================================================================
MDKnob::MDKnob (MasterDeskProcessor& p, const juce::String& paramId)
    : proc (p), id (paramId)
{
    setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);

    if (auto* param = proc.apvts.getParameter (id))
        setDoubleClickReturnValue (true, param->convertFrom0to1 (param->getDefaultValue()));
}

void MDKnob::mouseDown (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu())
    {
        juce::PopupMenu m;
        m.addItem (1, "Lock parameter (keeps value while browsing presets)",
                   true, proc.isParamLocked (id));
        m.addItem (2, "Reset to default");

        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this),
                         [this] (int result)
                         {
                             if (result == 1)
                                 proc.toggleParamLock (id);
                             else if (result == 2)
                             {
                                 if (auto* param = proc.apvts.getParameter (id))
                                 {
                                     param->beginChangeGesture();
                                     param->setValueNotifyingHost (param->getDefaultValue());
                                     param->endChangeGesture();
                                 }
                             }
                             repaint();
                         });
        return;
    }

    juce::Slider::mouseDown (e);
}

void MDKnob::paintOverChildren (juce::Graphics& g)
{
    if (proc.isParamLocked (id))
    {
        const float s = juce::jmax (8.0f, (float) getWidth() * 0.10f);
        juce::Rectangle<float> r ((float) getWidth() - s - 2.0f, 2.0f, s, s);
        g.setColour (juce::Colour (0xffd9a02b));
        g.fillRoundedRectangle (r.withTrimmedTop (s * 0.35f), 1.5f);
        g.drawEllipse (r.reduced (s * 0.22f).withTrimmedBottom (s * 0.30f), 1.4f);
    }
}

//==============================================================================
MasterDeskEditor::LabelledKnob::LabelledKnob (MasterDeskEditor& ed, MasterDeskProcessor& p,
                                              const juce::String& paramId, const juce::String& title)
    : knob (p, paramId)
{
    ed.addAndMakeVisible (knob);
    ed.addAndMakeVisible (value);
    ed.addAndMakeVisible (name);

    value.setJustificationType (juce::Justification::centred);
    value.setInterceptsMouseClicks (false, false);
    value.setColour (juce::Label::textColourId, Palette::text);

    name.setText (title, juce::dontSendNotification);
    name.setJustificationType (juce::Justification::centred);
    name.setInterceptsMouseClicks (false, false);
    name.setColour (juce::Label::textColourId, Palette::text.withAlpha (0.92f));

    attachment = std::make_unique<SliderAtt> (p.apvts, paramId, knob);
    knob.setTooltip (title);
}

void MasterDeskEditor::LabelledKnob::setBounds (juce::Rectangle<int> knobArea,
                                                float valueFontPx, float nameFontPx)
{
    knob.setBounds (knobArea);
    const int lw = juce::jmax (knobArea.getWidth() * 2, 90);
    const int cx = knobArea.getCentreX();
    value.setFont (edFont (valueFontPx));
    name.setFont (edFont (nameFontPx, true));
    value.setBounds (cx - lw / 2, knobArea.getBottom() - 2, lw, (int) (valueFontPx * 1.4f));
    name.setBounds  (cx - lw / 2, value.getBounds().getBottom() - 1, lw, (int) (nameFontPx * 1.5f));
}

void MasterDeskEditor::LabelledKnob::refreshValue (juce::AudioProcessorValueTreeState& apvts,
                                                   const juce::String& paramId)
{
    if (auto* param = apvts.getParameter (paramId))
        value.setText (param->getCurrentValueAsText(), juce::dontSendNotification);
}

//==============================================================================
MasterDeskEditor::BarSlider::BarSlider (MasterDeskEditor& ed, MasterDeskProcessor& p,
                                        const juce::String& paramId, const juce::String& title)
{
    ed.addAndMakeVisible (slider);
    ed.addAndMakeVisible (name);
    slider.setSliderStyle (juce::Slider::LinearBar);
    slider.setColour (juce::Slider::trackColourId, juce::Colour (0xff353535));
    slider.setColour (juce::Slider::backgroundColourId, Palette::lcdBg);
    slider.setColour (juce::Slider::textBoxTextColourId, Palette::lcdText);
    slider.setTextBoxIsEditable (false);
    slider.setTooltip (title);

    name.setText (title, juce::dontSendNotification);
    name.setJustificationType (juce::Justification::centredRight);
    name.setColour (juce::Label::textColourId, Palette::textDim);
    name.setBorderSize ({ 1, 1, 1, 2 });

    attachment = std::make_unique<SliderAtt> (p.apvts, paramId, slider);
}

//==============================================================================
MasterDeskEditor::MasterDeskEditor (MasterDeskProcessor& p)
    : AudioProcessorEditor (&p),
      proc (p),
      outputTrim  (*this, p, md::param::outputTrim, "Output Trim"),
      thd         (*this, p, md::param::thd,        "THD"),
      volume      (*this, p, md::param::volume,     "1. Volume"),
      foundationK (*this, p, md::param::foundation, "2. Foundation"),
      stereoEnh   (*this, p, md::param::stereoEnh,  "Stereo Enhance"),
      toneK       (*this, p, md::param::tone,       "3. Tone"),
      analogBar   (*this, p, md::param::analog,      "ANLG"),
      tempBar     (*this, p, md::param::temperature, "TEMP"),
      mixBar      (*this, p, md::param::mix,         "MIX"),
      ceilingBar  (*this, p, md::param::ceiling,     "CEIL"),
      scHpfBar    (*this, p, md::param::scHpf,       "HPF")
{
    setLookAndFeel (&lnf);

    addAndMakeVisible (meter);
    meter.onSpectrumModeChange = [this] (bool on) { specButton.setToggleState (on, juce::dontSendNotification); };

    // ---- compressor / tone row ----
    for (auto* l : { &compLabel, &toneLabel })
    {
        addAndMakeVisible (*l);
        l->setColour (juce::Label::textColourId, Palette::text);
        l->setJustificationType (juce::Justification::centredRight);
    }

    addAndMakeVisible (compModeButton);
    compModeButton.setToggleState (true, juce::dontSendNotification);
    compModeButton.setTooltip ("Compressor character: SOFT (2:1 glue) / HARD (4:1 punch)");
    compModeButton.onClick = [this]
    {
        if (auto* param = proc.apvts.getParameter (md::param::compMode))
        {
            const bool soft = param->getValue() < 0.5f;
            param->beginChangeGesture();
            param->setValueNotifyingHost (soft ? 1.0f : 0.0f);
            param->endChangeGesture();
        }
    };

    static const char* toneNames[4] = { "A", "B", "C", "D" };
    for (int i = 0; i < 4; ++i)
    {
        addAndMakeVisible (toneButtons[i]);
        toneButtons[i].setButtonText (toneNames[i]);
        toneButtons[i].setTooltip (juce::String ("Tone voicing ") + toneNames[i]
                                   + (i == 0 ? " - modern"  : i == 1 ? " - presence"
                                    : i == 2 ? " - air"     : " - warm"));
        toneButtons[i].onClick = [this, i]
        {
            if (auto* param = proc.apvts.getParameter (md::param::toneStyle))
            {
                param->beginChangeGesture();
                param->setValueNotifyingHost ((float) i / 3.0f);
                param->endChangeGesture();
            }
        };
    }

    // ---- toolbar row 1: presets, A/B, undo, spec, bypass ----
    for (auto* b : { &presetPrev, &presetNext, &presetSave, &abCopy,
                     &undoButton, &redoButton, &specButton, &bypassButton })
        addAndMakeVisible (*b);

    addAndMakeVisible (presetBox);
    presetBox.setTextWhenNothingSelected ("Default");
    presetBox.setTooltip ("Preset browser (right-click knobs to lock them while browsing)");
    presetBox.onChange = [this]
    {
        const int idx = presetBox.getSelectedItemIndex();
        if (idx >= 0)
            proc.presets->applyPreset (idx);
    };

    presetPrev.onClick = [this]
    {
        const int n = proc.presets->getPresetNames().size();
        if (n > 0) proc.presets->applyPreset ((proc.presets->getCurrentPresetIndex() + n - 1) % n);
    };
    presetNext.onClick = [this]
    {
        const int n = proc.presets->getPresetNames().size();
        if (n > 0) proc.presets->applyPreset ((proc.presets->getCurrentPresetIndex() + 1) % n);
    };
    presetSave.onClick = [this] { promptSavePreset(); };
    presetSave.setTooltip ("Save the current settings as a user preset");

    for (int i = 0; i < 2; ++i)
    {
        addAndMakeVisible (abButtons[i]);
        abButtons[i].setButtonText (i == 0 ? "A" : "B");
        abButtons[i].setTooltip ("A/B compare slots");
        abButtons[i].onClick = [this, i] { proc.presets->setActiveSlot (i); };
    }
    abCopy.setTooltip ("Copy the active slot to the other one");
    abCopy.onClick = [this]
    {
        const int active = proc.presets->getActiveSlot();
        proc.presets->storeCurrentTo (active);
        proc.presets->copySlot (active, 1 - active);
    };

    undoButton.onClick = [this] { proc.undoManager.undo(); };
    redoButton.onClick = [this] { proc.undoManager.redo(); };

    specButton.setTooltip ("Toggle spectrum analyser view");
    specButton.onClick = [this] { meter.setSpectrumMode (! meter.isSpectrumMode()); };

    bypassButton.setClickingTogglesState (true);
    bypassButton.setTooltip ("Latency-compensated, click-free bypass");
    bypassAtt = std::make_unique<ButtonAtt> (proc.apvts, md::param::bypass, bypassButton);

    // ---- toolbar row 2: engine controls ----
    auto initChoiceBox = [this] (juce::ComboBox& box, const char* paramId, const char* tip)
    {
        addAndMakeVisible (box);
        if (auto* param = proc.apvts.getParameter (paramId))
        {
            int id = 1;
            for (auto& s : param->getAllValueStrings())
                box.addItem (s, id++);
        }
        box.setTooltip (tip);
    };

    initChoiceBox (osBox,    md::param::oversampling, "Oversampling of the nonlinear stage");
    initChoiceBox (phaseBox, md::param::phaseMode,    "Anti-alias filters: minimum phase (low latency) or linear phase");
    initChoiceBox (modeBox,  md::param::channelMode,  "Processing domain: stereo, mid-side or dual mono");
    osAtt    = std::make_unique<ComboAtt> (proc.apvts, md::param::oversampling, osBox);
    phaseAtt = std::make_unique<ComboAtt> (proc.apvts, md::param::phaseMode,    phaseBox);
    modeAtt  = std::make_unique<ComboAtt> (proc.apvts, md::param::channelMode,  modeBox);

    for (auto* b : { &tpButton, &agButton, &scButton })
    {
        addAndMakeVisible (*b);
        b->setClickingTogglesState (true);
    }
    tpButton.setTooltip ("True-peak (inter-sample) limiting");
    agButton.setTooltip ("Automatic loudness-matched gain compensation");
    scButton.setTooltip ("Key the compressor from the external sidechain bus");
    tpAtt = std::make_unique<ButtonAtt> (proc.apvts, md::param::truePeak,     tpButton);
    agAtt = std::make_unique<ButtonAtt> (proc.apvts, md::param::autoGain,     agButton);
    scAtt = std::make_unique<ButtonAtt> (proc.apvts, md::param::extSidechain, scButton);

    spectrumScratch.resize (8192);

    // ---- window ----
    // read the persisted size before the resize limits fire a resized() that
    // would overwrite the stored value with the clamped minimum
    const int w = (int) (double) proc.apvts.state.getProperty ("uiWidth",  1000);
    const int h = (int) (double) proc.apvts.state.getProperty ("uiHeight", 700);

    setResizable (true, true);
    setResizeLimits (760, 532, 2400, 1680);
    setSize (juce::jlimit (760, 2400, w), juce::jlimit (532, 1680, h));

#if MASTERDESK_USE_OPENGL
    glContext.setContinuousRepainting (false);
    glContext.attachTo (*this);
#endif

    refreshPresetBox();
    startTimerHz (30);
}

MasterDeskEditor::~MasterDeskEditor()
{
#if MASTERDESK_USE_OPENGL
    glContext.detach();
#endif
    setLookAndFeel (nullptr);
}

//==============================================================================
juce::Rectangle<int> MasterDeskEditor::proportional (float x, float y, float w, float h) const
{
    return { (int) (x * (float) getWidth()),  (int) (y * (float) getHeight()),
             (int) (w * (float) getWidth()),  (int) (h * (float) getHeight()) };
}

void MasterDeskEditor::resized()
{
    const float W = (float) getWidth();
    const float H = (float) getHeight();
    const float fVal  = juce::jmax (10.0f, H * 0.019f);
    const float fName = juce::jmax (10.0f, H * 0.021f);

    auto square = [&] (float cx, float cy, float d)
    {
        return juce::Rectangle<int> ((int) (cx * W - d * W * 0.5f), (int) (cy * H - d * W * 0.5f),
                                     (int) (d * W), (int) (d * W));
    };

    outputTrim.setBounds  (square (0.115f, 0.195f, 0.080f), fVal, fName);
    thd.setBounds         (square (0.885f, 0.195f, 0.080f), fVal, fName);
    volume.setBounds      (square (0.150f, 0.610f, 0.170f), fVal, fName * 1.25f);
    foundationK.setBounds (square (0.850f, 0.610f, 0.170f), fVal, fName * 1.25f);
    stereoEnh.setBounds   (square (0.410f, 0.700f, 0.085f), fVal, fName);
    toneK.setBounds       (square (0.590f, 0.700f, 0.085f), fVal, fName);

    meter.setBounds (proportional (0.285f, 0.100f, 0.430f, 0.345f));

    // compressor / tone row
    {
        auto row = proportional (0.285f, 0.465f, 0.430f, 0.042f);
        const int h = row.getHeight();
        compLabel.setFont (edFont (juce::jmax (10.0f, H * 0.020f)));
        toneLabel.setFont (edFont (juce::jmax (10.0f, H * 0.020f)));

        auto left = row.removeFromLeft (row.getWidth() / 2).reduced (2, 0);
        compLabel.setBounds (left.removeFromLeft ((int) (left.getWidth() * 0.55f)));
        left.removeFromLeft (6);
        compModeButton.setBounds (left.removeFromLeft ((int) (W * 0.055f)).withHeight (h));

        auto right = row.reduced (2, 0);
        toneLabel.setBounds (right.removeFromLeft ((int) (right.getWidth() * 0.30f)));
        right.removeFromLeft (6);
        const int bw = (int) (W * 0.028f);
        for (auto& b : toneButtons)
        {
            b.setBounds (right.removeFromLeft (bw).withHeight (h));
            right.removeFromLeft (4);
        }
    }

    // ---- toolbar ----
    auto bar = proportional (0.018f, 0.880f, 0.964f, 0.100f);
    const int rowH = bar.getHeight() / 2 - 2;

    auto row1 = bar.removeFromTop (rowH);
    bar.removeFromTop (4);
    auto row2 = bar.removeFromTop (rowH);

    auto place = [] (juce::Rectangle<int>& area, juce::Component& c, int w, int gap = 4)
    {
        c.setBounds (area.removeFromLeft (w));
        area.removeFromLeft (gap);
    };

    const int u = juce::jmax (24, (int) (W * 0.030f));
    place (row1, presetPrev, u / 2 + 6);
    place (row1, presetBox, (int) (W * 0.170f));
    place (row1, presetNext, u / 2 + 6);
    place (row1, presetSave, (int) (W * 0.048f), 12);
    place (row1, abButtons[0], u / 2 + 8);
    place (row1, abButtons[1], u / 2 + 8);
    place (row1, abCopy, (int) (W * 0.042f), 12);
    place (row1, undoButton, (int) (W * 0.050f));
    place (row1, redoButton, (int) (W * 0.050f), 12);

    bypassButton.setBounds (row1.removeFromRight ((int) (W * 0.062f)));
    row1.removeFromRight (6);
    specButton.setBounds (row1.removeFromRight ((int) (W * 0.048f)));

    place (row2, osBox, (int) (W * 0.058f));
    place (row2, phaseBox, (int) (W * 0.092f));
    place (row2, modeBox, (int) (W * 0.094f), 10);
    place (row2, tpButton, (int) (W * 0.032f));
    place (row2, agButton, (int) (W * 0.044f));
    place (row2, scButton, (int) (W * 0.050f), 10);

    auto placeBar = [&] (BarSlider& bs, float nameW, float barW)
    {
        bs.name.setFont (edFont (juce::jmax (8.5f, H * 0.0135f), true));
        bs.name.setBounds (row2.removeFromLeft ((int) (W * nameW)));
        bs.slider.setBounds (row2.removeFromLeft ((int) (W * barW)));
        row2.removeFromLeft (4);
    };
    placeBar (analogBar,  0.033f, 0.056f);
    placeBar (tempBar,    0.033f, 0.048f);
    placeBar (mixBar,     0.026f, 0.052f);
    placeBar (ceilingBar, 0.030f, 0.062f);
    placeBar (scHpfBar,   0.027f, 0.058f);

    // persist size
    proc.apvts.state.setProperty ("uiWidth",  getWidth(),  nullptr);
    proc.apvts.state.setProperty ("uiHeight", getHeight(), nullptr);
}

//==============================================================================
void MasterDeskEditor::paint (juce::Graphics& g)
{
    const float W = (float) getWidth();
    const float H = (float) getHeight();

    g.fillAll (Palette::panel);

    // brushed texture
    if (backgroundTexture.isNull() || backgroundTexture.getWidth() != 256)
    {
        backgroundTexture = juce::Image (juce::Image::ARGB, 256, 256, true);
        juce::Graphics tg (backgroundTexture);
        juce::Random rnd (0x4d61446bl);
        for (int i = 0; i < 9000; ++i)
        {
            const float x = rnd.nextFloat() * 256.0f, y = rnd.nextFloat() * 256.0f;
            tg.setColour (juce::Colours::white.withAlpha (rnd.nextFloat() * 0.028f));
            tg.fillRect (x, y, 1.0f, 1.0f);
            tg.setColour (juce::Colours::black.withAlpha (rnd.nextFloat() * 0.05f));
            tg.fillRect (256.0f - x, y, 1.0f, 1.0f);
        }
    }
    g.setTiledImageFill (backgroundTexture, 0, 0, 1.0f);
    g.fillRect (getLocalBounds());

    // vignette
    juce::ColourGradient vin (juce::Colours::transparentBlack, W * 0.5f, H * 0.42f,
                              juce::Colours::black.withAlpha (0.42f), 0.0f, 0.0f, true);
    g.setGradientFill (vin);
    g.fillRect (getLocalBounds());

    // ---- header ----
    {
        const float cy = H * 0.052f;

        // logo: red circle with a white wave glyph
        const float r = juce::jmax (9.0f, H * 0.020f);
        const float cx = W * 0.300f;
        g.setColour (Palette::accentRed);
        g.fillEllipse (cx - r, cy - r, r * 2.0f, r * 2.0f);
        g.setColour (juce::Colours::white);
        juce::Path wave;
        wave.startNewSubPath (cx - r * 0.55f, cy + r * 0.25f);
        wave.cubicTo (cx - r * 0.25f, cy - r * 0.75f, cx + r * 0.05f, cy + r * 0.75f, cx + r * 0.30f, cy - r * 0.15f);
        wave.lineTo (cx + r * 0.55f, cy - r * 0.15f);
        g.strokePath (wave, juce::PathStrokeType (juce::jmax (1.6f, r * 0.18f),
                                                  juce::PathStrokeType::curved,
                                                  juce::PathStrokeType::rounded));

        g.setColour (Palette::text);
        g.setFont (edFont (juce::jmax (14.0f, H * 0.031f), true));
        g.drawText ("MASTERDESK", juce::Rectangle<float> (W * 0.318f, cy - H * 0.022f, W * 0.20f, H * 0.044f),
                    juce::Justification::centredLeft, false);
        g.setColour (Palette::textDim);
        g.setFont (edFont (juce::jmax (13.0f, H * 0.027f)));
        g.drawText ("Classic", juce::Rectangle<float> (W * 0.520f, cy - H * 0.022f, W * 0.14f, H * 0.044f),
                    juce::Justification::centredLeft, false);

        // "TOL inside" badge (tolerance-modelled analog channels)
        juce::Rectangle<float> badge (W * 0.918f, cy - H * 0.020f, W * 0.048f, H * 0.040f);
        g.setColour (juce::Colour (0xff0a0a0a));
        g.fillRoundedRectangle (badge, 3.0f);
        g.setColour (juce::Colour (0xff3c3c3c));
        g.drawRoundedRectangle (badge, 3.0f, 1.0f);
        g.setColour (Palette::text);
        g.setFont (edFont (juce::jmax (9.0f, H * 0.017f), true));
        g.drawText ("TOL", badge.withTrimmedBottom (badge.getHeight() * 0.45f),
                    juce::Justification::centredBottom, false);
        g.setFont (edFont (juce::jmax (7.0f, H * 0.012f)));
        g.setColour (Palette::textDim);
        g.drawText ("inside", badge.withTrimmedTop (badge.getHeight() * 0.55f),
                    juce::Justification::centredTop, false);
    }

    // toolbar backdrop
    {
        auto bar = proportional (0.0f, 0.868f, 1.0f, 0.132f).toFloat();
        g.setColour (juce::Colours::black.withAlpha (0.25f));
        g.fillRect (bar);
        g.setColour (juce::Colours::white.withAlpha (0.05f));
        g.drawLine (bar.getX(), bar.getY(), bar.getRight(), bar.getY());
    }
}

//==============================================================================
void MasterDeskEditor::timerCallback()
{
    // ---- meters ----
    meter.update (proc.meters.dr.load(),
                  proc.meters.peakL.load() > proc.meters.peakR.load()
                      ? proc.meters.peakL.load() : proc.meters.peakR.load(),
                  proc.meters.lufsShort.load(),
                  proc.meters.lufsIntegrated.load(),
                  proc.meters.compGr.load(),
                  proc.meters.limGr.load(),
                  proc.meters.truePeak.load());

    // ---- spectrum feed ----
    const int got = proc.readSpectrum (spectrumScratch.data(), (int) spectrumScratch.size());
    if (got > 0 && meter.isSpectrumMode())
        meter.pushSamples (spectrumScratch.data(), got);

    // ---- dynamic widget state ----
    updateDynamicState();

    // group parameter edits into undo transactions about twice a second
    if (++undoTickCounter >= 15)
    {
        undoTickCounter = 0;
        proc.undoManager.beginNewTransaction();
        refreshPresetBox();
    }
}

void MasterDeskEditor::updateDynamicState()
{
    outputTrim.refreshValue  (proc.apvts, md::param::outputTrim);
    thd.refreshValue         (proc.apvts, md::param::thd);
    volume.refreshValue      (proc.apvts, md::param::volume);
    foundationK.refreshValue (proc.apvts, md::param::foundation);
    stereoEnh.refreshValue   (proc.apvts, md::param::stereoEnh);
    toneK.refreshValue       (proc.apvts, md::param::tone);

    if (auto* param = proc.apvts.getRawParameterValue (md::param::compMode))
        compModeButton.setButtonText (param->load() < 0.5f ? "SOFT" : "HARD");

    if (auto* param = proc.apvts.getRawParameterValue (md::param::toneStyle))
    {
        const int idx = (int) param->load();
        for (int i = 0; i < 4; ++i)
            toneButtons[i].setToggleState (i == idx, juce::dontSendNotification);
    }

    undoButton.setEnabled (proc.undoManager.canUndo());
    redoButton.setEnabled (proc.undoManager.canRedo());

    const int active = proc.presets->getActiveSlot();
    abButtons[0].setToggleState (active == 0, juce::dontSendNotification);
    abButtons[1].setToggleState (active == 1, juce::dontSendNotification);
    abCopy.setButtonText (active == 0 ? "A>B" : "B>A");

    presetBox.setTextWhenNothingSelected (proc.presets->getCurrentPresetName());
}

void MasterDeskEditor::refreshPresetBox()
{
    auto names = proc.presets->getPresetNames();
    const auto hash = names.joinIntoString ("|");
    if (hash == lastPresetListHash)
        return;
    lastPresetListHash = hash;

    presetBox.clear (juce::dontSendNotification);
    int id = 1;
    for (auto& n : names)
    {
        presetBox.addItem (n, id++);
        if (id - 2 == proc.presets->getNumFactoryPresets() - 1)
            presetBox.addSeparator();
    }
}

void MasterDeskEditor::promptSavePreset()
{
    auto* aw = new juce::AlertWindow ("Save preset",
                                      "Store the current settings as a user preset:",
                                      juce::MessageBoxIconType::NoIcon);
    aw->addTextEditor ("name", proc.presets->getCurrentPresetName(), "Name");
    aw->addButton ("Save", 1, juce::KeyPress (juce::KeyPress::returnKey));
    aw->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));

    aw->enterModalState (true,
        juce::ModalCallbackFunction::create ([this, aw] (int result)
        {
            if (result == 1)
            {
                proc.presets->saveUserPreset (aw->getTextEditorContents ("name"));
                lastPresetListHash.clear();
                refreshPresetBox();
            }
        }),
        true);
}
