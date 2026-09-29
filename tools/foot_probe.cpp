// avgen_foot_probe -- how far a character's planted feet slide as it sets off from standing (the GV3
// art pass, item 3: "a small amount of visible sliding before the first actual footstep/contact").
//
//   avgen_foot_probe --project p.json [--start S] [--seconds N] [--fps F] [--names a,b,...] [--out f.json]
//                    [--dump name:t0:t1 [--pose-out poses.json]]
//
// `--pose-out` writes the dumped character's every joint, in the world, every frame of the dump's window: the
// instrument for "this change does not move that body", compared between two builds (revision round 1).
//
// Which film (revision round 1): by default the engine is stepped exactly 1/fps a frame, as a seek replays and as
// `avgen_cast_trace` does. A render's `FixedStepClock` handed `update` the difference of two rounded instants
// instead, and a body that decides amplified that last bit into a different path (GV3: 0.7 mm at 6.28 s, 114 m
// by 168 s); ADR-990 made the clock hand out 1/fps. `--render-clock WxH` steps as `RenderJob` does -- its warm-up
// frame and second seek, its clock, its viewport -- to check the two still agree; `--fixed-clock`, `--warm-up`
// and `--viewport WxH` are its three parts, and `--instants` / `--delta-difference` spell a constant-step frame's
// instant, or its delta, the old clock's way (`--delta-difference` reproduces the parted film).
// `--camera-out FILE [--track a,b]` writes the drawn camera (and those bodies) every frame.
//
// It runs the real `app::Engine` in Offline mode, as `avgen_cast_trace` does, and after every frame reads
// each character's DRAWN feet: the rig's evaluated pose through the skinned mesh's world transform. No
// clip is analysed and nothing is re-derived: this is where the feet were in the picture.
//
// A foot is PLANTED on a frame when it is low -- within `kPlantBand` of that foot's own lowest height
// above the ground over the window, a fraction of its vertical range (ADR-546: a planted foot is low and
// still, not stationary) -- and not rising or falling faster than `kPlantVertical`. A planted foot that
// moves across the ground is sliding; the metres it covers are the slide.
//
// A START is a body that has stood still (measured ground speed under `kStillSpeed`) for at least
// `kStillBefore` and then moves. For each start it reports:
//   * `slideBeforeStep`: metres the planted feet slid from the start to the first footstep -- the first
//     frame a foot that was planted is lifted clear -- summed over the feet;
//   * `bodyBeforeStep`: metres the body travelled over the same frames;
//   * `firstStepSeconds`: how long after the start that first footstep came;
//   * the gait's activity and the locomotion phase on the first moving frame, and which clip the rig
//     was playing.
// and, for context, the whole window's planted slide per metre travelled, per character.
//
// Measure with every rig posed every frame (the final's `updateHz: 0`): a rig posed at 30 Hz in a 60 fps
// film slides by one frame's travel on every other frame, which is a different defect.

#include "app/engine.hpp"
#include "core/log.hpp"
#include "core/time.hpp"
#include "entity/entity.hpp"
#include "entity/locomotion.hpp"
#include "scene/composition.hpp"
#include "scene/detail_limits.hpp"
#include "scene/motion_analysis.hpp"
#include "scene/skeleton.hpp"

#include <fmt/format.h>
#include <glm/gtc/quaternion.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

using namespace avgen;

