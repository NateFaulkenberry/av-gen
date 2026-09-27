// avgen_cast_trace -- where every character is, at every moment of a film: the call sheet a
// cinematographer needs before placing a camera on somebody who is not following a script.
//
//   avgen_cast_trace (--project p.json | --scene s.scene.json)
//                    [--seconds N] [--start S] [--fps F] [--hz H] [--nodes a,b,...] [--camera]
//                    [--out file.json] [--plan plan.json] [--save-project out.json]
//
//   --project P   load a project through `Engine::loadProject`, the path the application opens
//   --scene S     load a bare composition through `Engine::loadComposition`
//   --seconds N   simulated seconds (default 60)
//   --start S     seek to S first, as a render of a range does, and trace from there (default 0)
//   --fps F       the simulation's step, which must be the render's frame rate (default 60)
//   --hz H        samples written per second (default 10)
//   --nodes LIST  composition nodes to trace besides the entities, by name (a hero, a camera rig)
//   --camera      also write the camera's pose at EVERY frame (the render's fps, not --hz)
//   --out FILE    write the JSON there (default stdout)
//   --plan P      compile a Director Plan into the loaded project first, exactly as `avgen --plan`
//                 does (ADR-929), so a plan's set pieces can be traced without a GPU
//   --save-project OUT   write the project with the plan installed, before tracing it
//
// ## Set pieces (ADR-929)
//
// Every staging scenario that is a set piece (`setpiece/<id>`) gets an entry under "setPieces": when
// each of its beats was actually entered (its world events), where the craft was at each, which
// animals it bound and where each was when the lift began and when it was retired, and -- the
// invariant ADR-385 exists for -- how fast the craft moved, and how far it drifted from its station,
// while the beam was rising and the animals were lifted. A plan's nominal times are the request;
// these are what the film did.
//
// Autonomous characters decide where they go, so the only way to know where an alien will be at
// 2:30 is to run the film to 2:30. This runs the real `app::Engine` in Offline mode -- the same
// Director performances, staging, parameter overrides and music events the render sees -- and
// writes each entity's position, facing, speed, activity and visibility at a fixed sample rate.
//
// Two conditions make the trace describe the render rather than some other film:
//
//   * **The step is the render's.** An entity integrates at 1/fps (`EntityWorld::seekExact` steps
//     the same grid), so a 30 fps trace of a film rendered at 60 is a different simulation, and for
//     a character that decides, a different path. Trace at the fps you render at.
//   * **Entity distance culling is off,** as it is in an offline render (`DetailLimits::
//     offlineDefault`). The live viewport culls far bodies; a trace that culled would describe the
//     viewport.
//
// ---- the camera track (`--camera`, ADR-911) ----------------------------------------------------
//
// The frame's camera, read after `Engine::update` -- resolved, evaluated, shaken and framed, on the
// lens the frame is projected with -- so camera stability can be measured without rendering, by
// anything that reads JSON. One entry per frame, at the render's fps, under `"camera"`:
//
//   "camera": {
//     "fps": 60,
//     "t":           [s, ...],          the frame's instant
//     "eye":         [[x, y, z], ...],  metres, world; 10 um resolution
//     "target":      [[x, y, z], ...],  the point the camera looks at
//     "vfov":        [deg, ...],        the vertical field of view the frame is projected with
//     "shot":        [i, ...],          index into the scene's cameraDirection.shots of the shot on
//                                       screen; -1 when an event or the default camera has the frame
//     "camera":      ["slug", ...],     the active camera's parameter slug; "" is the main camera
//     "reason":      ["shot", ...],     why it has the frame: "shot", "event" or "default"
//     "blend":       [b, ...],          1 unless a blend is in progress (0 = all the outgoing camera)
//     "aimPoint":    [[x, y, z] | null, ...]   the active camera's aim node, raw, plus its aim offset:
//                                       the subject the shot frames, before any smoothing. null when
//                                       the active camera aims at no node
//     "followPoint": [[x, y, z] | null, ...]   the active camera's follow node, raw. null when none
//   }
//
// "Raw" means the node's world transform as the simulation left it -- stride bob included -- so
// `aimPoint` projected through (eye, target, vfov) is where the subject sits in frame, and the eye's
// travel against `followPoint`'s is how far the camera moves for the distance its subject covers.
//
// It traces; it does not judge. Exit status: 0 when the file was written, 1 on a load failure or
// bad arguments.

