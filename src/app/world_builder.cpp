#include "app/world_builder.hpp"

#include "app/engine.hpp"
#include "core/log.hpp"
#include "scene/composition.hpp"

#include <chrono>
#include <utility>

namespace avgen::app {
namespace {
using Clock = std::chrono::steady_clock;

// The terrain a generated world needs when the scene has none. Sized from the recipe so a world
// that says it is four hundred metres across is four hundred metres across, and given enough noise
// layers to have somewhere for an ecology to prefer and avoid -- a perfectly flat plane makes every
// slope and altitude rule in the placer a no-op, which would look like the rules were ignored.
scene::CompositionNode defaultTerrainFor(const world::WorldRecipe& recipe) {
    scene::CompositionNode node;
    node.name = "terrain";
    node.kind = scene::NodeKind::Terrain;
    node.worldMap.name = recipe.world;
    node.worldMap.seed = recipe.seed;
    node.worldMap.size = glm::vec2(recipe.extent, recipe.extent);
    node.worldMap.erosion = 0.55f;
    // One NoiseLayer is one octave, so the stack is written out: a broad landform, a ridged
    // mid scale that gives slopes something to be, and a fine layer for texture underfoot.
    const float span = std::max(recipe.extent, 1.0f);
    world::NoiseLayer broad;
    broad.frequency = 1.0f / (span * 0.55f);
    broad.amplitude = span * 0.055f;
    broad.warp = span * 0.04f;
    world::NoiseLayer ridges;
    ridges.frequency = 1.0f / (span * 0.16f);
    ridges.amplitude = span * 0.022f;
    ridges.ridged = 0.65f;
    world::NoiseLayer detail;
    detail.frequency = 1.0f / (span * 0.045f);
    detail.amplitude = span * 0.006f;
    node.worldMap.layers = {broad, ridges, detail};
    // Without these, every layer the composer emits names a biome the terrain has never heard of
    // and the ecology refuses all of them. One vocabulary, defined next to the composer.
    node.worldMap.biomes = world::composerBiomes();
    return node;
}
} // namespace

Result<void> installWorld(Engine& engine, const GeneratedWorld& world) {
    auto* composition = engine.composition();
    if (composition == nullptr) {
        return fail("Generate World needs a composition; create or open one first");
    }
    if (world.composed.layers.empty()) {
        return fail("world '{}' composed no layers", world.recipe.world);
    }

    // Find the terrain this world grows on. The first one wins: a scene with two terrains is
    // ambiguous, and silently picking one of several is the kind of choice that is discovered later
    // as a bug.
    scene::CompositionNode* existing = nullptr;
    for (const auto& node : composition->nodes()) {
        if (node && node->kind == scene::NodeKind::Terrain) {
            existing = composition->findNode(node->name);
            break;
        }
    }

    if (existing != nullptr) {
        // Mutated in place rather than replaced. A CompositionNode is deliberately not copyable --
        // it owns built chunks and generated data -- and removing and re-adding it would throw away
        // everything else the node carries: its transform, its material, its world map, its name in
        // every route that targets it.
        //
        // The composer owns the ecology entirely, so this replaces rather than appends; otherwise
        // generating twice would double every layer.
        existing->ecology.layers = world.composed.layers;
    } else {
        auto node = defaultTerrainFor(world.recipe);
        node.ecology.layers = world.composed.layers;
        auto added = engine.addNode(std::move(node));
        if (!added) {
            return std::unexpected(added.error());
        }
    }

    // The composition plan is not a new concept: ADR-038's CompositionData already carries focal
    // points and exclusion regions, and already turns them into the reserved
    // "composition.clearance" / "composition.exclusion" / "composition.weight" fields that density
    // filters and effectors consume. So the plan is *translated into that*, rather than kept beside
    // it as a second description of the same idea -- which also means the void regions actually
    // thin the ecology instead of merely being drawn in a debug view.
    scene::CompositionData data = composition->composition();
    data.focalPoints.clear();
    data.exclusions.clear();
    for (const auto& focal : world.composed.plan.focal) {
        scene::FocalPoint point;
        point.name = focal.assetId.empty() ? "focus" : focal.assetId;
        point.position = glm::vec3(focal.center.x, 0.0f, focal.center.y);
        point.radius = focal.radius;
        point.weight = focal.strength;
        data.focalPoints.push_back(std::move(point));
    }
    for (std::size_t i = 0; i < world.composed.plan.voids.size(); ++i) {
        const auto& v = world.composed.plan.voids[i];
        scene::ExclusionRegion region;
        region.name = "void" + std::to_string(i);
        region.shape = scene::ExclusionRegion::Shape::Sphere;
        region.position = glm::vec3(v.center.x, 0.0f, v.center.y);
        region.radius = v.radius;
        data.exclusions.push_back(std::move(region));
    }
    // setComposition also marks the composition dirty, which is what schedules the rebuild; there
    // is no need to reach for the private one.
    composition->setComposition(std::move(data));
    log::info("world '{}': installed {} scatter layer(s), {} focal, {} void region(s)",
              world.recipe.world, world.composed.layers.size(), world.composed.plan.focal.size(),
              world.composed.plan.voids.size());
    return {};
}

JobId WorldBuilder::generate(world::WorldRecipe recipe, assets::AssetLibrary library) {
    JobRequest request;
    request.type = "world.generate";
    request.name = recipe.world.empty() ? std::string("world") : recipe.world;
    request.body = [this, recipe = std::move(recipe),
                    library = std::move(library)](JobContext& ctx) -> Result<void> {
        // Three stages, all of which report real work. Composition is fast enough that a bar is
        // almost decoration -- but it is an honest bar, and the same shape carries the slow stages
        // that will hang off this later.
        ctx.setStages({"Reading the recipe", "Composing ecology", "Preparing layers"});
        ctx.beginStage(0);
        ctx.setOperation("validating '" + recipe.world + "'");
        if (auto ok = recipe.validate(); !ok) {
            return std::unexpected(ok.error());
        }
        ctx.setStageProgress(1.0f);
        if (ctx.shouldCancel()) {
            return {};
        }

        ctx.beginStage(1);
        ctx.setOperation("composing from " + std::to_string(library.size()) + " asset(s)");
        const auto start = Clock::now();
        auto composed = world::composeWorld(recipe, library);
        if (!composed) {
            return std::unexpected(composed.error());
        }
        const double seconds = std::chrono::duration<double>(Clock::now() - start).count();
        ctx.setStageProgress(1.0f);
        if (ctx.shouldCancel()) {
            return {};
        }

        ctx.beginStage(2);
        const auto total = static_cast<float>(std::max<std::size_t>(composed->layers.size(), 1));
        for (std::size_t i = 0; i < composed->layers.size(); ++i) {
            if (ctx.shouldCancel()) {
                return {};
            }
            ctx.setOperation(composed->layers[i].name);
            ctx.setStageProgress(static_cast<float>(i + 1) / total);
        }
        ctx.log("composed " + std::to_string(composed->layers.size()) + " layer(s) in " +
                std::to_string(seconds) + " s");

        GeneratedWorld out;
        out.recipe = recipe;
        out.composed = std::move(*composed);
        out.composeSeconds = seconds;
        out.assetsConsidered = library.size();
        {
            // The worker's last act is to hand a value over. It does not touch the scene: the
            // renderer is reading that on another thread, and installing from here would be a race
            // that shows up as a crash under load rather than in a test.
            std::lock_guard<std::mutex> lock(mutex_);
            ready_.push_back(std::move(out));
        }
        return {};
    };
    return jobs_.submit(std::move(request));
}

std::vector<GeneratedWorld> WorldBuilder::collect() {
    std::lock_guard<std::mutex> lock(mutex_);
    return std::exchange(ready_, {});
}

bool WorldBuilder::busy() const {
    for (const auto& status : jobs_.statuses()) {
        if (status.type == "world.generate" && !jobStateIsTerminal(status.state)) {
            return true;
        }
    }
    return false;
}

} // namespace avgen::app
