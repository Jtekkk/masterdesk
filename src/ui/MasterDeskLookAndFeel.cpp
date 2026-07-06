#include "MasterDeskLookAndFeel.h"

namespace md::ui
{

static juce::Font mdFont (float height, bool bold = false)
{
    auto opts = juce::FontOptions (height, bold ? juce::Font::bold : juce::Font::plain);
    return juce::Font (opts);
}

MasterDeskLookAndFeel::MasterDeskLookAndFeel()
{
    setColour (juce::Slider::textBoxTextColourId, Palette::text);
    setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour (juce::Label::textColourId, Palette::text);
    setColour (juce::TextButton::textColourOnId,  juce::Colour (0xff101010));
    setColour (juce::TextButton::textColourOffId, Palette::text);
    setColour (juce::ComboBox::backgroundColourId, Palette::lcdBg);
    setColour (juce::ComboBox::textColourId, Palette::lcdText);
    setColour (juce::ComboBox::outlineColourId, juce::Colour (0xff3a3a3a));
    setColour (juce::ComboBox::arrowColourId, Palette::textDim);
    setColour (juce::PopupMenu::backgroundColourId, juce::Colour (0xff141414));
    setColour (juce::PopupMenu::textColourId, Palette::text);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, juce::Colour (0xff3a3a3a));
    setColour (juce::TooltipWindow::backgroundColourId, juce::Colour (0xff101010));
    setColour (juce::TooltipWindow::textColourId, Palette::text);
    setColour (juce::TextEditor::backgroundColourId, Palette::lcdBg);
    setColour (juce::TextEditor::textColourId, Palette::lcdText);
    setColour (juce::AlertWindow::backgroundColourId, Palette::panelLight);
    setColour (juce::AlertWindow::textColourId, Palette::text);
}

//==============================================================================
void MasterDeskLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h,
                                              float pos, float startAngle, float endAngle,
                                              juce::Slider& slider)
{
    auto bounds = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h).reduced (2.0f);
    const float size   = juce::jmin (bounds.getWidth(), bounds.getHeight());
    const auto  centre = bounds.getCentre();
    const float dotRad   = size * 0.5f;
    const float knobRad  = size * 0.40f;
    const float angle    = startAngle + pos * (endAngle - startAngle);

    // ---- position dots around the knob ----
    {
        g.setColour (juce::Colour (0xff5a5a5a));
        const int numDots = 11;
        for (int i = 0; i < numDots; ++i)
        {
            const float a = startAngle + (endAngle - startAngle) * (float) i / (float) (numDots - 1);
            const float ds = size * 0.018f + 0.8f;
            const juce::Point<float> p (centre.x + dotRad * std::sin (a),
                                        centre.y - dotRad * std::cos (a));
            g.fillEllipse (p.x - ds * 0.5f, p.y - ds * 0.5f, ds, ds);
        }
    }

    // ---- drop shadow ----
    g.setColour (juce::Colours::black.withAlpha (0.55f));
    g.fillEllipse (centre.x - knobRad, centre.y - knobRad + size * 0.02f,
                   knobRad * 2.0f, knobRad * 2.0f);

    // ---- body: machined black knob ----
    {
        juce::ColourGradient body (juce::Colour (0xff3a3a3a),
                                   centre.x - knobRad * 0.6f, centre.y - knobRad * 0.8f,
                                   juce::Colour (0xff070707),
                                   centre.x + knobRad * 0.5f, centre.y + knobRad,
                                   false);
        g.setGradientFill (body);
        g.fillEllipse (centre.x - knobRad, centre.y - knobRad, knobRad * 2.0f, knobRad * 2.0f);

        // rim
        g.setColour (juce::Colour (0xff4c4c4c));
        g.drawEllipse (centre.x - knobRad, centre.y - knobRad, knobRad * 2.0f, knobRad * 2.0f,
                       juce::jmax (1.0f, size * 0.012f));

        // top face (slightly inset, darker)
        const float faceRad = knobRad * 0.82f;
        juce::ColourGradient face (juce::Colour (0xff262626),
                                   centre.x, centre.y - faceRad,
                                   juce::Colour (0xff101010),
                                   centre.x, centre.y + faceRad, false);
        g.setGradientFill (face);
        g.fillEllipse (centre.x - faceRad, centre.y - faceRad, faceRad * 2.0f, faceRad * 2.0f);

        // specular arc
        g.setColour (juce::Colours::white.withAlpha (0.06f));
        juce::Path spec;
        spec.addCentredArc (centre.x, centre.y, knobRad * 0.9f, knobRad * 0.9f, 0.0f,
                            -2.4f, -0.6f, true);
        g.strokePath (spec, juce::PathStrokeType (size * 0.03f));
    }

    // ---- pointer ----
    {
        const float len   = knobRad * 0.78f;
        const float inner = knobRad * 0.20f;
        const juce::Point<float> tip  (centre.x + len   * std::sin (angle),
                                       centre.y - len   * std::cos (angle));
        const juce::Point<float> tail (centre.x + inner * std::sin (angle),
                                       centre.y - inner * std::cos (angle));
        g.setColour (slider.isEnabled() ? juce::Colour (0xffdadada) : Palette::textDim);
        g.drawLine ({ tail, tip }, juce::jmax (1.6f, size * 0.028f));
    }
}

