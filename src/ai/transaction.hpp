#pragma once

// Transactions for AI edits (ADR-094, spec §26).
//
// ## Why this is not an undo system
//
// §26 says to integrate with the existing undo architecture rather than create a second one, and
// the editor pass is building one right now (world-authoring spec §28). Building a second here
// would be the mistake §26 names. So this file does two separate things and keeps them apart:
//
//   - **`SnapshotStore`** is the transaction *primitive*: a whole-document capture of the
//     parameter domain, taken before a task and restored if the task fails or is cancelled. It is
//     built on `params::saveProject` / `params::loadProject`, which already round-trip and are
//     already tested, so it inherits a serialisation format the project has confidence in rather
//     than inventing a diff format nobody has exercised.
//
//   - **`TransactionSink`** is the *seam*. It is one virtual with three methods. The default
//     implementation is snapshot-based. When the editor's undo stack lands, the application
//     installs a sink that opens a compound undo group instead, and the orchestrator, the tools
//     and the tests do not change. That is the whole reconciliation: one file to write, at merge.
//
// ## What a snapshot covers, exactly
//
// Parameter *base* values, modulation routes, presets, and the timeline. Nothing else.
//
// That is not a shortcut; it is the boundary of the tool surface this pass ships. Every mutating
// tool in `engine_tools.cpp` writes a parameter base value, a modulation route, or a timeline
// track -- so the transaction domain and the tool domain are the same set, and a rollback is
// therefore complete rather than approximately complete.
//
// It deliberately does **not** cover: the source rack, composition node structure (creating or
// deleting a node), loaded assets, the 2D layer stack, shader layers, scene states, world macros
// or the sequence. A tool that touched any of those would be outside the transaction and a
// rollback would leave its change behind -- so no such tool is registered. If one is added, it
// must extend the snapshot in the same commit, and `ToolAnnotations::undoable` is where that is
// declared: a mutating tool with `undoable == false` is a tool whose changes a rollback will not
// reach, and `Transaction` says so rather than pretending otherwise.
//
// ## Why whole documents rather than diffs
//
// Restoring a diff requires knowing what the inverse of every operation is. Restoring a document
// requires knowing how to load one, which this engine has done since milestone 0.9. A project's
// parameter block is a few hundred numbers; capturing it costs a JSON serialise, which measured
// well under a millisecond on the scenes in `examples/`. Correctness is worth that.

#include "ai/tool_context.hpp"
#include "core/error.hpp"

#include <nlohmann/json.hpp>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace avgen::app {
class Engine;
} // namespace avgen::app

namespace avgen::ai {

struct ProjectSnapshot {
    std::string id;      // "snap-1", stable for the session
    std::string label;   // why it was taken
    double createdAt = 0.0; // seconds since the store was constructed
    nlohmann::json document; // an avgen-project document restricted to the parameter domain

    [[nodiscard]] nlohmann::json describe() const; // id/label/time/counts, without the payload
};

// A bounded ring. Bounded because an agent that takes a snapshot before every tool call would
// otherwise grow a session's memory without limit, and the oldest snapshot in a long session is
// the least likely to be wanted.
class SnapshotStore {
public:
    static constexpr std::size_t kDefaultCapacity = 32;

    explicit SnapshotStore(std::size_t capacity = kDefaultCapacity) : capacity_(capacity) {}

    // Captures the parameter domain. Never fails: serialisation of live in-memory state has no
    // I/O and no parse.
    const ProjectSnapshot& capture(const app::Engine& engine, std::string label);

    // Restores one by id. Rebinds afterwards, so routes and timeline tracks point at live
    // parameters again. Fails only when the id is unknown or the document does not load, and
    // mutates nothing on failure (`params::loadProject` has the same guarantee).
    [[nodiscard]] Result<void> restore(app::Engine& engine, std::string_view id);

    [[nodiscard]] const ProjectSnapshot* find(std::string_view id) const;
    [[nodiscard]] const std::vector<ProjectSnapshot>& all() const { return snapshots_; }
    [[nodiscard]] std::size_t size() const { return snapshots_.size(); }
    void clear() { snapshots_.clear(); }

    // The document a snapshot holds, for tests and for the diff summary.
    [[nodiscard]] static nlohmann::json captureDocument(const app::Engine& engine);
    [[nodiscard]] static Result<void> applyDocument(app::Engine& engine, const nlohmann::json& doc);

    // Parameter paths whose base value differs between two captured documents, sorted. This is how
    // a transaction reports "73 underlying modifications" as something a person can read.
    [[nodiscard]] static std::vector<std::string> changedParameters(const nlohmann::json& before,
                                                                    const nlohmann::json& after);

private:
    std::vector<ProjectSnapshot> snapshots_;
    std::size_t capacity_;
    std::uint64_t nextId_ = 1;
};

// ---- the seam with the editor's undo ---------------------------------------------------------

// Implemented once here (snapshots) and once, later, over the editor's undo stack. The
// orchestrator only ever sees this interface.
class TransactionSink {
public:
    virtual ~TransactionSink() = default;
    // Opens a group. `label` is what a person will see in an undo menu.
    virtual void begin(const std::string& label) = 0;
    // Closes it as one logical action.
    virtual void commit(const std::string& label) = 0;
    // Discards the group and puts the project back.
    virtual void abort() = 0;
    // False when no rollback is possible; the orchestrator then refuses to start a mutating task
    // rather than starting one it cannot undo.
    [[nodiscard]] virtual bool available() const = 0;
    // What the user is told this transaction is backed by.
    [[nodiscard]] virtual std::string_view kind() const = 0;
};

// The default: one snapshot at begin, restored on abort, kept on commit.
class SnapshotTransactionSink final : public TransactionSink {
public:
    SnapshotTransactionSink(app::Engine& engine, SnapshotStore& store)
        : engine_(&engine), store_(&store) {}

    void begin(const std::string& label) override;
    void commit(const std::string& label) override;
    void abort() override;
    [[nodiscard]] bool available() const override { return engine_ != nullptr; }
    [[nodiscard]] std::string_view kind() const override { return "project snapshot"; }

    // The snapshot taken at begin(), for the summary. Empty outside a transaction.
    [[nodiscard]] const std::string& openSnapshotId() const { return openId_; }

private:
    app::Engine* engine_ = nullptr;
    SnapshotStore* store_ = nullptr;
    std::string openId_;
};

// RAII over a sink. Rolls back unless committed, including when a tool throws its way out.
class Transaction {
public:
    Transaction(TransactionSink& sink, std::string label);
    ~Transaction();
    Transaction(const Transaction&) = delete;
    Transaction& operator=(const Transaction&) = delete;

    void commit();
    void rollback();
    [[nodiscard]] bool open() const { return open_; }
    [[nodiscard]] const std::string& label() const { return label_; }

private:
    TransactionSink* sink_;
    std::string label_;
    bool open_ = true;
};

} // namespace avgen::ai
