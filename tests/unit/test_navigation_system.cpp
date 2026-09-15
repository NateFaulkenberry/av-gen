// The navigation stack ADR-093 added: per-instance obstacles, a navigation graph, and a body that
// stays on the ground.
//
// These are the parts that can be pinned without a world. The integration test next door
// (`test_world_navigation.cpp`) measures the same things on Glowmere itself, which is what proves
// they were wired in; this is what says what they are supposed to do.

#include "entity/entity.hpp"
#include "entity/grounding.hpp"
#include "entity/nav_grid.hpp"
#include "entity/navigation.hpp"
#include "entity/obstacles.hpp"
#include "spatial/obstacle_field.hpp"
#include "params/parameter_set.hpp"

#include <array>
#include "world/world_map.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <nlohmann/json.hpp>

#include <cmath>
#include <memory>
#include <string>
#include <vector>

using namespace avgen;

namespace {

spatial::NavigationObstacle solid(float x, float z, float radius, float height,
                                  spatial::ObstacleType type = spatial::ObstacleType::Rock) {
    spatial::NavigationObstacle o;
    o.center = glm::vec2(x, z);
    o.radius = radius;
    o.base = 0.0f;
    o.height = height;
    o.type = type;
    return o;
}

} // namespace

// ---- the obstacle field ------------------------------------------------------------------------

TEST_CASE("an obstacle field answers where the solids are", "[navigation][obstacles]") {
    spatial::ObstacleField field;
    field.add(solid(0.0f, 0.0f, 1.0f, 3.0f));
    field.add(solid(20.0f, 0.0f, 2.0f, 4.0f));
    field.add(solid(-14.0f, 9.0f, 0.5f, 12.0f, spatial::ObstacleType::Trunk));
    field.build();
    REQUIRE(field.built());
    CHECK(field.size() == 3);
    CHECK(field.blockingCount() == 3);

    SECTION("isOccupied is the plain disc test the shared query surface forwards to") {
        CHECK(field.isOccupied(0.0f, 0.0f, 0.0f));
        CHECK(field.isOccupied(1.4f, 0.0f, 0.5f));   // rims overlap
        CHECK_FALSE(field.isOccupied(1.6f, 0.0f, 0.5f)); // and here they do not
        CHECK_FALSE(field.isOccupied(200.0f, 200.0f, 1.0f));
    }

    SECTION("an unindexed field gives the same answers, more slowly") {
        // The fallback matters: `add` invalidates the index, and a spatial structure that silently
        // answered from a stale one would be wrong in a way nothing checks.
        spatial::ObstacleField loose;
        loose.add(solid(0.0f, 0.0f, 1.0f, 3.0f));
        REQUIRE_FALSE(loose.built());
        CHECK(loose.isOccupied(0.5f, 0.0f, 0.1f));
        CHECK_FALSE(loose.isOccupied(5.0f, 0.0f, 0.1f));
    }

    SECTION("a walker steps over what is short and walks round what is not") {
        spatial::ObstacleField low;
        low.add(solid(0.0f, 0.0f, 1.0f, 0.2f)); // a kerb
        low.build();
        spatial::ObstacleFilter walker;
        walker.bodyRadius = 0.45f;
        walker.stepOver = 0.4f;
        spatial::ObstacleHit hit;
        CHECK_FALSE(low.blocker(glm::vec2(0.0f), walker, hit));
        // isOccupied knows nothing about a walker's legs, and should not: it is a plan-view query.
        CHECK(low.isOccupied(0.0f, 0.0f, 0.1f));
    }

    SECTION("a segment test catches what a sampled one walks through") {
        // The defect the whole representation exists for. A half-metre trunk between two samples of
        // a path is invisible to a point test every couple of metres.
        spatial::ObstacleField trunks;
        trunks.add(solid(5.0f, 0.0f, 0.5f, 14.0f, spatial::ObstacleType::Trunk));
        trunks.build();
        spatial::ObstacleFilter walker;
        CHECK(trunks.segmentBlocked(glm::vec2(0.0f, 0.0f), glm::vec2(10.0f, 0.0f), walker));
        CHECK_FALSE(trunks.segmentBlocked(glm::vec2(0.0f, 4.0f), glm::vec2(10.0f, 4.0f), walker));
        spatial::ObstacleHit hit;
        REQUIRE(trunks.segmentHit(glm::vec2(0.0f, 0.0f), glm::vec2(10.0f, 0.0f), walker, hit));
        CHECK(hit.index == 0);
    }

    SECTION("clearance reports distance, not merely blockage") {
        spatial::ObstacleFilter walker;
        walker.bodyRadius = 0.5f;
        CHECK_THAT(field.clearance(glm::vec2(6.0f, 0.0f), walker, 20.0f),
                   Catch::Matchers::WithinAbs(4.5, 1e-4)); // 6 - 1 (radius) - 0.5 (body)
        CHECK(field.clearance(glm::vec2(0.0f, 0.0f), walker, 20.0f) < 0.0f); // inside
    }

    SECTION("resolve pushes a body out of what it is inside, and nowhere otherwise") {
        spatial::ObstacleFilter walker;
        walker.bodyRadius = 0.5f;
        const glm::vec2 push = field.resolve(glm::vec2(0.8f, 0.0f), walker);
        CHECK(push.x > 0.0f);
        CHECK_THAT(glm::length(push), Catch::Matchers::WithinAbs(0.7, 1e-4)); // 1.5 reach - 0.8
        CHECK(glm::length(field.resolve(glm::vec2(50.0f, 50.0f), walker)) == 0.0f);
    }

    SECTION("a non-blocking obstacle is invisible unless asked for") {
        spatial::ObstacleField soft;
        spatial::NavigationObstacle plant = solid(0.0f, 0.0f, 2.0f, 1.0f, spatial::ObstacleType::Vegetation);
        plant.traversal = spatial::Traversal::Passable;
        soft.add(plant);
        soft.build();
        CHECK(soft.blockingCount() == 0);
        CHECK_FALSE(soft.isOccupied(0.0f, 0.0f, 0.1f));
        spatial::ObstacleFilter walker;
        spatial::ObstacleHit hit;
        CHECK_FALSE(soft.blocker(glm::vec2(0.0f), walker, hit));
        walker.includeNonBlocking = true;
        CHECK(soft.blocker(glm::vec2(0.0f), walker, hit));
    }

    SECTION("the same obstacles always hash the same") {
        spatial::ObstacleField again;
        again.add(solid(0.0f, 0.0f, 1.0f, 3.0f));
        again.add(solid(20.0f, 0.0f, 2.0f, 4.0f));
        again.add(solid(-14.0f, 9.0f, 0.5f, 12.0f, spatial::ObstacleType::Trunk));
        again.build(7.0f); // a different cell size is not a different world
        CHECK(again.contentHash() == field.contentHash());
    }
}

// ---- what becomes an obstacle --------------------------------------------------------------------

