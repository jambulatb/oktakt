#pragma once

#include "Widgets.h"
#include "UiHost.h"

namespace ok::ui
{

class Browser : public juce::Component
{
public:
    Browser (Session&, UiHost&);
    void refresh();
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    struct Row
    {
        juce::File file;
        bool isDir = false;
        int depth = 0;
    };

    struct List : juce::Component
    {
        explicit List (Browser& b) : owner (b) { setRepaintsOnMouseActivity (true); }
        void paint (juce::Graphics&) override;
        void mouseUp (const juce::MouseEvent&) override;
        void mouseDrag (const juce::MouseEvent&) override;
        void mouseMove (const juce::MouseEvent&) override { repaint(); }
        Browser& owner;
        bool dragStarted = false;
    };

    void rebuild();
    void chooseRoot();
    const std::vector<juce::File>& childrenOf (const juce::File& dir);
    void walk (const juce::File& dir, int depth);

    Session& session;
    UiHost& host;
    Btn source;
    juce::TextEditor search;
    List list { *this };
    juce::Viewport viewport;
    std::vector<Row> rows;
    std::set<juce::String> openDirs;
    std::map<juce::String, std::vector<juce::File>> cache;
    std::unique_ptr<juce::FileChooser> chooser;
    juce::File shownRoot;
    static constexpr int kRowH = 28;
};

} // namespace ok::ui
