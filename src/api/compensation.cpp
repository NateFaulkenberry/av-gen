#include "api/compensation.hpp"

#include "core/log.hpp"

#include <fmt/format.h>
#include <fmt/ranges.h>

#include <algorithm>
#include <fstream>
#include <system_error>

namespace avgen::api {
namespace {

namespace fs = std::filesystem;

fs::path canonicalPath(const fs::path& p) {
    std::error_code ec;
    fs::path full = fs::weakly_canonical(p, ec);
    return ec ? p : full;
}

} // namespace

// ---- claims ----------------------------------------------------------------------------------------

FileClaims& FileClaims::instance() {
    static FileClaims claims;
    return claims;
}

bool FileClaims::claim(const std::string& path, const std::string& txn) {
    if (txn.empty()) {
        return false;
    }
    const std::lock_guard lock(mutex_);
    auto it = claims_.find(path);
    if (it == claims_.end()) {
        claims_.emplace(path, Claim{txn, {}, false});
        return true;
    }
    if (it->second.owner == txn) {
        return true;
    }
    it->second.dependants.insert(txn);
    return false;
}

void FileClaims::use(const std::string& path, const std::string& txn) {
    const std::lock_guard lock(mutex_);
    auto it = claims_.find(path);
    if (it == claims_.end() || it->second.owner == txn) {
        return;
    }
    if (txn.empty()) {
        it->second.pinned = true;
    } else {
        it->second.dependants.insert(txn);
    }
}

std::string FileClaims::blocker(const std::string& path, const std::string& owner) const {
    const std::lock_guard lock(mutex_);
    const auto it = claims_.find(path);
    if (it == claims_.end()) {
        return {};
    }
    if (it->second.owner != owner) {
        return fmt::format("created by another transaction ({})", it->second.owner);
    }
    if (it->second.pinned) {
        return "used outside this transaction, or by a transaction that committed";
    }
    if (!it->second.dependants.empty()) {
        return fmt::format("used by another open transaction ({})", *it->second.dependants.begin());
    }
    return {};
}

void FileClaims::committed(const std::string& txn) {
    if (txn.empty()) {
        return;
    }
    const std::lock_guard lock(mutex_);
    for (auto it = claims_.begin(); it != claims_.end();) {
        if (it->second.owner == txn) {
            it = claims_.erase(it);
            continue;
        }
        if (it->second.dependants.erase(txn) > 0) {
            it->second.pinned = true;
        }
        ++it;
    }
}

void FileClaims::rolledBack(const std::string& txn) {
    if (txn.empty()) {
        return;
    }
    const std::lock_guard lock(mutex_);
    for (auto it = claims_.begin(); it != claims_.end();) {
        if (it->second.owner == txn) {
            it = claims_.erase(it);
            continue;
        }
        it->second.dependants.erase(txn);
        ++it;
    }
}

void FileClaims::release(const std::string& path, const std::string& txn) {
    if (txn.empty()) {
        return;
    }
    const std::lock_guard lock(mutex_);
    const auto it = claims_.find(path);
    if (it == claims_.end()) {
        return;
    }
    if (it->second.owner == txn) {
        claims_.erase(it);
    } else {
        it->second.dependants.erase(txn);
    }
}

bool FileClaims::claimed(const std::string& path) const {
    const std::lock_guard lock(mutex_);
    return claims_.contains(path);
}

// ---- the log -----------------------------------------------------------------------------------------

void CompensationLog::record(const FileEvent& event) {
    const fs::path path = canonicalPath(event.path);
    const std::string key = path.string();
    FileClaims& claims = FileClaims::instance();
    const auto mine = std::find_if(entries_.begin(), entries_.end(), [&](const Entry& e) { return e.path == path; });
    if (event.kind == FileEvent::Kind::Used) {
        if (mine == entries_.end()) {
            claims.use(key, transaction_);
        }
        return;
    }
    if (mine != entries_.end()) {
        // The first write in this log is the one a rollback undoes: a file this transaction created and
        // then rewrote is still a file it created, and a replaced file's bytes are the ones it found.
        return;
    }
    Entry entry;
    entry.kind = event.kind;
    entry.path = path;
    entry.role = event.role;
    entry.previous = event.previous;
    entry.owned = transaction_.empty() ? !claims.claimed(key) : claims.claim(key, transaction_);
    entries_.push_back(std::move(entry));
}

CompensationLog::Outcome CompensationLog::rollback(const std::function<bool(const fs::path&)>& referenced) {
    Outcome out;
    FileClaims& claims = FileClaims::instance();
    for (auto it = entries_.rbegin(); it != entries_.rend(); ++it) {
        const Entry& e = *it;
        const std::string key = e.path.string();
        // Whatever happens to the file, this log is done with it; a blocker is read first.
        struct Release {
            FileClaims& claims;
            const std::string& key;
            const std::string& txn;
            bool owned;
            ~Release() {
                if (owned) {
                    claims.release(key, txn);
                }
            }
        } release{claims, key, transaction_, e.owned};
        if (e.role == FileRole::RenderOutput) {
            out.kept.push_back(key + ": a render output; rollbacks never delete artefacts");
            continue;
        }
        if (e.kind == FileEvent::Kind::Overwritten) {
            out.kept.push_back(key + ": overwritten, and its previous contents were not kept");
            continue;
        }
        std::string blocker = e.owned ? claims.blocker(key, transaction_) : std::string("created by another transaction");
        if (blocker.empty() && e.kind == FileEvent::Kind::Created && referenced && referenced(e.path)) {
            blocker = "referenced by the project or scene as restored, or by the saved files on disk";
        }
        if (!blocker.empty()) {
            out.kept.push_back(key + ": " + blocker);
            continue;
        }
        std::error_code ec;
        if (e.kind == FileEvent::Kind::Created) {
            if (fs::remove(e.path, ec) && !ec) {
                out.deleted.push_back(key);
            } else if (ec) {
                out.kept.push_back(key + ": could not delete: " + ec.message());
            }
            continue;
        }
        // Replaced: put the bytes back.
        std::ofstream file(e.path, std::ios::binary | std::ios::trunc);
        file.write(e.previous.data(), static_cast<std::streamsize>(e.previous.size()));
        if (file) {
            out.restored.push_back(key);
        } else {
            out.kept.push_back(key + ": could not restore its previous contents");
        }
    }
    entries_.clear();
    if (!out.kept.empty()) {
        log::warn("api: rollback kept {} file(s): {}", out.kept.size(), fmt::format("{}", fmt::join(out.kept, "; ")));
    }
    return out;
}

void CompensationLog::commit() {
    FileClaims& claims = FileClaims::instance();
    for (const Entry& e : entries_) {
        if (e.owned) {
            claims.release(e.path.string(), transaction_);
        }
    }
    entries_.clear();
}

void CompensationLog::absorb(CompensationLog&& other) {
    for (Entry& e : other.entries_) {
        const bool known = std::any_of(entries_.begin(), entries_.end(), [&](const Entry& m) { return m.path == e.path; });
        if (!known) {
            entries_.push_back(std::move(e));
        }
    }
    other.entries_.clear();
}

std::vector<std::string> CompensationLog::createdFiles() const {
    std::vector<std::string> out;
    for (const Entry& e : entries_) {
        if (e.kind == FileEvent::Kind::Created) {
            out.push_back(e.path.string());
        }
    }
    return out;
}

std::vector<std::string> CompensationLog::unrestorable() const {
    std::vector<std::string> out;
    for (const Entry& e : entries_) {
        if (e.kind == FileEvent::Kind::Overwritten) {
            out.push_back(e.path.string());
        }
    }
    return out;
}

nlohmann::json CompensationLog::Outcome::toJson() const {
    return nlohmann::json{{"deleted", deleted}, {"restored", restored}, {"kept", kept}};
}

} // namespace avgen::api
