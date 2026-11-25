#include "api/path_policy.hpp"

#include "app/engine.hpp"
#include "assets/asset_registry.hpp"

#include <fmt/format.h>

#include <algorithm>
#include <system_error>

namespace avgen::api {
namespace {

namespace fs = std::filesystem;

constexpr std::string_view kRootsRecovery = "start the session with --root <dir> or --write-root <dir>";

ai::ToolError refusal(ai::ToolErrorCode code, std::string message, std::string recovery,
                      nlohmann::json details = nullptr) {
    return ai::ToolError{code, std::move(message), std::move(recovery), std::move(details)};
}

fs::path canonicalOrEmpty(const fs::path& p) {
    std::error_code ec;
    fs::path full = fs::weakly_canonical(p, ec);
    return ec ? fs::path{} : full;
}

} // namespace

const char* projectAccessName(ProjectAccess access) {
    switch (access) {
    case ProjectAccess::None: return "none";
    case ProjectAccess::Read: return "read";
    case ProjectAccess::ReadWrite: return "read-write";
    }
    return "none";
}

bool pathInside(const fs::path& full, const fs::path& root) {
    if (root.empty()) {
        return false;
    }
    const fs::path a = canonicalOrEmpty(full);
    const fs::path b = canonicalOrEmpty(root);
    if (a.empty() || b.empty()) {
        return false;
    }
    const fs::path rel = a.lexically_relative(b);
    return !rel.empty() && *rel.begin() != "..";
}

ai::ToolError noRootsRefusal(std::string_view what) {
    return refusal(ai::ToolErrorCode::PermissionDenied,
                   fmt::format("this session was given no folders it may {}", what), std::string(kRootsRecovery));
}

// ---- permissions ---------------------------------------------------------------------------------

bool SessionPermissions::within(const SessionPermissions& wider) const {
    const auto underAny = [](const fs::path& p, const std::vector<fs::path>& roots) {
        return std::any_of(roots.begin(), roots.end(), [&](const fs::path& r) { return pathInside(p, r); });
    };
    for (const fs::path& r : readRoots) {
        if (!underAny(r, wider.readRoots) && !underAny(r, wider.writeRoots)) {
            return false;
        }
    }
    for (const fs::path& r : writeRoots) {
        if (!underAny(r, wider.writeRoots)) {
            return false;
        }
    }
    if ((openProjectFolder && !wider.openProjectFolder) || (gpu && !wider.gpu) ||
        (subprocess && !wider.subprocess) || (liveControl && !wider.liveControl)) {
        return false;
    }
    return static_cast<int>(projectAccess) <= static_cast<int>(wider.projectAccess);
}

nlohmann::json SessionPermissions::toJson() const {
    nlohmann::json read = nlohmann::json::array();
    for (const fs::path& r : readRoots) {
        read.push_back(r.string());
    }
    nlohmann::json write = nlohmann::json::array();
    for (const fs::path& r : writeRoots) {
        write.push_back(r.string());
    }
    return nlohmann::json{{"readRoots", std::move(read)},
                          {"writeRoots", std::move(write)},
                          {"openProjectFolder", openProjectFolder},
                          {"projectAccess", projectAccessName(projectAccess)},
                          {"gpu", gpu},
                          {"subprocess", subprocess},
                          {"liveControl", liveControl}};
}

// ---- the policy ------------------------------------------------------------------------------------

PathPolicy::PathPolicy(SessionPermissions permissions, const app::Engine* engine)
    : permissions_(std::move(permissions)), engine_(engine) {}

fs::path PathPolicy::projectFolder() const {
    if (engine_ == nullptr || engine_->projectPath().empty()) {
        return {};
    }
    // The engine keeps the path it was given. A `--project column.json` on the command line was resolved
    // by the shell's working directory when it was opened; making it absolute here interprets the engine's
    // own record, not a client's argument.
    std::error_code ec;
    const fs::path file = fs::absolute(engine_->projectPath(), ec);
    return ec ? fs::path{} : file.lexically_normal().parent_path();
}

fs::path PathPolicy::firstWriteRoot() const {
    return permissions_.writeRoots.empty() ? fs::path{} : permissions_.writeRoots.front();
}

bool PathPolicy::hasAnyRoot() const {
    return !permissions_.readRoots.empty() || !permissions_.writeRoots.empty() ||
           (permissions_.openProjectFolder && permissions_.projectAccess != ProjectAccess::None &&
            !projectFolder().empty());
}

std::vector<fs::path> PathPolicy::searchRoots() const {
    std::vector<fs::path> out = permissions_.readRoots;
    if (permissions_.openProjectFolder && permissions_.projectAccess != ProjectAccess::None) {
        if (const fs::path folder = projectFolder(); !folder.empty()) {
            const fs::path canonicalFolder = canonicalOrEmpty(folder);
            const bool listed = std::any_of(out.begin(), out.end(), [&](const fs::path& r) {
                return canonicalOrEmpty(r) == canonicalFolder;
            });
            if (!listed) {
                out.push_back(folder);
            }
        }
    }
    return out;
}

bool PathPolicy::inside(const fs::path& full, bool forWriting) const {
    for (const fs::path& root : permissions_.writeRoots) {
        if (pathInside(full, root)) {
            return true;
        }
    }
    if (!forWriting) {
        for (const fs::path& root : permissions_.readRoots) {
            if (pathInside(full, root)) {
                return true;
            }
        }
    }
    const bool projectAllowed = permissions_.openProjectFolder &&
                                (forWriting ? permissions_.projectAccess == ProjectAccess::ReadWrite
                                            : permissions_.projectAccess != ProjectAccess::None);
    return projectAllowed && pathInside(full, projectFolder());
}

PathResult PathPolicy::resolveAssetId(std::string_view id) const {
    constexpr std::string_view kProject = "asset://project/";
    if (!id.starts_with(kProject)) {
        return std::unexpected(refusal(
            ai::ToolErrorCode::Unsupported,
            fmt::format("'{}' is not an id this engine resolves; project assets are asset://project/<path>", id),
            "pass the file path, or the asset://project/ id asset.import returned (asset://engine/ is reserved)",
            nlohmann::json{{"path", std::string(id)}}));
    }
    if (id.size() == kProject.size()) {
        return std::unexpected(refusal(ai::ToolErrorCode::InvalidArguments, fmt::format("'{}' names no file", id),
                                       "asset://project/<type>/<file>.<ext>", nlohmann::json{{"path", std::string(id)}}));
    }
    const fs::path folder = projectFolder();
    if (folder.empty()) {
        return std::unexpected(refusal(ai::ToolErrorCode::FailedPrecondition,
                                       fmt::format("'{}' names a file in the open project, and none is open", id),
                                       "use project.create or project.open first",
                                       nlohmann::json{{"path", std::string(id)}}));
    }
    // The one resolver: the registry's own mapping, anchored to the project folder. A private registry,
    // because the engine's is anchored to the scene file's folder and belongs to the host thread.
    const assets::AssetRegistry anchor(folder);
    const fs::path full = anchor.resolve(fs::path(std::string(id)));
    if (!pathInside(full, folder / "assets")) {
        return std::unexpected(refusal(
            ai::ToolErrorCode::PermissionDenied,
            fmt::format("'{}' leaves the project's assets folder", id),
            "an asset id names a file below <project>/assets; asset.list reports the ids there",
            nlohmann::json{{"path", std::string(id)}, {"resolved", full.string()}}));
    }
    return full.lexically_normal();
}

std::string PathPolicy::assetIdFor(const fs::path& file) const {
    const fs::path folder = projectFolder();
    if (folder.empty() || file.empty()) {
        return {};
    }
    const assets::AssetRegistry anchor(folder);
    const fs::path full = anchor.resolve(file);
    // assetId tests the lexical relation; the containment answer is taken on the canonical form, as everywhere.
    return pathInside(full, folder / "assets") ? anchor.assetId(full) : std::string{};
}

PathResult PathPolicy::resolve(std::string_view given) const {
    if (given.empty()) {
        return std::unexpected(refusal(ai::ToolErrorCode::InvalidArguments, "no path given", "name a file"));
    }
    if (given.starts_with("asset://")) {
        return resolveAssetId(given);
    }
    fs::path p{std::string(given)};
    if (p.is_relative()) {
        fs::path base = projectFolder();
        if (base.empty()) {
            base = firstWriteRoot();
        }
        if (base.empty()) {
            return std::unexpected(refusal(
                ai::ToolErrorCode::PermissionDenied,
                fmt::format("'{}' is relative, and this session has no project folder or write root to resolve it "
                            "against",
                            given),
                "pass an absolute path, open a project, or " + std::string(kRootsRecovery),
                nlohmann::json{{"path", std::string(given)}}));
        }
        p = base / p;
    }
    // The path handed back is absolute and lexically normal, in the caller's spelling (`/var/...` stays
    // `/var/...` on a machine where that is a symlink); every containment answer is taken on its canonical
    // form, which is the file the operating system will actually open.
    const fs::path lexical = p.lexically_normal();
    if (canonicalOrEmpty(lexical).empty()) {
        return std::unexpected(refusal(ai::ToolErrorCode::InvalidArguments,
                                       fmt::format("'{}' cannot be resolved to a path", given), "name a file",
                                       nlohmann::json{{"path", std::string(given)}}));
    }
    return lexical;
}

PathResult PathPolicy::forRead(std::string_view given) const {
    if (!hasAnyRoot()) {
        return std::unexpected(noRootsRefusal("read"));
    }
    auto resolved = resolve(given);
    if (!resolved) {
        return resolved;
    }
    if (!inside(*resolved, false)) {
        return std::unexpected(
            refusal(ai::ToolErrorCode::PermissionDenied,
                    fmt::format("'{}' is not inside a folder this session may read", given),
                    "asset.list_importable reports what is reachable; session.info lists the read roots",
                    nlohmann::json{{"path", std::string(given)}, {"resolved", resolved->string()}}));
    }
    std::error_code ec;
    if (!fs::exists(*resolved, ec)) {
        return std::unexpected(refusal(ai::ToolErrorCode::NotFound, fmt::format("'{}' does not exist", given),
                                       "asset.list_importable reports the files that are there",
                                       nlohmann::json{{"path", std::string(given)}, {"resolved", resolved->string()}}));
    }
    return resolved;
}

PathResult PathPolicy::forWrite(std::string_view given) const {
    const bool projectWritableRule = permissions_.openProjectFolder && !projectFolder().empty();
    if (permissions_.writeRoots.empty() && !projectWritableRule) {
        return std::unexpected(noRootsRefusal("write"));
    }
    if (permissions_.projectAccess != ProjectAccess::ReadWrite) {
        return std::unexpected(refusal(ai::ToolErrorCode::PermissionDenied,
                                       fmt::format("this session may not write ('{}')", given),
                                       "the session's projectAccess is read-only; start one with write access",
                                       nlohmann::json{{"path", std::string(given)}}));
    }
    auto resolved = resolve(given);
    if (!resolved) {
        return resolved;
    }
    if (!inside(*resolved, true)) {
        return std::unexpected(
            refusal(ai::ToolErrorCode::PermissionDenied,
                    fmt::format("'{}' is not inside a folder this session may write", given),
                    "session.info lists the write roots",
                    nlohmann::json{{"path", std::string(given)}, {"resolved", resolved->string()}}));
    }
    return resolved;
}

std::optional<ai::ToolError> PathPolicy::projectWriteRefusal() const {
    const fs::path folder = projectFolder();
    if (folder.empty()) {
        return refusal(ai::ToolErrorCode::FailedPrecondition, "this session has no project open",
                       "use project.create or project.open first");
    }
    if (permissions_.projectAccess != ProjectAccess::ReadWrite) {
        return refusal(ai::ToolErrorCode::PermissionDenied, "this session may read the project but not write it",
                       "start a session with projectAccess read-write");
    }
    if (!inside(folder, true)) {
        return refusal(ai::ToolErrorCode::PermissionDenied,
                       fmt::format("the open project's folder '{}' is not inside a folder this session may write",
                                   folder.string()),
                       std::string(kRootsRecovery), nlohmann::json{{"path", folder.string()}});
    }
    return std::nullopt;
}

bool PathPolicy::projectWritable() const { return !projectWriteRefusal().has_value(); }

} // namespace avgen::api