TEST_CASE("the obstacle policy tells scenery from a solid", "[navigation][obstacles]") {
    SECTION("the composer's category wins where there is one") {
        CHECK(entity::classifyAsset("rock", "anything.gltf") == spatial::ObstacleType::Rock);
        CHECK(entity::classifyAsset("structure", "anything.gltf") == spatial::ObstacleType::Structure);
    }
    SECTION("and the asset's own path decides where there is not") {
        // Glowmere's scene file predates categories entirely, which is why this path exists.
        CHECK(entity::classifyAsset("", "../assets/quaternius/glTF/CommonTree_1.gltf") ==
              spatial::ObstacleType::Trunk);
        CHECK(entity::classifyAsset("", "../assets/quaternius/glTF/Rock_Medium_1.gltf") ==
              spatial::ObstacleType::Rock);
        CHECK(entity::classifyAsset("", "../assets/quaternius/glTF/Grass_Common_Short.gltf") ==
              spatial::ObstacleType::Vegetation);
        CHECK(entity::classifyAsset("", "../assets/quaternius/glTF/Fern_1.gltf") ==
              spatial::ObstacleType::Vegetation);
    }
    SECTION("a layer named for its species is classified by that when the asset is generic") {
        world::ScatterLayer layer;
        layer.name = "canopy";
        layer.asset = "../assets/library/plant_large.gltf";
        CHECK(entity::classifyScatterLayer(layer) == spatial::ObstacleType::Trunk);
    }
    SECTION("a whole layer of ground cover costs one comparison and produces nothing") {
        // 120,000 grass instances must not be iterated to discover they are grass.
        world::ScatterLayer grass;
        grass.name = "grass";
        grass.asset = "Grass_Common_Short.gltf";
        grass.height = 0.7f;
        grass.minScale = 0.6f;
        grass.maxScale = 1.6f;
        spatial::PointCloud cloud;
        cloud.resize(5000);
        spatial::ObstacleField out;
        CHECK(entity::obstaclesFromScatter(grass, cloud, 0.3f, 1.3f, {}, out) == 0);
        CHECK(out.empty());
    }
    SECTION("a tall thing contributes its trunk, not its canopy") {
        world::ScatterLayer trees;
        trees.name = "canopy";
        trees.asset = "CommonTree_1.gltf";
        trees.height = 14.0f;
        trees.minScale = 1.0f;
        trees.maxScale = 1.0f;
        spatial::PointCloud cloud;
        cloud.resize(1);
        cloud.positions()[0] = glm::vec3(3.0f, 10.0f, -4.0f);
        cloud.scales()[0] = glm::vec3(1.0f);
        spatial::ObstacleField out;
        REQUIRE(entity::obstaclesFromScatter(trees, cloud, 0.36f, 7.0f, {}, out) == 1);
        const spatial::NavigationObstacle& tree = out.obstacles()[0];
        CHECK(tree.type == spatial::ObstacleType::Trunk);
        CHECK(tree.height == 14.0f);
        CHECK(tree.base == 10.0f);
        // The asset bounds a fourteen-metre tree at about five metres of radius. Recording that
        // would shut the forest to anything that walks.
        CHECK(tree.radius < 1.2f);
        CHECK(tree.radius > 0.2f);
    }
    SECTION("the same species blocks at one size and not at another") {
        // Per-instance, which is the whole reason this is not a per-layer flag: a big fan plant is
        // walked round and a small one is waded through.
        world::ScatterLayer plants;
        plants.name = "fan-plants";
        plants.asset = "Plant_1_Big.gltf";
        plants.height = 3.2f;
        plants.minScale = 0.65f;
        plants.maxScale = 1.6f;
        spatial::PointCloud cloud;
        cloud.resize(2);
        cloud.positions()[0] = glm::vec3(0.0f);
        cloud.scales()[0] = glm::vec3(0.7f); // 2.24 m: waded through
        cloud.positions()[1] = glm::vec3(10.0f, 0.0f, 0.0f);
        cloud.scales()[1] = glm::vec3(1.55f); // 4.96 m: walked around
        spatial::ObstacleField out;
        CHECK(entity::obstaclesFromScatter(plants, cloud, 0.4f, 2.3f, {}, out) == 1);
        REQUIRE(out.size() == 1);
        CHECK(out.obstacles()[0].center.x == 10.0f);
    }
    SECTION("heroes contribute their authored volume, trimmed") {
        std::vector<world::HeroPoint> heroes(1);
        heroes[0].name = "elder";
        heroes[0].position = glm::vec3(-1.0f, -9.0f, -46.0f);
        heroes[0].radius = 8.2f;
        heroes[0].height = 16.5f;
        spatial::ObstacleField out;
        REQUIRE(entity::obstaclesFromHeroes(heroes, out) == 1);
        const spatial::NavigationObstacle& hero = out.obstacles()[0];
        CHECK(hero.type == spatial::ObstacleType::Structure);
        CHECK(hero.height == 16.5f);
        // A hero's radius is sized for a camera to frame it; using it whole would put an
        // exclusion zone around a tree a character is meant to be able to walk up to.
        CHECK(hero.radius < 8.2f);
        CHECK(hero.radius > 3.0f);
    }
}

// ---- traversal: what it takes to get past a thing ------------------------------------------------

TEST_CASE("a solid says what getting past it takes, and the body says what it can do about that",
          "[navigation][obstacles][traversal]") {
    // ADR-196. The old arrangement had exactly one notion of traversal and it was a float on the
    // *query* -- so the field could say how tall a solid was and nothing about what kind of effort
    // it took, and every solid a body could not step over was the same solid.
    const entity::ObstaclePolicy policy;

    SECTION("a short rock is stepped over and a tall one is walked round") {
        CHECK(entity::traversalClass(spatial::ObstacleType::Rock, 0.3f, policy) ==
              spatial::Traversal::StepOver);
        CHECK(entity::traversalClass(spatial::ObstacleType::Rock, 3.0f, policy) ==
              spatial::Traversal::Blocking);
        // And the band between them is the one the class exists for.
        CHECK(entity::traversalClass(spatial::ObstacleType::Rock, 1.0f, policy) ==
              spatial::Traversal::Jumpable);
    }

    SECTION("every class has a name a diagnostic can print") {
        // The same reason `navRejectName` and `pathStatusName` exist: a class nobody can read is a
        // class nobody acts on, and this one is the answer to "why did it go round that".
        CHECK(std::string(spatial::traversalName(spatial::Traversal::Passable)) == "passable");
        CHECK(std::string(spatial::traversalName(spatial::Traversal::StepOver)) == "step-over");
        CHECK(std::string(spatial::traversalName(spatial::Traversal::Jumpable)) == "jumpable");
        CHECK(std::string(spatial::traversalName(spatial::Traversal::Blocking)) == "blocking");
    }

    SECTION("a creature is never vaulted, whatever size it is") {
        // The one thing identity decides here: a solid that walks away mid-jump is not one to
        // commit to. A rock of the same height is.
        CHECK(entity::traversalClass(spatial::ObstacleType::Creature, 1.0f, policy) ==
              spatial::Traversal::Blocking);
        CHECK(entity::traversalClass(spatial::ObstacleType::Rock, 1.0f, policy) ==
              spatial::Traversal::Jumpable);
    }

    SECTION("a body that cannot jump treats a jumpable solid exactly as a blocking one") {
        spatial::ObstacleField field;
        spatial::NavigationObstacle boulder = solid(0.0f, 0.0f, 1.0f, 1.0f);
        boulder.traversal = spatial::Traversal::Jumpable;
        field.add(boulder);
        field.build();

        spatial::ObstacleFilter walker; // jumpOver defaults to 0: it cannot
        walker.bodyRadius = 0.45f;
        walker.stepOver = 0.4f;
        REQUIRE(walker.jumpOver == 0.0f);
        spatial::ObstacleHit hit;
        CHECK(field.blocker(glm::vec2(0.5f, 0.0f), walker, hit));
        CHECK(field.segmentBlocked(glm::vec2(-6.0f, 0.0f), glm::vec2(6.0f, 0.0f), walker));
        CHECK(field.traversalAt(glm::vec2(0.5f, 0.0f), walker, hit) ==
              spatial::Traversal::Blocking);

        // The counter-arm, so the one above is not passing because nothing can ever pass: a body
        // that *can* clear a metre walks the same line.
        spatial::ObstacleFilter deer = walker;
        deer.jumpOver = 1.2f;
        CHECK_FALSE(field.blocker(glm::vec2(0.5f, 0.0f), deer, hit));
        CHECK_FALSE(field.segmentBlocked(glm::vec2(-6.0f, 0.0f), glm::vec2(6.0f, 0.0f), deer));
        CHECK(field.traversalAt(glm::vec2(0.5f, 0.0f), deer, hit) == spatial::Traversal::Jumpable);
        CHECK(hit.index == 0);

        // And a jump is an ability, not a licence: a solid taller than the body can clear is a
        // wall to it too.
        spatial::ObstacleField wall;
        spatial::NavigationObstacle tall = solid(0.0f, 0.0f, 1.0f, 2.5f);
        tall.traversal = spatial::Traversal::Jumpable;
        wall.add(tall);
        wall.build();
        CHECK(wall.blocker(glm::vec2(0.5f, 0.0f), deer, hit));
    }

    SECTION("traversalAt answers with the hardest thing in the way") {
        spatial::ObstacleField field;
        spatial::NavigationObstacle kerb = solid(0.0f, 0.0f, 1.0f, 0.2f);
        kerb.traversal = spatial::Traversal::StepOver;
        field.add(kerb);
        spatial::NavigationObstacle boulder = solid(6.0f, 0.0f, 1.0f, 1.0f);
        boulder.traversal = spatial::Traversal::Jumpable;
        field.add(boulder);
        spatial::NavigationObstacle trunk = solid(6.0f, 1.0f, 1.0f, 9.0f, spatial::ObstacleType::Trunk);
        field.add(trunk);
        field.build();

        spatial::ObstacleFilter deer;
        deer.bodyRadius = 0.45f;
        deer.stepOver = 0.4f;
        deer.jumpOver = 1.2f;
        spatial::ObstacleHit hit;
        CHECK(field.traversalAt(glm::vec2(0.0f, 0.0f), deer, hit) == spatial::Traversal::StepOver);
        CHECK(hit.index == 0);
        // Standing where the boulder and the trunk both reach: the trunk is the answer, because
        // the question is what it takes to get past, and the hardest thing decides.
        CHECK(field.traversalAt(glm::vec2(6.0f, 0.5f), deer, hit) == spatial::Traversal::Blocking);
        CHECK(hit.index == 2);
        CHECK(field.traversalAt(glm::vec2(60.0f, 60.0f), deer, hit) == spatial::Traversal::Passable);
    }

    SECTION("the class a solid is born with does not promise anything to a body") {
        // A nominal step-over is still a wall to something whose own step is lower, which is why
        // the class lives on the obstacle and the ability lives on the filter.
        spatial::ObstacleField field;
        spatial::NavigationObstacle kerb = solid(0.0f, 0.0f, 1.0f, 0.35f);
        kerb.traversal = spatial::Traversal::StepOver;
        field.add(kerb);
        field.build();
        spatial::ObstacleFilter cart;
        cart.bodyRadius = 0.6f;
        cart.stepOver = 0.05f; // a wheel
        spatial::ObstacleHit hit;
        CHECK(field.blocker(glm::vec2(0.0f), cart, hit));
    }

    SECTION("the traversal class is part of what makes two worlds the same world") {
        spatial::ObstacleField a;
        a.add(solid(3.0f, 4.0f, 1.0f, 1.0f));
        spatial::ObstacleField b;
        spatial::NavigationObstacle jumpable = solid(3.0f, 4.0f, 1.0f, 1.0f);
        jumpable.traversal = spatial::Traversal::Jumpable;
        b.add(jumpable);
        CHECK(a.contentHash() != b.contentHash());
    }
}

