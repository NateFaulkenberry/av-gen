#pragma once

// The AI panel's decisions, separated from its drawing (ADR-752).
//
// "Undo this task" used to restore a whole-document snapshot, outside the editor's history. That
// was a second undo mechanism -- and in the application it was also unreachable, because the panel
// showed the button only when the task carried a snapshot id, and the orchestrator found that id by
// casting the sink to `SnapshotTransactionSink` while the application installs the history sink.
//
// A task is now one command on the editor's history, and undoing it is undoing that command. The
// only question the panel has to ask is whether that command is still the one an undo would take,
// and that has an exact answer: the history's state id is the task's own id exactly when nothing has
// been done or undone since. Asked here so a test can ask it without a window.

#include <cstdint>

namespace avgen::ui {

enum class TaskUndo : std::uint8_t {
    None,       // the task recorded no edit (read-only, rolled back, or no history sink)
    Available,  // the task's command is the newest on the history: an undo takes exactly it
    Superseded, // edits (or undos) happened since; the history list is where it can be reached
};

// `taskEditState` is `ai::TaskOutcome::editState`; the other two are the history's `stateId()` and
// `canUndo()` now.
[[nodiscard]] constexpr TaskUndo taskUndoState(std::uint64_t taskEditState, std::uint64_t historyState,
                                               bool historyCanUndo) {
    if (taskEditState == 0) {
        return TaskUndo::None;
    }
    return historyCanUndo && historyState == taskEditState ? TaskUndo::Available : TaskUndo::Superseded;
}

} // namespace avgen::ui
