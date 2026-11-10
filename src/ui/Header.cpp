#include "Header.h"

namespace ok::ui
{

static juce::String partName (int i) { return juce::String::charToString ((juce::juce_wchar) ('A' + i)); }

static void strokeSvg (juce::Graphics& g, juce::Rectangle<float> r, const char* svg, float box, juce::Colour c, float width = 1.5f)
{
    auto p = juce::Drawable::parseSVGPath (svg);
    float s = juce::jmin (r.getWidth(), r.getHeight()) / box;
    auto size = box * s;
    p.applyTransform (juce::AffineTransform::scale (s).translated (r.getCentreX() - size * 0.5f, r.getCentreY() - size * 0.5f));
    g.setColour (c);
    g.strokePath (p, juce::PathStrokeType (width, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

//==============================================================================
BpmBox::BpmBox (Session& s) : session (s)
{
    setTooltip ("Drag up / down or scroll. Shift for 0.1 steps. Double-click to type");
    setMouseCursor (juce::MouseCursor::UpDownResizeCursor);
}

void BpmBox::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (C::btn);
    g.fillRoundedRectangle (r, 7.0f);
    if (editor != nullptr) return;
    auto txt = juce::String (session.bpm(), 1);
    auto f1 = mono (13.0f);
    auto f2 = sans (11.0f);
    int w1 = juce::GlyphArrangement::getStringWidthInt (f1, txt), w2 = juce::GlyphArrangement::getStringWidthInt (f2, "BPM");
    float x = r.getCentreX() - (w1 + 5 + w2) * 0.5f;
    g.setColour (C::text);
    g.setFont (f1);
    g.drawText (txt, juce::Rectangle<float> (x, 0, (float) w1 + 2, r.getHeight()), juce::Justification::centredLeft, false);
    g.setColour (C::muted);
    g.setFont (f2);
    g.drawText ("BPM", juce::Rectangle<float> (x + w1 + 5, 0, (float) w2 + 2, r.getHeight()), juce::Justification::centredLeft, false);
}

void BpmBox::mouseDown (const juce::MouseEvent&) { startBpm = session.bpm(); }

void BpmBox::mouseDrag (const juce::MouseEvent& e)
{
    double step = e.mods.isShiftDown() ? 0.1 : 1.0;
    double dy = -e.getDistanceFromDragStartY() / 3.0;
    session.setBpm (startBpm + std::round (dy) * step);
    repaint();
}

void BpmBox::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    wheelAcc += w.deltaY * (w.isReversed ? -1.0f : 1.0f);
    int steps = (int) (wheelAcc / 0.07f);
    if (steps == 0) return;
    wheelAcc -= steps * 0.07f;
    session.setBpm (session.bpm() + steps * (e.mods.isShiftDown() ? 0.1 : 1.0));
    repaint();
}

void BpmBox::mouseDoubleClick (const juce::MouseEvent&)
{
    editor = std::make_unique<juce::TextEditor>();
    editor->setFont (mono (13.0f));
    editor->setText (juce::String (session.bpm(), 1), false);
    editor->setJustification (juce::Justification::centredLeft);
    editor->setIndents (8, 6);
    editor->setInputRestrictions (6, "0123456789.,");
    editor->onReturnKey = [this] { commit(); };
    editor->onEscapeKey = [this] { juce::MessageManager::callAsync ([sp = juce::Component::SafePointer<BpmBox> (this)] { if (sp) { sp->editor = nullptr; sp->repaint(); } }); };
    editor->onFocusLost = [this] { commit(); };
    addAndMakeVisible (*editor);
    resized();
    editor->grabKeyboardFocus();
    editor->selectAll();
    repaint();
}

void BpmBox::commit()
{
    if (editor == nullptr) return;
    auto v = editor->getText().replaceCharacter (',', '.').getDoubleValue();
    if (v > 0) session.setBpm (v);
    juce::MessageManager::callAsync ([sp = juce::Component::SafePointer<BpmBox> (this)] { if (sp) { sp->editor = nullptr; sp->repaint(); } });
}

void BpmBox::resized()
{
    if (editor) editor->setBounds (getLocalBounds());
}

//==============================================================================
Header::Header (Session& s, UiHost& h) : session (s), host (h), bpm (s), master (s)
{
    pattern.onClick = [this] { showPatternMenu(); };
    pattern.icon = [this] (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour)
    {
        float x = r.getX() + 12;
        g.setColour (C::muted);
        g.setFont (caps (11.0f, 400));
        int w = juce::GlyphArrangement::getStringWidthInt (caps (11.0f, 400), "PATTERN");
        g.drawText ("PATTERN", juce::Rectangle<float> (x, r.getY(), (float) w + 2, r.getHeight()), juce::Justification::centredLeft, false);
        x += w + 10;
        g.setColour (C::text);
        g.setFont (sans (14.0f, 600));
        int nw = juce::GlyphArrangement::getStringWidthInt (sans (14.0f, 600), session.doc.name);
        g.drawText (session.doc.name, juce::Rectangle<float> (x, r.getY(), (float) nw + 2, r.getHeight()), juce::Justification::centredLeft, false);
        x += nw + 10;
        if (session.dirty)
        {
            g.setColour (C::orange);
            g.fillEllipse (x, r.getCentreY() - 3.5f, 7.0f, 7.0f);
            x += 17;
        }
        drawChevron (g, { x + 5, r.getCentreY() }, C::text2, 10.0f);
    };
    pattern.setTooltip ("Pattern: save, load, new");
    addAndMakeVisible (pattern);

    undoBtn.icon = [] (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour)
    { strokeSvg (g, r.withSizeKeepingCentre (16, 16), "M5 4L2 7L5 10M2 7H10A4 4 0 0 1 10 15H7", 16, C::text2); };
    redoBtn.icon = [] (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour)
    { strokeSvg (g, r.withSizeKeepingCentre (16, 16), "M11 4L14 7L11 10M14 7H6A4 4 0 0 0 6 15H9", 16, C::text2); };
    undoBtn.radius = redoBtn.radius = 7.0f;
    undoBtn.onClick = [this] { session.undo(); };
    redoBtn.onClick = [this] { session.redo(); };
    undoBtn.setTooltip ("Undo  (Ctrl Z)");
    redoBtn.setTooltip ("Redo  (Ctrl Shift Z)");
    addAndMakeVisible (undoBtn);
    addAndMakeVisible (redoBtn);

    for (int i = 0; i < kNumParts; ++i)
    {
        auto& b = parts[i];
        b.radius = 7.0f;
        b.icon = [this, i] (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour)
        {
            auto& p = session.doc.parts[(size_t) i];
            bool cur = session.part == i, q = session.queuedPart() == i;
            if (cur) { g.setColour (C::orange); g.fillRoundedRectangle (r, 7.0f); }
            else if (p.exists) { g.setColour (parts[i].isMouseOver() ? C::hover : C::btn); g.fillRoundedRectangle (r, 7.0f); }
            else { g.setColour (C::menuBorder); g.drawRoundedRectangle (r.reduced (0.5f), 7.0f, 1.0f); }
            if (q) { g.setColour (C::orange); g.drawRoundedRectangle (r.reduced (1.0f), 7.0f, 2.0f); }
            auto ink = cur ? C::ink : (p.exists ? C::text : C::muted);
            g.setColour (ink);
            g.setFont (sans (14.0f, 700));
            g.drawText (partName (i), r.withHeight (r.getHeight() * 0.58f).withTrimmedTop (4), juce::Justification::centredBottom, false);
            g.setColour (ink.withAlpha (0.8f));
            g.setFont (mono (9.0f));
            juce::String len = p.exists ? juce::String (p.bars) + (p.bars == 1 ? " bar" : " bars") : juce::String ("empty");
            g.drawText (len, r.withTrimmedTop (r.getHeight() * 0.58f).withTrimmedBottom (4), juce::Justification::centredTop, false);
        };
        b.bg = b.bgHover = juce::Colours::transparentBlack;
        b.onClick = [this, i] { session.pickPart (i); };
        b.onRightClick = [this, i] { showPartMenu (i); };
        b.setTooltip ("Part " + partName (i) + juce::String::fromUTF8 (" · right-click: copy, paste, clear"));
        addAndMakeVisible (b);
    }

    switchSeg.items = { "Now", "Bar end", "Part end" };
    switchSeg.get = [this] { return (int) session.switchMode(); };
    switchSeg.set = [this] (int i) { session.setSwitchMode ((SwitchMode) i); };
    addAndMakeVisible (switchSeg);

    queuePill.bg = queuePill.bgHover = C::orange;
    queuePill.textCol = C::ink;
    queuePill.radius = 13.0f;
    queuePill.font = sans (12.0f, 700);
    queuePill.just = juce::Justification::centredLeft;
    queuePill.padX = 10;
    queuePill.icon = nullptr;
    queuePill.onClick = [this] { session.cancelQueue(); };
    queuePill.setTooltip ("Cancel the queued part");
    addChildComponent (queuePill);

    toastUndo.text = "Undo";
    toastUndo.textCol = C::orange;
    toastUndo.radius = 5.0f;
    toastUndo.onClick = [this] { session.undo(); };
    addChildComponent (toastUndo);

    play.radius = 7.0f;
    play.icon = [this] (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour)
    {
        auto c = r.getCentre();
        if (session.isPlaying())
        {
            g.setColour (C::ink);
            g.fillRoundedRectangle (juce::Rectangle<float> (8, 8).withCentre (c), 1.0f);
        }
        else
        {
            juce::Path p;
            p.addTriangle (c.x - 3.5f, c.y - 4.5f, c.x + 4.5f, c.y, c.x - 3.5f, c.y + 4.5f);
            g.setColour (C::text);
            g.fillPath (p);
        }
    };
    play.onClick = [this] { session.togglePlay(); };
    play.setTooltip ("Play / Stop  (Space)");

    tap.text = "Tap";
    tap.radius = 7.0f;
    tap.onClick = [this] { session.tap(); };
    tap.setTooltip (juce::String::fromUTF8 ("Tap tempo · a pause over 2 s starts a new count"));

    metro.radius = 7.0f;
    metro.icon = [this] (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour)
    {
        auto ink = session.engine.metronomeOn.load() ? C::ink : C::text;
        auto b = r.withSizeKeepingCentre (16, 16);
        strokeSvg (g, b, "M5.2 1.8h5.6L14 14.5H2Z", 16, ink);
        strokeSvg (g, b, "M8 11L11.8 4.2", 16, ink);
        g.setColour (ink);
        g.fillEllipse (b.getX() + 10.6f - 1.3f, b.getY() + 6.3f - 1.3f, 2.6f, 2.6f);
    };
    metro.onClick = [this] { session.engine.metronomeOn = ! session.engine.metronomeOn.load(); refresh(); };
    metro.setTooltip (juce::String::fromUTF8 ("Metronome · volume, time signature and accent in Settings"));

    hostBtn.radius = 7.0f;
    hostBtn.icon = [this] (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour)
    {
        float x = r.getX() + 10;
        g.setColour (session.isPlaying() ? C::teal : C::dim);
        g.fillEllipse (x, r.getCentreY() - 4, 8, 8);
        x += 16;
        auto t = juce::String (session.engine.currentBpm.load(), 1);
        g.setFont (mono (13.0f));
        g.setColour (C::muted);
        int w = juce::GlyphArrangement::getStringWidthInt (mono (13.0f), t);
        g.drawText (t, juce::Rectangle<float> (x, r.getY(), (float) w + 2, r.getHeight()), juce::Justification::centredLeft, false);
        x += w + 8;
        g.setFont (sans (12.0f));
        g.setColour (C::dim);
        g.drawText (juce::String::fromUTF8 ("BPM \xc2\xb7 from host"), juce::Rectangle<float> (x, r.getY(), 120, r.getHeight()), juce::Justification::centredLeft, false);
    };
    hostBtn.setTooltip ("Tempo and play / stop come from the DAW");

    if (session.isPlugin) addAndMakeVisible (hostBtn);
    else
    {
        addAndMakeVisible (play);
        addAndMakeVisible (bpm);
        addAndMakeVisible (tap);
        addAndMakeVisible (metro);
    }

    master.knobPx = 30;
    master.label = "MASTER";
    master.labelSize = 10.0f;
    master.valueSize = 12.0f;
    master.min = 0;
    master.max = 127;
    master.def = 100;
    master.colour = [] { return C::text; };
    master.get = [this] { return session.master; };
    master.set = [this] (int v) { session.setMaster (v); };
    master.format = [] (int v) { return Session::masterText (v); };
    master.setTooltip ("Master volume. Drag up / down or scroll, double-click resets");
    addAndMakeVisible (master);

    gear.radius = 7.0f;
    gear.icon = [] (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour)
    {
        auto b = r.withSizeKeepingCentre (16, 16);
        strokeSvg (g, b, "M12 8.5a3.5 3.5 0 1 0 0 7a3.5 3.5 0 0 0 0-7Z", 24, C::text, 1.4f);
        strokeSvg (g, b, "M19.4 13.5a7.6 7.6 0 0 0 0-3l2-1.6l-2-3.4l-2.4 1a7.7 7.7 0 0 0-2.6-1.5L14 2.5h-4l-.4 2.5A7.7 7.7 0 0 0 7 6.5l-2.4-1l-2 3.4l2 1.6a7.6 7.6 0 0 0 0 3l-2 1.6l2 3.4l2.4-1a7.7 7.7 0 0 0 2.6 1.5l.4 2.5h4l.4-2.5a7.7 7.7 0 0 0 2.6-1.5l2.4 1l2-3.4Z", 24, C::text, 1.2f);
    };
    gear.onClick = [this] { host.toggleSettings(); };
    gear.setTooltip ("Settings");
    addAndMakeVisible (gear);

    refresh();
}

void Header::refresh()
{
    const int q = session.queuedPart();
    queuePill.setVisible (q >= 0);
    if (q >= 0)
    {
        auto mode = session.switchMode();
        queuePill.text = juce::String::fromUTF8 ("\xe2\x86\x92 ") + partName (q)
                       + (mode == SwitchMode::BarEnd ? " at bar end" : mode == SwitchMode::PartEnd ? " at part end" : " now")
                       + juce::String::fromUTF8 ("  \xc3\x97");
    }
    toastUndo.setVisible (session.toastHasUndo && session.toastText.isNotEmpty());
    undoBtn.dimmed = ! session.canUndo();
    redoBtn.dimmed = ! session.canRedo();

    play.bg = play.bgHover = session.isPlaying() ? C::teal : C::btn;
    if (! session.isPlaying()) play.bgHover = C::hover;
    play.toggled = false;
    const bool m = session.engine.metronomeOn.load();
    metro.bg = m ? C::text : C::btn;
    metro.bgHover = m ? C::text : C::hover;

    resized();
    repaint();
    for (auto* c : getChildren()) c->repaint();
}

void Header::resized()
{
    const int h = getHeight();
    auto centreY = [h] (int height) { return (h - height) / 2; };
    seps.clear();

    int x = 16;
    auto logoFont = sans (16.0f, 700).withExtraKerningFactor (0.2f);
    int lw = juce::GlyphArrangement::getStringWidthInt (logoFont, "OKTAKT") + 2;
    logoArea = { x, 0, lw, h };
    x += lw + 10;

    int pw = 12 + juce::GlyphArrangement::getStringWidthInt (caps (11.0f, 400), "PATTERN") + 10
           + juce::GlyphArrangement::getStringWidthInt (sans (14.0f, 600), session.doc.name) + 10
           + (session.dirty ? 17 : 0) + 10 + 12;
    pattern.setBounds (x, centreY (34), pw, 34);
    x += pw + 10;

    undoBtn.setBounds (x, centreY (30), 30, 30);
    redoBtn.setBounds (x + 34, centreY (30), 30, 30);
    x += 64 + 10;
    seps.push_back ({ x, centreY (24), 1, 24 });
    x += 1 + 10;

    int plw = juce::GlyphArrangement::getStringWidthInt (caps (11.0f, 400), "PART") + 2;
    partLabel = { x, 0, plw, h };
    x += plw + 6;
    for (int i = 0; i < kNumParts; ++i)
    {
        parts[i].setBounds (x, centreY (38), 48, 38);
        x += 48 + 6;
    }
    x += 4;

    int slw = juce::GlyphArrangement::getStringWidthInt (caps (11.0f, 400), "SWITCH") + 2;
    switchLabel = { x, 0, slw, h };
    x += slw + 6;
    switchSeg.padX = 10;
    int sw = switchSeg.idealWidth();
    switchSeg.setBounds (x, centreY (32), sw, 32);
    x += sw + 10;

    if (queuePill.isVisible())
    {
        int qw = queuePill.idealWidth (10);
        queuePill.setBounds (x, centreY (26), qw, 26);
        x += qw + 10;
    }
    const int leftEnd = x;

    // From the right
    int r = getWidth() - 16;
    gear.setBounds (r - 30, centreY (30), 30, 30);
    r -= 30 + 10;
    int mw = 30 + 6 + 62;
    master.setBounds (r - mw, centreY (34), mw, 34);
    r -= mw + 10;
    seps.push_back ({ r - 1, centreY (24), 1, 24 });
    r -= 1 + 10;

    if (session.isPlugin)
    {
        int hw = 10 + 16 + juce::GlyphArrangement::getStringWidthInt (mono (13.0f), "124.0") + 8
               + juce::GlyphArrangement::getStringWidthInt (sans (12.0f), juce::String::fromUTF8 ("BPM \xc2\xb7 from host")) + 12;
        hostBtn.setBounds (r - hw, centreY (30), hw, 30);
        r -= hw + 10;
    }
    else
    {
        metro.setBounds (r - 30, centreY (30), 30, 30);
        r -= 30 + 6;
        int tw = tap.idealWidth (10);
        tap.setBounds (r - tw, centreY (30), tw, 30);
        r -= tw + 6;
        bpm.setBounds (r - 84, centreY (30), 84, 30);
        r -= 84 + 6;
        play.setBounds (r - 34, centreY (30), 34, 30);
        r -= 34 + 10;
    }
    seps.push_back ({ r - 1, centreY (24), 1, 24 });
    r -= 1 + 10;

    if (toastUndo.isVisible())
    {
        toastUndo.setBounds (r - 50, centreY (24), 50, 24);
        r -= 50 + 8;
    }
    toastArea = { leftEnd, 0, juce::jmax (0, r - leftEnd), h };
}

void Header::paint (juce::Graphics& g)
{
    g.fillAll (C::header);
    g.setColour (C::border);
    g.fillRect (0, getHeight() - 1, getWidth(), 1);

    g.setColour (C::orange);
    g.setFont (sans (16.0f, 700).withExtraKerningFactor (0.2f));
    g.drawText ("OKTAKT", logoArea, juce::Justification::centredLeft, false);

    g.setColour (C::muted);
    g.setFont (caps (11.0f, 400));
    g.drawText ("PART", partLabel, juce::Justification::centredLeft, false);
    g.drawText ("SWITCH", switchLabel, juce::Justification::centredLeft, false);

    g.setColour (C::sep);
    for (auto& s : seps) g.fillRect (s);

    if (session.toastText.isNotEmpty())
    {
        g.setColour (C::teal);
        g.setFont (sans (12.0f));
        g.drawText (session.toastText, toastArea, juce::Justification::centredRight, true);
    }
}

void Header::showPatternMenu()
{
    juce::PopupMenu m;
    m.addItem (3, "New pattern");
    juce::PopupMenu::Item save ("Save");
    save.itemID = 1;
    save.shortcutKeyDescription = "Ctrl S";
    m.addItem (save);
    m.addItem (2, juce::String::fromUTF8 ("Save as\xe2\x80\xa6"));
    m.addSeparator();
    m.addSectionHeader ("LOAD");
    auto files = session.libraryFiles();
    for (int i = 0; i < juce::jmin (40, files.size()); ++i)
    {
        juce::PopupMenu::Item it (files[i].getFileNameWithoutExtension());
        it.itemID = 100 + i;
        it.isTicked = files[i] == session.patternFile;
        it.shortcutKeyDescription = files[i].getLastModificationTime().formatted ("%d %b");
        m.addItem (it);
    }
    if (files.isEmpty()) m.addItem (99, "No saved patterns yet", false);
    m.addSeparator();
    m.addItem (97, juce::String::fromUTF8 ("Show patterns folder"));
    m.addItem (98, juce::String::fromUTF8 ("Saves all parts A\xe2\x80\x93" "D \xc2\xb7 samples stay linked, not copied"), false);

    showMenu (m, &pattern, [this, files] (int r)
    {
        if (r == 1) { if (! session.save()) host.promptSaveAs(); }
        else if (r == 2) host.promptSaveAs();
        else if (r == 97) session.libraryFolder().startAsProcess();
        else if (r == 3) host.confirmDiscard ([this] { session.newPattern(); });
        else if (r >= 100 && r - 100 < files.size())
        {
            auto f = files[r - 100];
            host.confirmDiscard ([this, f] { session.load (f); });
        }
    }, 260);
}

void Header::showPartMenu (int i)
{
    const bool exists = session.doc.parts[(size_t) i].exists;
    int existing = 0;
    for (auto& p : session.doc.parts) existing += p.exists ? 1 : 0;
    juce::PopupMenu m;
    m.addItem (1, "Copy part " + partName (i), exists);
    m.addItem (2, session.hasPartClip() ? "Paste into part " + partName (i) : juce::String ("Paste (nothing copied)"), session.hasPartClip());
    m.addItem (3, "Clear part " + partName (i), exists && existing > 1);
    showMenu (m, &parts[i], [this, i] (int r)
    {
        if (r == 1) session.copyPart (i);
        else if (r == 2) session.pastePart (i);
        else if (r == 3) session.clearPart (i);
    }, 180);
}

} // namespace ok::ui
