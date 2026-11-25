#pragma once

// Session permissions and the one place a client-supplied path is resolved (ADR-2153, Engine API S1).
//
// Before this, every tool that took a path resolved it itself: against the process's working directory,
// tested against `ToolContext::contentRoots` for a read and against nothing at all for a write, and refused
// with whichever code its author picked (`NotFound` for "outside the roots", `Unavailable` for "no root").
// An agent could not tell "missing" from "forbidden", and a relative path meant whatever folder the
// process happened to start in.
//
// Now a tool asks the session's policy, through `ToolContext::paths()`:
//
//   - `forRead(given)`  : the path resolved and inside a readable root, or `PermissionDenied` / `NotFound`;
//   - `forWrite(given)` : the path resolved and inside a writable root, or `PermissionDenied`;
//   - `projectWritable()`: whether the open project's folder may be written (imports, saves).
//
// Resolution:
//
//   1. A relative path is relative to the open project's folder; with no project open, to the first write
//      root. Never to the process's working directory: a session started from `/` and one started from the
//      project folder resolve `assets/a.glb` the same way.
//   2. It is made absolute and canonical (`weakly_canonical`: `..` and symlinks are resolved) BEFORE the
//      containment test, because a prefix test on an unresolved path is not a containment test --
//      "<root>/../../etc/passwd" starts with the root. This is `ToolContext::resolveContent`'s algorithm,
//      moved here; that function is gone.
//   3. Write roots are implicitly readable.
//   4. `asset://project/<p>` is an asset id (ADR-2156, stage S3): the file `<project>/assets/<p>`, resolved by
//      `AssetRegistry::resolve` anchored to the open PROJECT's folder (not the scene file's, which is the
//      engine registry's base), then held to `<project>/assets` on its canonical form -- so `..` and a
//      symlink that leaves the folder are refused -- and then to the same root test as any path. Every tool
//      that takes a file therefore takes an id too, with no resolution of its own. Other `asset://` schemes
//      are refused with `Unsupported` (`asset://engine/` is reserved).
//
// The engine's own resources (shaders, built-in content, help) are not gated: permissions apply to paths a
// client names and to files the session writes.

#include "ai/tool_api.hpp"

#include <nlohmann/json.hpp>

#include <cstdint>
#include <expected>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace avgen::app {
class Engine;
} // namespace avgen::app

namespace avgen::api {

enum class ProjectAccess : std::uint8_t {
    None,      // no project tool may run (a session that only inspects the engine's catalogue)
    Read,      // a project may be opened and read; nothing that changes or writes it runs
    ReadWrite, // the default
};
[[nodiscard]] const char* projectAccessName(ProjectAccess access);

// Fixed when the session is created. `Session::narrow` may take permissions away; nothing adds them.
struct SessionPermissions {
    std::vector<std::filesystem::path> readRoots;
    std::vector<std::filesystem::path> writeRoots; // implicitly readable
    // The open project's folder is a read AND write root, whichever folder that is at the moment of the
    // call (owner decision 1, for Studio's assistant). A rule rather than a path, so it follows every way a
    // project is opened -- File > Open, a recent file, `project.open`, `--project` -- without each of them
    // having to remember to refresh a list. Off for every other client.
    bool openProjectFolder = false;
    ProjectAccess projectAccess = ProjectAccess::ReadWrite;
    bool gpu = true;          // may start GPU work (render jobs: stage S6)
    bool subprocess = false;  // may start a child process (performance.profile_scene and its family)
    bool liveControl = false; // OSC/MIDI listeners; set only by the editor's own rule (app/cli_batch.hpp)

    // True when every permission here is also granted by `wider`: a subset of its roots (each root inside
    // one of `wider`'s), no flag `wider` lacks, and no greater project access.
    [[nodiscard]] bool within(const SessionPermissions& wider) const;
    [[nodiscard]] nlohmann::json toJson() const;
};

using PathResult = std::expected<std::filesystem::path, ai::ToolError>;

// The refusal a session with no roots at all gives a path tool. One sentence, so every tool says the same.
[[nodiscard]] ai::ToolError noRootsRefusal(std::string_view what);

class PathPolicy {
public:
    // No roots: refuses every path (the right default -- a session that was never told where it may read
    // must not have somewhere guessed for it).
    PathPolicy() = default;
    // `engine` supplies the open project's folder, read at each call (host thread). Null: no project.
    PathPolicy(SessionPermissions permissions, const app::Engine* engine);

    // A path the session may read. `PermissionDenied` outside every readable root (details.path is what
    // was given, details.resolved what it resolved to), `NotFound` when it is inside but does not exist,
    // `InvalidArguments` when empty. An `asset://project/` id resolves as described above (`FailedPrecondition`
    // with no project open, `PermissionDenied` when it leaves the project's assets folder); another
    // `asset://` scheme is `Unsupported`.
    [[nodiscard]] PathResult forRead(std::string_view given) const;
    // A path the session may write (it need not exist). `PermissionDenied` outside every writable root.
    [[nodiscard]] PathResult forWrite(std::string_view given) const;
    // A project is open, the session may write projects, and the project's folder is inside a writable
    // root. What `project.save` and the import tools need before they write beside the project file.
    [[nodiscard]] bool projectWritable() const;
    // Why not, as a refusal; empty when it is.
    [[nodiscard]] std::optional<ai::ToolError> projectWriteRefusal() const;

    // The open project's folder, absolute, or empty.
    [[nodiscard]] std::filesystem::path projectFolder() const;
    // Where a bare name (`project.create {name}`) lives: the first write root, or empty.
    [[nodiscard]] std::filesystem::path firstWriteRoot() const;
    // The asset id of a file in the open project's assets folder (`asset://project/models/x.glb`), the form
    // `AssetRegistry::assetId` gives anchored to the project folder; empty for any other file or no project.
    [[nodiscard]] std::string assetIdFor(const std::filesystem::path& file) const;
    // The explicit read roots, as given.
    [[nodiscard]] const std::vector<std::filesystem::path>& readRoots() const { return permissions_.readRoots; }
    // The folders a listing walks (`asset.list_importable`, `world.list_recipes`): the read roots, then the
    // open project's folder when the session may read it. Write roots are readable but not walked: in
    // Studio the first one is every project the person has, and a listing of all of them is not "media
    // you could import".
    [[nodiscard]] std::vector<std::filesystem::path> searchRoots() const;
    // Whether the session can read anything at all (any root, or the open project folder rule).
    [[nodiscard]] bool hasAnyRoot() const;
    [[nodiscard]] const SessionPermissions& permissions() const { return permissions_; }

private:
    [[nodiscard]] PathResult resolve(std::string_view given) const;
    [[nodiscard]] bool inside(const std::filesystem::path& full, bool forWriting) const;
    // `asset://project/<p>` -> the project-anchored registry, held to `<project>/assets` (ADR-2156).
    [[nodiscard]] PathResult resolveAssetId(std::string_view id) const;

    SessionPermissions permissions_;
    const app::Engine* engine_ = nullptr;
};

// True when `full` is `root` or below it, after both are made canonical. Exposed for the compensation log
// and the tests; the policy uses it for every containment answer.
[[nodiscard]] bool pathInside(const std::filesystem::path& full, const std::filesystem::path& root);

} // namespace avgen::api
