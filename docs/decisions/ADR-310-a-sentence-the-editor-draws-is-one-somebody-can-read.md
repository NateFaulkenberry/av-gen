# ADR-310: A sentence the editor draws is one somebody can read

**Status:** Accepted
**Date:** 2026-09-18
**Context:** ADR-225 (a setting the application does not keep is not a setting), ADR-182 (a probe
that cannot fail proves nothing), ADR-197 (the viewport overlay's labels, found by looking at a
rasterised overlay rather than by reasoning about one).

## Context

The report was one sentence: "make sure all of them wrap text properly, right now text is getting
cut off in places with no way to read it."

It was accurate, and the shape of the problem was not what a reader of the code would have guessed.
Across 19,000 lines of `src/ui` there was exactly **one** `PushTextWrapPos` -- around a shader
compile error in the Control panel -- and **150** `ImGui::SetTooltip` calls with no wrap position at
all. `style.hpp` already had a `hoverTip` helper, with a paragraph of reasoning above it, that
nothing in the application had ever called.

Photographed on `examples/world/glowmere-valley-2-multicam.json` at the size the editor opens at
(1440x900), before any change:

- The Control panel's status line read `... route 48 (beat.pulse -> atmos/Bioluminescent
  Comet/coreIntensity): unknown target parameter 'atmos/` and stopped at the window edge. The rest
  was drawn nowhere. That line is how a project's load warnings reach a person.
- Every modulation route's slider was drawn with its label -- `audio.bass -> material/...` --
  outside the window: a column of identical bars with nothing to say which route each one was.
- The Volume slider was an unlabelled bar with a stray `V` against the border.
- The Settings panel lost `Installation preferences apply across projects and follow this machine.`
  after `proje`, and drew the theme combo box on top of the words `System follows macOS`.
- The Performance panel's frame line lost `over N frames`.
- The Assets list's name column was about fifteen points wide: `el / be / be / Ca / be / ca`, under
  a header that was a single ellipsis.

At 1000x820 -- a size a panel can be dragged to -- the Sequence panel's toolbar ended in a `Catcl`
against the border, and the Edit panel's third snap field was absent altogether. A docked ImGui
panel has no horizontal scrollbar, so neither was reachable by any gesture.

## Decisions

### 1. The rule

**Text the application draws that nobody can read is information it does not have.** This is
ADR-225's rule about settings, applied to the other direction of the same conversation. A label
truncated with an ellipsis that a tooltip repeats is cosmetic. A sentence whose second half is not
drawn is a defect, because the information has no other route to the reader.

### 2. Wrapping is declared once, where a panel is begun

`ControlPanel::drawPanels` holds the only `ImGui::Begin` for a panel in the application. The
`WrapText` guard goes there, and all nineteen panels wrap. It is a guard rather than a pair of calls
because `PushTextWrapPos` must be popped on the window it was pushed on and an early `return` is how
these bodies normally end.

It does **not** reach into child windows: ImGui resets `DC.TextWrapPos` per window, so every
`BeginChild` holding prose carries its own. It also does not reach `BulletText` or `LabelText`,
which render through `RenderText` and ignore the wrap position whatever is pushed -- `bulletWrapped`
exists for the first of those and there is no call site for the second.

### 3. A widget leaves room for its own label

ImGui draws a widget's label *after* the widget, so `SetNextItemWidth(-1)` means "put the label past
the right edge". `SetNextItemWidth(-90)` is the same mistake with a guess where the measurement
should be: ninety points holds `gain` and loses `audio.bass -> particles/emission`. Call sites use
`itemWidthForLabel`, which measures the label in the current font.

### 4. A row that does not fit wraps; it does not run off the side

`sameLineOrWrap` replaces `SameLine()` on a toolbar, asking whether the item about to be submitted
fits before the content region's right edge. A fixed label column (`SameLine(190.0f)`) yields
through `labelColumnX` rather than letting a widget land on the text -- two strings in the same
pixels is worse than one of them being cut.

### 5. The arithmetic is in `ui_logic.hpp`, with a test

Appearance cannot be asserted; a width can. `wrapWidthFor`, `itemWidthBesideLabel`, `labelColumnX`,
`tooltipWrapWidth` and `toolbarItemFits` live beside `stripLanesFor` for the reason that one does:
a bulk rename once made every sequencer lane zero pixels tall while the whole suite stayed green.

The number worth writing down: **ImGui reads a negative wrap position as "do not wrap at all"**
(`CalcWrapWidthForPos` returns 0 below zero; `TextEx` wraps only when `wrap_pos_x >= 0`). Every wrap
width in this application is a subtraction, so a panel dragged narrower than the inset used to
switch wrapping *off* -- the exact failure this pass was called in to fix, arriving by way of the
fix. `kMinWrapWidth` is a floor rather than a clamp to zero because zero, as a *width*, breaks after
every character.

A tooltip needs a number for a second reason: it is an auto-resizing window, so "wrap at the
window's right edge" is circular inside one. `tooltipWrapWidth` takes the smaller of a
forty-character measure and a fraction of the viewport.

## What this does not fix

Tab labels still ellipsise (`World Effe…`, `Auto-direc…`) -- that is ImGui's tab bar, the full name
is in the window, and it is the cosmetic case the rule excludes. The Assets name column is still
truncated in a 275-point dock, because no rule makes an asset path fit there; it is now truncated
where the rows can be told apart rather than where they cannot.

## Evidence

Before and after captures at 1440x900 and 1000x820 on the owner's project, per panel. Per ADR-182,
the before picture is the control: a fix with no before picture is an assertion. Seven panels --
Modulation, Cameras, Render, Parameters, Graph, Composition, Auto-director -- were photographed
clipped-free before any change and are unchanged after it.
