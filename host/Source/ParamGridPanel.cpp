#include "ParamGridPanel.h"
#include "ParamGridLayout.h"
#include "ParamMap.h"
// ParamMap.h is included for DISPLAY ONLY — formatZone/parseZone/mapSlotToZone
// inside the text-box lambdas in applyPresentation() (PF-037).
//
// It was previously excluded on purpose, and that reasoning still stands in the
// direction it was aimed: this panel used to convert patch defaults into slot
// values and WRITE them, and that seeding moved to the processor (PF-033, see
// the long note in refreshParamKnobs). The prohibition is on writing slot
// values from here, not on reading the conversion. Reading it is in fact the
// point — the alternative to calling mapSlotToZone is an inline formula in the
// UI layer, which is the exact defect ParamMap.h's own header comment was
// written about ("two halves of one conversion, implemented once each,
// disagreeing").
//
// So: nothing below may assign to an APVTS parameter. The text lambdas convert
// for the eye and hand the result back to JUCE.

ParamGridPanel::ParamGridPanel(PluginForgeProcessor& p)
    : processor(p)
{
    addAndMakeVisible(viewport);
    // We own `content` as a member — the Viewport must NOT delete it (second arg
    // false). setViewedComponent / setScrollBarsShown verified against
    // juce_Viewport.h:76 and :201. Vertical scrollbar only; the content width is
    // fitted to the visible area so a horizontal bar never appears.
    viewport.setViewedComponent(&content, false);
    viewport.setScrollBarsShown(true, false);
}

int ParamGridPanel::contentHeightForCurrentMode() const
{
    const int n = static_cast<int>(controls.size());

    // Horizontal stacks one full-width control per row. A horizontal slider needs
    // WIDTH, not height, and the sqrt grid actively fights it: at 5 or 6 columns
    // the track ends up shorter than its own value box, which is unusable. One
    // per row trades a taller window for a track you can actually aim with.
    if (style == ControlStyle::Horizontal)
        return n * kRowH;

    return ParamGridLayout::rowsFor(n) * kCellH;
}

int ParamGridPanel::preferredContentHeight() const
{
    return contentHeightForCurrentMode();
}

