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

// The terrain node a generated world needs when the scene has none. The heightfield itself comes
// from the composer, which has to know the ground in order to decide where the viewpoint and the
// heroes go; this only wraps it in a node.
scene::CompositionNode defaultTerrainFor(const world::WorldRecipe& recipe) {
    scene::CompositionNode node;
    node.name = "terrain";
    node.kind = scene::NodeKind::Terrain;
    node.worldMap = world::terrainFor(recipe);
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

// The half of an art direction that is not the air: the painterly surface mode, and the
// post-processing restraint that decides whether the emission ladder's top rungs read as luminous
// or as smeared. Bloom belongs to the art direction for exactly that reason -- a threshold of 1.0
// with an intensity of 0.18 is what makes a glow selective, and the same ladder under a threshold
// of 0.2 is a fog of light with no hierarchy left in it.
void applyArtDirection(Engine& engine, const world::ArtDirectionProfile& profile) {
    setBool(engine, "scene/stylized", profile.stylized);
    const world::PostProfile& post = profile.post;
    setBool(engine, "post/bloom/enabled", post.bloomEnabled);
    setFloat(engine, "post/bloom/intensity", post.bloomIntensity);
    setFloat(engine, "post/bloom/threshold", post.bloomThreshold);
    setFloat(engine, "post/bloom/knee", post.bloomKnee);
    setFloat(engine, "post/bloom/radius", post.bloomRadius);
    setFloat(engine, "post/bloom/emissionWeight", post.bloomEmissionWeight);
    setFloat(engine, "post/tonemap/chroma-retention", post.chromaRetention);
    setFloat(engine, "post/output/antialias", post.antialias);
    if (auto* p = engine.params().find("post/tonemap/operator")) {
        if (auto* i = dynamic_cast<params::Parameter<int>*>(p)) {
            i->setBase(post.tonemap);
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
    setVec3(engine, "scene/styledSkyAmbient", env.styledSkyAmbient);
    setVec3(engine, "scene/styledGroundAmbient", env.styledGroundAmbient);
}

// Installs a hero's authored reactions as ordinary modulation routes.
//
// Through the modulator, deliberately. A hero reacting to music must go through the same routes as
// everything else that reacts to music: a second reaction system beside it would be a second thing
// to debug when a scene does not move, and it would not appear in the route list, the graph editor,
// or a saved project.
std::size_t installHeroReactions(Engine& engine, const world::HeroPoint& hero) {
    if (hero.reactionProfile.empty()) {
        return 0;
    }
    const world::HeroReactionProfile* profile = world::findHeroReactionProfile(hero.reactionProfile);
    if (profile == nullptr) {
        log::warn("hero '{}': unknown reaction profile '{}'", hero.name, hero.reactionProfile);
        return 0;
    }
    std::size_t installed = 0;
    for (const world::HeroReaction& reaction : profile->reactions) {
        const world::HeroBehaviourTarget target = world::heroBehaviourTarget(reaction.behaviour);
        if (!target.supported) {
            log::info("hero '{}': {} needs {}, so it is not wired", hero.name,
                      world::heroBehaviourName(reaction.behaviour), target.missing);
            continue;
        }
        const std::string path = "nodes/" + hero.name + "/" + target.suffix;
        if (engine.params().find(path) == nullptr) {
            log::warn("hero '{}': no parameter '{}'", hero.name, path);
            continue;
        }
        params::ModRoute route;
        route.source = reaction.source;
        route.target = path;
        route.component = target.component;
        route.amount = reaction.amount;
        route.op = params::ModOp::Add;
        route.chain.attackMs = reaction.attackMs;
        route.chain.decayMs = reaction.decayMs;
        engine.modulator().addRoute(std::move(route));
        ++installed;
    }
    return installed;
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
        // Faint luminous mottling on the ground itself, which is an art-direction decision rather
        // than a terrain one -- it is the difference between ground and ground that is alive.
        node.terrain.groundMottle = true;
        node.terrain.groundGlow = world.composed.profile.groundGlow;
        node.terrain.groundGlowColor = world.composed.profile.groundGlowColor;
        node.terrain.groundGlowCoverage = world.composed.profile.groundGlowCoverage;
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
    applyArtDirection(engine, world.composed.profile);
    // The rig. Without one, a generated world is lit by whatever the scene defaults to, and the
    // profile's key-to-ambient ratio -- which is what makes a night a night -- has nowhere to go.
    if (auto rig = composition->installLightRig(world::rigFor(world.composed.profile)); !rig) {
        log::warn("world '{}': light rig: {}", world.recipe.world, rig.error().message);
    }

    // ---- the heroes ------------------------------------------------------------------------------
    // The things worth travelling towards, placed as ordinary glTF nodes so each can be selected,
    // moved, retextured and deleted like anything else in the scene. A person who dislikes where
    // the composer put one is not fighting a special case.
    //
    // A hero is meant to be an authored assembly (ADR-072) -- Glowmere's elder is three procedural
    // nodes plus a practical light. Placing a single asset is the fallback the composer currently
    // produces, and it is honestly weaker than an assembly: it gives the world a large silhouette
    // in a cleared space with a camera that knows how to approach it, and not a designed object.
    const scene::CompositionNode* terrainNode = nullptr;
    for (const auto& node : composition->nodes()) {
        if (node && node->kind == scene::NodeKind::Terrain) {
            terrainNode = node.get();
            break;
        }
    }
    const assets::AssetLibrary* library = world.library.size() > 0 ? &world.library : nullptr;
    for (const world::HeroPoint& hero : world.composed.plan.heroes) {
        if (hero.assetId.empty() || library == nullptr) {
            continue;
        }
        const assets::AssetDescriptor* asset = library->find(hero.assetId);
        if (asset == nullptr) {
            log::warn("world '{}': hero '{}' names '{}', which is not in the library",
                      world.recipe.world, hero.name, hero.assetId);
            continue;
        }
        if (composition->findNode(hero.name) != nullptr) {
            composition->removeNode(hero.name);   // regenerating replaces rather than stacking
        }
        scene::CompositionNode node;
        node.name = hero.name;
        node.kind = scene::NodeKind::Gltf;
        node.asset = library->resolve(*asset).generic_string();
        // On the ground. A hero floating over a valley or buried in a hillside is the most
        // conspicuous possible way to say the placement was never checked.
        float ground = 0.0f;
        if (terrainNode != nullptr) {
            ground = terrainNode->worldMap.height(glm::vec2(hero.position.x, hero.position.z));
        }
        node.transform.position = glm::vec3(hero.position.x, ground + hero.position.y, hero.position.z);
        node.transform.scale = glm::vec3(hero.scale);
        node.transform.rotation = glm::angleAxis(hero.yaw, glm::vec3(0.0f, 1.0f, 0.0f));
        if (auto added = engine.addNode(std::move(node)); !added) {
            log::warn("world '{}': hero '{}': {}", world.recipe.world, hero.name,
                      added.error().message);
        } else {
            // Reactions are installed after the node exists, because a route binds to a parameter
            // and the node's parameters are registered when it is added.
            const std::size_t routes = installHeroReactions(engine, hero);
            log::info("world '{}': hero '{}' importance {:.2f}, {:.0f} m tall, stand-off {:.0f} m, "
                      "{} reaction(s)",
                      world.recipe.world, hero.name, hero.importance,
                      asset->naturalSize.y * hero.scale, hero.preferredCameraDistance, routes);
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
        out.library = library;
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
