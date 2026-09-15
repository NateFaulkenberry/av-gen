# ADR-231: The interface says what it can do, and what it is doing

**Status:** accepted (2026-09-15)
**Context:** the UI responsiveness and polish brief, and its right-click addendum
**Extends** ADR-076 (the editor shell), ADR-084 (the editor's frame), ADR-101 (editing is an
application concern), ADR-103 (the sequencer strip's lanes), ADR-064 (honest progress)

## Context

Two complaints, and they are not the same complaint.

> "I click something, wait for the engine, and eventually the UI changes."

> "This is a powerful engine with an ImGui editor."

The first is about latency. ADR-084 answered most of it already — the swapchain wait moved ahead of
input sampling, expensive procedural regeneration learned to wait for a drag to stop — and its
consequences section names what it left: `Composition::rebuild()` is all-or-nothing, `world::scatter`
is single-threaded, the viewport pick blocks the main thread per click, and *"geometry that is
deliberately a few frames behind the slider should say so; nothing shows it yet, and it should."*

The second is about the interface having no vocabulary. Measured rather than asserted: the whole of
`src/` contained **one** call to `ImGui::SetMouseCursor`, in the Help panel's link. Every draggable
block, every resizable edge and every splitter in the application presented the same arrow as a
static label. Every panel chose its own colour literals, so "selected" was a different colour in each
of the four lanes of a single strip and none of them moved when the theme did. There were three
right-click menus in the entire application.

## Decisions

### 1. Colour is a vocabulary, declared once

`ui::Palette` holds every colour the application is allowed to mean something with: four surfaces,
two borders, three text weights, four interaction states, six semantic meanings, and the playhead.
`applyTheme` derives every Dear ImGui colour from it rather than patching `StyleColorsDark`.

Two omissions are the decision as much as the roles are. **No per-track or per-kind hue ramp**: the
strip's lanes are told apart by position and by their headers, and a rainbow of lanes is decoration
pretending to be information. **No disabled background**: a disabled control is the normal control
with disabled text on it, because a control that changes shape when unavailable is harder to find
again when it is not.

`interactionFill` is the single answer to "what colour is this right now", and the order of its
arguments is load-bearing: active beats selected beats hovered, and a hover over something already
selected *lifts* the selection colour rather than replacing it. Getting that order wrong is what
makes a selected row appear to deselect itself as the pointer crosses it.

**Rejected:** letting each panel keep its own literals and only unifying the sequencer. The strip was
the worst case but not the only one, and a vocabulary with one fluent speaker is not a vocabulary.

### 2. An affordance is declared where the hit test already is

Which part of a block the pointer is over is answered once, by `blockZoneAt` in `ui_logic.hpp`, and
the hover treatment, the cursor and the drag all read that one answer. This is ADR-103's rule applied
to a second axis: the lane arithmetic was moved there because the drawing and the hit testing had
drifted apart and a click meant to scrub moved the audio instead.

The grips are strictly **inside** the block. A grip that reached past an edge would swallow the first
points of its neighbour, and in a lane packed edge to edge that is every click. They shrink on a
narrow block, to a third of its width, so the body never disappears behind its own handles, and
below eighteen points they are not offered at all — a three-pixel resize target is not a target, and
zooming in is the honest answer.

**What the platform cannot say, recorded so nobody looks for it again:** there is no open-hand or
closed-hand cursor. `ImGuiMouseCursor` has eleven shapes and neither is among them; SDL3's system
cursor list has no `OPENHAND`/`CLOSEDHAND` either. Reaching them would mean bypassing the ImGui
backend, which reasserts the cursor from `GetMouseCursor()` every frame. "This block can be dragged"
is therefore the move cursor, which is what every editor uses for the same meaning and is honestly
available.

### 3. A right press that travels is not a click

Dear ImGui opens a context menu on the **release** of the right button and measures nothing —
`IsPopupOpenRequestForItem` is `IsMouseReleased(button) && IsItemHovered(...)` and no more. On a list
row that is exactly right. Over the sequencer strip, where a right-drag pans the view, and over the
canvas, where it turns the camera, it is wrong: every pan and every look-around would end by opening
a menu.

So those two surfaces track the press themselves (`ui::updateContextClick`) and ask for a menu only
when the button came up near where it went down. `travelled` **latches**: once a press has moved it
cannot become a click again however far it comes back, because testing the distance only at the
release would call a there-and-back pan a click.

### 4. A context menu runs commands that already exist

ADR-101 says the menu, the keyboard and a context menu all arrive through `EditSystem::execute`, and
that is now true rather than aspirational. `ui::worldObjectMenuBody` is written once and called from
the hierarchy and from the viewport, because a menu that offers different things for the same object
depending on where it was clicked is one nobody can learn.

Right-clicking an object outside the selection makes it the selection first. What the menu is about
is captured **at the click**, never hit-tested from the live pointer: a popup is submitted on later
frames, by which time the pointer has moved, and a menu that resolved its subject at choice-time
would act on whatever happened to be underneath.

**Two absences are the decision.** There is no **rename** anywhere in this application — not on
`WorldEditor`, not in `world_edit.hpp`, not as an `EditCommand` record, and `composition.cpp`
explicitly refuses to rename a hero. Offering it would mean a new mutation path and a new undo record
behind a context menu, which is the one thing the addendum says not to build. And no **reset
position**, whose identity is the world origin and which is therefore a trap; rotation and scale have
obvious identities and are offered.

The sequencer's menus do **not** participate in undo, because the sequencer has no undo — `seq::Sequence`
is a value the panel edits and re-bakes. That is stated rather than hidden. The mitigation is that
the toolbar and the menu now share one function per operation, so the two cannot drift.

### 5. The canvas says when the world is behind the interface, and only then

`Composition::proceduralsAwaitingRebuild()` has existed since ADR-084 with no caller. It has one now,
alongside job progress and song analysis.

The threshold is the decision. Nothing at all under 180 ms, then a fade over 140. Procedural
regeneration defers for 90 ms *by design*, and an indicator that fired on every one of those would
blink through every frame of every slider drag — which does not communicate "working", it teaches
the eye to ignore the one place the application has to speak. A bar appears only when the work
measures itself; ADR-064 settled that inventing a percentage is worse than admitting ignorance, and
`JobStatus::progressKnown` is the flag that carries the admission.

It is drawn into the canvas *window's* draw list and deliberately **not** inside
`ui::drawViewportOverlay`. That overlay is what `avgen_overlay_shot` rasterises and compares byte for
byte, and a thing that animates off a wall clock belongs nowhere near it. No pixel of a `--headless`
capture or an offline render can reach it: neither builds an ImGui context at all.

### 6. Opening a project is deferred by one frame, not moved to a thread

The load is not threaded. The composition is not thread-safe, the environment map's prefilter is GPU
work and all GPU work here is the main thread's, and the parameter set is torn down and rebuilt
underneath everything that reads it. ADR-084 rejected threading a far smaller piece of this for the
same reasons, and the brief's section 19 rules out moving unsafe work off the main thread to improve
a number.

What is safe is to say so first: the request is remembered, the canvas paints "Opening <name>" over
the scene that is still there, that frame is presented, and the work runs at the top of the next one.

**The freeze is the same length.** That is stated plainly because the alternative — implying that a
spinner made it faster — is exactly what section 19 forbids. What made it shorter was §7.

## What was measured

| | before | after |
|---|---|---|
| `glowmere-stylized.json` open, total | 2147 ms | **1442 ms** |
| ...of which "Building the scene" | 1964 ms | **1259 ms** |
| viewport pick, blocking GPU round trips per click | 2 | **1** |
| sequencer strip visible at 1440x900, default layout | 82 of 195 points | **134 of 135** |
| toolbar above the strip | 190 points | **138 points** |
| `SetMouseCursor` call sites in `src/` | 1 | 14 |
| `ui.build`, empty scene idle (the reference in docs/application-performance.md §5) | 0.044 ms | 0.047 ms min |

### 7. `world::scatter` is threaded, exactly rather than approximately

Item P1-3 of `docs/application-performance.md` §14, recorded and unfixed, while `world::buildTerrain`
directly beside it has had a thread pool since ADR-090. `sample(1)` during start-up, 1648 main-thread
samples: 771 of them inside `world::scatter` — **47% of a 2.14-second project load, on one core.**

It parallelises *exactly*, and that is why it is safe to do at all. Three properties, checked rather
than assumed, and breaking any one breaks determinism:

1. every random number is `random01(layer.seed, cellId, channel)`, a pure hash of the cell's own
   index — no sequential generator whose state depends on how many cells came before;
2. everything read is const and free of mutable caches (`WorldMap::sample`/`height`, `BiomeSet::at`,
   `waterTable`, `HabitatIndex::nearest`, the noise functions);
3. bands are contiguous row ranges concatenated in ascending order, so the emitted sequence is the
   *same sequence* and `renumberIndices` and every downstream consumer see no change.

Verified by capture rather than by argument: `--headless --frames 6 --capture` threaded and serial
are byte-identical, sha256 `3f4dbe24…aefaaa`, 3,888,016 bytes, while the timings differ by a third.
`AVGEN_SCATTER_WORKERS=<n>` forces the count so that comparison stays checkable later rather than
being a claim about two builds that no longer both exist — the same reason `AVGEN_NO_TERRAIN_CACHE`
exists.

## Consequences

- The sequencer was unusable at the size the editor opens at, and that was found by an instrument
  rather than by looking: `--ui-script strip` reported a scrub to `0.00 s` and then said why.
- A scrub no longer re-bakes the sequence. Releasing one called `touch()`, and a scrub changes
  nothing the bake reads.
- `avgen_overlay_shot` renders in the application's palette, so a shot can be used to judge contrast.
  The first one taken through it found a column of checkboxes five levels of grey from its
  background, which is why `Palette::control` exists.
- **Not built:** dragging clips in the audio lane (ADR-103's reason still stands), rename anywhere,
  a live stage readout during a load, and per-clip context actions in the world that need a pick.

## Revisit triggers

- If a second caller outside the transport needs an icon button, promote `glyphButton` into
  `ui/style.hpp`; it is deliberately not there for one client.
- If rename becomes an `EditCommand`, the context menus have a place ready for it.
- If the load ever becomes safe to thread — which means the composition becoming thread-safe first —
  §6 is the decision to revisit, and the deferral it added becomes unnecessary rather than wrong.
