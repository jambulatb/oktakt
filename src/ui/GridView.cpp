#include "GridView.h"

namespace ok::ui
{

static float velOpacity (int v) { return 0.3f + 0.7f * (float) juce::jlimit (0, 127, v) / 127.0f; }

static juce::String signedText (int v) { return v > 0 ? "+" + juce::String (v) : (v < 0 ? juce::String::fromUTF8 ("\xe2\x88\x92") + juce::String (-v) : juce::String ("0")); }

static juce::String fmtParam (ParamId id, int v)
{
    return paramInfo (id).min < 0 ? signedText (v) : juce::String (v);
}

//==============================================================================
class ColourPicker : public juce::Component
{
public:
    ColourPicker (Session& s, uint32_t id, std::function<void()> done) : session (s), trackId (id), onDone (std::move (done))
    {
        setSize (8 + 6 * 22 + 5 * 6 + 8, 8 + 2 * 22 + 6 + 8);
        setRepaintsOnMouseActivity (true);
    }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        g.setColour (C::menu);
        g.fillRoundedRectangle (r, 8.0f);
        g.setColour (C::menuBorder);
        g.drawRoundedRectangle (r.reduced (0.5f), 8.0f, 1.0f);
        auto* t = session.doc.findTrack (trackId);
        for (int i = 0; i < kPaletteSize; ++i)
        {
            auto b = swatch (i);
            if (t != nullptr && t->colour == i)
            {
                g.setColour (C::text);
                g.drawRoundedRectangle (b.expanded (3.0f), 7.0f, 2.0f);
            }
            g.setColour (trackColour (i));
            g.fillRoundedRectangle (b, 5.0f);
        }
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        for (int i = 0; i < kPaletteSize; ++i)
            if (swatch (i).contains (e.position))
            {
                session.setColour (trackId, i);
                if (onDone) onDone();
                return;
            }
    }

private:
    juce::Rectangle<float> swatch (int i) const
    {
        return { 8.0f + (i % 6) * 28.0f, 8.0f + (i / 6) * 28.0f, 22.0f, 22.0f };
    }
    Session& session;
    uint32_t trackId;
    std::function<void()> onDone;
};

//==============================================================================
GridRows::GridRows (Session& s, UiHost& h) : session (s), host (h)
{
    setWantsKeyboardFocus (false);
    startTimerHz (30);
}

GridRows::~GridRows() = default;

void GridRows::refresh()
{
    layout();
    repaint();
}

void GridRows::layout()
{
    items.clear();
    blockTop = blockBottom = -1;
    int y = 0;
    const auto& tracks = session.doc.tracks;
    for (int i = 0; i < (int) tracks.size(); ++i)
    {
        items.push_back ({ Item::Track, i, ParamId::Velocity, y, kRowH });
        y += kRowH + kGap;
        if (session.trackActive() && tracks[(size_t) i].id == session.selected)
        {
            blockTop = y;
            y += 3;
            items.push_back ({ Item::Param, i, ParamId::Velocity, y, kRowH });
            y += kRowH + kGap;
            for (auto p : tracks[(size_t) i].rows)
            {
                items.push_back ({ Item::Param, i, p, y, kRowH });
                y += kRowH + kGap;
            }
            items.push_back ({ Item::AddParam, i, ParamId::Velocity, y, 22 });
            y += 22 + 5;
            blockBottom = y;
            y += kGap;
        }
    }
    items.push_back ({ Item::Empty, -1, ParamId::Velocity, y, tracks.empty() ? 220 : 48 });
}

int GridRows::contentHeight() const
{
    return items.empty() ? 0 : items.back().y + items.back().h + 8;
}

std::vector<juce::Range<float>> GridRows::barRanges() const
{
    const int B = session.doc.parts[(size_t) session.part].bars;
    const float cellsW = (float) getWidth() - 4.0f - kCellsX;
    const float barW = (cellsW - 8.0f * (B - 1)) / (float) B;
    std::vector<juce::Range<float>> out;
    for (int b = 0; b < B; ++b)
        out.push_back (juce::Range<float>::withStartAndLength (kCellsX + b * (barW + 8.0f), barW));
    return out;
}

juce::Rectangle<float> GridRows::cellRect (const Track& t, int bar, int step, int y, int h) const
{
    auto bars = barRanges();
    const auto& br = bars[(size_t) juce::jlimit (0, (int) bars.size() - 1, bar)];
    const int G = t.groups, N = t.steps;
    const float groupW = (br.getLength() - 4.0f * (G - 1)) / (float) G;
    const float cellW = juce::jmax (1.0f, (groupW - 2.0f * (N - 1)) / (float) N);
    const int g = step / N, n = step % N;
    return { br.getStart() + g * (groupW + 4.0f) + n * (cellW + 2.0f), (float) y, cellW, (float) h };
}

bool GridRows::cellAt (const Track& t, float x, int& bar, int& step) const
{
    auto bars = barRanges();
    for (int b = 0; b < (int) bars.size(); ++b)
    {
        if (x < bars[(size_t) b].getStart() || x >= bars[(size_t) b].getEnd()) continue;
        const int G = t.groups, N = t.steps;
        const float groupW = (bars[(size_t) b].getLength() - 4.0f * (G - 1)) / (float) G;
        const float cellW = juce::jmax (1.0f, (groupW - 2.0f * (N - 1)) / (float) N);
        float rx = x - bars[(size_t) b].getStart();
        int g = juce::jlimit (0, G - 1, (int) (rx / (groupW + 4.0f)));
        float gx = rx - g * (groupW + 4.0f);
        int n = juce::jlimit (0, N - 1, (int) (gx / (cellW + 2.0f)));
        bar = b;
        step = g * N + n;
        return true;
    }
    return false;
}

juce::Rectangle<int> GridRows::rowArea (int item) const
{
    auto& it = items[(size_t) item];
    return { 0, it.y, getWidth(), it.h };
}

