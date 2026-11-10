#include "Theme.h"
#include "BinaryData.h"
#include "../Model.h"

namespace ok::ui
{

namespace
{
    struct Faces
    {
        juce::Typeface::Ptr sans[4], mono[2];

        Faces()
        {
            auto load = [] (const char* data, int size) { return juce::Typeface::createSystemTypefaceFor (data, (size_t) size); };
            sans[0] = load (BinaryData::IBMPlexSansCondensedRegular_ttf, BinaryData::IBMPlexSansCondensedRegular_ttfSize);
            sans[1] = load (BinaryData::IBMPlexSansCondensedMedium_ttf, BinaryData::IBMPlexSansCondensedMedium_ttfSize);
            sans[2] = load (BinaryData::IBMPlexSansCondensedSemiBold_ttf, BinaryData::IBMPlexSansCondensedSemiBold_ttfSize);
            sans[3] = load (BinaryData::IBMPlexSansCondensedBold_ttf, BinaryData::IBMPlexSansCondensedBold_ttfSize);
            mono[0] = load (BinaryData::IBMPlexMonoRegular_ttf, BinaryData::IBMPlexMonoRegular_ttfSize);
            mono[1] = load (BinaryData::IBMPlexMonoSemiBold_ttf, BinaryData::IBMPlexMonoSemiBold_ttfSize);
        }
    };