namespace {

constexpr float kStillSpeed = 0.05f;    // m/s: the body is standing
constexpr double kStillBefore = 0.4;    // s of standing that makes the next movement a start
constexpr float kPlantBand = 0.15f;     // of the foot's vertical range above its lowest
constexpr float kPlantVertical = 0.25f; // m/s: a foot rising or falling faster than this is stepping
constexpr double kStartWindow = 1.5;    // s after a start in which the first footstep is looked for
constexpr double kJump = 0.15;          // m in one frame: faster than any walking foot swings (9 m/s at 60 fps)

struct Options {
    std::string project;
    double start = 0.0;
    double seconds = 60.0;
    double fps = 60.0;
    std::vector<std::string> names;
    std::string out;
    std::string dump; // name:t0:t1 -- every frame of one character, for reading a start by eye
    std::string profile; // name -- its first clip sampled over its length, feet in model space
    std::string poseOut; // with --dump: every joint of that character, in the world, every frame, to a file
    std::string feetOut; // every character's feet, in the world, every frame, to a file (comparing two builds)
    std::string cameraOut; // the drawn camera's eye and target, every frame (play against seek)
    std::vector<std::string> track; // with --camera-out: these entities' positions on each camera line
    int renderWidth = 0;   // --render-clock WxH: step as `RenderJob` does (its warm-up, its clock, its viewport)
    int renderHeight = 0;
    // The three things --render-clock does, one at a time (which of them changes the film), and two spellings of
    // a constant-step frame:
    bool fixedClock = false; // --fixed-clock: the frame time from `FixedStepClock::tick`
    bool instants = false;   // --instants: the instant spelled i / fps, as the clock and the replay spell it
    bool deltaDifference = false; // --delta-difference: dt as the difference of two such instants, as the clock does
    bool warmUp = false;     // --warm-up: the job's dt-0 warm-up update and its second seek
    int viewWidth = 0;       // --viewport WxH: `setViewport` before every update
    int viewHeight = 0;
};

bool parse(int argc, char** argv, Options& o) {
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        const auto next = [&]() -> const char* { return i + 1 < argc ? argv[++i] : nullptr; };
        if (a == "--project") {
            const char* v = next();
            if (v == nullptr) return false;
            o.project = v;
        } else if (a == "--start") {
            const char* v = next();
            if (v == nullptr) return false;
            o.start = std::atof(v);
        } else if (a == "--seconds") {
            const char* v = next();
            if (v == nullptr) return false;
            o.seconds = std::atof(v);
        } else if (a == "--fps") {
            const char* v = next();
            if (v == nullptr) return false;
            o.fps = std::atof(v);
        } else if (a == "--names") {
            const char* v = next();
            if (v == nullptr) return false;
            std::stringstream ss(v);
            std::string item;
            while (std::getline(ss, item, ',')) {
                o.names.push_back(item);
            }
        } else if (a == "--profile") {
            const char* v = next();
            if (v == nullptr) return false;
            o.profile = v;
        } else if (a == "--dump") {
            const char* v = next();
            if (v == nullptr) return false;
            o.dump = v;
        } else if (a == "--pose-out") {
            const char* v = next();
            if (v == nullptr) return false;
            o.poseOut = v;
        } else if (a == "--camera-out") {
            const char* v = next();
            if (v == nullptr) return false;
            o.cameraOut = v;
        } else if (a == "--track") {
            const char* v = next();
            if (v == nullptr) return false;
            std::stringstream ss(v);
            std::string item;
            while (std::getline(ss, item, ',')) {
                o.track.push_back(item);
            }
        } else if (a == "--render-clock") {
            const char* v = next();
            if (v == nullptr || std::sscanf(v, "%dx%d", &o.renderWidth, &o.renderHeight) != 2) return false;
            o.fixedClock = true;
            o.warmUp = true;
            o.viewWidth = o.renderWidth;
            o.viewHeight = o.renderHeight;
        } else if (a == "--fixed-clock") {
            o.fixedClock = true;
        } else if (a == "--instants") {
            o.instants = true;
        } else if (a == "--delta-difference") {
            o.deltaDifference = true;
        } else if (a == "--warm-up") {
            o.warmUp = true;
        } else if (a == "--viewport") {
            const char* v = next();
            if (v == nullptr || std::sscanf(v, "%dx%d", &o.viewWidth, &o.viewHeight) != 2) return false;
        } else if (a == "--feet-out") {
            const char* v = next();
            if (v == nullptr) return false;
            o.feetOut = v;
        } else if (a == "--out") {
            const char* v = next();
            if (v == nullptr) return false;
            o.out = v;
        } else {
            return false;
        }
    }
    return !o.project.empty() && o.fps > 0.0 && o.seconds > 0.0;
}

bool isFoot(const std::string& joint) {
    // Glowmere's aliens (`foot.l`, `toes_01.l`) and farm animals (`HoofF.L`; the `.001` copies are the
    // hoof tips' end bones, which duplicate their parents).
    if (joint.find(".001") != std::string::npos) {
        return false;
    }
    return joint.rfind("foot.", 0) == 0 || joint.rfind("toes_01.", 0) == 0 || joint.rfind("Hoof", 0) == 0;
}

struct Sample {
    double t = 0.0;
    glm::vec3 body{0.0f};
    float speed = 0.0f; // measured ground speed of the body
    std::string activity;
    std::string phase;
    std::string clip;
    float rate = 0.0f;      // what the gait asked the clip to play at
    float clipSpeed = 0.0f; // what the player is playing it at
    float clipTime = 0.0f;  // the clip's local second
    std::vector<glm::vec3> feet;
    std::vector<float> height; // above the ground
    std::vector<glm::vec3> joints; // --pose-out: every joint in the world
    float yaw = 0.0f;               // the body's facing about +Y, radians
    bool posed = false;
    std::string layerNote;          // --dump: each ground foot layer's plane at its foot, and its IK and body state
    glm::quat head{1.0f, 0.0f, 0.0f, 0.0f}; // --feet-out: the head joint's rotation in the world (item 7)
    bool hasHead = false;
};

struct Character {
    std::string name;
    std::string node;
    std::vector<std::string> footNames;
    std::vector<Sample> samples;
    // Revision round 1 (ADR-987): each foot's stances in the rig's "Walk" clip, read as the backward stroke
    // of the in-place cycle (the contact analysis' sweep mode) -- the same definition whether or not the
    // scene asks for any contact analysis, so a before and an after are measured alike.
    std::vector<std::vector<scene::ContactSpan>> walkStances;
};

