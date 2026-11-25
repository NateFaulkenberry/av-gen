#pragma once

// The engine's one version identity (ADR-2150). Before this there were three answers: the project
// stamp said 0.1.0 (`Engine::kAppVersion`), the window title said 0.2 and the start-up log said
// 0.1.0, while the revision that actually identifies a build lived only in the generated build-info
// header. Every place that states a version now reads this.

#include <nlohmann/json.hpp>

#include <string>

namespace avgen::api {

// The engine's release version. Bumped by hand at a release, never per commit: the revision says
// which commit a build came from.
inline constexpr const char* kEngineVersion = "0.3.0";

// The public API level, an integer. Bumped on a BREAKING change to the tool catalogue -- a tool
// removed or renamed, a required input added, an input's type narrowed, an output field removed --
// and never for an additive one. There are no aliases for a renamed tool (ADR-441): the level goes
// up and every caller moves. tests/unit/test_api_catalogue.cpp refuses a breaking catalogue change
// that does not bump it.
inline constexpr int kApiLevel = 2; // 2: ADR-2156 (project tools take a path; asset.import requires file)

struct VersionInfo {
    std::string engine;    // kEngineVersion
    std::string revision;  // the git revision the build came from ("unknown" outside a checkout)
    std::string branch;
    std::string buildType; // Release, Debug, ...
    bool dirty = false;    // the tree had uncommitted tracked changes at build time
    int api = kApiLevel;
    int projectFormat = 0; // the avgen-project version this build writes
    int sceneFormat = 0;   // the avgen-scene version this build reads and writes

    [[nodiscard]] nlohmann::json toJson() const;
};

[[nodiscard]] const VersionInfo& version();

// "avgen 0.3.0 (api 1, revision 1a2b3c4d5e6f, Release)" with "-dirty" on the revision when it was.
[[nodiscard]] std::string versionLine();

} // namespace avgen::api
