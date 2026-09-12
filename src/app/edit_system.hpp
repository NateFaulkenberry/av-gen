#pragma once

// The application's editing model: one history, one clipboard, one way to ask for an edit.
//
// ## Why this exists
//
// Undo belonged to the world editor. That was right while the world editor was the only thing that
// edited anything, and it stops being right the moment a second editor appears: the user does not
// know or care which panel performed an operation, they know that Cmd+Z takes back the last thing
// they did. A history per editor cannot answer that, because neither editor knows which of them
// acted most recently.
//
// The alternative this deliberately avoids is a manager per editor with the menu choosing between
// them -- `if (world) world.undo() else if (timeline) timeline.undo()`. That reads fine with two
// editors and collapses at four, and every new editor has to be added to every call site that ever
// asks for an edit.
//
// So: the system owns the history; editors own their domain. An editor says what it *can* do
// (`canEdit`) and does it (`doEdit`), and records what it did in the one history. The menu, the
// keyboard, a context menu and any future command palette all arrive through `execute`, so they
// cannot drift apart -- that is the whole of ADR-101.
//
// ## What it does not own
//
// Undo and redo are the system's, because they act on the history rather than on a document, and
// no editor is entitled to a private answer about them. Everything else is contextual and belongs
// to whichever editor has the focus.
//
// ## Threading
//
// Editing happens on the UI thread, against the engine's parameter and composition state, exactly
// as the world editor already did. Nothing here touches GPU or audio state; commands write
// parameters and composition nodes, and the renderer picks the result up on its next frame the way
// it does for any other edit.

#include "ui/edit_history.hpp"

#include <nlohmann/json_fwd.hpp>

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace avgen::app {

class Engine;
class EditSystem;

// What the application can be asked to do. Undo and Redo are answered by the system; the rest are
// asked of whichever editor has the focus.
enum class EditAction : std::uint8_t {
    Undo,
    Redo,
    Cut,
    Copy,
    Paste,
    Duplicate,
    Delete,
    SelectAll,
    SelectNone,
};

// Every action, for menus and for tests that must not miss one when a new action is added.
[[nodiscard]] const std::vector<EditAction>& editActions();
// "Undo", "Select All" -- the menu's wording, and the stem of a history label.
[[nodiscard]] const char* editActionName(EditAction action);
// The shortcut as a person reads it: "Cmd+Z". Shown in the menu, and the one place that spelling
// lives, so the menu cannot advertise a key the dispatcher does not answer to.
[[nodiscard]] const char* editActionShortcut(EditAction action);

// ---- the clipboard ------------------------------------------------------------------------------
//
// Structured, not text. A world node is a tree with materials, transforms and asset references; the
// operating system's text clipboard can carry a serialisation of one but cannot say what it is, and
// a paste that cannot tell a node from a timeline marker has to guess.
//
// `type` is what the payload holds and is what a context checks before offering Paste. `version`
// travels with it because a payload may outlive the build that wrote it once this is put on the
// system pasteboard, which is a thing to get right at the start rather than after the first
// mismatch.
struct ClipboardPayload {
    std::string type;    // "world/nodes"
    int version = 1;
    std::string source;  // the editor that produced it, for the UI
    std::shared_ptr<const nlohmann::json> data;

    [[nodiscard]] bool empty() const { return type.empty() || data == nullptr; }
};

class Clipboard {
public:
    void set(ClipboardPayload payload);
    void clear();
    [[nodiscard]] bool empty() const { return payload_.empty(); }
    // True when the clipboard holds this kind of thing. A context asks before offering Paste, so
    // the menu never advertises a paste that would arrive as the wrong shape.
    [[nodiscard]] bool holds(std::string_view type) const;
    [[nodiscard]] const ClipboardPayload& payload() const { return payload_; }

private:
    ClipboardPayload payload_;
};

// ---- what an editor implements ------------------------------------------------------------------
//
// Deliberately small. An editor answers what it can do and does it; it does not own a history, a
// clipboard, or any part of how an action reached it.
class EditContext {
public:
    virtual ~EditContext() = default;

    // For the history's source column and for diagnostics: "World", "Timeline".
    [[nodiscard]] virtual std::string_view editContextName() const = 0;
    // Whether this action would do something *right now*. The menu asks this to decide whether to
    // grey an item, so an answer of true is a promise that `doEdit` will act.
    [[nodiscard]] virtual bool canEdit(EditAction action) const = 0;
    // Performs it. False means the editor declined after all, and the system moves on to the next
    // context rather than reporting an edit that did not happen.
    virtual bool doEdit(EditAction action, Engine& engine, EditSystem& edits) = 0;

    // After an undo or redo, the selection the command recorded for the side just restored.
    //
    // Not pure: an editor with no selection of its own ignores it. Undoing a delete that gave you
    // back five objects and left nothing selected is the case this exists for -- the objects are
    // there and the user has no idea which ones came back.
    virtual void editSelectionRestored(const std::vector<std::string>& /*names*/) {}
};

// ---- the system ---------------------------------------------------------------------------------

class EditSystem {
public:
    [[nodiscard]] ui::EditHistory& history() { return history_; }
    [[nodiscard]] const ui::EditHistory& history() const { return history_; }
    [[nodiscard]] Clipboard& clipboard() { return clipboard_; }
    [[nodiscard]] const Clipboard& clipboard() const { return clipboard_; }

    // Editors register once. Registration order is the fallback order, used when nothing has the
    // focus or the focused editor declines.
    void addContext(EditContext& context);
    void removeContext(const EditContext& context);
    // Which editor the user is working in. Null means "no particular one", and the system falls back
    // to registration order.
    void setFocus(EditContext* context);
    [[nodiscard]] EditContext* focus() const { return focus_; }

    // Would this do something? The menu greys an item on a false, and the keyboard ignores it, so
    // the two cannot disagree about what is available.
    [[nodiscard]] bool canExecute(EditAction action) const;
    // Does it. False when nothing could.
    bool execute(EditAction action, Engine& engine);

    // What the menu should say: "Undo Move 3 objects", or plain "Undo" with nothing to undo. The
    // description comes from the command, so the menu says what will actually happen.
    [[nodiscard]] std::string menuLabel(EditAction action) const;

    // ---- save state ----------------------------------------------------------------------------
    // The project is clean when the document is in the state it was saved in. Compared by the
    // history's state identity rather than its depth, so a re-edit after an undo is dirty even
    // though the count agrees (see EditHistory::stateId).
    void markSaved();
    [[nodiscard]] bool dirty() const;
    // A project boundary: the old history describes a document that is gone, and undoing into it
    // would edit the new one. Leaves the project clean, because a freshly loaded one is.
    void clearHistory();

private:
    [[nodiscard]] EditContext* contextFor(EditAction action) const;
    void announceSelection(const std::vector<std::string>& names);

    ui::EditHistory history_;
    Clipboard clipboard_;
    std::vector<EditContext*> contexts_;
    EditContext* focus_ = nullptr;
    std::uint64_t savedState_ = 0;
};

} // namespace avgen::app
