#pragma once

/*  MasterDesk — look & feel.

    Entirely vector-drawn (crisp at any scale / DPI): black console knobs
    with a machined rim, white pointer and a ring of position dots; beveled
    hardware buttons; dark LCD combo boxes. Colours sampled from the
    reference faceplate.
*/

#include <juce_gui_basics/juce_gui_basics.h>

namespace md::ui
{

struct Palette
{
    static inline const juce::Colour panel        { 0xff181818 };
    static inline const juce::Colour panelLight   { 0xff222222 };
    static inline const juce::Colour text         { 0xffd6d6d6 };
    static inline const juce::Colour textDim      { 0xff8f8f8f };
    static inline const juce::Colour accentRed    { 0xffd6342c };
    static inline const juce::Colour meterFace    { 0xffefe9da };
    static inline const juce::Colour meterGreen   { 0xff3fae4c };
    static inline const juce::Colour meterRed     { 0xffd0342c };
    static inline const juce::Colour lcdBg        { 0xff090909 };
    static inline const juce::Colour lcdText      { 0xffe8e8e8 };
    static inline const juce::Colour buttonOn     { 0xffe3e3e3 };
    static inline const juce::Colour buttonOff    { 0xff2c2c2c };
};

class MasterDeskLookAndFeel : public juce::LookAndFeel_V4
{
public:
    MasterDeskLookAndFeel();

    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h,
                           float sliderPosProportional, float rotaryStartAngle,
                           float rotaryEndAngle, juce::Slider&) override;

    void drawButtonBackground (juce::Graphics&, juce::Button&,
                               const juce::Colour& backgroundColour,
                               bool shouldDrawButtonAsHighlighted,
                               bool shouldDrawButtonAsDown) override;

    void drawButtonText (juce::Graphics&, juce::TextButton&,
                         bool shouldDrawButtonAsHighlighted,
                         bool shouldDrawButtonAsDown) override;

    void drawComboBox (juce::Graphics&, int width, int height, bool isButtonDown,
                       int buttonX, int buttonY, int buttonW, int buttonH,
                       juce::ComboBox&) override;

    void positionComboBoxText (juce::ComboBox&, juce::Label&) override;

    juce::Font getComboBoxFont (juce::ComboBox&) override;
    juce::Font getPopupMenuFont() override;
    juce::Font getTextButtonFont (juce::TextButton&, int buttonHeight) override;
};

} // namespace md::ui
