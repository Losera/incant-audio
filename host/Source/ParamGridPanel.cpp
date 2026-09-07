#include "ParamGridPanel.h"
#include "ArchetypeLayout.h"
#include "ParamGridLayout.h"
#include "ParamMap.h"
#include "Theme.h"
#include "ForgeLookAndFeel.h"   // resolveThemeFont -- see its header comment
#include "GeneratedFaceLookAndFeel.h"   // A3c -- displayFont/readoutFont dispatch
#include <algorithm>   // find_if, stable_sort, count_if — deriveLayoutFromGroups + applyUiIr
#include <functional>  // std::hash — derivePalette
#include <set>         // canonical sorted group names — derivePalette
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
    // Sectioned layout has its own row geometry (a heading row per section
    // plus one row per control, regardless of style) -- checked first, same
    // as layoutControls() checks activeSections first. Before applyUiIr() had
    // a caller this branch was unreachable, so the sqrt-grid formula below
    // was the only one the shell's window-size request had ever exercised;
    // wiring the renderer made this the load-bearing branch for any grouped
    // patch, so it has to agree with what layoutSectioned() actually lays out.
    if (! activeSections.empty())
        return contentHeightForSections();

    const int n = static_cast<int>(controls.size());

    // Horizontal stacks one full-width control per row. A horizontal slider needs
    // WIDTH, not height, and the sqrt grid actively fights it: at 5 or 6 columns
    // the track ends up shorter than its own value box, which is unusable. One
    // per row trades a taller window for a track you can actually aim with.
    if (style == ControlStyle::Horizontal)
        return n * kRowH;

    return ParamGridLayout::rowsFor(n) * kCellH;
}

