#include "ai/capabilities.hpp"

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
    {"entity", "Creating and deleting objects", {}, false,
     "Not exposed in this pass. Composition node structure is outside the snapshot the AI "
     "transaction restores, so a created or deleted node would survive a rollback. The editor's "
     "undo system is being built concurrently; once the AI transaction is backed by it rather than "
     "by parameter snapshots, node creation and deletion can be added. Nodes that already exist "
     "can be moved, hidden and shown."},
    {"terrain", "Terrain", {}, false,
     "Not exposed in this pass. Terrain generation is being reworked concurrently and its "
     "parameter surface is in flux. The LOD and view-distance knobs on an existing terrain node "
     "are reachable through parameter.set today."},
    {"water", "Water", {}, false,
     "Not exposed in this pass. Water bodies and their rendering are being reworked concurrently. "
     "Whatever water parameters the current scene registers are visible to parameter.search."},
    {"navigation", "Navigation and character behaviour", {}, false,
     "Not exposed in this pass. The navigation and entity-behaviour surfaces are being reworked "
     "concurrently."},
    {"light-editing", "Editing individual lights", {}, false,
     "Lights are regenerated from the scene description on every rebuild, so a direct write to one "
     "would be discarded. Lighting is changed through the lightrig/*, scene/keyLight and "
     "env/sky/sunIntensity parameters. Exposing per-light editing would need each light in the "
     "flattened scene to register parameters the way a light rig's lights already do."},
    {"render", "Rendering to a file", {}, false,
     "Not exposed. Writing files is an external side effect and, per the security model, needs a "
     "separately designed capability with explicit consent rather than a tool."},
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

    nlohmann::json domains = nlohmann::json::array();
    for (const DomainCapability& domain : kDomains) {
        nlohmann::json j;
        j["id"] = domain.id;
        j["title"] = domain.title;
        if (!domain.description.empty()) {
            j["description"] = domain.description;
        }
        j["available"] = domain.available;
        if (!domain.available) {
            j["unavailableReason"] = domain.unavailableReason;
        }
        nlohmann::json operations = nlohmann::json::array();
        for (const Tool* tool : registry.all()) {
            if (tool->definition.domain() != domain.id) {
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
        if (!operations.empty()) {
            j["operations"] = std::move(operations);
        }
        domains.push_back(std::move(j));
    }

    // What is only knowable by asking this session. An agent told "materials are supported" when
    // the open scene is the built-in orb, which has no procedural parts, would waste a turn
    // finding that out.
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
    out["domains"] = std::move(domains);
    out["thisSession"] = std::move(present);
    out["transaction"] =
        "AI changes run inside one transaction per task. The rollback primitive is a snapshot of "
        "the parameter domain -- parameter values, modulation routes, presets and the timeline -- "
        "which is exactly the set of things the tools above can change.";
    return out;
}

} // namespace avgen::ai
