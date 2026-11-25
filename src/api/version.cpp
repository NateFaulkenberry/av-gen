#include "api/version.hpp"

#include "params/serialization.hpp"
#include "scene/composition.hpp"

#include "avgen_build_info.hpp"

#include <fmt/format.h>

namespace avgen::api {

nlohmann::json VersionInfo::toJson() const {
    return nlohmann::json{{"engine", engine},       {"revision", revision},
                          {"branch", branch},       {"buildType", buildType},
                          {"dirty", dirty},         {"api", api},
                          {"projectFormat", projectFormat}, {"sceneFormat", sceneFormat}};
}

const VersionInfo& version() {
    static const VersionInfo info = [] {
        VersionInfo v;
        v.engine = kEngineVersion;
        v.revision = AVGEN_GIT_REVISION;
        v.branch = AVGEN_GIT_BRANCH;
        v.buildType = AVGEN_BUILD_TYPE;
        v.dirty = AVGEN_GIT_DIRTY != 0;
        v.api = kApiLevel;
        v.projectFormat = params::kProjectFormatVersion;
        v.sceneFormat = scene::Composition::kFormatVersion;
        return v;
    }();
    return info;
}

std::string versionLine() {
    const VersionInfo& v = version();
    return fmt::format("avgen {} (api {}, revision {}{}, {})", v.engine, v.api, v.revision, v.dirty ? "-dirty" : "",
                       v.buildType);
}

} // namespace avgen::api