// ---- a layer that says what it is ----------------------------------------------------------------

TEST_CASE("a scatter layer can overrule the filename heuristic", "[navigation][obstacles][traversal]") {
    // ADR-196. `classifyScatterLayer` is keywords over an asset path, and it is wrong in ways an
    // author can see and, until now, could not fix.
    spatial::PointCloud cloud;
    cloud.resize(3);
    for (std::size_t i = 0; i < 3; ++i) {
        cloud.positions()[i] = glm::vec3(static_cast<float>(i) * 12.0f, 0.0f, 0.0f);
        cloud.scales()[i] = glm::vec3(1.0f);
    }

    SECTION("a layer marked passable contributes nothing however tall it is") {
        world::ScatterLayer grove;
        grove.name = "canopy";              // every heuristic in the file says trunk
        grove.asset = "CommonTree_1.gltf";
        grove.height = 14.0f;               // and it is four times the blocking threshold
        grove.minScale = 1.0f;
        grove.maxScale = 1.0f;
        spatial::ObstacleField out;
        REQUIRE(entity::obstaclesFromScatter(grove, cloud, 0.36f, 7.0f, {}, out) == 3);
        out.clear();
        grove.navigation = world::ScatterNavigation::Passable;
        CHECK(entity::obstaclesFromScatter(grove, cloud, 0.36f, 7.0f, {}, out) == 0);
        CHECK(out.empty());
    }

    SECTION("a layer marked blocks contributes below the height threshold its class would impose") {
        // A 1.2 m plant: vegetation by every keyword, and `vegetationMinHeight` is 4 m, so the
        // whole layer is rejected by one comparison. The author says otherwise.
        world::ScatterLayer thicket;
        thicket.name = "thicket";
        thicket.asset = "Plant_7.gltf";
        thicket.height = 1.2f;
        thicket.minScale = 1.0f;
        thicket.maxScale = 1.0f;
        spatial::ObstacleField control;
        REQUIRE(entity::obstaclesFromScatter(thicket, cloud, 0.4f, 1.0f, {}, control) == 0);

        thicket.navigation = world::ScatterNavigation::Blocks;
        spatial::ObstacleField out;
        REQUIRE(entity::obstaclesFromScatter(thicket, cloud, 0.4f, 1.0f, {}, out) == 3);
        // Declared solid means `Blocking`, not "jumpable because it happens to be 1.2 m": the
        // author overruled the heuristic once and it does not get a second say.
        CHECK(out.obstacles()[0].traversal == spatial::Traversal::Blocking);
        CHECK(out.obstacles()[0].type == spatial::ObstacleType::Vegetation); // identity is untouched
        out.build();
        spatial::ObstacleFilter walker;
        spatial::ObstacleHit hit;
        CHECK(out.blocker(glm::vec2(12.0f, 0.0f), walker, hit));
    }

    SECTION("auto is the heuristic, and it classes by height as it always did") {
        world::ScatterLayer boulders;
        boulders.name = "boulders";
        boulders.asset = "Rock_Medium_1.gltf";
        boulders.height = 1.0f;   // over rockMinHeight (0.9), under jumpOverHeight (1.2)
        boulders.minScale = 1.0f;
        boulders.maxScale = 1.0f;
        spatial::ObstacleField out;
        REQUIRE(entity::obstaclesFromScatter(boulders, cloud, 0.45f, 1.0f, {}, out) == 3);
        CHECK(out.obstacles()[0].type == spatial::ObstacleType::Rock);
        CHECK(out.obstacles()[0].traversal == spatial::Traversal::Jumpable);

        world::ScatterLayer cliffs = boulders;
        cliffs.height = 6.0f;
        spatial::ObstacleField tall;
        REQUIRE(entity::obstaclesFromScatter(cliffs, cloud, 0.45f, 1.0f, {}, tall) == 3);
        CHECK(tall.obstacles()[0].traversal == spatial::Traversal::Blocking);
    }

    SECTION("a hero is blocking whatever its proportions") {
        std::vector<world::HeroPoint> heroes(1);
        heroes[0].name = "marker";
        heroes[0].position = glm::vec3(0.0f);
        heroes[0].radius = 2.0f;
        heroes[0].height = 0.9f; // inside the vault band, and not a thing anybody vaults
        spatial::ObstacleField out;
        REQUIRE(entity::obstaclesFromHeroes(heroes, out) == 1);
        CHECK(out.obstacles()[0].traversal == spatial::Traversal::Blocking);
    }
}

// ---- the navigation graph ------------------------------------------------------------------------

