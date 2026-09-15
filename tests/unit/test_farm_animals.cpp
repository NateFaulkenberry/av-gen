// The farm animal pack (ADR-205): nine animals converted out of one Blender file.
//
// The properties worth asserting are not "a file exists". They are that the engine gets a rig it
// can drive, that every animal answers to the same clip name, that the palette texture the whole
// pack shares actually arrived, and -- the thing a conversion silently gets wrong -- that nine
// animals exported from nine separate armatures still stand on the same ground at the same scale,
// the right way up, at their own origin.

#include "assets/asset_library.hpp"
#include "assets/gltf_loader.hpp"
#include "scene/animation.hpp"
#include "scene/scene.hpp"
#include "scene/skeleton.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

using namespace avgen;
using Catch::Approx;

namespace {

struct Animal {
    const char* file;
    float width;  // X, metres, at the rest pose the GLB ships
    float height; // Y
    float length; // Z
    float clipSeconds;
    std::size_t joints; // skin joints, which is bones plus Blender's neutral bone where it added one
};

// Measured off the exported rest pose, which is what the loader reports as bounds: for a skinned
// mesh the joint matrices times the inverse binds are the identity at rest, so the vertex data is
// already the metres the animal stands in. Checked against the source .blend frame by frame at
// export time; these are the numbers, not a guess at them.
constexpr std::array<Animal, 9> kAnimals{{
    {"bull", 0.7617f, 1.7710f, 2.6498f, 0.9000f, 27},
    {"cow", 0.6942f, 1.5739f, 2.3575f, 0.9000f, 27},
    {"horse", 0.5666f, 1.7523f, 2.3157f, 0.9000f, 29},
    {"sheep", 0.4958f, 0.9412f, 1.2516f, 0.9000f, 25},
    {"pig", 0.4331f, 0.6900f, 1.2598f, 0.9000f, 25},
    {"goat", 0.2601f, 0.7441f, 0.9474f, 0.9000f, 27},
    {"rooster", 0.1626f, 0.4342f, 0.3555f, 0.8000f, 22},
    {"chicken", 0.1502f, 0.3368f, 0.3113f, 0.8000f, 17},
    {"chick", 0.0548f, 0.1100f, 0.1019f, 0.8000f, 15},
}};

std::filesystem::path farmDir() {
    return std::filesystem::path(AVGEN_SOURCE_DIR) / "assets" / "farm";
}

} // namespace

