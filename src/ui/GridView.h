#pragma once

#include "Widgets.h"
#include "UiHost.h"

namespace ok::ui
{

// Track rows, cells and the automation rows of the active track
class GridRows : public juce::Component,
                 public juce::TooltipClient,
                 public juce::DragAndDropTarget,
                 public juce::FileDragAndDropTarget,
                 private juce::Timer
{
public:
    static constexpr int kCellsX = 270;
    static constexpr int kRowH = 28, kGap = 3;

    GridRows (Session&, UiHost&);
    ~GridRows() override;

    void refresh();
    int contentHeight() const;
    juce::String getTooltip() override;
    void startRename (uint32_t id);

    void paint (juce::Graphics&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

    bool isInterestedInDragSource (const SourceDetails&) override;
    void itemDragMove (const SourceDetails&) override;
    void itemDragExit (const SourceDetails&) override;
    void itemDropped (const SourceDetails&) override;
    bool isInterestedInFileDrag (const juce::StringArray&) override;
    void fileDragMove (const juce::StringArray&, int x, int y) override;
    void fileDragExit (const juce::StringArray&) override;
    void filesDropped (const juce::StringArray&, int x, int y) override;

    // x positions of bars for the ruler: [x, width] per bar
    std::vector<juce::Range<float>> barRanges() const;

private:
    struct Item
    {
        enum Kind { Track, Param, AddParam, Empty } kind = Track;
        int track = -1;               // index in doc.tracks
        ParamId param = ParamId::Velocity;
        int y = 0, h = kRowH;
    };

    struct Target
    {
        enum Kind { None, Swatch, Name, Dots, Vol, Pan, Mute, Solo, Cell, ParamRemove, ParamCell, AddParam, Empty } kind = None;
        int item = -1, track = -1, bar = -1, step = -1;
        ParamId param = ParamId::Velocity;
    };

    void timerCallback() override;
    void layout();
    Target hitTest (juce::Point<float> p) const;
    juce::Rectangle<float> cellRect (const Track& t, int bar, int step, int y, int h) const;
    bool cellAt (const Track& t, float x, int& bar, int& step) const;
    void paintTrackRow (juce::Graphics&, const Item&, bool hover);
    void paintParamRow (juce::Graphics&, const Item&);
    void showTrackMenu (int trackIndex, juce::Rectangle<int> anchor);
    void showColours (int trackIndex);
    void showAddParam (juce::Rectangle<int> anchor);
    void setDropFromY (int y, bool isTrackDrag);
    void setSliceDrop (const juce::String& desc, juce::Point<int> p);
    void dropFiles (const juce::StringArray& files);
    void commitRename (bool apply);
    juce::Rectangle<int> rowArea (int item) const;
    juce::Rectangle<int> dotsArea (int item) const;

    Session& session;
    UiHost& host;
    std::vector<Item> items;
    int blockTop = -1, blockBottom = -1;
    Target hover, press;

    enum class Drag { None, Vol, Pan, Param };
    Drag drag = Drag::None;
    float dragStartY = 0;
    int dragStartVal = 0;
    bool movedTrack = false;
    float wheelAcc = 0;

    int dropTrack = -1, dropWhere = 0;   // where: -1 above, 0 on, 1 below, 2 append, 3 on a cell (a slice)
    int dropBar = -1, dropStep = -1;
    bool dropActive = false;

    std::unique_ptr<juce::TextEditor> renameEd;
    uint32_t renamingId = 0;
    std::vector<int> playSteps;
};

class Ruler : public juce::Component
{
public:
    Ruler (Session& s, GridRows& r) : session (s), rows (r) { setRepaintsOnMouseActivity (true); }
    void paint (juce::Graphics&) override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    Session& session;
    GridRows& rows;
};

class SwingBox : public juce::Component, public juce::SettableTooltipClient
{
public:
    explicit SwingBox (Session& s);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

private:
    void set (int v);
    Session& session;
    int start = 50;
    float wheelAcc = 0;
};

class GridSection : public juce::Component
{
public:
    GridSection (Session&, UiHost&);
    void refresh();
    void resized() override;
    void paint (juce::Graphics&) override;
    void startRename (uint32_t id) { rows.startRename (id); }

private:
    Session& session;
    Seg zoom;
    SwingBox swing;
    GridRows rows;
    Ruler ruler { session, rows };
    juce::Viewport viewport;
    juce::Rectangle<int> hintArea, legendArea;
};

} // namespace ok::ui