TEST_CASE("the navigation graph routes around what steering cannot", "[navigation][grid]") {
    // A deliberately flat, featureless world. The point of this test is the graph, and running it
    // on `defaultWorld` measured something else entirely: that world has a river through it, and
    // two points on opposite banks are genuinely unreachable -- a correct refusal that looks
    // exactly like a broken planner. A flat world makes "no route" mean "the wall".
    world::WorldMap map;
    map.name = "flat";
    map.size = glm::vec2(200.0f, 200.0f);
    map.baseHeight = 0.0f;
    map.prepare();
    world::Ecology ecology;
    world::ClearanceField clearance;
    clearance.map = &map;
    clearance.ecology = &ecology;
    clearance.cameraRadius = 0.6f;
    clearance.groundClearance = 0.0f;

    // A wall of solids with one gap in it. Local steering fans a few radians either side of the
    // straight line and cannot see round something this long; a planner can.
    auto obstacles = std::make_shared<spatial::ObstacleField>();
    for (float z = -40.0f; z <= 40.0f; z += 1.5f) {
        if (z > 24.0f && z < 32.0f) {
            continue; // the gap
        }
        obstacles->add(solid(0.0f, z, 1.2f, 6.0f));
    }
    obstacles->build();

    entity::Navigator nav(&map, clearance);
    nav.setObstacles(obstacles);
    nav.buildGrid(2.0f);
    REQUIRE(nav.grid() != nullptr);
    REQUIRE(nav.grid()->valid());
    const entity::NavGridStats& stats = nav.grid()->stats();
    INFO("grid " << stats.width << "x" << stats.height << ", " << stats.walkable << " of "
                 << stats.cells << " walkable, " << stats.blocked << " blocked");
    // The wall is in the graph. A grid that recorded none of it would route straight through and
    // look like it was working.
    REQUIRE(stats.blocked > 30);
    REQUIRE(stats.walkable > stats.cells / 2);

    const glm::vec2 from(-25.0f, 0.0f);
    const glm::vec2 to(25.0f, 0.0f);

    SECTION("the straight line is refused, and steering cannot see why") {
        CHECK_FALSE(nav.pathClear(from, to));
        // This is the limitation the planner exists for, stated as an assertion rather than as a
        // comment. Steering looks a few metres ahead; twenty-five metres from a wall, those few
        // metres are perfectly clear, so it reports the straight line and walks the character into
        // something it cannot see yet. Local steering is not a substitute for a route, and the
        // navigation that was here -- which had only steering -- could not have been tuned into one.
        const glm::vec2 straight = glm::normalize(to - from);
        CHECK(glm::dot(nav.steer(from, to, 6.0f), straight) > 0.999f);
        // Up against it, it does see, and turns.
        CHECK(glm::dot(nav.steer(glm::vec2(-4.0f, 0.0f), to, 6.0f), straight) < 0.95f);
    }

    SECTION("a route exists, and it goes through the gap") {
        std::vector<glm::vec2> path;
        REQUIRE(nav.findPath(from, to, path));
        REQUIRE(!path.empty());
        INFO("expansions " << nav.grid()->lastExpansions() << ", waypoints " << path.size());
        CHECK(glm::length(path.back() - to) < 3.0f);
        // Every leg is clear of solids: the route is walkable, not merely connected on a grid.
        glm::vec2 previous = from;
        float length = 0.0f;
        float maxZ = 0.0f;
        for (const glm::vec2& point : path) {
            INFO("leg to " << point.x << ", " << point.y);
            CHECK_FALSE(obstacles->segmentBlocked(previous, point, nav.filter(0.0f)));
            length += glm::length(point - previous);
            maxZ = std::max(maxZ, point.y);
            previous = point;
        }
        // It detoured through the gap rather than taking the 50 m straight line.
        INFO("route length " << length << " m, furthest z " << maxZ);
        CHECK(length > 55.0f);
        CHECK(maxZ > 18.0f);
        // And it is a route, not a staircase of cell centres: the string pull should leave far
        // fewer waypoints than the fifty-odd cells it crossed.
        CHECK(path.size() < 16);
    }

    SECTION("a route is asked for by request and answered with a reason") {
        // The seam an action layer uses (§6). A reason rather than `false`, because "try a nearer
        // goal", "try a different kind of goal" and "try again" are three different things to do.
        entity::PathRequest request;
        request.from = from;
        request.to = to;
        const entity::PathResult route = nav.requestPath(request);
        INFO("status: " << entity::pathStatusName(route.status));
        CHECK(route.status == entity::PathStatus::Ok);
        CHECK(route.ok());
        CHECK(!route.waypoints.empty());
        CHECK(route.length > 55.0f);
        CHECK(route.expansions > 0);
        CHECK(glm::length(route.goal - to) < 3.0f);
    }

    SECTION("asking to go where you already are is not a failure") {
        entity::PathRequest request;
        request.from = from;
        request.to = from + glm::vec2(0.4f, 0.0f);
        request.goalTolerance = 2.0f;
        const entity::PathResult route = nav.requestPath(request);
        CHECK(route.status == entity::PathStatus::AlreadyThere);
        CHECK(route.ok());
        CHECK(route.waypoints.empty());
    }

    SECTION("a destination outside the world is named as such, not searched for") {
        entity::PathRequest request;
        request.from = from;
        request.to = glm::vec2(5000.0f, 5000.0f);
        const entity::PathResult route = nav.requestPath(request);
        CHECK(route.status == entity::PathStatus::NoGoal);
        CHECK_FALSE(route.ok());
        CHECK(route.waypoints.empty());
    }

    SECTION("a route in hand is re-checked without repeating the search") {
        std::vector<glm::vec2> path;
        REQUIRE(nav.findPath(from, to, path));
        CHECK(nav.pathValid(from, path, 0));
        // Drop a wall across it. The route is now a route through a rock, and the point of
        // `pathValid` is that a walker finds that out before walking into it.
        auto blocked = std::make_shared<spatial::ObstacleField>();
        for (float z = -95.0f; z <= 95.0f; z += 1.5f) {
            blocked->add(solid(0.0f, z, 1.2f, 6.0f));
        }
        blocked->build();
        entity::Navigator changed(&map, clearance);
        changed.setObstacles(blocked);
        CHECK_FALSE(changed.pathValid(from, path, 0));
        CHECK_FALSE(changed.pathValid(from, {}, 0)); // an empty route is not a valid one
    }

    SECTION("a wall with no gap is reported as no route, not walked into") {
        // The failure that must not be silent. A planner that shrugged and returned the straight
        // line would put a character into a rock and call it arrival, which is what the rejection
        // sampler it replaced effectively did.
        auto sealed = std::make_shared<spatial::ObstacleField>();
        for (float z = -95.0f; z <= 95.0f; z += 1.5f) {
            sealed->add(solid(0.0f, z, 1.2f, 6.0f));
        }
        sealed->build();
        entity::Navigator walled(&map, clearance);
        walled.setObstacles(sealed);
        walled.buildGrid(2.0f);
        std::vector<glm::vec2> path;
        CHECK_FALSE(walled.findPath(from, to, path));
        CHECK(path.empty());
    }

    SECTION("a destination inside a solid ends at the nearest place a body could stand") {
        std::vector<glm::vec2> path;
        const glm::vec2 insideARock(0.0f, 0.0f);
        if (nav.findPath(from, insideARock, path)) {
            REQUIRE(!path.empty());
            CHECK_FALSE(obstacles->isOccupied(path.back().x, path.back().y, 0.4f));
        }
    }

    SECTION("the walkable ground is one connected region, and it knows it") {
        // §5: islands must be explicit. A wall with a gap in it does not divide the world, and the
        // flood fill has to agree with the search about that -- if it were more generous, two cells
        // would be called connected and no route would ever be found between them.
        const entity::NavGridStats& s = nav.grid()->stats();
        INFO("regions " << s.regions << ", largest " << s.largestRegion << " of " << s.walkable);
        CHECK(s.regions >= 1);
        CHECK(nav.grid()->connected(from, to));
        CHECK(nav.grid()->regionAt(from) != 0);
        CHECK(nav.grid()->regionAt(from) == nav.grid()->regionAt(to));
        CHECK(nav.grid()->regionSize(nav.grid()->regionAt(from)) > 100);
        // A point outside the world belongs to no region.
        CHECK(nav.grid()->regionAt(glm::vec2(5000.0f, 5000.0f)) == 0);
    }

    SECTION("a sealed wall makes two regions, and unreachable is answered without searching") {
        auto sealed = std::make_shared<spatial::ObstacleField>();
        for (float z = -95.0f; z <= 95.0f; z += 1.5f) {
            sealed->add(solid(0.0f, z, 1.2f, 6.0f));
        }
        sealed->build();
        entity::Navigator split(&map, clearance);
        split.setObstacles(sealed);
        split.buildGrid(2.0f);
        REQUIRE(split.grid() != nullptr);
        const entity::NavGridStats& s = split.grid()->stats();
        INFO("regions " << s.regions << ", largest " << s.largestRegion << " of " << s.walkable);
        CHECK(s.regions >= 2);
        CHECK_FALSE(split.grid()->connected(from, to));

        entity::PathRequest request;
        request.from = from;
        request.to = to;
        const entity::PathResult route = split.requestPath(request);
        CHECK(route.status == entity::PathStatus::Unreachable);
        CHECK_FALSE(route.ok());
        // And it cost nothing. Without regions this answer required opening every cell on this side
        // of the wall first -- in Glowmere, nine and a half thousand of them, for a fact already
        // known the moment the grid was built.
        CHECK(route.expansions == 0);
    }

    SECTION("the same request always produces the same route") {
        std::vector<glm::vec2> a;
        std::vector<glm::vec2> b;
        REQUIRE(nav.findPath(from, to, a));
        REQUIRE(nav.findPath(from, to, b));
        REQUIRE(a.size() == b.size());
        for (std::size_t i = 0; i < a.size(); ++i) {
            CHECK(a[i] == b[i]);
        }
    }

    SECTION("without a graph the navigator still answers, straight-line") {
        // A scene that never built one -- a test, a world with no terrain -- must keep working.
        entity::Navigator bare(&map, clearance);
        std::vector<glm::vec2> path;
        CHECK(bare.findPath(glm::vec2(-10.0f, 0.0f), glm::vec2(-5.0f, 0.0f), path));
        CHECK(path.size() == 1);
    }
}

// ---- grounding -----------------------------------------------------------------------------------