GridRows::Target GridRows::hitTest (juce::Point<float> p) const
{
    Target t;
    for (int i = 0; i < (int) items.size(); ++i)
    {
        auto& it = items[(size_t) i];
        if (p.y < it.y || p.y >= it.y + it.h) continue;
        t.item = i;
        t.track = it.track;
        const float x = p.x;
        if (it.kind == Item::Empty) { t.kind = Target::Empty; return t; }
        if (it.kind == Item::AddParam)
        {
            int w = juce::GlyphArrangement::getStringWidthInt (sans (12.0f, 600), "+ Parameter") + 20;
            if (x >= 30 && x < 30 + w) t.kind = Target::AddParam;
            return t;
        }
        const auto& tr = session.doc.tracks[(size_t) it.track];
        if (it.kind == Item::Param)
        {
            t.param = it.param;
            if (x >= 2 && x < 26 && it.param != ParamId::Velocity) { t.kind = Target::ParamRemove; return t; }
            int bar, step;
            if (x >= kCellsX && cellAt (tr, x, bar, step))
            {
                if (bar < session.curPart().bars && session.hitAt (tr.id, keyOf (bar, step)) != nullptr)
                {
                    t.kind = Target::ParamCell;
                    t.bar = bar;
                    t.step = step;
                }
            }
            return t;
        }
        // Track row
        if (x < 18) t.kind = Target::Swatch;
        else if (x < 94) t.kind = Target::Name;
        else if (x < 120) t.kind = Target::Dots;
        else if (x >= 126 && x < 154) t.kind = Target::Vol;
        else if (x >= 162 && x < 190) t.kind = Target::Pan;
        else if (x >= 198 && x < 226) t.kind = Target::Mute;
        else if (x >= 234 && x < 262) t.kind = Target::Solo;
        else if (x >= kCellsX)
        {
            int bar, step;
            if (cellAt (tr, x, bar, step)) { t.kind = Target::Cell; t.bar = bar; t.step = step; }
        }
        return t;
    }
    return t;
}

juce::String GridRows::getTooltip()
{
    auto t = hitTest (getMouseXYRelative().toFloat());
    if (t.track < 0 || t.track >= (int) session.doc.tracks.size()) return {};
    const auto& tr = session.doc.tracks[(size_t) t.track];
    auto panText = [] (int p) { return p == 0 ? juce::String ("C") : (p < 0 ? "L" + juce::String (-p) : "R" + juce::String (p)); };
    switch (t.kind)
    {
        case Target::Name:
            if (tr.missing) return "File not found: " + sampleFileName (tr.samplePath);
            return tr.name + juce::String::fromUTF8 (" \xc2\xb7 click to open, drag to move. Rename in the \xc2\xb7\xc2\xb7\xc2\xb7 menu");
        case Target::Swatch: return juce::String::fromUTF8 ("Track colour \xc2\xb7 drag to move the track");
        case Target::Dots:   return "Track actions: rename, colour, duplicate and more";
        case Target::Vol:    return "Volume " + juce::String (tr.volume) + ". Drag up or down or scroll, double-click resets to 100";
        case Target::Pan:    return "Pan " + panText (tr.pan) + ". Drag up or down or scroll, double-click centres";
        case Target::Mute:   return "Mute";
        case Target::Solo:   return "Solo";
        case Target::Cell:
        {
            int n = session.hitAt (tr.id, keyOf (t.bar, t.step)) != nullptr ? session.sliceNumberOf (tr, keyOf (t.bar, t.step)) : 0;
            if (n > 0) return "Plays slice " + juce::String (n) + juce::String::fromUTF8 (" \xc2\xb7 scroll to pick another slice");
            return {};
        }
        case Target::ParamRemove: return "Remove this row";
        case Target::ParamCell:
            if (t.param == ParamId::Random || t.param == ParamId::Accuracy)
                if (auto* h = session.hitAt (tr.id, keyOf (t.bar, t.step)); h != nullptr && h->pinned)
                    return juce::String (paramInfo (t.param).label) + " does not act on a pinned hit. Alt+click the hit to unpin";
            return {};
        default: return {};
    }
}

//==============================================================================
void GridRows::timerCallback()
{
    std::vector<int> steps;
    if (session.isPlaying() && session.engine.currentPart.load() == session.part)
    {
        const double pos = session.engine.posInPart.load();
        for (auto& t : session.doc.tracks)
        {
            int bar = (int) (pos / 4.0);
            int st = (int) (std::fmod (pos, 4.0) / 4.0 * t.stepsPerBar());
            steps.push_back (bar * kKeyStride + st);
        }
    }
    if (steps != playSteps)
    {
        playSteps = steps;
        repaint();
    }
}