// ⚠️ `params` is the PER-SLOT view (ParamPool::publishedSlots()), not the compact
// list Faust captured. Index i is macro slot i, and an unused slot carries the
// null-zone sentinel remap() writes.
//
// It used to be the compact list, and the difference was invisible while
// ParamPool::remap was positional -- params[i] WAS slot i, so the two views were
// the same object seen twice. Identity-keyed assignment separates them, and this
// panel has to render the one it is ATTACHED to. Passing the compact list here
// after that change bound control i to slotId(i) while displaying the i'th
// parameter in Faust's alphabetical order, so knobs drove parameters they were
// not labelled with (EditorSessionTest scenario 15).
//
// A welcome side effect, not the reason: knob POSITIONS are now stable across a
// regeneration. A param that reclaims slot 7 stays in the same cell of the grid
// instead of shuffling when an alphabetically-earlier control is introduced.
void ParamGridPanel::refreshParamKnobs(const FaustEngine::ParamList& params)
{
    const int slotCount =
        juce::jmin(static_cast<int>(params.size()), ParamPool::POOL_SIZE);

    // ── NO SEEDING HERE. It belongs to the processor, and only to it ────────
    // This function used to seed every mapped slot from the patch defaults, on
    // every compile, unconditionally. That was correct when it was written and
    // became a data-loss bug the moment LoadMode existed (PF-033).
    //
    // The restore path is the failure. setStateInformation replaces the APVTS
    // state with the SAVED values and then recompiles with LoadMode::Iterate
    // precisely so nothing resets them (PluginProcessor.cpp:65-69). The compile
    // succeeds, the callback hops to the message thread, it lands HERE — and the
    // seeding overwrote every restored value with the patch's declared default.
    // So reopening a saved project put every knob back to factory position, but
    // only if the editor happened to be open, which is the same
    // conditional-on-the-UI defect PF-020 fixed, running in the other direction.
    // StatePersistenceTest could not see it: it never constructs an editor.
    //
    // The processor already does this properly. resetMappedSlotsToDefaults()
    // (PluginProcessor.cpp:112-142) covers all 64 slots rather than just the
    // mapped ones, zeroes the unmapped remainder so a stale value cannot reappear
    // under a later patch, uses the same ParamMap conversion, and runs inside the
    // swap protocol's safe window — after the audioBusy drain, before ready=true
    // — which is the only point at which slot values can be rewritten without
    // pushToFaust concurrently reading them. A message-thread write from here had
    // none of those properties.
    //
    // The reason the seeding mattered at all still holds: pushToFaust
    // denormalises, so a slot left at 0.0 maps to its zone MINIMUM (a 20 Hz
    // cutoff = silence). That is exactly what the processor's Fresh path now
    // guarantees, on every load, with or without an editor.

    // ── Rebuild the widgets ─────────────────────────────────────────────────
    // clear() first so each old attachment detaches (Control destroys attachment
    // before widget) before we bind a fresh one to the same slot ID: a slot may
    // switch widget KIND between patches (a knob patch replaced by a toggle patch),
    // and two live attachments on one parameter is undefined.
    controls.clear();
    controls.reserve(static_cast<size_t>(slotCount));

    for (int slot = 0; slot < slotCount; ++slot)
    {
        const auto& p = params[static_cast<size_t>(slot)];

        // A null zone is remap()'s unused-slot sentinel. Skipped rather than
        // rendered, so the grid shows only the controls the patch actually has --
        // the same visible result as before, reached by asking the pool instead
        // of assuming the list was dense.
        if (p.zone == nullptr)
            continue;

        const auto id = ParamPool::slotId(slot);
        Control c;
        c.slot = slot;

        // Keep the metadata so a presentation change can restyle without rebuilding.
        // The zone pointer must NOT survive the copy: it points into the DSP instance
        // that produced it and dangles the moment that instance is deleted
        // (FaustEngine.h:59-64), which this copy outlives.
        c.meta      = p;
        c.meta.zone = nullptr;

        c.label = std::make_unique<juce::Label>();
        c.label->setJustificationType(juce::Justification::centred);
        c.label->setFont(juce::Font(12.0f));
        c.label->setText(juce::String(p.label), juce::dontSendNotification);
        content.addAndMakeVisible(*c.label);

        if (p.kind == FaustEngine::Kind::Button
            || p.kind == FaustEngine::Kind::CheckButton)
        {
            // Toggle-kind params must render as a ToggleButton, never a rotary
            // (docs/ui_design_plan.md §3). ButtonAttachment maps the 0..1 slot to
            // the button's on/off state (juce_AudioProcessorValueTreeState.h:590).
            auto tb = std::make_unique<juce::ToggleButton>();
            c.buttonAtt =
                std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
                    processor.apvts, id, *tb);
            content.addAndMakeVisible(*tb);
            c.widget = std::move(tb);
        }
        else
        {
            // TYPE only. Every styling decision — slider style, text box, readout —
            // belongs to applyPresentation() below, so that a mode change can
            // restyle in place without touching an attachment.
            auto sl = std::make_unique<juce::Slider>();
            c.sliderAtt =
                std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
                    processor.apvts, id, *sl);
            content.addAndMakeVisible(*sl);
            c.widget = std::move(sl);
        }

        applyPresentation(c);
        controls.push_back(std::move(c));
    }

    layoutControls();
    ++refreshCount;     // see refreshCountForTest() in the header
}