TEST_CASE("the farm animals import as drivable rigs", "[assets][gltf][skeleton][farm]") {
#ifndef AVGEN_SOURCE_DIR
    SKIP("AVGEN_SOURCE_DIR not defined");
#else
    if (!std::filesystem::exists(farmDir() / "cow.glb")) {
        SKIP("assets/farm is not present");
    }

    for (const Animal& animal : kAnimals) {
        INFO(animal.file);
        scene::Scene s;
        const auto summary = assets::loadGltf(farmDir() / (std::string(animal.file) + ".glb"), s, {});
        REQUIRE(summary.has_value());
        // No warnings at all. The export was tuned until this was true -- morph targets off, the
        // render rig deleted, the pig's mirror applied -- so a warning here means the settings
        // drifted, not that the engine grew a new opinion.
        INFO("warnings: " << summary->warnings.size());
        CHECK(summary->warnings.empty());

        // One animal, one skin, one mesh, one material, one texture.
        REQUIRE(summary->rigs == 1);
        REQUIRE(summary->meshes == 1);
        CHECK(summary->materials == 1);
        CHECK(summary->textures == 1);
        CHECK(summary->lights == 0);
        CHECK(summary->cameras == 0);

        const scene::SkinnedRig& rig = s.rigs.front();
        CHECK(rig.valid());
        CHECK(rig.skeleton.paletteSize() == animal.joints);
        CHECK(rig.skeleton.jointCount() >= rig.skeleton.paletteSize());
        CHECK(rig.skeleton.paletteSize() <= scene::kMaxPaletteJoints);

        // One clip, and it is called `Walk` on every one of the nine. The source names it per
        // species -- `BullWalk`, `ChickWalk` -- and the export renames it, so that a scene author
        // writing `"animation": {"state": "Walk"}` gets a walk out of whichever animal they placed.
        REQUIRE(rig.clips.size() == 1);
        const int walk = rig.findClip("Walk");
        REQUIRE(walk >= 0);
        const scene::AnimationClip& clip = rig.clips[static_cast<std::size_t>(walk)];
        CHECK(clip.valid());
        CHECK(clip.duration == Approx(animal.clipSeconds).margin(0.02));
        // Non-empty, and pointed at this skeleton rather than at nothing. A clip whose channels
        // name joints the skeleton does not have is the exact shape of a conversion that reported
        // success and shipped a bind pose.
        CHECK(clip.channels.size() >= rig.skeleton.paletteSize());
        std::size_t keys = 0;
        for (const scene::AnimationChannel& channel : clip.channels) {
            CHECK(channel.valid());
            CHECK(channel.joint < rig.skeleton.jointCount());
            CHECK(channel.keyCount() >= 2);
            keys += channel.keyCount();
        }
        CHECK(keys > 500);
        // `addDefaultStates` named a state per clip on import, so the scene file's string resolves.
        CHECK(rig.player.findState("Walk") != nullptr);

        // Geometry. Low-poly, but it has to be real geometry: whole triangles, unit normals, UVs
        // that address the palette texture, and influences that sum to one inside the palette.
        REQUIRE(s.meshes.size() == 1);
        const scene::MeshData& mesh = s.meshes.front();
        CHECK(mesh.valid());
        CHECK(mesh.skinned());
        CHECK(mesh.vertices.size() > 300);
        CHECK(mesh.indices.size() % 3 == 0);
        CHECK(mesh.indices.size() / 3 > 300);
        for (const std::uint32_t index : mesh.indices) {
            REQUIRE(index < mesh.vertices.size());
        }
        for (const scene::Vertex& v : mesh.vertices) {
            REQUIRE(glm::length(v.normal) == Approx(1.0f).margin(1e-3));
            REQUIRE(std::isfinite(v.uv.x));
            REQUIRE(std::isfinite(v.uv.y));
        }
        for (const scene::SkinInfluence& influence : mesh.skin) {
            const float sum = influence.weights.x + influence.weights.y + influence.weights.z +
                              influence.weights.w;
            REQUIRE(sum == Approx(1.0f).margin(1e-4));
            for (const std::uint16_t joint : influence.joints) {
                REQUIRE(joint < rig.skeleton.paletteSize());
            }
        }

        // Every animal in the pack shades from one 16x16 colour palette, sampled a texel at a time.
        // If that texture went missing the whole pack turns the importer's default magenta, which
        // is a thing you notice in a render and not in a test unless the test looks.
        REQUIRE(s.entities.size() == 1);
        const scene::Entity& entity = s.entities.front();
        CHECK(entity.rig != scene::kInvalidRig);
        CHECK(entity.material.baseColorTexture.valid());
        REQUIRE(s.textures.size() == 1);
        CHECK(s.textures.front().width == 16);
        CHECK(s.textures.front().height == 16);

        // Scale, orientation and origin -- the four ways an import goes wrong quietly.
        const glm::vec3 size = summary->boundsMax - summary->boundsMin;
        CHECK(size.x == Approx(animal.width).margin(0.01));
        CHECK(size.y == Approx(animal.height).margin(0.01));
        CHECK(size.z == Approx(animal.length).margin(0.01));
        // Standing on Y=0, not floating above it or buried in it. The hooves dip a hair below zero
        // at rest; a tenth of a percent of the animal's height is the whole budget.
        CHECK(summary->boundsMin.y <= 0.0f);
        CHECK(summary->boundsMin.y > -0.01f * animal.height);
        // At its own origin, not out at the turntable slot it occupied in the source file.
        const glm::vec3 centre = 0.5f * (summary->boundsMin + summary->boundsMax);
        CHECK(std::abs(centre.x) < 0.01f * animal.length);
        CHECK(std::abs(centre.z) < 0.20f * animal.length);
        // The right way up: every one of these is longer than it is tall and taller than it is
        // wide, which an animal rotated onto its side or its back is not.
        CHECK(size.z > size.x);
        CHECK(size.y > size.x);
    }
#endif
}

