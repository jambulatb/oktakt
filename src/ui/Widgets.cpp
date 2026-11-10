#include "Widgets.h"

namespace ok::ui
{

int Btn::idealWidth (int pad) const
{
    int w = juce::GlyphArrangement::getStringWidthInt (font, textFn ? textFn() : text) + pad * 2;
    if (chevron) w += 14;
    return w;
}

void Btn::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    const bool over = isMouseOver (true) && ! dimmed;
    const bool on = isOn ? isOn() : toggled;
    const auto label = textFn ? textFn() : text;
    juce::Colour b = on ? onBg : (over ? bgHover : bg);
    if (! b.isTransparent())
    {
        g.setColour (b);
        g.fillRoundedRectangle (r, radius);
    }
    if (! outlineCol.isTransparent())
    {
        g.setColour (outlineCol);
        g.drawRoundedRectangle (r.reduced (0.5f), radius, 1.0f);
    }
    auto ink = on ? onText : textCol;
    if (dimmed) ink = C::dim;

    auto content = r.reduced ((float) padX, 0.0f);
    if (chevron)
    {
        drawChevron (g, { content.getRight() - 6.0f, r.getCentreY() }, C::text2, 8.0f);
        content.removeFromRight (14.0f);
    }
    if (icon)
    {
        if (label.isEmpty()) icon (g, r, ink);
        else icon (g, content.removeFromLeft (r.getHeight()), ink);
    }
    if (label.isNotEmpty())
    {
        g.setColour (ink);
        g.setFont (font);
        g.drawText (label, content, just, true);
    }
}

void Btn::mouseUp (const juce::MouseEvent& e)
{
    if (dimmed || ! getLocalBounds().contains (e.getPosition())) return;
    if (e.mods.isPopupMenu())
    {
        if (onRightClick) onRightClick();
        return;
    }
    if (onClickEvent) onClickEvent (e);
    if (onClick) onClick();
}

//==============================================================================
juce::String Knob::valueText() const
{
    if (! get) return {};
    int v = get();
    if (! list.isEmpty()) return list[juce::jlimit (0, list.size() - 1, v)];
    return format ? format (v) : juce::String (v);
}

void Knob::paint (juce::Graphics& g)
{
    if (! get) return;
    const int v = get();
    float frac = list.isEmpty() ? (float) (v - min) / (float) juce::jmax (1, max - min)
                                : (float) v / (float) juce::jmax (1, list.size() - 1);
    auto col = colour ? colour() : C::orange;
    if (! active) g.setOpacity (0.35f);

    auto r = getLocalBounds().toFloat();
    auto kr = juce::Rectangle<float> ((float) knobPx, (float) knobPx).withCentre ({ showText ? knobPx * 0.5f : r.getCentreX(), r.getCentreY() });
    juce::Graphics::ScopedSaveState ss (g);
    if (! active) g.beginTransparencyLayer (0.35f);
    drawKnob (g, kr, frac, col, bipolar, strokeScale, innerRadius);

    if (showText)
    {
        auto tr = r.withTrimmedLeft ((float) knobPx + 6.0f);
        float lh = labelSize + 3.0f, vh = valueSize + 5.0f;
        float y = r.getCentreY() - (lh + vh) * 0.5f;
        g.setColour (labelCol);
        g.setFont (sans (labelSize));
        g.drawText (label, juce::Rectangle<float> (tr.getX(), y, tr.getWidth(), lh), juce::Justification::centredLeft, true);
        g.setColour (C::text);
        g.setFont (mono (valueSize));
        g.drawText (valueText(), juce::Rectangle<float> (tr.getX(), y + lh, tr.getWidth() + 8, vh), juce::Justification::centredLeft, false);
    }
    if (! active) g.endTransparencyLayer();
}

void Knob::apply (int v)
{
    if (! list.isEmpty()) v = juce::jlimit (0, list.size() - 1, v);
    else v = juce::jlimit (min, max, v);
    if (get && v == get()) return;
    if (set) set (v);
    repaint();
}

void Knob::mouseDown (const juce::MouseEvent& e)
{
    if (! active || ! get) return;
    if (e.mods.isPopupMenu()) return;
    dragging = list.isEmpty();
    dragStartY = (float) e.position.y;
    dragStartVal = get();
    if (dragging) session.checkpoint();
}

void Knob::mouseDrag (const juce::MouseEvent& e)
{
    if (! dragging) return;
    float range = (float) (max - min);
    float sens = range > 60.0f ? 1.0f : 0.3f;
    if (range > 300.0f) sens = 2.0f;
    if (e.mods.isShiftDown()) sens *= 0.25f;
    float dy = dragStartY - (float) e.position.y;
    apply (dragStartVal + (int) std::lround (dy * sens));
}

void Knob::mouseUp (const juce::MouseEvent& e)
{
    if (! active || ! get) return;
    if (! list.isEmpty() && ! e.mods.isPopupMenu() && e.getDistanceFromDragStart() < 4 && getLocalBounds().contains (e.getPosition()))
    {
        session.checkpoint();
        apply ((get() + 1) % list.size());
    }
    dragging = false;
}

