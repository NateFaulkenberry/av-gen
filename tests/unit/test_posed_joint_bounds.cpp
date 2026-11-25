// ADR-2136: a skinned mesh's cull box from per-joint boxes, not from skinning every vertex.
//
// The one property that matters is that the box is never smaller than the geometry the frame draws: a box that
// misses a vertex culls a limb at the screen edge. So the cheap box is checked against the exact per-vertex skin
// (`scene::skinnedVertexBounds`, the arithmetic the skinning shader does) over every clip of real rigs -- the
// Glowmere/Neon Dreams aliens, the farm animals, the astronaut -- plus random poses far outside anything a clip
// plays, and the looseness is measured and bounded so a later change cannot quietly trade it all away.

#include "assets/gltf_loader.hpp"
#include "scene/animation.hpp"
#include "scene/scene.hpp"
#include "scene/skeleton.hpp"

#include <catch2/catch_test_macros.hpp>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <random>
#include <string>
#include <vector>

using namespace avgen;

namespace {

std::filesystem::path sourceDir() { return std::filesystem::path(AVGEN_SOURCE_DIR); }

struct Looseness {
    std::size_t boxes = 0;
    double sumVolumeRatio = 0.0; // cheap volume / exact volume, for the mean
    double worstVolumeRatio = 1.0;
    double sumSlack = 0.0;       // the most the box overreaches on one side of one axis, as a fraction of the
    double worstSlack = 0.0;     // object's largest exact extent -- how far past the object a cull test reaches
};

// Every vertex of every skinned mesh of the scene must lie in the cheap box -- for the box to be *never smaller*,
// the exact box (computed independently, by the shader's arithmetic) must be inside it, per axis, with no
// tolerance at all.
void checkPose(scene::Scene& s, scene::RigId rig, Looseness& loose) {
    for (scene::MeshId m = 0; m < s.meshes.size(); ++m) {
        const scene::MeshData& mesh = s.meshes[m];
        if (!mesh.skinned()) {
            continue;
        }
        const auto exact = scene::skinnedVertexBounds(mesh, s.rigs[rig].palette);
        const auto cheap = s.posedMeshBounds(m, rig);
        REQUIRE(exact.has_value() == cheap.has_value());
        if (!exact) {
            continue;
        }
        INFO("mesh " << mesh.name << " exact " << exact->min.x << "," << exact->min.y << "," << exact->min.z
                     << " .. " << exact->max.x << "," << exact->max.y << "," << exact->max.z << "; cheap "
                     << cheap->min.x << "," << cheap->min.y << "," << cheap->min.z << " .. " << cheap->max.x << ","
                     << cheap->max.y << "," << cheap->max.z);
        REQUIRE(cheap->min.x <= exact->min.x);
        REQUIRE(cheap->min.y <= exact->min.y);
        REQUIRE(cheap->min.z <= exact->min.z);
        REQUIRE(cheap->max.x >= exact->max.x);
        REQUIRE(cheap->max.y >= exact->max.y);
        REQUIRE(cheap->max.z >= exact->max.z);
        const glm::dvec3 e = glm::dvec3(exact->max - exact->min);
        const glm::dvec3 c = glm::dvec3(cheap->max - cheap->min);
        const double size = std::max({e.x, e.y, e.z, 1e-6});
        const glm::dvec3 floor(1e-2 * size); // a flat card has a zero-width axis
        const glm::dvec3 ratio = (c + floor) / (e + floor);
        const double volume = ratio.x * ratio.y * ratio.z;
        loose.sumVolumeRatio += volume;
        loose.worstVolumeRatio = std::max(loose.worstVolumeRatio, volume);
        const glm::dvec3 under = glm::dvec3(exact->min - cheap->min);
        const glm::dvec3 over = glm::dvec3(cheap->max - exact->max);
        const double slack = std::max({under.x, under.y, under.z, over.x, over.y, over.z}) / size;
        loose.sumSlack += slack;
        loose.worstSlack = std::max(loose.worstSlack, slack);
        ++loose.boxes;
    }
}

// Every clip, sampled on a grid, then random poses: each joint turned up to `radians` about a random axis on top
// of the rest pose, and a random translation -- poses no clip plays, which is where a box built from the clips
// would be caught out.
struct Sweep {
    Looseness clips;
    Looseness random;
};

Sweep sweep(const std::filesystem::path& file, int samplesPerClip, int randomPoses) {
    scene::Scene s;
    const auto summary = assets::loadGltf(file, s, {.loadImages = false});
    REQUIRE(summary.has_value());
    REQUIRE(!s.rigs.empty());
    Sweep out;
    std::mt19937 rng(2136);
    std::uniform_real_distribution<float> unit(-1.0f, 1.0f);
    for (scene::RigId r = 0; r < s.rigs.size(); ++r) {
        scene::SkinnedRig& rig = s.rigs[r];
        scene::Pose pose;
        std::vector<glm::mat4> scratch;
        for (const scene::AnimationClip& clip : rig.clips) {
            for (int i = 0; i < samplesPerClip; ++i) {
                scene::setRestPose(rig.skeleton, pose);
                scene::sampleClip(clip, clip.duration * static_cast<float>(i) / static_cast<float>(samplesPerClip),
                                  pose);
                scene::skinningPalette(rig.skeleton, pose, scratch, rig.palette);
                checkPose(s, r, out.clips);
            }
        }
        for (int i = 0; i < randomPoses; ++i) {
            scene::setRestPose(rig.skeleton, pose);
            for (scene::Transform& t : pose.local) {
                const glm::vec3 axis = glm::normalize(glm::vec3(unit(rng), unit(rng), unit(rng)) + glm::vec3(1e-3f));
                t.rotation = glm::normalize(t.rotation * glm::angleAxis(1.2f * unit(rng), axis));
            }
            pose.local.front().position += glm::vec3(unit(rng), unit(rng), unit(rng)) * 3.0f;
            scene::skinningPalette(rig.skeleton, pose, scratch, rig.palette);
            checkPose(s, r, out.random);
        }
    }
    return out;
}

} // namespace

