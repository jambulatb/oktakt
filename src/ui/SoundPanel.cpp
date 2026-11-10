#include "SoundPanel.h"
#include "../Dsp.h"

namespace ok::ui
{

static juce::String sgn (int v) { return v > 0 ? "+" + juce::String (v) : (v < 0 ? juce::String::fromUTF8 ("\xe2\x88\x92") + juce::String (-v) : juce::String ("0")); }

//==============================================================================
void ValueBox::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (C::btn2);
    g.fillRoundedRectangle (r, 5.0f);
    if (hasKeyboardFocus (false))
    {
        g.setColour (C::menuBorder);
        g.drawRoundedRectangle (r.reduced (0.5f), 5.0f, 1.0f);
    }
    if (editor || ! get) return;
    g.setColour (C::text);
    g.setFont (mono (13.0f));
    int v = get();
    g.drawText (format ? format (v) : juce::String (v), r, juce::Justification::centred, false);
}

void ValueBox::apply (int v)
{
    v = juce::jlimit (min, max, v);
    if (get && v == get()) return;
    if (set) set (v);
    repaint();
}

void ValueBox::mouseDown (const juce::MouseEvent&)
{
    if (! get) return;
    start = get();
    if (onGesture) onGesture();
}

void ValueBox::mouseDrag (const juce::MouseEvent& e)
{
    float sens = (max - min) > 30 ? 1.0f : 0.15f;
    apply (start + (int) std::lround (-e.getDistanceFromDragStartY() * sens));
}

void ValueBox::mouseDoubleClick (const juce::MouseEvent&)
{
    if (! typeOnDoubleClick)
    {
        if (onGesture) onGesture();
        apply (def);
        return;
    }
    editor = std::make_unique<juce::TextEditor>();
    editor->setFont (mono (13.0f));
    editor->setJustification (juce::Justification::centred);
    editor->setInputRestrictions (3, "0123456789");
    editor->setText (juce::String (get ? get() : def), false);
    auto finish = [this] (bool ok)
    {
        if (editor == nullptr) return;
        if (ok)
        {
            auto v = editor->getText().getIntValue();
            if (onGesture) onGesture();
            apply (v);
        }
        juce::MessageManager::callAsync ([sp = juce::Component::SafePointer<ValueBox> (this)] { if (sp) { sp->editor = nullptr; sp->repaint(); } });
    };
    editor->onReturnKey = [finish] { finish (true); };
    editor->onFocusLost = [finish] { finish (true); };
    editor->onEscapeKey = [finish] { finish (false); };
    addAndMakeVisible (*editor);
    resized();
    editor->grabKeyboardFocus();
    editor->selectAll();
}

void ValueBox::mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& w)
{
    if (! get) return;
    wheelAcc += w.deltaY * (w.isReversed ? -1.0f : 1.0f);
    int steps = (int) (wheelAcc / 0.08f);
    if (steps == 0) return;
    wheelAcc -= steps * 0.08f;
    if (onGesture) onGesture();
    apply (get() + (steps > 0 ? 1 : -1));
}

bool ValueBox::keyPressed (const juce::KeyPress& k)
{
    if (! get) return false;
    if (k.getKeyCode() == juce::KeyPress::upKey) { if (onGesture) onGesture(); apply (get() + 1); return true; }
    if (k.getKeyCode() == juce::KeyPress::downKey) { if (onGesture) onGesture(); apply (get() - 1); return true; }
    return false;
}

//==============================================================================
WaveView::Geo WaveView::geometry() const
{
    Geo g;
    g.w = (float) getWidth();
    g.h = (float) getHeight() - 4.0f - kTicksH;
    auto* t = const_cast<Session&> (session).selectedTrack();
    if (t == nullptr) return g;
    auto s = session.samples.getCached (t->samplePath);
    double secs = s != nullptr ? s->seconds() : 1.0;
    g.lenS = (float) (secs * t->sound.stretch / 100.0);
    double bpm = session.isPlugin ? session.engine.currentBpm.load() : session.bpm();
    g.bars = (float) (g.lenS * bpm / 240.0);
    g.span = juce::jmax (1.0f, g.bars);
    g.areaW = g.w * g.bars / g.span;
    return g;
}

double WaveView::fracAt (float x) const
{
    auto g = geometry();
    return g.areaW > 0 ? juce::jlimit (0.0, 1.0, (double) (x / g.areaW)) : 0.0;
}

int WaveView::markerAt (float x) const
{
    auto* t = const_cast<Session&> (session).selectedTrack();
    if (t == nullptr || t->sound.play != PlayMode::Slices) return -1;
    auto g = geometry();
    int best = -1;
    float bestD = 6.0f;
    for (int i = 1; i < (int) t->sound.slices.size(); ++i)
    {
        float d = std::abs (g.xOf (t->sound.slices[(size_t) i].start) - x);
        if (d < bestD) { bestD = d; best = i; }
    }
    return best;
}

int WaveView::sliceAt (float x) const
{
    auto* t = const_cast<Session&> (session).selectedTrack();
    if (t == nullptr || t->sound.play != PlayMode::Slices || t->sound.slices.empty()) return -1;
    double f = fracAt (x);
    int idx = 0;
    for (int i = 0; i < (int) t->sound.slices.size(); ++i)
        if (t->sound.slices[(size_t) i].start <= f) idx = i;
    return idx;
}

