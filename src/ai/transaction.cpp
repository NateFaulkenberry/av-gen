#include "ai/transaction.hpp"

#include "app/engine.hpp"
#include "core/log.hpp"
#include "params/serialization.hpp"

#include <algorithm>
#include <chrono>

namespace avgen::ai {
namespace {

double nowSeconds() {
    using clock = std::chrono::steady_clock;
    static const clock::time_point origin = clock::now();
    return std::chrono::duration<double>(clock::now() - origin).count();
}

} // namespace

nlohmann::json ProjectSnapshot::describe() const {
    nlohmann::json j;
    j["id"] = id;
    j["label"] = label;
    j["createdAt"] = createdAt;
    if (const auto p = document.find("parameters"); p != document.end() && p->is_object()) {
        j["parameters"] = p->size();
    }
    if (const auto r = document.find("routes"); r != document.end() && r->is_array()) {
        j["routes"] = r->size();
    }
    if (const auto t = document.find("timeline"); t != document.end() && t->is_object()) {
        if (const auto tracks = t->find("tracks"); tracks != t->end() && tracks->is_array()) {
            j["timelineTracks"] = tracks->size();
        }
    }
    return j;
}

// ---- capture / apply --------------------------------------------------------------------------

nlohmann::json SnapshotStore::captureDocument(const app::Engine& engine) {
    // Sources are deliberately omitted: nothing in this pass's tool surface creates or removes one,
    // and restoring the rack would mean re-attaching it to the bus, which is the engine's own
    // private sequencing. See the header for why the domain is exactly this.
    auto& mutableEngine = const_cast<app::Engine&>(engine); // NOLINT: the accessors below are non-const
    nlohmann::json doc = params::saveProject(mutableEngine.params(), mutableEngine.modulator(),
                                             nullptr, &mutableEngine.presets());
    doc["timeline"] = mutableEngine.timeline().toJson();
    return doc;
}

Result<void> SnapshotStore::applyDocument(app::Engine& engine, const nlohmann::json& doc) {
    if (auto r = params::loadProject(doc, engine.params(), engine.modulator(), nullptr,
                                     &engine.presets());
        !r) {
        return r;
    }
    if (const auto timeline = doc.find("timeline"); timeline != doc.end()) {
        if (auto r = engine.timeline().fromJson(*timeline); !r) {
            return r;
        }
    } else {
        engine.timeline().clear();
    }
    // Routes and tracks hold raw parameter pointers; after a load they name paths again and have
    // to be resolved. Skipping this is the exact failure ADR-019 hit once already -- a loadProject
    // that destroyed the routes it had just installed.
    engine.rebind();
    engine.modulator().resetState();
    return {};
}

std::vector<std::string> SnapshotStore::changedParameters(const nlohmann::json& before,
                                                          const nlohmann::json& after) {
    std::vector<std::string> changed;
    const auto beforeParams = before.find("parameters");
    const auto afterParams = after.find("parameters");
    const bool haveBefore = beforeParams != before.end() && beforeParams->is_object();
    const bool haveAfter = afterParams != after.end() && afterParams->is_object();
    if (!haveAfter) {
        return changed;
    }
    for (const auto& [path, value] : afterParams->items()) {
        if (!haveBefore || !beforeParams->contains(path) || beforeParams->at(path) != value) {
            changed.push_back(path);
        }
    }
    if (haveBefore) {
        for (const auto& [path, value] : beforeParams->items()) {
            if (!afterParams->contains(path)) {
                changed.push_back(path);
            }
        }
    }
    std::sort(changed.begin(), changed.end());
    changed.erase(std::unique(changed.begin(), changed.end()), changed.end());
    return changed;
}

const ProjectSnapshot& SnapshotStore::capture(const app::Engine& engine, std::string label) {
    ProjectSnapshot snapshot;
    snapshot.id = fmt::format("snap-{}", nextId_++);
    snapshot.label = std::move(label);
    snapshot.createdAt = nowSeconds();
    snapshot.document = captureDocument(engine);
    snapshots_.push_back(std::move(snapshot));
    if (capacity_ > 0 && snapshots_.size() > capacity_) {
        snapshots_.erase(snapshots_.begin());
    }
    return snapshots_.back();
}

const ProjectSnapshot* SnapshotStore::find(std::string_view id) const {
    const auto it = std::find_if(snapshots_.begin(), snapshots_.end(),
                                 [&](const ProjectSnapshot& s) { return s.id == id; });
    return it == snapshots_.end() ? nullptr : &*it;
}

Result<void> SnapshotStore::restore(app::Engine& engine, std::string_view id) {
    const ProjectSnapshot* snapshot = find(id);
    if (snapshot == nullptr) {
        return fail("no snapshot '{}'", id);
    }
    // Copy: applyDocument migrates in place inside params::loadProject and the stored snapshot has
    // to stay restorable more than once.
    const nlohmann::json document = snapshot->document;
    return applyDocument(engine, document);
}

// ---- the snapshot-backed sink ------------------------------------------------------------------

void SnapshotTransactionSink::begin(const std::string& label) {
    if (engine_ == nullptr || store_ == nullptr) {
        return;
    }
    openId_ = store_->capture(*engine_, label).id;
    log::info("ai: transaction '{}' opened ({})", label, openId_);
}

void SnapshotTransactionSink::commit(const std::string& label) {
    // Nothing to do but let go of the rollback point: the changes are already in the engine, and
    // the snapshot stays in the store so `project.restore_snapshot` can still reach it.
    log::info("ai: transaction '{}' committed (rollback point {})", label,
              openId_.empty() ? "none" : openId_);
    openId_.clear();
}

void SnapshotTransactionSink::abort() {
    if (engine_ == nullptr || store_ == nullptr || openId_.empty()) {
        return;
    }
    if (auto r = store_->restore(*engine_, openId_); !r) {
        // The one case where the project can be left partially modified. Say so loudly rather than
        // silently: the snapshot is still in the store and can be restored by hand.
        log::error("ai: rollback to {} failed: {}", openId_, r.error().message);
    } else {
        log::info("ai: rolled back to {}", openId_);
    }
    openId_.clear();
}

// ---- RAII ---------------------------------------------------------------------------------------

Transaction::Transaction(TransactionSink& sink, std::string label)
    : sink_(&sink), label_(std::move(label)) {
    sink_->begin(label_);
}

Transaction::~Transaction() {
    if (open_) {
        sink_->abort();
    }
}

void Transaction::commit() {
    if (!open_) {
        return;
    }
    open_ = false;
    sink_->commit(label_);
}

void Transaction::rollback() {
    if (!open_) {
        return;
    }
    open_ = false;
    sink_->abort();
}

} // namespace avgen::ai