//==============================================================================
void MasterDeskLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& button,
                                                  const juce::Colour&, bool highlighted, bool down)
{
    auto b = button.getLocalBounds().toFloat().reduced (0.5f);
    const bool on = button.getToggleState();
    const float corner = 2.5f;

    auto base = on ? Palette::buttonOn : Palette::buttonOff;
    if (down)             base = base.darker (0.25f);
    else if (highlighted) base = base.brighter (0.12f);

    juce::ColourGradient grad (base.brighter (on ? 0.06f : 0.18f), b.getX(), b.getY(),
                               base.darker (0.35f), b.getX(), b.getBottom(), false);
    g.setGradientFill (grad);
    g.fillRoundedRectangle (b, corner);

    g.setColour (juce::Colours::black.withAlpha (0.85f));
    g.drawRoundedRectangle (b, corner, 1.0f);

    // top bevel line
    g.setColour (juce::Colours::white.withAlpha (on ? 0.35f : 0.08f));
    g.drawLine (b.getX() + 2.0f, b.getY() + 1.0f, b.getRight() - 2.0f, b.getY() + 1.0f);
}

void MasterDeskLookAndFeel::drawButtonText (juce::Graphics& g, juce::TextButton& button,
                                            bool, bool)
{
    g.setFont (getTextButtonFont (button, button.getHeight()));
    g.setColour (button.getToggleState() ? juce::Colour (0xff101010)
                                         : (button.isEnabled() ? Palette::text : Palette::textDim));
    g.drawFittedText (button.getButtonText(), button.getLocalBounds().reduced (2, 1),
                      juce::Justification::centred, 1);
}

//==============================================================================
void MasterDeskLookAndFeel::drawComboBox (juce::Graphics& g, int width, int height, bool,
                                          int, int, int, int, juce::ComboBox& box)
{
    auto b = juce::Rectangle<float> (0.0f, 0.0f, (float) width, (float) height).reduced (0.5f);
    g.setColour (Palette::lcdBg);
    g.fillRoundedRectangle (b, 2.5f);
    g.setColour (box.findColour (juce::ComboBox::outlineColourId));
    g.drawRoundedRectangle (b, 2.5f, 1.0f);

    juce::Path arrow;
    const float ax = (float) width - 11.0f, ay = (float) height * 0.5f;
    arrow.addTriangle (ax - 3.5f, ay - 2.0f, ax + 3.5f, ay - 2.0f, ax, ay + 3.0f);
    g.setColour (Palette::textDim);
    g.fillPath (arrow);
}

void MasterDeskLookAndFeel::positionComboBoxText (juce::ComboBox& box, juce::Label& label)
{
    label.setBounds (3, 1, box.getWidth() - 15, box.getHeight() - 2);
    label.setBorderSize ({ 1, 2, 1, 2 });
    label.setFont (getComboBoxFont (box));
}

juce::Font MasterDeskLookAndFeel::getComboBoxFont (juce::ComboBox&)          { return mdFont (11.5f); }
juce::Font MasterDeskLookAndFeel::getPopupMenuFont()                         { return mdFont (13.0f); }
juce::Font MasterDeskLookAndFeel::getTextButtonFont (juce::TextButton&, int) { return mdFont (11.5f, true); }

} // namespace md::ui