void WaveView::paint (juce::Graphics& g)
{
    auto* t = session.selectedTrack();
    if (t == nullptr) return;
    const auto geo = geometry();
    const auto& s = t->sound;
    const auto col = trackColour (t->colour);
    auto sample = session.samples.getCached (t->samplePath);
    auto box = juce::Rectangle<float> (0, 0, geo.w, geo.h);

    {
        juce::Graphics::ScopedSaveState ss (g);
        juce::Path clip;
        clip.addRoundedRectangle (box, 6.0f);
        g.reduceClipRegion (clip);
        g.setColour (C::wave);
        g.fillRect (box);

        // beat and bar lines
        for (int qb = 1; qb < (int) std::ceil (geo.span * 4.0f - 0.01f); ++qb)
        {
            float x = qb / 4.0f / geo.span * geo.w;
            g.setColour (qb % 4 == 0 ? C::knobTrack : juce::Colour (0xff1C1D21));
            g.fillRect (x, 0.0f, 1.0f, geo.h);
        }
        if (geo.areaW < geo.w - 1.0f)
        {
            g.setColour (juce::Colours::white.withAlpha (0.02f));
            g.fillRect (0.0f, 0.0f, geo.areaW, geo.h);
            juce::Path edge;
            edge.startNewSubPath (geo.areaW, 0);
            edge.lineTo (geo.areaW, geo.h);
            juce::Path dashed;
            const float dashes[] = { 4.0f, 3.0f };
            juce::PathStrokeType (1.0f).createDashedStroke (dashed, edge, dashes, 2);
            g.setColour (juce::Colour (0xff4A4D55));
            g.fillPath (dashed);
        }

        // regions: slices or the whole sample
        std::vector<std::pair<double, double>> regions;
        const bool slices = s.play == PlayMode::Slices && ! s.slices.empty();
        if (slices)
            for (size_t i = 0; i < s.slices.size(); ++i)
                regions.push_back ({ s.slices[i].start, i + 1 < s.slices.size() ? s.slices[i + 1].start : 1.0 });
        else
            regions.push_back ({ s.regionStart, s.regionEnd });

        if (slices)
            for (int i = 0; i < (int) regions.size(); ++i)
            {
                auto r = juce::Rectangle<float> (geo.xOf (regions[(size_t) i].first), 0, geo.xOf (regions[(size_t) i].second) - geo.xOf (regions[(size_t) i].first), geo.h);
                g.setColour (selectedSlice == i ? juce::Colour (0x1FECEAE4) : (i % 2 ? juce::Colour (0xff131416) : C::wave));
                g.fillRect (r);
            }

        // One-shot IN / OUT: gain at a position of the sample, the same as the engine applies
        const bool fades = s.amp == AmpMode::OneShot && (s.fadeIn > 0 || s.fadeOut > 0);
        auto fadeGain = [&] (double f)
        {
            if (! fades) return 1.0f;
            for (auto& [a, b] : regions)
            {
                if (f < a || f >= b) continue;
                double s0 = a + (b - a) * s.start / 127.0;
                double e0 = juce::jmin (b, s0 + (b - a) * s.length / 127.0);
                if (f < s0 || f >= e0) return 1.0f;
                double len = e0 - s0, gn = 1.0;
                if (s.fadeIn > 0) gn = juce::jmin (gn, (f - s0) / (len * s.fadeIn / 100.0));
                if (s.fadeOut > 0) gn = juce::jmin (gn, (e0 - f) / (len * s.fadeOut / 100.0));
                return (float) juce::jlimit (0.0, 1.0, gn);
            }
            return 1.0f;
        };

        // waveform
        if (sample != nullptr && geo.areaW > 1.0f)
        {
            const auto& pk = sample->peaks;
            const int cols = (int) geo.areaW;
            const float cy = geo.h * 0.5f, amp = geo.h * 0.45f;
            g.setColour (col.withAlpha (0.8f));
            for (int x = 0; x < cols; x += 2)
            {
                size_t a = (size_t) ((double) x / geo.areaW * pk.size());
                size_t b = juce::jmax (a + 1, (size_t) ((double) (x + 2) / geo.areaW * pk.size()));
                float p = 0.0f;
                for (size_t k = a; k < juce::jmin (b, pk.size()); ++k) p = juce::jmax (p, pk[k]);
                float hh = juce::jmax (0.6f, p * amp * fadeGain ((x + 1.0) / geo.areaW));
                g.fillRect ((float) x, cy - hh, 1.4f, hh * 2.0f);
            }
        }

        // dims from Start / Length, and outside the region
        g.setColour (juce::Colours::black.withAlpha (0.6f));
        if (! slices)
        {
            if (s.regionStart > 0.0) g.fillRect (0.0f, 0.0f, geo.xOf (s.regionStart), geo.h);
            if (s.regionEnd < 1.0) g.fillRect (geo.xOf (s.regionEnd), 0.0f, geo.areaW - geo.xOf (s.regionEnd), geo.h);
        }
        juce::Path env;
        const float top = 8.0f, bottom = geo.h - 4.0f;
        const float pps = geo.lenS > 0 ? geo.areaW / geo.lenS : 0.0f;
        auto secs = [] (int k) { return k == 0 ? 0.0001f : dsp::knobToSeconds (k); };
        for (auto& [a, b] : regions)
        {
            double s0 = a + (b - a) * s.start / 127.0;
            double e0 = juce::jmin (b, s0 + (b - a) * s.length / 127.0);
            g.setColour (juce::Colours::black.withAlpha (0.6f));
            if (s0 > a) g.fillRect (geo.xOf (a), 0.0f, geo.xOf (s0) - geo.xOf (a), geo.h);
            if (e0 < b) g.fillRect (geo.xOf (e0), 0.0f, geo.xOf (b) - geo.xOf (e0), geo.h);

            float xs = geo.xOf (s0), xe = geo.xOf (e0);
            if (fades)
            {
                auto yAt = [&] (double f) { return bottom - (bottom - top) * fadeGain (f); };
                double fi = s0 + (e0 - s0) * s.fadeIn / 100.0, fo = e0 - (e0 - s0) * s.fadeOut / 100.0;
                if (fi > fo) fi = fo = s0 + (e0 - s0) * s.fadeIn / (double) (s.fadeIn + s.fadeOut);
                env.startNewSubPath (xs, yAt (s0));
                env.lineTo (geo.xOf (fi), yAt (fi));
                env.lineTo (geo.xOf (fo), yAt (fo));
                env.lineTo (xe, yAt (e0 - 1e-9));
            }
            else if (s.amp == AmpMode::AHD)
            {
                float x1 = juce::jmin (xe, xs + secs (s.attack) * pps);
                float x2 = juce::jmin (xe, x1 + (s.hold == 0 ? 0.0f : dsp::knobToSeconds (s.hold)) * pps);
                float x3 = juce::jmin (xe, x2 + secs (s.decay) * pps);
                env.startNewSubPath (xs, bottom);
                env.lineTo (x1, top);
                env.lineTo (x2, top);
                env.lineTo (x3, bottom);
                if (x3 < xe) env.lineTo (xe, bottom);
            }
            else if (s.amp == AmpMode::ADSR)
            {
                double bpm = session.isPlugin ? session.engine.currentBpm.load() : session.bpm();
                float gate = juce::jmin (xe, xs + (float) (60.0 / bpm) * pps);
                float susY = bottom - (bottom - top) * s.sustain / 127.0f;
                float a1 = juce::jmin (gate, xs + secs (s.attack) * pps);
                float d1 = juce::jmin (gate, a1 + secs (s.decay) * pps);
                float r1 = juce::jmin (xe, gate + secs (s.release) * pps);
                env.startNewSubPath (xs, bottom);
                env.lineTo (a1, top);
                env.lineTo (d1, susY);
                env.lineTo (gate, susY);
                env.lineTo (r1, bottom);
                if (r1 < xe) env.lineTo (xe, bottom);
            }
        }
        if (! env.isEmpty())
        {
            juce::Path dashed;
            const float dashes[] = { 4.0f, 3.0f };
            juce::PathStrokeType (1.5f).createDashedStroke (dashed, env, dashes, 2);
            g.setColour (C::text);
            g.fillPath (dashed);
        }

        // slice markers
        if (slices)
        {
            auto tab = [&] (float x, int n, bool on)
            {
                auto txt = juce::String (n);
                float w = juce::jmax (18.0f, (float) juce::GlyphArrangement::getStringWidthInt (mono (11.0f, 600), txt) + 8.0f);
                juce::Path p;
                p.addRoundedRectangle (x, 0.0f, w, 16.0f, 4.0f, 4.0f, false, false, false, true);
                g.setColour (on ? col : C::text);
                g.fillPath (p);
                g.setColour (C::ink);
                g.setFont (mono (11.0f, 600));
                g.drawText (txt, juce::Rectangle<float> (x, 0, w, 16), juce::Justification::centred, false);
            };
            for (int i = 1; i < (int) s.slices.size(); ++i)
            {
                float x = geo.xOf (s.slices[(size_t) i].start);
                g.setColour (C::text.withAlpha (0.8f));
                g.fillRect (x - 1.0f, 0.0f, 2.0f, geo.h);
                tab (x, i + 1, selectedSlice == i);
            }
            tab (0.0f, 1, selectedSlice == 0);
        }

        if (t->missing)
        {
            g.setColour (juce::Colour (0xff141518));
            g.fillRect (box);
            g.setColour (C::red);
            g.setFont (sans (13.0f, 600));
            g.drawText ("File not found", box.withTrimmedBottom (box.getHeight() * 0.5f - 4), juce::Justification::centredBottom, false);
            g.setColour (C::muted);
            g.setFont (sans (11.0f));
            g.drawText ("Drop a sample here to replace it", box.withTrimmedTop (box.getHeight() * 0.5f + 4), juce::Justification::centredTop, false);
        }

        // file name
        auto name = sampleFileName (t->samplePath);
        auto nf = mono (11.0f);
        float nw = (float) juce::GlyphArrangement::getStringWidthInt (nf, name) + 12.0f;
        auto nr = juce::Rectangle<float> (6, geo.h - 22, juce::jmin (nw, geo.w - 12), 16);
        g.setColour (juce::Colour (0xcc101113));
        g.fillRoundedRectangle (nr, 4.0f);
        g.setColour (C::text2);
        g.setFont (nf);
        g.drawText (name, nr.reduced (6, 0), juce::Justification::centredLeft, true);
    }
    if (hot)
    {
        g.setColour (C::orange);
        g.drawRoundedRectangle (box.reduced (1.0f), 6.0f, 2.0f);
    }

    // bar ruler and length hint
    auto ticks = juce::Rectangle<float> (0, geo.h + 4, geo.w, (float) kTicksH);
    for (int b = 0; b < (int) std::ceil (geo.span - 0.01f); ++b)
    {
        float x = b / geo.span * geo.w;
        g.setColour (C::dim);
        g.fillRect (x, ticks.getY(), 1.0f, ticks.getHeight());
        g.setColour (C::text);
        g.setFont (mono (11.0f));
        g.drawText (juce::String (b + 1), juce::Rectangle<float> (x + 4, ticks.getY(), 30, ticks.getHeight()), juce::Justification::centredLeft, false);
    }
    double secs = sample != nullptr ? sample->seconds() : 0.0;
    double bpm = session.isPlugin ? session.engine.currentBpm.load() : session.bpm();
    juce::String hint;
    if (s.stretch == 100) hint = "Original length " + juce::String (secs, 2) + " s";
    else
    {
        hint = "Length " + juce::String (secs, 2) + juce::String::fromUTF8 (" \xe2\x86\x92 ") + juce::String (geo.lenS, 2) + " s";
        if (s.sampleMode == SampleMode::Repitch)
        {
            double st = -12.0 * std::log2 (s.stretch / 100.0);
            hint += juce::String::fromUTF8 (" \xc2\xb7 ") + (st > 0 ? "+" : "") + juce::String (st, 1) + " st";
        }
        else hint += juce::String::fromUTF8 (" \xc2\xb7 pitch kept");
    }
    hint += juce::String::fromUTF8 (" \xc2\xb7 ") + juce::String (geo.bars, 2) + " bars at " + juce::String (bpm, 1) + " BPM";
    if (s.amp == AmpMode::OneShot && (s.fadeIn > 0 || s.fadeOut > 0)) hint += juce::String::fromUTF8 (" \xc2\xb7 dashed line = In / Out");
    if (s.amp == AmpMode::AHD) hint += juce::String::fromUTF8 (" \xc2\xb7 dashed line = amp envelope");
    if (s.amp == AmpMode::ADSR) hint += juce::String::fromUTF8 (" \xc2\xb7 dashed line = amp envelope (note held for 1 beat)");
    auto hf = sans (11.0f);
    float hw = juce::jmin (ticks.getWidth(), (float) juce::GlyphArrangement::getStringWidthInt (hf, hint) + 12.0f);
    auto hr = ticks.withLeft (ticks.getRight() - hw);
    g.setColour (C::panel);
    g.fillRect (hr);
    g.setColour (C::muted);
    g.setFont (hf);
    g.drawText (hint, hr, juce::Justification::centredRight, true);
}

