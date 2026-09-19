# ADR-361: Two lists of the same thing in one window, and a warning nothing but a person could see

Status: accepted
Date: 2026-09-19
Branch: `agent/mbackend`
Relates to: ADR-007 (Dear ImGui), the help-system spec (§3, §28), ADR-182 (a probe that cannot
fail proves nothing), ADR-350 (a rule stated in a comment is not a rule a test holds up)

*Numbered 361 because 360 is the highest in `docs/decisions/README.md` at `8f23d2ec`. If another
branch takes it first, this file is the one that moves.*

## What the owner saw

```
Programmer error: 2 visible items with conflicting ID!
Code should use PushID()/PopID() in loops, or append "##xx" to same-label identifiers!
Empty label e.g. Button("") == same ID as parent widget/node. Use Button("##xx") instead!
```

## The first thing to get right: the third line is not a clue

It reads like a description of the offending widget and it is not. All three lines are static
bullets that `ImGui::ErrorCheckEndFrameFinalizeErrorTooltip` prints every time
(`imgui.cpp:12031-12042`, Dear ImGui 1.92.9b as pinned). The conflicting items can have any label.

For the record, and it is worth recording because it is where a search naturally starts: there are
**zero** `ImGui::Xxx("")` call sites anywhere in `src/`, and every repeated-row loop in `src/ui/`
already pushes an id. A census of `##`-literals that appear more than once found sixteen, and all
sixteen are separated either by a `PushID` or by mutually exclusive branches. The bug was not in
that family at all.

The second thing worth knowing is that the two items **do not have to overlap on screen**. The
counter is incremented in `ItemAdd` before the window and rect tests (`imgui.cpp:5140-5147`); all
that is required is the same final id in the same window in the same frame, with the pointer over
one of them.

## The cause

`HelpPanel::drawSidebar` drew two lists inside one `BeginChild("help-sidebar")`: "Recently viewed",
and the full category tree. A topic that has been opened is in **both** — `recent_` is the last
eight visited ids and nothing removes those documents from `db_.categories()`.

Both lists identified a row the same way:

```cpp
ImGui::PushID(doc->id.c_str());
ImGui::Selectable(doc->title.c_str(), doc->id == current_);
```

`ImGui::CollapsingHeader` does **not** push the id stack — `ImGuiTreeNodeFlags_CollapsingHeader`
carries `NoTreePushOnOpen` — and `ImGui::Indent` has no id effect whatsoever. So the two lists sat
at the identical id-stack depth and the two rows hashed to one id.

Reproducing it needs nothing unusual: open any topic in "Getting Started" (which is
`DefaultOpen`), expand "Recently viewed", hover either row. **Two** visible items, which is the
number in the message.

## The decision

A sidebar row's identity is **(which list, which document)**, not (which document). Each group
carries the scope the panel pushes before drawing it, and the scope is pushed around the header as
well as the rows, because a header is an item with an id too.

The grouping is now data — `ui::helpSidebarGroups(db, recent)` — in **`src/ui/help_sidebar.cpp`,
which is ImGui-free and is on the test target's source list**, for the reason `output_preview.cpp`
and `world_edit.cpp` are: a rule that can only be checked by looking at the screen is a rule nobody
checks, and this one went unchecked until a person hovered the right row. `help_panel.cpp` draws
the groups and decides nothing about them, so the test and the product cannot describe different
sidebars.

Category scopes are prefixed `cat/`, so a category one day named "recent" cannot collide with the
list above it.

Two incidental improvements, named rather than smuggled: a recently-viewed row now gets the status
colour and the summary tooltip that a category row always had, because there is one code path now
instead of two.

## The tests, and the control that fails

Per ADR-182, the arm is worthless without a control that could have come out the other way, so the
test computes row identities **both** ways — with the scope and with the document alone — and
asserts the second one **collides**:

* on a synthetic database with two recently-viewed topics: `duplicateCount(without scope) == 2`,
  `duplicateCount(with scope) == 0`;
* on the **shipped** `docs/help` content with eight recently-viewed topics:
  `duplicateCount(without scope) == 8`, `duplicateCount(with scope) == 0`, and no two groups share
  a scope.

Both controls pass, which is the measurement that the defect was real and is what the runtime
warning was counting. A test that only asserted the fixed form would pass just as happily against a
database in which nothing repeats.

## What this does not fix

A second candidate was found and is **not** this bug, but is the same shape and should be watched:
`world_edit_panel.cpp:659` keys an object row on `PushID(node.name.c_str())`, and two sibling nodes
with the same name would collide five widgets over. `Composition::uniqueName` normally prevents it;
hand-edited or imported scene JSON bypasses that. The same pattern is at `control_panel.cpp:2584`,
`:1700`, `:3524`, `graph_editor.cpp:153`, `world_builder_panel.cpp:188` and `:221`.

`io.ConfigDebugHighlightIdConflicts` was not touched. Nothing in this repository ever enabled it —
it is Dear ImGui's own default — and it stays on.
