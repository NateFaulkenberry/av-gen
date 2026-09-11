// Where a hero element may hang in a world, answered by the world rather than by a person nudging
// numbers (ADR-088).
//
// The scene's own queries already know how high the ground is, what grows there, and how far
// inside a hero a point is. Composition -- is it in frame, is it against the sky, is it clear of
// the subject -- is arithmetic on the camera. Between them there is no step that wants a human
// guess, and every number in this file is derived rather than chosen.

#include "entity/placement.hpp"
#include "params/modulation.hpp"
#include "params/parameter_set.hpp"
#include "scene/composition.hpp"
#include "world/camera_clearance.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <vector>

using namespace avgen;

namespace {

constexpr const char* kScene = AVGEN_SOURCE_DIR "/examples/world/glowmere-stylized.scene.json";

// The camera the scene will actually be rendered through, asked the same way the renderer asks it:
// the Camera's own view() and projection(), so "in frame" here and "in frame" there are one test.
struct Shot {
    const scene::Camera* camera = nullptr;
    float aspect = 1920.0f / 1080.0f; // the project's render size

    [[nodiscard]] glm::vec3 project(glm::vec3 p) const {
        const glm::vec4 clip = camera->projection(aspect) * camera->view() * glm::vec4(p, 1.0f);
        if (clip.w <= 0.0f) {
            return glm::vec3(0.0f, 0.0f, -1.0f);
        }
        return glm::vec3(clip.x / clip.w, clip.y / clip.w, clip.w);
    }
    [[nodiscard]] glm::vec3 eye() const { return camera->position; }
    // The half-angle a sphere of `radius` subtends, as a fraction of the half-frame height.
    [[nodiscard]] float screenRadius(glm::vec3 p, float radius) const {
        const float distance = glm::length(p - camera->position);
        if (distance <= radius) {
            return 10.0f;
        }
        return std::atan(radius / distance) / std::atan(std::tan(camera->effectiveFovY() * 0.5f));
    }
};

// The scene file's camera reaches scene_.camera through the parameter set, so a composition that
// was loaded but never attached is still looking through the default lens from the origin. Every
// question in this file is asked of the showcase camera, so it has to be the real one.
struct Showcase {
    assets::AssetRegistry registry;
    params::ParameterSet params;
    params::Modulator modulator;
    std::unique_ptr<scene::Composition> comp;

    [[nodiscard]] scene::Composition& operator*() const { return *comp; }
};

std::unique_ptr<Showcase> loadShowcase() {
    auto show = std::make_unique<Showcase>();
    show->registry.setBaseDirectory(std::filesystem::path(kScene).parent_path());
    auto loaded = scene::Composition::loadFile(kScene, show->registry);
    REQUIRE(loaded);
    show->comp = std::move(*loaded);
    show->comp->attach(show->params, show->modulator);
    show->comp->update(FrameTime{});
    return show;
}

Shot shotOf(const scene::Composition& comp) {
    return Shot{&comp.scene().camera, 1920.0f / 1080.0f};
}

const scene::CompositionNode* terrainOf(const scene::Composition& comp) {
    for (const auto& node : comp.nodes()) {
        if (node->kind == scene::NodeKind::Terrain) {
            return node.get();
        }
    }
    return nullptr;
}

world::ClearanceField fieldOf(const scene::Composition& comp, const scene::CompositionNode& terrain) {
    world::ClearanceField field;
    field.map = &terrain.worldMap;
    field.ecology = &terrain.ecology;
    field.heroes = comp.heroes();
    return field;
}

// Whether the straight line from the eye to `p` clears the ground all the way. A craft that is
// high above its own patch of ground but behind a ridge is not silhouetted against anything.
bool skylined(const world::WorldMap& map, glm::vec3 eye, glm::vec3 p, float margin) {
    constexpr int kSteps = 64;
    for (int i = 1; i < kSteps; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(kSteps);
        const glm::vec3 s = eye + (p - eye) * t;
        if (s.y < map.height(glm::vec2(s.x, s.z)) + margin) {
            return false;
        }
    }
    return true;
}

} // namespace

// The craft the scene actually places. Read from the file rather than restated here, so this test
// checks the scene instead of agreeing with itself.
namespace {
struct Placed {
    glm::vec3 position{0.0f};
    float radius = 0.0f;
    float halfHeight = 0.0f;
    float underside = 0.0f; // world y of the lowest point
};

Placed placedCraft(const scene::Composition& comp, const std::string& node) {
    const scene::CompositionNode* n = comp.findNode(node);
    REQUIRE(n != nullptr);
    Placed out;
    out.position = comp.nodeWorldTransform(*n).position;
    // The asset's own extents, scaled by what the scene asked for. ufo.gltf is authored Z-up and
    // carries a x100 node matrix: 2.3035 radius and -0.4182..1.6111 in Z become, after the
    // importer's Z-up to Y-up conversion, 230.35 of radius and -41.82..161.11 of height.
    const glm::vec3 scale = n->procedural.sourceTransform.scale * n->transform.scale;
    out.radius = 230.35f * scale.x;
    out.underside = out.position.y - 41.82f * scale.y;
    out.halfHeight = (161.11f + 41.82f) * 0.5f * scale.y;
    return out;
}
} // namespace

