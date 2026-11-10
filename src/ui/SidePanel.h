#pragma once

#include "Widgets.h"
#include "UiHost.h"

namespace ok::ui
{

class SidePanel : public juce::Component
{
public:
    SidePanel (Session&, UiHost&);
    void refresh();
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    // Value shared by the STEP targets, or nullopt when they differ
    template <typename Fn> std::optional<int> common (Fn get);
    template <typename Fn> void setAll (Fn apply, const void* gesture = nullptr);

    void showRetrigMenu();

    Session& session;
    UiHost& host;
    Stepper groups, steps, condN, retrigLen;
    Knob random, accuracy, chance, micro, fade;
    Btn retrig, clearMarks;
    juce::ComboBox output;

    juce::Rectangle<int> gridTitle, groupsLabel, stepsLabel, positionTitle, randomTitle, divider, stepTitle, condLabel,
                         condSquares, retrigLabel, retrigLenLabel, outputTitle;
    std::vector<juce::Rectangle<int>> squares;
    bool clearCompact = false;   // Clear marks shrunk to a cross in the STEP title when there is no room below
};

} // namespace ok::ui
