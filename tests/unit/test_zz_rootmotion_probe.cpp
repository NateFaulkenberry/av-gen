// TEMPORARY PROBE -- delete before commit.
#include "assets/gltf_loader.hpp"
#include "scene/animation.hpp"
#include "scene/scene.hpp"
#include "scene/skeleton.hpp"
#include "support/stride_speed.hpp"

#include <catch2/catch_test_macros.hpp>
#include <fmt/format.h>

#include <filesystem>

using namespace avgen;
namespace fs = std::filesystem;

static fs::path assetRoot() {
    for (fs::path p = fs::current_path(); !p.empty(); p = p.parent_path()) {
        if (fs::exists(p / "assets" / "farm")) return p / "assets";
        if (p == p.parent_path()) break;
    }
    return {};
}

TEST_CASE("PROBE landing", "[.probe]") {
    scene::Scene s;
    const fs::path p = assetRoot() / "aliens" / "alien-scout.glb";
    REQUIRE(assets::loadGltf(p, s));
    REQUIRE(!s.rigs.empty());
    const scene::SkinnedRig& rig = s.rigs[0];
    fmt::print("joints {}:\n", rig.skeleton.joints.size());
    for (std::size_t i = 0; i < rig.skeleton.joints.size(); ++i) {
        fmt::print("  [{}] {} parent {}\n", i, rig.skeleton.joints[i].name, rig.skeleton.joints[i].parent);
    }
    fmt::print("clips {}:\n", rig.clips.size());
    for (const auto& c : rig.clips) fmt::print("  {} start {:.4f} dur {:.4f}\n", c.name, c.start, c.duration);

    for (const char* want : {"Landing", "Walking", "Fall_loop", "Jumping"}) {
        const int ci = rig.findClip(want);
        if (ci < 0) { fmt::print("NO CLIP {}\n", want); continue; }
        const scene::AnimationClip& clip = rig.clips[static_cast<std::size_t>(ci)];
        fmt::print("\n=== {} ===\n", clip.name);
        int lowest = -1;
        for (const auto& ch : clip.channels) {
            if (ch.path == scene::AnimationPath::Translation && !ch.times.empty()) {
                if (lowest < 0 || static_cast<int>(ch.joint) < lowest) lowest = static_cast<int>(ch.joint);
            }
        }
        fmt::print("translated-root joint index {} name {}\n", lowest,
                   lowest >= 0 ? rig.skeleton.joints[(std::size_t)lowest].name : "-");
        // every translation channel
        for (const auto& ch : clip.channels) {
            if (ch.path != scene::AnimationPath::Translation || ch.times.empty()) continue;
            const glm::vec4 a = ch.values.front();
            const glm::vec4 b = ch.values.back();
            fmt::print("  transch joint {} ({}) keys {} first ({:.4f},{:.4f},{:.4f}) last ({:.4f},{:.4f},{:.4f})\n",
                       ch.joint, rig.skeleton.joints[ch.joint].name, ch.times.size(), a.x, a.y, a.z, b.x, b.y, b.z);
        }
        const testing::ClipKeySpan span = testing::clipKeySpan(clip);
        scene::Pose pose; std::vector<glm::mat4> model;
        const int toeL = rig.skeleton.find("toes_01.l");
        const int foot = rig.skeleton.find("foot.l");
        const int hips = rig.skeleton.find("root.x");
        fmt::print("span {:.4f}..{:.4f} toeL {} foot {} rootx {}\n", span.first, span.last, toeL, foot, hips);
        for (int i = 0; i <= 8; ++i) {
            const float t = span.first + span.period() * (float)i / 8.0f;
            const glm::vec3 r = lowest >= 0 ? testing::jointPositionAt(rig, clip, lowest, t, pose, model) : glm::vec3(0);
            const glm::vec3 fo = foot >= 0 ? testing::jointPositionAt(rig, clip, foot, t, pose, model) : glm::vec3(0);
            const glm::vec3 to = toeL >= 0 ? testing::jointPositionAt(rig, clip, toeL, t, pose, model) : glm::vec3(0);
            fmt::print("  t {:.4f} root ({:+.4f},{:+.4f},{:+.4f}) foot.l ({:+.4f},{:+.4f},{:+.4f}) toe ({:+.4f},{:+.4f},{:+.4f})\n",
                       t, r.x, r.y, r.z, fo.x, fo.y, fo.z, to.x, to.y, to.z);
        }
    }
}
