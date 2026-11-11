#include "SidePanel.h"

namespace ok::ui
{

static juce::String signedStr (int v) { return v > 0 ? "+" + juce::String (v) : (v < 0 ? juce::String::fromUTF8 ("\xe2\x88\x92") + juce::String (-v) : juce::String ("0")); }

// STEP is one per track: read it from the active track
template <typename Fn>
std::optional<int> SidePanel::common (Fn get)
{
    auto* t = session.selectedTrack();
    if (t == nullptr) return std::nullopt;
    return get (t->step);
}

template <typename Fn>
void SidePanel::setAll (Fn apply, const void* gesture)
{
    auto* t = session.selectedTrack();
    if (t == nullptr) return;
    session.checkpoint (gesture);
    apply (*t, t->step);
    session.changed();
}

SidePanel::SidePanel (Session& s, UiHost& h)
    : session (s), host (h), random (s), accuracy (s), chance (s), micro (s), fade (s)
{
    auto setupStepper = [this] (Stepper& st, int lo, int hi, std::function<int()> get, std::function<void (int)> set)
    {
        st.min = lo; st.max = hi; st.get = std::move (get); st.set = std::move (set);
        addAndMakeVisible (st);
    };
    setupStepper (groups, 1, 11,
                  [this] { auto* t = session.selectedTrack(); return t ? t->groups : 4; },
                  [this] (int v) { if (auto* t = session.selectedTrack()) session.setGrid (t->id, v, t->steps); });
    setupStepper (steps, 1, 8,
                  [this] { auto* t = session.selectedTrack(); return t ? t->steps : 4; },
                  [this] (int v) { if (auto* t = session.selectedTrack()) session.setGrid (t->id, t->groups, v); });
    groups.setTooltip ("Groups per bar, 1-11 (buttons or scroll)");
    steps.setTooltip ("Steps in each group, 1-8 (buttons or scroll)");

    setupStepper (condN, 1, 8,
                  [this] { return common ([] (const StepSettings& h) { return h.condN; }).value_or (1); },
                  [this] (int v)
                  {
                      static const int g = 0;
                      setAll ([v] (Track&, StepSettings& h)
                      {
                          uint8_t m = h.condMask & StepSettings::mask (h.condN);
                          if (v > h.condN) m |= (uint8_t) (StepSettings::mask (v) & ~StepSettings::mask (h.condN));
                          h.condN = v;
                          h.condMask = (uint8_t) (m & StepSettings::mask (v));
                          if (h.condMask == 0) h.condMask = 1;
                      }, &g);
                  });
    condN.btn = 18; condN.valueW = 12; condN.fontSize = 12.0f;
    condN.setTooltip ("Loop cycle: how many passes the condition counts");

    setupStepper (retrigLen, 1, 16,
                  [this] { return common ([] (const StepSettings& h) { return h.retrigLen; }).value_or (1); },
                  [this] (int v) { static const int g = 0; setAll ([v] (Track&, StepSettings& h) { h.retrigLen = v; }, &g); });
    retrigLen.btn = 18; retrigLen.valueW = 18; retrigLen.fontSize = 12.0f;

    auto setupKnob = [this] (Knob& k, const char* label, int lo, int hi, int def, int px)
    {
        k.label = label; k.min = lo; k.max = hi; k.def = def; k.knobPx = px;
        k.valueSize = 12.0f;
        addAndMakeVisible (k);
    };
    setupKnob (random, "Random", 0, 100, 0, 32);
    random.get = [this] { auto* t = session.selectedTrack(); return t ? t->random : 0; };
    random.set = [this] (int v) { if (auto* t = session.selectedTrack()) { t->random = v; session.changed(); } };
    random.format = [] (int v) { return juce::String (v) + "%"; };
    random.colour = [this] { auto* t = session.selectedTrack(); return t ? trackColour (t->colour) : C::orange; };
    random.setTooltip ("Each pass drops some hits, adds quieter ones in empty cells and varies the level. 0% plays exactly what is written. A hit can set its own value in the Random row. Pinned hits stay as they are");

    setupKnob (accuracy, "Accuracy", 0, 100, 100, 32);
    accuracy.get = [this] { auto* t = session.selectedTrack(); return t ? t->accuracy : 100; };
    accuracy.set = [this] (int v) { if (auto* t = session.selectedTrack()) { t->accuracy = v; session.changed(); } };
    accuracy.format = [] (int v) { return juce::String (v) + "%"; };
    accuracy.colour = random.colour;
    accuracy.setTooltip ("100% plays exactly as written. Lower values shift hits in time and vary their level, anew on every pass. A hit can set its own value in the Accuracy row. Pinned hits stay as they are");

    setupKnob (chance, "Chance", 0, 100, 100, 36);
    chance.get = [this] { return common ([] (const StepSettings& h) { return h.chance; }).value_or (100); };
    chance.set = [this] (int v) { setAll ([v] (Track&, StepSettings& h) { h.chance = v; }, &chance); };
    chance.format = [] (int v) { return juce::String (v) + "%"; };

    setupKnob (micro, "Micro", -23, 23, 0, 36);
    micro.bipolar = true;
    micro.get = [this] { auto* t = session.selectedTrack(); return t ? t->micro : 0; };
    micro.set = [this] (int v) { if (auto* t = session.selectedTrack()) { t->micro = v; session.changed(); } };
    micro.format = [] (int v) { return signedStr (v); };
    micro.colour = random.colour;
    micro.setTooltip ("Moves every hit of the track earlier or later than its cell, less than one step (units of 1/24 step). A hit can set its own value in the Micro row");

    setupKnob (fade, "Fade", -100, 100, 0, 36);
    fade.bipolar = true;
    fade.get = [this] { return common ([] (const StepSettings& h) { return h.fade; }).value_or (0); };
    fade.set = [this] (int v) { setAll ([v] (Track&, StepSettings& h) { h.fade = v; }, &fade); };
    fade.format = [] (int v) { return signedStr (v); };
    fade.setTooltip ("Minus: repeats fade out, plus: they fade in, 0: even. The next hit of the track cuts unfinished repeats");

    for (auto* k : { &chance, &fade }) k->colour = [] { return C::orange; };

    retrig.font = mono (12.0f);
    retrig.bg = C::btn2;
    retrig.radius = 5.0f;
    retrig.just = juce::Justification::centredLeft;
    retrig.padX = 10;
    retrig.chevron = true;
    retrig.onClick = [this] { showRetrigMenu(); };
    addAndMakeVisible (retrig);

    clearMarks.text = juce::String::fromUTF8 ("Clear marks \xc2\xb7 Esc");
    clearMarks.font = sans (12.0f);
    clearMarks.onClick = [this] { session.clearMarks(); };
    clearMarks.setTooltip ("Clear marks (Esc)");
    addChildComponent (clearMarks);

    output.addItem ("Main", 1);
    for (int i = 2; i <= kNumOutputs; ++i) output.addItem ("Out " + juce::String (i), i);
    output.onChange = [this]
    {
        if (auto* t = session.selectedTrack())
        {
            int v = output.getSelectedId() - 1;
            if (v >= 0 && v != t->output) { session.checkpoint(); t->output = v; session.changed(); }
        }
    };
    output.setTooltip ("Where this track goes in your DAW. Main is the plugin's main stereo output. Out 2 to 16 are separate outputs: connect them in your DAW, otherwise the track is silent");
    if (session.isPlugin) addAndMakeVisible (output);

    refresh();
}

void SidePanel::showRetrigMenu()
{
    juce::PopupMenu m;
    auto cur = common ([] (const StepSettings& h) { return h.retrig; });
    for (int i = 0; i < kNumRetrigRates; ++i)
        m.addItem (i + 1, retrigName (i), true, cur && *cur == i);
    showMenu (m, &retrig, [this] (int r)
    {
        if (r <= 0) return;
        setAll ([r] (Track&, StepSettings& h) { h.retrig = r - 1; });
    }, retrig.getWidth());
}

void SidePanel::refresh()
{
    const bool active = session.trackActive();
    for (auto* c : getChildren()) c->setVisible (active);
    output.setVisible (active && session.isPlugin);
    clearMarks.setVisible (active && session.hasMarks());

    if (active)
    {
        auto r = common ([] (const StepSettings& h) { return h.retrig; });
        retrig.text = r ? juce::String (retrigName (*r)) : juce::String ("mixed");
        if (auto* t = session.selectedTrack())
            output.setSelectedId (t->output + 1, juce::dontSendNotification);
    }
    resized();
    repaint();
    for (auto* c : getChildren()) c->repaint();
}

void SidePanel::resized()
{
    auto r = getLocalBounds().reduced (14, 12);
    const int w = r.getWidth();
    int y = r.getY();
    const int x = r.getX();

    gridTitle = { x, y, w, 32 };
    y += 32 + 6;
    groupsLabel = { x, y, w, 28 };
    groups.setBounds (x + w - groups.idealWidth(), y + 2, groups.idealWidth(), 24);
    y += 28 + 6;
    stepsLabel = { x, y, w, 28 };
    steps.setBounds (x + w - steps.idealWidth(), y + 2, steps.idealWidth(), 24);
    y += 28 + 6 + 4;

    positionTitle = { x, y, w, 12 + 12 };
    y += 12 + 8 + 8;
    micro.setBounds (x, y, 80, 38);
    y += 38 + 12;

    randomTitle = { x, y, w, 12 + 12 };
    y += 12 + 8 + 8;
    random.setBounds (x, y, 80, 38);
    accuracy.setBounds (x + w - 80, y, 80, 38);
    y += 38 + 12;

    divider = { x, y, w, 1 };
    y += 1 + 12;
    stepTitle = { x, y, w, 16 };
    y += 16 + 12;

    // Whether the hit plays: Condition and Chance
    condLabel = { x, y, w, 16 };
    y += 16 + 6;
    const int condBtnsW = condN.idealWidth();
    condN.setBounds (x + w - condBtnsW, y, condBtnsW, 18);
    squares.clear();
    {
        int n = condN.get ? condN.get() : 1;
        int sx = x, sy = y;
        int avail = w - condBtnsW - 6;
        for (int i = 0; i < n; ++i)
        {
            if (sx + 18 > x + avail) { sx = x; sy += 22; }
            squares.push_back ({ sx, sy, 18, 18 });
            sx += 22;
        }
        y = sy + 18 + 8;
    }
    chance.setBounds (x, y, 77, 38);
    y += 38 + 12;

    // Retrig and the Fade of its repeats
    retrigLabel = { x, y, w, 16 };
    y += 16 + 6;
    retrig.setBounds (x, y + 5, w - 85, 28);
    fade.setBounds (x + w - 77, y, 77, 38);
    y += 38 + 6;
    retrigLenLabel = { x, y, w, 18 };
    retrigLen.setBounds (x + w - 38 - retrigLen.idealWidth(), y, retrigLen.idealWidth(), 18);
    y += 18 + 12;

    // OUTPUT (plugin only) sits at the bottom of the panel, whatever is above it
    int bottom = r.getBottom();
    if (session.isPlugin)
    {
        outputTitle = { x, bottom - 52, w, 16 };
        output.setBounds (x, bottom - 28, w, 28);
        bottom = outputTitle.getY() - 12 - 12;
    }

    clearCompact = clearMarks.isVisible() && y + 28 > bottom;
    if (clearCompact)
    {
        clearMarks.text = {};
        clearMarks.icon = [] (juce::Graphics& g, juce::Rectangle<float> b, juce::Colour) { drawCross (g, b.withSizeKeepingCentre (8, 8), C::text2, 1.5f); };
        clearMarks.setBounds (x + w - 20, stepTitle.getY() - 1, 20, 18);
    }
    else
    {
        clearMarks.text = juce::String::fromUTF8 ("Clear marks \xc2\xb7 Esc");
        clearMarks.icon = nullptr;
        clearMarks.setBounds (x, y, w, 28);
    }
}

void SidePanel::paint (juce::Graphics& g)
{
    g.fillAll (C::panel);
    g.setColour (C::border);
    g.fillRect (0, 0, 1, getHeight());
    if (! session.trackActive()) return;
    auto* t = session.selectedTrack();

    auto title = [&] (juce::Rectangle<int> r, const char* text)
    {
        g.setColour (C::muted);
        g.setFont (caps (12.0f));
        g.drawText (text, r.withHeight (16), juce::Justification::centredLeft, false);
    };
    auto label = [&] (juce::Rectangle<int> r, const juce::String& text)
    {
        g.setColour (C::muted);
        g.setFont (sans (12.0f));
        g.drawText (text, r, juce::Justification::centredLeft, false);
    };

    title (gridTitle, "GRID");
    g.setColour (C::muted);
    g.setFont (sans (11.0f));
    g.drawText (t->name, gridTitle.withTrimmedTop (18), juce::Justification::centredLeft, true);
    label (groupsLabel, "Groups");
    label (stepsLabel, "Steps in group");

    g.setColour (C::border);
    g.fillRect (positionTitle.getX(), positionTitle.getY() - 6, positionTitle.getWidth(), 1);
    title (positionTitle.withTrimmedTop (6), "POSITION");
    g.setColour (C::border);
    g.fillRect (randomTitle.getX(), randomTitle.getY() - 6, randomTitle.getWidth(), 1);
    title (randomTitle.withTrimmedTop (6), "RANDOM");
    g.setColour (C::border);
    g.fillRect (divider);

    title (stepTitle, "STEP");
    const int marked = session.markedCount (t->id);
    juce::String target = marked > 0 ? juce::String (marked) + " marked"
                                     : juce::String::fromUTF8 ("all hits \xc2\xb7 ") + juce::String (session.hitCount (t->id));
    g.setColour (C::text);
    g.setFont (sans (12.0f, 600));
    g.drawText (target, clearCompact ? stepTitle.withTrimmedRight (26) : stepTitle, juce::Justification::centredRight, false);

    // Condition
    label (condLabel, "Condition");
    auto n = common ([] (const StepSettings& h) { return h.condN; });
    auto m = common ([] (const StepSettings& h) { return (int) (h.condMask & StepSettings::mask (h.condN)); });
    juce::String shown = "mixed";
    if (n && m)
    {
        if (*m == StepSettings::mask (*n)) shown = "every pass";
        else
        {
            juce::StringArray on;
            for (int i = 0; i < *n; ++i) if ((*m >> i) & 1) on.add (juce::String (i + 1));
            shown = on.joinIntoString (",") + " of " + juce::String (*n);
        }
    }
    g.setColour (C::text);
    g.setFont (mono (12.0f));
    g.drawText (shown, condLabel, juce::Justification::centredRight, false);
    for (int i = 0; i < (int) squares.size(); ++i)
    {
        bool on = m ? ((*m >> i) & 1) != 0 : false;
        auto r = squares[(size_t) i].toFloat();
        g.setColour (on ? C::orange : C::btn2);
        g.fillRoundedRectangle (r, 4.0f);
        g.setColour (on ? C::ink : C::muted);
        g.setFont (mono (10.0f, 600));
        g.drawText (juce::String (i + 1), r, juce::Justification::centred, false);
    }

    // Retrig
    label (retrigLabel, "Retrig");
    auto ret = common ([] (const StepSettings& h) { return h.retrig; });
    auto rl = common ([] (const StepSettings& h) { return h.retrigLen; });
    juce::String count;
    if (ret && *ret > 0 && rl)
    {
        double stepLen = 4.0 / t->stepsPerBar();
        int reps = juce::jmax (1, (int) std::floor (*rl * stepLen / retrigPpq (*ret) + 1e-6));
        count = juce::String (reps) + juce::String::fromUTF8 (" \xc3\x97 hits");
    }
    else if (ret && *ret == 0) count = "off";
    g.setColour (C::text);
    g.setFont (mono (12.0f));
    g.drawText (count, retrigLabel, juce::Justification::centredRight, false);
    label (retrigLenLabel, "Retrig length");
    g.setColour (C::muted);
    g.setFont (sans (11.0f));
    g.drawText ("steps", retrigLenLabel, juce::Justification::centredRight, false);

    if (session.isPlugin)
    {
        g.setColour (C::border);
        g.fillRect (outputTitle.getX(), outputTitle.getY() - 12, outputTitle.getWidth(), 1);
        title (outputTitle, "OUTPUT");
        if (t->output > 0)
        {
            g.setColour (C::text2);
            g.setFont (sans (11.0f));
            g.drawText ("connect in your DAW", outputTitle, juce::Justification::centredRight, false);
        }
    }
}

void SidePanel::mouseUp (const juce::MouseEvent& e)
{
    for (int i = 0; i < (int) squares.size(); ++i)
        if (squares[(size_t) i].contains (e.getPosition()))
        {
            setAll ([i] (Track&, StepSettings& h)
            {
                if (i >= h.condN) return;
                uint8_t m = (uint8_t) (h.condMask ^ (1 << i));
                if ((m & StepSettings::mask (h.condN)) != 0) h.condMask = m;
            });
            return;
        }
}

} // namespace ok::ui