void WaveView::mouseMove (const juce::MouseEvent& e)
{
    auto cursor = juce::MouseCursor::NormalCursor;
    if (editing && markerAt (e.position.x) > 0) cursor = juce::MouseCursor::LeftRightResizeCursor;
    else if (! editing && selectedSlice >= 0 && e.position.y < geometry().h && sliceAt (e.position.x) == selectedSlice)
        cursor = juce::MouseCursor::DraggingHandCursor;
    setMouseCursor (cursor);
}

void WaveView::mouseDown (const juce::MouseEvent& e)
{
    dragMarker = -1;
    draggingSlice = false;
    pressSlice = (! editing && e.position.y < geometry().h) ? sliceAt (e.position.x) : -1;
    auto* t = session.selectedTrack();
    if (t == nullptr || ! editing) return;
    int m = markerAt (e.position.x);
    if (m > 0)
    {
        if (e.mods.isAltDown()) { session.removeMarker (t->id, m); selectedSlice = -1; }
        else dragMarker = m;
    }
}

void WaveView::mouseDrag (const juce::MouseEvent& e)
{
    auto* t = session.selectedTrack();
    if (t == nullptr) return;

    // Outside the editor a slice can be dragged into the grid
    if (! editing && pressSlice >= 0 && ! draggingSlice && e.getDistanceFromDragStart() > 5)
    {
        draggingSlice = true;
        selectedSlice = pressSlice;
        repaint();
        if (auto* dc = juce::DragAndDropContainer::findParentDragContainerFor (this))
        {
            auto label = t->name + juce::String::fromUTF8 (" \xc2\xb7 slice ") + juce::String (pressSlice + 1);
            auto font = sans (13.0f, 500);
            int w = juce::GlyphArrangement::getStringWidthInt (font, label) + 34;
            juce::Image img (juce::Image::ARGB, w, 28, true);
            {
                juce::Graphics g (img);
                g.setColour (C::rowSel.withAlpha (0.95f));
                g.fillRoundedRectangle (0, 0, (float) w, 28, 6);
                g.setColour (trackColour (t->colour));
                g.fillRoundedRectangle (6, 4, 10, 20, 3);
                g.setColour (C::text);
                g.setFont (font);
                g.drawText (label, 24, 0, w - 26, 28, juce::Justification::centredLeft);
            }
            dc->startDragging ("slice:" + juce::String ((int) t->id) + ":" + juce::String (pressSlice), this,
                               juce::ScaledImage (img), false, nullptr, &e.source);
        }
        return;
    }
    if (dragMarker <= 0) return;
    session.moveMarker (t->id, dragMarker, fracAt (e.position.x));
}

