#pragma once

// The compensation log: what a transaction does about the FILES its calls wrote (ADR-2153).
//
// A transaction's snapshot puts the project document back. It cannot un-copy a file. Before this, one tool
// (`asset.import`) deleted its own partial copies when it failed and nothing else did anything, so a
// rolled-back task that had imported a model left the model in the project folder, and a task that failed
// half-way through a glTF's dependencies left whichever ones had been copied.
//
// Now a tool reports each file it writes (`ToolContext::noteCreatedFile` and friends) and the session keeps
// the record. A rollback deletes what the transaction created, restores what it replaced, and reports what
// it could not, under four rules:
//
//   1. **A file that existed before the transaction is never deleted.** A tool reports `Created` only for a
//      path that did not exist when it wrote it; a path that existed is `Replaced` (its bytes kept, put
//      back) or `Overwritten` (reported as not restored).
//   2. **A file the project or scene names, as restored, is never deleted.** "Outside the transaction"
//      means: after the snapshot is put back, the restored document, the engine's audio and scene paths,
//      or the project and scene files on disk still reference it. A save made inside the transaction
//      cannot be un-written, so a file that save points at stays.
//   3. **A file another transaction created or depends on is never deleted.** Claims are process-wide
//      (`FileClaims`): two sessions can share one project folder. A file one transaction created and
//      another then used (a duplicate import, a node made from it) belongs to both; the creator's
//      rollback keeps it. A use outside any transaction, or by a transaction that committed, pins it.
//   4. **Render outputs are never deleted.** They are artefacts, not project state.

#include "ai/tool_context.hpp"

#include <nlohmann/json.hpp>

#include <filesystem>
#include <functional>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <vector>

namespace avgen::api {

// Which transaction created or depends on which file, for every session in the process. Keys are canonical
// paths; owners are `Session::transactionKey` values ("s2/txn-1").
class FileClaims {
public:
    static FileClaims& instance();

    // `txn` wrote `path`. True when it now owns it (nobody had claimed it); false when another live
    // transaction already did, in which case `txn` is recorded as depending on it.
    bool claim(const std::string& path, const std::string& txn);
    // `txn` (empty: no transaction) depends on `path`. A claim by a different transaction becomes shared;
    // a use outside any transaction pins it, since nothing will ever roll that use back.
    void use(const std::string& path, const std::string& txn);
    // Why `owner` may not delete `path`: another transaction depends on it, or it is pinned. Empty: it may.
    [[nodiscard]] std::string blocker(const std::string& path, const std::string& owner) const;
    // `txn` committed: its own files are permanent (claims dropped); what it depended on is pinned.
    void committed(const std::string& txn);
    // `txn` rolled back: every claim it held, owned or shared, is dropped.
    void rolledBack(const std::string& txn);
    // One file's claim by `txn` is dropped (a failed call's own file, put back by the call's log).
    void release(const std::string& path, const std::string& txn);
    // For tests.
    [[nodiscard]] bool claimed(const std::string& path) const;

private:
    struct Claim {
        std::string owner;
        std::set<std::string> dependants;
        bool pinned = false;
    };
    mutable std::mutex mutex_;
    std::map<std::string, Claim> claims_;
};

class CompensationLog {
public:
    using FileEvent = ai::ToolContext::FileEvent;
    using FileRole = ai::ToolContext::FileRole;

    // `transaction` is the process-wide key the claims are recorded under; empty for a call outside any
    // transaction (its files are kept, except when the call itself fails).
    explicit CompensationLog(std::string transaction = {}) : transaction_(std::move(transaction)) {}

    void record(const FileEvent& event);
    [[nodiscard]] bool empty() const { return entries_.empty(); }

    struct Outcome {
        std::vector<std::string> deleted;
        std::vector<std::string> restored;
        // "path: why it was kept or not restored"
        std::vector<std::string> kept;
        [[nodiscard]] nlohmann::json toJson() const;
    };
    // Puts back what can be put back, newest first. `referenced` answers rule 2 for one canonical path.
    Outcome rollback(const std::function<bool(const std::filesystem::path&)>& referenced);
    // Claims are released per file, so a failed call's log can be rolled back on its own without
    // touching the claims of the transaction it ran in.
    void commit();
    // Moves `other`'s entries in after this log's own (a successful call's files joining its transaction).
    void absorb(CompensationLog&& other);

    // The files this log created, for `transaction.status`.
    [[nodiscard]] std::vector<std::string> createdFiles() const;
    [[nodiscard]] std::vector<std::string> unrestorable() const; // overwritten without a copy

private:
    struct Entry {
        FileEvent::Kind kind = FileEvent::Kind::Created;
        std::filesystem::path path; // canonical
        FileRole role = FileRole::ProjectAsset;
        std::string previous;
        bool owned = false;
    };
    std::string transaction_;
    std::vector<Entry> entries_;
};

} // namespace avgen::api