void ParamGridPanel::applyPresentation(Control& c)
{
    // The name label follows the geometry, not the widget type, so it is set
    // BEFORE the Slider early-return -- a toggle in Horizontal style needs its
    // label left-aligned too.
    if (c.label != nullptr)
        c.label->setJustificationType(style == ControlStyle::Horizontal
                                          ? juce::Justification::centredLeft
                                          : juce::Justification::centred);

    // Anything that is not a Slider has no style to apply. A ToggleButton returns
    // here, which is why no rotary code below can ever reach a toggle-kind param.
    auto* sl = dynamic_cast<juce::Slider*>(c.widget.get());
    if (sl == nullptr)
        return;

    // ── The readout (PF-037) ────────────────────────────────────────────────
    // The slider's VALUE is the 0..1 slot, and that is correct and must stay:
    // the attachment, the APVTS and the host automation lane all speak slots.
    // Only the TEXT is denormalised, via the same ParamMap the audio path uses.
    //
    // ORDER IS LOAD-BEARING. SliderParameterAttachment's constructor assigns
    // both of these itself, delegating to the parameter's getText()
    // (juce_ParameterAttachments.cpp:118-119) — which for a plain
    // AudioParameterFloat(0..1) is what printed `0.04` for 800 Hz. Ours must be
    // assigned AFTER the attachment exists to win, and refreshParamKnobs calls
    // applyPresentation() after constructing it. Moving this earlier silently
    // restores the bug.
    //
    // `meta` is copied into each lambda rather than captured by reference: the
    // Control can be moved (controls is a vector that grows), and a reference
    // into it would dangle. meta.zone is already null (refreshParamKnobs), so
    // the copy carries no pointer into the DSP.
    sl->textFromValueFunction = [meta = c.meta](double slotValue)
    {
        return ParamMap::formatZone(
            ParamMap::mapSlotToZone(static_cast<float>(slotValue), meta), meta);
    };
    sl->valueFromTextFunction = [meta = c.meta](const juce::String& text)
    {
        return static_cast<double>(
            ParamMap::mapZoneToSlot(ParamMap::parseZone(text, meta), meta));
    };
    // The attachment has already pushed the current value through the OLD text
    // function, so the box is showing a slot number until something repaints.
    // updateText() re-renders it now (juce_Slider.h:844).
    sl->updateText();

    // ── Style override ──────────────────────────────────────────────────────
    // Rotary and Horizontal are user view choices and win over the Faust Kind.
    // Reaching here at all means the widget is a Slider, so the toggle kinds are
    // already excluded by the early return above — a Button/CheckButton is a
    // ToggleButton and is structurally unreachable from this code path, which is
    // what keeps PF-005's promise unconditional rather than style-dependent.
    if (style == ControlStyle::Rotary)
    {
        sl->setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
        sl->setTextBoxStyle(juce::Slider::TextBoxBelow, false, 64, 16);
        // Set explicitly rather than inheriting: JUCE's default arc is
        // 0 .. 2pi with no gap, which reads as a full ring and makes the
        // pointer position ambiguous at the extremes (juce_Slider.h:398-407).
        // 7 o'clock to 5 o'clock is the hardware convention.
        sl->setRotaryParameters(juce::MathConstants<float>::pi * 1.2f,
                                juce::MathConstants<float>::pi * 2.8f,
                                true);
        // Consistent drag feel between styles: without this a rotary maps
        // absolute vertical position, so switching style changes how far the
        // same gesture moves the value.
        sl->setVelocityBasedMode(true);
        return;
    }

    if (style == ControlStyle::Horizontal)
    {
        sl->setSliderStyle(juce::Slider::LinearHorizontal);
        sl->setTextBoxStyle(juce::Slider::TextBoxRight, false, 56, 16);
        sl->setVelocityBasedMode(false);
        return;
    }

    // ControlStyle::Faithful — the widget follows the DSL, not taste.
    sl->setVelocityBasedMode(false);
    switch (c.meta.kind)
    {
        case FaustEngine::Kind::HSlider:   // juce_Slider.h:63/96
            sl->setSliderStyle(juce::Slider::LinearHorizontal);
            sl->setTextBoxStyle(juce::Slider::TextBoxRight, false, 56, 16);
            break;
        case FaustEngine::Kind::VSlider:   // :64/99
            sl->setSliderStyle(juce::Slider::LinearVertical);
            sl->setTextBoxStyle(juce::Slider::TextBoxBelow, false, 56, 16);
            break;
        case FaustEngine::Kind::NumEntry:  // :75/96 — number box w/ inc-dec
            sl->setSliderStyle(juce::Slider::IncDecButtons);
            sl->setTextBoxStyle(juce::Slider::TextBoxLeft, false, 56, 16);
            break;
        case FaustEngine::Kind::Button:
        case FaustEngine::Kind::CheckButton:
        default:                            // :73 — rotary is the fallback
            sl->setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
            sl->setTextBoxStyle(juce::Slider::TextBoxBelow, false, 64, 16);
            break;
    }
}