void GridRows::paint (juce::Graphics& g)
{
    const auto& tracks = session.doc.tracks;
    if (blockTop >= 0)
    {
        g.setColour (C::rowSel);
        g.fillRoundedRectangle (juce::Rectangle<float> (0, (float) blockTop, (float) getWidth(), (float) (blockBottom - blockTop)), 6.0f);
    }

    for (int i = 0; i < (int) items.size(); ++i)
    {
        auto& it = items[(size_t) i];
        if (it.y > g.getClipBounds().getBottom() || it.y + it.h < g.getClipBounds().getY()) continue;
        switch (it.kind)
        {
            case Item::Track: paintTrackRow (g, it, hover.item == i); break;
            case Item::Param: paintParamRow (g, it); break;
            case Item::AddParam:
            {
                int w = juce::GlyphArrangement::getStringWidthInt (sans (12.0f, 600), "+ Parameter") + 20;
                auto r = juce::Rectangle<float> (30, (float) it.y, (float) w, 22);
                g.setColour (hover.kind == Target::AddParam ? C::hover : C::btn2);
                g.fillRoundedRectangle (r, 4.0f);
                g.setColour (C::text);
                g.setFont (sans (12.0f, 600));
                g.drawText ("+ Parameter", r, juce::Justification::centred, false);
                break;
            }
            case Item::Empty:
            {
                if (tracks.empty())
                {
                    auto r = juce::Rectangle<float> (4, (float) it.y, (float) getWidth() - 8, (float) it.h);
                    bool hot = dropActive && dropWhere == 2;
                    juce::Path p;
                    p.addRoundedRectangle (r.reduced (0.5f), 10.0f);
                    juce::Path dashed;
                    const float dashes[] = { 4.0f, 4.0f };
                    juce::PathStrokeType (1.0f).createDashedStroke (dashed, p, dashes, 2);
                    g.setColour (hot ? C::orange : C::menuBorder);
                    g.fillPath (dashed);
                    g.setColour (hot ? C::orange : C::muted);
                    g.setFont (sans (13.0f));
                    g.drawText (juce::String::fromUTF8 ("Drop samples here to add tracks \xc2\xb7 up to 16"), r, juce::Justification::centred, false);
                }
                else if (dropActive && dropWhere == 2)
                {
                    g.setColour (C::orange);
                    g.fillRect (0, it.y - 2, getWidth(), 3);
                }
                break;
            }
        }
    }
}

void GridRows::paintTrackRow (juce::Graphics& g, const Item& it, bool hoverRow)
{
    const auto& tracks = session.doc.tracks;
    const auto& t = tracks[(size_t) it.track];
    const bool sel = session.selected == t.id && session.trackActive();
    const auto col = trackColour (t.colour);
    bool anySolo = false;
    for (auto& x : tracks) anySolo = anySolo || x.solo;
    const bool muted = t.mute || (anySolo && ! t.solo);
    const float y = (float) it.y;
    const auto& part = session.curPart();
    const int B = part.bars;

    if (sel)
    {
        g.setColour (C::rowSel);
        g.fillRoundedRectangle (juce::Rectangle<float> (0, y, (float) getWidth(), (float) kRowH), 6.0f);
    }

    // swatch
    g.setColour (col);
    g.fillRoundedRectangle (4, y + 4, 10, 20, 3);

    // name
    const bool showDots = hoverRow || sel;
    if (renamingId != t.id)
    {
        auto nameArea = juce::Rectangle<float> (22, y, showDots ? 72.0f : 96.0f, (float) kRowH);
        auto font = sans (13.0f, 500);
        g.setFont (font);
        g.setColour (C::text);
        float tw = (float) juce::GlyphArrangement::getStringWidthInt (font, t.name);
        if (t.missing)
        {
            float avail = nameArea.getWidth() - 19.0f;
            float w = juce::jmin (tw, avail);
            g.drawText (t.name, nameArea.withWidth (w + 1), juce::Justification::centredLeft, true);
            drawWarning (g, { nameArea.getX() + w + 6, y + 7.5f, 13, 13 });
        }
        else
            g.drawText (t.name, nameArea, juce::Justification::centredLeft, true);
    }
    if (showDots)
    {
        auto r = juce::Rectangle<float> (98, y + 4, 20, 20);
        g.setColour (hover.kind == Target::Dots && hover.track == it.track ? C::hover : C::btn);
        g.fillRoundedRectangle (r, 4.0f);
        drawDots (g, r, C::text2);
    }

    // volume and pan
    auto knobCol = muted ? C::dim : col;
    drawKnob (g, { 127, y + 1, 26, 26 }, t.volume / 127.0f, knobCol, false, 5.0f, 10.0f);
    drawKnob (g, { 163, y + 1, 26, 26 }, (t.pan + 64) / 127.0f, knobCol, true, 5.0f, 10.0f);

    // mute / solo
    auto ms = [&] (float x, const char* txt, bool on, juce::Colour onCol)
    {
        juce::Rectangle<float> r (x, y, 28, 28);
        g.setColour (on ? onCol : C::btn);
        g.fillRoundedRectangle (r, 4.0f);
        g.setColour (on ? C::ink : C::muted);
        g.setFont (sans (11.0f, 700));
        g.drawText (txt, r, juce::Justification::centred, false);
    };
    ms (198, "M", t.mute, C::red);
    ms (234, "S", t.solo, C::yellow);

    // cells
    const TrackPart* tp = nullptr;
    if (auto f = part.tracks.find (t.id); f != part.tracks.end()) tp = &f->second;
    const bool slices = t.sound.play == PlayMode::Slices && ! t.sound.slices.empty();
    int playKey = -1;
    if (! playSteps.empty() && it.track < (int) playSteps.size()) playKey = playSteps[(size_t) it.track];

    for (int b = 0; b < B; ++b)
    {
        for (int st = 0; st < t.stepsPerBar(); ++st)
        {
            const int key = keyOf (b, st);
            auto r = cellRect (t, b, st, (int) y, kRowH);
            const float rad = juce::jmin (5.0f, r.getWidth() * 0.3f);
            const Hit* h = nullptr;
            if (tp != nullptr)
                if (auto f = tp->hits.find (key); f != tp->hits.end()) h = &f->second;
            const int grp = st / t.steps;

            if (h != nullptr)
            {
                const bool marked = h->marked;
                float a = muted ? 0.3f : velOpacity (h->velocity);
                if (marked) a = 1.0f;
                g.setColour (col.withAlpha (a));
                g.fillRoundedRectangle (r, rad);
                if (slices && r.getWidth() >= 11.0f)
                {
                    g.setColour (C::ink);
                    g.setFont (mono (10.0f, 600));
                    g.drawText (juce::String (session.sliceNumberOf (t, key)), r, juce::Justification::centred, false);
                }
                if (marked)
                {
                    g.setColour (C::ink);
                    g.fillEllipse (r.getRight() - 8.0f, r.getY() + 3.0f, 5.0f, 5.0f);
                }
                if (h->pinned)
                {
                    juce::Path tri;
                    tri.addTriangle (r.getX(), r.getBottom() - 8.0f, r.getX(), r.getBottom(), r.getX() + 8.0f, r.getBottom());
                    g.setColour (C::ink);
                    g.fillPath (tri);
                }
            }
            else
            {
                g.setColour (grp % 2 ? C::cellB : C::cellA);
                g.fillRoundedRectangle (r, rad);
            }
            if (key == playKey)
            {
                g.setColour (C::text.withAlpha (h != nullptr ? 0.9f : 0.35f));
                g.drawRoundedRectangle (r.reduced (0.75f), rad, 1.5f);
            }
            if (hover.kind == Target::Cell && hover.track == it.track && hover.bar == b && hover.step == st && h == nullptr)
            {
                g.setColour (C::text.withAlpha (0.08f));
                g.fillRoundedRectangle (r, rad);
            }
        }
    }

    // drop indicator
    if (dropActive && dropTrack == it.track)
    {
        g.setColour (C::orange);
        if (dropWhere == 3)
        {
            auto r = cellRect (t, dropBar, dropStep, (int) y, kRowH);
            g.drawRoundedRectangle (r.expanded (1.5f), 5.0f, 2.0f);
        }
        else if (dropWhere == 0) g.drawRoundedRectangle (juce::Rectangle<float> (1, y + 1, (float) getWidth() - 2, kRowH - 2.0f), 6.0f, 2.0f);
        else if (dropWhere < 0) g.fillRect (0.0f, y - 3.0f, (float) getWidth(), 3.0f);
        else g.fillRect (0.0f, y + kRowH, (float) getWidth(), 3.0f);
    }
}

