#pragma once

#include "Theme.h"
#include "../Session.h"

namespace ok::ui
{

// Flat button drawn like the mockup: rounded background, text and/or an icon
class Btn : public juce::Component, public juce::SettableTooltipClient
{
public:
    Btn() { setRepaintsOnMouseActivity (true); }
    explicit Btn (const juce::String& t) : Btn() { text = t; }

    juce::String text;
    juce::Font font = sans (12.0f, 600);
    juce::Colour bg = C::btn, bgHover = C::hover, textCol = C::text;
    juce::Colour onBg = C::text, onText = C::ink;
    juce::Colour outlineCol = juce::Colours::transparentBlack;
    bool toggled = false;
    bool dimmed = false;
    float radius = 6.0f;
    juce::Justification just = juce::Justification::centred;
    int padX = 0;
    bool chevron = false;

    std::function<void()> onClick;
    std::function<void (const juce::MouseEvent&)> onClickEvent;
    std::function<void()> onRightClick;
    std::function<void (juce::Graphics&, juce::Rectangle<float>, juce::Colour)> icon;
    std::function<bool()> isOn;                 // overrides `toggled` when set
    std::function<juce::String()> textFn;       // overrides `text` when set

    int idealWidth (int pad = 10) const;

    void paint (juce::Graphics&) override;
    void mouseUp (const juce::MouseEvent&) override;
};

// A rotary control: drag up/down or scroll, double-click resets. List mode cycles through names.
class Knob : public juce::Component, public juce::SettableTooltipClient
{
public:
    explicit Knob (Session& s) : session (s) {}

    std::function<int()> get;
    std::function<void (int)> set;
    std::function<juce::String (int)> format;
    std::function<juce::Colour()> colour;
    juce::StringArray list;

    juce::String label;
    int min = 0, max = 127, def = 0;
    bool bipolar = false;
    bool active = true;
    bool showText = true;
    int knobPx = 36;
    float labelSize = 11.0f, valueSize = 11.0f;
    float strokeScale = 4.0f, innerRadius = 11.0f;
    juce::Colour labelCol = C::muted;

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

    juce::String valueText() const;

private:
    void apply (int v);
    Session& session;
    float dragStartY = 0;
    int dragStartVal = 0;
    bool dragging = false;
    float wheelAcc = 0;
};

// Segmented control
class Seg : public juce::Component
{
public:
    juce::StringArray items;
    std::function<int()> get;
    std::function<void (int)> set;
    int padX = 10;
    float fontSize = 12.0f;
    float tracking = 0.0f;
    std::vector<bool> enabled;

    int idealWidth() const;
    void paint (juce::Graphics&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override { repaint(); }
    void mouseExit (const juce::MouseEvent&) override { repaint(); }

private:
    std::vector<juce::Rectangle<float>> itemRects() const;
};

// "−  N  +"
class Stepper : public juce::Component, public juce::SettableTooltipClient
{
public:
    std::function<int()> get;
    std::function<void (int)> set;
    std::function<juce::String (int)> format;
    int min = 1, max = 8;
    int btn = 24, valueW = 20;
    float fontSize = 14.0f;
    juce::String decTip, incTip;

    void paint (juce::Graphics&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    int idealWidth() const { return btn * 2 + valueW + 6; }

private:
    float wheelAcc = 0;
};

// Shows a PopupMenu under a component, or under `area` (in the component's coordinates) when given;
// the callback gets the chosen id (0 = dismissed)
void showMenu (juce::PopupMenu& m, juce::Component* target, std::function<void (int)> cb, int minWidth = 0,
               juce::Rectangle<int> area = {});

} // namespace ok::ui