TEST_CASE("the cheap posed box contains the exact skinned box of real rigs over many poses",
          "[scene][skeleton][culling][adr2136]") {
#ifndef AVGEN_SOURCE_DIR
    SKIP("AVGEN_SOURCE_DIR not defined");
#else
    // The Glowmere alien (imported), the Neon Dreams variants (90 joints, 2-4 meshes), farm animals of three sizes,
    // and a musician. All gitignored, commercial assets: a checkout without them skips the file, not the case.
    const std::vector<std::filesystem::path> files{
        sourceDir() / "assets" / "imported" / "alien.gltf",     sourceDir() / "assets" / "aliens" / "alien-pilot-nd.glb",
        sourceDir() / "assets" / "aliens" / "alien-scout-nd.glb", sourceDir() / "assets" / "aliens" / "alien-elder.glb",
        sourceDir() / "assets" / "farm" / "cow.glb",            sourceDir() / "assets" / "farm" / "horse.glb",
        sourceDir() / "assets" / "farm" / "chick.glb",          sourceDir() / "assets" / "musicians" / "astronaut_drums.glb",
    };
    std::size_t present = 0;
    for (const auto& file : files) {
        if (!std::filesystem::exists(file)) {
            WARN("not present, skipped: " << file.string());
            continue;
        }
        ++present;
        INFO(file.filename().string());
        const Sweep result = sweep(file, 24, 40);
        REQUIRE(result.clips.boxes > 0);
        REQUIRE(result.random.boxes > 0);
        const auto line = [](const char* what, const Looseness& l) {
            const double n = static_cast<double>(l.boxes);
            return std::string(what) + ": " + std::to_string(l.boxes) + " boxes, volume mean x" +
                   std::to_string(l.sumVolumeRatio / n) + " worst x" + std::to_string(l.worstVolumeRatio) +
                   ", overreach mean " + std::to_string(100.0 * l.sumSlack / n) + "% worst " +
                   std::to_string(100.0 * l.worstSlack) + "% of the object's size";
        };
        WARN(file.filename().string() << "\n  " << line("clips", result.clips) << "\n  "
                                      << line("random", result.random));
        // Loose, but not so loose that the cull stops culling (the numbers are in ADR-2136). A ceiling a little
        // above what was measured, so a change that gives the tightness away is caught.
        CHECK(result.clips.sumSlack / static_cast<double>(result.clips.boxes) < 0.25);
        CHECK(result.clips.worstSlack < 0.6);
    }
    if (present == 0) {
        SKIP("no rigged assets present");
    }
#endif
}

// The vertex scan happens once per mesh, not once per pose: the per-joint boxes are built on the first ask and
// reused by every later pose and by every rig that draws the mesh (a crowd shares its meshes). A mesh edit
// rebuilds them.
TEST_CASE("the per-joint boxes are built once per mesh, whatever the poses and rigs", "[scene][skeleton][culling][adr2136]") {
#ifndef AVGEN_SOURCE_DIR
    SKIP("AVGEN_SOURCE_DIR not defined");
#else
    const std::filesystem::path file = sourceDir() / "assets" / "farm" / "horse.glb";
    if (!std::filesystem::exists(file)) {
        SKIP("assets/farm is not present");
    }
    scene::Scene s;
    REQUIRE(assets::loadGltf(file, s, {.loadImages = false}).has_value());
    REQUIRE(s.rigs.size() == 1);
    REQUIRE(s.meshes.size() == 1);
    s.rigs.push_back(s.rigs.front()); // a second rig drawing the same mesh
    const std::uint64_t builds = s.jointBoxBuilds();
    const std::uint64_t computes = s.posedBoundsComputes();
    scene::Pose pose;
    std::vector<glm::mat4> scratch;
    const scene::AnimationClip& clip = s.rigs.front().clips.front();
    for (int i = 0; i < 30; ++i) {
        for (scene::RigId r = 0; r < 2; ++r) {
            scene::setRestPose(s.rigs[r].skeleton, pose);
            scene::sampleClip(clip, clip.duration * static_cast<float>(i + r) / 31.0f, pose);
            scene::skinningPalette(s.rigs[r].skeleton, pose, scratch, s.rigs[r].palette);
            REQUIRE(s.posedMeshBounds(0, r).has_value());
        }
    }
    CHECK(s.jointBoxBuilds() == builds + 1);
    CHECK(s.posedBoundsComputes() == computes + 60); // every pose is new, so every ask is a compute

    ++s.meshVersion; // an edit: the boxes are rebuilt, once
    REQUIRE(s.posedMeshBounds(0, 0).has_value());
    REQUIRE(s.posedMeshBounds(0, 1).has_value());
    CHECK(s.jointBoxBuilds() == builds + 2);
#endif
}