void GridRows::paintParamRow (juce::Graphics& g, const Item& it)
{
    const auto& t = session.doc.tracks[(size_t) it.track];
    const float y = (float) it.y;
    const bool isVel = it.param == ParamId::Velocity;
    const auto& info = paramInfo (it.param);

    if (! isVel)
        drawCross (g, juce::Rectangle<float> (10, y + it.h * 0.5f - 4, 8, 8), C::muted, 1.5f);
    g.setColour (C::text);
    g.setFont (sans (12.0f));
    g.drawText (info.label, juce::Rectangle<float> (30, y, 220, (float) it.h), juce::Justification::centredLeft, true);

    const auto& part = session.curPart();
    auto f = part.tracks.find (t.id);
    if (f == part.tracks.end()) return;
    for (auto& [key, h] : f->second.hits)
    {
        int b = barOfKey (key), st = stepOfKey (key);
        if (b >= part.bars || st >= t.stepsPerBar()) continue;
        auto r = cellRect (t, b, st, (int) y, it.h);
        const float rad = juce::jmin (4.0f, r.getWidth() * 0.3f);
        // Random and Accuracy leave pinned hits alone: their values stay editable but greyed out
        const bool inactive = h.pinned && (it.param == ParamId::Random || it.param == ParamId::Accuracy);
        g.setColour (inactive ? C::cellB : C::btn2);
        g.fillRoundedRectangle (r, rad);
        const int v = effectiveValue (t, h, it.param);
        const bool set = isVel || h.has (it.param);
        g.setColour (inactive ? C::dim : set ? C::text : C::muted);
        auto text = fmtParam (it.param, v);
        float fs = r.getWidth() < 26 ? 10.0f : 12.0f;
        if (juce::GlyphArrangement::getStringWidthInt (mono (fs), text) > r.getWidth() - 2) fs = 9.0f;
        g.setFont (mono (fs));
        g.drawText (text, r, juce::Justification::centred, false);
    }
}

//==============================================================================
void GridRows::mouseMove (const juce::MouseEvent& e)
{
    auto t = hitTest (e.position);
    bool changedHover = t.item != hover.item || t.kind != hover.kind || t.bar != hover.bar || t.step != hover.step;
    hover = t;
    auto cursor = juce::MouseCursor::NormalCursor;
    if (t.kind == Target::Vol || t.kind == Target::Pan || t.kind == Target::ParamCell)
        cursor = juce::MouseCursor::UpDownResizeCursor;
    else if (t.kind == Target::Name || t.kind == Target::Swatch)
        cursor = juce::MouseCursor::PointingHandCursor;
    setMouseCursor (cursor);
    if (changedHover) repaint();
}

void GridRows::mouseExit (const juce::MouseEvent&)
{
    hover = {};
    repaint();
}

void GridRows::mouseDown (const juce::MouseEvent& e)
{
    press = hitTest (e.position);
    drag = Drag::None;
    movedTrack = false;
    if (press.track < 0 || press.track >= (int) session.doc.tracks.size()) return;
    auto& t = session.doc.tracks[(size_t) press.track];

    if (e.mods.isPopupMenu())
    {
        if (items[(size_t) press.item].kind == Item::Track)
            showTrackMenu (press.track, juce::Rectangle<int> (e.getPosition().x, e.getPosition().y, 1, 1));
        press = {};
        return;
    }

    switch (press.kind)
    {
        case Target::Vol:
            session.checkpoint();
            drag = Drag::Vol; dragStartY = e.position.y; dragStartVal = t.volume;
            break;
        case Target::Pan:
            session.checkpoint();
            drag = Drag::Pan; dragStartY = e.position.y; dragStartVal = t.pan;
            break;
        case Target::Cell:
        {
            int key = keyOf (press.bar, press.step);
            if (press.bar >= session.curPart().bars) break;
            if (e.mods.isShiftDown()) session.toggleMark (t.id, key);
            else if (e.mods.isAltDown()) session.togglePin (t.id, key);
            else session.toggleHit (t.id, press.bar, press.step);
            break;
        }
        case Target::ParamCell:
        {
            auto* h = session.hitAt (t.id, keyOf (press.bar, press.step));
            if (h == nullptr) break;
            session.checkpoint();
            drag = Drag::Param;
            dragStartY = e.position.y;
            dragStartVal = effectiveValue (t, *h, press.param);
            break;
        }
        default: break;
    }
}