TEST_CASE("the craft hangs where the world says it may", "[entity][placement]") {
    auto show = loadShowcase();
    scene::Composition& comp = **show;

    const scene::CompositionNode* terrain = terrainOf(comp);
    REQUIRE(terrain != nullptr);
    const world::ClearanceField field = fieldOf(comp, *terrain);
    const Shot shot = shotOf(comp);
    const Placed craft = placedCraft(comp, "visitor");
    const glm::vec2 flat(craft.position.x, craft.position.z);

    SECTION("it is clear of the ground and of everything that grows under it") {
        // Not merely above the terrain: above the tallest thing the ecology says could grow there,
        // by the craft's own half-height plus room for the beam to exist as something other than a
        // stripe painted on a canopy.
        const float floorY = field.minimumHeight(flat);
        CHECK(craft.underside > floorY);
        const float ground = terrain->worldMap.height(flat);
        const float canopy = field.canopyHeight(flat);
        INFO("underside " << craft.underside << ", ground " << ground << ", canopy " << canopy);
        CHECK(craft.underside - (ground + canopy) > 8.0f);
    }

    SECTION("it is inside no other hero") {
        // heroPenetration is a capsule test over each hero's declared radius and height -- the same
        // test a directed camera is kept out of them by, so "does not intersect" here means the
        // same thing it means there. The craft is itself a hero, so it is left out of its own
        // test; every other one has to be clear.
        std::vector<world::HeroPoint> others;
        for (const world::HeroPoint& hero : comp.heroes()) {
            if (hero.name != "visitor") {
                others.push_back(hero);
            }
        }
        REQUIRE(others.size() == comp.heroes().size() - 1);
        world::ClearanceField wide = field;
        wide.heroes = others;
        wide.cameraRadius = craft.radius;
        CHECK(wide.heroPenetration(craft.position) == 0.0f);
        // And the whole craft, not only its centre: the rim at eight bearings, top and bottom.
        world::ClearanceField point = wide;
        point.cameraRadius = 0.0f;
        for (int i = 0; i < 8; ++i) {
            const float a = static_cast<float>(i) * 0.7853981634f;
            const glm::vec3 rim = craft.position + glm::vec3(std::cos(a), 0.0f, std::sin(a)) * craft.radius;
            CHECK(point.heroPenetration(rim) == 0.0f);
            CHECK(point.heroPenetration(rim + glm::vec3(0.0f, craft.halfHeight, 0.0f)) == 0.0f);
            CHECK(point.heroPenetration(rim - glm::vec3(0.0f, craft.halfHeight, 0.0f)) == 0.0f);
        }
    }

    SECTION("it is in frame, and it is not on top of the subject") {
        const glm::vec3 ndc = shot.project(craft.position);
        INFO("ndc " << ndc.x << ", " << ndc.y << " at " << ndc.z << " m");
        CHECK(ndc.z > 0.0f);
        const float r = shot.screenRadius(craft.position, craft.radius);
        // Wholly inside the frame with a margin, so no part of it is cut by an edge.
        CHECK(std::abs(ndc.x) + r * (1.0f / shot.aspect) < 0.92f);
        CHECK(std::abs(ndc.y) + r < 0.92f);
        // Big enough to read as an object rather than a speck: at least 3% of the frame height.
        CHECK(r > 0.03f);
        // ... and not so big it takes the frame off the elder, which is what this shot is about.
        CHECK(r < 0.20f);

        // Clear of the elder in the frame. The elder's crown is the scene's declared focal point.
        const world::HeroPoint* elder = nullptr;
        for (const world::HeroPoint& hero : comp.heroes()) {
            if (hero.name == "elder") {
                elder = &hero;
            }
        }
        REQUIRE(elder != nullptr);
        const glm::vec3 crown = elder->position + glm::vec3(0.0f, elder->height * 0.75f, 0.0f);
        const glm::vec3 elderNdc = shot.project(crown);
        const float elderR = shot.screenRadius(crown, elder->radius);
        const glm::vec2 separation((ndc.x - elderNdc.x) * shot.aspect, ndc.y - elderNdc.y);
        INFO("elder ndc " << elderNdc.x << ", " << elderNdc.y << " r " << elderR << "; craft r " << r);
        CHECK(glm::length(separation) > elderR + r);
    }

    SECTION("it is silhouetted against the sky") {
        // Every point on the line of sight is above the ground, so nothing in the valley is behind
        // it; and it is above the canopy of everywhere between, so no tree is either.
        CHECK(skylined(terrain->worldMap, shot.eye(), craft.position, 1.0f));
        constexpr int kSteps = 48;
        float highestBehind = -1e9f;
        for (int i = 1; i <= kSteps; ++i) {
            const float t = static_cast<float>(i) / static_cast<float>(kSteps);
            // Carry on past the craft to the far edge of the world: what is *behind* it is what it
            // would be read against.
            const glm::vec3 s = shot.eye() + (craft.position - shot.eye()) * (1.0f + t * 2.5f);
            const glm::vec2 p(s.x, s.z);
            if (std::abs(p.x) > 320.0f || std::abs(p.y) > 320.0f) {
                break;
            }
            const glm::vec3 ndc = shot.project(s);
            if (ndc.z <= 0.0f) {
                continue;
            }
            highestBehind = std::max(highestBehind,
                                     terrain->worldMap.height(p) + field.canopyHeight(p) - s.y);
        }
        INFO("highest thing behind the craft, relative to the line of sight: " << highestBehind);
        CHECK(highestBehind < 0.0f);
    }

    SECTION("there is a column under it for a beam") {
        // The beam reaches the ground, so the ground under it has to be ground: open, not a
        // hillside and not the inside of a tree.
        const world::Sample s = terrain->worldMap.sample(flat, 0.5f);
        CHECK(s.slope < 0.5f);
        CHECK(field.canopyHeight(flat) < 6.0f);
        // And the ground it lands on is not under water, which would read as a beam into a puddle.
        CHECK_FALSE(s.submerged);
    }
}

