#include "app/world_builder.hpp"

#include "app/engine.hpp"
#include "core/log.hpp"
#include "params/parameter_set.hpp"
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

// Parameter writes, by path. A generated world's atmosphere is expressed as parameter *base*
// values on purpose: it is then exactly as editable, automatable, routable and saveable as a value
// somebody typed into the inspector, and a second "generated environment" state that overrides the
// parameter system would be a second source of truth for the same numbers.
void setFloat(Engine& engine, const std::string& path, float value) {
    if (auto* p = engine.params().find(path)) {
        if (auto* f = dynamic_cast<params::Parameter<float>*>(p)) {
            f->setBase(value);
            return;
        }
    }
    log::debug("world: no float parameter '{}'", path);
}

void setVec3(Engine& engine, const std::string& path, const glm::vec3& value) {
    if (auto* p = engine.params().find(path)) {
        if (auto* v = dynamic_cast<params::Parameter<glm::vec3>*>(p)) {
            v->setBase(value);
            return;
        }
    }
    log::debug("world: no vec3 parameter '{}'", path);
}

void setBool(Engine& engine, const std::string& path, bool value) {
    if (auto* p = engine.params().find(path)) {
        if (auto* b = dynamic_cast<params::Parameter<bool>*>(p)) {
            b->setBase(value);
        }
    }
}