    Faces& faces()
    {
        static Faces f;
        return f;
    }
}

juce::Font sans (float size, int weight)
{
    int i = weight >= 700 ? 3 : weight >= 600 ? 2 : weight >= 500 ? 1 : 0;
    return juce::Font (juce::FontOptions (faces().sans[i]).withPointHeight (size));
}

juce::Font mono (float size, int weight)
{
    return juce::Font (juce::FontOptions (faces().mono[weight >= 600 ? 1 : 0]).withPointHeight (size));
}

juce::Font caps (float size, int weight)
{
    return sans (size, weight).withExtraKerningFactor (0.12f);
}

juce::Colour trackColour (int paletteIndex)
{
    return juce::Colour (ok::paletteArgb (paletteIndex));
}

void drawKnob (juce::Graphics& g, juce::Rectangle<float> area, float frac, juce::Colour arc, bool bipolar,
               float strokeScale, float innerRadius)
{
    using namespace juce;
    const float s = juce::jmin (area.getWidth(), area.getHeight()) / 48.0f;
    const auto c = area.getCentre();
    const float r = 18.0f * s, stroke = strokeScale * s;
    frac = juce::jlimit (0.0f, 1.0f, frac);

    const float a0 = MathConstants<float>::pi * 1.25f, a1 = MathConstants<float>::pi * 2.75f;
    Path track;
    track.addCentredArc (c.x, c.y, r, r, 0.0f, a0, a1, true);
    g.setColour (C::knobTrack);
    g.strokePath (track, PathStrokeType (stroke, PathStrokeType::curved, PathStrokeType::rounded));

    Path val;
    if (bipolar)
    {
        const float top = MathConstants<float>::twoPi;
        const float av = top + (frac - 0.5f) * MathConstants<float>::pi * 1.5f;
        if (std::abs (av - top) > 0.01f)
            val.addCentredArc (c.x, c.y, r, r, 0.0f, juce::jmin (top, av), juce::jmax (top, av), true);
        else
            val.addCentredArc (c.x, c.y, r, r, 0.0f, top - 0.01f, top + 0.01f, true);
    }
    else
        val.addCentredArc (c.x, c.y, r, r, 0.0f, a0, a0 + juce::jmax (0.02f, frac) * MathConstants<float>::pi * 1.5f, true);
    g.setColour (arc);
    g.strokePath (val, PathStrokeType (stroke, PathStrokeType::curved, PathStrokeType::rounded));

    g.setColour (C::btn2);
    g.fillEllipse (Rectangle<float> (innerRadius * 2 * s, innerRadius * 2 * s).withCentre (c));

    const float ang = (135.0f + frac * 270.0f) * MathConstants<float>::pi / 180.0f;
    const float pr = 9.0f * s;
    g.setColour (C::text);
    g.drawLine (c.x, c.y, c.x + pr * std::cos (ang), c.y + pr * std::sin (ang), juce::jmax (1.5f, 2.0f * s * (strokeScale > 4.5f ? 1.5f : 1.0f)));
}

void drawChevron (juce::Graphics& g, juce::Point<float> c, juce::Colour col, float size)
{
    juce::Path p;
    float h = size * 0.3f, w = size * 0.3f;
    p.startNewSubPath (c.x - w, c.y - h * 0.5f);
    p.lineTo (c.x, c.y + h * 0.5f);
    p.lineTo (c.x + w, c.y - h * 0.5f);
    g.setColour (col);
    g.strokePath (p, juce::PathStrokeType (1.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

void drawCross (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour c, float thickness)
{
    g.setColour (c);
    g.drawLine (r.getX(), r.getY(), r.getRight(), r.getBottom(), thickness);
    g.drawLine (r.getRight(), r.getY(), r.getX(), r.getBottom(), thickness);
}

void drawDots (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour c)
{
    g.setColour (c);
    auto cy = r.getCentreY();
    for (int i = -1; i <= 1; ++i)
        g.fillEllipse (juce::Rectangle<float> (2.8f, 2.8f).withCentre ({ r.getCentreX() + i * 4.0f, cy }));
}

void drawWarning (juce::Graphics& g, juce::Rectangle<float> r)
{
    juce::Path tri;
    tri.addTriangle (r.getCentreX(), r.getY() + r.getHeight() * 0.09f, r.getRight(), r.getBottom() - r.getHeight() * 0.12f,
                     r.getX(), r.getBottom() - r.getHeight() * 0.12f);
    g.setColour (C::red);
    g.fillPath (tri);
    g.setColour (C::ink);
    auto cx = r.getCentreX();
    g.drawLine (cx, r.getY() + r.getHeight() * 0.38f, cx, r.getY() + r.getHeight() * 0.62f, r.getWidth() * 0.1f);
    g.fillEllipse (juce::Rectangle<float> (r.getWidth() * 0.11f, r.getWidth() * 0.11f).withCentre ({ cx, r.getY() + r.getHeight() * 0.75f }));
}

//==============================================================================
LookAndFeel::LookAndFeel()
{
    setColour (juce::PopupMenu::backgroundColourId, C::menu);
    setColour (juce::PopupMenu::textColourId, C::text);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, C::btn2);
    setColour (juce::PopupMenu::highlightedTextColourId, C::text);
    setColour (juce::ComboBox::backgroundColourId, C::btn);
    setColour (juce::ComboBox::textColourId, C::text);
    setColour (juce::ComboBox::outlineColourId, C::menuBorder);
    setColour (juce::ComboBox::arrowColourId, C::text2);
    setColour (juce::TextEditor::backgroundColourId, C::wave);
    setColour (juce::TextEditor::textColourId, C::text);
    setColour (juce::TextEditor::highlightColourId, C::orange.withAlpha (0.35f));
    setColour (juce::TextEditor::outlineColourId, C::orange);
    setColour (juce::TextEditor::focusedOutlineColourId, C::orange);
    setColour (juce::CaretComponent::caretColourId, C::text);
    setColour (juce::Label::textColourId, C::text);
    setColour (juce::ScrollBar::thumbColourId, C::btn2);
    setColour (juce::TooltipWindow::backgroundColourId, C::menu);
    setColour (juce::TooltipWindow::textColourId, C::text);
    setColour (juce::AlertWindow::backgroundColourId, C::menu);
    setColour (juce::AlertWindow::textColourId, C::text);
    setColour (juce::AlertWindow::outlineColourId, C::menuBorder);
    setColour (juce::TextButton::buttonColourId, C::btn);
    setColour (juce::TextButton::textColourOffId, C::text);
    setColour (juce::TextButton::textColourOnId, C::ink);
    setColour (juce::ResizableWindow::backgroundColourId, C::bg);
    setColour (juce::DocumentWindow::textColourId, C::text);
    setColour (juce::Slider::thumbColourId, C::text);
    setColour (juce::Slider::trackColourId, C::text);
    setColour (juce::Slider::backgroundColourId, C::knobTrack);
}

juce::Font LookAndFeel::getPopupMenuFont() { return sans (13.0f); }

void LookAndFeel::drawPopupMenuBackground (juce::Graphics& g, int width, int height)
{
    g.fillAll (C::menu);
    g.setColour (C::menuBorder);
    g.drawRect (0, 0, width, height, 1);
}

void LookAndFeel::getIdealPopupMenuItemSize (const juce::String& text, bool isSeparator, int, int& idealWidth, int& idealHeight)
{
    if (isSeparator) { idealWidth = 50; idealHeight = 9; return; }
    auto f = getPopupMenuFont();
    idealHeight = 30;
    idealWidth = juce::GlyphArrangement::getStringWidthInt (f, text) + 60;
}

void LookAndFeel::drawPopupMenuItem (juce::Graphics& g, const juce::Rectangle<int>& area, bool isSeparator, bool isActive,
                                     bool isHighlighted, bool isTicked, bool hasSubMenu, const juce::String& text,
                                     const juce::String& shortcutKeyText, const juce::Drawable*, const juce::Colour* textColour)
{
    if (isSeparator)
    {
        g.setColour (C::blockBorder);
        g.fillRect (area.reduced (4, 0).withHeight (1).withY (area.getCentreY()));
        return;
    }
    auto r = area.toFloat().reduced (2.0f, 1.0f);
    if (isHighlighted && isActive)
    {
        g.setColour (C::btn2);
        g.fillRoundedRectangle (r, 5.0f);
    }
    else if (isTicked)
    {
        g.setColour (C::hover);
        g.fillRoundedRectangle (r, 5.0f);
    }
    auto col = textColour != nullptr ? *textColour : C::text;
    if (! isActive) col = C::dim;
    g.setColour (col);
    g.setFont (sans (13.0f));
    auto tr = area.reduced (12, 0);
    g.drawText (text, tr, juce::Justification::centredLeft, true);
    if (shortcutKeyText.isNotEmpty())
    {
        g.setColour (C::dim);
        g.setFont (mono (11.0f));
        g.drawText (shortcutKeyText, tr, juce::Justification::centredRight, true);
    }
    if (hasSubMenu)
        drawChevron (g, { (float) area.getRight() - 14.0f, (float) area.getCentreY() }, C::text2, 8.0f);
}

void LookAndFeel::drawPopupMenuSectionHeader (juce::Graphics& g, const juce::Rectangle<int>& area, const juce::String& name)
{
    g.setColour (C::muted);
    g.setFont (caps (11.0f, 400));
    g.drawText (name, area.reduced (12, 0).withTrimmedTop (4), juce::Justification::centredLeft, true);
}

void LookAndFeel::drawComboBox (juce::Graphics& g, int width, int height, bool, int, int, int, int, juce::ComboBox& box)
{
    auto r = juce::Rectangle<float> (0, 0, (float) width, (float) height).reduced (0.5f);
    g.setColour (C::btn);
    g.fillRoundedRectangle (r, 6.0f);
    g.setColour (C::menuBorder);
    g.drawRoundedRectangle (r, 6.0f, 1.0f);
    drawChevron (g, { (float) width - 12.0f, height * 0.5f }, box.isEnabled() ? C::text2 : C::dim, 9.0f);
}

juce::Font LookAndFeel::getComboBoxFont (juce::ComboBox&) { return sans (12.0f); }

void LookAndFeel::positionComboBoxText (juce::ComboBox& box, juce::Label& label)
{
    label.setBounds (4, 0, box.getWidth() - 24, box.getHeight());
    label.setFont (getComboBoxFont (box));
}

void LookAndFeel::fillTextEditorBackground (juce::Graphics& g, int width, int height, juce::TextEditor&)
{
    g.setColour (C::wave);
    g.fillRoundedRectangle (0, 0, (float) width, (float) height, 5.0f);
}

void LookAndFeel::drawTextEditorOutline (juce::Graphics& g, int width, int height, juce::TextEditor& ed)
{
    g.setColour (ed.findColour (ed.hasKeyboardFocus (true) ? juce::TextEditor::focusedOutlineColourId : juce::TextEditor::outlineColourId));
    g.drawRoundedRectangle (0.5f, 0.5f, (float) width - 1, (float) height - 1, 5.0f, 1.0f);
}

void LookAndFeel::drawLinearSlider (juce::Graphics& g, int x, int y, int width, int height, float sliderPos, float, float,
                                    juce::Slider::SliderStyle, juce::Slider&)
{
    auto cy = y + height * 0.5f;
    g.setColour (C::knobTrack);
    g.fillRoundedRectangle ((float) x, cy - 2, (float) width, 4, 2);
    g.setColour (C::text);
    g.fillRoundedRectangle ((float) x, cy - 2, sliderPos - x, 4, 2);
    g.fillEllipse (juce::Rectangle<float> (12, 12).withCentre ({ sliderPos, cy }));
}

void LookAndFeel::drawScrollbar (juce::Graphics& g, juce::ScrollBar&, int x, int y, int width, int height, bool vertical,
                                 int thumbStart, int thumbSize, bool over, bool)
{
    juce::Rectangle<float> r = vertical ? juce::Rectangle<float> ((float) x + 2, (float) thumbStart, (float) width - 4, (float) thumbSize)
                                        : juce::Rectangle<float> ((float) thumbStart, (float) y + 2, (float) thumbSize, (float) height - 4);
    g.setColour (over ? C::hover : C::btn2);
    g.fillRoundedRectangle (r, 3.0f);
}

void LookAndFeel::drawTooltip (juce::Graphics& g, const juce::String& text, int width, int height)
{
    auto r = juce::Rectangle<float> (0, 0, (float) width, (float) height);
    g.setColour (C::menu);
    g.fillRoundedRectangle (r, 6.0f);
    g.setColour (C::menuBorder);
    g.drawRoundedRectangle (r.reduced (0.5f), 6.0f, 1.0f);
    g.setColour (C::text);
    g.setFont (sans (12.0f));
    g.drawFittedText (text, r.reduced (8, 4).toNearestInt(), juce::Justification::centredLeft, 4);
}

juce::Rectangle<int> LookAndFeel::getTooltipBounds (const juce::String& tipText, juce::Point<int> screenPos, juce::Rectangle<int> parentArea)
{
    auto f = sans (12.0f);
    int w = juce::jmin (360, juce::GlyphArrangement::getStringWidthInt (f, tipText) + 18);
    int lines = juce::jmax (1, juce::GlyphArrangement::getStringWidthInt (f, tipText) / 340 + 1);
    int h = 10 + lines * 16;
    return juce::Rectangle<int> (screenPos.x > parentArea.getCentreX() ? screenPos.x - (w + 12) : screenPos.x + 24,
                                 screenPos.y > parentArea.getCentreY() ? screenPos.y - (h + 6) : screenPos.y + 6, w, h)
        .constrainedWithin (parentArea);
}

void LookAndFeel::drawAlertBox (juce::Graphics& g, juce::AlertWindow& alert, const juce::Rectangle<int>& textArea, juce::TextLayout& layout)
{
    auto r = alert.getLocalBounds().toFloat();
    g.setColour (C::menu);
    g.fillRoundedRectangle (r, 10.0f);
    g.setColour (C::menuBorder);
    g.drawRoundedRectangle (r.reduced (0.5f), 10.0f, 1.0f);
    layout.draw (g, textArea.toFloat());
}

juce::Font LookAndFeel::getAlertWindowTitleFont() { return sans (15.0f, 600); }
juce::Font LookAndFeel::getAlertWindowMessageFont() { return sans (13.0f); }
juce::Font LookAndFeel::getAlertWindowFont() { return sans (13.0f); }

void LookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool over, bool down)
{
    auto r = b.getLocalBounds().toFloat().reduced (0.5f);
    g.setColour (down ? C::text : (over ? C::hover : C::btn));
    g.fillRoundedRectangle (r, 6.0f);
}

juce::Font LookAndFeel::getTextButtonFont (juce::TextButton&, int) { return sans (13.0f, 600); }

} // namespace ok::ui
