#pragma once

// The engine's side of the Director's reactivity (ADR-924, ADR-925): the project's authored routes
// and its sources as the plain values a plan compiles into, the reactive catalogue built from the
// engine's parameters, and a compiled plan's routes and sources installed. `src/directing/` never
// reads the engine (ADR-750); this is where the engine's state becomes what it does read.

#include "app/engine.hpp"
#include "app/source_document.hpp"
#include "core/error.hpp"
#include "directing/reactive_catalog.hpp"
#include "directing/scene_facts.hpp"
#include "params/serialization.hpp"
#include "scene/route_liveness.hpp"

#include <nlohmann/json.hpp>

#include <string>
#include <vector>

namespace avgen::app {

// The routes a person (or a plan) authored: not the ones a graph, an entity's reactions or a world
// macro install, which those subsystems rebuild themselves and a project never saves.
[[nodiscard]] inline std::vector<params::ModRoute> authoredRoutes(Engine& engine) {
    std::vector<params::ModRoute> out;
    for (const params::ModRoute& r : engine.modulator().routes()) {
        if (r.fromGraph || r.fromEntity || r.fromMacro) {
            continue;
        }
        // Only what a save keeps: the runtime half is re-derived by a bind.
        if (auto copy = params::routeFromJson(params::routeToJson(r)); copy) {
            out.push_back(std::move(*copy));
        }
    }
    return out;
}

// ADR-925: the reactive catalogue of the loaded project.
[[nodiscard]] inline directing::ReactiveCatalog reactiveCatalogFor(Engine& engine, const params::liveness::Facts* liveness,
                                                                  const std::vector<std::string>& keyedTargets) {
    directing::ReactiveInputs in;
    in.params = &engine.params();
    in.liveness = liveness;
    in.composition = engine.composition();
    in.effects = engine.effects();
    const std::vector<params::ModRoute> routes = authoredRoutes(engine);
    in.routes = routes;
    in.keyedTargets = keyedTargets;
    return directing::buildReactiveCatalog(in);
}

// ADR-924: a compilation's routes and sources into the engine. Sources first, so every signal a
// route reads is declared when the routes bind; the authored route list then becomes the staged one
// (the subsystems' routes are kept); one rebind at the end.
[[nodiscard]] inline Result<void> installReactivity(Engine& engine, const directing::Staging& staged) {
    bool changed = false;
    auto sources = setSourcesDocument(engine, staged.sources);
    if (!sources) {
        return std::unexpected(sources.error());
    }
    changed = *sources;
    nlohmann::json want = nlohmann::json::array();
    for (const params::ModRoute& r : staged.routes) {
        want.push_back(params::routeToJson(r));
    }
    nlohmann::json have = nlohmann::json::array();
    for (const params::ModRoute& r : authoredRoutes(engine)) {
        have.push_back(params::routeToJson(r));
    }
    if (want != have) {
        std::vector<params::ModRoute>& live = engine.modulator().routes();
        std::erase_if(live, [](const params::ModRoute& r) { return !r.fromGraph && !r.fromEntity && !r.fromMacro; });
        for (const params::ModRoute& r : staged.routes) {
            (void)engine.modulator().addRoute(r);
        }
        changed = true;
    }
    if (changed) {
        engine.rebind();
    }
    return {};
}

} // namespace avgen::app
