#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace ok::ui
{

namespace C
{
    const juce::Colour bg          { 0xff121315 };
    const juce::Colour header      { 0xff191A1D };
    const juce::Colour border      { 0xff26282D };
    const juce::Colour panel       { 0xff17181B };
    const juce::Colour btn         { 0xff24262B };
    const juce::Colour btn2        { 0xff2A2C31 };
    const juce::Colour hover       { 0xff2E3036 };
    const juce::Colour text        { 0xffECEAE4 };
    const juce::Colour text2       { 0xffC9C7C1 };
    const juce::Colour muted       { 0xffA6A8AE };
    const juce::Colour dim         { 0xff6E717A };
    const juce::Colour orange      { 0xffF2A23A };
    const juce::Colour teal        { 0xff5CC6B8 };
    const juce::Colour red         { 0xffFF7A6B };
    const juce::Colour yellow      { 0xffF5D547 };
    const juce::Colour menu        { 0xff1E2024 };
    const juce::Colour menuBorder  { 0xff3A3D44 };
    const juce::Colour ink         { 0xff141517 };
    const juce::Colour cellA       { 0xff27292E };
    const juce::Colour cellB       { 0xff1F2125 };
    const juce::Colour rowSel      { 0xff1C1E22 };
    const juce::Colour knobTrack   { 0xff2E3036 };
    const juce::Colour wave        { 0xff101113 };
    const juce::Colour block       { 0xff1F2125 };
    const juce::Colour blockBorder { 0xff2C2E33 };
    const juce::Colour sep         { 0xff2E3036 };
}

juce::Font sans (float size, int weight = 400);
juce::Font mono (float size, int weight = 400);
juce::Font caps (float size, int weight = 600);   // tracked small caps labels

void drawKnob (juce::Graphics& g, juce::Rectangle<float> area, float frac, juce::Colour arc,
               bool bipolar = false, float strokeScale = 4.0f, float innerRadius = 11.0f);
void drawChevron (juce::Graphics& g, juce::Point<float> centre, juce::Colour c, float size = 8.0f);
void drawCross (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour c, float thickness = 1.5f);
void drawDots (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour c);
void drawWarning (juce::Graphics& g, juce::Rectangle<float> r);
juce::Colour trackColour (int paletteIndex);

class LookAndFeel : public juce::LookAndFeel_V4
{
public:
    LookAndFeel();

    juce::Font getPopupMenuFont() override;
    void drawPopupMenuBackground (juce::Graphics&, int width, int height) override;
    void drawPopupMenuItem (juce::Graphics&, const juce::Rectangle<int>& area, bool isSeparator, bool isActive,
                            bool isHighlighted, bool isTicked, bool hasSubMenu, const juce::String& text,
                            const juce::String& shortcutKeyText, const juce::Drawable* icon,
                            const juce::Colour* textColour) override;
    void drawPopupMenuSectionHeader (juce::Graphics&, const juce::Rectangle<int>& area, const juce::String& sectionName) override;
    void getIdealPopupMenuItemSize (const juce::String& text, bool isSeparator, int standardMenuItemHeight,
                                    int& idealWidth, int& idealHeight) override;
    int getPopupMenuBorderSize() override { return 6; }

    void drawComboBox (juce::Graphics&, int width, int height, bool isButtonDown, int buttonX, int buttonY,
                       int buttonW, int buttonH, juce::ComboBox&) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override;
    void positionComboBoxText (juce::ComboBox&, juce::Label&) override;

    void fillTextEditorBackground (juce::Graphics&, int width, int height, juce::TextEditor&) override;
    void drawTextEditorOutline (juce::Graphics&, int width, int height, juce::TextEditor&) override;

    void drawLinearSlider (juce::Graphics&, int x, int y, int width, int height, float sliderPos, float minSliderPos,
                           float maxSliderPos, juce::Slider::SliderStyle, juce::Slider&) override;

    void drawScrollbar (juce::Graphics&, juce::ScrollBar&, int x, int y, int width, int height, bool isScrollbarVertical,
                        int thumbStartPosition, int thumbSize, bool isMouseOver, bool isMouseDown) override;
    int getDefaultScrollbarWidth() override { return 8; }

    void drawTooltip (juce::Graphics&, const juce::String& text, int width, int height) override;
    juce::Rectangle<int> getTooltipBounds (const juce::String& tipText, juce::Point<int> screenPos, juce::Rectangle<int> parentArea) override;

    void drawAlertBox (juce::Graphics&, juce::AlertWindow&, const juce::Rectangle<int>& textArea, juce::TextLayout&) override;
    juce::Font getAlertWindowTitleFont() override;
    juce::Font getAlertWindowMessageFont() override;
    juce::Font getAlertWindowFont() override;
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool, bool) override;
    juce::Font getTextButtonFont (juce::TextButton&, int) override;
};

} // namespace ok::ui