void WaveView::mouseUp (const juce::MouseEvent& e)
{
    auto* t = session.selectedTrack();
    bool dragged = dragMarker > 0 || draggingSlice;
    dragMarker = -1;
    draggingSlice = false;
    pressSlice = -1;
    if (t == nullptr || dragged || e.mods.isAltDown() || e.getDistanceFromDragStart() > 3) return;
    if (e.position.y > geometry().h) return;
    double f = fracAt (e.position.x);
    if (t->sound.play == PlayMode::Slices && ! t->sound.slices.empty())
    {
        int idx = 0;
        for (int i = 0; i < (int) t->sound.slices.size(); ++i)
            if (t->sound.slices[(size_t) i].start <= f) idx = i;
        selectedSlice = (selectedSlice == idx) ? -1 : idx;
        session.previewSlice (t->id, idx);
    }
    else if (auto s = session.samples.getCached (t->samplePath))
        session.engine.preview (s, t->sound.regionStart, t->sound.regionEnd);
    repaint();
}

void WaveView::mouseDoubleClick (const juce::MouseEvent& e)
{
    auto* t = session.selectedTrack();
    if (t == nullptr || ! editing || t->sound.play != PlayMode::Slices) return;
    if (markerAt (e.position.x) > 0) return;
    session.addMarker (t->id, fracAt (e.position.x));
}

bool WaveView::isInterestedInDragSource (const SourceDetails& d)
{
    return d.description.toString().startsWith ("file:");
}

void WaveView::itemDropped (const SourceDetails& d)
{
    hot = false;
    if (auto* t = session.selectedTrack())
        session.replaceSample (t->id, juce::File (d.description.toString().fromFirstOccurrenceOf ("file:", false, false)));
    repaint();
}

bool WaveView::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (auto& f : files)
        if (session.samples.isAudioFile (juce::File (f))) return true;
    return false;
}

void WaveView::filesDropped (const juce::StringArray& files, int, int)
{
    hot = false;
    if (auto* t = session.selectedTrack())
        for (auto& f : files)
            if (session.samples.isAudioFile (juce::File (f))) { session.replaceSample (t->id, juce::File (f)); break; }
    repaint();
}

//==============================================================================
SoundPanel::SoundPanel (Session& s, UiHost& h) : session (s), host (h), wave (s)
{
    close.icon = [] (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour) { drawCross (g, r.withSizeKeepingCentre (9, 9), C::text2, 1.6f); };
    close.onClick = [this] { session.closeTrack(); };
    close.setTooltip ("Close");
    addAndMakeVisible (close);

    tabs.items = { "Whole", "Slices" };
    tabs.padX = 14;
    tabs.tracking = 0.06f;
    tabs.get = [this] { auto* t = session.selectedTrack(); return t && t->sound.play == PlayMode::Slices ? 1 : 0; };
    tabs.set = [this] (int i)
    {
        if (auto* t = session.selectedTrack())
        {
            auto m = i == 1 ? PlayMode::Slices : PlayMode::Whole;
            if (t->sound.play == m) return;
            session.checkpoint();
            t->sound.play = m;
            if (m == PlayMode::Slices && t->sound.slices.size() <= 1) session.rechop (*t);
            wave.editing = false;
            wave.selectedSlice = -1;
            session.changed();
        }
    };
    addAndMakeVisible (tabs);

    editBtn.padX = 14;
    editBtn.radius = 6.0f;
    editBtn.onClick = [this] { wave.editing = ! wave.editing; refresh(); };
    addChildComponent (editBtn);

    chop.items = { "By hits", "Equal parts", "Manual" };
    chop.padX = 11;
    chop.get = [this] { auto* t = session.selectedTrack(); return t ? (int) t->sound.chop : 0; };
    chop.set = [this] (int i) { setChop ((ChopMode) i); };
    addChildComponent (chop);

    sens.min = 0; sens.max = 100; sens.def = 55;
    sens.get = [this] { auto* t = session.selectedTrack(); return t ? t->sound.sensitivity : 55; };
    sens.set = [this] (int v) { if (auto* t = session.selectedTrack()) { t->sound.sensitivity = v; session.rechop (*t); session.changed(); } };
    sens.format = [] (int v) { return juce::String (v) + "%"; };
    sens.onGesture = [this] { session.checkpoint (&sens); };
    sens.setTooltip ("Sensitivity: higher finds more hits. Drag or scroll, double-click resets");
    addChildComponent (sens);

    parts.min = 2; parts.max = 20; parts.def = 8;
    parts.typeOnDoubleClick = true;
    parts.get = [this] { auto* t = session.selectedTrack(); return t ? t->sound.equalParts : 8; };
    parts.set = [this] (int v) { if (auto* t = session.selectedTrack()) { t->sound.equalParts = v; session.rechop (*t); session.changed(); } };
    parts.onGesture = [this] { session.checkpoint (&parts); };
    parts.setTooltip ("Number of equal parts: double-click to type, scroll or use the arrow keys");
    addChildComponent (parts);

    addAndMakeVisible (wave);
    editBtn.toFront (false);   // drawn over the top right corner of the wave
}