namespace {

// Vertical acceleration of a height series, as RMS of its second difference. What a viewer reads as
// jitter: not how far the body is from the surface, but how sharply it changes direction.
double verticalAcceleration(const std::vector<float>& series) {
    if (series.size() < 3) {
        return 0.0;
    }
    double sum = 0.0;
    for (std::size_t i = 2; i < series.size(); ++i) {
        const double d = static_cast<double>(series[i]) - 2.0 * series[i - 1] + series[i - 2];
        sum += d * d;
    }
    return std::sqrt(sum / static_cast<double>(series.size() - 2));
}

// Walks a straight line at full update rate and records both what the ground follower produced and
// what snapping straight onto the surface would have.
void walkStraight(const entity::Navigator& nav, const entity::GroundSettings& settings,
                  std::vector<float>& followed, std::vector<float>& snapped) {
    constexpr double kStep = 1.0 / 60.0;
    constexpr float kSpeed = 4.0f;
    entity::GroundFollower body;
    glm::vec2 at(-90.0f, -30.0f);
    const glm::vec2 heading(1.0f, 0.0f);
    const float yaw = std::atan2(heading.x, heading.y);
    for (int i = 0; i < 2400; ++i) {
        followed.push_back(body.update(nav, at, yaw, kSpeed, kStep, settings).height);
        snapped.push_back(nav.groundHeight(at));
        at += heading * kSpeed * static_cast<float>(kStep);
    }
}

} // namespace

TEST_CASE("a grounded body follows the surface without inheriting its noise", "[navigation][grounding]") {
    world::Ecology ecology;
    entity::GroundSettings settings;
    constexpr double kStep = 1.0 / 60.0;

    SECTION("on ground rougher than the body, it removes what the body would bridge") {
        // The condition the footprint filter exists for, and the only one in which it can be shown
        // to do anything: terrain carrying detail finer than the walker standing on it. At four
        // metres a second and sixty hertz the body takes 67 mm steps, so a 40 cm wrinkle arrives as
        // a vertical twitch six frames wide -- visible, and nothing to do with the character.
        world::WorldMap rough;
        rough.size = glm::vec2(240.0f, 240.0f);
        rough.layers.push_back({.frequency = 0.008f, .amplitude = 24.0f});
        rough.layers.push_back({.frequency = 1.6f, .amplitude = 0.35f}); // 60 cm wrinkles
        rough.prepare();
        world::ClearanceField clearance;
        clearance.map = &rough;
        clearance.ecology = &ecology;
        clearance.cameraRadius = 0.6f;
        clearance.groundClearance = 0.0f;
        const entity::Navigator nav(&rough, clearance);

        std::vector<float> followed;
        std::vector<float> snapped;
        walkStraight(nav, settings, followed, snapped);
        const double smooth = verticalAcceleration(followed);
        const double raw = verticalAcceleration(snapped);
        INFO("vertical acceleration " << smooth << " followed vs " << raw << " snapped");
        REQUIRE(raw > 0.0);
        // The claim the ground follower makes, and the reason the filter is spatial rather than
        // temporal. The first version smoothed only in time: it spent the whole walk pinned against
        // its own clamp and came out slightly *worse* than snapping.
        CHECK(smooth < raw * 0.6);
    }

    SECTION("on ground smoother than the body it changes almost nothing, and never rings") {
        // The other half of the claim, and the one that catches an over-eager filter. Where all the
        // vertical motion is the hill itself, a walker must climb the hill: a follower that
        // flattened this would be lagging terrain a viewer can see, and one that overshot would be
        // adding motion that is not there.
        world::WorldMap smoothMap = world::defaultWorld();
        world::ClearanceField clearance;
        clearance.map = &smoothMap;
        clearance.ecology = &ecology;
        clearance.cameraRadius = 0.6f;
        clearance.groundClearance = 0.0f;
        const entity::Navigator nav(&smoothMap, clearance);

        std::vector<float> followed;
        std::vector<float> snapped;
        walkStraight(nav, settings, followed, snapped);
        const double smooth = verticalAcceleration(followed);
        const double raw = verticalAcceleration(snapped);
        INFO("vertical acceleration " << smooth << " followed vs " << raw << " snapped");
        CHECK(smooth <= raw);
    }

    SECTION("and it is never inside the ground, nor floating above it") {
        world::WorldMap map = world::defaultWorld();
        world::ClearanceField clearance;
        clearance.map = &map;
        clearance.ecology = &ecology;
        clearance.cameraRadius = 0.6f;
        clearance.groundClearance = 0.0f;
        const entity::Navigator nav(&map, clearance);

        entity::GroundFollower body;
        glm::vec2 p(-90.0f, -30.0f);
        const glm::vec2 heading(1.0f, 0.0f);
        const float yaw = std::atan2(heading.x, heading.y);
        float worstFloat = 0.0f;
        float worstSink = 0.0f;
        float worstPointSink = 0.0f;
        for (int i = 0; i < 2400; ++i) {
            const entity::GroundResult result = body.update(nav, p, yaw, 4.0f, kStep, settings);
            const float ground = nav.groundHeight(p);
            // Penetration is a statement about the body, not about a point. A body with a footprint
            // rests on what is under that footprint, and its origin sitting a few centimetres below
            // the sample at its own centre is not the mesh being in the ground -- it is the ground
            // under the rest of it being lower. What is forbidden is going below *all* of it.
            float lowest = ground;
            for (int k = 0; k < 8; ++k) {
                const float angle = static_cast<float>(k) * 0.7853982f;
                lowest = std::min(lowest, nav.groundHeight(p + glm::vec2(std::cos(angle), std::sin(angle)) *
                                                                   settings.footprint));
            }
            worstFloat = std::max(worstFloat, result.height - ground);
            worstSink = std::max(worstSink, lowest - result.height);
            worstPointSink = std::max(worstPointSink, ground - result.height);
            p += heading * 4.0f * static_cast<float>(kStep);
        }
        INFO("worst float " << worstFloat << " above the centre sample, " << worstSink
                            << " below the footprint, " << worstPointSink << " below the centre");
        CHECK(worstSink <= 1e-3f);                  // never below everything under it
        CHECK(worstPointSink <= 0.15f);             // and never far below the point it stands on
        CHECK(worstFloat <= settings.maxFloat + 0.06f);
    }

    SECTION("it stands upright when told not to lean") {
        world::WorldMap map = world::defaultWorld();
        world::ClearanceField clearance;
        clearance.map = &map;
        clearance.ecology = &ecology;
        const entity::Navigator nav(&map, clearance);
        entity::GroundSettings upright = settings;
        upright.slopeAlign = 0.0f;
        entity::GroundFollower body;
        const entity::GroundResult level =
            body.update(nav, glm::vec2(0.0f, 0.0f), 0.0f, 0.0f, kStep, upright);
        CHECK(level.pitch == 0.0f);
        CHECK(level.roll == 0.0f);
    }

    SECTION("a body with no world under it stands on the zero plane rather than refusing") {
        const entity::Navigator nowhere;
        entity::GroundFollower orphan;
        const entity::GroundResult result =
            orphan.update(nowhere, glm::vec2(500.0f, 500.0f), 0.0f, 0.0f, kStep, settings);
        CHECK(result.height == 0.0f);
        CHECK(result.grounded);
    }
}

