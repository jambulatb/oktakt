#include "Browser.h"

namespace ok::ui
{

static void drawFolder (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour c)
{
    auto p = juce::Drawable::parseSVGPath ("M1.5 3.5H5.5L7 5H12.5V11.5H1.5Z");
    p.applyTransform (juce::AffineTransform::translation (r.getX(), r.getY()));
    g.setColour (c);
    g.strokePath (p, juce::PathStrokeType (1.3f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

static juce::String prettyPath (const juce::File& f)
{
    auto home = juce::File::getSpecialLocation (juce::File::userHomeDirectory).getFullPathName();
    auto p = f.getFullPathName();
    if (p.startsWith (home)) p = "~" + p.substring (home.length());
    return p;
}

Browser::Browser (Session& s, UiHost& h) : session (s), host (h)
{
    source.text = juce::String::fromUTF8 ("Source\xe2\x80\xa6");
    source.font = sans (11.0f, 600);
    source.radius = 5.0f;
    source.onClick = [this] { chooseRoot(); };
    source.setTooltip ("Choose the sample source folder");
    addAndMakeVisible (source);

    search.setTextToShowWhenEmpty ("Search in this folder", C::dim);
    search.setFont (sans (13.0f));
    search.setIndents (10, 7);
    search.setColour (juce::TextEditor::outlineColourId, C::blockBorder);
    search.setColour (juce::TextEditor::focusedOutlineColourId, C::orange);
    search.onTextChange = [this] { rebuild(); };
    search.onEscapeKey = [this] { search.clear(); rebuild(); };
    addAndMakeVisible (search);

    viewport.setViewedComponent (&list, false);
    viewport.setScrollBarsShown (true, false);
    viewport.setScrollBarThickness (8);
    addAndMakeVisible (viewport);
    refresh();
}

void Browser::refresh()
{
    if (shownRoot != session.browserRoot)
    {
        shownRoot = session.browserRoot;
        cache.clear();
        openDirs.clear();
        rebuild();
    }
    list.repaint();
    repaint();
}

const std::vector<juce::File>& Browser::childrenOf (const juce::File& dir)
{
    auto key = dir.getFullPathName();
    auto it = cache.find (key);
    if (it != cache.end()) return it->second;

    std::vector<juce::File> dirs, files;
    for (const auto& entry : juce::RangedDirectoryIterator (dir, false, "*", juce::File::findFilesAndDirectories))
    {
        auto f = entry.getFile();
        if (entry.isHidden() || f.getFileName().startsWithChar ('.')) continue;
        if (entry.isDirectory()) dirs.push_back (f);
        else if (session.samples.isAudioFile (f)) files.push_back (f);
    }
    auto byName = [] (const juce::File& a, const juce::File& b) { return a.getFileName().compareNatural (b.getFileName()) < 0; };
    std::sort (dirs.begin(), dirs.end(), byName);
    std::sort (files.begin(), files.end(), byName);
    dirs.insert (dirs.end(), files.begin(), files.end());
    return cache[key] = dirs;
}

void Browser::walk (const juce::File& dir, int depth)
{
    for (auto& f : childrenOf (dir))
    {
        bool isDir = f.isDirectory();
        rows.push_back ({ f, isDir, depth });
        if (isDir && openDirs.count (f.getFullPathName())) walk (f, depth + 1);
    }
}

void Browser::rebuild()
{
    rows.clear();
    auto q = search.getText().trim().toLowerCase();
    if (q.isEmpty())
        walk (session.browserRoot, 0);
    else
    {
        int visited = 0;
        for (const auto& entry : juce::RangedDirectoryIterator (session.browserRoot, true, "*", juce::File::findFiles))
        {
            if (++visited > 20000 || rows.size() > 400) break;
            auto f = entry.getFile();
            if (entry.isHidden()) continue;
            if (session.samples.isAudioFile (f) && f.getFileName().toLowerCase().contains (q))
                rows.push_back ({ f, false, 0 });
        }
    }
    list.setSize (juce::jmax (10, viewport.getMaximumVisibleWidth()), juce::jmax (1, (int) rows.size()) * kRowH + 8);
    list.repaint();
}

void Browser::chooseRoot()
{
    chooser = std::make_unique<juce::FileChooser> ("Sample source folder", session.browserRoot);
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                          [this] (const juce::FileChooser& fc)
                          {
                              auto r = fc.getResult();
                              if (r.isDirectory())
                              {
                                  session.browserRoot = r;
                                  refresh();
                              }
                          });
}

void Browser::resized()
{
    auto r = getLocalBounds().reduced (12);
    auto top = r.removeFromTop (22);
    source.setBounds (top.removeFromRight (source.idealWidth (8)));
    r.removeFromTop (8);
    r.removeFromTop (16);   // root path line
    r.removeFromTop (8);
    search.setBounds (r.removeFromTop (30));
    r.removeFromTop (8);
    viewport.setBounds (r);
    list.setSize (juce::jmax (10, viewport.getMaximumVisibleWidth()), juce::jmax (1, (int) rows.size()) * kRowH + 8);
}

void Browser::paint (juce::Graphics& g)
{
    g.fillAll (C::panel);
    g.setColour (C::border);
    g.fillRect (getWidth() - 1, 0, 1, getHeight());

    auto r = getLocalBounds().reduced (12);
    g.setColour (C::muted);
    g.setFont (caps (12.0f));
    g.drawText ("SAMPLES", r.removeFromTop (22), juce::Justification::centredLeft, false);
    r.removeFromTop (8);
    auto pathRow = r.removeFromTop (16);
    drawFolder (g, pathRow.removeFromLeft (14).toFloat().withY ((float) pathRow.getY() + 1), C::text2);
    pathRow.removeFromLeft (6);
    g.setColour (C::muted);
    g.setFont (mono (11.0f));
    g.drawText (prettyPath (session.browserRoot), pathRow, juce::Justification::centredLeft, true);
}

//==============================================================================
void Browser::List::paint (juce::Graphics& g)
{
    auto& rows = owner.rows;
    auto mouse = getMouseXYRelative();
    bool over = isMouseOver();
    if (rows.empty())
    {
        g.setColour (C::dim);
        g.setFont (sans (12.0f));
        g.drawText (owner.search.getText().isEmpty() ? "No samples in this folder" : "Nothing found",
                    getLocalBounds().removeFromTop (kRowH).reduced (8, 0), juce::Justification::centredLeft, false);
        return;
    }
    for (int i = 0; i < (int) rows.size(); ++i)
    {
        auto& row = rows[(size_t) i];
        auto r = juce::Rectangle<int> (0, i * kRowH, getWidth(), kRowH);
        if (r.getBottom() < g.getClipBounds().getY() || r.getY() > g.getClipBounds().getBottom()) continue;
        bool previewing = ! row.isDir && row.file.getFullPathName() == owner.session.previewPath;
        if (previewing || (over && r.contains (mouse)))
        {
            g.setColour (previewing ? C::cellA : C::rowSel);
            g.fillRoundedRectangle (r.toFloat(), 6.0f);
        }
        if (row.isDir)
        {
            float x = 4.0f + row.depth * 14.0f;
            bool open = owner.openDirs.count (row.file.getFullPathName()) != 0;
            auto cc = juce::Point<float> (x + 4, (float) r.getCentreY());
            juce::Path chev;
            if (open) { chev.startNewSubPath (cc.x - 3, cc.y - 1.5f); chev.lineTo (cc.x, cc.y + 1.5f); chev.lineTo (cc.x + 3, cc.y - 1.5f); }
            else { chev.startNewSubPath (cc.x - 1.5f, cc.y - 3); chev.lineTo (cc.x + 1.5f, cc.y); chev.lineTo (cc.x - 1.5f, cc.y + 3); }
            g.setColour (C::muted);
            g.strokePath (chev, juce::PathStrokeType (1.4f));
            drawFolder (g, { x + 14, (float) r.getCentreY() - 7, 14, 14 }, C::text2);
            g.setColour (C::text);
            g.setFont (sans (13.0f));
            g.drawText (row.file.getFileName(), r.withTrimmedLeft ((int) x + 34).withTrimmedRight (6), juce::Justification::centredLeft, true);
        }
        else
        {
            float x = 18.0f + row.depth * 14.0f;
            juce::Path tri;
            float cy = (float) r.getCentreY();
            tri.addTriangle (x + 4, cy - 4.5f, x + 11, cy, x + 4, cy + 4.5f);
            g.setColour (previewing ? C::orange : C::muted);
            g.fillPath (tri);
            g.setColour (C::text);
            g.setFont (sans (13.0f));
            g.drawText (row.file.getFileName(), r.withTrimmedLeft ((int) x + 22).withTrimmedRight (6), juce::Justification::centredLeft, true);
        }
    }
}

void Browser::List::mouseUp (const juce::MouseEvent& e)
{
    if (dragStarted) { dragStarted = false; return; }
    int i = e.y / kRowH;
    if (i < 0 || i >= (int) owner.rows.size()) return;
    auto& row = owner.rows[(size_t) i];
    if (row.isDir)
    {
        auto key = row.file.getFullPathName();
        if (owner.openDirs.count (key)) owner.openDirs.erase (key);
        else owner.openDirs.insert (key);
        owner.rebuild();
    }
    else
        owner.session.setPreview (row.file.getFullPathName());
}

void Browser::List::mouseDrag (const juce::MouseEvent& e)
{
    if (dragStarted || e.getDistanceFromDragStart() < 6) return;
    int i = e.getMouseDownY() / kRowH;
    if (i < 0 || i >= (int) owner.rows.size() || owner.rows[(size_t) i].isDir) return;
    auto& f = owner.rows[(size_t) i].file;
    if (auto* dc = juce::DragAndDropContainer::findParentDragContainerFor (this))
    {
        dragStarted = true;
        auto font = sans (13.0f);
        int w = juce::GlyphArrangement::getStringWidthInt (font, f.getFileName()) + 24;
        juce::Image img (juce::Image::ARGB, w, 26, true);
        {
            juce::Graphics g (img);
            g.setColour (C::menu.withAlpha (0.92f));
            g.fillRoundedRectangle (0, 0, (float) w, 26, 6);
            g.setColour (C::orange);
            g.drawRoundedRectangle (0.5f, 0.5f, (float) w - 1, 25, 6, 1);
            g.setColour (C::text);
            g.setFont (font);
            g.drawText (f.getFileName(), 12, 0, w - 16, 26, juce::Justification::centredLeft);
        }
        dc->startDragging ("file:" + f.getFullPathName(), this, juce::ScaledImage (img), false, nullptr, &e.source);
    }
}

} // namespace ok::ui
