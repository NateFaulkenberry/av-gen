// Glowmere Valley 2 - Multi-Camera: does the ported animation actually animate? (ADR-300, ADR-344)
//
// ADR-344 moved this film's five aliens onto the pose layer stack: an `Aim` over the head group and
// an `Additive` startle over the spine, on every one of the five nodes. `test_character_lab_layers`
// already proves the *mechanism* on the lab fixture. What nothing checked is the thing the owner
// actually asked about -- whether the layers do anything **in this film**, on these five rigs, with
// these behaviours driving them.
//
// That gap is the exact shape of the failure ADR-300 was written against. A layer whose mask names
// a joint the rig does not carry, or whose clip the rig does not have, is not an error: it resolves
// to `NoJoints` / `NoSource`, writes nothing, costs nothing and reports nothing unless somebody
// asks. `alien-scout.glb` is a flat Auto-Rig Pro export whose eyes and mouth are *siblings* of the
// head; the five masks name `head.x`, `Eye_L`, `Eye_R`, `Mouth`, `Antenna` and pivot on `head.x`;
// the startle names `Fight_head_hit`. Every one of those is a string in a JSON file, and a typo in
// any of them produces a film that renders perfectly and never turns a head.
//
// ADR-182 decides the shape of the evidence. Three of the four arms below carry a control that has
// to come out the other way:
//
//   * the film's own five rigs resolve, and a **deliberately misspelt copy of the same scene** does
//     not -- so "resolved" is a measurement and not a tautology about a file that parses;
//   * the look layer writes joints during the film, and the **startle layer on the same rigs in the
//     same run writes none**, which is what a driven layer and an undriven one look like side by
//     side;
//   * the aim is masked, so the number of joints it writes is the mask's size and not the rig's 89.
//
// GPU-free: composition load, entity simulation and posing are all CPU.

#include "assets/asset_registry.hpp"
#include "core/time.hpp"
#include "entity/entity.hpp"
#include "entity/locomotion.hpp"
#include "params/modulation.hpp"
#include "params/parameter_set.hpp"
#include "scene/composition.hpp"
#include "scene/pose_layers.hpp"
#include "signals/signal_bus.hpp"

#include <catch2/catch_test_macros.hpp>

#include <fmt/format.h>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <system_error>
#include <vector>

using namespace avgen;
using nlohmann::json;
namespace fs = std::filesystem;

