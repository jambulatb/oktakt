#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "../PluginProcessor.h"
#include "UiHost.h"
#include "Theme.h"

namespace ok::ui
{

class Header;
class Browser;
class GridSection;
class SidePanel;
class SoundPanel;
class Settings;

class Editor : public juce::AudioProcessorEditor,
               public juce::DragAndDropContainer,
               public UiHost,
               private juce::ChangeListener
{
public:
    static constexpr int kWidth = 1440, kHeight = 1008;

    explicit Editor (OktaktProcessor&);
    ~Editor() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;

    // UiHost
    void showPopover (std::unique_ptr<juce::Component> content, juce::Rectangle<int> anchorInRoot, bool alignRight) override;
    void closePopover() override;
    juce::Component& rootComponent() override { return root; }
    void toggleSettings() override;
    void promptSaveAs() override;
    void startRename (uint32_t trackId) override;
    void confirmDiscard (std::function<void()> then) override { confirmClose (std::move (then)); }

    // Closing the standalone window: asks to save unsaved changes first; `close` runs unless the user cancels
    void confirmClose (std::function<void()> close);
    bool isConfirmingClose() const { return confirmWindow != nullptr; }

    void refresh();

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void layoutRoot();

    OktaktProcessor& proc;
    Session& session;
    LookAndFeel lnf;
    juce::TooltipWindow tooltips { this, 600 };

    struct Root : juce::Component
    {
        void paint (juce::Graphics& g) override { g.fillAll (C::bg); }
    } root;

    std::unique_ptr<Header> header;
    std::unique_ptr<Browser> browser;
    std::unique_ptr<GridSection> grid;
    std::unique_ptr<SidePanel> side;
    std::unique_ptr<SoundPanel> sound;

    struct Dismisser;
    std::unique_ptr<Dismisser> dismisser;
    std::unique_ptr<juce::Component> popover;
    bool settingsOpen = false;
    std::unique_ptr<juce::AlertWindow> saveAsWindow, confirmWindow;
    std::function<void()> afterSaveAs;   // runs once Save as… has saved (closing after "Save")
};

} // namespace ok::ui