void GridRows::mouseDrag (const juce::MouseEvent& e)
{
    if (press.track < 0 || press.track >= (int) session.doc.tracks.size()) return;
    auto& t = session.doc.tracks[(size_t) press.track];

    if ((press.kind == Target::Name || press.kind == Target::Swatch) && ! movedTrack && e.getDistanceFromDragStart() > 5)
    {
        movedTrack = true;
        if (auto* dc = juce::DragAndDropContainer::findParentDragContainerFor (this))
        {
            auto font = sans (13.0f, 500);
            int w = juce::GlyphArrangement::getStringWidthInt (font, t.name) + 34;
            juce::Image img (juce::Image::ARGB, w, 28, true);
            {
                juce::Graphics g (img);
                g.setColour (C::rowSel.withAlpha (0.95f));
                g.fillRoundedRectangle (0, 0, (float) w, 28, 6);
                g.setColour (trackColour (t.colour));
                g.fillRoundedRectangle (6, 4, 10, 20, 3);
                g.setColour (C::text);
                g.setFont (font);
                g.drawText (t.name, 24, 0, w - 26, 28, juce::Justification::centredLeft);
            }
            dc->startDragging ("track:" + juce::String ((int) t.id), this, juce::ScaledImage (img), false, nullptr, &e.source);
        }
        return;
    }

    const float dy = dragStartY - e.position.y;
    switch (drag)
    {
        case Drag::Vol:
            t.volume = juce::jlimit (0, 127, dragStartVal + (int) std::lround (dy * (e.mods.isShiftDown() ? 0.25f : 1.0f)));
            session.changed();
            break;
        case Drag::Pan:
            t.pan = juce::jlimit (-64, 63, dragStartVal + (int) std::lround (dy * (e.mods.isShiftDown() ? 0.25f : 1.0f)));
            session.changed();
            break;
        case Drag::Param:
        {
            int key = keyOf (press.bar, press.step);
            auto* h = session.hitAt (t.id, key);
            if (h == nullptr) break;
            const auto& info = paramInfo (press.param);
            float sens = (info.max - info.min) > 60 ? 1.0f : 0.3f;
            if (e.mods.isShiftDown()) sens *= 0.25f;
            int v = dragStartVal + (int) std::lround (dy * sens);
            setHitParam (t, *h, press.param, v);
            session.changed();
            break;
        }
        case Drag::None: break;
    }
}

void GridRows::mouseUp (const juce::MouseEvent& e)
{
    const auto p = press;
    press = {};
    const bool wasDrag = drag != Drag::None;
    drag = Drag::None;
    if (movedTrack || wasDrag || e.mods.isPopupMenu()) return;
    if (p.track < 0 || p.track >= (int) session.doc.tracks.size()) return;
    if (hitTest (e.position).kind != p.kind) return;
    auto& t = session.doc.tracks[(size_t) p.track];

    switch (p.kind)
    {
        case Target::Name:
            if (e.getNumberOfClicks() == 1) session.selectTrack (t.id);
            break;
        case Target::Swatch: showColours (p.track); break;
        case Target::Dots: showTrackMenu (p.track, dotsArea (p.item)); break;
        case Target::Mute: session.checkpoint(); t.mute = ! t.mute; session.changed(); break;
        case Target::Solo: session.checkpoint(); t.solo = ! t.solo; session.changed(); break;
        case Target::ParamRemove:
        {
            session.checkpoint();
            auto id = p.param;
            t.rows.erase (std::remove (t.rows.begin(), t.rows.end(), id), t.rows.end());
            for (auto& part : session.doc.parts)
                if (auto f = part.tracks.find (t.id); f != part.tracks.end())
                    for (auto& [k, h] : f->second.hits)
                        h.p[(size_t) id] = Hit::kUnset;
            session.changed();
            break;
        }
        case Target::AddParam:
            showAddParam (rowArea (p.item).withX (30).withWidth (juce::GlyphArrangement::getStringWidthInt (sans (12.0f, 600), "+ Parameter") + 20));
            break;
        default: break;
    }
}

void GridRows::mouseDoubleClick (const juce::MouseEvent& e)
{
    auto t = hitTest (e.position);
    if (t.track < 0 || t.track >= (int) session.doc.tracks.size()) return;
    auto& tr = session.doc.tracks[(size_t) t.track];
    switch (t.kind)
    {
        case Target::Vol: session.checkpoint(); tr.volume = 100; session.changed(); break;
        case Target::Pan: session.checkpoint(); tr.pan = 0; session.changed(); break;
        case Target::ParamCell:
        {
            auto* h = session.hitAt (tr.id, keyOf (t.bar, t.step));
            if (h == nullptr) break;
            session.checkpoint();
            if (t.param == ParamId::Velocity) h->velocity = defaultVelocity (tr, t.step);
            else h->p[(size_t) t.param] = Hit::kUnset;
            session.changed();
            break;
        }
        default: break;
    }
}

void GridRows::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    auto t = hitTest (e.position);
    if (t.track < 0 || t.track >= (int) session.doc.tracks.size()
        || ! (t.kind == Target::Vol || t.kind == Target::Pan || t.kind == Target::ParamCell
              || (t.kind == Target::Cell && session.doc.tracks[(size_t) t.track].sound.play == PlayMode::Slices
                  && session.hitAt (session.doc.tracks[(size_t) t.track].id, keyOf (t.bar, t.step)) != nullptr)))
    {
        Component::mouseWheelMove (e, w);
        return;
    }
    wheelAcc += w.deltaY * (w.isReversed ? -1.0f : 1.0f);
    int steps = (int) (wheelAcc / 0.07f);
    if (steps == 0) return;
    wheelAcc -= steps * 0.07f;
    auto& tr = session.doc.tracks[(size_t) t.track];

    if (t.kind == Target::Cell)
    {
        session.scrollSlice (tr.id, keyOf (t.bar, t.step), steps > 0 ? 1 : -1);
        return;
    }
    session.checkpoint (this);
    if (t.kind == Target::Vol) tr.volume = juce::jlimit (0, 127, tr.volume + steps * 2);
    else if (t.kind == Target::Pan) tr.pan = juce::jlimit (-64, 63, tr.pan + steps * 2);
    else if (auto* h = session.hitAt (tr.id, keyOf (t.bar, t.step)))
        setHitParam (tr, *h, t.param, effectiveValue (tr, *h, t.param) + steps);
    session.changed();
}

