#pragma once

// The engine's side of the Director (ADR-755, ADR-756). `src/directing/` never reads the engine
// (ADR-750); these functions are where the engine's state becomes the plain values it does read, and
// where a compiled plan becomes one undoable edit.

#include "analysis/analysis_track.hpp"
#include "app/engine.hpp"
#include "core/error.hpp"
#include "directing/compiler.hpp"
#include "directing/resolver.hpp"
#include "directing/scene_facts.hpp"
#include "directing/time_ref.hpp"
#include "entity/entity.hpp"
#include "scene/composition.hpp"
#include "world/hero.hpp"

#include <fmt/format.h>

#include <algorithm>
#include <string>
#include <vector>

namespace avgen::app {

// The piece as the Director places times in it: the sequence's sections (authored timeline first),
// the analysed beat grid and tempo, and the piece's duration.
[[nodiscard]] inline directing::MusicalContext musicalContextFor(const Engine& engine) {
    std::vector<double> beats;
    double tempo = 0.0;
    if (const analysis::AnalysisTrack* track = engine.track(); track != nullptr) {
        beats = track->beats().beatTimes;
        tempo = static_cast<double>(track->beats().tempoBpm);
    }
    // The piece is bounded by its audio. With none, the transport's duration is only the extent of
    // whatever is already on the timeline -- which is exactly what a plan may be extending -- so it
    // is no bound at all (0 = unknown).
    return directing::musicalContextFrom(engine.sequence(), beats, tempo, engine.audioDurationSeconds());
}

// Everything in the scene a plan can name, plus every parameter path (by exact path only).
[[nodiscard]] inline directing::SubjectIndex subjectIndexFor(Engine& engine) {
    directing::SubjectIndex index;
    if (const scene::Composition* comp = engine.composition(); comp != nullptr) {
        index = directing::SubjectIndex::fromComposition(*comp);
    }
    std::vector<std::string> paths;
    paths.reserve(engine.params().size());
    // (bases are gathered by sceneFactsFor; this index only needs the paths)
    for (const params::IParameter* p : engine.params().ordered()) {
        if (p != nullptr) {
            paths.emplace_back(p->path());
        }
    }
    index.addParameters(paths);
    return index;
}

// The staging view a plan is validated and compiled against. Copies, so a dry run changes nothing.
[[nodiscard]] inline directing::SceneFacts sceneFactsFor(Engine& engine) {
    directing::SceneFacts facts;
    facts.subjects = subjectIndexFor(engine);
    facts.music = musicalContextFor(engine);
    facts.staged.sequence = engine.sequence();
    facts.staged.effects = engine.capturedEffects();
    facts.plans = engine.directingPlans();
    if (const scene::Composition* comp = engine.composition(); comp != nullptr) {
        facts.capabilities = directing::CapabilityRegistry::fromComposition(*comp);
        // What a goal's walk will ask: the entity world's path provider, straight from the character.
        facts.walkable = [comp](glm::vec2 from, glm::vec2 to) -> std::optional<bool> {
            std::vector<glm::vec2> route;
            switch (comp->entityWorld().pathProvider().route(from, to, route)) {
            case entity::RouteStatus::Ready: return true;
            case entity::RouteStatus::Unreachable: return false;
            case entity::RouteStatus::Pending: return std::nullopt;
            }
            return std::nullopt;
        };
        // The same ground a performer stands on (Engine::setSequence's `groundHeightAt`).
        if (const world::TerrainQuery ground = comp->terrainQuery(); ground.valid()) {
            facts.groundAt = [ground](float x, float z) { return ground.surfaceAt(glm::vec2(x, z)); };
        }
        facts.staged.cameras = comp->cameraDirection();
        // Places are AUTHORED positions -- a hero's anchor, a node's base transform -- never a
        // simulation's current state, so validation and compilation are the same whenever they run.
        for (const world::HeroPoint& hero : comp->heroes()) {
            facts.places.push_back(directing::Place{directing::SubjectKind::Hero, hero.name, hero.position, hero.radius,
                                                    hero.height, hero.preferredCameraDistance});
        }
        for (const auto& node : comp->nodes()) {
            if (node == nullptr) {
                continue;
            }
            // A node an entity drives is where its simulation left it, not a fact: not a place.
            const bool driven = std::any_of(comp->entities().begin(), comp->entities().end(), [&](const entity::EntityDesc& e) {
                return (e.node.empty() ? e.name : e.node) == node->name;
            });
            if (driven ||
                std::any_of(facts.places.begin(), facts.places.end(), [&](const directing::Place& p) { return p.id == node->name; })) {
                continue;
            }
            facts.places.push_back(directing::Place{directing::SubjectKind::Node, node->name,
                                                    comp->nodeWorldTransform(*node).position, 2.0f, 0.0f, 0.0f});
        }
    } else {
        facts.staged.cameras.ensureMainCamera();
    }
    facts.parameterBases.reserve(engine.params().size());
    for (const params::IParameter* p : engine.params().ordered()) {
        if (p == nullptr) {
            continue;
        }
        std::vector<float> base(p->componentCount());
        for (std::size_t i = 0; i < base.size(); ++i) {
            base[i] = p->baseComponent(i);
        }
        facts.parameterBases.emplace_back(std::string(p->path()), std::move(base));
    }
    if (const scene::Composition* comp = engine.composition(); comp != nullptr) {
        for (const entity::EntityDesc& desc : comp->entities()) {
            directing::CharacterMark mark;
            mark.id = desc.name;
            mark.node = desc.node.empty() ? desc.name : desc.node;
            if (const params::IParameter* p = engine.params().find("nodes/" + mark.node + "/position");
                p != nullptr && p->componentCount() == 3) {
                mark.anchor = glm::vec3(p->baseComponent(0), p->baseComponent(1), p->baseComponent(2)); // the BASE
            }
            facts.characters.push_back(std::move(mark));
        }
    }
    const auto& owned = engine.sequenceTargets();
    for (const params::Track& track : engine.timeline().tracks()) {
        if (std::find(owned.begin(), owned.end(), track.target) == owned.end()) {
            facts.authorTrackTargets.push_back(track.target);
        }
    }
    return facts;
}

// Installs a compilation: the sequence, the camera collection and the effect list (each only as
// needed), and the plan -- inserted, or replacing its earlier revision -- with its `produced`
// fingerprints taken from the content AS INSTALLED. Records no history of its own: a caller wraps it
// (`applyCompilation` in a capture; an AI task in its transaction, whose sink makes it one command).
// Stops at the first refusal and says which; the caller owns the rollback.
[[nodiscard]] inline Result<void> installCompilation(Engine& engine, const directing::Compilation& compilation) {
    // Cameras before the sequence (ADR-760): a shot's tracks key a rig's channels
    // (`cameras/<slug>/followOffset`), so the rig's parameters should exist when the sequence
    // installs and binds. The other order also works today, only because `setCameraDirection`
    // rebinds the whole timeline afterwards; this order does not lean on that.
    if (engine.composition() != nullptr) {
        if (auto r = engine.setCameraDirection(compilation.staged.cameras); !r) {
            return fail("{}", r.error().message);
        }
        // ADR-760: a follow rig's offsets are parameters, and re-registering keeps an existing
        // parameter's value -- so a rig revised in place (a new chase distance) would keep the old
        // offset. The collection is the compiled one, so its offsets are what should be in force.
        // Every rig, not just the plan's: the others were captured from these very bases.
        for (const scene::CameraRig& rig : compilation.staged.cameras.cameras) {
            const auto write = [&](const char* channel, glm::vec3 value) {
                if (params::IParameter* p = engine.params().find("cameras/" + rig.slug + "/" + channel);
                    p != nullptr && p->componentCount() == 3) {
                    for (std::size_t i = 0; i < 3; ++i) {
                        p->setBaseComponent(i, value[static_cast<int>(i)]);
                    }
                }
            };
            if (!rig.slug.empty() && !rig.followNode.empty()) {
                write("followOffset", rig.followOffset);
            }
            if (!rig.slug.empty() && !rig.aimNode.empty()) {
                write("aimOffset", rig.aimOffset);
            }
        }
    }
    if (auto r = engine.setSequence(compilation.staged.sequence); !r) {
        return fail("{}", r.error().message);
    }
    // The effect list only when it changed: `setEffects` re-registers every fx/ parameter, which is
    // not free and not needed for a plan that touched no effect.
    const auto effectsJson = [](const std::vector<world::EffectInstance>& list) {
        nlohmann::json out = nlohmann::json::array();
        for (const world::EffectInstance& e : list) {
            out.push_back(e.toJson());
        }
        return out;
    };
    if (effectsJson(compilation.staged.effects) != effectsJson(engine.capturedEffects())) {
        if (auto r = engine.setEffects(compilation.staged.effects); !r) {
            return fail("{}", r.error().message);
        }
    }
    // Fingerprints of the content AS INSTALLED, not as compiled. Installing is not the identity:
    // an effect's window start becomes a float parameter (118.645 -> 118.64499...), and a fingerprint
    // of the compiled value would read as a hand edit on the very next revision.
    directing::Plan stored = compilation.plan;
    const directing::Staging installed = sceneFactsFor(engine).staged;
    for (directing::ContentRef& ref : stored.produced) {
        if (const auto content = directing::contentOf(ref, installed)) {
            ref.fingerprint = directing::fingerprint(*content);
        }
    }
    auto& plans = engine.directingPlans();
    const auto existing = std::find_if(plans.begin(), plans.end(),
                                       [&](const directing::Plan& p) { return p.id == stored.id; });
    if (existing != plans.end()) {
        *existing = std::move(stored);
    } else {
        plans.push_back(std::move(stored));
    }
    return {};
}

// After an install: is what the plan says it produced actually there, exactly as recorded?
// (Spec §19's ValidateCommittedState.) Empty when it is; otherwise one line per discrepancy.
[[nodiscard]] inline std::vector<std::string> verifyInstalled(Engine& engine, const std::string& planId) {
    std::vector<std::string> problems;
    const auto& plans = engine.directingPlans();
    const auto plan = std::find_if(plans.begin(), plans.end(), [&](const directing::Plan& p) { return p.id == planId; });
    if (plan == plans.end()) {
        problems.push_back(fmt::format("plan '{}' is not in the project", planId));
        return problems;
    }
    const directing::Staging installed = sceneFactsFor(engine).staged;
    for (const directing::ContentRef& ref : plan->produced) {
        const auto content = directing::contentOf(ref, installed);
        if (!content) {
            problems.push_back(fmt::format("{} '{}' is missing", directing::contentDomainName(ref.domain), ref.id));
        } else if (directing::fingerprint(*content) != ref.fingerprint) {
            problems.push_back(fmt::format("{} '{}' is not as recorded", directing::contentDomainName(ref.domain), ref.id));
        }
    }
    return problems;
}

} // namespace avgen::app