void Knob::mouseDoubleClick (const juce::MouseEvent&)
{
    if (! active || ! get || ! list.isEmpty()) return;
    session.checkpoint();
    apply (def);
}

void Knob::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    if (! active || ! get) return;
    wheelAcc += w.deltaY * (w.isReversed ? -1.0f : 1.0f);
    int steps = (int) (wheelAcc / 0.07f);
    if (steps == 0) return;
    wheelAcc -= steps * 0.07f;
    session.checkpoint (this);
    if (! list.isEmpty())
    {
        int n = list.size();
        apply (((get() + steps) % n + n) % n);
        return;
    }
    int step = (max - min) > 300 ? 2 : 1;
    if (e.mods.isShiftDown()) step = 1;
    apply (get() + steps * step);
}

//==============================================================================
std::vector<juce::Rectangle<float>> Seg::itemRects() const
{
    std::vector<juce::Rectangle<float>> out;
    auto f = sans (fontSize, 600).withExtraKerningFactor (tracking);
    float x = 3.0f;
    float h = (float) getHeight() - 6.0f;
    for (auto& it : items)
    {
        float w = (float) juce::GlyphArrangement::getStringWidthInt (f, it) + padX * 2.0f;
        out.push_back ({ x, 3.0f, w, h });
        x += w + 2.0f;
    }
    return out;
}

int Seg::idealWidth() const
{
    auto r = itemRects();
    return r.empty() ? 6 : (int) std::ceil (r.back().getRight() + 3.0f);
}

void Seg::paint (juce::Graphics& g)
{
    g.setColour (C::btn);
    g.fillRoundedRectangle (getLocalBounds().toFloat(), 7.0f);
    const int cur = get ? get() : -1;
    auto rects = itemRects();
    auto mouse = getMouseXYRelative().toFloat();
    for (int i = 0; i < (int) rects.size(); ++i)
    {
        bool on = i == cur;
        bool en = enabled.empty() || enabled[(size_t) i];
        if (on)
        {
            g.setColour (C::text);
            g.fillRoundedRectangle (rects[(size_t) i], 5.0f);
        }
        else if (isMouseOver() && rects[(size_t) i].contains (mouse) && en)
        {
            g.setColour (C::hover);
            g.fillRoundedRectangle (rects[(size_t) i], 5.0f);
        }
        g.setColour (on ? C::ink : (en ? C::text2 : C::dim));
        g.setFont (sans (fontSize, 600).withExtraKerningFactor (tracking));
        g.drawText (items[i], rects[(size_t) i], juce::Justification::centred, false);
    }
}

void Seg::mouseUp (const juce::MouseEvent& e)
{
    auto rects = itemRects();
    for (int i = 0; i < (int) rects.size(); ++i)
        if (rects[(size_t) i].contains (e.position) && (enabled.empty() || enabled[(size_t) i]))
        {
            if (set) set (i);
            repaint();
            return;
        }
}

//==============================================================================
void Stepper::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    auto b1 = r.removeFromLeft ((float) btn).withSizeKeepingCentre ((float) btn, (float) btn);
    auto b2 = r.removeFromRight ((float) btn).withSizeKeepingCentre ((float) btn, (float) btn);
    float rad = btn <= 18 ? 4.0f : 5.0f;
    g.setColour (C::btn2);
    g.fillRoundedRectangle (b1, rad);
    g.fillRoundedRectangle (b2, rad);
    g.setColour (C::text);
    g.setFont (sans (btn <= 18 ? 13.0f : 14.0f, 600));
    g.drawText (juce::String::fromUTF8 ("\xe2\x88\x92"), b1, juce::Justification::centred, false);
    g.drawText ("+", b2, juce::Justification::centred, false);
    g.setFont (mono (fontSize, 600));
    int v = get ? get() : 0;
    g.drawText (format ? format (v) : juce::String (v), r, juce::Justification::centred, false);
}

void Stepper::mouseUp (const juce::MouseEvent& e)
{
    if (! get || ! set || ! getLocalBounds().contains (e.getPosition())) return;
    int v = get();
    if (e.x < btn + 2) set (juce::jlimit (min, max, v - 1));
    else if (e.x > getWidth() - btn - 2) set (juce::jlimit (min, max, v + 1));
    repaint();
}

void Stepper::mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& w)
{
    if (! get || ! set) return;
    wheelAcc += w.deltaY * (w.isReversed ? -1.0f : 1.0f);
    int steps = (int) (wheelAcc / 0.1f);
    if (steps == 0) return;
    wheelAcc -= steps * 0.1f;
    set (juce::jlimit (min, max, get() + (steps > 0 ? 1 : -1)));
    repaint();
}

//==============================================================================
void showMenu (juce::PopupMenu& m, juce::Component* target, std::function<void (int)> cb, int minWidth, juce::Rectangle<int> area)
{
    auto opts = juce::PopupMenu::Options().withTargetComponent (target).withMinimumWidth (minWidth);
    // The component sets the menu's scale; the area sets where it opens
    if (target != nullptr && ! area.isEmpty())
        opts = opts.withTargetScreenArea (target->localAreaToGlobal (area));
    m.showMenuAsync (opts, [cb] (int r) { if (cb) cb (r); });
}

} // namespace ok::ui
