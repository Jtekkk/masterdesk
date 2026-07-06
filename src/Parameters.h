#pragma once

/*  MasterDesk — parameter definitions.

    Single source of truth for parameter IDs, ranges, defaults and display
    formatting. Both the processor and the preset system include this.
*/

#include <juce_audio_processors/juce_audio_processors.h>

namespace md::param
{

// IDs -------------------------------------------------------------------
inline constexpr auto volume       = "volume";
inline constexpr auto foundation   = "foundation";
inline constexpr auto tone         = "tone";
inline constexpr auto toneStyle    = "toneStyle";
inline constexpr auto thd          = "thd";
inline constexpr auto stereoEnh    = "stereoEnhance";
inline constexpr auto outputTrim   = "outputTrim";
inline constexpr auto compMode     = "compMode";
inline constexpr auto channelMode  = "channelMode";
inline constexpr auto oversampling = "oversampling";
inline constexpr auto phaseMode    = "phaseMode";
inline constexpr auto mix          = "mix";
inline constexpr auto autoGain     = "autoGain";
inline constexpr auto analog       = "analog";
inline constexpr auto temperature  = "temperature";
inline constexpr auto truePeak     = "truePeak";
inline constexpr auto ceiling      = "ceiling";
inline constexpr auto extSidechain = "extSidechain";
inline constexpr auto scHpf        = "scHpf";
inline constexpr auto bypass       = "bypass";

//==========================================================================
inline juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
{
    using namespace juce;
    using FloatParam  = AudioParameterFloat;
    using ChoiceParam = AudioParameterChoice;
    using BoolParam   = AudioParameterBool;

    auto dbText = [] (float v, int) { return String (v, 1) + " dB"; };
    auto pcText = [] (float v, int) { return String (roundToInt (v)) + " %"; };

    std::vector<std::unique_ptr<RangedAudioParameter>> p;

    p.push_back (std::make_unique<FloatParam> (
        ParameterID { volume, 1 }, "Volume",
        NormalisableRange<float> (-12.0f, 12.0f, 0.01f), 0.0f,
        AudioParameterFloatAttributes().withLabel ("dB").withStringFromValueFunction (dbText)));

    p.push_back (std::make_unique<FloatParam> (
        ParameterID { foundation, 1 }, "Foundation",
        NormalisableRange<float> (0.0f, 10.0f, 0.1f), 3.0f,
        AudioParameterFloatAttributes().withStringFromValueFunction (
            [] (float v, int) { return String (v, 1); })));

    p.push_back (std::make_unique<FloatParam> (
        ParameterID { tone, 1 }, "Tone",
        NormalisableRange<float> (0.0f, 100.0f, 1.0f), 16.0f,
        AudioParameterFloatAttributes().withLabel ("%").withStringFromValueFunction (pcText)));

    p.push_back (std::make_unique<ChoiceParam> (
        ParameterID { toneStyle, 1 }, "Tone Style",
        StringArray { "A", "B", "C", "D" }, 0));

    p.push_back (std::make_unique<FloatParam> (
        ParameterID { thd, 1 }, "THD",
        NormalisableRange<float> (-90.0f, -24.0f, 0.5f), -52.0f,
        AudioParameterFloatAttributes().withLabel ("dB").withStringFromValueFunction (
            [] (float v, int) { return String (roundToInt (v)); })));

    p.push_back (std::make_unique<FloatParam> (
        ParameterID { stereoEnh, 1 }, "Stereo Enhance",
        NormalisableRange<float> (0.0f, 100.0f, 1.0f), 6.0f,
        AudioParameterFloatAttributes().withLabel ("%").withStringFromValueFunction (pcText)));

    p.push_back (std::make_unique<FloatParam> (
        ParameterID { outputTrim, 1 }, "Output Trim",
        NormalisableRange<float> (-12.0f, 12.0f, 0.01f), 0.0f,
        AudioParameterFloatAttributes().withLabel ("dB").withStringFromValueFunction (dbText)));

    p.push_back (std::make_unique<ChoiceParam> (
        ParameterID { compMode, 1 }, "Compressor",
        StringArray { "Soft", "Hard" }, 0));

    p.push_back (std::make_unique<ChoiceParam> (
        ParameterID { channelMode, 1 }, "Channel Mode",
        StringArray { "Stereo", "Mid-Side", "Dual Mono" }, 0));

    p.push_back (std::make_unique<ChoiceParam> (
        ParameterID { oversampling, 1 }, "Oversampling",
        StringArray { "Off", "2x", "4x", "8x", "16x" }, 2));

    p.push_back (std::make_unique<ChoiceParam> (
        ParameterID { phaseMode, 1 }, "AA Phase",
        StringArray { "Min Phase", "Lin Phase" }, 0));

    p.push_back (std::make_unique<FloatParam> (
        ParameterID { mix, 1 }, "Mix",
        NormalisableRange<float> (0.0f, 100.0f, 1.0f), 100.0f,
        AudioParameterFloatAttributes().withLabel ("%").withStringFromValueFunction (pcText)));

    p.push_back (std::make_unique<BoolParam> (
        ParameterID { autoGain, 1 }, "Auto Gain", false));

    p.push_back (std::make_unique<FloatParam> (
        ParameterID { analog, 1 }, "Analog",
        NormalisableRange<float> (0.0f, 200.0f, 1.0f), 100.0f,
        AudioParameterFloatAttributes().withLabel ("%").withStringFromValueFunction (pcText)));

    p.push_back (std::make_unique<FloatParam> (
        ParameterID { temperature, 1 }, "Temperature",
        NormalisableRange<float> (15.0f, 45.0f, 0.5f), 25.0f,
        AudioParameterFloatAttributes().withLabel ("degC").withStringFromValueFunction (
            [] (float v, int) { return String (roundToInt (v)) + " C"; })));

    p.push_back (std::make_unique<BoolParam> (
        ParameterID { truePeak, 1 }, "True Peak Limit", true));

    p.push_back (std::make_unique<FloatParam> (
        ParameterID { ceiling, 1 }, "Ceiling",
        NormalisableRange<float> (-3.0f, 0.0f, 0.1f), -0.3f,
        AudioParameterFloatAttributes().withLabel ("dBTP").withStringFromValueFunction (dbText)));

    p.push_back (std::make_unique<BoolParam> (
        ParameterID { extSidechain, 1 }, "Ext Sidechain", false));

    p.push_back (std::make_unique<FloatParam> (
        ParameterID { scHpf, 1 }, "SC High-Pass",
        NormalisableRange<float> (20.0f, 500.0f, 1.0f, 0.5f), 60.0f,
        AudioParameterFloatAttributes().withLabel ("Hz").withStringFromValueFunction (
            [] (float v, int) { return String (roundToInt (v)) + " Hz"; })));

    p.push_back (std::make_unique<BoolParam> (
        ParameterID { bypass, 1 }, "Bypass", false));

    return { p.begin(), p.end() };
}

/** Every automatable ID, used by presets / A-B / locking. */
inline const juce::StringArray& allIds()
{
    static const juce::StringArray ids {
        volume, foundation, tone, toneStyle, thd, stereoEnh, outputTrim,
        compMode, channelMode, oversampling, phaseMode, mix, autoGain,
        analog, temperature, truePeak, ceiling, extSidechain, scHpf, bypass
    };
    return ids;
}

} // namespace md::param