// PROBE (temporary): the reported defect -- four-legged farm animals walking INTO a hill rather
// than up it. A body of length L on a slope t, pitched by p, puts its nose (L/2)(tan t - tan p)
// below the ground and floats its tail by the same. Swept across slope angle, because the severity
// is a function of the grade and Glowmere has a range of them.
TEST_CASE("PROBE quadruped nose penetration against slope angle", "[.probe][slope]") {
    world::Ecology ecology;
    struct Sample { float slopeDeg; glm::vec2 at; const world::WorldMap* map; };
    std::vector<world::WorldMap> maps;
    // Increasing amplitude at one low frequency: smooth hills of increasing steepness, so a search
    // over them finds points at a range of real gradients.
    for (float amp : {12.0f, 30.0f, 60.0f, 110.0f, 180.0f}) {
        world::WorldMap m;
        m.size = glm::vec2(600.0f, 600.0f);
        m.layers.push_back({.frequency = 0.004f, .amplitude = amp});
        m.prepare();
        maps.push_back(std::move(m));
    }

    struct Animal { const char* name; float length; };
    const Animal animals[] = {{"bull", 2.65f * 3.6f}, {"cow", 2.36f * 3.6f}, {"horse", 2.32f * 3.6f},
                              {"pig", 1.26f * 3.6f},  {"sheep", 1.25f * 3.6f}, {"goat", 0.95f * 3.6f}};

    const auto measure = [](const entity::Navigator& nav, glm::vec2 at,
                            const entity::GroundSettings& settings, float length) {
        entity::GroundFollower body;
        const glm::vec2 heading(1.0f, 0.0f);
        const float yaw = std::atan2(heading.x, heading.y);
        entity::GroundResult r;
        for (int i = 0; i < 400; ++i) {
            r = body.update(nav, at, yaw, 0.0f, 1.0 / 60.0, settings);
        }
        const float half = length * 0.5f;
        const float nose = nav.groundHeight(at + heading * half) -
                           (r.height + half * std::sin(r.pitch / 57.2957795f));
        return std::pair<float, float>{r.pitch, nose};
    };

    WARN("slope  animal   SHIPPING (fp 0.55 m, align 0.55)      PROPOSED (fp = L/2, align 0.92)");
    for (const world::WorldMap& map : maps) {
        world::ClearanceField clearance;
        clearance.map = &map;
        clearance.ecology = &ecology;
        const entity::Navigator nav(&map, clearance);
        glm::vec2 best(0.0f);
        float bestGrad = 0.0f;
        for (float x = -200.0f; x < 200.0f; x += 2.0f) {
            for (float z = -200.0f; z < 200.0f; z += 2.0f) {
                const glm::vec2 at(x, z);
                const float d = (nav.groundHeight(at + glm::vec2(4.0f, 0.0f)) -
                                 nav.groundHeight(at - glm::vec2(4.0f, 0.0f))) / 8.0f;
                if (d > bestGrad) { bestGrad = d; best = at; }
            }
        }
        const float slopeDeg = std::atan(bestGrad) * 57.2957795f;
        // The two knobs separated, because changing both on one hunch is how a fix gets credited to
        // the wrong half of itself.
        if (slopeDeg > 20.0f && slopeDeg < 25.0f) {
            const Animal& a = animals[0]; // the bull: the longest body and the worst case
            for (float align : {0.55f, 0.80f, 0.92f, 1.00f}) {
                for (float fp : {0.55f, a.length * 0.25f, a.length * 0.5f}) {
                    entity::GroundSettings g;
                    g.slopeAlign = align;
                    g.footprint = fp;
                    const auto m = measure(nav, best, g, a.length);
                    WARN("MATRIX align " << align << " footprint " << fp << " -> pitch " << m.first
                                         << " nose " << m.second);
                }
            }
        }
    }
}

// ---- agent-agent separation (§11) ---------------------------------------------------------------

TEST_CASE("characters make room for each other rather than standing in each other",
          "[navigation][crowd]") {
    // Two walkers given the same destination from opposite sides converge on it, and §11 asks that
    // they not end up inside one another. The mechanism is separation rather than avoidance: bodies
    // that each planned around the other would replan whenever anyone walked past, and two that each
    // waited for the other would deadlock facing each other forever.
    //
    // The scene is deliberately a flat empty world, because what is being tested is the bodies, not
    // the terrain.
    world::WorldMap map;
    map.name = "flat";
    map.size = glm::vec2(160.0f, 160.0f);
    map.prepare();
    world::Ecology ecology;
    world::ClearanceField clearance;
    clearance.map = &map;
    clearance.ecology = &ecology;
    const entity::Navigator nav(&map, clearance);

    params::ParameterSet params;
    entity::EntityWorld world;
    std::vector<entity::EntityDesc> descs;
    for (int i = 0; i < 2; ++i) {
        entity::EntityDesc desc;
        desc.name = i == 0 ? "one" : "two";
        desc.seed = static_cast<std::uint32_t>(7717 + i * 13);
        entity::BehaviorDesc walk;
        walk.kind = "explore";
        walk.settings = nlohmann::json{{"speed", 3.0f},     {"runSpeed", 3.0f}, {"bodyRadius", 2.0f},
                                       {"minRange", 4.0f},  {"maxRange", 60.0f}, {"strollChance", 1.0f},
                                       {"idleMin", 0.0f},   {"idleMax", 0.0f},  {"observeChance", 0.0f}};
        desc.behaviors.push_back(walk);
        descs.push_back(std::move(desc));
    }
    world.setEntities(std::move(descs), 4242u);
    // Two bindings the entities drive nothing through: this test is about the behaviour layer, and
    // an entity with no node still runs its behaviours (that is the ADR-088 split).
    std::vector<entity::NodeBinding> bindings;
    for (const char* name : {"one", "two"}) {
        entity::NodeBinding binding;
        binding.node = name;
        binding.exists = true;
        binding.transformPrefix = std::string("nodes/") + name + "/";
        // Half a metre apart, with two-metre bodies: they start three metres inside each other.
        // Waiting for two wanderers to happen to collide tests nothing -- over nine hundred frames
        // of an earlier version of this they never met once, and it passed.
        binding.anchor = glm::vec3(name[0] == 'o' ? -0.25f : 0.25f, 0.0f, 0.0f);
        bindings.push_back(std::move(binding));
    }
    world.setBindings(std::move(bindings));
    world.setNavigator(nav);
    world.registerParameters(params, "entity/");
    world.bind(params, "entity/");

    float worstOverlap = 0.0f;
    std::size_t framesTouching = 0;
    float gapAtStart = 0.0f;
    float worstLateOverlap = 0.0f;
    for (std::uint64_t frame = 0; frame < 900; ++frame) {
        entity::EntityUpdate tick;
        tick.time = static_cast<double>(frame) / 60.0;
        tick.dt = 1.0 / 60.0;
        tick.frameIndex = frame;
        world.update(tick, params);
        const glm::vec3 a = world.find("one")->locomotion().position;
        const glm::vec3 b = world.find("two")->locomotion().position;
        const float gap = glm::length(glm::vec2(a.x - b.x, a.z - b.z));
        const float overlap = 4.0f - gap; // two 2 m bodies
        if (frame == 0) {
            gapAtStart = gap;
        }
        if (overlap > 0.0f) {
            ++framesTouching;
            worstOverlap = std::max(worstOverlap, overlap);
            // After a second they have had every chance to resolve it.
            if (frame > 60) {
                worstLateOverlap = std::max(worstLateOverlap, overlap);
            }
        }
    }
    INFO("gap at start " << gapAtStart << " m; worst overlap " << worstOverlap << " m over "
                         << framesTouching << " frames of 900, worst after the first second "
                         << worstLateOverlap << " m");
    // They began three metres inside each other, so the test is only meaningful if that was real.
    REQUIRE(gapAtStart < 1.5f);
    // And they got out of each other. Half a metre of a four-metre separation is contact rather
    // than co-location; anything more is two characters occupying the same ground.
    CHECK(worstLateOverlap < 0.5f);

    SECTION("a body that declared no radius takes no part in it") {
        // A craft flies over a crowd. `radius` defaults to 0 and that has to mean "not a body"
        // rather than "a body of no size", or every hovering thing would shove the ground traffic.
        CHECK(world.crowd().size() <= world.size());
        for (const spatial::NavigationObstacle& body : world.crowd().obstacles()) {
            CHECK(body.radius > 0.0f);
            CHECK(body.type == spatial::ObstacleType::Creature);
        }
    }
}

// ---- water as a depth rather than a wall --------------------------------------------------------

namespace {

// A flat, noiseless world with two watercourses across it: a ford half a metre deep at x = -30 and
// a channel two and a half metres deep at x = +30. Flat on purpose -- what is being measured is the
// water rule, and terrain noise would put a slope rejection in front of it. A `River` feature's
// path level *is* its water surface and `amplitude` is how far the bed sits below it, so the depth
// at the centre of each course is exactly the amplitude.
world::WorldMap twoCrossings() {
    world::WorldMap map;
    map.name = "two crossings";
    map.size = glm::vec2(240.0f, 240.0f);
    map.baseHeight = 0.0f;
    world::Feature ford;
    ford.name = "ford";
    ford.kind = world::FeatureKind::River;
    ford.path = {{-30.0f, 0.0f, -110.0f}, {-30.0f, 0.0f, 110.0f}};
    ford.width = 12.0f;
    ford.amplitude = 0.5f;
    ford.water = true;
    map.features.push_back(ford);
    world::Feature channel = ford;
    channel.name = "channel";
    channel.path = {{30.0f, 0.0f, -110.0f}, {30.0f, 0.0f, 110.0f}};
    channel.amplitude = 2.5f;
    map.features.push_back(channel);
    map.prepare();
    return map;
}

entity::Navigator walkerOn(const world::WorldMap& map, world::Ecology& ecology, float wadeDepth) {
    world::ClearanceField clearance;
    clearance.map = &map;
    clearance.ecology = &ecology;
    clearance.cameraRadius = 0.6f;
    clearance.groundClearance = 0.0f;
    entity::NavSettings settings;
    settings.wadeDepth = wadeDepth;
    return entity::Navigator(&map, clearance, settings);
}

} // namespace

