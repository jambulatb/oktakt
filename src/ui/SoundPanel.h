#pragma once

#include "Widgets.h"
#include "UiHost.h"

namespace ok::ui
{

// Small numeric box: drag up/down or scroll; double-click resets or lets you type
class ValueBox : public juce::Component, public juce::SettableTooltipClient
{
public:
    std::function<int()> get;
    std::function<void (int)> set;
    std::function<juce::String (int)> format;
    std::function<void()> onGesture;
    int min = 0, max = 100, def = 0;
    bool typeOnDoubleClick = false;

    ValueBox() { setMouseCursor (juce::MouseCursor::UpDownResizeCursor); setWantsKeyboardFocus (true); }
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    bool keyPressed (const juce::KeyPress&) override;
    void resized() override { if (editor) editor->setBounds (getLocalBounds()); }

private:
    void apply (int v);
    std::unique_ptr<juce::TextEditor> editor;
    int start = 0;
    float wheelAcc = 0;
};

class WaveView : public juce::Component,
                 public juce::DragAndDropTarget,
                 public juce::FileDragAndDropTarget
{
public:
    WaveView (Session& s) : session (s) {}

    bool editing = false;
    int selectedSlice = -1;
    static constexpr int kTicksH = 16;

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;

    bool isInterestedInDragSource (const SourceDetails&) override;
    void itemDragEnter (const SourceDetails&) override { hot = true; repaint(); }
    void itemDragExit (const SourceDetails&) override { hot = false; repaint(); }
    void itemDropped (const SourceDetails&) override;
    bool isInterestedInFileDrag (const juce::StringArray&) override;
    void fileDragEnter (const juce::StringArray&, int, int) override { hot = true; repaint(); }
    void fileDragExit (const juce::StringArray&) override { hot = false; repaint(); }
    void filesDropped (const juce::StringArray&, int, int) override;

private:
    struct Geo
    {
        float w = 0, h = 0, areaW = 0, span = 1, bars = 0, lenS = 0;
        float xOf (double frac) const { return (float) frac * areaW; }
    };
    Geo geometry() const;
    int markerAt (float x) const;
    double fracAt (float x) const;
    int sliceAt (float x) const;       // slice under x in Slices mode, -1 otherwise

    Session& session;
    bool hot = false;
    int dragMarker = -1;
    int pressSlice = -1;              // a slice pressed outside the editor: dragging it takes it to the grid
    bool draggingSlice = false;
};

class SoundPanel : public juce::Component
{
public:
    SoundPanel (Session&, UiHost&);
    ~SoundPanel() override;
    void refresh();
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    struct Block
    {
        juce::String title;
        int cols = 1;
        juce::Rectangle<int> bounds;
        juce::String note;
    };

    void rebuildBlocks();
    juce::String structureKey() const;
    Knob& addKnob (const juce::String& label, int min, int max, int def,
                   std::function<int& (Track&)> ref, std::function<juce::String (int)> fmt = {}, bool bipolar = false);
    Btn& addPill (const juce::String& text, std::function<bool& (Track&)> ref, const juce::String& tip);
    Btn& addMenu (std::function<juce::String()> text, std::function<void (Btn&)> open);
    void setChop (ChopMode m);

    Session& session;
    UiHost& host;

    Btn close, editBtn;
    Seg tabs, chop;
    ValueBox sens, parts;
    WaveView wave;

    juce::Rectangle<int> swatchArea, nameArea, infoArea, chopLabel, sensLabel, partsLabel, hintArea, blocksArea;
    std::vector<Block> blocks;
    std::vector<std::unique_ptr<juce::Component>> blockKids;
    std::vector<Btn*> pills;
    juce::String builtKey;
    uint32_t shownTrack = 0;
};

} // namespace ok::ui
