#include "ai/capabilities.hpp"

#include "api/describe.hpp"
#include "api/version.hpp"
#include "app/engine.hpp"
#include "scene/composition.hpp"

#include <algorithm>

namespace avgen::ai {
namespace {

// Authored, and mostly about honesty. Every `available = false` entry names the reason and the
// smallest thing that would change it, because "unsupported" without a reason is indistinguishable
// from "forgotten" and an agent cannot reason about either.
const std::vector<DomainCapability> kDomains = {
    {"parameter", "Parameters",
     "The engine's universal value surface: every scene, camera, environment, material, light-rig "
     "and post value with a path, type, range and metadata. Readable, writable, keyframeable and "
     "modulatable.",
     true, {}},
    {"scene", "Scene and nodes",
     "Composition nodes: find them, read them, move them, show and hide them. Transforms are "
     "parameters, so they animate and save like everything else.",
     true, {}},
    {"camera", "Camera",
     "Pose, mode, field of view, framing a subject, and the physical lens, exposure and focus "
     "settings.",
     true, {}},
    {"environment", "Environment and atmosphere",
     "Sky, sun, fog, volumetrics, wind, ambient brightness and exposure.", true, {}},
    {"material", "Materials",
     "Per-node emissive boost and roughness, and per-part tint, emissive colour, emissive gain, "
     "roughness and opacity on procedural nodes. Emissive values are HDR and are not clamped.",
     true, {}},
    {"lighting", "Lighting",
     "Reading every light in the flattened scene, and the light-rig parameters that drive them.",
     true, {}},
    {"sequencer", "Timeline and automation",
     "Keyframing any parameter against seconds or beats, reading tempo and playhead, removing "
     "tracks, seeking.",
     true, {}},
    {"audio", "Audio analysis",
     "Loudness, frequency bands, spectral centroid, onsets, tempo, beat position and musical event "
     "history.",
     true, {}},
    {"signal", "Signals", "Every named control signal on the bus and its live value.", true, {}},
    {"modulation", "Modulation",
     "Connecting a signal to a parameter with depth, operation, polarity, smoothing and envelopes.",
     true, {}},
    {"performance", "Performance",
     "Frame time, GPU time, draw calls, triangles, culling, light count and per-pass GPU cost.",
     true, {}},
    {"project", "Project and rollback",
     "Project state, and snapshots of the parameter domain used as the transaction primitive.",
     true, {}},

    // ---- what this build deliberately does not expose -------------------------------------------
    // ADR-2152 (C3): the old text said node creation and deletion were not exposed because a rollback
    // could not undo them. The scene domain has both now, and the rollback snapshot captures the
    // composition (ai::SnapshotStore::captureDocument), so what is left unexposed is the entities'.
    {"entity", "Entities and their behaviour", {}, false,
     "Entities can be read: what each drives, its behaviours and reactions, and its profile. "
     "Adding, removing or re-profiling an entity is not exposed; scene nodes are created, deleted "
     "and parented in the scene domain."},
    {"terrain", "Terrain", {}, false,
     "Not exposed in this pass. Terrain generation is being reworked concurrently and its "
     "parameter surface is in flux. The LOD and view-distance knobs on an existing terrain node "
     "are reachable through the parameter domain today."},
    {"water", "Water", {}, false,
     "Not exposed in this pass. Water bodies and their rendering are being reworked concurrently. "
     "Whatever water parameters the current scene registers can be found and set through the "
     "parameter domain."},
    {"navigation", "Navigation and character behaviour", {}, false,
     "Not exposed in this pass. The navigation and entity-behaviour surfaces are being reworked "
     "concurrently."},
    // ADR-2152 (C3): the old text said per-light editing needed each light to register parameters.
    // Authored scene lights already do (Composition registers lights/<id>/*).
    {"light-editing", "Adding and removing lights", {}, false,
     "A light the scene file authors registers parameters (lights/<id>/enabled, intensity, color, "
     "position, temperature, tint, castsShadow, range and more), so an existing light is edited "
     "through the parameter domain like any other value. Adding or removing a light changes the "
     "scene document and is not exposed yet. Lights a rig drives are changed through lightrig/*; "
     "lights the ecology derives are rebuilt every frame and are not editable."},
    {"render", "Rendering to a file", {}, false,
     "What the camera sees can be measured. Writing a render to a file is not exposed: it is an "
     "external side effect and, per the security model, needs a separately designed capability "
     "with explicit consent rather than a tool."},
    {"vision", "Looking at the rendered frame", {}, false,
     "Not exposed. The agent inspects structured renderer and performance state; it cannot see the "
     "image. Faking visual verification would be worse than not having it. The provider layer "
     "already reports a vision capability per provider, which is where an image-feedback loop "
     "would attach."},
};

} // namespace

const std::vector<DomainCapability>& domainCapabilities() { return kDomains; }

nlohmann::json capabilityDocument(const ToolRegistry& registry, const app::Engine& engine) {
    auto& mutableEngine = const_cast<app::Engine&>(engine); // NOLINT: the accessors are non-const

    // Availability is GENERATED, not declared (Director program 0.5). A domain is available when
    // the registry has tools in it: the authored `available` flag went stale the moment a tool
    // landed in a domain it called unavailable -- `entity` and `render` both said "not exposed"
    // while listing `entity.list` and `render.probe`. The authored reason survives as `limits`,
    // because what a domain still cannot do is worth saying; it just no longer decides anything.
    const auto operationsIn = [&](std::string_view id) {
        nlohmann::json operations = nlohmann::json::array();
        for (const Tool* tool : registry.all()) {
            if (tool->definition.domain() != id) {
                continue;
            }
            nlohmann::json op;
            op["name"] = tool->definition.name;
            op["description"] = tool->definition.description;
            op["readOnly"] = tool->definition.annotations.readOnly;
            op["mutatesProject"] = tool->definition.annotations.mutatesProject;
            op["mutatesSession"] = tool->definition.annotations.mutatesSession;
            op["destructive"] = tool->definition.annotations.destructive;
            op["idempotent"] = tool->definition.annotations.idempotent;
            op["expensive"] = tool->definition.annotations.expensive;
            op["reversible"] = tool->definition.annotations.undoable;
            op["requiresMainThread"] = tool->definition.annotations.requiresMainThread;
            operations.push_back(std::move(op));
        }
        return operations;
    };
    nlohmann::json domains = nlohmann::json::array();
    std::vector<std::string> described;
    for (const DomainCapability& domain : kDomains) {
        described.push_back(domain.id);
        nlohmann::json j;
        j["id"] = domain.id;
        j["title"] = domain.title;
        if (!domain.description.empty()) {
            j["description"] = domain.description;
        }
        nlohmann::json operations = operationsIn(domain.id);
        // ADR-2150: "supported" means THIS BUILD can do it (it has tools in the domain). Whether it can be done
        // in this session (available: the host hook, the GPU, the audio) and whether this client may (permitted)
        // are separate answers, given per tool by the session (session.info), never inferred from this one.
        const bool supported = !operations.empty() || domain.available;
        j["supported"] = supported;
        if (!domain.unavailableReason.empty()) {
            j[supported ? "limits" : "unsupportedReason"] = domain.unavailableReason;
        }
        if (!operations.empty()) {
            j["operations"] = std::move(operations);
        }
        domains.push_back(std::move(j));
    }
    // A domain a tool names that nobody wrote prose for is still a domain: listed, not dropped.
    for (const Tool* tool : registry.all()) {
        const std::string id(tool->definition.domain());
        if (std::find(described.begin(), described.end(), id) != described.end()) {
            continue;
        }
        described.push_back(id);
        domains.push_back({{"id", id}, {"title", id}, {"supported", true}, {"operations", operationsIn(id)}});
    }

    nlohmann::json present;
    present["composition"] = mutableEngine.composition() != nullptr;
    present["nodes"] =
        mutableEngine.composition() != nullptr ? mutableEngine.composition()->nodes().size() : 0;
    present["audio"] = engine.hasAudio();
    present["analysis"] = engine.hasFrame();
    present["lights"] = engine.scene().lights.size();
    present["timelineTracks"] = mutableEngine.timeline().tracks().size();
    present["modulationRoutes"] = mutableEngine.modulator().routes().size();
    present["lightRig"] = std::any_of(mutableEngine.params().ordered().begin(),
                                      mutableEngine.params().ordered().end(),
                                      [](const params::IParameter* p) {
                                          return p->group() == "lightrig";
                                      });
    present["sky"] = mutableEngine.params().find("env/sky/enabled") != nullptr;
    present["fog"] = mutableEngine.params().find("scene/volumeDensity") != nullptr; // ADR-705: the one density

    nlohmann::json out;
    out["engine"] = "AV Gen";
    out["version"] = api::version().toJson(); // ADR-2150: the same identity as avgen --version
    // ADR-2152: what this build can parse, generated (api::featureList). Supported, not available: a
    // feature here is one the parser reads, which says nothing about this session's GPU, audio or files.
    out["features"] = api::featureList();
    out["domains"] = std::move(domains);
    out["thisSession"] = std::move(present);
    out["transaction"] =
        "AI changes run inside one transaction per task. A task that fails or is cancelled is rolled "
        "back to a snapshot of the project taken before its first change. A task that succeeds is "
        "ONE entry on the editor's undo history, covering parameter values, the sequence, the "
        "camera collection and camera track, timeline keys, modulation routes and added nodes, so "
        "the user undoes it with the same Cmd+Z as their own edits. A node a task deletes cannot "
        "be brought back by that undo.";
    return out;
}

} // namespace avgen::ai
