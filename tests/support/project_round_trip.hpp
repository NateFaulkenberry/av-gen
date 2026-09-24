#pragma once

// The persistence round-trip gate the Director program requires (director-system-progress.md,
// spec §7): create -> serialize -> deserialize -> compare, through the REAL save and load.
//
// **It steps the engine before it saves, and that is not optional.** The worst save defect of the
// week (hero anchors drifting on every save, `test_glowmere_multicam_defects.cpp`) was invisible to
// a serializer round trip: the document straight after a load was fine, and the drift appeared only
// once the engine had *run a frame* and the save photographed live state back over the authored
// value. So `saveAndReload` refuses to take fewer than one frame, and it steps the reloaded engine
// too, so what is compared on the far side is a session that has run, not a parse.
//
// It saves through `Engine::saveProject` -- the function the editor's Cmd+S calls -- rather than
// `projectDocument`, so a test built on it exercises the bytes that reach the disk. And it compares
// the top-level key sets as well as the domain a test cares about, because an app save once removed
// two whole top-level keys while the file grew (the camera-bake defect): size says nothing.

#include "app/engine.hpp"
#include "core/error.hpp"
#include "core/time.hpp"

#include <nlohmann/json.hpp>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <set>
#include <string>
#include <string_view>
#include <system_error>
#include <unistd.h>
#include <vector>

namespace avgen::testsupport {

// A directory unique to this process and label, removed when it goes out of scope. The pid is in
// the name because every agent in a session shares one temp root (docs/testing.md, "The scratchpad
// is shared by every agent in a session").
class ScratchDir {
public:
    explicit ScratchDir(std::string_view label)
        : path_(std::filesystem::temp_directory_path() /
                (std::string("avgen_") + std::string(label) + "_" +
                 std::to_string(static_cast<long long>(::getpid())))) {
        std::error_code ec;
        std::filesystem::remove_all(path_, ec);
        std::filesystem::create_directories(path_, ec);
    }
    ~ScratchDir() {
        std::error_code ec;
        std::filesystem::remove_all(path_, ec);
    }
    ScratchDir(const ScratchDir&) = delete;
    ScratchDir& operator=(const ScratchDir&) = delete;

    [[nodiscard]] const std::filesystem::path& path() const { return path_; }
    [[nodiscard]] std::filesystem::path operator/(std::string_view name) const { return path_ / name; }

private:
    std::filesystem::path path_;
};

// Steps `frames` frames of 1/60 s from `startSeconds`. The first frame has a zero delta, like the
// application's.
inline void stepFrames(app::Engine& engine, int frames, double startSeconds = 0.0) {
    constexpr double kDt = 1.0 / 60.0;
    for (int i = 0; i < frames; ++i) {
        FrameTime time;
        time.renderTime = startSeconds + (static_cast<double>(i) * kDt);
        time.deltaTime = i == 0 ? 0.0 : kDt;
        time.frameIndex = static_cast<std::uint64_t>(i);
        engine.update(time);
    }
}

inline nlohmann::json readJson(const std::filesystem::path& path) {
    std::ifstream in(path);
    return nlohmann::json::parse(in, nullptr, false);
}

struct ProjectRoundTrip {
    nlohmann::json saved;                  // the file the first engine wrote
    std::unique_ptr<app::Engine> reloaded; // a fresh Offline engine that loaded it and ran a frame
    nlohmann::json resaved;                // what the reloaded engine would write, after its frame
    std::vector<std::string> warnings;     // the reload's projectWarnings()
};

// Steps `engine` (at least one frame), saves it to `file` through the real save path, loads that
// file into a fresh Offline engine, steps that one frame, and serialises it again. Fails only when
// the save or the load fails; everything else is for the caller to compare.
inline Result<ProjectRoundTrip> saveAndReload(app::Engine& engine, const std::filesystem::path& file,
                                              int framesBeforeSave = 1) {
    stepFrames(engine, framesBeforeSave < 1 ? 1 : framesBeforeSave);
    if (auto saved = engine.saveProject(file); !saved) {
        return std::unexpected(saved.error());
    }
    ProjectRoundTrip out;
    out.saved = readJson(file);
    out.reloaded = std::make_unique<app::Engine>(app::EngineMode::Offline);
    if (auto loaded = out.reloaded->loadProject(file); !loaded) {
        return std::unexpected(loaded.error());
    }
    out.warnings = out.reloaded->projectWarnings();
    stepFrames(*out.reloaded, 1);
    out.resaved = out.reloaded->projectDocument(file);
    return out;
}

// Top-level keys `before` has and `after` does not. The check the camera-bake defect needed.
inline std::vector<std::string> missingTopLevelKeys(const nlohmann::json& before,
                                                    const nlohmann::json& after) {
    std::vector<std::string> missing;
    if (!before.is_object()) {
        return missing;
    }
    for (const auto& [key, value] : before.items()) {
        if (!after.is_object() || !after.contains(key)) {
            missing.push_back(key);
        }
    }
    return missing;
}

// The JSON-patch paths on which two documents differ, for a failure message a person can act on.
// Capped, because a whole-domain mismatch can produce thousands.
inline std::vector<std::string> differingPaths(const nlohmann::json& a, const nlohmann::json& b,
                                               std::size_t cap = 24) {
    std::vector<std::string> out;
    for (const auto& op : nlohmann::json::diff(a, b)) {
        if (out.size() >= cap) {
            out.emplace_back("...");
            break;
        }
        out.push_back(op.value("op", std::string{}) + " " + op.value("path", std::string{}));
    }
    return out;
}

} // namespace avgen::testsupport
