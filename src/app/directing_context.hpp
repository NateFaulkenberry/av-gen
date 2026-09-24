#pragma once

// The engine's side of the Director (ADR-755, ADR-756). `src/directing/` never reads the engine
// (ADR-750); these functions are where the engine's state becomes the plain values it does read, and
// where a compiled plan becomes one undoable edit.

#include "analysis/analysis_track.hpp"
#include "app/edit_capture.hpp"
#include "app/engine.hpp"
#include "core/error.hpp"
#include "directing/compiler.hpp"
#include "directing/resolver.hpp"
#include "directing/scene_facts.hpp"
#include "directing/time_ref.hpp"
#include "entity/entity.hpp"
#include "scene/composition.hpp"
#include "ui/edit_history.hpp"
#include "world/hero.hpp"

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
    facts.sequence = engine.sequence();
    facts.plans = engine.directingPlans();
    if (const scene::Composition* comp = engine.composition(); comp != nullptr) {
        facts.capabilities = directing::CapabilityRegistry::fromComposition(*comp);
        facts.cameras = comp->cameraDirection();
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
        facts.cameras.ensureMainCamera();
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
    const auto& owned = engine.sequenceTargets();
    for (const params::Track& track : engine.timeline().tracks()) {
        if (std::find(owned.begin(), owned.end(), track.target) == owned.end()) {
            facts.authorTrackTargets.push_back(track.target);
        }
    }
    return facts;
}

// Installs a compilation as ONE command on the editor's history: the sequence, the camera collection
// and the plan (inserted, or replacing its earlier revision) together, so one undo takes back the
// content and its provenance at once (ADR-752, ADR-755). A refused install is undone before returning
// and pushes nothing.
[[nodiscard]] inline Result<void> applyCompilation(Engine& engine, ui::EditHistory& history,
                                                   const directing::Compilation& compilation) {
    EditCapture capture;
    capture.begin(engine);
    const auto undoAndFail = [&](std::string message) -> Result<void> {
        ui::EditCommand partial = capture.finish(engine, "refused");
        (void)ui::applyEdit(engine, partial, false);
        return fail("{}", message);
    };
    if (auto r = engine.setSequence(compilation.sequence); !r) {
        return undoAndFail(r.error().message);
    }
    if (engine.composition() != nullptr) {
        if (auto r = engine.setCameraDirection(compilation.cameras); !r) {
            return undoAndFail(r.error().message);
        }
    }
    auto& plans = engine.directingPlans();
    const auto existing = std::find_if(plans.begin(), plans.end(),
                                       [&](const directing::Plan& p) { return p.id == compilation.plan.id; });
    if (existing != plans.end()) {
        *existing = compilation.plan;
    } else {
        plans.push_back(compilation.plan);
    }
    history.push(capture.finish(engine, "Director: " + (compilation.plan.title.empty() ? compilation.plan.id
                                                                                         : compilation.plan.title)));
    return {};
}

} // namespace avgen::app
