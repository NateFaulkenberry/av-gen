#include "app/ai_edit_sink.hpp"

#include "app/engine.hpp"
#include "core/log.hpp"

#include <fmt/ranges.h>

namespace avgen::app {

void EditHistoryTransactionSink::begin(const std::string& label) {
    // The snapshot goes first and stays underneath: it is what makes an abort possible, and this
    // sink does not replace that promise, only adds an undoable one.
    fallback_->begin(label);
    committedState_ = 0;
    capture_.begin(*engine_);
}

void EditHistoryTransactionSink::commit(const std::string& label) {
    fallback_->commit(label);
    committedState_ = 0;
    if (!capture_.open()) {
        return;
    }
    ui::EditCommand command = capture_.finish(*engine_, label);
    if (!capture_.unrecoverable().empty()) {
        // Committed, so it stands; but an undo of this task cannot bring these back.
        log::warn("ai: '{}' removed {} node(s) an undo cannot restore: {}", label,
                  capture_.unrecoverable().size(), fmt::join(capture_.unrecoverable(), ", "));
    }
    if (command.empty()) {
        return; // a task that changed nothing is not an edit, and must not leave an entry
    }
    log::info("ai: '{}' recorded as one undoable edit ({} thing(s))", label, command.touched());
    edits_->history().push(std::move(command));
    committedState_ = edits_->history().stateId();
}

void EditHistoryTransactionSink::abort() {
    // The snapshot puts the document back. Nothing is pushed: a task that was rolled back did not
    // happen, and an entry for it would offer to undo an edit the user never saw.
    fallback_->abort();
    capture_.cancel();
    committedState_ = 0;
}

} // namespace avgen::app
