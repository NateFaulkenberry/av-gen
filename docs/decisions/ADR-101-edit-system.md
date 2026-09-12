# ADR-101: Editing is an application concern, and the history belongs to the application

**Status:** accepted
**Date:** 2026-09-11
**Context:** The global edit system brief; supersedes the ownership half of ADR-092's undo design

## The problem

`ui::EditHistory` was a public member of `ui::WorldEditor`. That was right while the world editor was
the only thing that edited anything, and it stops being right the moment there is a second editor.

The user does not know or care which panel performed an operation. They know that Cmd+Z takes back
the last thing they did. **A history per editor cannot answer that**, because neither editor knows
which of them acted most recently. Two stacks can each say what *they* last did; nothing can say
what *happened* last.

The same gap showed up three more times:

- The keyboard handler was a switch naming one editor — `case SDLK_Z: editor.undo()`.
- There was no Edit menu, so there was nothing that had to ask "is this available?" in general.
- The clipboard was a `std::vector<std::string>` of node names inside the editor, so copy, delete,
  paste pasted nothing.

## What was already right, and is kept

The brief sketched a virtual `EditCommand` with `execute`/`undo`/`redo`. **The existing model is
better for this engine and is kept**, because it already had the properties that matter:

- A command is a **record of the edit**, not a snapshot. A Glowmere scene is 256 terrain chunks, a
  quarter of a million scattered instances and several thousand parameters; copying it to make one
  flower undoable costs more than placing the flower.
- Multi-object edits are one entry, because a command holds *lists* of parameter, parent and node
  changes rather than one of each.
- A drag opens a command, writes through it and closes once on release, so an interaction is one
  entry rather than sixty.
- A departing node is **kept, not described** — the command owns the `CompositionNode` while it is
  out of the scene, because serialising it would lose a loaded glTF, a nested child composition or a
  terrain's built chunks, and lose it exactly where the user is most certain nothing was lost.

So this ADR changes **ownership and reach**, not the command model.

## The decision

The application owns one `EditSystem`: the history, the clipboard, and the dispatch. Editors own
their domain and answer two questions — what they *can* do, and do it.

```
    menu ─┐
keyboard ─┼─► EditSystem::execute ─► the focused EditContext ─► records into the one history
   panel ─┘
```

Three consequences follow, and each is a rule.

**Undo and redo are the system's and cannot be claimed by a context.** They act on the history rather
than on a document. An editor answering privately is exactly the isolated stack being replaced.

**Focus is a preference, not a veto.** The focused editor is asked first, because the user's
attention is the best available statement of which document an ambiguous action means — but an
action it cannot perform falls through to the others. Clicking the timeline and pressing Delete
should still delete what is selected in the world.

**Availability is asked, never assumed.** `canExecute` answers the menu and gates the keyboard, so
an item is grey exactly when the shortcut would do nothing. There is no second opinion for them to
disagree over.

## A state has an identity, not a depth

Save state cannot be a stack height. Undo twice, make a different edit, and the stack stands at the
same height holding an entirely different document; a marker compared against a count calls that
saved. So each command carries the serial of the state it produced, and the document is named by the
newest one.

Two cases fall out of that and are tested:

- The **empty-stack state has its own identity, and it moves** when the oldest command is trimmed
  away. Once a command has fallen off the bottom, "undo everything" lands on the document as it
  stood after that command, not the one the session opened with.
- **Clearing takes a fresh identity** rather than returning to zero, because a cleared history
  describes a different document — a project loaded, a scene swapped.

## The clipboard holds things, not a format

A payload carries what it *is* (`world/nodes`), a version, and the objects themselves. Reading it
back goes through the declared type, so a caller cannot reinterpret a timeline's payload as a tree
by asking confidently.

It holds **clones**, for the same reason a command owns a departing node: a serialisation loses what
the scene file does not write. A clipboard is a place a thing waits, not a format it is written in.
`version` still travels, because the day this goes on the system pasteboard it will need one and
that is a bad day to start.

Copy does not enter the history — it changes nothing. Cut does, as **one** command: undoing a cut
must bring the objects back in one press rather than leaving the user to discover that the first
Cmd+Z only took back a copy.

## Where it lives, and why not somewhere better

`src/app/`, the executable.

The library boundary decides this. `ai/` is compiled into `avgen_core`; `ui/` into
`avgen_platform`; `app::Engine` into the executable. Moving the history down into core would force a
core-to-Engine dependency, which is worse than the problem. The executable is the only layer that
can see the history, the AI control plane, the engine and the UI at once.

This is also why `ai::TransactionSink` is a virtual: core cannot reach the history, so the
application installs the implementation. That seam was designed for this and is honoured rather than
replaced.

## Consequences

**Good.** One place to ask for an edit, so a menu item, a shortcut, a panel button and a scripted
action cannot mean different things. A new editor participates by implementing two methods and
recording commands; it does not touch the menu, the keyboard, or any existing call site.

**Bad.** An editor can now record a command that another editor's undo will take back, and nothing
stops it recording one it cannot reverse. The history trusts its contributors, and a context that
pushes a command whose `applyEdit` cannot reverse cleanly will fail at undo time rather than at push
time.

**Watch for.** `WorldEditor::reset()` clears the **whole** history on a scene swap. That is right
today, with one editor whose commands all describe the scene being replaced. The moment a second
editor participates it is wrong — swapping a scene would discard a timeline edit that had nothing to
do with it. Per-context invalidation is needed before that lands, and is deliberately not built now:
there is no second editor to design it against, and a guess would be the thing to unpick later.
