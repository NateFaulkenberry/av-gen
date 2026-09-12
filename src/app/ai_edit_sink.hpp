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
// ## What it does not cover
//
// Only parameters. A task that adds or removes composition nodes is not reversed by this group; the
// snapshot store is still what makes such a task abortable, and it remains installed underneath.
// A future sink can record node changes too once the tools that make them report what they made.

#include "ai/transaction.hpp"
#include "app/edit_system.hpp"

#include <string>
#include <string_view>
#include <unordered_map>
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

private:
    Engine* engine_;
    EditSystem* edits_;
    // Still underneath: this sink makes a task *undoable*, the snapshot makes it *abortable*, and
    // those are different promises. Aborting a half-finished task has to put back node structure
    // that a parameter diff cannot describe.
    ai::TransactionSink* fallback_;
    std::unordered_map<std::string, std::vector<float>> before_;
    bool open_ = false;
};

} // namespace avgen::app