SoundPanel::~SoundPanel() = default;

void SoundPanel::setChop (ChopMode m)
{
    auto* t = session.selectedTrack();
    if (t == nullptr || t->sound.chop == m) return;
    session.checkpoint();
    t->sound.chop = m;
    if (m != ChopMode::Manual) session.rechop (*t);
    session.changed();
}

juce::String SoundPanel::structureKey() const
{
    auto* t = const_cast<Session&> (session).selectedTrack();
    if (t == nullptr) return {};
    return juce::String ((int) t->id) + ":" + juce::String ((int) t->sound.amp) + ":" + juce::String ((int) t->sound.filter)
         + ":" + juce::String ((int) t->sound.stereo);
}

void SoundPanel::refresh()
{
    auto* t = session.selectedTrack();
    if (t == nullptr) return;
    if (t->id != shownTrack)
    {
        shownTrack = t->id;
        wave.editing = false;
        wave.selectedSlice = -1;
    }
    const bool slices = t->sound.play == PlayMode::Slices;
    if (! slices) wave.editing = false;
    const bool editing = wave.editing;

    editBtn.setVisible (slices);
    editBtn.text = editing ? "Done" : "Edit slices";
    editBtn.toggled = editing;
    chop.setVisible (editing);
    sens.setVisible (editing && t->sound.chop == ChopMode::Transients);
    parts.setVisible (editing && t->sound.chop == ChopMode::Equal);

    if (structureKey() != builtKey) rebuildBlocks();
    for (auto& k : blockKids) k->setVisible (! editing);
    for (auto* p : pills) p->onBg = trackColour (t->colour);

    resized();
    repaint();
    for (auto* c : getChildren()) c->repaint();
}

void SoundPanel::resized()
{
    auto r = getLocalBounds().reduced (16, 14);
    auto head = r.removeFromTop (34);
    swatchArea = head.removeFromLeft (10).withSizeKeepingCentre (10, 22);
    head.removeFromLeft (12);
    auto* t = session.selectedTrack();
    int nw = t ? juce::GlyphArrangement::getStringWidthInt (sans (15.0f, 600), t->name) + 4 : 10;
    nameArea = head.removeFromLeft (juce::jmin (nw, 360));
    head.removeFromLeft (18);
    tabs.setBounds (head.removeFromLeft (tabs.idealWidth()));
    close.setBounds (head.removeFromRight (28).withSizeKeepingCentre (28, 28));
    r.removeFromTop (10);

    // The wave stays where it is in both modes: the slice count sits next to the tabs,
    // Edit slices in the top right corner of the wave, the CHOP row under the wave
    const bool slices = t && t->sound.play == PlayMode::Slices;
    head.removeFromLeft (14);
    infoArea = slices ? head.removeFromLeft (100) : juce::Rectangle<int>();

    wave.setBounds (r.removeFromTop ((wave.editing ? 158 : 90) + 4 + WaveView::kTicksH));
    {
        const int bw = editBtn.idealWidth (14);
        editBtn.setBounds (wave.getRight() - 6 - bw, wave.getY() + 20, bw, 24);   // under the slice number tabs
    }
    r.removeFromTop (10);

    if (wave.editing)
    {
        auto row = r.removeFromTop (30);
        chopLabel = row.removeFromLeft (juce::GlyphArrangement::getStringWidthInt (caps (11.0f), "CHOP") + 2);
        row.removeFromLeft (12);
        chop.setBounds (row.removeFromLeft (chop.idealWidth()).withSizeKeepingCentre (chop.idealWidth(), 32));
        row.removeFromLeft (12);
        sensLabel = partsLabel = hintArea = {};
        if (t && t->sound.chop == ChopMode::Transients)
        {
            sensLabel = row.removeFromLeft (70);
            sens.setBounds (row.removeFromLeft (52).withSizeKeepingCentre (52, 26));
        }
        else if (t && t->sound.chop == ChopMode::Equal)
        {
            partsLabel = row.removeFromLeft (36);
            parts.setBounds (row.removeFromLeft (56).withSizeKeepingCentre (56, 26));
            row.removeFromLeft (10);
            hintArea = row;
        }
        else hintArea = row;
        r.removeFromTop (10);
    }

    blocksArea = r.withHeight (120);

    for (size_t i = 0; i < blockKids.size(); ++i)
        blockKids[i]->setBounds (blockKids[i]->getProperties()["rx"].operator int() + blocksArea.getX(),
                                 blockKids[i]->getProperties()["ry"].operator int() + blocksArea.getY(),
                                 blockKids[i]->getProperties()["rw"].operator int(),
                                 blockKids[i]->getProperties()["rh"].operator int());
}