int ParamGridPanel::contentHeightForSections() const
{
    // ADR-035 gap 4 / session 018 A4, corrected on fix/pedal-layout-packing:
    // layoutFor() is now TOTAL -- every archetype, including "pedal",
    // "utility", "", or a name this build predates, gets a real geometry via
    // ArchetypeLayout::layoutFor(). There is no second, flat-sum formula left
    // to keep in sync with layoutSectioned() -- the "one height function"
    // principle ParamGridPanel.h's header states now holds for every Layout,
    // not just four archetype names, closing the exact kChromeHeight defect
    // shape a second formula here used to risk.
    //
    // Passing viewport.getWidth() unconditionally is safe even when it is
    // still 0 (this function is called from contentHeightForCurrentMode(),
    // which feeds the shell's window-size REQUEST -- it can run before the
    // shell has granted a width at all): packing (ArchetypeLayout.h's
    // perRowFor()) is a pure function of controlCount/span, never of pixel
    // width, so row COUNT -- and therefore height -- cannot change once a
    // real width is granted. Only the Rect's `w`/`x` fields depend on width,
    // and this function discards them.
    std::vector<ArchetypeLayout::SectionInput> inputs;
    inputs.reserve(activeSections.size());
    for (const auto& section : activeSections)
    {
        std::vector<bool> large(section.controls.size(), false);
        for (size_t i = 0; i < section.controls.size(); ++i)
            large[i] = (section.controls[i].size == "lg");
        inputs.push_back({ static_cast<int>(section.controls.size()), section.span, std::move(large) });
    }

    return ArchetypeLayout::layoutFor(activeArchetype.toStdString(), inputs, viewport.getWidth(),
                                       kCellH, kHeadingH, kSectionGapH)
               .contentHeight;
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

    // ── Drop the previous IR before the controls it points into ─────────────
    // activeSections/irLookup are NOT cleared by controls.clear() below -- they
    // are separate members, deliberately (the header comment on activeSections
    // says so: "so a presentation change doesn't destroy the IR"). But
    // irLookup's values are raw Control* into `controls`, and layoutControls()
    // branches on `! activeSections.empty()` BEFORE applyUiIr() gets a chance
    // to rebuild either of them for the new patch. Left uncleared, a compile
    // whose control count SHRINKS relative to the previous one is a real
    // heap-use-after-free, reproduced live (revert these two lines and run
    // EditorSessionTest scenario 34 under this target's own ASAN build --
    // host/CMakeLists.txt:649-650 -- it SEGVs inside layoutSectioned()).
    // `controls` is `.reserve(POOL_SIZE)`d unconditionally on every call
    // (`params` is ParamPool's 64-slot per-slot view, so params.size() is
    // always 64), so its capacity reaches 64 on a Session's first compile and
    // the buffer's address never moves again -- a GROWING recompile just
    // placement-constructs new Controls over the same addresses stale
    // pointers already referenced, which is silently WRONG (a stale pointer
    // ends up naming a live but different control) rather than a crash. A
    // SHRINKING recompile leaves the indices past the new, smaller size
    // destructed by clear() and never reconstructed: each abandoned Control's
    // widget unique_ptrs already deleted their Label/Slider before this
    // function returns, so the stale pointer irLookup still hands out for
    // that slot dereferences an object that is actually gone. Clearing here
    // makes refreshParamKnobs' own trailing layoutControls() call take the
    // safe flat-grid branch; the shell's follow-up applyUiIr() call
    // (PluginEditor.cpp) then rebuilds both against the new controls and
    // re-lays-out, this time correctly.
    activeSections.clear();
    irLookup.clear();

    // ADR-022 §3 / T7: recompute this compile's accent before the per-control
    // loop below, so applyPresentation() (called once per control from inside
    // it) always sees the palette for the patch actually being built, never a
    // stale one from the previous compile.
    currentPalette = derivePalette(params, processor.isInstrumentForTest());
    currentTitle   = deriveTitle(params, processor.isInstrumentForTest());

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
        // Font is set by applyPresentation() below (called once per control,
        // right after this loop), not here -- A3c made the label font
        // theme-aware (activeTheme.display), and activeTheme is not settled
        // for THIS compile until applyUiIr() runs, which happens after
        // refreshParamKnobs() returns. Setting it twice (a stale value here,
        // the real one moments later in applyPresentation) would be dead
        // code, not a correctness issue, but is worth not writing anyway.
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
            // The custom LookAndFeel queries hover/drag state while painting.
            // Component does not repaint on mouse movement unless opted in
            // (juce_Component.h:627-631), so without this the visual state would
            // update only when some unrelated event happened to invalidate it.
            sl->setRepaintsOnMouseActivity(true);
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
    {
        c.label->setJustificationType(style == ControlStyle::Horizontal
                                          ? juce::Justification::centredLeft
                                          : juce::Justification::centred);
        // A3c: theme-aware label font. Re-read every call (build time AND
        // every setControlStyle() restyle) rather than cached anywhere, same
        // reasoning as the accent lines further down this function --
        // currentLabelFont()'s own header comment explains why a single
        // shared decision point matters here.
        c.label->setFont(currentLabelFont());
    }

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

    // ── Accent (ADR-022 §3 / T7; face-aware since the A3d fix) ──────────────
    // Applied here, not in refreshParamKnobs(), because applyPresentation()
    // already "owns every styling decision" (header comment) and re-runs on
    // every setControlStyle() restyle -- so a style change can never leave a
    // control showing the previous compile's accent. Scoped to this ONE
    // Slider via setColour() rather than the shared ForgeLookAndFeel's
    // ColourScheme, which is process-wide and would recolour the host chrome
    // (title, prompt panel) along with the grid. thumb/track/rotarySliderFill
    // are the three colourIds LookAndFeel_V4 actually reads for a Slider's
    // value (juce_LookAndFeel_V4.cpp:1029,1024,1068) -- ToggleButton::
    // tickColourId is deliberately not touched here: it derives from the
    // scheme's defaultText, not defaultFill/highlightedFill
    // (juce_LookAndFeel_V4.cpp:1342), so a toggle was never going to pick this
    // up regardless, and Control's early return above already keeps this
    // whole block unreachable for one anyway.
    //
    // faceAccentActive/faceAccentColour (A3d fix): a face's theme accent is
    // not known at the moment a control is first built -- PluginEditor's
    // compile-success callback calls applyGeneratedFace() AFTER
    // refreshParamKnobs()/applyUiIr() -- so this cannot simply always use
    // currentPalette. Both members are set by setFaceAccent(), called
    // whenever a face attaches, changes, or detaches; reading them HERE
    // (rather than only re-colouring existing widgets from setFaceAccent()
    // itself) is what keeps a later setControlStyle() restyle from silently
    // reverting an attached face's accent back to the heuristic -- see this
    // member's own declaration in the header for the reproduction.
    const auto accent = faceAccentActive ? faceAccentColour : currentPalette;
    sl->setColour(juce::Slider::thumbColourId, accent);
    sl->setColour(juce::Slider::trackColourId, accent);
    sl->setColour(juce::Slider::rotarySliderFillColourId, accent);

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
        // PF-039: this used to be `case Button: case CheckButton: default:`,
        // falling into a rotary -- described as "the live fallback" in
        // docs/ui_design_plan.md §3, but structurally unreachable: `sl` above
        // already proved this control is a juce::Slider*, and Button/
        // CheckButton always become a ToggleButton instead (the early return
        // at the top of applyPresentation, PF-005's promise). No generated
        // plugin has ever shown a rotary via this arm. Kind::Meter (added
        // 2026-08-02) is equally unreachable here for a different reason: a
        // meter is never writable (isWritable(), FaustEngine.h), so it never
        // gets a ParamPool slot and never becomes a Control in the first
        // place (see T4/PF-052 -- meters need their own rendering path, not
        // this one). Asserting instead of silently defaulting turns "believed
        // unreachable" into "enforced unreachable": if either invariant ever
        // breaks, this fires instead of quietly mislabelling a control.
        case FaustEngine::Kind::Button:
        case FaustEngine::Kind::CheckButton:
        case FaustEngine::Kind::Meter:
            jassertfalse;
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
    // Headings belong to whichever branch below actually runs: rebuilt by
    // layoutSectioned() when taken, left empty (so paint() draws nothing) on
    // the flat-grid path. Clearing once here rather than in each branch is
    // what keeps a stale heading from a PREVIOUS sectioned patch from surviving
    // a later flat-grid one. repaint() now, unconditionally: the flat-grid
    // path has no other repaint call and would otherwise leave a stale heading
    // drawn on screen with nothing left to invalidate it.
    content.headings.clear();
    content.repaint();

    // Sectioned layout: when a UI IR has defined sections, use the sectioned
    // path instead of the default sqrt-grid. Controls not named in the IR are
    // already appended to the trailing "Parameters" section by applyUiIr().
    if (! activeSections.empty())
    {
        layoutSectioned();
        return;
    }

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

namespace
{
    // Canonical section rank (ADR-022 Track 1.2 amendment). Faust reports
    // groups ALPHABETICALLY (FaustEngine.h's `ParamInfo::group` doc), which
    // reads as Env -> Filter -> Fx -> Osc for the flagship grouped fixture --
    // not how a musician scans a synth panel (Osc -> Filter -> Env -> Fx).
    // Substring match, case-insensitive, against the group's own name (groups
    // in every fixture examined are flat single-segment names; a nested path
    // like "Filter/Cutoff" still matches on "filter" via contains()).
    // Returns -1 for no match -- deriveLayoutFromGroups sorts unranked groups
    // after every ranked one, keeping first-seen order among themselves.
    int sectionRank(const juce::String& group)
    {
        struct Bucket { int rank; std::initializer_list<const char*> keys; };
        static const Bucket kBuckets[] =
        {
            { 0, { "osc", "source", "input" } },
            { 1, { "filter" } },
            { 2, { "env", "amp" } },
            { 3, { "mod", "lfo" } },
            { 4, { "fx" } },
            { 5, { "out", "master" } },
        };
        const auto g = group.toLowerCase();
        for (const auto& b : kBuckets)
            for (const auto* key : b.keys)
                if (g.contains(key))
                    return b.rank;
        return -1;
    }
}

UiIr::Layout ParamGridPanel::deriveLayoutFromGroups(const FaustEngine::ParamList& params,
                                                     bool isInstrument)
{
    // One entry per distinct group value, in first-encounter order among the
    // OCCUPIED slots. `params` is ParamPool's per-slot view (see the ⚠️ on
    // refreshParamKnobs above) -- a null zone is the unused-slot sentinel and
    // is skipped exactly like refreshParamKnobs skips it, so this function
    // reports on the same control set the grid is about to render, not on 64
    // pool slots most of which are empty.
    struct GroupEntry
    {
        juce::String group;
        int firstIndex = 0;
        std::vector<std::string> labels;
    };
    std::vector<GroupEntry> groups;
    int occupiedSlots = 0;

    for (int i = 0; i < static_cast<int>(params.size()); ++i)
    {
        const auto& p = params[static_cast<size_t>(i)];
        if (p.zone == nullptr)
            continue;
        ++occupiedSlots;

        const auto group = juce::String(p.group);
        auto it = std::find_if(groups.begin(), groups.end(),
                               [&group](const GroupEntry& e) { return e.group == group; });
        if (it == groups.end())
        {
            groups.push_back({ group, i, {} });
            it = std::prev(groups.end());
        }
        it->labels.push_back(p.label);
    }

    const int nonEmptyGroups = static_cast<int>(
        std::count_if(groups.begin(), groups.end(),
                      [](const GroupEntry& e) { return e.group.isNotEmpty(); }));

    // Suppression threshold. A heading over one or two knobs, or a single
    // section wrapping the entire patch, is worse than the flat grid it would
    // replace -- an empty `sections` list hands the params straight back to
    // applyUiIr's "fall through to layoutControls()" path (checked via
    // ir.sections.empty() since ADR-029 §4, not ir.schema != 1 -- see that
    // function's own comment). Components are independent of section
    // suppression -- a 3-knob effect still has a real answer for "does this
    // have a keyboard" -- so this is schema 2 with components populated and
    // sections left empty, not UiIr::empty()'s schema 0.
    if (nonEmptyGroups <= 1 || occupiedSlots < 4)
    {
        UiIr::Layout layout;
        layout.schema = 2;
        layout.components = deriveComponents(params, isInstrument);
        return layout;
    }

    // Rank, unknown-groups-last, ties broken by first-seen order -- so the
    // catch-all empty-group bucket (no keyword can match "") always sorts
    // after every named group, matching applyUiIr's own "unnamed controls
    // trail" convention below.
    constexpr int kUnranked = 1000;
    constexpr int kUngrouped = 1001;
    std::stable_sort(groups.begin(), groups.end(),
        [](const GroupEntry& a, const GroupEntry& b)
        {
            const auto rankOf = [](const GroupEntry& e)
            {
                if (e.group.isEmpty()) return kUngrouped;
                const int r = sectionRank(e.group);
                return r < 0 ? kUnranked : r;
            };
            const int ra = rankOf(a);
            const int rb = rankOf(b);
            if (ra != rb) return ra < rb;
            return a.firstIndex < b.firstIndex;
        });

    UiIr::Layout layout;
    layout.schema = 2;   // ADR-029 §4: was 1; components below is the addition
    layout.components = deriveComponents(params, isInstrument);

    // Fold single-control groups (fix/pedal-layout-packing). A vgroup wrapping
    // one control renders as its own heading + hairline + row + trailing gap --
    // "its own tiny plugin", not part of a grouped panel. The live shape this
    // exists for: "warm analog tape saturation effect with input drive, tone,
    // output level, and a wet/dry mix" -> four one-control vgroups -> four
    // one-knob sections, half the panel dead. Two outcomes, both keyed on
    // whether ANY group is real (>= 2 controls) rather than on the average
    // (an average of two is cleared by [1,1,4] and by [3,1] alike, leaving a
    // lone-knob heading standing in both):
    //   * no real group  -> one neutral "Controls" section holding everything
    //     (the tape case; ranked order is preserved by the sort above).
    //   * some real group -> fold each singleton forward into the preceding
    //     real group (or, for a leading singleton, into the first real group).
    //     [2,2,1] stays FILTER(2) + OUTPUT(3), not one flat grid -- the
    //     grouping the generation *did* get right survives.
    // `occupiedSlots < 4` is already returned empty above, so the smallest
    // input that reaches here is 4 controls in >= 2 groups.
    const auto isReal = [](const GroupEntry& g) { return g.labels.size() >= 2; };
    const int realGroups = static_cast<int>(std::count_if(groups.begin(), groups.end(), isReal));

    if (static_cast<int>(groups.size()) > 1 && realGroups < static_cast<int>(groups.size()))
    {
        if (realGroups == 0)
        {
            GroupEntry merged { "Controls", groups.front().firstIndex, {} };
            for (const auto& g : groups)
                merged.labels.insert(merged.labels.end(), g.labels.begin(), g.labels.end());
            groups.clear();
            groups.push_back(std::move(merged));
        }
        else
        {
            std::vector<GroupEntry> folded;
            std::vector<std::string> pending;   // leading singletons, no real group kept yet
            for (auto& g : groups)
            {
                if (isReal(g))
                {
                    g.labels.insert(g.labels.begin(), pending.begin(), pending.end());
                    pending.clear();
                    folded.push_back(std::move(g));
                }
                else if (! folded.empty())
                    folded.back().labels.insert(folded.back().labels.end(),
                                                g.labels.begin(), g.labels.end());
                else
                    pending.insert(pending.end(), g.labels.begin(), g.labels.end());
            }
            groups = std::move(folded);   // realGroups >= 1, so pending is always drained
        }
    }

    for (const auto& g : groups)
    {
        UiIr::Section section;
        section.id = g.group.isEmpty()
                       ? "unassigned"
                       : g.group.toLowerCase().replaceCharacter(' ', '-').toStdString();
        section.title = g.group.isEmpty() ? "Parameters" : g.group.toStdString();
        section.span = 1;
        for (const auto& label : g.labels)
        {
            UiIr::ControlRef ref;
            ref.paramLabel = label;
            section.controls.push_back(std::move(ref));
        }
        layout.sections.push_back(std::move(section));
    }
    return layout;
}

// ADR-029 §4. keyboard mirrors the exact bool PluginEditor's includeKeyboard
// already gates on (FaustEngine::isInstrument() via
// processor.isInstrumentForTest(), 89268ec) -- this does not introduce a
// second, potentially-diverging notion of "is this playable". sampleBrowser
// is always true, restating 89268ec's explicit decision rather than changing
// it.
//
// meter CANNOT currently be true when called from refreshParamKnobs'
// call site, and that is not a bug in the loop below -- it is PF-052, still
// open. `params` here is ParamPool's PER-SLOT view (see the callback comment
// at PluginEditor.cpp:188 and deriveLayoutFromGroups' own note above), and
// ParamPool::remap (ParamPool.cpp:69-77) marks every Kind::Meter ineligible
// for a slot before this function ever sees the list -- "pushToFaust
// consequently never sees them" is that file's own words for exactly this
// discard. The RAW FaustEngine::ParamList (before paramPool.remap()) still
// has them, at PluginProcessor.cpp:558, four calls upstream of here.
// The loop below is still correct AS A FUNCTION -- it does the right thing
// with whatever list it is given, verified directly against a hand-built
// ParamList in EditorSessionTest's scenario 41 -- and needs no further
// change once PF-052 either widens what reaches refreshParamKnobs or gives
// ParamGridPanel a second, meter-carrying input. Landing it now, honestly
// inert against production's real params view, is cheaper than re-deriving
// this exact loop when PF-052 lands.
UiIr::Components ParamGridPanel::deriveComponents(const FaustEngine::ParamList& params,
                                                   bool isInstrument)
{
    UiIr::Components c;
    c.keyboard = isInstrument;
    c.sampleBrowser = true;
    for (const auto& p : params)
    {
        if (p.zone != nullptr && p.kind == FaustEngine::Kind::Meter)
        {
            c.meter = true;
            break;
        }
    }
    return c;
}

// Heuristic per-generation accent (ADR-022 §3 / T7). Distinct non-empty group
// names go in canonically SORTED (not first-seen, unlike deriveLayoutFromGroups
// above) so the palette cannot flip just because a regeneration reordered which
// group Faust happened to report first among an otherwise-unchanged set --
// std::set gives that for free. Folds in isInstrument so an instrument and an
// effect with the same group names are not guaranteed the same accent.
//
// No manifest concern: this is a pure function of compile-time facts already
// present in `params` (no wall-clock, no RNG, no container-iteration-order
// dependence), so re-running the gallery against an unchanged fixture always
// derives the same accent -- nothing here can churn UiDesignGallery's
// reference manifest, which is why the contract's hazard note about pinning a
// seed does not apply to this implementation.
namespace
{
    // Shared by derivePalette() and deriveTitle() below (ADR-029 §5) so a
    // patch's title and its accent colour are guaranteed to agree -- both are
    // formattings of the SAME index, never two separate hashes that could
    // drift out of correspondence.
    size_t paletteIndex(const FaustEngine::ParamList& params, bool isInstrument)
    {
        std::set<std::string> groupNames;
        for (const auto& p : params)
            if (p.zone != nullptr && ! p.group.empty())
                groupNames.insert(p.group);

        std::string key = isInstrument ? "instrument|" : "effect|";
        for (const auto& g : groupNames)
        {
            key += g;
            key += '|';
        }

        return std::hash<std::string>{}(key) % Theme::GeneratedAccent::swatches.size();
    }
}

juce::Colour ParamGridPanel::derivePalette(const FaustEngine::ParamList& params,
                                            bool isInstrument)
{
    return Theme::GeneratedAccent::swatches[paletteIndex(params, isInstrument)];
}

// ADR-029 §5: a name from the same inputs above, instead of a colour index.
// The four accent names are the exact vocabulary Theme.h's GeneratedAccent
// swatches are already commented with (ember/amber/rust/coral) -- not an
// invented wordlist -- so this costs zero new captured data, per the ADR.
juce::String ParamGridPanel::deriveTitle(const FaustEngine::ParamList& params,
                                          bool isInstrument)
{
    static constexpr const char* kAccentNames[] = { "Ember", "Amber", "Rust", "Coral" };
    static_assert(sizeof(kAccentNames) / sizeof(kAccentNames[0])
                      == Theme::GeneratedAccent::swatches.size(),
                  "kAccentNames must name every GeneratedAccent swatch, in the same order");

    const auto index = paletteIndex(params, isInstrument);
    return juce::String(kAccentNames[index]) + (isInstrument ? " Instrument" : " Effect");
}

// ── UI IR rendering (ADR-024 / Phase 1a) ────────────────────────────────────
// Applies a renderer-agnostic sectioned layout. The IR controls GROUPING and
// COLUMN SPAN only — widget type still comes from applyPresentation() and the
// Faust Kind. When no IR is present, or it names nothing, this is a no-op and
// the default sqrt-grid rules.
//
// A parameter that is compiled but NOT named in the IR is appended to a
// trailing "Parameters" section rather than dropped — an invisible parameter is
// a defect the panel must not introduce (mirrors the B3 note in session 001).
void ParamGridPanel::applyUiIr(const UiIr::Layout& ir)
{
    activeSections.clear();
    activeArchetype = juce::String(ir.archetype);
    activeTokens = juce::String(ir.tokens);
    activeComponents = ir.components;
    // UiIr schema 3 (ADR-035 Step 3). Stored for observability and for
    // resized()/paint() consumers that will follow (the arc-knob geometry,
    // A3d); the LookAndFeel swap this theme drives is owned by
    // PluginForgeEditor::applyGeneratedFace(), which reads the same Layout.
    activeTheme = ir.theme;

    // ADR-029 §4: checks ir.sections directly rather than ir.schema != 1, so a
    // schema-2 Layout with components but no sections (deriveLayoutFromGroups'
    // own suppression case, below) falls through to the SAME flat-grid path a
    // schema-0 "no IR" Layout always has -- schema alone no longer implies
    // "has sections to apply" now that a Layout can carry components with an
    // empty sections list. ir.schema < 1 still rejects "no IR"/unrecognised
    // schemas exactly as the old != 1 check did.
    if (ir.schema < 1 || ir.sections.empty() || controls.empty())
    {
        layoutControls();
        return;
    }

    // The IR addresses params by their FaustEngine label (the same key
    // ParamPool::slotId uses for the identity scheme). Duplicate labels within
    // a section are not allowed; the first match wins and the rest fall through
    // to the trailing section.
    std::vector<Control*> unplaced;
    for (auto& c : controls)
        unplaced.push_back(&c);

    // Copy only the sections that name at least one known control.
    for (const auto& section : ir.sections)
    {
        UiIr::Section s;
        s.id = section.id;
        s.title = section.title;
        s.span = section.span;
        for (const auto& ref : section.controls)
        {
            auto it = std::find_if(unplaced.begin(), unplaced.end(),
                                   [&ref](const Control* c) {
                                       return c->meta.label == ref.paramLabel;
                                   });
            if (it == unplaced.end())
                continue;      // unknown label — silently ignored, never a crash
            unplaced.erase(it);
            s.controls.push_back(ref);
        }
        if (! s.controls.empty())
            activeSections.push_back(std::move(s));
    }

    // Any control the IR did not mention goes in a trailing section.
    if (! unplaced.empty())
    {
        UiIr::Section trailing;
        trailing.id = "unassigned";
        trailing.title = "Parameters";
        for (auto* c : unplaced)
        {
            UiIr::ControlRef ref;
            ref.paramLabel = c->meta.label;
            ref.style = "";
            ref.size = "";
            trailing.controls.push_back(ref);
        }
        activeSections.push_back(std::move(trailing));
    }

    // A Section stores ControlRef values; layoutControls() needs the actual
    // widget pointers. Rather than re-search per layout pass, map ref → control
    // now so the geometry pass below can consult it.
    irLookup.clear();
    for (const auto& section : activeSections)
        for (const auto& ref : section.controls)
        {
            auto it = std::find_if(controls.begin(), controls.end(),
                                   [&ref](const Control& c) {
                                       return c.meta.label == ref.paramLabel;
                                   });
            if (it != controls.end())
                irLookup[ref.paramLabel] = &*it;
        }

    layoutControls();
}

// ── Sectioned geometry (used when activeSections is non-empty) ─────────────
// Rows are computed per section: a heading row (section title) then one row of
// cells per control, honouring the control's span. Column count derives from the
// widest section rather than the global sqrt — a 12-param synth with a 2-wide
// filter section reads as intended, not squashed.
void ParamGridPanel::placeControlInCell(Control& c, juce::Rectangle<int> cell)
{
    c.label->setBounds(cell.removeFromTop(kLabelH));
    auto body = cell.reduced(4);
    if (c.buttonAtt != nullptr)
        c.widget->setBounds(body.withSizeKeepingCentre(
            juce::jmin(body.getWidth(), 90), juce::jmin(body.getHeight(), 28)));
    else
        c.widget->setBounds(body);
}

void ParamGridPanel::layoutSectioned()
{
    if (controls.empty() || activeSections.empty())
        return;

    const int fullW = viewport.getWidth();
    if (fullW == 0)
        return;

    // A3c: computed once per pass and handed to `content` as plain data,
    // same "decide once, paint reads" split `content.headings` itself
    // already uses -- kept consistent with that sibling field even though
    // ContentArea::paint() could now make this same dynamic_cast call
    // itself (displayFont() no longer needs an enum argument only
    // ParamGridPanel had -- see GeneratedFaceLookAndFeel.h's
    // themeDisplayEnum for why that changed). Computed BEFORE the archetype
    // dispatch below (merge note: A4 landed on main while this branch was in
    // flight) so it applies uniformly whichever geometry path -- the
    // ArchetypeLayout early-return or the grid fallback -- actually places
    // this pass's headings.
    if (auto* faceLnf = dynamic_cast<GeneratedFaceLookAndFeel*>(&content.getLookAndFeel()))
        content.sectionTitleFont =
            faceLnf->displayFont(Theme::Type::sectionTitle().getHeight());
    else
        content.sectionTitleFont = resolveThemeFont(content, Theme::Type::sectionTitle());

    // ADR-035 gap 4 / session 018 A4, corrected on fix/pedal-layout-packing:
    // ArchetypeLayout::layoutFor() is now TOTAL -- "pedal", "utility", "", or
    // a name this build predates all get a real geometry (row()), not the
    // old ungeometried grid fallback that used to live below this comment.
    // That fallback placed every control at x=0, one per row, and gave a
    // `size:"lg"` control a double-width box at that SAME x=0 -- since JUCE
    // centres a rotary slider within whatever bounds it is handed, the `lg`
    // knob's visual centre landed at a different x than every `md` sibling's.
    // Both defects are gone with the fallback: every archetype now packs
    // through the same stackColumn()-based geometry, and `size:"lg"` means
    // "this control gets its own full-width row" (ArchetypeLayout.h).
    const auto archetype = activeArchetype.toStdString();
    std::vector<ArchetypeLayout::SectionInput> inputs;
    inputs.reserve(activeSections.size());
    for (const auto& section : activeSections)
    {
        std::vector<bool> large(section.controls.size(), false);
        for (size_t i = 0; i < section.controls.size(); ++i)
            large[i] = (section.controls[i].size == "lg");
        inputs.push_back({ static_cast<int>(section.controls.size()), section.span, std::move(large) });
    }

    const auto result = ArchetypeLayout::layoutFor(archetype, inputs, fullW,
                                                     kCellH, kHeadingH, kSectionGapH);

    // 4px inset on every heading rect so a section title reads with a
    // consistent left margin regardless of archetype.
    for (size_t s = 0; s < activeSections.size(); ++s)
    {
        const auto& h = result.headings[s];
        auto heading = juce::Rectangle<int>(h.x + 4, h.y, juce::jmax(0, h.w - 8), h.h);
        content.headings.push_back({ heading, juce::String(activeSections[s].title) });
    }

    for (const auto& pc : result.controls)
    {
        const auto& ref = activeSections[static_cast<size_t>(pc.section)]
                               .controls[static_cast<size_t>(pc.index)];
        auto it = irLookup.find(ref.paramLabel);
        if (it == irLookup.end())
            continue;
        placeControlInCell(*it->second,
                            juce::Rectangle<int>(pc.bounds.x, pc.bounds.y, pc.bounds.w, pc.bounds.h));
    }

    content.setSize(fullW, juce::jmax(result.contentHeight, viewport.getHeight()));
    content.repaint();   // headings were just (re)populated above
}

void ParamGridPanel::ContentArea::paint(juce::Graphics& g)
{
    // A3c: sectionTitleFont is decided by layoutSectioned() (ParamGridPanel
    // has activeTheme in scope; this class does not), not re-derived here --
    // see that member's own header comment.
    g.setFont(sectionTitleFont);
    for (const auto& h : headings)
    {
        g.setColour(Theme::textSecondary);
        g.drawText(h.title.toUpperCase(), h.bounds, juce::Justification::centredLeft);

        // A hairline under the title, not a filled card behind every control:
        // T3's "cell backgrounds" is deliberately NOT built here. Card-style
        // backgrounds change how every existing style (Faithful/Rotary/
        // Horizontal) and every fixture reads, which is exactly the kind of
        // visual call COLLABORATION.md §1 reserves for a human looking at a
        // gallery contact sheet, not a default reached for while wiring
        // tooltips. A hairline is additive and reversible either way.
        g.setColour(Theme::outline);
        g.drawHorizontalLine(h.bounds.getBottom(),
                             static_cast<float>(h.bounds.getX()),
                             static_cast<float>(h.bounds.getRight()));
    }
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

juce::String ParamGridPanel::controlStyleForTest(int index) const
{
    if (index < 0 || index >= static_cast<int>(controls.size()))
        return {};
    return juce::String(controls[static_cast<size_t>(index)].meta.style);
}

juce::String ParamGridPanel::controlOrientationForTest(int index) const
{
    if (index < 0 || index >= static_cast<int>(controls.size()))
        return {};
    switch (controls[static_cast<size_t>(index)].meta.orientation)
    {
        case FaustEngine::GroupOrientation::Horizontal: return "horizontal";
        case FaustEngine::GroupOrientation::Vertical:   return "vertical";
        case FaustEngine::GroupOrientation::Tab:        return "tab";
        case FaustEngine::GroupOrientation::None:       return "none";
    }
    return "none";
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

juce::Colour ParamGridPanel::controlWidgetColourForTest(int index, int colourId) const
{
    if (index < 0 || index >= static_cast<int>(controls.size()))
        return {};
    // Component::findColour, not the LookAndFeel's -- see the header comment
    // on why that distinction is the entire point of this accessor.
    return controls[static_cast<size_t>(index)].widget->findColour(colourId);
}

juce::String ParamGridPanel::controlLabelFontNameForTest(int index) const
{
    if (index < 0 || index >= static_cast<int>(controls.size())
        || controls[static_cast<size_t>(index)].label == nullptr)
        return {};
    return controls[static_cast<size_t>(index)].label->getFont().getTypefaceName();
}

// A3d fix (see ParamGridPanel.h's header comment on this method, and on the
// faceAccentActive/faceAccentColour members, for the full story). Two jobs,
// both necessary:
//   1. Persist the decision so a LATER applyPresentation() call (e.g. from
//      setControlStyle()) keeps using it, not just the controls that exist
//      right now.
//   2. Re-colour the controls that already exist, immediately -- a face
//      attaching does not by itself trigger a fresh applyPresentation() pass
//      over anything, so without this loop the fix would only take effect on
//      the NEXT compile, not the one that just requested this face.
// Skips anything that is not a Slider by the same dynamic_cast guard
// applyPresentation() itself uses -- a toggle-kind control was never touched
// by the original colours either.
// A3c: theme-aware label font -- theme.display via the attached
// GeneratedFaceLookAndFeel if one is attached (content's own LookAndFeel,
// walked via the normal parent-chain lookup resolveThemeFont already relies
// on elsewhere in this file), else the shipped Theme::Type::label(). A
// dynamic_cast, not a stored bool, because "is a face attached" and "which
// LookAndFeel is actually walking the tree right now" must never be able to
// disagree -- the two are the same fact asked two different ways.
juce::Font ParamGridPanel::currentLabelFont() const
{
    if (auto* faceLnf = dynamic_cast<GeneratedFaceLookAndFeel*>(&content.getLookAndFeel()))
        return faceLnf->displayFont(Theme::Type::label().getHeight());
    return resolveThemeFont(content, Theme::Type::label());
}

// A3d fix, extended by A3c. Re-applies applyPresentation() to every control
// rather than a bespoke colour-only loop (which is what this function used to
// be) -- applyPresentation() already "owns every styling decision"
// (its own header comment), so calling it again is the SAME operation
// setControlStyle() already relies on being idempotent, just triggered by a
// different event (a face attaching/detaching/changing rather than a style
// button). This is what makes the A3c label font AND the A3d accent both
// take effect together, from one call, the moment PluginEditor::
// applyGeneratedFace() invokes this -- neither can drift out of sync with
// the other because both are read from the SAME re-application pass.
void ParamGridPanel::setFaceAccent(bool faceActive, juce::Colour faceAccent)
{
    faceAccentActive = faceActive;
    faceAccentColour = faceAccent;

    for (auto& c : controls)
        applyPresentation(c);
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
