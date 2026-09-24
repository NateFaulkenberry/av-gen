#pragma once

// The seam `ai/transaction.hpp` was written to wait for (ADR-094 §26, ADR-101).
//
// The AI control plane wraps a mutating task in a transaction so a failed or cancelled task can be
// rolled back. Its default implementation takes a whole-document snapshot, which is correct and is
// invisible to the user: a task that succeeds leaves nothing in the undo history, so "the assistant
// moved my camera" cannot be taken back with Cmd+Z like every other edit.
//
// That was deliberate at the time. `transaction.hpp` says so:
//
//     "TransactionSink is the seam. ... When the editor's undo stack lands, the application installs
//      a sink that opens a compound undo group instead, and the orchestrator, the tools and the
//      tests do not change. That is the whole reconciliation: one file to write, at merge."
//
// This is that file. The reconciliation is honoured rather than replaced -- §49's warning about
// parallel history managers is about exactly this pair, and the seam existed so there would be one.
//
// ## How one task becomes one command
//
// The tools write parameters through the engine, in numbers nobody counts. So the group is measured
// rather than recorded: base values are read at `begin`, read again at `commit`, and what differs
// becomes one `EditCommand`. That is the same shape the editor's own drag coalescing uses -- capture
// at the start, compare at the end, push once -- for the same reason, and it needs no cooperation
// from the tools.
//
// The cost is two passes over the parameter set per task. A large project carries about twenty
// thousand parameters, so this is well under a millisecond at a boundary that already involves a
// language model. It is emphatically not something to do per frame, and it is not asked to be.
//
// ## What it covers (ADR-752)
//
// Every domain `app::EditCapture` measures: parameter bases, the sequence, the camera collection and
// camera track, the author timeline and routes, added nodes and parent changes. So a task that adds a
// shot, a marker, a keyframe and a camera is **one** Cmd+Z, undone by the same records a person's
// edits are. Before ADR-752 it recorded parameters only, and a task's sequence, keyframe, route and
// node edits never reached the undo stack at all.
//
// The snapshot stays underneath, as the transaction's ABORT: a task that fails or is cancelled is
// rolled back to the document it started from, which is a different promise from undo. The one
// thing the capture cannot restore -- a node a tool destroyed -- is still covered by that abort, and
// a committed task that destroyed nodes says so in the log.

#include "ai/transaction.hpp"
#include "app/edit_capture.hpp"
#include "app/edit_system.hpp"

#include <string>
#include <string_view>
#include <cstdint>
#include <vector>

namespace avgen::app {

class Engine;

class EditHistoryTransactionSink final : public ai::TransactionSink {
public:
    EditHistoryTransactionSink(Engine& engine, EditSystem& edits, ai::TransactionSink& fallback)
        : engine_(&engine), edits_(&edits), fallback_(&fallback) {}

    void begin(const std::string& label) override;
    void commit(const std::string& label) override;
    void abort() override;
    // The fallback is what actually guarantees a rollback, so availability is its answer.
    [[nodiscard]] bool available() const override { return fallback_->available(); }
    [[nodiscard]] std::string_view kind() const override { return "undo history"; }
    // The history state the last commit produced, or 0 when it pushed nothing.
    [[nodiscard]] std::uint64_t committedEditState() const override { return committedState_; }

private:
    Engine* engine_;
    EditSystem* edits_;
    // Still underneath: this sink makes a task *undoable*, the snapshot makes it *abortable*, and
    // those are different promises. Aborting a half-finished task has to put back node structure
    // that a parameter diff cannot describe.
    ai::TransactionSink* fallback_;
    EditCapture capture_;
    std::uint64_t committedState_ = 0;
};

} // namespace avgen::app