//==============================================================================
juce::Rectangle<int> GridRows::dotsArea (int item) const
{
    return { 98, items[(size_t) item].y + 4, 20, 20 };
}

void GridRows::showTrackMenu (int trackIndex, juce::Rectangle<int> anchor)
{
    auto id = session.doc.tracks[(size_t) trackIndex].id;
    enum { kRename = 1, kColor, kDuplicate, kClearHits, kDelete, kCopy, kPaste };

    juce::PopupMenu m;
    juce::PopupMenu::Item rename ("Rename");
    rename.itemID = kRename;
    rename.shortcutKeyDescription = "F2";
    m.addItem (rename);
    m.addItem (kColor, "Color");
    m.addItem (kDuplicate, "Duplicate");
    m.addSeparator();

    // Copy / Paste / Clear act on every bar of the track in the shown part
    m.addItem (kCopy, "Copy");
    m.addItem (kPaste, "Paste", session.trackClipIsWhole() && session.canPasteTrack (id, -1));
    m.addItem (kClearHits, "Clear");
    m.addSeparator();
    juce::PopupMenu::Item del ("Delete");
    del.itemID = kDelete;
    del.shortcutKeyDescription = "Del";
    del.colour = C::red;
    m.addItem (del);

    showMenu (m, this, [this, id, trackIndex] (int r)
    {
        if (r == kRename) startRename (id);
        else if (r == kColor) showColours (trackIndex);
        else if (r == kDuplicate) session.duplicateTrack (id);
        else if (r == kClearHits) session.clearHits (id);
        else if (r == kDelete) session.deleteTrack (id);
        else if (r == kCopy) session.copyTrack (id, -1);
        else if (r == kPaste) session.pasteTrack (id, -1);
    }, 200, anchor);
}

void GridRows::showColours (int trackIndex)
{
    if (trackIndex < 0 || trackIndex >= (int) session.doc.tracks.size()) return;
    auto id = session.doc.tracks[(size_t) trackIndex].id;
    int item = -1;
    for (int i = 0; i < (int) items.size(); ++i)
        if (items[(size_t) i].kind == Item::Track && items[(size_t) i].track == trackIndex) item = i;
    if (item < 0) return;
    auto anchor = host.rootComponent().getLocalArea (this, juce::Rectangle<int> (0, items[(size_t) item].y, 20, kRowH));
    host.showPopover (std::make_unique<ColourPicker> (session, id, [this] { host.closePopover(); }), anchor);
}

void GridRows::showAddParam (juce::Rectangle<int> anchor)
{
    auto* t = session.selectedTrack();
    if (t == nullptr) return;
    juce::PopupMenu m;
    juce::String group;
    for (int i = 1; i < kNumParams; ++i)
    {
        auto id = (ParamId) i;
        const auto& info = paramInfo (id);
        if (group != info.group)
        {
            group = info.group;
            m.addSectionHeader (group);
        }
        bool shown = std::find (t->rows.begin(), t->rows.end(), id) != t->rows.end();
        m.addItem (100 + i, info.label, ! shown, shown);
    }
    auto id = t->id;
    showMenu (m, this, [this, id] (int r)
    {
        if (r < 100) return;
        auto* tr = session.doc.findTrack (id);
        if (tr == nullptr) return;
        session.checkpoint();
        tr->rows.push_back ((ParamId) (r - 100));
        session.changed();
    }, 200, anchor);
}

//==============================================================================
void GridRows::startRename (uint32_t id)
{
    int idx = session.doc.indexOf (id);
    if (idx < 0) return;
    int item = -1;
    for (int i = 0; i < (int) items.size(); ++i)
        if (items[(size_t) i].kind == Item::Track && items[(size_t) i].track == idx) item = i;
    if (item < 0) return;

    renamingId = id;
    renameEd = std::make_unique<juce::TextEditor>();
    renameEd->setFont (sans (13.0f, 500));
    renameEd->setText (session.doc.tracks[(size_t) idx].name, false);
    renameEd->setIndents (6, 4);
    renameEd->setBounds (20, items[(size_t) item].y + 2, 98, 24);
    renameEd->onReturnKey = [this] { commitRename (true); };
    renameEd->onEscapeKey = [this] { commitRename (false); };
    renameEd->onFocusLost = [this] { commitRename (true); };
    addAndMakeVisible (*renameEd);
    renameEd->grabKeyboardFocus();
    renameEd->selectAll();
    repaint();
}

void GridRows::commitRename (bool apply)
{
    if (renameEd == nullptr) return;
    auto text = renameEd->getText();
    auto id = renamingId;
    renamingId = 0;
    juce::MessageManager::callAsync ([sp = juce::Component::SafePointer<GridRows> (this)] { if (sp) sp->renameEd = nullptr; });
    if (apply) session.renameTrack (id, text);
    repaint();
}