const char* ParamGridPanel::controlStyleName(ControlStyle s)
{
    switch (s)
    {
        case ControlStyle::Rotary:     return "rotary";
        case ControlStyle::Horizontal: return "horizontal";
        case ControlStyle::Faithful:   break;
    }
    return "faithful";
}

ParamGridPanel::ControlStyle ParamGridPanel::controlStyleFromName(const juce::String& name)
{
    if (name == "rotary")     return ControlStyle::Rotary;
    if (name == "horizontal") return ControlStyle::Horizontal;
    return ControlStyle::Faithful;   // unknown/absent degrades to the shipped layout
}

void ParamGridPanel::setControlStyle(ControlStyle s)
{
    if (s == style)
        return;                       // idempotent: no needless relayout or repaint

    style = s;

    // Restyle in place. Note what is NOT here: no widget is destroyed, no
    // attachment is rebuilt, no APVTS value is written. Each Slider keeps the
    // SliderAttachment it was born with, so the parameter never sees the style
    // change at all -- which is the entire point. A rebuild here would push
    // values back through the attachments and thrash every slot.
    for (auto& c : controls)
        applyPresentation(c);

    // Geometry differs per style (grid cells vs full-width rows), so the content
    // height changes and the shell may want a different window height. resized()
    // re-runs layoutControls() against the new mode.
    resized();
}

void ParamGridPanel::layoutControls()
{
    const int n    = static_cast<int>(controls.size());
    const int cols = ParamGridLayout::columnsFor(n);
    // No `rows` local: the row count is only ever needed as a height, and that now
    // comes from contentHeightForCurrentMode() below.

    // Decide the scrollbar from geometry (not from the Viewport's current, possibly
    // stale, scrollbar state) so the content width is deterministic and no
    // horizontal bar ever appears. getScrollBarThickness() verified juce_Viewport.h:244.
    const int fullW    = viewport.getWidth();
    const int fullH    = viewport.getHeight();
    // Through contentHeightForCurrentMode(), NOT a second `rows * kCellH` — the
    // shell sizes the window from preferredContentHeight() and this lays the cells
    // out; two independent copies of that arithmetic is the kChromeHeight defect
    // (see PluginEditor.h), which drifted in exactly this way.
    const int contentH = contentHeightForCurrentMode();
    const bool vscroll = contentH > fullH;
    const int viewW    = juce::jmax(0, fullW - (vscroll ? viewport.getScrollBarThickness() : 0));

    // Content is at least as tall as the viewport so a short patch fills the region
    // (no dead scroll gap); a tall patch scrolls.
    content.setSize(viewW, juce::jmax(contentH, fullH));

    if (n == 0 || cols == 0 || viewW == 0)
        return;

    // ── Horizontal: one full-width row per control ──────────────────────────
    // Label on the left at a fixed width so the tracks all start at the same x
    // and read as a column; the slider takes the rest of the row.
    if (style == ControlStyle::Horizontal)
    {
        constexpr int kNameW = 96;
        for (int i = 0; i < n; ++i)
        {
            auto& c = controls[static_cast<size_t>(i)];
            auto row = juce::Rectangle<int>(0, i * kRowH, viewW, kRowH).reduced(4, 3);
            c.label->setBounds(row.removeFromLeft(juce::jmin(kNameW, row.getWidth() / 2)));
            if (c.buttonAtt != nullptr)
                c.widget->setBounds(row.withWidth(juce::jmin(row.getWidth(), 90)));
            else
                c.widget->setBounds(row);
        }
        return;
    }

    const int cellW = viewW / cols;
    for (int i = 0; i < n; ++i)
    {
        auto& c = controls[static_cast<size_t>(i)];
        auto cell = juce::Rectangle<int>((i % cols) * cellW,
                                         (i / cols) * kCellH,
                                         cellW, kCellH);
        c.label->setBounds(cell.removeFromTop(kLabelH));
        auto body = cell.reduced(4);

        if (c.buttonAtt != nullptr)
            // A toggle is a fixed-height control; centre a compact box in the cell
            // rather than stretching a checkbox across it.
            c.widget->setBounds(body.withSizeKeepingCentre(
                juce::jmin(body.getWidth(), 90), juce::jmin(body.getHeight(), 28)));
        else
            c.widget->setBounds(body);
    }
}

