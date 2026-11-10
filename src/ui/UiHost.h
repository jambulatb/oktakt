#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <cstdint>
#include <functional>

namespace ok::ui
{

// What panels may ask of the window that holds them
struct UiHost
{
    virtual ~UiHost() = default;

    // Shows a floating panel next to an area given in root coordinates; a click outside closes it
    virtual void showPopover (std::unique_ptr<juce::Component> content, juce::Rectangle<int> anchorInRoot, bool alignRight = false) = 0;
    virtual void closePopover() = 0;
    virtual juce::Component& rootComponent() = 0;

    virtual void toggleSettings() = 0;
    virtual void promptSaveAs() = 0;
    virtual void startRename (uint32_t trackId) = 0;
    // Asks to save unsaved changes first (Save / Don't save / Cancel); `then` runs unless the user cancels
    virtual void confirmDiscard (std::function<void()> then) = 0;
};

} // namespace ok::ui