TEST_CASE("a walker meets water as a depth, not as a wall", "[navigation][water]") {
    const world::WorldMap map = twoCrossings();
    world::Ecology ecology;
    const glm::vec2 dry(0.0f, 0.0f);
    const glm::vec2 ford(-30.0f, 0.0f);
    const glm::vec2 channel(30.0f, 0.0f);

    // The world is what the test says it is. Without this the two rejections below could both be
    // "there is no water anywhere" and the test would pass on a map with no river in it.
    const entity::Navigator dryWalker = walkerOn(map, ecology, 0.0f);
    REQUIRE_THAT(dryWalker.terrain().waterDepthAt(ford),
                 Catch::Matchers::WithinAbs(0.5f, 0.02f));
    REQUIRE_THAT(dryWalker.terrain().waterDepthAt(channel),
                 Catch::Matchers::WithinAbs(2.5f, 0.02f));
    REQUIRE(dryWalker.terrain().waterDepthAt(dry) == 0.0f);

    SECTION("the sample carries the depth, and it is the world's own number") {
        // Carried rather than re-derived: `NavSample` used to stop at the water *surface*, so
        // every caller that wanted the depth took a second sample of the world to get it.
        CHECK(dryWalker.sample(dry).waterDepth == 0.0f);
        CHECK(dryWalker.sample(ford).waterDepth == dryWalker.terrain().waterDepthAt(ford));
        CHECK(dryWalker.sample(channel).waterDepth == dryWalker.terrain().waterDepthAt(channel));
        // And it is filled in on a rejected sample too. A walker that is told only "submerged"
        // cannot tell a puddle it should have crossed from a lake it must go round.
        CHECK(dryWalker.sample(ford).reject == entity::NavReject::Submerged);
        CHECK(dryWalker.sample(ford).waterDepth > 0.4f);
    }

    SECTION("with no wade band declared, all water is a wall -- exactly as before") {
        CHECK(dryWalker.sample(dry).navigable);
        CHECK_FALSE(dryWalker.sample(ford).navigable);
        CHECK(dryWalker.sample(ford).reject == entity::NavReject::Submerged);
        CHECK_FALSE(dryWalker.sample(channel).navigable);
        CHECK(dryWalker.sample(channel).reject == entity::NavReject::Submerged);
    }

    SECTION("the freeboard margin is what a wade band replaces, not what it adds to") {
        // Dry ground standing 0.2 m above a water surface: no depth at all, and refused by the
        // 0.35 m margin that has always been the rule. A body that will cross a half-metre ford and
        // still refuses to stand on a bank two handspans above the water is not describing
        // anything, so declaring a wade band retires the margin rather than stacking on it.
        world::WorldMap shelf;
        shelf.name = "a shelf above the sea";
        shelf.size = glm::vec2(200.0f, 200.0f);
        shelf.baseHeight = 0.2f;
        shelf.seaLevel = 0.0f;
        shelf.prepare();
        world::Ecology bare;
        const entity::NavSample dryRule = walkerOn(shelf, bare, 0.0f).sample(glm::vec2(0.0f));
        REQUIRE(dryRule.waterDepth == 0.0f);
        REQUIRE(std::isfinite(dryRule.waterSurface));
        CHECK_FALSE(dryRule.navigable);
        CHECK(dryRule.reject == entity::NavReject::Submerged);
        CHECK(walkerOn(shelf, bare, 0.4f).sample(glm::vec2(0.0f)).navigable);
    }

    SECTION("with a wade band, the shallow crossing opens and the deep one does not") {
        const entity::Navigator wader = walkerOn(map, ecology, 0.8f);
        CHECK(wader.sample(dry).navigable);
        CHECK(wader.sample(ford).navigable);
        CHECK(wader.sample(ford).waterDepth > 0.4f); // and it is genuinely wet
        CHECK_FALSE(wader.sample(channel).navigable);
        CHECK(wader.sample(channel).reject == entity::NavReject::Submerged);
        // The band is a band. Raise it past the channel and the channel opens too, which is what
        // makes this a depth rather than a second hard-coded waterline.
        const entity::Navigator diver = walkerOn(map, ecology, 3.0f);
        CHECK(diver.sample(channel).navigable);
    }

    SECTION("the shared terrain query and the navigator answer the same question") {
        // §3's whole reason for existing. These are one set of rules, and a wade band that only
        // one of them knew about would be the duplication ADR-090 removed, growing back.
        const entity::Navigator wader = walkerOn(map, ecology, 0.8f);
        CHECK(wader.terrain().isWalkable(ford));
        CHECK_FALSE(dryWalker.terrain().isWalkable(ford));
        CHECK(wader.terrain().rejectAt(channel) == world::TerrainReject::Submerged);
    }
}

TEST_CASE("the navigation graph agrees with the walker about water", "[navigation][water][grid]") {
    const world::WorldMap map = twoCrossings();
    world::Ecology ecology;
    const glm::vec2 ford(-30.0f, 0.0f);
    const glm::vec2 channel(30.0f, 0.0f);

    entity::Navigator dryWalker = walkerOn(map, ecology, 0.0f);
    dryWalker.buildGrid(2.0f);
    entity::Navigator wader = walkerOn(map, ecology, 0.8f);
    wader.buildGrid(2.0f);
    REQUIRE(dryWalker.grid() != nullptr);
    REQUIRE(wader.grid() != nullptr);

    INFO("dry walkable " << dryWalker.grid()->stats().walkable << ", wading walkable "
                         << wader.grid()->stats().walkable << " of "
                         << wader.grid()->stats().cells);

    SECTION("a ford is out of the graph until the walker will cross it, and never the channel") {
        CHECK_FALSE(dryWalker.grid()->walkable(ford));
        CHECK(wader.grid()->walkable(ford));
        CHECK_FALSE(dryWalker.grid()->walkable(channel));
        CHECK_FALSE(wader.grid()->walkable(channel));
        // The ford is a strip through the whole world, so opening it is a big change to the
        // walkable set rather than a cell here and there.
        CHECK(wader.grid()->stats().walkable > dryWalker.grid()->stats().walkable + 500);
    }

    SECTION("a dry world's cells record no depth at all") {
        // The cost term below multiplies this byte, so "zero everywhere" is what makes the claim
        // that nothing changes for a world that does not wade a fact rather than an intention.
        std::size_t wet = 0;
        for (const entity::NavCell& c : dryWalker.grid()->cells()) {
            wet += c.wade > 0 ? 1u : 0u;
        }
        CHECK(wet == 0);
    }

    SECTION("a wading world's cells record how deep the water is") {
        const entity::NavCell& shallow = wader.grid()->at(wader.grid()->cellOf(ford));
        CHECK(shallow.wade > 0);
        // 0.5 m of a 0.8 m band, quantised over 0..255.
        CHECK(shallow.wade > 140);
        CHECK(shallow.wade < 190);
        // Dry ground is dry: a cell well away from either course carries nothing.
        CHECK(wader.grid()->at(wader.grid()->cellOf(glm::vec2(0.0f, 0.0f))).wade == 0);
    }

    SECTION("the two banks of the ford are one region only for the walker that can cross it") {
        // The point of all of it. A ford splits a world in two for a body that stops at the water,
        // and does not for one that does not, and that is a fact about reachability rather than
        // about the look of a cell.
        const glm::vec2 west(-60.0f, 0.0f);
        const glm::vec2 east(-6.0f, 0.0f);
        REQUIRE(dryWalker.grid()->walkable(west));
        REQUIRE(dryWalker.grid()->walkable(east));
        CHECK_FALSE(dryWalker.grid()->connected(west, east));
        CHECK(wader.grid()->connected(west, east));
    }
}

namespace {

// The deepest water a route passes through, sampled every half metre along the polyline. A route is
// a handful of waypoints and the water between two of them is invisible to a test that only looks
// at the ends -- which is the same mistake `pathClear` used to make about tree trunks.
float deepestAlong(const entity::Navigator& nav, glm::vec2 from,
                   const std::vector<glm::vec2>& route) {
    float deepest = 0.0f;
    glm::vec2 previous = from;
    for (const glm::vec2& leg : route) {
        const float span = glm::length(leg - previous);
        const int steps = std::max(1, static_cast<int>(std::ceil(span / 0.5f)));
        for (int i = 0; i <= steps; ++i) {
            const glm::vec2 at = previous + (leg - previous) * (static_cast<float>(i) /
                                                                static_cast<float>(steps));
            deepest = std::max(deepest, nav.terrain().waterDepthAt(at));
        }
        previous = leg;
    }
    return deepest;
}

float lengthOf(glm::vec2 from, const std::vector<glm::vec2>& route) {
    float total = 0.0f;
    glm::vec2 previous = from;
    for (const glm::vec2& leg : route) {
        total += glm::length(leg - previous);
        previous = leg;
    }
    return total;
}

} // namespace