namespace {

fs::path worldDir() { return fs::path(AVGEN_SOURCE_DIR) / "examples" / "world"; }

constexpr const char* kFilm = "glowmere-valley-2-multicam.scene.json";
const std::array<const char*, 5> kCast{{"rook", "tide", "sage", "ember", "vane"}};

// What one body's layers did over a run.
struct LayerTrack {
    std::size_t stackSize = 0;
    std::set<std::string> resolutions; // every LayerResolution seen, by name, over the run
    float maxLookWeight = 0.0f;
    float maxReactionWeight = 0.0f;
    std::uint32_t maxApplied = 0;
    std::uint32_t maxJoints = 0;
    int framesLookApplied = 0;
    int framesAnyApplied = 0;
    // The gait half of "the animation is wrong": a body in a walk clip that is not travelling fast
    // enough for `matchRate` to keep the feet on the ground.
    int framesWalking = 0;
    int framesSlipping = 0;
    float slowestWalk = 1e9f;
    // What the *state* machine said the body was doing, before the gait folded Observe into Idle.
    // `decide`'s `interest` considerer authors `"activity": "observe"` on every one of the five,
    // and whether that ever reaches the body is the question the look layer's answer turns on.
    std::map<std::string, int> stateActivity;
    int framesHasLookTarget = 0;
    int frames = 0;
    // The resolved masks, per layer: how many names the spec asked for and how many of them this
    // rig carries. This is what "the layer resolved" means at load, before any intent is written,
    // and it is the only reading that separates "nobody asked it to do anything" from "it could
    // not have done anything if they had" -- `LayerResolution` reports `Inactive` for both.
    std::vector<std::pair<std::uint32_t, std::uint32_t>> maskNamedKept;
};

// The film, played through a scratch copy in its own directory: the scene names two entity profiles
// and a light rig by relative path, and `fromJson` has no base to resolve them against.
std::map<std::string, LayerTrack> playLayers(double seconds, const json& patch = json::object(),
                                             const char* tag = "plain", bool dwellZero = false,
                                             double hz = 40.0) {
    std::ifstream in(worldDir() / kFilm);
    REQUIRE(in.good());
    json doc;
    in >> doc;
    if (!patch.empty()) {
        doc.merge_patch(patch);
    }
    if (dwellZero) {
        // ADR-182's control for the arm that says the aim layer is driven by the attend pose: with
        // no dwell there is no `Pose` action, so there is no target to publish and the head must
        // never turn. It has to be reachable from here rather than from a patch, because `dwell`
        // lives three arrays deep and `merge_patch` cannot index one.
        for (json& e : doc.at("entities")) {
            if (!e.contains("behaviors")) {
                continue;
            }
            for (json& b : e.at("behaviors")) {
                if (b.value("kind", std::string{}) != "decide" || !b.contains("considerers")) {
                    continue;
                }
                for (json& c : b.at("considerers")) {
                    if (c.contains("dwell")) {
                        c["dwell"] = 0.0;
                    }
                }
            }
        }
    }
    const fs::path scratch = worldDir() / fmt::format("_probe-layers-{}.scene.json", tag);
    {
        std::ofstream out(scratch);
        REQUIRE(out.good());
        out << doc.dump(1);
    }
    struct Remove {
        fs::path p;
        ~Remove() {
            std::error_code ec;
            fs::remove(p, ec);
        }
    } remove{scratch};

    assets::AssetRegistry registry(worldDir());
    params::ParameterSet params;
    params::Modulator modulator;
    signals::SignalBus bus;
    auto loaded = scene::Composition::loadFile(scratch, registry);
    INFO((loaded.has_value() ? std::string() : loaded.error().message));
    REQUIRE(loaded.has_value());
    std::unique_ptr<scene::Composition> comp = std::move(*loaded);
    comp->attach(params, modulator);
    comp->setViewport(1600, 900);
    // ADR-186's offline setting, and here it is load-bearing rather than tidy: with the distance
    // cull on, `updateRigs` stops posing a rig past `cullDistance` and every layer number below
    // would be a measurement of where the camera happened to be.
    comp->scene().detailLimits.entityDistanceCull = false;

    std::map<std::string, LayerTrack> out;
    const double step = 1.0 / hz;
    const auto frames = static_cast<int>(std::llround(seconds * hz));
    FrameTime time;
    for (int i = 0; i < frames; ++i) {
        time.renderTime = static_cast<double>(i) * step;
        time.deltaTime = i == 0 ? 0.0 : step;
        time.frameIndex = static_cast<std::uint64_t>(i);
        params.resetFinals();
        comp->updateFields(time, bus, modulator);
        modulator.applyRoutes(bus, params, time.deltaTime);
        comp->updateBehaviour(time, bus);
        comp->update(time);
        for (const char* name : kCast) {
            const scene::CompositionNode* node = comp->findNode(name);
            if (node == nullptr || node->rigs.empty()) {
                continue;
            }
            LayerTrack& t = out[name];
            ++t.frames;
            for (const scene::RigId id : node->rigs) {
                if (id >= comp->scene().rigs.size()) {
                    continue;
                }
                const scene::SkinnedRig& rig = comp->scene().rigs[id];
                t.stackSize = std::max(t.stackSize, rig.layers.size());
                if (t.maskNamedKept.empty()) {
                    for (const scene::JointMask& mask : rig.layers.masks()) {
                        t.maskNamedKept.emplace_back(mask.named, mask.joints);
                    }
                }
                const auto& results = rig.layers.results();
                for (std::size_t k = 0; k < results.size(); ++k) {
                    t.resolutions.insert(scene::layerResolutionName(results[k]));
                }
                for (std::size_t k = 0; k < rig.layers.layers().size(); ++k) {
                    const scene::PoseLayer& layer = rig.layers.layers()[k];
                    if (layer.drive == scene::PoseLayerDrive::Look) {
                        t.maxLookWeight = std::max(t.maxLookWeight, layer.weight);
                        if (k < results.size() && results[k] == scene::LayerResolution::Applied) {
                            ++t.framesLookApplied;
                        }
                    } else if (layer.drive == scene::PoseLayerDrive::Reaction) {
                        t.maxReactionWeight = std::max(t.maxReactionWeight, layer.weight);
                    }
                }
                t.maxApplied = std::max(t.maxApplied, rig.layerStats.applied);
                t.maxJoints = std::max(t.maxJoints, rig.layerStats.joints);
                if (rig.layerStats.applied > 0) {
                    ++t.framesAnyApplied;
                }
            }
            const entity::Entity* e = comp->entityWorld().find(name);
            if (e != nullptr) {
                const entity::LocomotionState& loco = e->locomotion();
                t.stateActivity[entity::activityName(e->state().activity)] += 1;
                if (loco.hasLookTarget) {
                    ++t.framesHasLookTarget;
                }
                const bool moving = loco.activity == entity::Activity::Walk ||
                                    loco.activity == entity::Activity::Run;
                if (moving) {
                    ++t.framesWalking;
                    t.slowestWalk = std::min(t.slowestWalk, loco.speed);
                    // `matchRate` cannot slow the clip below `rateMin`, so below
                    // `rateMin x walkSpeed` the feet are moving faster than the body. The entity
                    // layer warns about this once per body; this counts the frames.
                    const entity::GaitSettings& gait = e->desc().gait;
                    if (gait.matchRate && loco.speed < gait.rateMin * gait.walkSpeed * 0.75f) {
                        ++t.framesSlipping;
                    }
                }
            }
        }
    }
    return out;
}

} // namespace

