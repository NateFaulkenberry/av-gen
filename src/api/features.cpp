// ADR-2152: the feature ids this build can parse (`avgen --describe features`, `capability.list`).
//
// Generated, never authored: each group is the list or name table that decides it. In a file of its
// own rather than in describe.cpp, so `capability.list` (ai/capabilities.cpp) reaches it without
// pulling the whole tool catalogue's registry into every binary that links the capability tools.
//
// A feature id says the parser reads a thing, not that this session can run it (ADR-2150's
// supported / available / permitted): `node.gltf` is listed on a machine with no glTF to load.

#include "api/describe.hpp"

#include "app/render_settings.hpp"
#include "scene/composition.hpp"
#include "world/effects/effect_kind.hpp"
#include "world/effects/effect_registry.hpp"

#include <string>
#include <vector>

namespace avgen::api {

std::vector<std::string> featureList() {
    std::vector<std::string> out;
    // Every top-level scene block but the envelope.
    for (const std::string_view key : scene::sceneFileKeys()) {
        if (key != "format" && key != "version" && key != "name") {
            out.push_back("scene." + std::string(key));
        }
    }
    for (const std::string_view key : scene::sceneEnvironmentKeys()) {
        out.push_back("scene.environment." + std::string(key));
    }
    for (const scene::NodeKind kind : scene::allNodeKinds()) {
        out.push_back("node." + std::string(scene::nodeKindName(kind)));
    }
    for (const world::EffectKind kind : world::declaredEffectKinds()) {
        out.push_back("effect." + std::string(world::effectKindName(kind)));
    }
    for (const app::RenderOutput output : app::allRenderOutputs()) {
        out.push_back("render." + std::string(app::renderOutputName(output)));
    }
    for (const std::string_view aov : app::RenderSettings::aovNames()) {
        out.push_back("aov." + std::string(aov));
    }
    // The project's optional audio inputs: `assets.stems` and `assets.audioClips` are keys of the
    // project's `assets` block, and `feeds` its top-level feed routing (ADR-1580). The C1 test holds
    // each to a key the project schema types.
    out.emplace_back("assets.stems");
    out.emplace_back("assets.feeds");
    out.emplace_back("assets.audioClips");
    return out;
}

} // namespace avgen::api