const world::WorldMap* terrainMap(const scene::Composition& comp) {
    for (const auto& n : comp.nodes()) {
        if (n->kind == scene::NodeKind::Terrain) {
            return &n->worldMap;
        }
    }
    return nullptr;
}

} // namespace

int main(int argc, char** argv) {
    Options o;
    if (!parse(argc, argv, o)) {
        std::fprintf(stderr, "usage: avgen_foot_probe --project p.json [--start S] [--seconds N] [--fps F] "
                             "[--names a,b] [--out f.json]\n");
        return 1;
    }
    log::init(log::Level::Warn);
    app::Engine engine(app::EngineMode::Offline);
    if (const auto loaded = engine.loadProject(o.project); !loaded) {
        std::fprintf(stderr, "load failed: %s\n", loaded.error().message.c_str());
        return 1;
    }
    scene::Composition* comp = engine.composition();
    if (comp == nullptr) {
        std::fprintf(stderr, "not a composition\n");
        return 1;
    }
    // What an offline render takes (ADR-191): every rig posed at its own rate whatever the distance,
    // and no entity frozen far away. The live limits would pose a far rig at 20 Hz and measure that.
    engine.setDetailLimits(scene::DetailLimits::offlineDefault());
    const world::WorldMap* map = terrainMap(*comp);

    std::vector<Character> cast;
    for (const auto& e : comp->entityWorld().entities()) {
        const entity::EntityDesc& d = e->desc();
        if (!o.names.empty() && std::find(o.names.begin(), o.names.end(), d.name) == o.names.end()) {
            continue;
        }
        if (d.clips.empty()) {
            continue; // a craft or a prop, not a walker
        }
        Character c;
        c.name = d.name;
        c.node = d.node.empty() ? d.name : d.node;
        cast.push_back(std::move(c));
    }

    if (!o.profile.empty()) {
        // The clip itself, in the rig's own space: each foot's height and its velocity through the
        // cycle. Rate matching assumes a planted foot moves backward at one speed; this shows where it
        // does not.
        const std::size_t colon = o.profile.find(':');
        const std::string who = o.profile.substr(0, colon);
        const std::string clipName = colon == std::string::npos ? std::string() : o.profile.substr(colon + 1);
        for (const Character& c : cast) {
            if (c.name != who) {
                continue;
            }
            const scene::CompositionNode* node = comp->findNode(c.node);
            if (node == nullptr || node->rigs.empty()) {
                continue;
            }
            const scene::SkinnedRig& rig = comp->scene().rigs[node->rigs.front()];
            const int wanted = clipName.empty() ? 0 : rig.findClip(clipName);
            const scene::AnimationClip& clip = rig.clips[static_cast<std::size_t>(std::max(wanted, 0))];
            std::vector<int> joints;
            std::vector<std::string> names;
            for (const scene::Joint& j : rig.skeleton.joints) {
                if (isFoot(j.name)) {
                    joints.push_back(rig.skeleton.find(j.name));
                    names.push_back(j.name);
                }
            }
            scene::Pose pose;
            std::vector<glm::mat4> m0;
            std::vector<glm::mat4> m1;
            const float step = 1.0f / 60.0f;
            std::fprintf(stderr, "clip '%s' duration %.3f\n", clip.name.c_str(), clip.duration);
            for (float t = 0.0f; t + step <= clip.duration + 1e-4f; t += step) {
                scene::setRestPose(rig.skeleton, pose);
                scene::sampleClip(clip, t, pose);
                scene::poseToModel(rig.skeleton, pose, m0);
                scene::setRestPose(rig.skeleton, pose);
                scene::sampleClip(clip, t + step, pose);
                scene::poseToModel(rig.skeleton, pose, m1);
                std::fprintf(stderr, "%6.3f", t);
                for (std::size_t k = 0; k < joints.size(); ++k) {
                    const glm::vec3 a = glm::vec3(m0[static_cast<std::size_t>(joints[k])][3]);
                    const glm::vec3 b = glm::vec3(m1[static_cast<std::size_t>(joints[k])][3]);
                    const glm::vec3 v = (b - a) / step;
                    std::fprintf(stderr, " | %s y%6.3f vx%6.2f vz%6.2f", names[k].c_str(), a.y, v.x, v.z);
                }
                std::fprintf(stderr, "\n");
            }
        }
        return 0;
    }
    const double dt = 1.0 / o.fps;
    const auto first = static_cast<std::uint64_t>(std::llround(o.start * o.fps));
    const auto frames = static_cast<std::uint64_t>(std::llround(o.seconds * o.fps)) + 1;
    std::unique_ptr<FixedStepClock> clock;
    const auto viewport = [&] {
        if (o.viewWidth > 0) {
            engine.setViewport(static_cast<std::uint32_t>(o.viewWidth), static_cast<std::uint32_t>(o.viewHeight));
        }
    };
    if (o.warmUp) {
        // `RenderJob::start`, step for step: the pipeline warm-up drives the engine one dt-0 frame at the
        // start (on a throwaway renderer, which reads the scene and writes nothing back), the camera state
        // is reset, and the job positions itself there with a second seek.
        FixedStepClock warm(o.fps);
        warm.restartAt(static_cast<double>(first) * dt);
        engine.seekSeconds(static_cast<double>(first) * dt);
        const FrameTime t = engine.tick(warm);
        viewport();
        engine.update(t);
        engine.resetCameraState();
        engine.seekSeconds(static_cast<double>(first) * dt);
    } else if (first > 0) {
        engine.seekSeconds(static_cast<double>(first) * dt);
    }
    if (o.fixedClock) {
        // The job's clock: every frame is `tick` on it.
        clock = std::make_unique<FixedStepClock>(o.fps);
        clock->restartAt(static_cast<double>(first) * dt);
    }
    std::vector<glm::mat4> model;
    std::ofstream cameraFile;
    if (!o.cameraOut.empty()) {
        cameraFile.open(o.cameraOut);
    }
    for (std::uint64_t i = first; i < first + frames; ++i) {
        FrameTime time;
        if (clock) {
            time = engine.tick(*clock);
        } else {
            const auto instant = [&](std::uint64_t k) {
                return o.instants || o.deltaDifference ? static_cast<double>(k) / o.fps : static_cast<double>(k) * dt;
            };
            time.renderTime = instant(i);
            time.deltaTime = i == first ? 0.0 : (o.deltaDifference ? instant(i) - instant(i - 1) : dt);
            time.frameIndex = i;
        }
        viewport();
        engine.update(time);
        const scene::Scene& scene = comp->scene();
        if (cameraFile.is_open()) {
            cameraFile << time.renderTime << ' ' << scene.camera.position.x << ' ' << scene.camera.position.y << ' '
                       << scene.camera.position.z << ' ' << scene.camera.target.x << ' ' << scene.camera.target.y << ' '
                       << scene.camera.target.z;
            for (const std::string& name : o.track) {
                if (const entity::Entity* e = comp->entityWorld().find(name)) {
                    const glm::vec3 p = e->state().position();
                    cameraFile << ' ' << p.x << ' ' << p.y << ' ' << p.z;
                }
            }
            cameraFile << '\n';
        }
        for (Character& c : cast) {
            const entity::Entity* e = comp->entityWorld().find(c.name);
            const scene::CompositionNode* node = comp->findNode(c.node);
            if (e == nullptr || node == nullptr || node->rigs.empty()) {
                continue;
            }
            const scene::RigId id = node->rigs.front();
            if (id >= scene.rigs.size()) {
                continue;
            }
            const scene::SkinnedRig& rig = scene.rigs[id];
            world::NodeView view;
            if (!comp->nodeView(c.node, view)) {
                continue;
            }
            // The skinned mesh's own world transform: the rig's model space is that entity's.
            glm::mat4 toWorld = view.world;
            for (std::uint32_t k = view.firstEntity; k < view.firstEntity + view.entityCount && k < scene.entities.size(); ++k) {
                if (scene.entities[k].rig == id) {
                    toWorld = scene.entities[k].transform.matrix();
                    break;
                }
            }
            if (c.footNames.empty()) {
                for (const scene::Joint& j : rig.skeleton.joints) {
                    if (isFoot(j.name)) {
                        c.footNames.push_back(j.name);
                    }
                }
                const int walk = rig.findClip("Walk");
                if (walk >= 0 && !c.footNames.empty()) {
                    std::vector<scene::ContactJoint> joints;
                    for (const std::string& f : c.footNames) {
                        joints.push_back(scene::ContactJoint{f, scene::ContactKind::Foot});
                    }
                    scene::ContactSettings sweep;
                    sweep.mode = scene::ContactMode::Sweep;
                    for (const scene::ContactTrack& t :
                         scene::detectContacts(rig.skeleton, rig.clips[static_cast<std::size_t>(walk)], joints, sweep)) {
                        c.walkStances.push_back(t.spans);
                    }
                }
            }
            scene::poseToModel(rig.skeleton, rig.pose, model);
            Sample s;
            if (!o.poseOut.empty() && o.dump.rfind(c.name + ":", 0) == 0) {
                for (const glm::mat4& m : model) {
                    s.joints.push_back(glm::vec3(toWorld * glm::vec4(glm::vec3(m[3]), 1.0f)));
                }
            }
            s.t = time.renderTime;
            const entity::LocomotionState& loco = e->locomotion();
            s.body = glm::vec3(toWorld[3]);
            s.yaw = std::atan2(toWorld[2].x, toWorld[2].z);
            s.speed = std::sqrt((loco.velocity.x * loco.velocity.x) + (loco.velocity.z * loco.velocity.z));
            s.activity = entity::activityName(loco.activity);
            s.phase = entity::locomotionPhaseName(loco.phase);
            s.clip = std::string(rig.player.currentState());
            s.rate = loco.playbackRate;
            s.clipSpeed = rig.player.currentSpeed();
            s.clipTime = rig.player.stateTime(rig.clips, time.renderTime);
            s.posed = rig.paletteTime == time.renderTime;
            for (const std::string& f : c.footNames) {
                const int j = rig.skeleton.find(f);
                const glm::vec3 p = j >= 0 && static_cast<std::size_t>(j) < model.size()
                                        ? glm::vec3(toWorld * glm::vec4(glm::vec3(model[static_cast<std::size_t>(j)][3]), 1.0f))
                                        : glm::vec3(0.0f);
                s.feet.push_back(p);
                s.height.push_back(map != nullptr ? p.y - map->height(glm::vec2(p.x, p.z)) : p.y);
            }
            if (!o.feetOut.empty()) {
                const int h = rig.skeleton.find("head.x");
                if (h >= 0 && static_cast<std::size_t>(h) < model.size()) {
                    glm::mat3 r(toWorld * model[static_cast<std::size_t>(h)]);
                    r[0] = glm::normalize(r[0]);
                    r[1] = glm::normalize(r[1]);
                    r[2] = glm::normalize(r[2]);
                    s.head = glm::normalize(glm::quat_cast(r));
                    s.hasHead = true;
                }
            }
            if (!o.dump.empty() && o.dump.rfind(c.name + ":", 0) == 0) {
                // Where each ground layer's plane is under its foot, against the terrain there; the IK's answer;
                // and how far the body's reach solve moved the body.
                const scene::PoseLayerStack& stack = rig.layers;
                const auto& ls = stack.layers();
                for (std::size_t k = 0; k < ls.size(); ++k) {
                    const scene::PoseLayer& l = ls[k];
                    if (l.kind != scene::PoseLayerKind::Foot || !l.hasGround) {
                        continue;
                    }
                    const int j = rig.skeleton.find(l.chainTip);
                    if (j < 0 || static_cast<std::size_t>(j) >= model.size()) {
                        continue;
                    }
                    const glm::vec3 foot = glm::vec3(toWorld * glm::vec4(glm::vec3(model[static_cast<std::size_t>(j)][3]), 1.0f));
                    const glm::vec3 point = glm::vec3(toWorld * glm::vec4(l.groundPoint, 1.0f));
                    const glm::vec3 normal = glm::normalize(glm::transpose(glm::inverse(glm::mat3(toWorld))) * l.groundNormal);
                    const float planeY = std::fabs(normal.y) > 1e-3f
                                             ? point.y - ((normal.x * (foot.x - point.x)) + (normal.z * (foot.z - point.z))) / normal.y
                                             : point.y;
                    const float terrain = map != nullptr ? map->height(glm::vec2(foot.x, foot.z)) : 0.0f;
                    const int ik = k < stack.ikStatuses().size() ? static_cast<int>(stack.ikStatuses()[k]) : -1;
                    const int res = k < stack.results().size() ? static_cast<int>(stack.results()[k]) : -1;
                    s.layerNote += fmt::format(" [{} plane{:+.3f} ik{} r{}]", l.chainTip, planeY - terrain, ik, res);
                }
                s.layerNote += fmt::format(" body{:+.3f}", stack.bodyCompensation().translation.y);
            }
            c.samples.push_back(std::move(s));
        }
    }

    if (!o.dump.empty()) {
        const std::size_t a = o.dump.find(':');
        const std::size_t b = o.dump.find(':', a + 1);
        const std::string who = o.dump.substr(0, a);
        const double t0 = std::atof(o.dump.substr(a + 1, b - a - 1).c_str());
        const double t1 = std::atof(o.dump.substr(b + 1).c_str());
        for (const Character& c : cast) {
            if (c.name != who) {
                continue;
            }
            for (const Sample& s : c.samples) {
                if (s.t < t0 || s.t > t1) {
                    continue;
                }
                std::fprintf(stderr, "%7.3f body %9.3f %9.3f yaw %7.4f ", s.t, s.body.x, s.body.z, s.yaw);
                std::fprintf(stderr, "%s speed %5.2f %-5s %-9s %-10s rate %5.3f/%5.3f clip %6.3f", s.posed ? "P" : "-",
                             s.speed, s.activity.c_str(), s.phase.c_str(), s.clip.c_str(), s.rate, s.clipSpeed, s.clipTime);
                for (std::size_t f = 0; f < s.feet.size(); ++f) {
                    std::fprintf(stderr, " | %s h%6.3f x%8.3f z%8.3f", c.footNames[f].c_str(), s.height[f], s.feet[f].x,
                                 s.feet[f].z);
                }
                std::fprintf(stderr, "%s\n", s.layerNote.c_str());
            }
        }
    }
    // --pose-out: the dumped character's every joint, every frame of its window, for comparing two builds.
    if (!o.poseOut.empty() && !o.dump.empty()) {
        const std::size_t a = o.dump.find(':');
        const std::size_t b = o.dump.find(':', a + 1);
        const std::string who = o.dump.substr(0, a);
        const double t0 = std::atof(o.dump.substr(a + 1, b - a - 1).c_str());
        const double t1 = std::atof(o.dump.substr(b + 1).c_str());
        nlohmann::json poses = nlohmann::json::array();
        for (const Character& c : cast) {
            if (c.name != who) {
                continue;
            }
            for (const Sample& s : c.samples) {
                if (s.t < t0 || s.t > t1) {
                    continue;
                }
                nlohmann::json js = nlohmann::json::array();
                for (const glm::vec3& p : s.joints) {
                    js.push_back({p.x, p.y, p.z});
                }
                poses.push_back({{"t", s.t}, {"clip", s.clip}, {"clipTime", s.clipTime}, {"joints", std::move(js)}});
            }
        }
        std::ofstream(o.poseOut) << poses.dump() << "\n";
    }
    if (!o.feetOut.empty()) {
        std::ofstream feetFile(o.feetOut);
        for (const Character& c : cast) {
            for (const Sample& smp : c.samples) {
                feetFile << c.name << ' ' << smp.t << ' ' << smp.yaw << ' ' << smp.activity << ' ' << (smp.posed ? 1 : 0);
                if (smp.hasHead) {
                    feetFile << " head " << smp.head.w << ' ' << smp.head.x << ' ' << smp.head.y << ' ' << smp.head.z;
                }
                for (std::size_t f = 0; f < smp.feet.size(); ++f) {
                    feetFile << ' ' << c.footNames[f] << ' ' << smp.feet[f].x << ' ' << smp.feet[f].y << ' ' << smp.feet[f].z;
                }
                feetFile << '\n';
            }
        }
    }
    // ---- analysis -------------------------------------------------------------------------------
    nlohmann::json out;
    out["project"] = o.project;
    out["window"] = {o.start, o.start + o.seconds};
    out["fps"] = o.fps;
    nlohmann::json people = nlohmann::json::array();
    nlohmann::json starts = nlohmann::json::array();
    for (const Character& c : cast) {
        const std::size_t n = c.samples.size();
        const std::size_t feet = c.footNames.size();
        if (n < 3 || feet == 0) {
            continue;
        }
        // Each foot's lowest height and vertical range over the window, for its plant band.
        std::vector<float> lo(feet, 1e9f);
        std::vector<float> hi(feet, -1e9f);
        for (const Sample& s : c.samples) {
            for (std::size_t f = 0; f < feet; ++f) {
                lo[f] = std::min(lo[f], s.height[f]);
                hi[f] = std::max(hi[f], s.height[f]);
            }
        }
        const auto planted = [&](std::size_t k, std::size_t f) {
            if (k == 0 || k >= n) {
                return false;
            }
            const float range = std::max(hi[f] - lo[f], 0.05f);
            const float low = lo[f] + (kPlantBand * range);
            const float vy = (c.samples[k].feet[f].y - c.samples[k - 1].feet[f].y) / static_cast<float>(dt);
            return c.samples[k].height[f] <= low && std::fabs(vy) <= kPlantVertical;
        };
        const auto slideAt = [&](std::size_t k, std::size_t f) {
            if (!planted(k, f) || !planted(k - 1, f)) {
                return 0.0f;
            }
            const glm::vec3 d = c.samples[k].feet[f] - c.samples[k - 1].feet[f];
            return std::sqrt((d.x * d.x) + (d.z * d.z));
        };
        double totalSlide = 0.0;
        double totalTravel = 0.0;
        for (std::size_t k = 1; k < n; ++k) {
            const glm::vec3 d = c.samples[k].body - c.samples[k - 1].body;
            totalTravel += std::sqrt((d.x * d.x) + (d.z * d.z));
            for (std::size_t f = 0; f < feet; ++f) {
                totalSlide += slideAt(k, f);
            }
        }
        // The stance slide (ADR-987): how far each foot moves over the ground while the Walk has it in its
        // backward stroke, split at 70% of the stroke -- the held part, and the part a stance lock hands
        // back to the clip.
        double heldSlide = 0.0;
        double releaseSlide = 0.0;
        const auto stanceAt = [&](const Sample& smp, std::size_t f, float& along) {
            if (smp.clip != "Walk" || f >= c.walkStances.size()) {
                return -1;
            }
            const auto& spans = c.walkStances[f];
            for (std::size_t i = 0; i < spans.size(); ++i) {
                const scene::ContactSpan& sp = spans[i];
                const float t = smp.clipTime;
                const bool inside = sp.wraps() ? (t >= sp.start || t <= sp.end) : (t >= sp.start && t <= sp.end);
                if (inside) {
                    const float elapsed = sp.wraps() && t <= sp.end ? (sp.clipLength - sp.start) + t : t - sp.start;
                    along = elapsed / std::max(sp.duration(), 1e-4f);
                    return static_cast<int>(i);
                }
            }
            return -1;
        };
        for (std::size_t k = 1; k < n; ++k) {
            for (std::size_t f = 0; f < feet; ++f) {
                float a0 = 0.0f;
                float a1 = 0.0f;
                const int s0 = stanceAt(c.samples[k - 1], f, a0);
                const int s1 = stanceAt(c.samples[k], f, a1);
                if (s0 < 0 || s0 != s1 || a1 < a0) {
                    continue; // not both in one stance, or the stroke started again
                }
                const glm::vec3 d = c.samples[k].feet[f] - c.samples[k - 1].feet[f];
                const double slide = std::sqrt((d.x * d.x) + (d.z * d.z));
                (a1 <= 0.7f ? heldSlide : releaseSlide) += slide;
            }
        }
        // Standing still, and snapping (ADR-987): how far the feet move while the body stands (both frames
        // under `kStillSpeed`), the largest one-frame move of a standing foot, and every frame on which any
        // foot moves further than `kJump` -- faster than a walking foot swings -- whatever the body does.
        double stillSlide = 0.0;
        double stillSeconds = 0.0;
        double stillMax = 0.0;
        double stillMaxT = -1.0;
        nlohmann::json jumps = nlohmann::json::array();
        std::size_t jumpCount = 0;
        for (std::size_t k = 1; k < n; ++k) {
            const bool still = c.samples[k].speed < kStillSpeed && c.samples[k - 1].speed < kStillSpeed;
            double worst = 0.0;
            for (std::size_t f = 0; f < feet; ++f) {
                const glm::vec3 d = c.samples[k].feet[f] - c.samples[k - 1].feet[f];
                const double move = std::sqrt((d.x * d.x) + (d.z * d.z));
                worst = std::max(worst, move);
                if (still) {
                    stillSlide += move;
                    if (move > stillMax) {
                        stillMax = move;
                        stillMaxT = c.samples[k].t;
                    }
                }
            }
            if (still) {
                stillSeconds += dt;
            }
            if (worst > kJump) {
                ++jumpCount;
                if (jumps.size() < 12) {
                    jumps.push_back({{"t", c.samples[k].t}, {"metres", worst}, {"activity", c.samples[k].activity}});
                }
            }
        }
        // Swing clearance (revision round 1, item 6): for each step -- a run of frames in which the foot moves
        // over the ground faster than 1.5 times the body, and at least 0.3 m/s: swinging, read off the drawn pose
        // itself, since a body posed by its motion provider is not where its clip player says -- the highest
        // the drawn foot gets above the terrain; and the lowest any foot gets anywhere (below zero is through
        // the ground), with how many frames a foot spends more than 2 cm under. By the activity at the peak.
        nlohmann::json swing = nlohmann::json::object();
        {
            std::map<std::string, std::vector<double>> peaks; // "walk foot", "run toes_01", ...
            double lowest = 1e9;
            double lowestT = -1.0;
            std::size_t through = 0;
            for (std::size_t f = 0; f < feet; ++f) {
                const std::string kind = c.footNames[f].substr(0, c.footNames[f].find('.'));
                double peak = -1e9;
                std::string peakActivity;
                bool inSwing = false;
                for (std::size_t k = 0; k < n; ++k) {
                    const Sample& smp = c.samples[k];
                    if (smp.height[f] < lowest) {
                        lowest = smp.height[f];
                        lowestT = smp.t;
                    }
                    if (smp.height[f] < -0.02f) {
                        ++through;
                    }
                    bool swingNow = false;
                    if (k > 0) {
                        const Sample& prev = c.samples[k - 1];
                        const glm::vec3 d = smp.feet[f] - prev.feet[f];
                        const glm::vec3 b = smp.body - prev.body;
                        const double footSpeed = std::sqrt((d.x * d.x) + (d.z * d.z)) / dt;
                        const double bodySpeed = std::sqrt((b.x * b.x) + (b.z * b.z)) / dt;
                        swingNow = footSpeed > std::max(1.5 * bodySpeed, 0.3);
                    }
                    if (swingNow) {
                        if (!inSwing) {
                            peak = -1e9;
                        }
                        inSwing = true;
                        if (smp.height[f] > peak) {
                            peak = smp.height[f];
                            peakActivity = smp.activity;
                        }
                    } else if (inSwing) {
                        inSwing = false;
                        if (peakActivity == "walk" || peakActivity == "run") {
                            peaks[peakActivity + " " + kind].push_back(peak);
                        }
                    }
                }
            }
            const auto quantile = [](std::vector<double> v, double q) {
                if (v.empty()) {
                    return 0.0;
                }
                std::sort(v.begin(), v.end());
                return v[static_cast<std::size_t>(q * static_cast<double>(v.size() - 1))];
            };
            for (const auto& [key, v] : peaks) {
                swing[key] = {{"steps", v.size()}, {"peakMedian", quantile(v, 0.5)}, {"peakP10", quantile(v, 0.1)},
                              {"peakP90", quantile(v, 0.9)}};
            }
            swing["lowest"] = lowest;
            swing["lowestT"] = lowestT;
            swing["throughFrames"] = through;
        }
        people.push_back({{"name", c.name}, {"feet", c.footNames}, {"travelled", totalTravel}, {"swing", swing},
                          {"plantedSlide", totalSlide},
                          {"slidePerMetre", totalTravel > 1e-3 ? totalSlide / totalTravel : 0.0},
                          {"stanceHeldSlide", heldSlide}, {"stanceReleaseSlide", releaseSlide},
                          {"stanceHeldPerMetre", totalTravel > 1e-3 ? heldSlide / totalTravel : 0.0},
                          {"stanceReleasePerMetre", totalTravel > 1e-3 ? releaseSlide / totalTravel : 0.0},
                          {"stillSlide", stillSlide}, {"stillSeconds", stillSeconds},
                          {"stillFootMax", stillMax}, {"stillFootMaxT", stillMaxT},
                          {"jumpFrames", jumpCount}, {"jumps", std::move(jumps)}});
        // Starts.
        double stillSince = c.samples[0].t;
        for (std::size_t k = 1; k < n; ++k) {
            const Sample& s = c.samples[k];
            if (s.speed < kStillSpeed) {
                if (c.samples[k - 1].speed >= kStillSpeed) {
                    stillSince = s.t;
                }
                continue;
            }
            if (c.samples[k - 1].speed >= kStillSpeed || s.t - stillSince < kStillBefore) {
                continue;
            }
            // A start at sample k. Walk forward to the first footstep: a foot planted at the start that
            // is then lifted (no longer planted, and rising).
            std::vector<bool> wasPlanted(feet, false);
            for (std::size_t f = 0; f < feet; ++f) {
                wasPlanted[f] = planted(k, f) || planted(k - 1, f);
            }
            double slide = 0.0;
            double body = 0.0;
            double firstStep = -1.0;
            // The defect in its own terms, which needs no contact test: how far the feet travel over
            // the ground while the gait still plays a STANDING clip and the body is moving.
            double standingSlide = 0.0;
            double standingSeconds = 0.0;
            for (std::size_t m = k; m < n && c.samples[m].t - s.t <= kStartWindow; ++m) {
                const Sample& a = c.samples[m];
                if (a.activity != "idle" && a.activity != "turn") {
                    break;
                }
                if (a.speed < kStillSpeed) {
                    continue;
                }
                standingSeconds += dt;
                for (std::size_t f = 0; f < feet; ++f) {
                    const glm::vec3 d = a.feet[f] - c.samples[m - 1].feet[f];
                    standingSlide += std::sqrt((d.x * d.x) + (d.z * d.z));
                }
            }
            standingSlide /= static_cast<double>(feet); // per foot
            for (std::size_t m = k; m < n && c.samples[m].t - s.t <= kStartWindow; ++m) {
                bool stepped = false;
                for (std::size_t f = 0; f < feet; ++f) {
                    if (wasPlanted[f] && !planted(m, f) && c.samples[m].feet[f].y > c.samples[m - 1].feet[f].y) {
                        stepped = true;
                    }
                }
                if (stepped) {
                    firstStep = c.samples[m].t - s.t;
                    break;
                }
                for (std::size_t f = 0; f < feet; ++f) {
                    slide += slideAt(m, f);
                }
                const glm::vec3 d = c.samples[m].body - c.samples[m - 1].body;
                body += std::sqrt((d.x * d.x) + (d.z * d.z));
            }
            starts.push_back({{"name", c.name}, {"t", s.t}, {"standingSlidePerFoot", standingSlide},
                              {"standingSeconds", standingSeconds}, {"slideBeforeStep", slide}, {"bodyBeforeStep", body},
                              {"firstStepSeconds", firstStep}, {"activity", s.activity}, {"phase", s.phase},
                              {"clip", s.clip}, {"speed", s.speed}});
        }
    }
    out["characters"] = std::move(people);
    out["starts"] = std::move(starts);
    const std::string text = out.dump(1);
    if (o.out.empty()) {
        std::printf("%s\n", text.c_str());
    } else {
        std::ofstream(o.out) << text << "\n";
    }
    return 0;
}