// ---------------------------------------------------------------------------------------------
// The probe. Everything the guards below assert, printed, so the next person can see the shape
// rather than infer it from a threshold.
//
//   ./build/release/tests/avgen_tests "[.probe][multicam-layers]"
TEST_CASE("probe: what the multicam's pose layers do over ninety seconds",
          "[.probe][multicam-layers]") {
    const auto run = playLayers(90.0);
    fmt::print("\n--- the film's pose layers, 90 s at 40 Hz ---\n");
    for (const char* name : kCast) {
        const auto it = run.find(name);
        if (it == run.end()) {
            fmt::print("  {:6s} NO RIG\n", name);
            continue;
        }
        const LayerTrack& t = it->second;
        std::string res;
        for (const std::string& r : t.resolutions) {
            res += (res.empty() ? "" : ",") + r;
        }
        fmt::print("  {:6s} layers {}  resolutions [{}]  look w<={:.2f} applied {}/{}  "
                   "reaction w<={:.2f}  joints<={}\n",
                   name, t.stackSize, res, t.maxLookWeight, t.framesLookApplied, t.frames,
                   t.maxReactionWeight, t.maxJoints);
        fmt::print("         walking {}/{} frames, of which {} below the rate floor "
                   "(slowest {:.3f} m/s)\n",
                   t.framesWalking, t.frames, t.framesSlipping, t.slowestWalk);
        std::string acts;
        for (const auto& [k, v] : t.stateActivity) {
            acts += fmt::format("{}{}:{}", acts.empty() ? "" : " ", k, v);
        }
        fmt::print("         state activity [{}]  look target published on {}/{} frames\n", acts,
                   t.framesHasLookTarget, t.frames);
    }
}

// ---------------------------------------------------------------------------------------------
TEST_CASE("The multicam's five aliens carry two pose layers that resolve against their own rigs",
          "[glowmere][multicam][layers]") {
    // Short: binding happens at load and nothing below depends on the simulation.
    const auto run = playLayers(0.5);
    for (const char* name : kCast) {
        const auto it = run.find(name);
        INFO(name << ": the film's scene authors two layers on this node");
        REQUIRE(it != run.end());
        const LayerTrack& t = it->second;
        INFO(name << ": stack of " << t.stackSize);
        CHECK(t.stackSize == 2);
        // The three ways a layer is silently nothing. `Inactive` and `NoTarget` are legitimate --
        // they mean "nobody asked it to do anything this frame" -- and the guard must not forbid
        // them or it would forbid a character that is not looking at anything.
        for (const char* bad : {"NoJoints", "NoPivot", "NoSource"}) {
            INFO(name << ": layer resolution '" << bad << "' means a name in the scene file is not "
                                                          "a name in the rig");
            CHECK(t.resolutions.count(bad) == 0);
        }
    }
}

