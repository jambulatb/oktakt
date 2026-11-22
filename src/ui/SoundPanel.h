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
                 public juce::SettableTooltipClient,
                 public juce::DragAndDropTarget,
                 public juce::FileDragAndDropTarget
{
public:
    WaveView (Session& s) : session (s) {}

    bool editing = false;
    int selectedSlice = -1;
    static constexpr int kTicksH = 16;
    static constexpr int kOverviewH = 8;   // under the ruler when zoomed in: which part of the sample is shown

    // Zoom works in both modes; the view stays when the slice editor opens or closes
    std::function<void()> onZoomChanged;             // zoomed in or back to the whole sample
    void resetZoom();
    void zoomToBar();                                // the bar under the middle of the view
    void zoomAround (float x, double factor);        // factor > 1 zooms in; the point under x stays put
    bool isZoomed() const { return viewLen < 0.9999; }
    double visibleStart() const { return viewStart; }   // fractions of the timeline under the ruler
    double visibleLength() const { return viewLen; }
    float xOfSampleFrac (double frac) const { return geometry().xOf (frac); }

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    void mouseMagnify (const juce::MouseEvent&, float scaleFactor) override;

    bool isInterestedInDragSource (const SourceDetails&) override;
    void itemDragEnter (const SourceDetails&) override { hot = true; repaint(); }
    void itemDragExit (const SourceDetails&) override { hot = false; repaint(); }
    void itemDropped (const SourceDetails&) override;
    bool isInterestedInFileDrag (const juce::StringArray&) override;
    void fileDragEnter (const juce::StringArray&, int, int) override { hot = true; repaint(); }
    void fileDragExit (const juce::StringArray&) override { hot = false; repaint(); }
    void filesDropped (const juce::StringArray&, int, int) override;

private:
    // The timeline under the ruler is `span` bars long (at least one); the sample takes `bars` of it.
    // v0 / vl: the visible part of the timeline, fractions of span.
    struct Geo
    {
        float w = 0, h = 0, span = 1, bars = 0, lenS = 0;
        double v0 = 0.0, vl = 1.0;
        double tOf (double frac) const { return span > 0 ? frac * bars / span : 0.0; }       // sample -> timeline
        double fracOfT (double t) const { return bars > 0 ? t * span / bars : 0.0; }         // timeline -> sample
        float xOfT (double t) const { return (float) ((t - v0) / vl * w); }
        float xOf (double frac) const { return xOfT (tOf (frac)); }
        double tAt (float x) const { return w > 0 ? v0 + x / w * vl : 0.0; }
        double fracAtUnclamped (float x) const { return fracOfT (tAt (x)); }
    };
    Geo geometry() const;
    int markerAt (float x) const;
    double fracAt (float x) const;
    int sliceAt (float x) const;       // slice under x in Slices mode, -1 otherwise
    double minViewLength() const;      // the deepest zoom: half a sample per pixel
    void clampView();
    bool overOverview (juce::Point<float> p) const;
    void setView (double start, double length);

    Session& session;
    bool hot = false;
    int dragMarker = -1;
    int pressSlice = -1;              // a slice pressed outside the editor: dragging it takes it to the grid
    bool draggingSlice = false;
    double viewStart = 0.0, viewLen = 1.0;
    enum class ViewDrag { none, pan, thumb } viewDrag = ViewDrag::none;
    double viewDragStart = 0.0;
    bool hoverOverview = false;
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
    void updateZoomButtons();

    Session& session;
    UiHost& host;

    Btn close, editBtn, zoomOut, zoomIn, zoomBar, zoomAll;
    Seg tabs, chop;
    ValueBox sens, parts;
    WaveView wave;

    juce::Rectangle<int> swatchArea, nameArea, infoArea, chopLabel, sensLabel, partsLabel, hintArea, blocksArea;
    std::vector<Block> blocks;
    std::vector<std::unique_ptr<juce::Component>> blockKids;
    std::vector<Btn*> pills;
    juce::String builtKey;
    uint32_t shownTrack = 0, zoomTrack = 0;
    juce::String shownSample;
};

} // namespace ok::ui
