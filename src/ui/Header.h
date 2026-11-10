#pragma once

#include "Widgets.h"
#include "UiHost.h"

namespace ok::ui
{

class BpmBox : public juce::Component, public juce::SettableTooltipClient
{
public:
    explicit BpmBox (Session& s);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    void resized() override;

private:
    void commit();
    Session& session;
    std::unique_ptr<juce::TextEditor> editor;
    double startBpm = 120.0;
    float wheelAcc = 0;
};

class Header : public juce::Component
{
public:
    Header (Session&, UiHost&);
    void refresh();
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void showPatternMenu();
    void showPartMenu (int index);

    Session& session;
    UiHost& host;

    Btn pattern, undoBtn, redoBtn, queuePill, toastUndo, play, tap, metro, hostBtn, gear;
    Btn parts[kNumParts];
    Seg switchSeg;
    BpmBox bpm;
    Knob master;

    juce::Rectangle<int> logoArea, partLabel, switchLabel, toastArea;
    std::vector<juce::Rectangle<int>> seps;
};

} // namespace ok::ui