//==============================================================================
void GridRows::setDropFromY (int y, bool isTrackDrag)
{
    dropTrack = -1;
    dropWhere = 2;
    for (auto& it : items)
    {
        if (y < it.y - kGap || y >= it.y + it.h + kGap) continue;
        if (it.kind == Item::Empty) break;
        dropTrack = it.track;
        if (it.kind == Item::Track)
        {
            float rel = (float) (y - it.y) / (float) it.h;
            dropWhere = isTrackDrag ? (rel < 0.5f ? -1 : 1) : (rel < 0.25f ? -1 : (rel > 0.75f ? 1 : 0));
        }
        else dropWhere = isTrackDrag ? 1 : 0;
        break;
    }
    dropActive = true;
    repaint();
}

bool GridRows::isInterestedInDragSource (const SourceDetails& d)
{
    auto s = d.description.toString();
    return s.startsWith ("file:") || s.startsWith ("track:") || s.startsWith ("slice:");
}

// A slice from the slicer: on a cell of its own track it becomes a hit there, anywhere else a new track
void GridRows::setSliceDrop (const juce::String& desc, juce::Point<int> p)
{
    const auto srcId = (uint32_t) desc.fromFirstOccurrenceOf ("slice:", false, false).upToFirstOccurrenceOf (":", false, false).getIntValue();
    dropBar = dropStep = -1;
    for (auto& it : items)
    {
        if (it.kind != Item::Track || p.y < it.y || p.y >= it.y + it.h) continue;
        const auto& t = session.doc.tracks[(size_t) it.track];
        int bar, step;
        if (t.id == srcId && p.x >= kCellsX && cellAt (t, (float) p.x, bar, step) && bar < session.curPart().bars)
        {
            dropTrack = it.track;
            dropWhere = 3;
            dropBar = bar;
            dropStep = step;
            dropActive = true;
            repaint();
            return;
        }
    }
    setDropFromY (p.y, true);
}

void GridRows::itemDragMove (const SourceDetails& d)
{
    if (d.description.toString().startsWith ("slice:")) { setSliceDrop (d.description.toString(), d.localPosition); return; }
    setDropFromY (d.localPosition.y, d.description.toString().startsWith ("track:"));
}

void GridRows::itemDragExit (const SourceDetails&)
{
    dropActive = false;
    repaint();
}

void GridRows::itemDropped (const SourceDetails& d)
{
    auto s = d.description.toString();
    if (s.startsWith ("slice:"))
    {
        setSliceDrop (s, d.localPosition);
        dropActive = false;
        auto rest = s.fromFirstOccurrenceOf ("slice:", false, false);
        auto srcId = (uint32_t) rest.upToFirstOccurrenceOf (":", false, false).getIntValue();
        int sliceIdx = rest.fromFirstOccurrenceOf (":", false, false).getIntValue();
        if (dropWhere == 3)
            session.putSliceHit (srcId, dropBar, dropStep, sliceIdx);
        else
            session.sliceToTrack (srcId, sliceIdx, dropTrack < 0 ? (int) session.doc.tracks.size() : dropTrack + (dropWhere > 0 ? 1 : 0));
        repaint();
        return;
    }
    setDropFromY (d.localPosition.y, s.startsWith ("track:"));
    dropActive = false;
    if (s.startsWith ("track:"))
    {
        auto id = (uint32_t) s.fromFirstOccurrenceOf ("track:", false, false).getIntValue();
        int idx = dropTrack < 0 ? (int) session.doc.tracks.size() : dropTrack + (dropWhere > 0 ? 1 : 0);
        session.moveTrack (id, idx);
    }
    else
        dropFiles ({ s.fromFirstOccurrenceOf ("file:", false, false) });
    repaint();
}

bool GridRows::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (auto& f : files)
        if (session.samples.isAudioFile (juce::File (f))) return true;
    return false;
}

void GridRows::fileDragMove (const juce::StringArray&, int, int y) { setDropFromY (y, false); }

void GridRows::fileDragExit (const juce::StringArray&)
{
    dropActive = false;
    repaint();
}

void GridRows::filesDropped (const juce::StringArray& files, int, int y)
{
    setDropFromY (y, false);
    dropActive = false;
    juce::StringArray audio;
    for (auto& f : files)
        if (session.samples.isAudioFile (juce::File (f))) audio.add (f);
    dropFiles (audio);
    repaint();
}

void GridRows::dropFiles (const juce::StringArray& files)
{
    if (files.isEmpty()) return;
    int idx = dropTrack < 0 ? (int) session.doc.tracks.size() : dropTrack + (dropWhere > 0 ? 1 : 0);
    int first = 0;
    if (dropTrack >= 0 && dropWhere == 0)
    {
        session.replaceSample (session.doc.tracks[(size_t) dropTrack].id, juce::File (files[0]));
        first = 1;
        idx = dropTrack + 1;
    }
    for (int i = first; i < files.size(); ++i)
    {
        if (! session.addTrack (juce::File (files[i]), idx)) break;
        ++idx;
    }
}

//==============================================================================
void Ruler::paint (juce::Graphics& g)
{
    auto bars = rows.barRanges();
    auto mouse = getMouseXYRelative().toFloat();
    for (int b = 0; b < (int) bars.size(); ++b)
    {
        auto br = bars[(size_t) b];
        const float beatW = (br.getLength() - 12.0f) / 4.0f;
        for (int bt = 0; bt < 4; ++bt)
        {
            float x = br.getStart() + bt * (beatW + 4.0f);
            if (bt == 0)
            {
                g.setColour (C::dim);
                g.fillRect (x, 0.0f, 1.0f, (float) getHeight());
                g.setColour (C::text);
                g.setFont (mono (11.0f));
                g.drawText (juce::String (b + 1), juce::Rectangle<float> (x + 5, 0, 30, (float) getHeight()), juce::Justification::centredLeft, false);
            }
        }
        auto dots = juce::Rectangle<float> (br.getEnd() - 24.0f, 1.0f, 24.0f, 20.0f);
        g.setColour (isMouseOver() && dots.contains (mouse) ? C::hover : C::btn);
        g.fillRoundedRectangle (dots, 4.0f);
        drawDots (g, dots, C::text2);
    }
}