TEST_CASE("A joint name this rig does not carry resolves to nothing, and the film's do not",
          "[glowmere][multicam][layers]") {
    // ADR-182's control for the arm above. `merge_patch` cannot reach into an array, so the patch
    // replaces the one node wholesale -- which is also what makes the arm honest: everything but
    // the five joint names is byte-identical.
    //
    // The reading is the resolved **mask**, not `LayerResolution`. A layer nobody has asked to do
    // anything this frame reports `Inactive` whether its mask is good or empty, so a control built
    // on the resolution would have passed for the wrong reason -- which is exactly what the first
    // cut of this test did.
    std::ifstream in(worldDir() / kFilm);
    REQUIRE(in.good());
    json doc;
    in >> doc;
    json nodes = doc.at("nodes");
    bool patched = false;
    for (json& n : nodes) {
        if (n.value("name", std::string{}) != "rook") {
            continue;
        }
        // A plausible misspelling, not a nonsense string: `head` rather than `head.x` is exactly
        // the mistake an author who has seen a Mixamo rig makes on an Auto-Rig Pro one.
        n["animation"]["layers"][0]["joints"] = json::array({"head", "Eye_L2", "Eye_R2"});
        n["animation"]["layers"][0]["pivot"] = "head";
        patched = true;
    }
    REQUIRE(patched);
    const auto run = playLayers(0.5, json{{"nodes", nodes}}, "misspelt");
    const auto rook = run.find("rook");
    REQUIRE(rook != run.end());
    REQUIRE(rook->second.maskNamedKept.size() == 2);
    INFO("rook's misspelt aim mask asked for " << rook->second.maskNamedKept[0].first
                                               << " names and kept "
                                               << rook->second.maskNamedKept[0].second);
    CHECK(rook->second.maskNamedKept[0].second == 0);

    // And the other four, untouched in the same document, still resolve: the failure is the names
    // and not the patch.
    for (const char* name : {"tide", "sage", "ember", "vane"}) {
        const auto it = run.find(name);
        REQUIRE(it != run.end());
        REQUIRE(it->second.maskNamedKept.size() == 2);
        INFO(name << ": aim mask kept " << it->second.maskNamedKept[0].second << " of "
                  << it->second.maskNamedKept[0].first << " names, startle kept "
                  << it->second.maskNamedKept[1].second);
        CHECK(it->second.maskNamedKept[0].second == 5);
        CHECK(it->second.maskNamedKept[1].second > 0);
    }
}

TEST_CASE("Every one of the film's five aliens turns its head, and the attend pose is why",
          "[glowmere][multicam][layers]") {
    // Sixty seconds: `rook` is the slowest of the five to commit to anything and reaches its first
    // attend pose at about forty.
    const auto run = playLayers(60.0, json::object(), "drive");
    int turning = 0;
    for (const char* name : kCast) {
        const auto it = run.find(name);
        REQUIRE(it != run.end());
        const LayerTrack& t = it->second;
        INFO(name << ": look layer applied on " << t.framesLookApplied << " of " << t.frames
                  << " frames, max weight " << t.maxLookWeight << ", up to " << t.maxJoints
                  << " joints");
        // **Joints, not weight.** A layer can carry weight 1.0 and write nothing -- that is what a
        // mask that missed looks like, and it is the failure this file exists for.
        CHECK(t.framesLookApplied > 0);
        CHECK(t.maxJoints > 0);
        // The mask is five names on a rig of eighty-nine joints. A layer writing the whole rig
        // would satisfy "it moved" and would be the other failure ADR-300 names.
        CHECK(t.maxJoints <= 5);
        turning += t.framesLookApplied > 0 ? 1 : 0;
    }
    INFO(turning << " of the five turned their heads; before the attend pose carried its subject "
                 << "it was one, and the one was `sage`, which kept an `interest` behaviour");
    CHECK(turning == 5);

    // The control, and it must come out the other way: the same film with every considerer's dwell
    // at zero emits no `Pose`, so nothing publishes a look target and no head turns. If this ever
    // passes with heads turning, the arm above is measuring something other than the attend pose.
    const auto control = playLayers(60.0, json::object(), "nodwell", true);
    int turningWithoutDwell = 0;
    for (const char* name : kCast) {
        const auto it = control.find(name);
        REQUIRE(it != control.end());
        INFO(name << ": with dwell 0 the look layer applied on " << it->second.framesLookApplied
                  << " frames");
        turningWithoutDwell += it->second.framesLookApplied > 0 ? 1 : 0;
    }
    // `sage` is expected to keep turning: its head is driven by the `interest` *behaviour*, which
    // is a different mechanism from the `interest` considerer and has no dwell of its own. So the
    // control is "four went to zero", not "all five did" -- and naming that here is what stops the
    // next person reading a passing control as proof of something it does not say.
    INFO("only sage, whose head is driven by its `interest` behaviour, may still turn");
    CHECK(turningWithoutDwell <= 1);
}