#include "app/directing_plan_file.hpp"
#include "app/engine.hpp"
#include "core/log.hpp"
#include "core/time.hpp"
#include "entity/entity.hpp"
#include "entity/locomotion.hpp"
#include "params/parameter_set.hpp"
#include "scene/camera_rig.hpp"
#include "scene/composition.hpp"
#include "scene/detail_limits.hpp"
#include "stage/setpiece.hpp"
#include "stage/staging.hpp"

#include <fmt/format.h>
#include <nlohmann/json.hpp>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

using namespace avgen;
namespace fs = std::filesystem;

namespace {

struct Options {
    fs::path project;
    fs::path scene;
    double seconds = 60.0;
    double start = 0.0;
    double fps = 60.0;
    double hz = 10.0;
    std::vector<std::string> nodes;
    bool camera = false;
    fs::path out;
    fs::path plan;
    fs::path saveProject;
};

void usage() {
    std::fprintf(stderr,
                 "usage: avgen_cast_trace (--project p.json | --scene s.scene.json)\n"
                 "                        [--seconds N] [--start S] [--fps F] [--hz H] [--nodes a,b,...]\n"
                 "                        [--camera] [--out file.json] [--plan plan.json]\n"
                 "                        [--save-project out.json]\n");
}

std::vector<std::string> splitList(const char* text) {
    std::vector<std::string> out;
    std::stringstream stream(text);
    std::string item;
    while (std::getline(stream, item, ',')) {
        if (!item.empty()) {
            out.push_back(item);
        }
    }
    return out;
}

bool parse(int argc, char** argv, Options& o) {
    for (int i = 1; i < argc; ++i) {
        const char* a = argv[i];
        const bool hasValue = i + 1 < argc;
        if (std::strcmp(a, "--project") == 0 && hasValue) {
            o.project = argv[++i];
        } else if (std::strcmp(a, "--scene") == 0 && hasValue) {
            o.scene = argv[++i];
        } else if (std::strcmp(a, "--seconds") == 0 && hasValue) {
            o.seconds = std::atof(argv[++i]);
        } else if (std::strcmp(a, "--start") == 0 && hasValue) {
            o.start = std::atof(argv[++i]);
        } else if (std::strcmp(a, "--fps") == 0 && hasValue) {
            o.fps = std::atof(argv[++i]);
        } else if (std::strcmp(a, "--hz") == 0 && hasValue) {
            o.hz = std::atof(argv[++i]);
        } else if (std::strcmp(a, "--nodes") == 0 && hasValue) {
            o.nodes = splitList(argv[++i]);
        } else if (std::strcmp(a, "--camera") == 0) {
            o.camera = true;
        } else if (std::strcmp(a, "--out") == 0 && hasValue) {
            o.out = argv[++i];
        } else if (std::strcmp(a, "--plan") == 0 && hasValue) {
            o.plan = argv[++i];
        } else if (std::strcmp(a, "--save-project") == 0 && hasValue) {
            o.saveProject = argv[++i];
        } else {
            std::fprintf(stderr, "unknown or incomplete argument: %s\n", a);
            return false;
        }
    }
    if (o.project.empty() == o.scene.empty()) {
        std::fprintf(stderr, "exactly one of --project and --scene is required\n");
        return false;
    }
    if (!(o.seconds > 0.0) || !(o.fps > 0.0) || !(o.hz > 0.0) || o.hz > o.fps) {
        std::fprintf(stderr, "--seconds, --fps and --hz must be positive, and --hz no more than --fps\n");
        return false;
    }
    if (!(o.start >= 0.0)) {
        std::fprintf(stderr, "--start must not be negative\n");
        return false;
    }
    return true;
}

// Three decimals: a millimetre is finer than any framing decision, and the file stays readable.
double rounded(double v) { return std::round(v * 1000.0) / 1000.0; }

nlohmann::json vec3(const glm::vec3& v) { return {rounded(v.x), rounded(v.y), rounded(v.z)}; }

// The camera track is finer: a tenth of a degree at five metres is 9 mm, and the stability metrics
// read residuals far below that. Ten micrometres keeps the rounding out of every measurement.
double fine(double v) { return std::round(v * 100000.0) / 100000.0; }

nlohmann::json fineVec3(const glm::vec3& v) { return {fine(v.x), fine(v.y), fine(v.z)}; }

struct CameraTrack {
    nlohmann::json t = nlohmann::json::array();
    nlohmann::json eye = nlohmann::json::array();
    nlohmann::json target = nlohmann::json::array();
    nlohmann::json vfov = nlohmann::json::array();
    nlohmann::json shot = nlohmann::json::array();
    nlohmann::json camera = nlohmann::json::array();
    nlohmann::json reason = nlohmann::json::array();
    nlohmann::json blend = nlohmann::json::array();
    nlohmann::json aimPoint = nlohmann::json::array();
    nlohmann::json followPoint = nlohmann::json::array();
};

// A keyable offset's final value: what this frame's timeline and routes made of it.
glm::vec3 channelFinal(const params::ParameterSet& params, const std::string& path, glm::vec3 fallback) {
    const params::IParameter* p = params.find(path);
    if (p == nullptr) {
        return fallback;
    }
    return glm::vec3(p->finalComponent(0), p->finalComponent(1), p->finalComponent(2));
}

void recordCamera(CameraTrack& track, const app::Engine& engine, const scene::Composition& composition,
                  const params::ParameterSet& params, double seconds) {
    const scene::Camera& camera = composition.scene().camera;
    const scene::ActiveCameraState active = engine.activeCamera();
    const scene::CameraDirection& direction = composition.cameraDirection();
    // The shot on screen, by the resolver's own rule: the last shot naming the active camera whose
    // span began when the claim did and contains the instant.
    int shot = -1;
    if (active.reason == scene::ActiveCameraReason::Shot) {
        for (std::size_t i = 0; i < direction.shots.size(); ++i) {
            const scene::CameraShot& s = direction.shots[i];
            if (s.camera == active.camera && s.startSeconds == active.sinceSeconds && s.contains(seconds)) {
                shot = static_cast<int>(i);
            }
        }
    }
    const scene::CameraRig* rig = direction.find(active.camera);
    nlohmann::json aimPoint = nullptr;
    nlohmann::json followPoint = nullptr;
    if (rig != nullptr && rig->id != scene::kMainCamera) {
        const std::string prefix = "cameras/" + rig->slug + "/";
        if (!rig->aimNode.empty()) {
            if (const scene::CompositionNode* node = composition.findNode(rig->aimNode); node != nullptr) {
                const glm::vec3 offset = channelFinal(params, prefix + "aimOffset", rig->aimOffset);
                aimPoint = fineVec3(composition.nodeWorldTransform(*node).position + offset);
            }
        }
        if (!rig->followNode.empty()) {
            if (const scene::CompositionNode* node = composition.findNode(rig->followNode); node != nullptr) {
                followPoint = fineVec3(composition.nodeWorldTransform(*node).position);
            }
        }
    }
    track.t.push_back(std::round(seconds * 1e6) / 1e6);
    track.eye.push_back(fineVec3(camera.position));
    track.target.push_back(fineVec3(camera.target));
    track.vfov.push_back(std::round(static_cast<double>(glm::degrees(camera.effectiveFovY())) * 1e4) / 1e4);
    track.shot.push_back(shot);
    track.camera.push_back(rig != nullptr ? rig->slug : std::string());
    track.reason.push_back(scene::activeCameraReasonName(active.reason));
    track.blend.push_back(std::round(static_cast<double>(active.blend) * 1e4) / 1e4);
    track.aimPoint.push_back(std::move(aimPoint));
    track.followPoint.push_back(std::move(followPoint));
}

// What one set piece actually did (ADR-929), assembled from the director's own events frame by frame.
struct SetPieceTrace {
    std::string scenario;
    std::string actor;  // the staging actor
    std::string craft;  // the entity it drives
    std::string beam;   // the craft's `beam` part, if it has one
    std::vector<std::pair<std::string, double>> beats; // beat -> the instant it was entered, in order
    std::map<std::string, glm::vec3> craftAt;          // beat -> where the craft was then
    std::optional<double> finished;
    std::string ended; // "finished" | "cancelled"
    struct Animal {
        std::string role;
        std::string entity;
        double bound = -1.0;
        double lifted = -1.0;
        double retired = -1.0;
        glm::vec3 atLift{0.0f};
        glm::vec3 atRetire{0.0f};
    };
    std::vector<Animal> animals;
    std::vector<std::string> failures; // steps that failed, with why
    // While the beam rises and while the animals are lifted: the craft's fastest frame-to-frame speed
    // and its farthest horizontal drift from where it stood as that beat began.
    bool holding = false;
    glm::vec3 station{0.0f};
    float holdMaxSpeed = 0.0f;
    float holdMaxDrift = 0.0f;
    int holdFrames = 0;
};

struct Track {
    nlohmann::json t = nlohmann::json::array();
    nlohmann::json position = nlohmann::json::array();
    nlohmann::json yaw = nlohmann::json::array();
    nlohmann::json speed = nlohmann::json::array();
    nlohmann::json activity = nlohmann::json::array();
    nlohmann::json visible = nlohmann::json::array();
};

} // namespace