void SoundPanel::paint (juce::Graphics& g)
{
    g.fillAll (C::panel);
    g.setColour (C::border);
    g.fillRect (0, 0, getWidth(), 1);
    auto* t = session.selectedTrack();
    if (t == nullptr) return;

    g.setColour (trackColour (t->colour));
    g.fillRoundedRectangle (swatchArea.toFloat(), 3.0f);
    g.setColour (C::text);
    g.setFont (sans (15.0f, 600));
    g.drawText (t->name, nameArea, juce::Justification::centredLeft, true);

    if (! infoArea.isEmpty())
    {
        g.setColour (C::text);
        g.setFont (mono (12.0f));
        int n = (int) t->sound.slices.size();
        g.drawText (juce::String (n) + (n == 1 ? " slice" : " slices"), infoArea, juce::Justification::centredLeft, false);
    }

    if (wave.editing)
    {
        g.setColour (C::muted);
        g.setFont (caps (11.0f));
        g.drawText ("CHOP", chopLabel, juce::Justification::centredLeft, false);
        g.setFont (sans (12.0f));
        if (! sensLabel.isEmpty()) g.drawText ("Sensitivity", sensLabel, juce::Justification::centredLeft, false);
        if (! partsLabel.isEmpty()) g.drawText ("Parts", partsLabel, juce::Justification::centredLeft, false);
        if (! hintArea.isEmpty())
            g.drawText (t->sound.chop == ChopMode::Equal ? juce::String::fromUTF8 ("2\xe2\x80\x93" "20: type, scroll or use the arrows")
                                                         : juce::String::fromUTF8 ("Double-click to add a marker \xc2\xb7 drag to move \xc2\xb7 Alt-click to remove"),
                        hintArea, juce::Justification::centredLeft, true);
        return;
    }

    for (auto& b : blocks)
    {
        auto r = b.bounds.translated (blocksArea.getX(), blocksArea.getY()).toFloat();
        g.setColour (C::block);
        g.fillRoundedRectangle (r, 8.0f);
        g.setColour (C::blockBorder);
        g.drawRoundedRectangle (r.reduced (0.5f), 8.0f, 1.0f);
        g.setColour (C::text);
        g.setFont (caps (11.0f, 700));
        g.drawText (b.title, juce::Rectangle<float> (r.getX() + 9, r.getY() + 7, 90, 20), juce::Justification::centredLeft, false);
        if (b.note.isNotEmpty())
        {
            g.setColour (C::muted);
            g.setFont (sans (12.0f));
            g.drawText (b.note, r.withTrimmedTop (33).reduced (8, 6), juce::Justification::centred, false);
        }
    }
}

//==============================================================================
static void place (juce::Component& c, juce::Rectangle<int> r)
{
    c.getProperties().set ("rx", r.getX());
    c.getProperties().set ("ry", r.getY());
    c.getProperties().set ("rw", r.getWidth());
    c.getProperties().set ("rh", r.getHeight());
}

Knob& SoundPanel::addKnob (const juce::String& label, int min, int max, int def,
                          std::function<int& (Track&)> ref, std::function<juce::String (int)> fmt, bool bipolar)
{
    auto k = std::make_unique<Knob> (session);
    k->label = label;
    k->min = min; k->max = max; k->def = def;
    k->bipolar = bipolar;
    k->get = [this, ref, def] { auto* t = session.selectedTrack(); return t ? ref (*t) : def; };
    k->set = [this, ref] (int v) { if (auto* t = session.selectedTrack()) { ref (*t) = v; session.changed(); } };
    k->format = fmt;
    k->colour = [this] { auto* t = session.selectedTrack(); return t ? trackColour (t->colour) : C::orange; };
    k->setTooltip (label + ". Drag up or down or scroll, double-click resets");
    auto& out = *k;
    addAndMakeVisible (*k);
    blockKids.push_back (std::move (k));
    return out;
}

Btn& SoundPanel::addPill (const juce::String& text, std::function<bool& (Track&)> ref, const juce::String& tip)
{
    auto b = std::make_unique<Btn>();
    auto* raw = b.get();
    b->text = text;
    b->font = sans (8.0f, 700).withExtraKerningFactor (0.03f);
    b->radius = 3.0f;
    b->bg = b->bgHover = C::hover;
    b->textCol = C::muted;
    b->onBg = trackColour (0);
    b->isOn = [this, ref] { auto* t = session.selectedTrack(); return t != nullptr && ref (*t); };
    pills.push_back (raw);
    b->onClick = [this, ref, raw]
    {
        if (auto* t = session.selectedTrack())
        {
            session.checkpoint();
            ref (*t) = ! ref (*t);
            session.changed();
            raw->repaint();
        }
    };
    b->setTooltip (tip);
    addAndMakeVisible (*b);
    blockKids.push_back (std::move (b));
    return *raw;
}

Btn& SoundPanel::addMenu (std::function<juce::String()> text, std::function<void (Btn&)> open)
{
    auto b = std::make_unique<Btn>();
    auto* raw = b.get();
    b->font = sans (11.0f);
    b->radius = 4.0f;
    b->bg = C::btn2;
    b->just = juce::Justification::centredLeft;
    b->padX = 6;
    b->chevron = true;
    b->textFn = std::move (text);
    b->onClick = [raw, open] { open (*raw); };
    addAndMakeVisible (*b);
    blockKids.push_back (std::move (b));
    return *raw;
}

