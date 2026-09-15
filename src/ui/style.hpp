#pragma once

// Shared interaction and drawing primitives: the part of the application's visual language that is
// made of Dear ImGui calls.
//
// `theme.hpp` owns *what the colours are*; this owns *how a thing is drawn and what the pointer is
// told about it*. They are separate files because the palette has to be readable by code that never
// draws -- a test, a tool -- and because a panel that wants one of these almost never wants both.
//
// The rule this file exists to enforce: **an affordance is declared once**. Before it, the editor
// had exactly one `ImGui::SetMouseCursor` call in the whole of `src/` (a link in the Help panel),
// so every draggable block, every resizable edge and every splitter in the application presented
// the same arrow as a static label. Nothing told a person what could be done anywhere.

#include "ui/theme.hpp"
#include "ui/ui_logic.hpp"

#include <imgui.h>

#include <cstdint>
#include <string>

namespace avgen::ui {

// ---- cursors ------------------------------------------------------------------------------------
//
// What Dear ImGui and SDL3 can actually say, which is less than the brief asks for and is worth
// stating rather than papering over: **there is no open-hand or closed-hand cursor.**
// `ImGuiMouseCursor` has eleven shapes and neither is among them, and SDL3's system cursor list has
// no `OPENHAND`/`CLOSEDHAND` either -- they are a macOS `NSCursor` affordance that neither layer
// exposes. Reaching them would mean bypassing the ImGui backend, which reasserts the cursor from
// `GetMouseCursor()` on every frame.
//
// So "this block can be dragged" is said with the move cursor (SDL's `MOVE`, four arrows), which is
// the shape every editor uses for the same meaning and is honestly available, and the *pressed*
// state is communicated by the block itself rather than by a second cursor.
[[nodiscard]] ImGuiMouseCursor cursorForBlockZone(BlockZone zone);

// Sets the cursor for this frame. A convenience that exists so the call site reads as a statement
// about the thing under the pointer rather than as an ImGui call, and so every cursor in the
// application is greppable from one name.
void setHoverCursor(ImGuiMouseCursor cursor);

// ---- interaction states -------------------------------------------------------------------------
//
// One function for the whole application's answer to "what colour is this row/block/tab right now".
// The order is the one the states actually override each other in: being dragged beats being
// selected, which beats being hovered. Getting that order wrong is what makes a selected row appear
// to lose its selection as the pointer crosses it.
[[nodiscard]] ImU32 interactionFill(ImU32 base, bool hovered, bool selected, bool active);

// The outline that says "this one". Returns zero when nothing should be drawn, so a caller can skip
// the `AddRect` rather than draw a transparent one.
[[nodiscard]] ImU32 interactionOutline(bool hovered, bool selected, bool active);

// Mixes `over` onto `under` at `alpha`. Used for hover lifts, so that a lift is the same *amount*
// of lighter everywhere rather than a different literal per panel.
[[nodiscard]] ImU32 mixColour(ImU32 under, ImU32 over, float alpha);

// ---- the processing indicator (the brief's section 4) -------------------------------------------
//
// Drawn over the canvas while the world is catching up with the interface. Deliberately small,
// deliberately in a corner, and deliberately translucent: the scene underneath stays visible and
// stays interactive, because the application is working, not stopped.
//
// `fraction` is the honest one. Negative means "this work does not measure itself", and the
// indicator then shows a rotating arc and the stage's name and *no bar at all* -- ADR-064 already
// settled that inventing 72% is worse than admitting ignorance, and this obeys the same rule for
// the same reason. Zero or more draws a real bar.
//
// `seconds` drives the rotation. It is a wall clock, and it is confined to Dear ImGui's own draw
// list, which is recorded into the window's swapchain image after the world has been rendered into
// its own target. No pixel of a `--headless` capture, an offline render or `avgen_overlay_shot` can
// reach it: the first two build no ImGui at all, and the third rasterises `drawViewportOverlay`,
// which this is not part of.
void drawProcessingIndicator(ImDrawList* draw, ImVec2 topLeft, ImVec2 bottomRight, float opacity,
                             const char* stage, float fraction, double seconds);

// ---- context menus (the addendum's sections 8 and 11) -------------------------------------------
//
// One implementation, so that every menu in the application has the same padding, the same
// separators, the same disabled treatment and the same place to put a shortcut. The alternative --
// each panel calling `MenuItem` its own way -- is what produced four different row heights and two
// different ideas of where a shortcut goes in the panels that already had popups.
//
// Scoped so the style pushes cannot be forgotten: `ContextMenu` is a guard, and `open()` is false
// unless the popup is actually up.
class ContextMenu {
public:
    // `id` must be unique within the current ImGui id stack. Opens on a right-click over the last
    // submitted item.
    explicit ContextMenu(const char* id);
    // For a menu whose opening the caller decided itself -- the sequencer strip does, because a
    // right-*drag* there pans the view and must not end in a menu (see `updateContextClick`).
    ContextMenu(const char* id, bool openNow);
    ~ContextMenu();

    ContextMenu(const ContextMenu&) = delete;
    ContextMenu& operator=(const ContextMenu&) = delete;

    [[nodiscard]] bool open() const { return open_; }
    explicit operator bool() const { return open_; }

private:
    void begin(const char* id);
    bool open_ = false;
    bool styled_ = false;
};

// A row. `shortcut` may be null; when it is not, it is shown right-aligned and greyed, which is what
// makes a context menu a place people learn the keyboard from.
//
// **Never invent a shortcut here.** The string must name a binding that exists -- `Cmd+S` was
// advertised in this application's menus while unbound, and a menu that lies about the keyboard is
// worse than one that says nothing, because it trains a person to press something that does
// nothing. `shortcuts.hpp` holds the ones that are real.
bool menuAction(const char* label, const char* shortcut = nullptr, bool enabled = true);

// A row that shows and toggles a state (mute, solo, lock, visible).
bool menuToggle(const char* label, bool checked, const char* shortcut = nullptr, bool enabled = true);

// A heading naming what the menu is acting on -- "3 objects", "Clip: vocals.wav". One line, muted,
// followed by a separator. Professional menus lead with this because a context menu is the one
// surface where "which thing is this about?" is a real question.
void menuSubject(const std::string& text);

// ---- small shared widgets -----------------------------------------------------------------------

// NOTE on icon buttons: there is deliberately **no** `glyphButton` here. `transport_bar.cpp` has
// one in its anonymous namespace with a richer glyph set than a general version would have started
// with -- start, end, step-by-one each way, rewind, forward, loop -- and promoting it would have
// meant either losing glyphs or moving the transport's whole vocabulary into a shared header for
// one caller. A second, poorer implementation beside it is precisely what section 17 of the brief
// says not to do, so the transport keeps its own and it now takes its colours from `palette()`
// like everything else. Promote it here the day something outside the transport needs one.

// A tooltip that only appears after the pointer has settled, and that is styled like the rest of the
// application. ImGui's own delay is per-context and this is per-call, so a dense toolbar can be
// slower to speak than a sparse panel.
void hoverTip(const char* text, bool shortDelay = false);

} // namespace avgen::ui