int main(int argc, char** argv) {
    Options o;
    if (!parse(argc, argv, o)) {
        usage();
        return 1;
    }
    // The JSON may go to stdout, so the engine's chatter goes to stderr and only warnings reach it.
    log::init(log::Level::Warn);

    app::Engine engine(app::EngineMode::Offline);
    const auto loaded = o.project.empty() ? engine.loadComposition(o.scene) : engine.loadProject(o.project);
    if (!loaded) {
        std::fprintf(stderr, "load failed: %s\n", loaded.error().message.c_str());
        return 1;
    }
    scene::Composition* composition = engine.composition();
    if (composition == nullptr) {
        std::fprintf(stderr, "the loaded document is not a composition; there is no cast to trace\n");
        return 1;
    }
    // ADR-929: the plan first, installed exactly as `avgen --plan` installs it.
    nlohmann::json planReport;
    if (!o.plan.empty()) {
        auto applied = app::applyPlanFile(engine, o.plan);
        if (!applied) {
            std::fprintf(stderr, "plan: %s\n", applied.error().message.c_str());
            return 1;
        }
        planReport = applied->toJson();
        if (!applied->blocked.empty()) {
            std::fprintf(stderr, "plan: %zu item(s) could not be built; see the report's \"blocked\" and \"issues\"\n",
                         applied->blocked.size());
        }
        if (!o.saveProject.empty()) {
            if (auto saved = engine.saveProject(o.saveProject); !saved) {
                std::fprintf(stderr, "save: %s\n", saved.error().message.c_str());
                return 1;
            }
        }
        // The film is traced from its first frame, as a render of the saved project would play it.
        engine.seekSeconds(0.0);
    } else if (!o.saveProject.empty()) {
        std::fprintf(stderr, "--save-project is for writing the project a --plan compiled into\n");
        return 1;
    }

    // Through the engine, not the scene: `Engine::update` writes its own limits over the scene's at
    // the top of every frame (ADR-186), which is why `avgen_character_quality` checks it took.
    scene::DetailLimits limits = engine.detailLimits();
    limits.entityDistanceCull = false;
    engine.setDetailLimits(limits);

    for (const std::string& name : o.nodes) {
        if (composition->findNode(name) == nullptr) {
            std::fprintf(stderr, "no composition node named '%s'\n", name.c_str());
            return 1;
        }
    }

    // The application's frame convention: the first frame is t = 0 with a zero delta, every later
    // one a full step (`tests/support/project_round_trip.hpp` `stepFrames`). With `--start`, the
    // engine is seeked there first and the first frame after it has a zero delta, as a render of a
    // range opens (`RenderJob`): the ADR-700 replay has already simulated up to that instant.
    const double dt = 1.0 / o.fps;
    const auto first = static_cast<std::uint64_t>(std::llround(o.start * o.fps));
    const auto frames = static_cast<std::uint64_t>(std::llround(o.seconds * o.fps)) + 1;
    const auto every = std::max<std::uint64_t>(1, static_cast<std::uint64_t>(std::llround(o.fps / o.hz)));
    std::map<std::string, Track> entities;
    std::map<std::string, Track> nodes;
    CameraTrack cameraTrack;
    params::ParameterSet& params = engine.params();
    if (first > 0) {
        engine.seekSeconds(static_cast<double>(first) * dt);
    }

    // ADR-929: every set piece in the staging, and the entities its craft and beam are.
    std::vector<SetPieceTrace> pieces;
    for (const stage::ScenarioDesc& scenario : composition->staging().scenarios) {
        if (!stage::isSetPieceScenario(scenario.name)) {
            continue;
        }
        SetPieceTrace piece;
        piece.scenario = scenario.name;
        piece.actor = scenario.actor;
        for (const stage::ActorDesc& actor : composition->staging().actors) {
            if (actor.name == scenario.actor) {
                piece.craft = actor.driven();
                for (const stage::ActorPart& part : actor.parts) {
                    if (part.name == "beam") {
                        piece.beam = part.entity;
                    }
                }
            }
        }
        pieces.push_back(std::move(piece));
    }
    std::map<std::string, glm::vec3> lastCraft; // craft entity -> last frame's simulated position

    for (std::uint64_t i = first; i < first + frames; ++i) {
        FrameTime time;
        time.renderTime = static_cast<double>(i) * dt;
        time.deltaTime = i == first ? 0.0 : dt;
        time.frameIndex = i;
        engine.update(time);
        if (o.camera) {
            recordCamera(cameraTrack, engine, *composition, params, time.renderTime);
        }
        if (i == first && composition->scene().detailLimits.entityDistanceCull) {
            std::fprintf(stderr, "entity distance cull is still in force after the first update; "
                                 "the trace would describe the viewport, not the render\n");
            return 1;
        }
        // ---- set pieces, every frame: the director's events, and the craft while it holds ----
        const entity::EntityWorld& world = composition->entityWorld();
        const auto where = [&](const std::string& name) {
            const entity::Entity* e = world.find(name);
            return e != nullptr ? e->state().position() : glm::vec3(0.0f);
        };
        for (const stage::StageEvent& e : composition->director().events()) {
            for (SetPieceTrace& piece : pieces) {
                if (piece.scenario != e.scenario) {
                    continue;
                }
                switch (e.kind) {
                case stage::StageEventKind::Beat:
                    piece.beats.emplace_back(e.beat, time.renderTime);
                    piece.craftAt.emplace(e.beat, where(piece.craft));
                    if (e.beat == "beam" || e.beat == "lift") {
                        piece.holding = true;
                        piece.station = where(piece.craft);
                    } else {
                        piece.holding = false;
                    }
                    if (e.beat == "lift") {
                        for (SetPieceTrace::Animal& a : piece.animals) {
                            a.lifted = time.renderTime;
                            a.atLift = where(a.entity);
                        }
                    }
                    break;
                case stage::StageEventKind::Bound:
                    if (e.role.rfind("target", 0) == 0) {
                        piece.animals.push_back(SetPieceTrace::Animal{e.role, e.detail, time.renderTime});
                    }
                    break;
                case stage::StageEventKind::Retired:
                    for (SetPieceTrace::Animal& a : piece.animals) {
                        if (a.entity == e.detail) {
                            a.retired = time.renderTime;
                            a.atRetire = where(a.entity);
                        }
                    }
                    break;
                case stage::StageEventKind::StepFailed:
                    piece.failures.push_back(fmt::format("{:.3f}s {} {} {}: {}", time.renderTime, e.beat, e.role, e.step,
                                                         e.detail));
                    break;
                case stage::StageEventKind::Finished:
                case stage::StageEventKind::Cancelled:
                    piece.finished = time.renderTime;
                    piece.ended = e.kind == stage::StageEventKind::Finished ? "finished" : "cancelled";
                    piece.holding = false;
                    break;
                default: break;
                }
            }
        }
        for (SetPieceTrace& piece : pieces) {
            if (piece.craft.empty()) {
                continue;
            }
            const glm::vec3 now = where(piece.craft);
            const auto last = lastCraft.find(piece.craft);
            if (piece.holding && last != lastCraft.end() && time.deltaTime > 0.0) {
                const float speed = glm::length(now - last->second) / static_cast<float>(time.deltaTime);
                piece.holdMaxSpeed = std::max(piece.holdMaxSpeed, speed);
                piece.holdMaxDrift =
                    std::max(piece.holdMaxDrift, glm::length(glm::vec2(now.x - piece.station.x, now.z - piece.station.z)));
                ++piece.holdFrames;
            }
        }
        for (const SetPieceTrace& piece : pieces) {
            if (!piece.craft.empty()) {
                lastCraft[piece.craft] = where(piece.craft);
            }
        }

        if (i % every != 0) {
            continue;
        }
        for (const auto& owned : composition->entityWorld().entities()) {
            const entity::Entity& e = *owned;
            const entity::LocomotionState& loco = e.locomotion();
            Track& track = entities[e.name()];
            track.t.push_back(rounded(time.renderTime));
            track.position.push_back(vec3(loco.position));
            track.yaw.push_back(rounded(loco.yaw));
            track.speed.push_back(rounded(loco.speed));
            track.activity.push_back(entity::activityName(loco.activity));
            const params::IParameter* visible = params.find("nodes/" + e.desc().driven() + "/visible");
            track.visible.push_back(visible == nullptr || visible->finalComponent(0) > 0.5f);
        }
        for (const std::string& name : o.nodes) {
            const scene::CompositionNode* node = composition->findNode(name);
            const scene::Transform world = composition->nodeWorldTransform(*node);
            Track& track = nodes[name];
            track.t.push_back(rounded(time.renderTime));
            track.position.push_back(vec3(world.position));
            const params::IParameter* visible = params.find("nodes/" + name + "/visible");
            track.visible.push_back(visible == nullptr || visible->finalComponent(0) > 0.5f);
        }
    }

    nlohmann::json doc;
    doc["source"] = o.project.empty() ? o.scene.string() : o.project.string();
    doc["fps"] = o.fps;
    doc["hz"] = o.fps / static_cast<double>(every);
    doc["seconds"] = o.seconds;
    doc["start"] = static_cast<double>(first) * dt;
    for (auto& [name, track] : entities) {
        doc["entities"][name] = {{"t", std::move(track.t)},           {"position", std::move(track.position)},
                                 {"yaw", std::move(track.yaw)},       {"speed", std::move(track.speed)},
                                 {"activity", std::move(track.activity)}, {"visible", std::move(track.visible)}};
    }
    for (auto& [name, track] : nodes) {
        doc["nodes"][name] = {{"t", std::move(track.t)},
                              {"position", std::move(track.position)},
                              {"visible", std::move(track.visible)}};
    }
    if (o.camera) {
        doc["camera"] = {{"fps", o.fps},
                         {"t", std::move(cameraTrack.t)},
                         {"eye", std::move(cameraTrack.eye)},
                         {"target", std::move(cameraTrack.target)},
                         {"vfov", std::move(cameraTrack.vfov)},
                         {"shot", std::move(cameraTrack.shot)},
                         {"camera", std::move(cameraTrack.camera)},
                         {"reason", std::move(cameraTrack.reason)},
                         {"blend", std::move(cameraTrack.blend)},
                         {"aimPoint", std::move(cameraTrack.aimPoint)},
                         {"followPoint", std::move(cameraTrack.followPoint)}};
    }

    // ADR-929: what each set piece did.
    if (!pieces.empty()) {
        nlohmann::json list = nlohmann::json::array();
        for (const SetPieceTrace& piece : pieces) {
            nlohmann::json beats = nlohmann::json::object();
            nlohmann::json craftAt = nlohmann::json::object();
            for (const auto& [beat, t] : piece.beats) {
                if (!beats.contains(beat)) {
                    beats[beat] = rounded(t);
                }
            }
            for (const auto& [beat, p] : piece.craftAt) {
                craftAt[beat] = vec3(p);
            }
            nlohmann::json animals = nlohmann::json::array();
            for (const SetPieceTrace::Animal& a : piece.animals) {
                nlohmann::json one{{"role", a.role}, {"entity", a.entity}, {"bound", rounded(a.bound)}};
                if (a.lifted >= 0.0) {
                    one["lifted"] = rounded(a.lifted);
                    one["atLift"] = vec3(a.atLift);
                }
                if (a.retired >= 0.0) {
                    one["retired"] = rounded(a.retired);
                    one["atRetire"] = vec3(a.atRetire);
                }
                animals.push_back(std::move(one));
            }
            nlohmann::json one{{"id", stage::setPieceIdOf(piece.scenario)},
                               {"scenario", piece.scenario},
                               {"actor", piece.actor},
                               {"craft", piece.craft},
                               {"beats", std::move(beats)},
                               {"craftAt", std::move(craftAt)},
                               {"animals", std::move(animals)},
                               // The craft while the beam rose and the animals were lifted.
                               {"holding", {{"frames", piece.holdFrames},
                                            {"maxSpeed", rounded(piece.holdMaxSpeed)},
                                            {"maxDrift", rounded(piece.holdMaxDrift)}}}};
            if (piece.finished) {
                one["finished"] = rounded(*piece.finished);
                one["ended"] = piece.ended;
            }
            if (!piece.failures.empty()) {
                one["failures"] = piece.failures;
            }
            list.push_back(std::move(one));
        }
        doc["setPieces"] = std::move(list);
    }
    if (!planReport.is_null()) {
        doc["plan"] = std::move(planReport);
    }

    const std::string text = doc.dump();
    if (o.out.empty()) {
        std::fwrite(text.data(), 1, text.size(), stdout);
        std::fputc('\n', stdout);
    } else {
        std::ofstream file(o.out);
        file << text << '\n';
        if (!file) {
            std::fprintf(stderr, "could not write %s\n", o.out.string().c_str());
            return 1;
        }
    }
    return 0;
}