void applyEnvironment(Engine& engine, const world::EnvironmentPlan& env) {
    setBool(engine, "env/sky/enabled", true);
    setBool(engine, "env/sky/background", true);
    setVec3(engine, "env/sky/zenithColor", env.skyZenith);
    setVec3(engine, "env/sky/horizonColor", env.skyHorizon);
    setVec3(engine, "env/sky/groundColor", env.skyGround);
    setVec3(engine, "env/sky/sunColor", env.sunColor);
    setFloat(engine, "env/sky/intensity", env.skyIntensity);
    setFloat(engine, "env/sky/sunIntensity", env.sunIntensity);
    setFloat(engine, "env/sky/sunSize", env.sunSize);
    setFloat(engine, "env/sky/sunGlow", env.sunGlow);
    setFloat(engine, "env/sky/haze", env.haze);
    setFloat(engine, "scene/keyLight", env.keyLight);
    setVec3(engine, "scene/fogColor", env.fogColor);
    setFloat(engine, "scene/fogDensity", env.fogDensity);
    setFloat(engine, "scene/fogHeight", env.fogHeight);
    setFloat(engine, "scene/fogHeightFalloff", env.fogHeightFalloff);
    setFloat(engine, "scene/volumeDensity", env.volumeDensity);
    setFloat(engine, "scene/volumeScattering", env.volumeScattering);
    setFloat(engine, "scene/volumeAbsorption", env.volumeAbsorption);
    setFloat(engine, "scene/volumeAnisotropy", env.volumeAnisotropy);
    setFloat(engine, "scene/volumeEmission", env.volumeEmission);
    setFloat(engine, "scene/volumeNoise", env.volumeNoise);
    setFloat(engine, "scene/volumeNoiseScale", env.volumeNoiseScale);
    setFloat(engine, "scene/volumeNoiseSpeed", env.volumeNoiseSpeed);
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

    const bool freshWorld = existing == nullptr;
    if (existing != nullptr) {
        // Mutated in place rather than replaced. A CompositionNode is deliberately not copyable --
        // it owns built chunks and generated data -- and removing and re-adding it would throw away
        // everything else the node carries: its transform, its material, its world map, its name in
        // every route that targets it.
        //
        // The composer owns the ecology entirely, so this replaces rather than appends; otherwise
        // generating twice would double every layer.
        existing->ecology.layers = world.composed.layers;
        existing->ecology.clearances = world.composed.clearances;
    } else {
        auto node = defaultTerrainFor(world.recipe);
        node.ecology.layers = world.composed.layers;
        node.ecology.clearances = world.composed.clearances;
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

    // The atmosphere. Composed from the same recipe as the ecology and applied here because it is
    // the only half of a world that lives in parameters rather than in nodes.
    applyEnvironment(engine, world.composed.environment);

    // ---- the landmark --------------------------------------------------------------------------
    // One enormous object that reads as a landmark. The focal region marks where the composition
    // wants the eye to go; without something standing in it, that is a note about a composition
    // rather than a composition, which is what the first generated valley was.
    //
    // It is placed as an ordinary glTF node, so it can be selected, moved, retextured and deleted
    // like anything else in the scene, and so a person who does not like where the composer put it
    // is not fighting a special case.
    const scene::CompositionNode* terrainNode = nullptr;
    for (const auto& node : composition->nodes()) {
        if (node && node->kind == scene::NodeKind::Terrain) {
            terrainNode = node.get();
            break;
        }
    }
    for (std::size_t i = 0; i < world.composed.plan.focal.size(); ++i) {
        const auto& focal = world.composed.plan.focal[i];
        if (focal.landmarkPath.empty() || focal.landmarkScale <= 0.0f) {
            continue;
        }
        const std::string name = "landmark_" + std::to_string(i);
        if (composition->findNode(name) != nullptr) {
            composition->removeNode(name);   // regenerating replaces it rather than stacking
        }
        scene::CompositionNode node;
        node.name = name;
        node.kind = scene::NodeKind::Gltf;
        node.asset = focal.landmarkPath;
        // On the ground, not at y=0. A landmark floating over a valley or buried in a hillside is
        // the most conspicuous possible way to say the placement was never checked.
        float ground = 0.0f;
        if (terrainNode != nullptr) {
            ground = terrainNode->worldMap.height(focal.center);
        }
        node.transform.position = glm::vec3(focal.center.x, ground, focal.center.y);
        node.transform.scale = glm::vec3(focal.landmarkScale);
        // Turned to face nowhere in particular, deterministically, so two worlds from one seed are
        // still the same world.
        node.transform.rotation =
            glm::angleAxis(static_cast<float>(world.recipe.seed % 360u) * 0.0174532925f,
                           glm::vec3(0.0f, 1.0f, 0.0f));
        if (auto added = engine.addNode(std::move(node)); !added) {
            log::warn("world '{}': landmark '{}' could not be placed: {}", world.recipe.world,
                      focal.landmarkPath, added.error().message);
        } else {
            log::info("world '{}': landmark {:.0f} m at ({:.0f}, {:.0f}), ground {:.1f} m",
                      world.recipe.world, focal.landmarkHeight, focal.center.x, focal.center.y,
                      ground);
        }
    }
    // Frame the camera on what was just composed, but only for a world that had no terrain before:
    // a generated world nobody can see is indistinguishable from one that failed to generate, while
    // a camera in an existing scene is somebody's decision and generating into that scene is not a
    // reason to overrule it.
    if (freshWorld) {
        // camera/position and camera/target are only read in mode 1 (free). A fresh composition
        // defaults to the orbit mode, which ignores both and circles the bounds centre -- so
        // framing a world without also setting the mode set two parameters nothing looked at, and
        // the rendered frame came out byte-identical. That default is also where the orbiting
        // camera in every scene so far came from.
        if (auto* mode = engine.params().find("camera/mode")) {
            if (auto* m = dynamic_cast<params::Parameter<int>*>(mode)) {
                m->setBase(1);
            }
        }
        if (auto* position = engine.params().find("camera/position")) {
            if (auto* vec = dynamic_cast<params::Parameter<glm::vec3>*>(position)) {
                const world::FocalRegion* focal =
                    world.composed.plan.focal.empty() ? nullptr : &world.composed.plan.focal.front();
                const glm::vec2 subject = focal != nullptr ? focal->center : glm::vec2(0.0f);
                const float span = std::max(world.recipe.extent, 1.0f);
                const float landmark = focal != nullptr ? focal->landmarkHeight : span * 0.06f;
                // The composer's viewpoint, not one invented here. It cleared a corridor to the
                // subject from that exact spot, so standing anywhere else means looking at a world
                // arranged for somewhere else -- and, in a world this dense, means starting the
                // shot with a tree trunk across the lens. That is what the first one did.
                const glm::vec2 eyeXZ = world.composed.plan.viewpoint;

                // On the ground, at head height. The first version put the eye at a thirty-fifth of
                // the world's width -- fifteen metres up in a four-hundred-metre valley -- which is
                // above the canopy and above every foreground plant, so a world composed to be
                // dense at the viewer's feet was rendered from where it has no feet.
                float eyeGround = 0.0f;
                if (terrainNode != nullptr) {
                    eyeGround = terrainNode->worldMap.height(eyeXZ);
                }
                const glm::vec3 eye(eyeXZ.x, eyeGround + 2.4f, eyeXZ.y);
                vec->setBase(eye);
                if (auto* target = engine.params().find("camera/target")) {
                    if (auto* t = dynamic_cast<params::Parameter<glm::vec3>*>(target)) {
                        // Aimed at the landmark's lower third rather than its middle: it puts the
                        // horizon low in frame and the subject's mass above it, and it leaves the
                        // top of the landmark near the top of the shot instead of the centre.
                        float subjectGround = 0.0f;
                        if (terrainNode != nullptr) {
                            subjectGround = terrainNode->worldMap.height(subject);
                        }
                        t->setBase(glm::vec3(subject.x, subjectGround + landmark * 0.34f, subject.y));
                    }
                }
                log::info("world '{}': camera framed on the focal region", world.recipe.world);
            }
        }
    }

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