void SoundPanel::rebuildBlocks()
{
    blockKids.clear();
    blocks.clear();
    pills.clear();
    builtKey = structureKey();
    auto* t = session.selectedTrack();
    if (t == nullptr) return;

    const int cellW = 82, cellH = 38, colGap = 8, rowGap = 6;
    int bx = 0;
    auto newBlock = [&] (const juce::String& title, int cols)
    {
        Block b;
        b.title = title;
        b.cols = cols;
        int w = cols * cellW + (cols - 1) * colGap + 16 + 2;
        b.bounds = { bx, 0, w, 120 };
        bx += w + 8;
        blocks.push_back (b);
        return (int) blocks.size() - 1;
    };
    auto cell = [&] (int bi, int i)
    {
        auto& b = blocks[(size_t) bi];
        return juce::Rectangle<int> (b.bounds.getX() + 9 + (i % b.cols) * (cellW + colGap), 1 + 6 + 20 + 6 + (i / b.cols) * (cellH + rowGap), cellW, cellH);
    };
    auto header = [&] (int bi) { auto& b = blocks[(size_t) bi]; return juce::Rectangle<int> (b.bounds.getX() + 9, 7, b.bounds.getWidth() - 18, 20); };
    auto pct = [] (int v) { return juce::String (v) + "%"; };
    auto refresher = [this] { juce::MessageManager::callAsync ([sp = juce::Component::SafePointer<SoundPanel> (this)] { if (sp) sp->refresh(); }); };

    // PITCH
    {
        int bi = newBlock ("PITCH", 1);
        place (addKnob ("Tune", -24, 24, 0, [] (Track& x) -> int& { return x.sound.tune; }, [] (int v) { return sgn (v) + " st"; }, true), cell (bi, 0));
        place (addKnob ("Fine", -50, 50, 0, [] (Track& x) -> int& { return x.sound.fine; }, [] (int v) { return sgn (v) + " ct"; }, true), cell (bi, 1));
    }
    // SAMPLE
    {
        int bi = newBlock ("SAMPLE", 2);
        auto& m = addMenu ([this] { auto* tr = session.selectedTrack(); return tr && tr->sound.sampleMode == SampleMode::Repitch ? juce::String ("Repitch") : juce::String ("Keep pitch"); },
                           [this] (Btn& b)
                           {
                               juce::PopupMenu pm;
                               auto* tr = session.selectedTrack();
                               if (! tr) return;
                               pm.addItem (1, "Keep pitch", true, tr->sound.sampleMode == SampleMode::KeepPitch);
                               pm.addItem (2, "Repitch", true, tr->sound.sampleMode == SampleMode::Repitch);
                               showMenu (pm, &b, [this, &b] (int r)
                               {
                                   auto* t2 = session.selectedTrack();
                                   if (r <= 0 || ! t2) return;
                                   session.checkpoint();
                                   t2->sound.sampleMode = r == 1 ? SampleMode::KeepPitch : SampleMode::Repitch;
                                   session.changed();
                                   b.repaint();
                               });
                           });
        m.setTooltip ("Keep pitch: stretching keeps the pitch. Repitch: pitch follows the length");
        auto h = header (bi);
        place (m, { h.getRight() - 78, h.getY(), 78, 20 });
        place (addKnob ("Start", 0, 127, 0, [] (Track& x) -> int& { return x.sound.start; }), cell (bi, 0));
        place (addKnob ("Length", 1, 127, 127, [] (Track& x) -> int& { return x.sound.length; }), cell (bi, 1));
        place (addKnob ("Stretch", 25, 400, 100, [] (Track& x) -> int& { return x.sound.stretch; }, pct), cell (bi, 2));
        auto c = cell (bi, 3);
        auto& rev = addPill ("REVERSE", [] (Track& x) -> bool& { return x.sound.reverse; }, "Play the sample backwards");
        auto& swp = addPill ("SWAP L/R", [] (Track& x) -> bool& { return x.sound.swapLR; }, "Swap left and right channels");
        place (rev, { c.getX(), c.getY() + 3, 52, 14 });
        place (swp, { c.getX(), c.getY() + 22, 52, 14 });
    }
    // AMP
    {
        int bi = newBlock ("AMP", 2);
        auto h = header (bi);
        auto& cut = addPill ("CUT", [] (Track& x) -> bool& { return x.sound.cut; },
                             "CUT: the next hit on this track stops the tail of the previous one instead of layering over it");
        place (cut, { h.getX() + 34, h.getY() + 3, 26, 14 });
        auto& m = addMenu ([this] { auto* tr = session.selectedTrack(); return juce::String (ampModeName (tr ? tr->sound.amp : AmpMode::OneShot)); },
                           [this, refresher] (Btn& b)
                           {
                               juce::PopupMenu pm;
                               auto* tr = session.selectedTrack();
                               if (! tr) return;
                               for (int i = 0; i < 3; ++i) pm.addItem (i + 1, ampModeName ((AmpMode) i), true, (int) tr->sound.amp == i);
                               showMenu (pm, &b, [this, refresher] (int r)
                               {
                                   auto* t2 = session.selectedTrack();
                                   if (r <= 0 || ! t2) return;
                                   session.checkpoint();
                                   t2->sound.amp = (AmpMode) (r - 1);
                                   session.changed();
                                   refresher();
                               });
                           });
        place (m, { h.getRight() - 78, h.getY(), 78, 20 });
        if (t->sound.amp == AmpMode::OneShot)
        {
            // Fades like IN / OUT of a sampler: IN from Start, OUT up to the end set by Length (in each slice in Slices)
            auto& in = addKnob ("In", 0, 100, 0, [] (Track& x) -> int& { return x.sound.fadeIn; }, pct);
            in.setTooltip ("Fade in from the Start point, in % of the part that plays. In Slices: inside each slice. Drag or scroll, double-click resets");
            place (in, cell (bi, 0));
            auto& outK = addKnob ("Out", 0, 100, 0, [] (Track& x) -> int& { return x.sound.fadeOut; }, pct);
            outK.setTooltip ("Fade out to the end set by Length, in % of the part that plays. In Slices: inside each slice. Drag or scroll, double-click resets");
            place (outK, cell (bi, 2));
        }
        else if (t->sound.amp == AmpMode::AHD)
        {
            place (addKnob ("Attack", 0, 127, 0, [] (Track& x) -> int& { return x.sound.attack; }), cell (bi, 0));
            place (addKnob ("Hold", 0, 127, 12, [] (Track& x) -> int& { return x.sound.hold; }), cell (bi, 1));
            place (addKnob ("Decay", 0, 127, 48, [] (Track& x) -> int& { return x.sound.decay; }), cell (bi, 2));
        }
        else
        {
            place (addKnob ("Attack", 0, 127, 0, [] (Track& x) -> int& { return x.sound.attack; }), cell (bi, 0));
            place (addKnob ("Decay", 0, 127, 48, [] (Track& x) -> int& { return x.sound.decay; }), cell (bi, 1));
            place (addKnob ("Sustain", 0, 127, 90, [] (Track& x) -> int& { return x.sound.sustain; }), cell (bi, 2));
            place (addKnob ("Release", 0, 127, 30, [] (Track& x) -> int& { return x.sound.release; }), cell (bi, 3));
        }
    }
    // TRANSIENT
    {
        int bi = newBlock ("TRANSIENT", 1);
        place (addKnob ("Punch", -100, 100, 0, [] (Track& x) -> int& { return x.sound.punch; }, sgn, true), cell (bi, 0));
        place (addKnob ("Tail", -100, 100, 0, [] (Track& x) -> int& { return x.sound.tail; }, sgn, true), cell (bi, 1));
    }
    // FILTER
    {
        int bi = newBlock ("FILTER", 3);
        auto h = header (bi);
        auto& m = addMenu ([this] { auto* tr = session.selectedTrack(); return juce::String (filterName (tr ? tr->sound.filter : FilterType::LP24)); },
                           [this, refresher] (Btn& b)
                           {
                               juce::PopupMenu pm;
                               auto* tr = session.selectedTrack();
                               if (! tr) return;
                               for (int i = 0; i < (int) FilterType::Count; ++i) pm.addItem (i + 1, filterName ((FilterType) i), true, (int) tr->sound.filter == i);
                               showMenu (pm, &b, [this, refresher] (int r)
                               {
                                   auto* t2 = session.selectedTrack();
                                   if (r <= 0 || ! t2) return;
                                   session.checkpoint();
                                   t2->sound.filter = (FilterType) (r - 1);
                                   session.changed();
                                   refresher();
                               }, 140);
                           });
        place (m, { h.getRight() - 92, h.getY(), 92, 20 });
        const auto f = t->sound.filter;
        juce::String l1 = "Cutoff", l2 = "Reso";
        if (f == FilterType::BP || f == FilterType::Notch) { l1 = "Freq"; l2 = "Width"; }
        if (f == FilterType::Comb) { l1 = "Pitch"; l2 = "Feedback"; }
        if (f == FilterType::Formant) { l1 = "Vowel"; l2 = "Sharp"; }
        std::function<juce::String (int)> cutFmt;
        if (f == FilterType::Formant) cutFmt = [] (int v) { static const char* V[] = { "A", "E", "I", "O", "U" }; return juce::String (V[juce::jmin (4, (int) (v / 25.6))]); };
        place (addKnob (l1, 0, 127, 127, [] (Track& x) -> int& { return x.sound.cutoff; }, cutFmt), cell (bi, 0));
        place (addKnob ("Env amt", -64, 63, 0, [] (Track& x) -> int& { return x.sound.envAmt; }, sgn, true), cell (bi, 1));
        if (f == FilterType::Morph)
            place (addKnob ("Morph", 0, 127, 0, [] (Track& x) -> int& { return x.sound.morph; },
                            [] (int v) { return juce::String (v < 43 ? "LP" : (v < 85 ? "BP" : "HP")); }), cell (bi, 2));
        else
            place (addKnob ("Base HP", 0, 127, 0, [] (Track& x) -> int& { return x.sound.baseHP; }), cell (bi, 2));
        place (addKnob (l2, 0, 127, 0, [] (Track& x) -> int& { return x.sound.reso; }), cell (bi, 3));
        place (addKnob ("Env dec", 0, 127, 48, [] (Track& x) -> int& { return x.sound.envDec; }), cell (bi, 4));
        place (addKnob ("Base LP", 0, 127, 127, [] (Track& x) -> int& { return x.sound.baseLP; }), cell (bi, 5));
    }
    // CHARACTER
    {
        int bi = newBlock ("CHARACTER", 3);
        auto gc = cell (bi, 0);
        auto& gain = addKnob ("Gain", -12, 24, 0, [] (Track& x) -> int& { return x.sound.gain; }, [] (int v) { return sgn (v) + " dB"; });
        place (gain, gc);
        auto& autoPill = addPill ("AUTO", [] (Track& x) -> bool& { return x.sound.autoGain; },
                                  "Auto: louder Drive or Gain is compensated so the level stays the same");
        autoPill.font = sans (7.0f, 700);
        autoPill.radius = 2.0f;
        place (autoPill, { gc.getX() + 18 - 13, gc.getY() + 33, 26, 10 });
        place (addKnob ("Bit red.", 0, 127, 0, [] (Track& x) -> int& { return x.sound.bitRed; }), cell (bi, 1));

        auto sc = cell (bi, 2);
        auto& stereo = addMenu ([this] { auto* tr = session.selectedTrack(); return juce::String (stereoName (tr ? tr->sound.stereo : StereoMode::Original)); },
                                [this, refresher] (Btn& b)
                                {
                                    juce::PopupMenu pm;
                                    auto* tr = session.selectedTrack();
                                    if (! tr) return;
                                    for (int i = 0; i < (int) StereoMode::Count; ++i) pm.addItem (i + 1, stereoName ((StereoMode) i), true, (int) tr->sound.stereo == i);
                                    showMenu (pm, &b, [this, refresher] (int r)
                                    {
                                        auto* t2 = session.selectedTrack();
                                        if (r <= 0 || ! t2) return;
                                        session.checkpoint();
                                        t2->sound.stereo = (StereoMode) (r - 1);
                                        session.changed();
                                        refresher();
                                    }, 130);
                                });
        stereo.setTooltip (juce::String::fromUTF8 ("Original: as in the file · Mono: summed · Narrow / Wide: stereo width · Haas: one side delayed a few ms · Alternate: hits go left and right in turn"));
        place (stereo, { sc.getX(), sc.getY() + 18, 82, 20 });
        auto lbl = std::make_unique<juce::Label> (juce::String(), "Stereo");
        lbl->setFont (sans (11.0f));
        lbl->setColour (juce::Label::textColourId, C::muted);
        lbl->setBorderSize ({ 0, 0, 0, 0 });
        lbl->setInterceptsMouseClicks (false, false);
        place (*lbl, { sc.getX(), sc.getY() + 1, 82, 14 });
        addAndMakeVisible (*lbl);
        blockKids.push_back (std::move (lbl));

        place (addKnob ("Drive", 0, 127, 0, [] (Track& x) -> int& { return x.sound.drive; }), cell (bi, 3));
        place (addKnob ("SR red.", 0, 127, 0, [] (Track& x) -> int& { return x.sound.srRed; }), cell (bi, 4));

        const auto mode = t->sound.stereo;
        const int mi = (int) mode;
        if (mode == StereoMode::Original || mode == StereoMode::Mono)
        {
            auto& k = addKnob ("Amt", 0, 1, 0, [] (Track& x) -> int& { return x.sound.stereoAmt[0]; }, [] (int) { return juce::String::fromUTF8 ("\xe2\x80\x94"); });
            k.active = false;
            k.setTooltip ("Not used by " + juce::String (stereoName (mode)));
            place (k, cell (bi, 5));
        }
        else
        {
            juce::String label = mode == StereoMode::Haas ? "Delay" : (mode == StereoMode::Alternate ? "Spread" : "Width");
            int lo = mode == StereoMode::Wide ? 100 : (mode == StereoMode::Haas ? 1 : 0);
            int hi = mode == StereoMode::Wide ? 200 : (mode == StereoMode::Haas ? 30 : 100);
            auto fmt = mode == StereoMode::Haas ? std::function<juce::String (int)> ([] (int v) { return juce::String (v) + " ms"; })
                                                : std::function<juce::String (int)> ([] (int v) { return juce::String (v) + "%"; });
            place (addKnob (label, lo, hi, SoundSettings::defaultStereoAmt (mode),
                            [mi] (Track& x) -> int& { return x.sound.stereoAmt[(size_t) mi]; }, fmt), cell (bi, 5));
        }
    }

}

} // namespace ok::ui