// A hidden driver: runs the placement search against the showcase camera and prints what it found,
// so a placement can be derived once and committed rather than nudged.
//   build/release/tests/avgen_tests "[.placement-search]"
TEST_CASE("placement search", "[.placement-search]") {
    auto show = loadShowcase();
    scene::Composition& comp = **show;
    const scene::CompositionNode* terrain = terrainOf(comp);
    REQUIRE(terrain != nullptr);
    const world::ClearanceField field = fieldOf(comp, *terrain);
    const Shot shot = shotOf(comp);

    std::printf("camera eye %.2f %.2f %.2f target %.2f %.2f %.2f fovY %.2f deg\n", shot.eye().x,
                shot.eye().y, shot.eye().z, comp.scene().camera.target.x, comp.scene().camera.target.y,
                comp.scene().camera.target.z, glm::degrees(comp.scene().camera.effectiveFovY()));
    for (const world::HeroPoint& hero : comp.heroes()) {
        const glm::vec3 centre = hero.position + glm::vec3(0.0f, hero.height * 0.5f, 0.0f);
        const glm::vec3 ndc = shot.project(centre);
        std::printf("hero %-13s ndc %+.3f %+.3f  r %.3f  d %.1f\n", hero.name.c_str(), ndc.x, ndc.y,
                    shot.screenRadius(centre, std::max(hero.radius, hero.height * 0.5f)), ndc.z);
    }

    for (const float diameter : {16.0f}) {
        for (const glm::vec2 want : {glm::vec2(0.13f, 0.50f), glm::vec2(0.13f, 0.56f),
                                     glm::vec2(0.13f, 0.62f), glm::vec2(0.13f, 0.68f),
                                     glm::vec2(0.20f, 0.62f), glm::vec2(0.06f, 0.62f)}) {
            entity::PlacementBrief brief;
            brief.camera = &comp.scene().camera;
            brief.aspect = 1920.0f / 1080.0f;
            brief.screenTarget = want;
            brief.screenSpread = 0.06f;
            brief.maxGroundSlope = 0.70f;
            brief.radius = diameter * 0.5f;
            brief.halfHeight = diameter * (202.93f / 460.70f) * 0.5f;
            brief.clearanceBelow = 14.0f;
            brief.map = &terrain->worldMap;
            brief.field = &field;
            brief.heroes = comp.heroes();
            brief.minScreenRadius = 0.07f;
            brief.maxScreenRadius = 0.30f;
            brief.minDistance = 55.0f;
            brief.maxDistance = 230.0f;
            brief.seed = 20260911u;
            brief.samples = 120000;
            const entity::PlacementResult r = entity::findPlacement(brief);
            if (!r.found) {
                std::printf("d=%4.1f want %+.2f %+.2f : nothing (%s)\n", diameter, want.x, want.y,
                            r.rejects.summary().c_str());
                continue;
            }
            std::printf("d=%4.1f want %+.2f %+.2f : pos %8.2f %8.2f %8.2f  ndc %+.3f %+.3f  r %.3f"
                        "  dist %6.1f  ground %7.2f  canopy %5.2f  head %6.2f  score %.3f\n",
                        diameter, want.x, want.y, r.position.x, r.position.y, r.position.z, r.ndc.x,
                        r.ndc.y, r.screenRadius, r.distance, r.groundBelow, r.canopyBelow, r.headroom,
                        r.score);
        }
    }
}