void ParamGridPanel::resized()
{
    viewport.setBounds(getLocalBounds());
    layoutControls();
}

// ── Test-only observables ───────────────────────────────────────────────────
// Read the widget's ACTUAL runtime type and style rather than re-deriving it
// from the Faust Kind. Re-deriving would make the test agree with
// refreshParamKnobs by construction and prove nothing — the claim under test is
// precisely that the switch above maps Kind to widget the way the header says.

ParamGridPanel::WidgetKind ParamGridPanel::controlKindForTest(int index) const
{
    if (index < 0 || index >= static_cast<int>(controls.size()))
        return WidgetKind::Unknown;

    auto* w = controls[static_cast<size_t>(index)].widget.get();
    if (dynamic_cast<juce::ToggleButton*>(w) != nullptr)
        return WidgetKind::Toggle;

    if (auto* sl = dynamic_cast<juce::Slider*>(w))
    {
        switch (sl->getSliderStyle())
        {
            case juce::Slider::LinearHorizontal: return WidgetKind::HorizontalSlider;
            case juce::Slider::LinearVertical:   return WidgetKind::VerticalSlider;
            case juce::Slider::IncDecButtons:    return WidgetKind::IncDec;
            case juce::Slider::RotaryHorizontalVerticalDrag:
            case juce::Slider::Rotary:
            case juce::Slider::RotaryHorizontalDrag:
            case juce::Slider::RotaryVerticalDrag: return WidgetKind::Rotary;
            default: return WidgetKind::Unknown;
        }
    }
    return WidgetKind::Unknown;
}

juce::String ParamGridPanel::controlLabelForTest(int index) const
{
    if (index < 0 || index >= static_cast<int>(controls.size()))
        return {};
    auto* l = controls[static_cast<size_t>(index)].label.get();
    return l != nullptr ? l->getText() : juce::String();
}

juce::String ParamGridPanel::controlGroupForTest(int index) const
{
    if (index < 0 || index >= static_cast<int>(controls.size()))
        return {};
    return juce::String(controls[static_cast<size_t>(index)].meta.group);
}

double ParamGridPanel::controlValueForTest(int index) const
{
    if (index < 0 || index >= static_cast<int>(controls.size()))
        return 0.0;

    auto* w = controls[static_cast<size_t>(index)].widget.get();
    if (auto* sl = dynamic_cast<juce::Slider*>(w))
        return sl->getValue();
    if (auto* tb = dynamic_cast<juce::ToggleButton*>(w))
        return tb->getToggleState() ? 1.0 : 0.0;
    return 0.0;
}

juce::String ParamGridPanel::controlTextForTest(int index) const
{
    if (index < 0 || index >= static_cast<int>(controls.size()))
        return {};

    auto* w = controls[static_cast<size_t>(index)].widget.get();
    if (auto* sl = dynamic_cast<juce::Slider*>(w))
        // getTextFromValue runs whichever textFromValueFunction is installed
        // (juce_Slider.h:754), which is the whole point: if applyPresentation
        // stopped overriding it, this returns the attachment's slot number.
        return sl->getTextFromValue(sl->getValue());
    if (auto* tb = dynamic_cast<juce::ToggleButton*>(w))
        return tb->getToggleState() ? "On" : "Off";
    return {};
}

const char* ParamGridPanel::widgetKindName(WidgetKind k)
{
    switch (k)
    {
        case WidgetKind::Rotary:           return "Rotary";
        case WidgetKind::HorizontalSlider: return "HorizontalSlider";
        case WidgetKind::VerticalSlider:   return "VerticalSlider";
        case WidgetKind::IncDec:           return "IncDec";
        case WidgetKind::Toggle:           return "Toggle";
        case WidgetKind::Unknown:
        default:                           return "Unknown";
    }
}
