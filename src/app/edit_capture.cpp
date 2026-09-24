#include "app/edit_capture.hpp"

#include "app/engine.hpp"
#include "params/serialization.hpp"
#include "scene/composition.hpp"

#include <algorithm>
#include <memory>
#include <utility>

namespace avgen::app {
namespace {

// Bases rather than finals: a final carries this frame's modulation and automation, and an undo
// that wrote those back would bake a moment of an LFO into the document.
std::unordered_map<std::string, std::vector<float>> captureBases(Engine& engine) {
    std::unordered_map<std::string, std::vector<float>> out;
    const params::ParameterSet& params = engine.params();
    out.reserve(params.size());
    for (const params::IParameter* parameter : params.ordered()) {
        if (parameter == nullptr) {
            continue;
        }
        std::vector<float> components(parameter->componentCount());
        for (std::size_t i = 0; i < components.size(); ++i) {
            components[i] = parameter->baseComponent(i);
        }
        out.emplace(parameter->path(), std::move(components));
    }
    return out;
}

nlohmann::json routesJson(const Engine& engine) {
    nlohmann::json out = nlohmann::json::array();
    for (const params::ModRoute& route : const_cast<Engine&>(engine).modulator().routes()) {
        out.push_back(params::routeToJson(route));
    }
    return out;
}

std::map<std::string, std::string> captureParents(Engine& engine) {
    std::map<std::string, std::string> out;
    if (const scene::Composition* comp = engine.composition(); comp != nullptr) {
        for (const auto& node : comp->nodes()) {
            if (node != nullptr) {
                out.emplace(node->name, node->parent);
            }
        }
    }
    return out;
}

} // namespace

void EditCapture::begin(Engine& engine) {
    bases_ = captureBases(engine);
    sequence_ = engine.sequence();
    sequenceJson_ = sequence_.toJson();
    cameras_ = ui::capturedCameraDirection(engine);
    rawCameras_ = engine.composition() != nullptr ? engine.composition()->cameraDirection() : scene::CameraDirection{};
    sequenceTargets_ = engine.sequenceTargets();
    // Unbound at once: the copy shares the live tracks' `IParameter*`, and an operation that
    // removes a parameter (a camera, a node) frees what a bound copy would still point at.
    timeline_ = engine.timeline();
    timeline_.unbind();
    timelineJson_ = timeline_.toJson();
    routes_ = engine.modulator().routes();
    routesJson_ = routesJson(engine);
    parents_ = captureParents(engine);
    plans_ = engine.directingPlans();
    unrecoverable_.clear();
    open_ = true;
}

void EditCapture::cancel() {
    open_ = false;
    bases_.clear();
    unrecoverable_.clear();
}

ui::EditCommand EditCapture::finish(Engine& engine, std::string label) {
    ui::EditCommand command(std::move(label));
    if (!open_) {
        return ui::EditCommand{};
    }
    open_ = false;

    // ---- nodes: added ones are live (undo detaches them); destroyed ones are reported ----------
    const std::map<std::string, std::string> parentsNow = captureParents(engine);
    for (const auto& [name, parent] : parentsNow) {
        const auto was = parents_.find(name);
        if (was == parents_.end()) {
            command.added.emplace_back(name);
        } else if (was->second != parent) {
            command.parents.push_back(ui::ParentChange{name, was->second, parent});
        }
    }
    unrecoverable_.clear();
    for (const auto& [name, parent] : parents_) {
        if (!parentsNow.contains(name)) {
            unrecoverable_.push_back(name);
        }
    }

    // ---- the Director Plans (ADR-755) ---------------------------------------------------------
    if (engine.directingPlans() != plans_) {
        auto change = std::make_unique<ui::PlanListChange>();
        change->before = std::move(plans_);
        change->after = engine.directingPlans();
        command.plans = std::move(change);
    }

    // ---- the author's automation --------------------------------------------------------------
    const nlohmann::json timelineNow = engine.timeline().toJson();
    const nlohmann::json routesNow = routesJson(engine);
    const bool timelineMoved = timelineNow != timelineJson_;
    const bool routesMoved = routesNow != routesJson_;

    // ---- the camera collection ----------------------------------------------------------------
    // Decided on the collection as authored, recorded with the bases captured into it. A camera
    // that only MOVED is a parameter edit (its `cameras/<slug>/position` base, below) and must not
    // also be recorded as a new collection.
    const scene::CameraDirection* rawNow =
        engine.composition() != nullptr ? &engine.composition()->cameraDirection() : nullptr;
    if (rawNow != nullptr && !(*rawNow == rawCameras_)) {
        auto change = std::make_unique<ui::CameraDirectionChange>();
        change->before = std::move(cameras_);
        change->after = ui::capturedCameraDirection(engine);
        command.cameras = std::move(change);
    }

    // ---- the sequence -------------------------------------------------------------------------
    // Compared by document: `seq::Sequence` has no `operator==`, and its JSON is the definition of
    // what a save keeps, which is the equality that matters for an undo.
    if (engine.sequence().toJson() != sequenceJson_) {
        auto change = std::make_unique<ui::TimelineChange>();
        change->before = std::move(sequence_);
        change->after = engine.sequence();
        command.timeline = std::move(change);
    }

    // The timeline is recorded when the AUTHOR's part of it moved. A sequence install rewrites the
    // tracks the sequence owns, and those come back with the sequence record; recording the whole
    // timeline for them as well would be harmless but would make every sequence edit look like a
    // key edit. So the sequence-owned targets are masked out of the comparison, not the record.
    if (timelineMoved && !command.timeline) {
        command.automation = std::make_unique<ui::AutomationChange>();
    } else if (timelineMoved) {
        std::vector<std::string> owned = sequenceTargets_;
        owned.insert(owned.end(), engine.sequenceTargets().begin(), engine.sequenceTargets().end());
        const auto authored = [&](const nlohmann::json& doc) {
            nlohmann::json out = doc;
            if (auto tracks = out.find("tracks"); tracks != out.end() && tracks->is_array()) {
                nlohmann::json kept = nlohmann::json::array();
                for (const auto& track : *tracks) {
                    const std::string target = track.value("target", std::string{});
                    if (std::find(owned.begin(), owned.end(), target) == owned.end()) {
                        kept.push_back(track);
                    }
                }
                *tracks = std::move(kept);
            }
            return out;
        };
        if (authored(timelineNow) != authored(timelineJson_)) {
            command.automation = std::make_unique<ui::AutomationChange>();
        }
    }
    if (routesMoved && !command.automation) {
        command.automation = std::make_unique<ui::AutomationChange>();
    }
    if (command.automation) {
        command.automation->before = std::move(timeline_);
        command.automation->after = engine.timeline();
        command.automation->after.unbind();
        command.automation->routesTouched = routesMoved;
        if (routesMoved) {
            command.automation->routesBefore = std::move(routes_);
            command.automation->routesAfter = engine.modulator().routes();
        }
    }

    // ---- parameter bases, last: only paths that existed at both ends ---------------------------
    for (const params::IParameter* parameter : engine.params().ordered()) {
        if (parameter == nullptr) {
            continue;
        }
        const auto was = bases_.find(std::string(parameter->path()));
        if (was == bases_.end()) {
            continue; // registered during the operation; its record (a camera, a node) owns it
        }
        std::vector<float> now(parameter->componentCount());
        for (std::size_t i = 0; i < now.size(); ++i) {
            now[i] = parameter->baseComponent(i);
        }
        if (now.size() != was->second.size() || now == was->second) {
            continue;
        }
        command.params.push_back(ui::ParamChange{std::string(parameter->path()), was->second, std::move(now)});
    }
    bases_.clear();
    return command;
}

} // namespace avgen::app
