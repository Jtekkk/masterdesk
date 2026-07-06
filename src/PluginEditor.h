#pragma once

/*  MasterDesk — editor.

    Faceplate layout follows the reference unit: Output Trim and THD up top,
    the Dynamic Range meter in the centre with the compressor / tone voicing
    switches beneath it, Volume and Foundation as the two large workhorse
    knobs, Stereo Enhance and Tone between them. A slim two-row toolbar at
    the bottom exposes the engine controls (oversampling, phase mode,
    channel mode, true-peak, auto-gain, sidechain, analog amount,
    temperature, mix, ceiling) plus presets, A/B, undo/redo and bypass.

    • Fully resizable (fixed aspect), every element proportional + vector.
    • Right-click any knob → lock it against preset browsing / reset it.
    • Optional GPU (OpenGL) accelerated rendering.
*/

#include <juce_audio_utils/juce_audio_utils.h>
#if MASTERDESK_USE_OPENGL
 #include <juce_opengl/juce_opengl.h>
#endif

#include "PluginProcessor.h"
#include "ui/MasterDeskLookAndFeel.h"
#include "ui/MeterPanel.h"

//==============================================================================
/** Rotary knob with right-click lock/reset menu and a lock badge. */
class MDKnob : public juce::Slider
{
public:
    MDKnob (MasterDeskProcessor& p, const juce::String& paramId);

    void mouseDown (const juce::MouseEvent&) override;
    void paintOverChildren (juce::Graphics&) override;

private:
    MasterDeskProcessor& proc;
    juce::String id;
};

//==============================================================================
class MasterDeskEditor : public juce::AudioProcessorEditor,
                         private juce::Timer
{
public:
    explicit MasterDeskEditor (MasterDeskProcessor&);
    ~MasterDeskEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void refreshPresetBox();
    void promptSavePreset();
    void updateDynamicState();

    juce::Rectangle<int> proportional (float x, float y, float w, float h) const;

    MasterDeskProcessor& proc;
    md::ui::MasterDeskLookAndFeel lnf;
    juce::TooltipWindow tooltips { this, 600 };

    juce::Image backgroundTexture;

    //==========================================================================
    using SliderAtt = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ComboAtt  = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    using ButtonAtt = juce::AudioProcessorValueTreeState::ButtonAttachment;

    struct LabelledKnob
    {
        LabelledKnob (MasterDeskEditor& ed, MasterDeskProcessor& p,
                      const juce::String& paramId, const juce::String& title);
        void setBounds (juce::Rectangle<int> knobArea, float valueFontPx, float nameFontPx);
        void refreshValue (juce::AudioProcessorValueTreeState& apvts, const juce::String& paramId);

        MDKnob knob;
        juce::Label value, name;
        std::unique_ptr<SliderAtt> attachment;
    };

    LabelledKnob outputTrim, thd, volume, foundationK, stereoEnh, toneK;

    md::ui::MeterPanel meter;

    // compressor / tone voicing row
    juce::Label compLabel { {}, "Compressor" }, toneLabel { {}, "Tone" };
    juce::TextButton compModeButton;
    juce::TextButton toneButtons[4];

    // toolbar row 1
    juce::TextButton presetPrev { "<" }, presetNext { ">" }, presetSave { "SAVE" };
    juce::ComboBox presetBox;
    juce::TextButton abButtons[2];
    juce::TextButton abCopy;
    juce::TextButton undoButton { "UNDO" }, redoButton { "REDO" };
    juce::TextButton specButton { "SPEC" };
    juce::TextButton bypassButton { "BYPASS" };
    std::unique_ptr<ButtonAtt> bypassAtt;

    // toolbar row 2
    juce::ComboBox osBox, phaseBox, modeBox;
    std::unique_ptr<ComboAtt> osAtt, phaseAtt, modeAtt;
    juce::TextButton tpButton { "TP" }, agButton { "AUTO" }, scButton { "EXT SC" };
    std::unique_ptr<ButtonAtt> tpAtt, agAtt, scAtt;

    struct BarSlider
    {
        BarSlider (MasterDeskEditor& ed, MasterDeskProcessor& p,
                   const juce::String& paramId, const juce::String& title);
        juce::Slider slider;
        juce::Label name;
        std::unique_ptr<SliderAtt> attachment;
    };
    BarSlider analogBar, tempBar, mixBar, ceilingBar, scHpfBar;

    std::vector<float> spectrumScratch;

    int undoTickCounter = 0;
    juce::String lastPresetListHash;

#if MASTERDESK_USE_OPENGL
    juce::OpenGLContext glContext;
#endif

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MasterDeskEditor)
};