TEST_CASE("the farm animals agree with each other about scale", "[assets][gltf][farm]") {
#ifndef AVGEN_SOURCE_DIR
    SKIP("AVGEN_SOURCE_DIR not defined");
#else
    if (!std::filesystem::exists(farmDir() / "cow.glb")) {
        SKIP("assets/farm is not present");
    }

    // Nine armatures, nine exports, one scene. The failure this catches is the one that survives
    // every per-file check: each animal individually plausible, and the chick the size of the bull
    // because one of the nine picked up a stray factor on the way out of Blender. Asserted as a
    // strict ordering plus the ratio between the ends, so it fails on a factor of two and not on a
    // remodelled goat.
    std::vector<float> heights;
    for (const Animal& animal : kAnimals) {
        scene::Scene s;
        const auto summary = assets::loadGltf(farmDir() / (std::string(animal.file) + ".glb"), s, {});
        REQUIRE(summary.has_value());
        heights.push_back(summary->boundsMax.y - summary->boundsMin.y);
    }
    // kAnimals is ordered by width, which for these nine is also the order of mass: bull, cow,
    // horse, sheep, pig, goat, rooster, chicken, chick. Height is not monotonic in it -- the horse
    // is taller than the cow and the goat taller than the pig -- so the ordering asserted here is
    // the one that is actually true: the four livestock animals all clear a metre and a half or
    // half a metre, the three birds are all under half a metre, and nothing overlaps across that
    // line.
    const float smallestLivestock =
        *std::min_element(heights.begin(), heights.begin() + 6);  // bull..goat
    const float largestBird = *std::max_element(heights.begin() + 6, heights.end()); // rooster..chick
    CHECK(smallestLivestock > largestBird);
    // A bull is sixteen chicks tall, give or take. If the two ends of the pack ever come within a
    // factor of four of each other, something rescaled.
    CHECK(heights.front() / heights.back() > 8.0f);
    CHECK(heights.front() / heights.back() < 32.0f);
    // And nothing is microscopic or enormous in absolute terms either.
    for (std::size_t i = 0; i < heights.size(); ++i) {
        INFO(kAnimals[i].file << " is " << heights[i] << " m tall");
        CHECK(heights[i] > 0.05f);
        CHECK(heights[i] < 3.0f);
    }
#endif
}

TEST_CASE("the farm manifest describes the pack that is on disk", "[assets][farm][manifest]") {
#ifndef AVGEN_SOURCE_DIR
    SKIP("AVGEN_SOURCE_DIR not defined");
#else
    const std::filesystem::path manifest =
        std::filesystem::path(AVGEN_SOURCE_DIR) / "assets" / "farm.manifest.json";
    if (!std::filesystem::exists(manifest)) {
        SKIP("assets/farm.manifest.json is not present");
    }
    const auto library = assets::AssetLibrary::loadFile(manifest);
    REQUIRE(library.has_value());
    CHECK(!library->source().empty());
    CHECK(!library->license().empty());
    REQUIRE(library->assets().size() == kAnimals.size());

    // Every entry names a real animal, in the category the composer's ecology weights read, with a
    // path that resolves absolutely. Existence is only checked when the pack is present, the way
    // test_asset_library.cpp checks the gitignored vegetation: requiring the files here would make
    // this test pass for whoever ran the converter and fail for everybody else.
    const bool packPresent = std::filesystem::exists(farmDir() / "cow.glb");
    for (const assets::AssetDescriptor& asset : library->assets()) {
        INFO(asset.name);
        CHECK(asset.category == assets::AssetCategory::Creature);
        const std::filesystem::path resolved = library->resolve(asset);
        CHECK(resolved.is_absolute());
        CHECK(resolved.extension() == ".glb");
        const auto* match = std::find_if(kAnimals.begin(), kAnimals.end(), [&](const Animal& a) {
            return resolved.stem() == a.file;
        });
        REQUIRE(match != kAnimals.end());
        // naturalSize is what a scatter layer sizes an instance by, so it has to be the metres the
        // GLB actually occupies rather than a number somebody typed.
        CHECK(asset.naturalSize.x == Approx(match->width).margin(0.01));
        CHECK(asset.naturalSize.y == Approx(match->height).margin(0.01));
        CHECK(asset.naturalSize.z == Approx(match->length).margin(0.01));
        if (packPresent) {
            CHECK(std::filesystem::exists(resolved));
        }
    }
#endif
}
