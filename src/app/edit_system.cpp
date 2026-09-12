#include "app/edit_system.hpp"

#include "app/engine.hpp"
#include "core/log.hpp"

#include <algorithm>

namespace avgen::app {

const std::vector<EditAction>& editActions() {
    static const std::vector<EditAction> all = {
        EditAction::Undo,      EditAction::Redo,   EditAction::Cut,
        EditAction::Copy,      EditAction::Paste,  EditAction::Duplicate,
        EditAction::Delete,    EditAction::SelectAll, EditAction::SelectNone,
    };
    return all;
}

const char* editActionName(EditAction action) {
    switch (action) {
    case EditAction::Undo:       return "Undo";
    case EditAction::Redo:       return "Redo";
    case EditAction::Cut:        return "Cut";
    case EditAction::Copy:       return "Copy";
    case EditAction::Paste:      return "Paste";
    case EditAction::Duplicate:  return "Duplicate";
    case EditAction::Delete:     return "Delete";
    case EditAction::SelectAll:  return "Select All";
    case EditAction::SelectNone: return "Select None";
    }
    return "Edit";
}

const char* editActionShortcut(EditAction action) {
    // Written the way Dear ImGui renders a menu shortcut, and the only place these strings live:
    // the dispatcher answers to the same set, so the menu cannot advertise a key nothing handles.
    switch (action) {
    case EditAction::Undo:       return "Cmd+Z";
    case EditAction::Redo:       return "Shift+Cmd+Z";
    case EditAction::Cut:        return "Cmd+X";
    case EditAction::Copy:       return "Cmd+C";
    case EditAction::Paste:      return "Cmd+V";
    case EditAction::Duplicate:  return "Cmd+D";
    case EditAction::Delete:     return "Del";
    case EditAction::SelectAll:  return "Cmd+A";
    case EditAction::SelectNone: return "Shift+Cmd+A";
    }
    return "";
}

// ---- clipboard ----------------------------------------------------------------------------------

void Clipboard::set(ClipboardPayload payload) { payload_ = std::move(payload); }

void Clipboard::clear() { payload_ = ClipboardPayload{}; }

bool Clipboard::holds(std::string_view type) const {
    return !payload_.empty() && payload_.type == type;
}

// ---- the system ---------------------------------------------------------------------------------

void EditSystem::addContext(EditContext& context) {
    if (std::find(contexts_.begin(), contexts_.end(), &context) == contexts_.end()) {
        contexts_.push_back(&context);
    }
}

void EditSystem::removeContext(const EditContext& context) {
    std::erase(contexts_, &context);
    if (focus_ == &context) {
        focus_ = nullptr;
    }
}

void EditSystem::setFocus(EditContext* context) { focus_ = context; }

EditContext* EditSystem::contextFor(EditAction action) const {
    // The focused editor first, because the user's attention is the best available statement of
    // which document an ambiguous action means. Then registration order, so an action still lands
    // when nothing is focused -- a menu click does not move the focus, and an action that did
    // nothing because a panel was not clicked first would read as a bug.
    if (focus_ != nullptr && focus_->canEdit(action)) {
        return focus_;
    }
    for (EditContext* context : contexts_) {
        if (context != nullptr && context->canEdit(action)) {
            return context;
        }
    }
    return nullptr;
}

void EditSystem::report(const ui::EditApply& applied, const char* what) {
    // An undo that could only restore four of five nodes has to say so -- and one that restored
    // nothing at all especially. This was being discarded, so an undo against a session with no
    // composition reported success and changed nothing, which is the worst available outcome: the
    // user believes the edit is back.
    if (applied.ok()) {
        return;
    }
    for (const std::string& problem : applied.problems) {
        log::warn("{}: {}", what, problem);
    }
}

void EditSystem::announceSelection(const std::vector<std::string>& names) {
    // Every context, not just the focused one: the command may have come from an editor the user
    // has since left, and an undo must put back what that edit had selected. A context that does
    // not recognise the names ignores them.
    for (EditContext* context : contexts_) {
        if (context != nullptr) {
            context->editSelectionRestored(names);
        }
    }
}

bool EditSystem::canExecute(EditAction action) const {
    switch (action) {
    case EditAction::Undo:
        return history_.canUndo();
    case EditAction::Redo:
        return history_.canRedo();
    default:
        break;
    }
    return contextFor(action) != nullptr;
}

bool EditSystem::execute(EditAction action, Engine& engine) {
    // Undo and redo are the system's own: they act on the history rather than on a document, and no
    // editor is entitled to a private answer about them. This is what makes history global -- an
    // undo takes back the last edit whoever made it, which is the one thing a user is certain of.
    if (action == EditAction::Undo) {
        if (!history_.canUndo()) {
            return false;
        }
        std::vector<std::string> selection;
        report(history_.undo(engine, &selection), "undo");
        announceSelection(selection);
        return true;
    }
    if (action == EditAction::Redo) {
        if (!history_.canRedo()) {
            return false;
        }
        std::vector<std::string> selection;
        report(history_.redo(engine, &selection), "redo");
        announceSelection(selection);
        return true;
    }

    EditContext* context = contextFor(action);
    if (context == nullptr) {
        return false;
    }
    return context->doEdit(action, engine, *this);
}

std::string EditSystem::menuLabel(EditAction action) const {
    const std::string name = editActionName(action);
    // Undo and redo name the command they would act on, so the menu says what will happen rather
    // than inviting the user to find out. Everything else is already its own description.
    if (action == EditAction::Undo && history_.canUndo()) {
        return name + " " + history_.undoLabel();
    }
    if (action == EditAction::Redo && history_.canRedo()) {
        return name + " " + history_.redoLabel();
    }
    return name;
}

void EditSystem::markSaved() { savedState_ = history_.stateId(); }

bool EditSystem::dirty() const { return history_.stateId() != savedState_; }

void EditSystem::clearHistory() {
    history_.clear();
    // Clean, because a project that has just been loaded or created is exactly as it was stored.
    savedState_ = history_.stateId();
}

} // namespace avgen::app
