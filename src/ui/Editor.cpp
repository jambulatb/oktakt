#include "Editor.h"
#include "Header.h"
#include "Browser.h"
#include "GridView.h"
#include "SidePanel.h"
#include "SoundPanel.h"
#include "Settings.h"

namespace ok::ui
{

struct Editor::Dismisser : juce::Component
{
    std::function<void()> onDismiss;
    void mouseDown (const juce::MouseEvent&) override { if (onDismiss) onDismiss(); }
};

Editor::Editor (OktaktProcessor& p)
    : AudioProcessorEditor (p), proc (p), session (*p.session)
{
    setLookAndFeel (&lnf);

    header = std::make_unique<Header> (session, *this);
    grid = std::make_unique<GridSection> (session, *this);
    side = std::make_unique<SidePanel> (session, *this);
    sound = std::make_unique<SoundPanel> (session, *this);
    root.addAndMakeVisible (*header);
    if (! session.isPlugin)
    {
        browser = std::make_unique<Browser> (session, *this);
        root.addAndMakeVisible (*browser);
    }
    root.addAndMakeVisible (*grid);
    root.addAndMakeVisible (*side);
    root.addChildComponent (*sound);
    addAndMakeVisible (root);
    root.setBounds (0, 0, kWidth, kHeight);

    setWantsKeyboardFocus (true);
    session.addChangeListener (this);

    setResizable (true, true);
    setResizeLimits (kWidth * 3 / 4, kHeight * 3 / 4, kWidth * 3 / 2, kHeight * 3 / 2);
    if (auto* c = getConstrainer()) c->setFixedAspectRatio ((double) kWidth / kHeight);
    setSize (kWidth, kHeight);
    refresh();
}

Editor::~Editor()
{
    session.removeChangeListener (this);
    closePopover();
    saveAsWindow = nullptr;
    confirmWindow = nullptr;
    setLookAndFeel (nullptr);
}

void Editor::paint (juce::Graphics& g)
{
    g.fillAll (C::bg);
}

void Editor::resized()
{
    // Fit inside the window keeping the proportions (a maximised or full-screen window rarely has them),
    // centred on the free side
    const float scale = juce::jmin ((float) getWidth() / (float) kWidth, (float) getHeight() / (float) kHeight);
    const float dx = std::floor (((float) getWidth() - kWidth * scale) * 0.5f);
    const float dy = std::floor (((float) getHeight() - kHeight * scale) * 0.5f);
    root.setTransform (juce::AffineTransform::scale (scale).translated (dx, dy));
}

void Editor::layoutRoot()
{
    const bool active = session.trackActive();
    header->setBounds (0, 0, kWidth, 52);
    int x = 0;
    if (browser != nullptr)
    {
        browser->setBounds (0, 52, 210, kHeight - 52);
        x = 210;
    }
    const int w = kWidth - x;
    const int gridH = active ? 600 : kHeight - 52;
    grid->setBounds (x, 52, w - 190, gridH);
    side->setBounds (x + w - 190, 52, 190, gridH);
    sound->setVisible (active);
    if (active) sound->setBounds (x, 52 + gridH, w, kHeight - 52 - gridH);
}

void Editor::refresh()
{
    layoutRoot();
    header->refresh();
    if (browser) browser->refresh();
    grid->refresh();
    side->refresh();
    if (session.trackActive()) sound->refresh();
}

void Editor::changeListenerCallback (juce::ChangeBroadcaster*)
{
    refresh();
}

//==============================================================================
void Editor::showPopover (std::unique_ptr<juce::Component> content, juce::Rectangle<int> anchor, bool alignRight)
{
    closePopover();
    dismisser = std::make_unique<Dismisser>();
    dismisser->onDismiss = [this] { juce::MessageManager::callAsync ([sp = juce::Component::SafePointer<Editor> (this)] { if (sp) sp->closePopover(); }); };
    dismisser->setBounds (0, 0, kWidth, kHeight);
    root.addAndMakeVisible (*dismisser);

    popover = std::move (content);
    auto size = popover->getBounds();
    int px = alignRight ? anchor.getRight() - size.getWidth() : anchor.getX();
    int py = anchor.getBottom() + 4;
    if (py + size.getHeight() > kHeight - 8) py = anchor.getY() - 4 - size.getHeight();
    px = juce::jlimit (8, kWidth - 8 - size.getWidth(), px);
    py = juce::jlimit (8, kHeight - 8 - size.getHeight(), py);
    popover->setBounds (px, py, size.getWidth(), size.getHeight());
    root.addAndMakeVisible (*popover);
}

void Editor::closePopover()
{
    settingsOpen = false;
    popover = nullptr;
    dismisser = nullptr;
}

void Editor::toggleSettings()
{
    if (settingsOpen) { closePopover(); return; }
    auto s = std::make_unique<Settings> (session, [this]
    {
        juce::MessageManager::callAsync ([sp = juce::Component::SafePointer<Editor> (this)] { if (sp) sp->closePopover(); });
    });
    showPopover (std::move (s), { kWidth - 17, 46, 1, 0 }, true);
    settingsOpen = true;
}

void Editor::promptSaveAs()
{
    saveAsWindow = std::make_unique<juce::AlertWindow> ("Save pattern as", "Name of the pattern in your library", juce::MessageBoxIconType::NoIcon);
    saveAsWindow->addTextEditor ("name", session.doc.name);
    saveAsWindow->addButton ("Save", 1, juce::KeyPress (juce::KeyPress::returnKey));
    saveAsWindow->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    if (auto* ed = saveAsWindow->getTextEditor ("name"))
    {
        ed->setFont (sans (13.0f));
        ed->setColour (juce::TextEditor::backgroundColourId, C::wave);
    }
    root.addAndMakeVisible (*saveAsWindow);
    saveAsWindow->setCentrePosition (kWidth / 2, kHeight / 3);
    saveAsWindow->enterModalState (true, juce::ModalCallbackFunction::create ([this] (int result)
    {
        bool saved = false;
        if (result == 1 && saveAsWindow != nullptr)
            saved = session.saveAs (saveAsWindow->getTextEditorContents ("name")) && ! session.dirty;
        auto then = std::move (afterSaveAs);
        afterSaveAs = nullptr;
        juce::MessageManager::callAsync ([sp = juce::Component::SafePointer<Editor> (this)] { if (sp) sp->saveAsWindow = nullptr; });
        if (saved && then) then();
    }), false);
}

void Editor::confirmClose (std::function<void()> close)
{
    if (! session.dirty) { close(); return; }
    if (confirmWindow != nullptr || saveAsWindow != nullptr) return;   // already asking
    closePopover();

    confirmWindow = std::make_unique<juce::AlertWindow> ("Save changes?",
        juce::String::fromUTF8 ("\xe2\x80\x9c") + session.doc.name + juce::String::fromUTF8 ("\xe2\x80\x9d has changes that are not saved to the pattern library."),
        juce::MessageBoxIconType::NoIcon);
    confirmWindow->addButton ("Save", 1, juce::KeyPress (juce::KeyPress::returnKey));
    confirmWindow->addButton (juce::String::fromUTF8 ("Don\xe2\x80\x99t save"), 2);
    confirmWindow->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    root.addAndMakeVisible (*confirmWindow);
    confirmWindow->setCentrePosition (kWidth / 2, kHeight / 3);
    confirmWindow->enterModalState (true, juce::ModalCallbackFunction::create ([this, close] (int result)
    {
        juce::MessageManager::callAsync ([sp = juce::Component::SafePointer<Editor> (this)] { if (sp) sp->confirmWindow = nullptr; });
        if (result == 2) { close(); return; }
        if (result != 1) return;
        if (session.save()) { if (! session.dirty) close(); return; }
        // Not saved yet: ask for a name, close once it is saved
        afterSaveAs = close;
        juce::MessageManager::callAsync ([sp = juce::Component::SafePointer<Editor> (this)] { if (sp) sp->promptSaveAs(); });
    }), false);
}

void Editor::startRename (uint32_t trackId)
{
    grid->startRename (trackId);
}

//==============================================================================
bool Editor::keyPressed (const juce::KeyPress& k)
{
    const auto mods = k.getModifiers();
    const int code = k.getKeyCode();

    if (mods.isCommandDown() && (code == 'z' || code == 'Z'))
    {
        if (mods.isShiftDown()) session.redo();
        else session.undo();
        return true;
    }
    if (mods.isCommandDown() && (code == 'y' || code == 'Y')) { session.redo(); return true; }
    if (mods.isCommandDown() && (code == 's' || code == 'S'))
    {
        if (! session.save()) promptSaveAs();
        return true;
    }
    if (code == juce::KeyPress::escapeKey)
    {
        if (popover != nullptr) closePopover();
        else session.clearMarks();
        return true;
    }
    if (code == juce::KeyPress::F2Key)
    {
        if (session.trackActive()) startRename (session.selected);
        return true;
    }
    if (code == juce::KeyPress::deleteKey || code == juce::KeyPress::backspaceKey)
    {
        if (session.trackActive()) session.deleteTrack (session.selected);
        return true;
    }
    if (code == juce::KeyPress::spaceKey && ! session.isPlugin)
    {
        session.togglePlay();
        return true;
    }
    return false;
}

} // namespace ok::ui