TEST_CASE("a ford costs more to cross than dry ground of the same length", "[navigation][water][grid]") {
    // A channel that stops halfway across the world, so there is a way round it as well as a way
    // through it, and the planner has an actual choice to make.
    world::WorldMap map;
    map.name = "a channel with an end";
    map.size = glm::vec2(240.0f, 240.0f);
    map.baseHeight = 0.0f;
    world::Feature channel;
    channel.name = "channel";
    channel.kind = world::FeatureKind::River;
    channel.path = {{0.0f, 0.0f, -110.0f}, {0.0f, 0.0f, 0.0f}};
    channel.width = 10.0f;
    channel.amplitude = 0.5f;
    channel.water = true;
    map.features.push_back(channel);
    map.prepare();

    world::Ecology ecology;
    entity::Navigator nav = walkerOn(map, ecology, 0.8f);
    nav.buildGrid(2.0f);
    REQUIRE(nav.grid() != nullptr);

    const glm::vec2 from(-20.0f, -6.0f);
    const glm::vec2 to(20.0f, -6.0f);
    // The straight line is genuinely wet, or nothing below measures anything.
    REQUIRE(nav.terrain().waterDepthAt(glm::vec2(0.0f, -6.0f)) > 0.4f);
    REQUIRE(nav.grid()->walkable(from));
    REQUIRE(nav.grid()->walkable(to));

    std::vector<glm::vec2> route;

    SECTION("priced at nothing, the planner walks through the river") {
        entity::NavPathCost free;
        free.wadePenalty = 0.0f;
        REQUIRE(nav.grid()->findPath(from, to, route, free));
        const float wet = deepestAlong(nav, from, route);
        INFO("unpriced route " << lengthOf(from, route) << " m, deepest " << wet << " m");
        CHECK(wet > 0.3f);
    }

    SECTION("priced steeply, it goes round the end of it -- and stays round it") {
        entity::NavPathCost dear;
        dear.wadePenalty = 40.0f;
        REQUIRE(nav.grid()->findPath(from, to, route, dear));
        const float wet = deepestAlong(nav, from, route);
        const float length = lengthOf(from, route);
        INFO("priced route " << length << " m, deepest " << wet << " m");
        CHECK(wet < 0.05f);
        // Going round costs metres, which is the point: the route is longer than the straight line
        // it refused, so this is a decision the cost model made rather than a coincidence.
        CHECK(length > 45.0f);
        // And the string pull left it alone. The pull knows only walkability, a wadeable cell is
        // walkable, and the straight line from one bank to the other is clear -- so without the
        // guard on it the smoother would put this route straight back through the water it was just
        // paid to avoid, and `wet` above would be half a metre.
    }

    SECTION("at the default price a short ford still beats a long detour") {
        // The ford is ten metres wide and going round it costs about fourteen. Wading is supposed
        // to be *dearer*, not forbidden: a price that refused every crossing would be the old
        // binary rule with extra steps.
        REQUIRE(nav.grid()->findPath(from, to, route, entity::NavPathCost{}));
        INFO("default route " << lengthOf(from, route) << " m, deepest "
                              << deepestAlong(nav, from, route) << " m");
        CHECK(deepestAlong(nav, from, route) > 0.3f);
    }
}

namespace {

// How far one `explore` character actually travels in a minute. Distance along the ground
// rather than displacement, because a wanderer that walked in a circle would show a displacement of
// nothing and a speed that was fine.
float walkedDistance(const world::WorldMap& map, world::Ecology& ecology, float wadeDepth,
                     float wadeDrag) {
    const entity::Navigator nav = walkerOn(map, ecology, wadeDepth);
    params::ParameterSet params;
    entity::EntityWorld world;
    entity::EntityDesc desc;
    desc.name = "walker";
    desc.seed = 9001u;
    entity::BehaviorDesc walk;
    walk.kind = "explore";
    // Every branch that could differ between the two runs is pinned: no running, no observing, no
    // idling, and a stroll every time rather than a choice from an interest registry. What is left
    // is a body walking, which is the thing being timed.
    walk.settings = nlohmann::json{{"speed", 3.0f},      {"runSpeed", 3.0f},  {"runChance", 0.0f},
                                   {"minRange", 20.0f},  {"maxRange", 60.0f}, {"strollChance", 1.0f},
                                   {"idleMin", 0.0f},    {"idleMax", 0.0f},   {"observeChance", 0.0f},
                                   {"wadeDrag", wadeDrag}};
    desc.behaviors.push_back(walk);
    std::vector<entity::EntityDesc> descs;
    descs.push_back(std::move(desc));
    world.setEntities(std::move(descs), 4242u);
    entity::NodeBinding binding;
    binding.node = "walker";
    binding.exists = true;
    binding.transformPrefix = "nodes/walker/";
    binding.anchor = glm::vec3(0.0f);
    std::vector<entity::NodeBinding> bindings;
    bindings.push_back(std::move(binding));
    world.setBindings(std::move(bindings));
    world.setNavigator(nav);
    world.registerParameters(params, "entity/");
    world.bind(params, "entity/");

    float travelled = 0.0f;
    glm::vec3 previous = world.find("walker")->locomotion().position;
    // A minute, so the character completes several strolls rather than one. A single leg would
    // measure the arrival taper as much as the walking.
    for (std::uint64_t frame = 0; frame < 3600; ++frame) {
        entity::EntityUpdate tick;
        tick.time = static_cast<double>(frame) / 60.0;
        tick.dt = 1.0 / 60.0;
        tick.frameIndex = frame;
        world.update(tick, params);
        const glm::vec3 now = world.find("walker")->locomotion().position;
        travelled += glm::length(glm::vec2(now.x - previous.x, now.z - previous.z));
        previous = now;
    }
    return travelled;
}

world::WorldMap flatWorld(float seaLevel) {
    world::WorldMap map;
    map.name = "flat";
    map.size = glm::vec2(200.0f, 200.0f);
    map.baseHeight = 0.0f;
    map.seaLevel = seaLevel;
    map.prepare();
    return map;
}

} // namespace

TEST_CASE("a wading walker is slower than a dry one", "[navigation][water][behavior]") {
    // Two runs of the same character, from the same seed, in two worlds that differ in exactly one
    // thing: forty centimetres of water over the whole of one of them. Every roll it makes is
    // identical, every destination it picks is identical -- so any difference in how far it gets is
    // the water and nothing else.
    world::WorldMap dry = flatWorld(-1000.0f);
    world::WorldMap flooded = flatWorld(0.4f);
    world::Ecology ecology;

    // The flood is real and it is crossable: the whole world is half the wade band deep.
    const entity::Navigator wader = walkerOn(flooded, ecology, 0.8f);
    REQUIRE_THAT(wader.terrain().waterDepthAt(glm::vec2(12.0f, -7.0f)),
                 Catch::Matchers::WithinAbs(0.4f, 0.001f));
    REQUIRE(wader.sample(glm::vec2(12.0f, -7.0f)).navigable);

    const float onLand = walkedDistance(dry, ecology, 0.8f, 0.55f);
    const float inWater = walkedDistance(flooded, ecology, 0.8f, 0.55f);
    REQUIRE(onLand > 100.0f); // it walked at all, or the comparison below is two zeroes
    const float ratio = inWater / onLand;
    INFO("dry " << onLand << " m, wading " << inWater << " m over 3600 frames; ratio " << ratio);
    // Half the wade band deep at a drag of 0.55 is 0.725 of full speed. Bounded either side rather
    // than asserted to a tolerance, because the two runs drift apart in phase once one of them is
    // behind -- what is being pinned is that the water cost it most of a quarter of its travel.
    CHECK(ratio > 0.55f);
    CHECK(ratio < 0.92f);
    // And it kept walking. A character that stopped dead in the water would also be "slower", and
    // would be a bug.
    CHECK(inWater > 100.0f);

    SECTION("and the drag is what does it") {
        // The control that makes the measurement above attributable. Same flooded world, same
        // seed, drag turned off: the trajectory is the dry one exactly, so what the first
        // measurement found was the knob and not some other consequence of standing in water --
        // a different ground height, a different walkable set, a different destination roll.
        const float undragged = walkedDistance(flooded, ecology, 0.8f, 0.0f);
        INFO("undragged " << undragged << " m against dry " << onLand << " m");
        CHECK_THAT(undragged, Catch::Matchers::WithinAbs(onLand, 0.001f));
    }
}