void Ruler::mouseUp (const juce::MouseEvent& e)
{
    auto bars = rows.barRanges();
    for (int b = 0; b < (int) bars.size(); ++b)
    {
        auto dots = juce::Rectangle<float> (bars[(size_t) b].getEnd() - 24.0f, 0.0f, 24.0f, 22.0f);
        if (! dots.contains (e.position)) continue;

        juce::PopupMenu m;
        m.addSectionHeader ("ALL TRACKS");
        m.addItem (1, "Copy");
        m.addItem (3, "Paste", session.canPasteBar (b));
        m.addItem (2, "Clear");
        showMenu (m, this, [this, b] (int r)
        {
            if (r == 1) session.copyBar (b);
            else if (r == 2) session.clearBar (b);
            else if (r == 3) session.pasteBar (b);
        }, 230, dots.toNearestInt());
        return;
    }
}

//==============================================================================
SwingBox::SwingBox (Session& s) : session (s)
{
    setTooltip ("Swing for the whole pattern: delays every second step in a group. Drag or scroll, double-click resets");
    setMouseCursor (juce::MouseCursor::UpDownResizeCursor);
}

void SwingBox::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (C::muted);
    g.setFont (sans (12.0f));
    g.drawText ("Swing", r, juce::Justification::centredLeft, false);
    g.setColour (C::text);
    g.setFont (mono (12.0f));
    g.drawText (juce::String (session.doc.swing) + "%", r, juce::Justification::centredRight, false);
}

void SwingBox::set (int v)
{
    v = juce::jlimit (50, 75, v);
    if (v == session.doc.swing) return;
    session.doc.swing = v;
    session.changed();
    repaint();
}

void SwingBox::mouseDown (const juce::MouseEvent&)
{
    session.checkpoint();
    start = session.doc.swing;
}

void SwingBox::mouseDrag (const juce::MouseEvent& e) { set (start + (int) std::lround (-e.getDistanceFromDragStartY() / 4.0f)); }

void SwingBox::mouseDoubleClick (const juce::MouseEvent&)
{
    session.checkpoint();
    set (50);
}

void SwingBox::mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& w)
{
    wheelAcc += w.deltaY * (w.isReversed ? -1.0f : 1.0f);
    int steps = (int) (wheelAcc / 0.07f);
    if (steps == 0) return;
    wheelAcc -= steps * 0.07f;
    session.checkpoint (this);
    set (session.doc.swing + steps);
}

//==============================================================================
GridSection::GridSection (Session& s, UiHost& h) : session (s), swing (s), rows (s, h)
{
    zoom.items = { "1 bar", "2 bars", "3 bars", "4 bars" };
    zoom.get = [this] { return session.curPart().bars - 1; };
    zoom.set = [this] (int i) { session.setPartBars (i + 1); };
    addAndMakeVisible (zoom);
    addAndMakeVisible (swing);
    addAndMakeVisible (ruler);
    viewport.setViewedComponent (&rows, false);
    viewport.setScrollBarsShown (true, false);
    viewport.setScrollBarThickness (8);
    addAndMakeVisible (viewport);
}

void GridSection::refresh()
{
    resized();
    zoom.repaint();
    swing.repaint();
    ruler.repaint();
    repaint();
}

void GridSection::resized()
{
    auto r = getLocalBounds().withTrimmedLeft (16).withTrimmedRight (16).withTrimmedTop (10).withTrimmedBottom (8);
    auto bar = r.removeFromTop (26);
    zoom.setBounds (bar.getX(), bar.getY(), zoom.idealWidth(), 26);
    swing.setBounds (bar.getRight() - 72, bar.getY(), 72, 26);
    hintArea = bar.withTrimmedLeft (zoom.idealWidth() + 24).withWidth (300);
    legendArea = bar.withTrimmedRight (72 + 16).withTrimmedLeft (bar.getWidth() - 72 - 16 - 230);
    r.removeFromTop (6);
    auto rulerRow = r.removeFromTop (22);
    r.removeFromTop (6);
    viewport.setBounds (r);
    const int w = viewport.getMaximumVisibleWidth();
    rows.setSize (w, 10);
    rows.refresh();
    rows.setSize (w, juce::jmax (r.getHeight(), rows.contentHeight()));
    ruler.setBounds (rulerRow.getX(), rulerRow.getY(), w, 22);
}

void GridSection::paint (juce::Graphics& g)
{
    g.setColour (C::muted);
    g.setFont (sans (12.0f));
    g.drawText (juce::String::fromUTF8 ("Shift-click \xe2\x80\x94 mark for STEP \xc2\xb7 Alt-click \xe2\x80\x94 pin"), hintArea, juce::Justification::centredLeft, false);

    // legend
    auto lr = legendArea.toFloat();
    float x = lr.getX();
    float cy = lr.getCentreY();
    g.setColour (C::orange);
    g.fillRoundedRectangle (x, cy - 7, 14, 14, 3);
    g.setColour (C::ink);
    g.fillEllipse (x + 8, cy - 5, 4, 4);
    g.setColour (C::muted);
    g.drawText ("Marked for STEP", juce::Rectangle<float> (x + 20, lr.getY(), 110, lr.getHeight()), juce::Justification::centredLeft, false);
    x += 20 + juce::GlyphArrangement::getStringWidthInt (sans (12.0f), "Marked for STEP") + 12;
    g.setColour (C::orange);
    g.fillRoundedRectangle (x, cy - 7, 14, 14, 3);
    juce::Path tri;
    tri.addTriangle (x, cy, x, cy + 7, x + 7, cy + 7);
    g.setColour (C::ink);
    g.fillPath (tri);
    g.setColour (C::muted);
    g.drawText ("Pinned", juce::Rectangle<float> (x + 20, lr.getY(), 60, lr.getHeight()), juce::Justification::centredLeft, false);
}

} // namespace ok::ui
