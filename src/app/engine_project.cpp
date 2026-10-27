// The Engine's project persistence: the project document it writes (`projectDocument`, save, copy, bundle), the
// load that reads one back (`loadProject`, `newProject`), the unsaved-changes sample, and the asset references and
// relinking both directions share. Split from engine.cpp (QA pass 2); the members are declared in engine.hpp.

#include "app/engine.hpp"
#include "audio/capture_coreaudio.hpp"
#include "app/project_paths.hpp"

#include "scene/authored_cut.hpp"

#include "organism/mushroom.hpp"
#include "scene/tree_generated.hpp"

#include "seq/layer_sink.hpp"
#include "seq/section_actions.hpp"
#include "stage/setpiece.hpp"

#include "core/phase_profiler.hpp"
#include "core/interaction_latency.hpp"
#include "core/phase2_probe.hpp" // TEMPORARY: ui-responsiveness phase 2
#include <bit>
#include <optional>

#include <cstdlib>
#include <string_view>
#include <functional>
#include <map>
#include <set>

#include "assets/image.hpp"
#include "core/hash.hpp"
#include "core/log.hpp"
#include "core/vortex.hpp"
#include "core/wind.hpp"
#include "params/serialization.hpp"
#include "world/effects/distortion_frame.hpp"
#include "world/effects/effect_params.hpp"
#include "world/effects/effect_registry.hpp"
#include "world/effects/particle_emitter.hpp"
#include "world/effects/entity_fx.hpp"
#include "world/effects/history_bank.hpp"
#include "world/effects/ribbon_frame.hpp"
#include "world/effects/shell_frame.hpp"
#include "world/effects/star_field.hpp"
#include "world/effects/transform_frame.hpp"

#include <fmt/ranges.h>

#include <nlohmann/json.hpp>

#include <fstream>

#include <algorithm>
#include <cmath>
#include <cctype>
#include <cstring>
#include <chrono>
#include <thread>


namespace avgen::app {

namespace {

// Project-relative path policy: relative when on the same root (".." allowed so a project can sit
// beside its assets), absolute otherwise.
std::string relativeTo(const std::filesystem::path& file, const std::filesystem::path& baseDir) {
    std::error_code ec;
    auto abs = std::filesystem::weakly_canonical(file, ec);
    if (ec) {
        abs = std::filesystem::absolute(file).lexically_normal();
    }
    auto base = std::filesystem::weakly_canonical(baseDir, ec);
    if (ec) {
        base = std::filesystem::absolute(baseDir).lexically_normal();
    }
    if (abs.root_name() != base.root_name()) {
        return abs.generic_string();
    }
    const auto rel = abs.lexically_relative(base);
    return rel.empty() ? abs.generic_string() : rel.generic_string();
}

// Asset references in the project's "assets" block and shader entries: an object
// { "path", "size", "sha256" } (the old bare string form is still read). Size and hash identify
// the file by content so a moved asset can be relinked.
struct AssetRef {
    std::string path;
    std::uintmax_t size = 0;
    bool hasSize = false;
    std::string sha256;
};

std::optional<AssetRef> readAssetRef(const nlohmann::json& j) {
    AssetRef ref;
    if (j.is_string()) {
        ref.path = j.get<std::string>();
        return ref;
    }
    if (!j.is_object() || !j.contains("path") || !j["path"].is_string()) {
        return std::nullopt;
    }
    ref.path = j["path"].get<std::string>();
    if (j.contains("size") && j["size"].is_number_unsigned()) {
        ref.size = j["size"].get<std::uintmax_t>();
        ref.hasSize = true;
    }
    if (j.contains("sha256") && j["sha256"].is_string()) {
        ref.sha256 = j["sha256"].get<std::string>();
    }
    return ref;
}

// Writes { "path": relative, "size", "sha256" } for a file (size/hash omitted when unreadable).
nlohmann::json assetRefJson(const std::filesystem::path& file, const std::filesystem::path& baseDir) {
    nlohmann::json j = nlohmann::json::object();
    j["path"] = relativeTo(file, baseDir);
    std::error_code ec;
    const auto size = std::filesystem::file_size(file, ec);
    if (!ec) {
        j["size"] = size;
        if (auto hash = sha256File(file)) {
            j["sha256"] = *hash;
        }
    }
    return j;
}

std::string assetRefPath(const nlohmann::json& j) {
    const auto ref = readAssetRef(j);
    return ref ? ref->path : std::string();
}

void setAssetRefPath(nlohmann::json& j, const std::string& path) {
    if (j.is_object()) {
        j["path"] = path;
    } else {
        j = path;
    }
}

// Looks for a moved asset under `root` (depth <= 6): the same file name, preferring the same
// size; with a stored hash the content must match. Returns the first acceptable candidate.
constexpr int kRelinkMaxDepth = 6;

std::optional<std::filesystem::path> findRelinkCandidate(const AssetRef& ref, const std::filesystem::path& missing,
                                                         const std::filesystem::path& root) {
    std::error_code ec;
    const auto wanted = missing.filename();
    if (wanted.empty() || !std::filesystem::is_directory(root, ec)) {
        return std::nullopt;
    }
    std::vector<std::pair<bool, std::filesystem::path>> candidates; // (size matches, path)
    auto it = std::filesystem::recursive_directory_iterator(
        root, std::filesystem::directory_options::skip_permission_denied, ec);
    for (; !ec && it != std::filesystem::recursive_directory_iterator(); it.increment(ec)) {
        if (it.depth() >= kRelinkMaxDepth) {
            it.disable_recursion_pending();
        }
        if (!it->is_regular_file(ec) || it->path().filename() != wanted) {
            continue;
        }
        const auto candidate = it->path().lexically_normal();
        if (candidate == missing) {
            continue;
        }
        const auto size = it->file_size(ec);
        const bool sizeMatches = !ec && (!ref.hasSize || size == ref.size);
        candidates.emplace_back(sizeMatches, candidate);
    }
    std::stable_sort(candidates.begin(), candidates.end(),
                     [](const auto& a, const auto& b) { return a.first && !b.first; });
    for (const auto& [sizeMatches, candidate] : candidates) {
        if (!ref.sha256.empty()) {
            auto hash = sha256File(candidate);
            if (!hash || *hash != ref.sha256) {
                continue; // same name, different content
            }
            return candidate;
        }
        if (ref.hasSize && !sizeMatches) {
            continue;
        }
        return candidate;
    }
    return std::nullopt;
}

// Files a glTF may reference next to itself (external buffers and images); copied with bundles.
bool isGltfSidecar(const std::filesystem::path& p) {
    std::string ext = p.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return ext == ".bin" || ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".ktx2" || ext == ".webp";
}

// Walks a scene file's node assets (recursively through nested scene files) and its environment
// map, calling `visit(absolutePath, isSceneFile)` for each.
void visitSceneFileAssets(const std::filesystem::path& sceneFile,
                          const std::function<void(const std::filesystem::path&, bool)>& visit, int depth = 0) {
    if (depth > scene::Composition::kMaxNestingDepth) {
        return;
    }
    std::ifstream in(sceneFile);
    nlohmann::json doc = nlohmann::json::parse(in, nullptr, false);
    if (!doc.is_object()) {
        return;
    }
    const auto dir = sceneFile.parent_path();
    if (doc.contains("environment") && doc["environment"].is_object() && doc["environment"].contains("map") &&
        doc["environment"]["map"].is_string()) {
        visit(resolveFrom(doc["environment"]["map"].get<std::string>(), dir), false);
    }
    if (doc.contains("materialPrograms") && doc["materialPrograms"].is_array()) {
        for (const auto& entry : doc["materialPrograms"]) {
            if (entry.is_string()) { // a library material referenced by path rather than inlined
                visit(resolveFrom(entry.get<std::string>(), dir), false);
            }
        }
    }
    if (doc.contains("nodes") && doc["nodes"].is_array()) {
        for (const auto& node : doc["nodes"]) {
            if (!node.is_object() || !node.contains("asset") || !node["asset"].is_string()) {
                continue;
            }
            const auto asset = resolveFrom(node["asset"].get<std::string>(), dir);
            const bool nested = node.value("kind", std::string()) == "scene";
            visit(asset, nested);
            if (nested && asset != sceneFile) {
                visitSceneFileAssets(asset, visit, depth + 1);
            }
        }
    }
}

// ADR-158's aim-follow entry, in the one spelling both `cameraAimFollow` and ADR-582's parked copy
// use. One writer and one reader for both keys, so a field added to one cannot be dropped by the
// other (docs/testing.md #38: a fact that lives in a copied region lives in N copies).
nlohmann::json aimFollowToJson(std::span<const scene::AimFollow> follow) {
    nlohmann::json shots = nlohmann::json::array();
    for (const scene::AimFollow& shot : follow) {
        nlohmann::json entry{{"start", shot.startSeconds},
                             {"end", shot.endSeconds},
                             {"hero", shot.hero},
                             {"heroAtCut", {shot.heroAtCut.x, shot.heroAtCut.y, shot.heroAtCut.z}}};
        // ADR-891: written only for a continuous take, so an edited cut's document is unchanged.
        if (shot.joinInSeconds > 0.0) {
            entry["joinIn"] = shot.joinInSeconds;
        }
        if (shot.joinOutSeconds > 0.0) {
            entry["joinOut"] = shot.joinOutSeconds;
        }
        shots.push_back(std::move(entry));
    }
    return shots;
}

std::vector<scene::AimFollow> aimFollowFromJson(const nlohmann::json& array, const char* key,
                                                const std::function<void(std::string)>& warn) {
    std::vector<scene::AimFollow> shots;
    if (!array.is_array()) {
        return shots;
    }
    for (const auto& entry : array) {
        if (!entry.is_object() || !entry.contains("heroAtCut") || !entry["heroAtCut"].is_array() ||
            entry["heroAtCut"].size() != 3) {
            warn(fmt::format("{}: a shot without a 'heroAtCut' position was skipped", key));
            continue;
        }
        scene::AimFollow shot;
        shot.startSeconds = entry.value("start", 0.0);
        shot.endSeconds = entry.value("end", 0.0);
        shot.hero = entry.value("hero", std::string{});
        const auto& at = entry["heroAtCut"];
        shot.heroAtCut = glm::vec3(at[0].get<float>(), at[1].get<float>(), at[2].get<float>());
        shot.joinInSeconds = std::max(entry.value("joinIn", 0.0), 0.0);
        shot.joinOutSeconds = std::max(entry.value("joinOut", 0.0), 0.0);
        if (shot.hero.empty() || !(shot.endSeconds > shot.startSeconds)) {
            warn(fmt::format("{}: a shot with no hero or no duration was skipped", key));
            continue;
        }
        shots.push_back(std::move(shot));
    }
    return shots;
}

// ADR-582's `parkedDirector` block. The tracks go through the timeline's own writer and reader and
// the shots through `CameraShot`'s, so a parked track or shot is literally the same document a live
// one is and cannot drift from it in what it supports.
nlohmann::json parkedCutToJson(const ParkedDirectorsCut& cut) {
    nlohmann::json out = nlohmann::json::object();
    params::Timeline scratch;
    scratch.tracks() = cut.tracks;
    out["tracks"] = scratch.toJson()["tracks"];
    out["aimFollow"] = aimFollowToJson(cut.aimFollow);
    nlohmann::json shots = nlohmann::json::array();
    for (const scene::CameraShot& shot : cut.cameraShots) {
        shots.push_back(shot.toJson());
    }
    out["cameraShots"] = std::move(shots);
    if (cut.continuousTake) {
        out["continuousTake"] = true; // ADR-892; absent is every other cut, and every older file
    }
    return out;
}

Result<ParkedDirectorsCut> parkedCutFromJson(const nlohmann::json& doc,
                                             const std::function<void(std::string)>& warn) {
    if (!doc.is_object()) {
        return fail("'parkedDirector' must be a JSON object");
    }
    ParkedDirectorsCut cut;
    if (const auto tracks = doc.find("tracks"); tracks != doc.end()) {
        params::Timeline scratch;
        if (auto ok = scratch.fromJson(nlohmann::json{{"tracks", *tracks}}); !ok) {
            return fail("parkedDirector: {}", ok.error().message);
        }
        cut.tracks = std::move(scratch.tracks());
    }
    if (const auto follow = doc.find("aimFollow"); follow != doc.end()) {
        cut.aimFollow = aimFollowFromJson(*follow, "parkedDirector.aimFollow", warn);
    }
    cut.continuousTake = doc.value("continuousTake", false); // ADR-892
    if (const auto shots = doc.find("cameraShots"); shots != doc.end()) {
        if (!shots->is_array()) {
            return fail("parkedDirector.cameraShots must be an array");
        }
        for (const auto& entry : *shots) {
            auto shot = scene::CameraShot::fromJson(entry);
            if (!shot) {
                return fail("parkedDirector.cameraShots: {}", shot.error().message);
            }
            cut.cameraShots.push_back(std::move(*shot));
        }
    }
    return cut;
}

} // namespace

// ---- the project document -- what a save writes, and what it compares against -----------------
//
// `projectDocument` is the serialiser; `saveProject` below writes what it returns, and the
// unsaved-changes check at ADR-440 compares against it. One function, so a save and a dirty
// check cannot disagree about what the project is.

nlohmann::json Engine::projectDocument(const std::filesystem::path& path) {
    nlohmann::json doc = params::saveProject(params_, modulator_, &sources_, &presets_);
    const auto dir = std::filesystem::absolute(path).parent_path();
    doc["app"] = {{"name", "avgen"}, {"version", kAppVersion}};
    // Shader layer paths relative to the project, with size and content hash for relinking.
    nlohmann::json shaders = shaderLayers_.toJson();
    for (auto& entry : shaders) {
        if (entry.is_object() && entry.contains("path") && entry["path"].is_string()) {
            const nlohmann::json ref = assetRefJson(entry["path"].get<std::string>(), dir);
            for (const auto& [key, value] : ref.items()) {
                entry[key] = value;
            }
        }
    }
    doc["shaders"] = std::move(shaders);
    if (!timeline_.empty()) {
        nlohmann::json timeline = timeline_.toJson();
        // Tracks the sequence baked are derived from it, exactly as its layers are, and the
        // sequence is saved below. Saving both means the next load reads them *and* re-bakes them,
        // and two tracks writing camera/position is not a blend -- it is whichever one the timeline
        // happens to apply second. So the file holds what the author wrote by hand; the bake is
        // recreated from the shots it came from.
        if (!sequenceTargets_.empty() && timeline.contains("tracks") && timeline["tracks"].is_array()) {
            nlohmann::json authored = nlohmann::json::array();
            for (const auto& track : timeline["tracks"]) {
                const std::string target = track.value("target", std::string{});
                if (std::find(sequenceTargets_.begin(), sequenceTargets_.end(), target) ==
                    sequenceTargets_.end()) {
                    authored.push_back(track);
                }
            }
            timeline["tracks"] = std::move(authored);
        }
        if (!timeline.value("tracks", nlohmann::json::array()).empty() ||
            !timeline.value("cues", nlohmann::json::array()).empty()) {
            doc["timeline"] = std::move(timeline);
        }
    }
    // ADR-225: the Auto-director's controls, beside the cut they produced.
    //
    // Written only when the author has moved something, so a project nobody directed keeps the file
    // it had -- the same rule `cameraAimFollow` and `cameraShotSpans` below follow, and the reason a
    // round trip on an untouched project is byte-stable.
    if (!(autoDirector_ == AutoDirectorSettings{})) {
        // `autoDirector`, not `director`: `director` is already the World Director's knob mappings
        // (ADR-088), written a few lines below. Two blocks under one key is one block, and the
        // second one written wins -- which is how this was caught: the load refused a document
        // whose `director` block was the other subsystem's.
        doc["autoDirector"] = autoDirector_.toJson();
    }
    // ADR-249: the song plan Song Mode directs to, beside the settings that say how freely.
    //
    // Its own key rather than a field of `autoDirector`, because the two are different kinds of
    // thing: the settings are a person's preferences and the plan is the piece's content. Written
    // only when there is one, so nothing changes for a project that has never used Song Mode.
    if (!songPlan_.empty()) {
        doc["songPlan"] = songPlan_.toJson();
    }
    // ADR-755: the Director Plans this project's content came from, as its provenance. Written only
    // when there is one, so a project that never used the Director is byte-for-byte what it was.
    if (!directingPlans_.empty() || !unreadableDirectingPlans_.empty()) {
        nlohmann::json plans = nlohmann::json::array();
        for (const directing::Plan& plan : directingPlans_) {
            plans.push_back(plan.toJson());
        }
        for (const nlohmann::json& raw : unreadableDirectingPlans_) {
            plans.push_back(raw);
        }
        doc["directingPlans"] = std::move(plans);
    }
    // ADR-931: what the evaluator said about those plans, beside them. Written only when there is
    // one, for the same byte-stability reason.
    if (!directingEvaluations_.empty()) {
        nlohmann::json evaluations = nlohmann::json::array();
        for (const directing::EvaluationReport& e : directingEvaluations_) {
            evaluations.push_back(e.toJson());
        }
        doc["directingEvaluations"] = std::move(evaluations);
    }
    // ADR-158: which hero each directed shot was cut for, beside the tracks it accompanies.
    //
    // A sibling of `timeline` rather than part of the scene, because that is what it belongs to: the
    // shots are camera automation, and a scene shared between two projects must not carry one
    // project's cut. Written only when there is a cut, so a project that was never directed keeps
    // the file it had.
    if (const auto* comp = composition(); comp != nullptr && !comp->aimFollow().empty()) {
        doc["cameraAimFollow"] = aimFollowToJson(comp->aimFollow());
    }
    // ADR-892: the cut in force is a continuous take, which owns the frame. Beside the follow table
    // for the same reason: an offline render loads the project, and a render whose authored camera
    // track cut where the window did not would be a different film. Written only when true, so every
    // other project keeps the document it had.
    if (const auto* comp = composition(); comp != nullptr && comp->continuousTake()) {
        doc["cameraContinuousTake"] = true;
    }
    // ADR-207: the other half of the same bake -- when the camera travels and when it holds, which
    // is what a world effect time-gates on. A sibling of `cameraAimFollow` for the identical reason,
    // and saved for a reason the aim-follow table taught the hard way: an offline render loads a
    // *project document*, so anything the director left only in memory is a cut the render does not
    // have. Without this the beam and the pulse were correct in the window and absent from every
    // frame anybody exported.
    if (!shotSpans_.empty()) {
        nlohmann::json spans = nlohmann::json::array();
        for (const world::ShotSpan& span : shotSpans_) {
            nlohmann::json entry{{"start", span.start},
                                 {"end", span.end},
                                 {"travel", span.travel},
                                 {"spotlight", span.spotlight},
                                 {"subject", span.subject},
                                 {"subjectPosition",
                                  {span.subjectPosition.x, span.subjectPosition.y, span.subjectPosition.z}},
                                 {"subjectRadius", span.subjectRadius}};
            if (!span.handoff.empty()) {
                entry["handoff"] = span.handoff;
                entry["handoffPosition"] = {span.handoffPosition.x, span.handoffPosition.y,
                                            span.handoffPosition.z};
            }
            spans.push_back(std::move(entry));
        }
        doc["cameraShotSpans"] = std::move(spans);
    }
    // ADR-582: a cut whose owner took the camera back is parked, not gone. The spans above are
    // written whether the director is steering or parked -- they are the film's focus schedule and
    // effects follow them either way -- and this block holds the half that steers the camera, so
    // "Resume director" in the next session puts back exactly what was there.
    //
    // Written only when something is parked, so a project that was never directed, or one whose
    // director is steering, keeps the file it had; its presence is the parked marker.
    if (!parkedCut_.empty()) {
        doc["parkedDirector"] = parkedCutToJson(parkedCut_);
    }
    // ADR-207 and ADR-230, on the argument `cameraShotSpans` above makes, for the two families it
    // did not cover -- and this time it is not a derived cut but the effects themselves.
    //
    // The effects belong to the `Composition`, and a project whose scene came from a file saves that
    // scene **by reference**: `assets.scene.path` plus a hash of the bytes already on disk. Nothing
    // writes the scene file. So an aurora added through the (since removed) World Effects panel lived in the composition the window was drawing and in no document any render
    // reads -- and a render builds its own `Engine` and loads the project (`startRenderFromUi` saves
    // the project for you and then renders *that file*). That is the whole of "the sky effects are in
    // the app and not in the video".
    //
    // Measured, on the owner's own project: `glowmere-valley-2-multicam.json` carries **94**
    // `atmos/Aurora/...` and `atmos/Bioluminescent Comet/...` parameter values, 47 each, and its
    // scene file authors **no** `atmosphericEffects` at all. Loading it warns 94 times about a
    // parameter nobody owns. The save kept every number of the aurora and lost the aurora.
    //
    // Written only when the session's list is not the one the scene file holds, so a project that
    // opened a scene and rendered it keeps the file it had -- the rule `autoDirector` and
    // `cameraShotSpans` are written under. An *emptied* list is a difference like any other and is
    // written as an empty array: a deletion that only survives while the process does is the same
    // defect pointing the other way. Both sides are compared after a round trip through the same
    // `fromJson`/`toJson`, because a hand-typed `0.0055` and the float the engine ran it as are one
    // authored value and must not read as an edit.
    //
    // Nothing is written for an inlined composition: `assets.scene.inline` is `Composition::toJson`,
    // which already carries the `effects` array. A second copy beside it would be a second answer.
    if (const scene::Composition* comp = composition(); comp != nullptr && !compositionPath_.empty()) {
        nlohmann::json sceneDoc;
        if (std::ifstream sceneIn(compositionPath_); sceneIn) {
            sceneDoc = nlohmann::json::parse(sceneIn, nullptr, false);
        }
        // The file's own list, put through the parse the engine puts it through. An entry the parser
        // refuses yields a null, which compares unequal to any array and so records the session's
        // list -- the safe direction: a scene file this build cannot read is not evidence that the
        // render already has what the window has.
        const auto onDisk = [&](const char* key, auto parse) {
            nlohmann::json out = nlohmann::json::array();
            if (!sceneDoc.is_object() || !sceneDoc.contains(key) || !sceneDoc[key].is_array()) {
                return out;
            }
            for (const auto& entry : sceneDoc[key]) {
                auto one = parse(entry);
                if (!one) {
                    return nlohmann::json{};
                }
                out.push_back(one->toJson());
            }
            return out;
        };
        // ADR-702: one list, every owner's, under one key.
        nlohmann::json liveEffects = nlohmann::json::array();
        for (const world::EffectInstance& effect : comp->effects()) {
            liveEffects.push_back(effect.toJson());
        }
        if (liveEffects != onDisk("effects", [](const nlohmann::json& j) {
                return world::EffectInstance::fromJson(j);
            })) {
            doc["effects"] = std::move(liveEffects);
        }
        // ADR-276, and it is the same defect a third time. Starring an object in the world editor
        // calls `Composition::setHeroes`; the composition is saved **by reference**; so the star
        // lived in the window and in no document any render reads. Measured on the owner's own
        // session: one hero starred, one hero in the composition, **zero** after a save and a
        // reload -- and an offline render builds its own `Engine` and loads the project, so a
        // starred hero never reached a deliverable at all.
        //
        // It is written here rather than into the scene file for the reason ADR-271 gives and
        // ADR-207/230 gave before it: nothing but a "Save Scene As..." dialog writes a scene file,
        // so a re-authored hero would be discarded by the next Cmd-S; and a scene is shared between
        // projects -- `glowmere-stylized.scene.json` backs two -- so an editor that wrote scenes
        // would change a project the user did not open and re-fingerprint it on every star.
        //
        // The same rule as its two siblings above: written only when the session's list is not the
        // one the scene file holds, compared after a round trip through the same `fromJson`/
        // `toJson` so a hand-typed number and the float the engine ran it as are one authored value
        // rather than an edit; and an *emptied* list is a difference like any other, written as an
        // empty array, because an unstar that only survives while the process does is the identical
        // defect pointing the other way.
        //
        // `cameraAimFollow` above names heroes by name, and this is what makes that table mean
        // something after a reload: a cut whose heroes did not survive the save is a cut aimed at
        // nothing (`Composition::applyDirectedAim` skips an entry whose hero is not in `heroes()`).
        // `authoredHeroes()`, not `heroes()`. The live list follows each hero's node *final*, so a
        // hero on a moving body walks with it; writing that is what made every save record the
        // walk. The note further down recording `heroes (the key appears once the herd has
        // walked)` as expected churn was this defect being logged as noise rather than diagnosed.
        nlohmann::json liveHeroes = nlohmann::json::array();
        for (const world::HeroPoint& hero : comp->authoredHeroes()) {
            liveHeroes.push_back(hero.toJson());
        }
        if (liveHeroes != onDisk("heroes", [](const nlohmann::json& j) {
                return world::HeroPoint::fromJson(j);
            })) {
            doc["heroes"] = std::move(liveHeroes);
        }
        // ADR-330, and it is the same defect a fourth time -- the one the owner reported first and
        // the one that survived ADR-276. Deleting an object in the world editor calls
        // `Composition::detachNode`; the composition is saved by reference; so the deletion lived in
        // the window and in no document any render reads. Measured on the owner's own project
        // (`glowmere-valley-2-multicam.json`): **80 nodes, 79 after the delete, 80 again after a
        // save and a reload**. The object came back, so it was in every frame of every export.
        //
        // Not the three keys above's shape, and the difference is the whole of why this one is
        // harder. `effects` and `heroes` are small lists the project can
        // simply hold a copy of. The nodes are the scene: a copy would be 80 objects, would make the
        // shared scene file dead for this project the moment anybody corrected it, and would be a
        // second answer to every question the `parameters` block already answers about where a node
        // is. So what is written is the **difference, by name** -- and `removed` is a negative fact
        // with nothing left to carry it, which is why it needed a record at all.
        //
        // `scene::nodeEditsAgainst` is the whole of the decision and is pure, so a test can assert
        // what a given pair of documents produces without an engine (ADR-278's argument for
        // `unknownKeys`, applied again). It returns null when the session's node set is the file's,
        // which is what keeps a save of an untouched project byte-stable -- the control that makes
        // the positive arms mean anything.
        //
        // `comp->toJson()` rather than a bespoke walk of `nodes()`: an added node then reaches the
        // project in exactly the serialisation a scene file would have given it, and graph-installed
        // nodes are left out by the one piece of code that already knows which those are.
        if (nlohmann::json edits = scene::nodeEditsAgainst(comp->toJson().value("nodes", nlohmann::json()), sceneDoc);
            !edits.is_null()) {
            doc["sceneNodes"] = std::move(edits);
        }
        // The authored lights, and this is the same defect a **fifth** time -- ADR-207's world
        // effects, ADR-230's atmospherics, ADR-276's heroes and ADR-330's nodes were the first
        // four. Adding a light in the Lights panel calls `Composition::setAuthoredLights`; the
        // composition is saved **by reference** (`assets.scene.path` plus a hash of the bytes
        // already on disk); so without this the light lived in the window and in no document any
        // render reads -- and an offline render builds its own `Engine` and loads the project, so
        // it would never have reached a deliverable at all. `check_project_integrity.py` passes on
        // that result, because the file is still perfectly valid; it is just missing a light.
        //
        // The whole list rather than ADR-330's difference-by-name, and the reason is the shape of
        // the data rather than a preference: a node's numbers are already in `parameters`, so the
        // project only owes the *set*. Most of a light's twenty-five fields -- `type`, `role`,
        // `node`, `up`, the area extents -- are not parameters and never will be, so a by-name
        // difference would record which lights exist and lose what they are. That puts lights in
        // `effects`/`heroes`' family, which is small lists the project can simply hold.
        //
        // `authoredLightsAgainst` canonicalises the file's own list out and back through the same
        // parser before comparing, so a scene that spells out `"intensity": 1.0` does not read as
        // an edit and an untouched project stays byte-stable.
        // `authoredLightsRestJson()` rather than `toJson()["lights"]`: the latter carries the
        // per-frame parameter writeback, so a project that merely dims a light would record its
        // whole lighting rig -- a second answer to a question `parameters` already answers.
        if (nlohmann::json lights = scene::authoredLightsAgainst(comp->authoredLightsRestJson(), sceneDoc);
            !lights.is_null()) {
            doc["lights"] = std::move(lights);
        }
        // The camera collection and the camera track, and this is the same defect a **sixth** time
        // (ADR-751). `Engine::setCameraDirection` -- the Cameras panel's "+ Camera", "Delete" and
        // "Cut to this camera", and every camera the Director compiles -- writes the composition;
        // the composition is saved by reference; so a camera or a cut lived in the window and in no
        // document a render reads. Measured before this: 2 cameras and 1 shot in the session, 1 and
        // 0 after a save and a reload (`test_director_persistence.cpp`).
        //
        // The whole collection, in `lights`' family rather than ADR-330's difference-by-name: a
        // rig's placement, aim node and lens are not parameters, so the project owes what the rigs
        // *are*, not only which exist. **Authored shots only.** `Directed` shots are Song Mode's
        // (ADR-249) and are regenerated at load from `autoDirector` and `songPlan`, which the
        // project already saves; writing them would photograph the run and make every directed
        // project look edited. Compared after a round trip through the same parser, so an untouched
        // project writes nothing and stays byte-stable.
        const auto authoredOnly = [](scene::CameraDirection direction) {
            std::erase_if(direction.shots, [](const scene::CameraShot& shot) {
                return shot.origin == scene::CameraShot::Origin::Directed;
            });
            return direction.toJson();
        };
        nlohmann::json fileCameras;
        if (!sceneDoc.is_object() || !sceneDoc.contains("cameraDirection")) {
            scene::CameraDirection none;
            none.ensureMainCamera();
            fileCameras = authoredOnly(std::move(none));
        } else if (auto parsed = scene::CameraDirection::fromJson(sceneDoc["cameraDirection"]); parsed) {
            parsed->ensureMainCamera();
            fileCameras = authoredOnly(std::move(*parsed));
        }
        if (nlohmann::json liveCameras = authoredOnly(comp->cameraDirection()); liveCameras != fileCameras) {
            doc["cameraDirection"] = std::move(liveCameras);
        }
        // The staging description, and this is the same defect a **seventh** time (ADR-929). A plan's
        // set pieces compile into `Composition::setStaging`; the composition is saved by reference;
        // so without this every UFO event a plan placed lived in the window and in no document a
        // render reads -- and GV3's generator compiles its plan headless and renders the saved file.
        //
        // The whole description, in `lights`' and `cameraDirection`'s family: a scenario's beats are
        // not parameters. Its parameter VALUES are, and they stay where they always were -- the
        // `parameters` block -- so this compares the description as installed (`staging()`), not the
        // one with the bases folded in (`capturedStaging()`): a project that merely tuned the
        // abduction's hover height keeps the file it had. Compared after a round trip through the
        // same parser, so an untouched project writes nothing and stays byte-stable.
        nlohmann::json fileStaging;
        if (!sceneDoc.is_object() || !sceneDoc.contains("staging")) {
            fileStaging = stage::stagingToJson(stage::StagingDesc{});
        } else if (auto parsed = stage::stagingFromJson(sceneDoc["staging"]); parsed) {
            fileStaging = stage::stagingToJson(*parsed);
        }
        if (nlohmann::json liveStaging = stage::stagingToJson(comp->staging()); liveStaging != fileStaging) {
            doc["staging"] = std::move(liveStaging);
        }
    }
    if (!states_.empty()) {
        doc["states"] = states_.toJson();
    }
    if (!palette_.empty()) {
        doc["palette"] = palette_.toJson(); // ADR-1043
    }
    if (!director_.mappings.empty()) {
        doc["director"] = director_.toJson();
    }
    if (!worldMacros_.empty()) {
        nlohmann::json macros = nlohmann::json::array();
        for (const WorldMacro& m : worldMacros_) {
            macros.push_back(m.toJson());
        }
        doc["worldMacros"] = std::move(macros);
    }
    // The 2D composition (ADR-083). Pulled back from the parameters first: the inspector and the
    // timeline write through the parameter set, and a project saved from the authored fields alone
    // would lose every edit made with a slider.
    layers_.pullAuthored();
    if (!layers_.empty()) {
        nlohmann::json composition = layers_.toJson();
        // Layers the sequence made are derived from its overlay cues, and the cues are saved just
        // below. Writing both would save the same lyric twice and, worse, restore it with a layer
        // id the next install will not reuse -- so the saved parameter values would attach to a
        // layer that no longer exists. The sequence owns them; "composition" holds what the author
        // made by hand.
        if (composition.contains("layers") && composition["layers"].is_array()) {
            nlohmann::json authored = nlohmann::json::array();
            for (const auto& layer : composition["layers"]) {
                const std::string name = layer.value("name", std::string{});
                if (!seq::CompositionLayerSink::ownedName(name)) {
                    authored.push_back(layer);
                }
            }
            composition["layers"] = std::move(authored);
        }
        if (!composition.value("layers", nlohmann::json::array()).empty()) {
            doc["composition"] = std::move(composition);
        }
        // Their parameters are derived too. Left in, a reload would try to apply "layers/7/anchor"
        // before the install has made layer 7, and warn about a parameter the author never wrote.
        if (doc.contains("parameters") && doc["parameters"].is_object()) {
            std::vector<std::string> doomed;
            for (const auto& layer : layers_.layers()) {
                if (!seq::CompositionLayerSink::ownedName(layer->name)) {
                    continue;
                }
                for (const std::string& path : layer->parameterPaths()) {
                    doomed.push_back(path);
                }
            }
            for (const std::string& path : doomed) {
                doc["parameters"].erase(path);
            }
        }
    }
    // The same argument, for the bodies a staging scenario moves (ADR-264).
    //
    // A scenario hides an animal once it has abducted it and shows the beam while it fires, so the
    // `visible` and the transform such a node has at the instant somebody presses save are a
    // photograph of a run. A project's `parameters` are applied *over* its scene at load, so saving
    // them means the next load opens with an invisible goat and a beam that never switches off --
    // and the beam's saved scale once ran it at 3.07 m against the 7.8 m the scene authors, which is
    // narrower than the animals it lifts.
    //
    // Four consecutive saves of Glowmere re-introduced exactly this while the fix for the last one
    // was being written, which is what moved it from a cleanup script to the save itself. A script
    // that has to be run after every save is a script somebody will forget to run.
    //
    // The scene keeps saying where these bodies start and whether they are visible; that is
    // authorship and it is not touched. What goes is only the project's copy of where the run left
    // them.
    if (doc.contains("parameters") && doc["parameters"].is_object()) {
        if (const scene::Composition* comp = composition(); comp != nullptr && !comp->staging().empty()) {
            const std::vector<entity::EntityDesc>& descs = comp->entities();
            std::vector<std::string> names;
            names.reserve(descs.size());
            for (const entity::EntityDesc& d : descs) {
                names.push_back(d.name);
            }
            const auto nodeOf = [&](const std::string& name) {
                for (const entity::EntityDesc& d : descs) {
                    if (d.name == name) {
                        return d.node.empty() ? d.name : d.node;
                    }
                }
                return name;
            };
            const auto tagsOf = [&](const std::string& name) {
                for (const entity::EntityDesc& d : descs) {
                    if (d.name == name) {
                        return d.tags;
                    }
                }
                return std::vector<std::string>{};
            };
            const std::set<std::string> owned =
                stage::scenarioOwnedNodes(comp->staging(), nodeOf, tagsOf, names);
            std::vector<std::string> doomed;
            for (const auto& [path, value] : doc["parameters"].items()) {
                const std::size_t first = path.find('/');
                const std::size_t second = path.find('/', first + 1);
                if (first == std::string::npos || second == std::string::npos) {
                    continue;
                }
                if (path.compare(0, first, "nodes") != 0) {
                    continue;
                }
                const std::string body = path.substr(first + 1, second - first - 1);
                const std::string field = path.substr(second + 1);
                if (owned.count(body) == 0) {
                    continue;
                }
                if (field == "visible" || field == "position" || field == "rotation" || field == "scale") {
                    doomed.push_back(path);
                }
            }
            for (const std::string& path : doomed) {
                doc["parameters"].erase(path);
            }
        }
    }
    if (hasSequence()) {
        doc["sequence"] = sequence_.toJson();
    }
    doc["render"] = render_.toJson();
    // ADR-366. Written unconditionally, beside "render" and for the same reason: a block
    // that is only emitted when it differs from the default is a writer gated on its own
    // subject, which is the half of ADR-350's defect that survived the first fix of it.
    doc["pathtrace"] = pathTrace_.toJson();
    // The transport's persistent half (ADR-102). Additively, and only when there is something to
    // say: a project that never set a loop gains no key, so files written before this round-trip
    // unchanged. The playing state, the position and the playback rate are deliberately *not* here
    // -- they are how you are working, not what the piece is.
    if (const TransportLoop& loop = transport_.loop();
        loop.enabled || loop.endSeconds > loop.startSeconds) {
        doc["transport"] = {{"loop",
                             {{"enabled", loop.enabled},
                              {"start", loop.startSeconds},
                              {"end", loop.endSeconds}}}};
    }
    // The artist's tempo, and only the artist's (ADR-394). A detected tempo is not written --
    // re-analysing produces it again, and persisting it would make a measurement look like a
    // decision. An embedded tempo is not written either: it lives in the audio file, and writing a
    // stale copy here would outlive the file being replaced. What must survive a save is the one
    // thing nothing can recompute: that a person chose this number.
    if (tempoOverride_.available) {
        doc["transport"]["tempo"] = {{"bpm", tempoOverride_.bpm},
                                     {"source", audio::tempoProvenanceToken(tempoOverride_.source)}};
        if (beatAnchor_) {
            doc["transport"]["tempo"]["anchor"] = *beatAnchor_; // ADR-1783: the marked beat
        }
    }
    // ADR-1580/1581: the feeds, only when one is set.
    if (nlohmann::json feeds = feeds_.toJson(); !feeds.empty()) {
        doc["feeds"] = std::move(feeds);
    }
    // ADR-1020: the `sonic` block as it was loaded, with its notes path written relative to this file.
    if (sonic_) {
        nlohmann::json block = sonic_->document;
        if (!sonic_->notesPath.empty()) {
            const std::string rel = relativeTo(sonic_->notesPath, dir);
            if (block.contains("notes") && block["notes"].is_object()) { // ADR-1667: the object form, kept
                block["notes"]["path"] = rel;
            } else {
                block["notes"] = rel;
            }
        }
        doc["sonic"] = std::move(block);
    }
    // ADR-1084: the live quality hint, when the project stated one (an unstated hint stays unstated,
    // so the default can move without rewriting every project that never chose).
    if (liveQualityStrategy_) {
        doc["live"]["qualityStrategy"] = std::string(liveQualityStrategyToken(*liveQualityStrategy_));
    }
    // ADR-1100: the rest of the live block, each field only when stated.
    if (liveSettings_.profile) {
        doc["live"]["profile"] = std::string(qualityProfileToken(*liveSettings_.profile));
    }
    if (liveSettings_.minimumLevel) {
        doc["live"]["minimumLevel"] = std::string(liveQualityLevelToken(*liveSettings_.minimumLevel));
    }
    if (!liveSettings_.priority.empty()) {
        doc["live"]["priority"] = liveSettings_.priority;
    }
    if (liveSettings_.overrides) {
        doc["live"]["overrides"] = nlohmann::json::parse(ceilingJsonText(*liveSettings_.overrides));
    }
    doc["control"] = controlHub_.map().toJson();
    if (outputs_.is_array() && !outputs_.empty()) {
        doc["outputs"] = outputs_;
    }
    doc["control"]["tempoSource"] = tempoSourceName(tempoSource_);
    // ADR-896: the meter's settings (bar 1's beat, bars per phrase, phrases per section) are
    // parameters now and are saved in `parameters` with the rest; their "detect" values are saved as
    // such, so a measurement is never written down as a decision.
    nlohmann::json assets = nlohmann::json::object();
    // One plain clip is written as it always was -- `assets.audio`, a single reference -- so every
    // project made before arrangements existed round-trips byte for byte. Anything richer is a clip
    // list instead, and the two are never both present: two places naming the audio is two places to
    // disagree about it.
    const bool plainSingle =
        audioClips_.size() == 1 && audioClips_.front() == audio::AudioClip{audioClips_.front().file};
    if (plainSingle && !audioPath_.empty()) {
        assets["audio"] = assetRefJson(audioPath_, dir);
    } else if (!audioClips_.empty()) {
        nlohmann::json list = nlohmann::json::array();
        for (const audio::AudioClip& clip : audioClips_) {
            nlohmann::json item = nlohmann::json::object();
            item["file"] = assetRefJson(clip.file, dir);
            // Only what differs from the default, so a clip that was merely dropped on the timeline
            // reads as one line rather than as eight fields of zero.
            if (clip.startSeconds != 0.0) item["start"] = clip.startSeconds;
            if (clip.inSeconds != 0.0) item["in"] = clip.inSeconds;
            if (clip.durationSeconds != 0.0) item["duration"] = clip.durationSeconds;
            if (clip.gain != 1.0f) item["gain"] = clip.gain;
            if (clip.fadeInSeconds != 0.0) item["fadeIn"] = clip.fadeInSeconds;
            if (clip.fadeOutSeconds != 0.0) item["fadeOut"] = clip.fadeOutSeconds;
            if (!clip.enabled) item["enabled"] = false;
            if (!clip.name.empty()) item["name"] = clip.name;
            list.push_back(std::move(item));
        }
        assets["audioClips"] = std::move(list);
    }
    if (!stems_.empty()) {
        // ADR-1520: analysis-only files, never part of the arrangement above.
        nlohmann::json list = nlohmann::json::array();
        for (const StemState& stem : stems_) {
            nlohmann::json item = nlohmann::json::object();
            item["key"] = stem.source.key;
            item["label"] = stem.source.label;
            item["file"] = assetRefJson(stem.source.file, dir);
            if (stem.source.offsetSeconds != 0.0) item["offset"] = stem.source.offsetSeconds;
            if (stem.source.role != audio::SourceRole::Other) item["role"] = audio::sourceRoleName(stem.source.role); // ADR-1630
            if (stem.source.gainDb != 0.0f) item["gainDb"] = stem.source.gainDb; // ADR-1582
            list.push_back(std::move(item));
        }
        assets["stems"] = std::move(list);
    }
    if (!environmentPath_.empty()) {
        assets["environment"] = assetRefJson(environmentPath_, dir);
    }
    nlohmann::json sceneRef = nlohmann::json::object();
    if (auto* comp = composition()) {
        if (compositionPath_.empty()) {
            // An unsaved composition: keep it inline so the project stays self-contained.
            sceneRef["kind"] = "composition";
            sceneRef["inline"] = comp->toJson();
        } else {
            sceneRef["kind"] = "composition";
            sceneRef["path"] = assetRefJson(compositionPath_, dir);
        }
    } else if (auto* gltf = gltfScene()) {
        sceneRef["kind"] = "gltf";
        sceneRef["path"] = assetRefJson(gltf->path(), dir);
    } else {
        sceneRef["kind"] = "orb";
    }
    assets["scene"] = std::move(sceneRef);
    doc["assets"] = std::move(assets);
    return doc;
}

Result<void> Engine::writeProjectCopy(const std::filesystem::path& path) {
    const nlohmann::json doc = projectDocument(path);
    std::ofstream out(path);
    if (!out) {
        return fail("cannot write '{}'", path.string());
    }
    out << doc.dump(2) << '\n';
    if (!out) {
        return fail("could not finish writing '{}'", path.string());
    }
    return {};
}

Result<void> Engine::saveProject(const std::filesystem::path& path) {
    nlohmann::json doc = projectDocument(path);
    std::ofstream out(path);
    if (!out) {
        return fail("cannot write '{}'", path.string());
    }
    out << doc.dump(2) << '\n';
    projectPath_ = path;
    // What is now on disk, in the shape the comparison uses. `doc` is the bytes that were just
    // written, so this cannot disagree with them (ADR-440).
    projectBaseline_ = std::move(doc);
    projectBaselineValid_ = true;
    projectDirty_ = false;
    return {};
}

// ---- unsaved changes (ADR-440) ---------------------------------------------------------------
//
// **Why there is no `bool dirty_` set by every mutation.** This codebase has a documented family of
// defects where a hand-maintained list drifts from the thing it describes -- a visitor that skipped
// seven struct members, an unregister table missing two fields, a test harness copying a uniform
// block field by field. A dirty flag is the same shape, and its failure is silent in the direction
// that loses work: every new mutation site is one more chance to forget it. The honest basis is
// that a project is dirty when serialising it now differs from what a save would have written the
// last time it agreed with the disk.
//
// **Why that is not a comparison against the file.** Measured, on 2026-09-20, by loading eight
// example projects, changing nothing, and diffing an immediate save against the file it came from:
//
//     glowmere-valley-2        347 differing JSON paths   610 KB -> 618 KB
//     glowmere-valley-2-multicam  279                     675 KB -> 689 KB
//     temple                   801                        6.5 KB -> 60 KB
//     hero                    1069                        1.3 KB -> 67 KB
//     chamber                 1576                        3.1 KB -> 113 KB
//     fungi                   1584                        0.8 KB -> 92 KB
//     lab                     1879                        3.4 KB -> 121 KB
//     night-shift            19798                        33 KB -> 1.6 MB
//
// Not one project round-trips. A hand-written file records the handful of parameters somebody
// changed; a save writes every parameter the live session has registered, which is why night-shift
// grows forty-nine times. A prompt built on "differs from the file" fires on every project, every
// time, and a prompt that always fires trains the reflex that dismisses it.
//
// **So the baseline is a serialisation, not the file.** It is taken at the end of `loadProject` and
// again in `saveProject`, from the same builder the save uses, so both sides of the comparison are
// the same function of the same engine and the load's own lossiness cancels out. What is being
// measured is "has anything changed since this project was opened or saved", which is the question
// the dialog asks.
//
// **The one thing that still moves on its own**, from the same measurement, playing 600 frames
// (20 s) with no input at all and saving again from the same engine:
//
//     glowmere-valley-2-multicam   18 paths: heroes[0,2,3,4,5,6]/position[0..2]
//                                   2 paths: parameters/particles/visitor-beam/{spawnRate,emissive}
//     glowmere-valley-2             1 path:  heroes  (the key appears once the herd has walked)
//     the other six                 0 paths
//
// That is ADR-386's per-frame parameter writeback and the entity simulation, and it is the whole of
// it -- twenty paths across eight projects. `sampleDirty` absorbs it **by measuring it rather than
// naming it**: on a sample where the host reports that nothing touched the application, every
// difference is by definition the engine's own, so the baseline simply moves. No list of noisy keys
// exists to drift out of date, which is the property the flag could not have.
//
// And it is monotone: once dirty, dirty until a save or a load. Otherwise a quiet second after an
// edit would absorb the edit, which is the flag's failure mode reintroduced by the back door.
void Engine::sampleProjectDirty(bool touchedSinceLastSample) {
    if (projectDirty_ || !projectBaselineValid_) {
        return;
    }
    nlohmann::json doc = projectDocument(projectPath_);
    if (doc == projectBaseline_) {
        return;
    }
    if (touchedSinceLastSample) {
        projectDirty_ = true;
    } else {
        projectBaseline_ = std::move(doc);
    }
}

bool Engine::projectDirty(bool touchedSinceLastSample) {
    sampleProjectDirty(touchedSinceLastSample);
    return projectDirty_;
}

void Engine::markProjectSaved() {
    projectBaseline_ = projectDocument(projectPath_);
    projectBaselineValid_ = true;
    projectDirty_ = false;
}

// ---- loading a project, and starting a new one ------------------------------------------------

Result<void> Engine::requireProjectScene() const {
    if (projectSceneError_.empty()) {
        return {};
    }
    return fail("the project's scene did not load, so what would be drawn is not the project's world: {}",
                projectSceneError_);
}

Result<void> Engine::loadProject(const std::filesystem::path& path) try {
    // ---- the stages, and what they cost -----------------------------------------------------
    //
    // The list is fixed and known before the first byte is read, which is what makes "stage 3 of 9"
    // a countable fact rather than a guess (ADR-064: a fabricated bar is indistinguishable from a
    // real one). Each stage's wall-clock is kept for `lastLoadTimings()`, because "opening a
    // project freezes the application" is not a number and the first job was to make it one.
    static constexpr std::array<std::string_view, 9> kStages{
        "Reading the project",  "Clearing the last project", "Loading audio",
        "Building the scene",   "Loading the environment",   "Layers and parameters",
        "Shaders and control",  "Timeline and automation",   "Finishing"};
    loadTimings_.clear();
    loadTimings_.reserve(kStages.size());
    auto stageStarted = std::chrono::steady_clock::now();
    int stageIndex = -1;
    const auto stage = [&](int index) {
        if (stageIndex >= 0 && stageIndex < static_cast<int>(kStages.size())) {
            const auto now = std::chrono::steady_clock::now();
            loadTimings_.emplace_back(
                std::string(kStages[static_cast<std::size_t>(stageIndex)]),
                std::chrono::duration<double, std::milli>(now - stageStarted).count());
            stageStarted = now;
        }
        stageIndex = index;
        if (loadReporter_) {
            loadReporter_(LoadStage{.name = kStages[static_cast<std::size_t>(index)],
                                    .index = index,
                                    .count = static_cast<int>(kStages.size())});
        }
    };
    // Closes the final stage's timing and prints the breakdown. Called on every exit that reached
    // a stage, including the failures -- a load that failed slowly is the one you most want the
    // numbers for.
    const auto finishTimings = [&] {
        stage(static_cast<int>(kStages.size()) - 1);
        stageIndex = -1;
        double total = 0.0;
        std::string line;
        for (const auto& [name, ms] : loadTimings_) {
            total += ms;
            if (ms >= 1.0) {
                line += fmt::format("{} {:.0f} ms; ", name, ms);
            }
        }
        log::info("project load: {:.0f} ms total -- {}", total, line.empty() ? "all stages under 1 ms" : line);
    };

    stage(0);
    std::ifstream in(path);
    if (!in) {
        return fail("cannot open '{}'", path.string());
    }
    nlohmann::json doc = nlohmann::json::parse(in, nullptr, false);
    if (doc.is_discarded()) {
        return fail("'{}' is not valid JSON", path.string());
    }
    if (!doc.is_object() || doc.value("format", std::string()) != params::kProjectFormatName) {
        return fail("'{}' is not an avgen project", path.string());
    }
    // Migrate once here so the two parameter passes below see a current document.
    auto migrated = params::migrateProject(doc);
    if (!migrated) {
        return std::unexpected(migrated.error());
    }
    for (const auto& step : migrated->steps) {
        log::info("project '{}' migrated: {}", path.filename().string(), step);
    }
    stage(1);
    const auto dir = std::filesystem::absolute(path).parent_path();
    projectWarnings_.clear();
    projectSceneError_.clear();
    // ADR-902: binds in the middle of the load check routes against the bus and parameters alone;
    // the scene's facts are read once the scene is whole. Restored on every way out.
    livenessReady_ = false;
    struct LivenessReady {
        bool& ready;
        ~LivenessReady() { ready = true; }
    } livenessGuard{livenessReady_};
    // The last project's timeline goes now, not at stage 7 where this one's is read: every rebind on the way (the
    // audio, the stems, the scene) bound the old tracks against the new parameters, and a project switch logged and
    // recorded "timeline: unknown targets" naming the previous project's targets as this one's warning.
    timeline_.clear();
    // Back to factory before anything of this project's is applied.
    //
    // Opening a project is a *replacement*, not a merge: a parameter the document does not mention
    // is the default, not whatever the last project left behind. Sources, routes and presets were
    // always replaced; parameters were not, and the ones that leaked are the ones the engine owns
    // rather than the scene -- all of `post/*`, the camera's lens, exposure and focus, the input
    // gain -- because a scene swap is what clears the parameter set and those are re-registered
    // immediately afterwards.
    //
    // It has to happen *here*, before the scene loads, and not in the parameter pass further down:
    // a composition's own `post` block is authored state that lands between the two, and a reset
    // after it would erase it.
    //
    // Resetting the structs as well as the parameters matters for the same ordering reason:
    // `installController` re-registers post and camera parameters from them (`keep = post_`),
    // using the current values as the new *defaults*, so a stale struct here would come back as a
    // stale default that no later reset could tell from an authored one.
    ++projectGeneration_; // ADR-1390: the editor's view state goes with the project this replaces
    for (params::IParameter* param : params_.ordered()) {
        if (param != nullptr && param->flags().serialized) {
            param->resetToDefault();
        }
    }
    post_ = scene::PostSettings{};
    lens_ = scene::LensSettings{};
    exposure_ = scene::ExposureSettings{};
    focus_ = scene::FocusSettings{};
    // The temporal effects (frame echo, mosh, feedback) re-register from `temporal_` the same way and were missing
    // here: after Feedback Mirror had played, every scene switched to kept its feedback on -- the owner's "spinning
    // tunnel that carried over from one scene" (2026-10-14; tests/integration/test_live_switch_carryover.cpp).
    temporal_ = scene::TemporalSettings{};
    meterSettings_ = MeterSettings{}; // ADR-896: the loop above reset the parameters; this, what they re-register from
    auto warn = [&](std::string message) {
        log::warn("project: {}", message);
        projectWarnings_.push_back(std::move(message));
    };
    auto sceneFailed = [&](const std::string& message) {
        projectSceneError_ = message;
        warn("scene: " + message);
    };
    // Resolves an asset reference (string or object form); a missing file is searched for under
    // the project folder by name, size and content hash and relinked with a warning.
    auto resolveAsset = [&](const nlohmann::json& j, const char* label) -> std::optional<std::filesystem::path> {
        const auto ref = readAssetRef(j);
        if (!ref) {
            return std::nullopt;
        }
        auto path = resolveFrom(ref->path, dir);
        std::error_code ec;
        if (std::filesystem::exists(path, ec)) {
            return path;
        }
        if (auto found = findRelinkCandidate(*ref, path, dir)) {
            warn(fmt::format("relinked {}: {} -> {}", label, ref->path, relativeTo(*found, dir)));
            return *found;
        }
        return path; // still missing: the loader reports it
    };

    // ADR-1084: the live quality hint. Absent means the default (balanced), not the last project's.
    liveQualityStrategy_.reset();
    {
        // ADR-1100: absent fields are unset, never the last project's.
        LiveProjectSettings ls;
        if (const auto live = doc.find("live"); live != doc.end() && live->is_object()) {
            if (live->contains("targetFps")) {
                // ADR-1360: named rather than silently ignored, so a project written before it says why its number
                // no longer applies.
                warn("live.targetFps is no longer read: the Live panel's Target is the one live frame-rate target "
                     "for every scene (ADR-1360)");
            }
            if (const auto v = live->find("profile"); v != live->end()) {
                const auto p = v->is_string() ? qualityProfileFromToken(v->get<std::string>()) : std::nullopt;
                if (p) {
                    ls.profile = *p;
                } else {
                    warn("live.profile must be quality, balanced or performance");
                }
            }
            if (const auto v = live->find("minimumLevel"); v != live->end()) {
                const auto l = v->is_string() ? liveQualityLevelFromToken(v->get<std::string>()) : std::nullopt;
                if (l) {
                    ls.minimumLevel = *l;
                } else {
                    warn("live.minimumLevel must be ultra, high, medium, low or emergency");
                }
            }
            if (const auto v = live->find("priority"); v != live->end()) {
                std::vector<std::string> groups;
                if (v->is_array()) {
                    for (const auto& g : *v) {
                        groups.push_back(g.is_string() ? g.get<std::string>() : std::string("?"));
                    }
                }
                std::string error;
                if (v->is_array() && ladderFromPriority(groups, error)) {
                    ls.priority = std::move(groups);
                } else {
                    warn("live.priority: " + (error.empty() ? std::string("must be an array of group names") : error));
                }
            }
            if (const auto v = live->find("overrides"); v != live->end()) {
                std::string error;
                if (auto c = ceilingFromJsonText(v->dump(), error)) {
                    ls.overrides = *c;
                } else {
                    warn("live.overrides: " + error);
                }
            }
        }
        setLiveSettings(std::move(ls));
    }
    if (const auto live = doc.find("live"); live != doc.end()) {
        if (!live->is_object()) {
            warn("live: must be an object");
        } else if (const auto hint = live->find("qualityStrategy"); hint != live->end()) {
            const std::string token = hint->is_string() ? hint->get<std::string>() : std::string{};
            if (const auto strategy = liveQualityStrategyFromToken(token)) {
                liveQualityStrategy_ = *strategy;
            } else {
                warn(fmt::format("live.qualityStrategy '{}' is not one of resolution_first, balanced, effects_first; "
                                 "using balanced",
                                 token));
            }
        }
    }
    // ADR-1580/1581: the feeds, read before the sonic block, the audio and the stems so that each builds what they
    // need once. Absent means the mix feeds everything, not the last project's feeds.
    feeds_ = {};
    // Rebuilt as the sonic block, the audio and the stems arrive; said once, when they all have (below).
    feedsQuiet_ = true;
    struct LoudAgain {
        bool& quiet;
        ~LoudAgain() { quiet = false; }
    } loudAgain{feedsQuiet_};
    if (const auto block = doc.find("feeds"); block != doc.end()) {
        if (auto parsed = audio::Feeds::fromJson(*block)) {
            feeds_ = std::move(*parsed);
        } else {
            warn(parsed.error().message + "; the mix feeds everything");
        }
    }
    // ADR-1020: the Sonic Garden block, read before the audio so that loading the audio analyses its timbre
    // once. Absent means the subsystem is off, not inherited from the last project.
    {
        const auto block = doc.find("sonic");
        if (auto r = setSonic(block != doc.end() ? *block : nlohmann::json(), dir); !r) {
            warn("sonic: " + r.error().message);
        }
        // ADR-1025: a project made to be played turns live input on in the live editor; any other turns it off.
        if (mode_ == EngineMode::Live && controlHub_.liveIo()) {
            static_cast<void>(setLiveSonic(sonic_ != nullptr && sonic_->live));
        }
    }
    stage(2);
    // The audio is the project's too. A document that names none means silence, not whatever was
    // playing before: opening a project with no `assets.audio` used to leave the previous piece
    // loaded, under a scene it was never written for, with a transport whose duration came from it.
    //
    // A live *input* is a device choice rather than project state and is left alone -- it already
    // holds no file, so there is nothing here for this to clear.
    {
        const auto assets = doc.find("assets");
        const bool statesAudio = assets != doc.end() && assets->is_object() &&
                                 (assets->contains("audioClips") || assets->contains("audio"));
        if (!statesAudio && input_ == nullptr && (hasAudio() || !audioClips_.empty())) {
            if (auto r = setAudioClips({}); !r) {
                warn("audio: " + r.error().message);
            }
        }
    }

    // ---- assets first: they define the parameter surface the rest of the document targets ----
    if (const auto assets = doc.find("assets"); assets != doc.end() && assets->is_object()) {
        std::filesystem::path sceneEnvironment;
        if (assets->contains("audioClips") && (*assets)["audioClips"].is_array()) {
            // An arrangement wins over a single reference; they are never both written.
            std::vector<audio::AudioClip> clips;
            for (const auto& item : (*assets)["audioClips"]) {
                if (!item.is_object() || !item.contains("file")) {
                    continue;
                }
                audio::AudioClip clip;
                clip.file = resolveAsset(item["file"], "audio clip").value_or(std::filesystem::path());
                clip.startSeconds = item.value("start", 0.0);
                clip.inSeconds = item.value("in", 0.0);
                clip.durationSeconds = item.value("duration", 0.0);
                clip.gain = item.value("gain", 1.0f);
                clip.fadeInSeconds = item.value("fadeIn", 0.0);
                clip.fadeOutSeconds = item.value("fadeOut", 0.0);
                clip.enabled = item.value("enabled", true);
                clip.name = item.value("name", std::string{});
                clips.push_back(std::move(clip));
            }
            if (auto r = setAudioClips(std::move(clips)); !r) {
                warn("audio: " + r.error().message);
                // The arrangement this project names could not be built. Silence, not the last
                // project's piece standing in for it.
                static_cast<void>(setAudioClips({}));
            }
        } else if (assets->contains("audio")) {
            if (const auto audio = resolveAsset((*assets)["audio"], "audio"); audio && *audio != audioPath_) {
                if (auto r = loadAudio(*audio); !r) {
                    warn("audio: " + r.error().message);
                    static_cast<void>(setAudioClips({})); // as above: silence, not the last piece
                }
            }
        }
        // ADR-1520: the analysis stems, after the mix they are checked against. A document with none has none.
        {
            std::vector<audio::StemSource> stems;
            if (const auto list = assets->find("stems"); list != assets->end() && list->is_array()) {
                for (const auto& item : *list) {
                    if (!item.is_object() || !item.contains("file")) {
                        warn("assets.stems: an entry without a file was skipped");
                        continue;
                    }
                    audio::StemSource stem;
                    stem.file = resolveAsset(item["file"], "stem").value_or(std::filesystem::path());
                    stem.key = item.value("key", std::string{});
                    stem.label = item.value("label", std::string{});
                    stem.offsetSeconds = item.value("offset", 0.0);
                    // ADR-1630: what the stem is, explicit only (no role is `other`; nothing is inferred from the
                    // key or the audio). `hits` is gone: a response feed decides which stems run the causal onsets.
                    if (const auto role = item.find("role"); role != item.end()) {
                        const auto parsed = role->is_string() ? audio::sourceRoleFromName(role->get<std::string>())
                                                              : std::nullopt;
                        if (parsed) {
                            stem.role = *parsed;
                        } else {
                            warn(fmt::format("assets.stems: stem '{}': role {} is not drums, bass, synth, vocal or other; "
                                             "it is other",
                                             stem.key, role->dump()));
                        }
                    }
                    if (item.contains("hits")) {
                        warn(fmt::format("assets.stems: stem '{}': \"hits\" is gone (ADR-1630); a response feed "
                                         "(\"feeds\") decides which stems run the causal onsets",
                                         stem.key));
                    }
                    stem.gainDb = std::clamp(item.value("gainDb", 0.0f), analysis::kMinTrimDb, analysis::kMaxTrimDb);
                    stems.push_back(std::move(stem));
                }
            }
            if (!stems.empty() || !stems_.empty()) {
                setStems(std::move(stems));
            }
        }
        stage(3);
        if (assets->contains("scene") && (*assets)["scene"].is_object()) {
            const auto& sceneRef = (*assets)["scene"];
            const std::string kind = sceneRef.value("kind", std::string("orb"));
            if (kind == "orb") {
                if (orbScene() == nullptr) {
                    loadOrbScene();
                }
            } else if (kind == "gltf" && sceneRef.contains("path")) {
                const auto scenePath = resolveAsset(sceneRef["path"], "scene").value_or(std::filesystem::path());
                if (gltfScene() == nullptr || gltfScene()->path() != scenePath) {
                    if (auto r = loadScene(scenePath); !r) {
                        sceneFailed(r.error().message);
                    }
                }
            } else if (kind == "composition" && sceneRef.contains("path")) {
                const auto scenePath = resolveAsset(sceneRef["path"], "scene").value_or(std::filesystem::path());
                const auto previousEnvironment = environmentPath_;
                environmentPath_.clear();
                // ADR-330: the objects this session added to, and removed from, the scene it saves
                // by reference. Passed *into* the load rather than applied after it, so the
                // composition the engine ends up with is the one the parser would have built from a
                // scene file with those edits made -- and so it is in place before anything reads
                // the node list. That ordering is not taste: the `parameters` block further down
                // carries `nodes/<name>/...` values, a hero names a node, and the world effects name
                // a hero. Nodes are the most structural thing a project can say, so they go first.
                if (auto r = loadComposition(scenePath, doc.value("sceneNodes", nlohmann::json())); !r) {
                    environmentPath_ = previousEnvironment;
                    sceneFailed(r.error().message);
                } else {
                    sceneEnvironment = environmentPath_;
                }
            } else if (kind == "composition" && sceneRef.contains("inline")) {
                // No node edits here, deliberately: `assets.scene.inline` *is* `Composition::toJson`
                // and already carries the live node list. A difference beside it would be a second
                // answer -- the same reason `heroes` and the two effect lists are not written for an
                // inlined composition either.
                registry_.setBaseDirectory(dir);
                auto comp = scene::Composition::fromJson(sceneRef["inline"], registry_);
                if (!comp) {
                    sceneFailed(comp.error().message);
                } else {
                    const float masterGain = modulator_.masterGain;
                    detachSceneParameters();
                    params_.clear();
                    modulator_.clearRoutes();
                    modulator_.masterGain = masterGain;
                    (*comp)->attach(params_, modulator_);
                    if (!(*comp)->environmentMap().empty()) {
                        sceneEnvironment = registry_.resolve((*comp)->environmentMap());
                    }
                    environmentPath_ = sceneEnvironment;
                    compositionPath_.clear();
                    const nlohmann::json inlinePost = (*comp)->postJson();
                    installController(std::move(*comp));
                    if (auto r = scene::applyPostJson(inlinePost, postParams_); !r) {
                        warn("scene: " + r.error().message);
                    }
                }
            } else {
                sceneFailed("unknown kind '" + kind + "'");
            }
        }
        stage(4);
        std::optional<std::filesystem::path> env;
        if (assets->contains("environment")) {
            env = resolveAsset((*assets)["environment"], "environment");
        }
        if (env) {
            if (*env != environmentPath_) {
                if (auto r = loadEnvironment(*env); !r) {
                    warn("environment: " + r.error().message);
                }
            }
        } else if (sceneEnvironment.empty() && !environmentPath_.empty()) {
            environmentPath_.clear();
            controller_->scene().environment.environmentMap = scene::kInvalidTexture;
            if (auto* comp = composition()) {
                comp->setEnvironmentMap({});
            }
        }
    }
    // Shader layer paths are project-relative on disk (and relinked like other assets).
    if (doc.contains("shaders") && doc["shaders"].is_array()) {
        for (auto& entry : doc["shaders"]) {
            if (entry.is_object() && entry.contains("path") && entry["path"].is_string()) {
                if (const auto shaderPath = resolveAsset(entry, "shader")) {
                    entry["path"] = shaderPath->string();
                }
            }
        }
    }

    stage(5);
    // ---- the session's effects, over the ones its scene authors ----
    //
    // Here, and for the reason the 2D composition below is here: the parameter block a few lines
    // down carries `fx/<id>/...` values, and `setEffects` is what *registers* those paths. Applied after the parameters, an
    // effect would arrive with every number back at its default and the project's own values would
    // already have been refused as unknown -- which is exactly what the 94 warnings this fix was
    // found by say.
    //
    // Over rather than instead of: the scene file is still the state that runs first (ADR-264), and
    // an absent key changes nothing. A present one is the session's answer, including an empty array,
    // which is how a deleted effect stays deleted.
    // ADR-276: the session's heroes, over the ones its scene authors. **Before** the effects, and
    // not by taste: an effect may be attached to a hero or name one as its source, and `Composition::setHeroes`
    // is what decides whether that name is real. The scene file's own reader orders them the same
    // way and says so (`composition.cpp`, "read after the heroes").
    //
    // Over rather than instead of (ADR-264): the scene file is still the state that runs first, an
    // absent key changes nothing, and a present one is the session's answer -- including an empty
    // array, which is how an unstarred object stays unstarred.
    if (const auto entry = doc.find("heroes"); entry != doc.end() && entry->is_array()) {
        std::vector<world::HeroPoint> heroes;
        heroes.reserve(entry->size());
        bool readable = true;
        for (std::size_t i = 0; i < entry->size(); ++i) {
            auto one = world::HeroPoint::fromJson((*entry)[i]);
            if (!one) {
                // Named, and the whole block refused rather than the member skipped: a hero that
                // quietly failed to load looks exactly like a hero nobody declared, which is
                // ADR-067 and ADR-070, twice bitten.
                warn(fmt::format("heroes[{}]: {}", i, one.error().message));
                readable = false;
                break;
            }
            heroes.push_back(std::move(*one));
        }
        if (readable) {
            if (auto* comp = composition(); comp != nullptr) {
                if (auto ok = comp->setHeroes(std::move(heroes)); !ok) {
                    warn("heroes: " + ok.error().message);
                }
            }
        }
    }
    // The authored lights the session ended with (the fifth of ADR-207's family; see the save).
    //
    // **Before `params::loadProject` below, and that ordering is the whole of whether this works.**
    // `setAuthoredLights` is what registers `lights/<id>/...`, and `params::loadProject` drops any
    // path it cannot find with a warning rather than keeping it -- so a lights block applied after
    // the parameters would leave every light's intensity, colour and position in the document and
    // in no parameter. That is ADR-358's shader-layer defect exactly, where `shader/<layer>/<input>`
    // was written by every save and met with "references unknown parameter; ignored" by every load.
    //
    // Over rather than instead of (ADR-264): an absent key changes nothing and the scene still runs
    // first; a present one is the session's answer, including an empty array, which is how a
    // deleted light stays deleted.
    if (const auto entry = doc.find("lights"); entry != doc.end() && entry->is_array()) {
        auto lights = scene::authoredLightsFromJson(*entry, "project");
        if (!lights) {
            warn(lights.error().message);
        } else if (auto* comp = composition(); comp != nullptr) {
            if (auto ok = comp->setAuthoredLights(std::move(*lights)); !ok) {
                warn("lights: " + ok.error().message);
            }
        }
    }
    // The session's cameras and authored camera track (ADR-751, the sixth of ADR-207's family; see
    // the save). **Before `params::loadProject`**, for the lights' reason: `setCameraDirection` is
    // what registers `cameras/<slug>/...`, and a rig's position and aim arrive in `parameters`.
    // Song Mode's `Directed` shots are not in the key -- they are regenerated -- so any the scene
    // file carries are kept rather than replaced.
    if (const auto entry = doc.find("cameraDirection"); entry != doc.end() && entry->is_object()) {
        auto direction = scene::CameraDirection::fromJson(*entry);
        if (!direction) {
            warn("cameraDirection: " + direction.error().message);
        } else if (auto* comp = composition(); comp != nullptr) {
            for (const scene::CameraShot& shot : comp->cameraDirection().shots) {
                if (shot.origin == scene::CameraShot::Origin::Directed) {
                    direction->shots.push_back(shot);
                }
            }
            if (auto ok = comp->setCameraDirection(std::move(*direction)); !ok) {
                warn("cameraDirection: " + ok.error().message);
            }
        }
    }
    // ADR-929: the session's staging -- the scene's scenarios plus the set pieces a plan compiled --
    // over the scene file's. **Before `params::loadProject`**, for the lights' reason: `setStaging`
    // registers `staging/<scenario>/...`, and a set piece's tuned values arrive in `parameters`.
    if (const auto entry = doc.find("staging"); entry != doc.end() && entry->is_object()) {
        auto staging = stage::stagingFromJson(*entry);
        if (!staging) {
            warn("staging: " + staging.error().message);
        } else if (auto* comp = composition(); comp != nullptr) {
            if (auto ok = comp->setStaging(std::move(*staging)); !ok) {
                warn("staging: " + ok.error().message);
            }
        }
    }
    // ADR-702: the session's effects -- every owner's, one list -- over the ones its scene authors.
    // After the heroes, because an effect can be attached to one. The pre-ADR-702 keys are named
    // rather than read (ADR-441: every tracked project was converted in the repository).
    for (const char* legacy : {"worldEffects", "atmosphericEffects"}) {
        if (doc.contains(legacy)) {
            warn(fmt::format("'{}' is the pre-ADR-702 effect format and was ignored; effects are one "
                             "'effects' array now (tools/migrate_effects.py converts a project)",
                             legacy));
        }
    }
    if (const auto entry = doc.find("effects"); entry != doc.end() && entry->is_array()) {
        std::vector<world::EffectInstance> effects;
        effects.reserve(entry->size());
        bool readable = true;
        for (std::size_t i = 0; i < entry->size(); ++i) {
            auto one = world::EffectInstance::fromJson((*entry)[i]);
            if (!one) {
                // Named, and the whole block refused rather than the member skipped: an effect that
                // quietly failed to load looks exactly like one nobody declared.
                warn(fmt::format("effects[{}]: {}", i, one.error().message));
                readable = false;
                break;
            }
            effects.push_back(std::move(*one));
        }
        if (readable) {
            if (auto ok = setEffects(std::move(effects)); !ok) {
                warn("effects: " + ok.error().message);
            }
        }
    }
    // ---- the 2D composition (ADR-083) ----
    // Here, and not later: the parameter block below carries "layers/<id>/..." values, and the
    // timeline below carries tracks aimed at them. Both need the parameters to exist first, and a
    // project written before this feature existed simply has no "composition" key.
    removeLayerParameters();
    if (const auto composition = doc.find("composition"); composition != doc.end()) {
        if (auto r = layers_.fromJson(*composition); !r) {
            return r;
        }
    } else {
        layers_.clear();
    }
    layers_.attach(params_);
    syncResponseControls(); // ADR-1062: a scene load clears the parameters; the response's must exist for the file's values
    // ADR-1043: the palette, attached BEFORE the parameter block below so its values land on the first pass (ADR-1420:
    // with a selector there are seven palette parameters; attached after, every load warned that each was unknown
    // before the second pass quietly applied it), and before the timeline so its tracks find them.
    if (!palette_.empty()) {
        palette_.detach(params_);
    }
    palette_ = params::Palette{};
    paletteReported_ = 0;
    if (doc.contains("palette")) {
        auto palette = params::Palette::fromJson(doc["palette"]);
        if (!palette) {
            return std::unexpected(palette.error());
        }
        palette_ = std::move(*palette);
        palette_.attach(params_);
    }

    if (auto r = params::loadProject(doc, params_, modulator_, &sources_, &presets_); !r) {
        return r;
    }
    if (doc.contains("control")) {
        auto map = control::ControlMap::fromJson(doc["control"]);
        if (!map) {
            return std::unexpected(map.error());
        }
        for (const std::string& w : map->warnings) {
            warn(w); // ADR-1700: a user mapping set aside, an unknown key -- said, not silently dropped
        }
        controlHub_.setMap(std::move(*map));
        const std::string tempo = doc["control"].value("tempoSource", std::string("analysis"));
        if (const auto source = tempoSourceFromName(tempo)) {
            setTempoSource(*source);
        } else {
            return fail("control.tempoSource '{}' unknown (analysis|midi)", tempo);
        }
        // ADR-896: the meter moved to the `music/meter/*` parameters and these keys are not read.
        // Said, not silently dropped -- a phrase length that loads without a word and does nothing is
        // the no-op family -- and not aliased either (ADR-442): the tracked projects were stripped.
        for (const char* stale : {"phraseBars", "sectionPhrases"}) {
            if (doc["control"].contains(stale)) {
                warn(fmt::format("control.{} is no longer read; the meter's settings are the parameters "
                                 "music/meter/phraseBars and music/meter/sectionPhrases (ADR-896)",
                                 stale));
            }
        }
    } else {
        controlHub_.setMap(control::ControlMap{});
        setTempoSource(TempoSource::Analysis);
    }
    stage(6);
    outputs_ = doc.contains("outputs") && doc["outputs"].is_array() ? doc["outputs"] : nlohmann::json::array();
    ensureControlSource();
    controlSource().resetToRest(); // ADR-1612: a loaded project starts with its knobs at rest
    sources_.attach(bus_, params_);
    if (doc.contains("shaders")) {
        if (auto r = shaderLayers_.fromJson(doc["shaders"]); !r) {
            return r;
        }
        // ADR-358. A shader layer's ISF inputs ARE parameters -- `ShaderLayerSet::registerInputs`
        // adds one per input under "shader/<layer>/<input>" -- but they do not exist until the
        // line above has run, and `params::loadProject` ran twenty-five lines up. So every
        // `"shader/..."` a project wrote was met with "references unknown parameter; ignored" and
        // the layer drew its file defaults. Not a hypothetical: the Tree of Life's cosmos layer
        // has eleven inputs and no project in the repository has ever been able to set one, which
        // is why the defect went unseen -- nobody could author the thing that would have shown it.
        //
        // A second, narrow pass rather than moving the load: `loadProject` also installs sources,
        // routes and presets, and reordering the whole of it to serve one prefix is a change whose
        // blast radius is every project. This one touches exactly the paths that could not
        // previously be set, so no existing picture moves -- there is no project in the repository
        // with a `shader/` parameter in it, because until now writing one did nothing.
        if (const auto parameters = doc.find("parameters");
            parameters != doc.end() && parameters->is_object()) {
            std::size_t applied = 0;
            for (const auto& [path, value] : parameters->items()) {
                if (!path.starts_with("shader/")) {
                    continue;
                }
                params::IParameter* param = params_.find(path);
                if (param == nullptr) {
                    log::warn("project references unknown shader parameter '{}'; ignored", path);
                    continue;
                }
                if (auto r = params::parameterFromJson(*param, value); !r) {
                    log::warn("project parameter '{}': {}", path, r.error().message);
                    continue;
                }
                ++applied;
            }
            // Said out loud, because the pass twenty lines up has already warned that these paths
            // are unknown -- it ran before the layers existed -- and a reader watching the log
            // would otherwise have only the warning and no sign that they were set after all.
            if (applied > 0) {
                log::info("project: {} shader-layer parameter(s) applied after the layers loaded", applied);
            }
        }
    } else {
        shaderLayers_.clear();
    }
    stage(7);
    if (doc.contains("timeline")) {
        if (auto r = timeline_.fromJson(doc["timeline"]); !r) {
            return r;
        }
    } else {
        timeline_.clear();
    }
    // ADR-225. Reset when absent rather than inherited, like everything else on this path: a
    // project written before this block existed, or one nobody directed, opens with the defaults
    // and no message -- its absence is the ordinary state, not a fault.
    autoDirector_ = AutoDirectorSettings{};
    if (const auto director = doc.find("autoDirector"); director != doc.end()) {
        auto parsed = AutoDirectorSettings::fromJson(*director);
        if (!parsed) {
            return std::unexpected(parsed.error());
        }
        autoDirector_ = *parsed;
    }
    // ADR-249. Cleared when absent for the same reason the settings are reset: a project with no
    // plan must not inherit the last one's, or Song Mode would direct this piece to another one's
    // sections.
    // ADR-755. Cleared when absent, like the song plan: a project must not inherit another's
    // provenance. A plan this build cannot read is a warning and is kept verbatim, because dropping
    // it here would make the next save delete it -- the defect family this program exists to avoid.
    directingPlans_.clear();
    unreadableDirectingPlans_.clear();
    // ADR-931: cleared when absent, like the plans they are about.
    directingEvaluations_.clear();
    if (const auto evaluations = doc.find("directingEvaluations"); evaluations != doc.end() && evaluations->is_array()) {
        for (std::size_t i = 0; i < evaluations->size(); ++i) {
            if (auto e = directing::EvaluationReport::fromJson((*evaluations)[i])) {
                directingEvaluations_.push_back(std::move(*e));
            } else {
                warn(fmt::format("directingEvaluations[{}]: not an evaluation; dropped", i));
            }
        }
    }
    if (const auto plans = doc.find("directingPlans"); plans != doc.end() && plans->is_array()) {
        for (std::size_t i = 0; i < plans->size(); ++i) {
            directing::PlanParse parsed = directing::parsePlan((*plans)[i]);
            if (parsed.plan) {
                directingPlans_.push_back(std::move(*parsed.plan));
                continue;
            }
            unreadableDirectingPlans_.push_back((*plans)[i]);
            const auto firstError = std::find_if(parsed.issues.begin(), parsed.issues.end(), [](const directing::Issue& issue) {
                return issue.severity == directing::Severity::Error;
            });
            warn(fmt::format("directingPlans[{}]: kept but not read: {}", i,
                             firstError != parsed.issues.end() ? firstError->message : std::string("unreadable")));
        }
    }
    songPlan_ = SongPlan{};
    if (const auto plan = doc.find("songPlan"); plan != doc.end()) {
        auto parsed = songPlanFromJson(*plan);
        if (!parsed) {
            return std::unexpected(parsed.error());
        }
        songPlan_ = std::move(*parsed);
    }
    // ADR-158. Cleared when absent, like the timeline above: a project without a cut must not
    // inherit the last one's, or the camera would chase a hero this scene has never heard of.
    if (auto* comp = composition()) {
        std::vector<scene::AimFollow> shots;
        if (const auto follow = doc.find("cameraAimFollow"); follow != doc.end()) {
            shots = aimFollowFromJson(*follow, "cameraAimFollow", warn);
        }
        comp->setAimFollow(std::move(shots));
        // ADR-892. Cleared when absent, as the table is: a project without a continuous take must
        // not inherit the last one's ownership of the frame.
        const auto take = doc.find("cameraContinuousTake");
        comp->setContinuousTake(take != doc.end() && take->is_boolean() && take->get<bool>());
        // ADR-217, and for exactly the reason the table above round-trips: an offline render reloads
        // the project before drawing it, so a hold that only existed in memory would make a rendered
        // file differ from the window that asked for it. It is derived rather than stored -- the
        // settings are already in `autoDirector`, and storing it twice would let the two disagree.
        // A project with no cut gets no hold, because `setAimHold` on an empty scenario is off.
    }
    // ADR-207, and cleared when absent for the same reason the aim-follow table is: a project with
    // no cut must not inherit the last one's, or a world effect would fire against a schedule for a
    // film this scene has never been in.
    {
        std::vector<world::ShotSpan> spans;
        if (doc.contains("cameraShotSpans") && doc["cameraShotSpans"].is_array()) {
            for (const auto& entry : doc["cameraShotSpans"]) {
                if (!entry.is_object()) {
                    continue;
                }
                world::ShotSpan span;
                span.start = entry.value("start", 0.0);
                span.end = entry.value("end", 0.0);
                span.travel = entry.value("travel", false);
                span.spotlight = entry.value("spotlight", false);
                // ADR-922: a span's `emphasis` is no longer a field. Files written before carry the
                // key and it is not read -- it never was, by anything but this loader.
                span.subject = entry.value("subject", std::string{});
                const auto readVec = [&entry](const char* key, glm::vec3& out) {
                    if (entry.contains(key) && entry[key].is_array() && entry[key].size() == 3) {
                        out = glm::vec3(entry[key][0].get<float>(), entry[key][1].get<float>(),
                                        entry[key][2].get<float>());
                    }
                };
                readVec("subjectPosition", span.subjectPosition);
                span.subjectRadius = entry.value("subjectRadius", 1.0f);
                span.handoff = entry.value("handoff", std::string{});
                readVec("handoffPosition", span.handoffPosition);
                if (!(span.end > span.start)) {
                    warn("cameraShotSpans: a span with no duration was skipped");
                    continue;
                }
                spans.push_back(std::move(span));
            }
        }
        shotSpans_ = std::move(spans);
    }
    // ADR-582, and cleared when absent for the same reason: a project with nothing parked must not
    // inherit the last one's parked cut, or "Resume director" would restore another film's camera.
    // A project saved before this block existed has none, and opens exactly as it always did.
    parkedCut_ = ParkedDirectorsCut{};
    if (const auto parked = doc.find("parkedDirector"); parked != doc.end()) {
        auto cut = parkedCutFromJson(*parked, warn);
        if (!cut) {
            return std::unexpected(cut.error());
        }
        parkedCut_ = std::move(*cut);
    }
    cueState_ = {};
    cueApplied_ = false;
    states_ = StateMachine{};
    if (doc.contains("states")) {
        if (auto r = states_.fromJson(doc["states"]); !r) {
            return r;
        }
        if (auto r = states_.validate(presets_); !r) {
            log::warn("project states: {}", r.error().message);
        }
    }
    worldMacros_.clear();
    if (doc.contains("worldMacros")) {
        if (!doc["worldMacros"].is_array()) {
            return fail("'worldMacros' must be an array");
        }
        for (const auto& mj : doc["worldMacros"]) {
            auto m = WorldMacro::fromJson(mj);
            if (!m) {
                return std::unexpected(m.error());
            }
            worldMacros_.push_back(std::move(*m));
        }
    }
    if (doc.contains("director")) {
        auto director = WorldDirector::fromJson(doc["director"]);
        if (!director) {
            return std::unexpected(director.error());
        }
        director_ = std::move(*director);
    } else {
        director_ = WorldDirector{};
    }
    applyWorldMacros();
    states_.reset(params_, presets_);
    if (doc.contains("render")) {
        auto r = RenderSettings::fromJson(doc["render"]);
        if (!r) {
            return std::unexpected(r.error());
        }
        render_ = *r;
    } else {
        render_ = RenderSettings{};
    }
    // A project with no "pathtrace" block gets the defaults rather than inheriting the trace
    // settings of whatever was open before -- the same rule the transport's loop follows, and for
    // the same reason: 512 samples set for one piece must not silently govern the next.
    if (doc.contains("pathtrace")) {
        auto r = PathTraceSettings::fromJson(doc["pathtrace"]);
        if (!r) {
            return std::unexpected(r.error());
        }
        pathTrace_ = *r;
    } else {
        pathTrace_ = PathTraceSettings{};
    }
    // The transport's persistent half. A project with no "transport" block clears the loop rather
    // than inheriting the one from whatever was open before: loading a project must not leave a
    // range from another piece quietly governing this one.
    transport_.clearLoop();
    // Same rule as the loop: a project with no tempo block gets none, rather than inheriting the
    // override from whatever was open before. A tempo from another piece quietly governing this
    // one is exactly the defect the reset exists to prevent.
    tempoOverride_ = {};
    beatAnchor_.reset();
    if (doc.contains("transport") && doc["transport"].is_object()) {
        const auto& block = doc["transport"];
        if (block.contains("loop") && block["loop"].is_object()) {
            const auto& loop = block["loop"];
            TransportLoop parsed;
            parsed.enabled = loop.value("enabled", false);
            parsed.startSeconds = loop.value("start", 0.0);
            parsed.endSeconds = loop.value("end", 0.0);
            transport_.setLoop(parsed);
        }
        if (block.contains("tempo") && block["tempo"].is_object()) {
            const auto& tempo = block["tempo"];
            const double bpm = tempo.value("bpm", 0.0);
            // Provenance round-trips with the value. A file that somehow records a non-user
            // provenance here is not honoured: only a decision is persisted, so anything else
            // would be a measurement masquerading as one.
            const auto source = audio::tempoProvenanceFromToken(tempo.value("source", "user"));
            if (source == audio::TempoProvenance::UserOverride && bpm >= audio::kMinPlausibleBpm &&
                bpm <= audio::kMaxPlausibleBpm) {
                tempoOverride_ = audio::AudioTempo{.available = true,
                                                   .bpm = bpm,
                                                   .source = audio::TempoProvenance::UserOverride,
                                                   .confidence = 1.0};
                // ADR-1783: the marked beat, which makes the override the file's beat clock.
                if (const auto a = tempo.find("anchor"); a != tempo.end() && a->is_number()) {
                    if (const double anchor = a->get<double>(); std::isfinite(anchor) && anchor >= 0.0) {
                        beatAnchor_ = anchor;
                    } else {
                        log::warn("project beat anchor {:g} ignored: not a time in the project", anchor);
                    }
                }
            } else if (bpm != 0.0) {
                log::warn("project tempo {:g} ({}) ignored", bpm, tempo.value("source", "user"));
            }
        }
    }
    // Parameter values for sources and shader inputs arrive in the same document; apply them
    // again now that those parameters exist (unknown-at-first-pass paths were skipped).
    if (auto r = params::loadProject(doc, params_, modulator_, nullptr, nullptr); !r) {
        return r;
    }
    // And re-anchor the entity layer, because the document that just landed can *move nodes*.
    //
    // A project serialises every node's position, rotation and scale -- Glowmere Valley 2's carries
    // 5,489 parameters -- and those values are applied *over* whatever the scene registered. The
    // entity layer was bound before that happened, so every entity driving a node the project moved
    // went on believing the node was where the scene file put it while the renderer drew it where
    // the project said. Nothing reported the disagreement and nothing closed it: measured in
    // Glowmere, the saucer's entity and the saucer's node were **28.661 m apart for the whole run**,
    // which is why an animal lifted correctly onto the beam's drawn axis was dragged twenty-eight
    // metres sideways on the way up (ADR-264).
    //
    // `installEntities` is idempotent and re-derives each anchor from the node's world transform,
    // now read from the parameter *bases* -- so this is the whole of the fix and it is general: it
    // is about projects and nodes, and knows nothing about saucers.
    if (scene::Composition* comp = composition(); comp != nullptr) {
        comp->installEntities();
    }
    livenessReady_ = true; // the scene is whole: this bind reads its facts
    rebind();
    modulator_.resetState();
    // The sequence goes on last, after every parameter a bake could possibly name exists: the
    // scene's nodes, the camera, the post chain, the sources and the shader inputs. Installing it
    // earlier would bind its tracks to a parameter set that was still being built, which is how a
    // feature ends up correct in every respect except that it does nothing (ADR-075).
    sequence_ = seq::Sequence{};
    sequenceTargets_.clear();
    sequenceReport_ = seq::InstallReport{};
    if (doc.contains("sequence")) {
        auto parsed = seq::Sequence::fromJson(doc["sequence"]);
        if (!parsed) {
            return fail("project sequence: {}", parsed.error().message);
        }
        sequence_ = std::move(*parsed);
        if (auto r = installSequence(); !r) {
            // A sequence that will not install is a load warning, not a load failure: the rest of
            // the project is perfectly good and the author needs to see the piece to fix it.
            log::warn("project sequence: {}", r.error().message);
            noteBindingProblem(fmt::format("sequence: {}", r.error().message));
        }
    }
    // ADR-902: every route, track and effect, now that everything a rule reads exists -- the shot
    // spans, the sequence's markers, the scene's programs and nodes.
    feedsQuiet_ = false;
    rebuildFeeds(); // ADR-1580/1581: every stem is known now (a key that names none is said), and no grid or feed of
                    // the last project's survives into this one
    if (input_ && channels_) {
        std::vector<std::string> keys;
        for (const signals::StemSignals& s : inputSignals_) {
            keys.push_back(s.key);
        }
        channels_->stop();
        rebuildChannelAnalysis(keys); // the live sources' roles under this project's feeds
    }
    reportRouteLiveness();
    finishTimings();
    projectPath_ = path;
    // A loaded project is a different piece. The transport stops and parks at its start rather than
    // carrying the previous project's playhead into it -- opening a project while another is playing
    // used to leave the new one running from wherever the old one had got to.
    refreshTransport();
    transport_.stop();
    seekSeconds(transport_.positionSeconds());
    reportCuePresetOverrides();
    // The baseline for "has anything changed since this was opened" (ADR-440). Taken here, at the
    // end of the load and from the same builder a save uses, rather than from `doc` -- the document
    // that was read is not the document this engine would write, by between 279 and 19,798 JSON
    // paths depending on the project, and a baseline taken from it would report every project dirty
    // the instant it opened. The comment on `sampleProjectDirty` has the measurement.
    markProjectSaved();
    log::info("project '{}' loaded: {} parameters, {} routes, {} sources, {} presets, {} timeline tracks, {} cues, {} warning(s)",
              path.filename().string(), params_.size(), modulator_.routes().size(), sources_.sources().size(),
              presets_.presets().size(), timeline_.tracks().size(), timeline_.cues().size(), projectWarnings_.size());
    return {};
} catch (const nlohmann::json::exception& e) {
    // A key of the wrong type (`"format": 1`) makes `.value()`/`.get<T>()` throw; nothing above
    // this caught it, so a damaged file terminated the application instead of failing to open
    // (tests/integration/test_project_malformed.cpp).
    return fail("'{}' is not a valid project: {}", path.string(), e.what());
}

void Engine::newProject() {
    ++projectGeneration_; // ADR-1390
    directingPlans_.clear();
    unreadableDirectingPlans_.clear();
    directingEvaluations_.clear();
    sequence_ = seq::Sequence{};
    sequenceTargets_.clear();
    sequenceReport_ = seq::InstallReport{};
    removeLayerParameters();
    layers_.clear();
    timeline_.clear();
    cueState_ = {};
    cueApplied_ = false;
    presets_.clear();
    sources_.clear();
    shaderLayers_.clear();
    environmentPath_.clear();
    if (!palette_.empty()) {
        palette_.detach(params_); // ADR-1043
    }
    palette_ = params::Palette{};
    loadOrbScene(); // clears the parameter set and routes, re-registers sources/post/shaders
    // Every parameter the engine owns, not only post. `camera/lens`, `camera/exposure` and
    // `camera/focus` sit in the same never-cleared set and used to survive File > New, which made
    // "new project" mean something different from "open project".
    for (auto* p : params_.ordered()) {
        const std::string_view path = p->path();
        if (path.starts_with("post/") || path.starts_with("camera/lens/") ||
            path.starts_with("camera/exposure/") || path.starts_with("camera/focus/") ||
            path == "audio/inputGain") {
            p->resetToDefault();
        }
    }
    post_ = scene::PostSettings{};
    lens_ = scene::LensSettings{};
    exposure_ = scene::ExposureSettings{};
    focus_ = scene::FocusSettings{};
    render_ = RenderSettings{};
    controlHub_.setMap(control::ControlMap{});
    outputs_ = nlohmann::json::array();
    setTempoSource(TempoSource::Analysis);
    // The meter's settings belong to a piece (ADR-896); a new one starts detected.
    setMeterSettings(MeterSettings{});
    ensureControlSource();
    sources_.attach(bus_, params_);
    modulator_.masterGain = 1.0f;
    // File > New is a project with no audio, like opening one that names none: the clip list was
    // already cleared here but the *installed* buffer was not, so the last piece kept playing.
    // A live input is a device choice and is left running (see `loadProject`).
    audioClips_.clear();
    clipSources_.clear();
    audioMix_ = audio::MixReport{};
    if (input_ == nullptr) {
        static_cast<void>(installAudio(nullptr));
    }
    projectPath_.clear();
    projectWarnings_.clear();
    projectSceneError_.clear();
    transport_.clearLoop();
    refreshTransport();
    transport_.stop();
    // A new project is clean: there is nothing in it yet to lose (ADR-440). Last, so the baseline
    // photographs the engine this function has finished resetting rather than the one it started
    // with -- taken at the top it would make File > New produce an immediately-dirty project.
    markProjectSaved();
}

// ---- referenced files and bundle export -------------------------------------------------------

std::vector<std::filesystem::path> Engine::referencedFiles() const {
    std::vector<std::filesystem::path> files;
    auto add = [&](const std::filesystem::path& p) {
        if (p.empty()) {
            return;
        }
        const auto abs = std::filesystem::absolute(p).lexically_normal();
        if (std::find(files.begin(), files.end(), abs) == files.end()) {
            files.push_back(abs);
        }
    };
    add(audioPath_);
    for (const audio::AudioClip& clip : audioClips_) {
        add(clip.file); // every file the arrangement plays, not only the one `audioPath_` names
    }
    add(environmentPath_);
    if (const auto* gltf = dynamic_cast<const scene::GltfScene*>(controller_.get())) {
        add(gltf->path());
    }
    if (!compositionPath_.empty()) {
        add(compositionPath_);
        visitSceneFileAssets(std::filesystem::absolute(compositionPath_), [&](const auto& p, bool) { add(p); });
    } else if (const auto* comp = dynamic_cast<const scene::Composition*>(controller_.get())) {
        for (const auto& node : comp->nodes()) {
            if (!node->asset.empty()) {
                const auto asset = registry_.resolve(node->asset);
                add(asset);
                if (node->kind == scene::NodeKind::Scene) {
                    visitSceneFileAssets(asset, [&](const auto& p, bool) { add(p); });
                }
            }
        }
    }
    for (const auto& layer : shaderLayers_.layers()) {
        add(layer->path);
    }
    return files;
}

Result<void> Engine::exportBundle(const std::filesystem::path& dir) {
    return bundleInto(dir / "project.json", dir / "assets");
}

// The one copier. `exportBundle` writes a folder with a fixed layout; Save As writes a project file
// the person named, with its copies beside it. They differ only in where the two outputs land, and
// a second implementation of "copy every asset and rewrite every reference" is how one of them
// quietly stops covering a reference the other handles -- which is exactly the defect this is being
// written to fix, one level up.
Result<void> Engine::bundleInto(const std::filesystem::path& projectFile,
                                const std::filesystem::path& assetsDir) {
    std::error_code ec;
    const auto dir = projectFile.parent_path();
    std::filesystem::create_directories(dir, ec);
    std::filesystem::create_directories(assetsDir, ec);
    if (ec) {
        return fail("cannot create '{}': {}", assetsDir.string(), ec.message());
    }
    // Unique destination names: keep the file name, suffix on collision between different sources.
    std::map<std::filesystem::path, std::filesystem::path> placed; // source -> destination
    std::set<std::string> usedNames;
    auto place = [&](const std::filesystem::path& source) -> std::filesystem::path {
        if (const auto it = placed.find(source); it != placed.end()) {
            return it->second;
        }
        std::string name = source.filename().string();
        const std::string stem = source.stem().string();
        const std::string ext = source.extension().string();
        for (int i = 2; usedNames.count(name) != 0; ++i) {
            name = stem + "_" + std::to_string(i) + ext;
        }
        usedNames.insert(name);
        const auto dest = assetsDir / name;
        placed[source] = dest;
        return dest;
    };
    auto copyFile = [&](const std::filesystem::path& source) -> Result<std::filesystem::path> {
        const auto dest = place(source);
        if (!std::filesystem::exists(source)) {
            return fail("missing file '{}'", source.string());
        }
        std::filesystem::copy_file(source, dest, std::filesystem::copy_options::overwrite_existing, ec);
        if (ec) {
            return fail("cannot copy '{}' to '{}': {}", source.string(), dest.string(), ec.message());
        }
        std::string ext = source.extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (ext == ".gltf") {
            // External buffers and images live next to the file; bring the plausible ones along.
            for (const auto& entry : std::filesystem::directory_iterator(source.parent_path(), ec)) {
                if (entry.is_regular_file() && isGltfSidecar(entry.path())) {
                    std::filesystem::copy_file(entry.path(), assetsDir / entry.path().filename(),
                                               std::filesystem::copy_options::overwrite_existing, ec);
                }
            }
        }
        return dest;
    };
    // Scene files are rewritten so their references point into the bundle.
    std::function<Result<std::filesystem::path>(const std::filesystem::path&, int)> bundleScene;
    bundleScene = [&](const std::filesystem::path& sceneFile, int depth) -> Result<std::filesystem::path> {
        if (depth > scene::Composition::kMaxNestingDepth) {
            return fail("scene files nest too deeply at '{}'", sceneFile.string());
        }
        if (const auto it = placed.find(sceneFile); it != placed.end()) {
            return it->second;
        }
        std::ifstream in(sceneFile);
        nlohmann::json doc = nlohmann::json::parse(in, nullptr, false);
        if (!doc.is_object()) {
            return fail("'{}' is not a valid scene file", sceneFile.string());
        }
        const auto dest = place(sceneFile);
        const auto srcDir = sceneFile.parent_path();
        if (doc.contains("environment") && doc["environment"].is_object() && doc["environment"].contains("map") &&
            doc["environment"]["map"].is_string()) {
            auto copied = copyFile(resolveFrom(doc["environment"]["map"].get<std::string>(), srcDir));
            if (!copied) {
                return std::unexpected(copied.error());
            }
            doc["environment"]["map"] = copied->filename().generic_string();
        }
        // The light rig is an asset path in the environment block, and it was missed here exactly
        // as it was missed in `saveComposition`: two copiers, the same omission, found twice. A
        // scene bundled without it opens in the new location with no lighting.
        // The parser also accepts the rig at the top level (Composition::fromJson), and the
        // Glowmere scenes put it there: a bundle that rewrote only the environment's copy left
        // `"lightRig": "../lightrigs/..."` pointing outside the bundle (QA pass, 2026-09-28).
        for (nlohmann::json* holder : {&doc, doc.contains("environment") ? &doc["environment"] : nullptr}) {
            if (holder == nullptr || !holder->is_object() || !holder->contains("lightRig") ||
                !(*holder)["lightRig"].is_string() || (*holder)["lightRig"].get<std::string>().empty()) {
                continue;
            }
            auto copied = copyFile(resolveFrom((*holder)["lightRig"].get<std::string>(), srcDir));
            if (!copied) {
                return std::unexpected(copied.error());
            }
            (*holder)["lightRig"] = copied->filename().generic_string();
        }
        // Entity profiles. `visitor` in the Tree of Life scenes names one, and a bundle without it
        // reports "cannot open entity profile" on open -- which is what caught this.
        if (doc.contains("entities") && doc["entities"].is_array()) {
            for (auto& entity : doc["entities"]) {
                if (!entity.is_object() || !entity.contains("profile") || !entity["profile"].is_string()) {
                    continue;
                }
                auto copied = copyFile(resolveFrom(entity["profile"].get<std::string>(), srcDir));
                if (!copied) {
                    return std::unexpected(copied.error());
                }
                entity["profile"] = copied->filename().generic_string();
            }
        }
        if (doc.contains("materialPrograms") && doc["materialPrograms"].is_array()) {
            for (auto& entry : doc["materialPrograms"]) {
                if (!entry.is_string()) {
                    continue;
                }
                auto copied = copyFile(resolveFrom(entry.get<std::string>(), srcDir));
                if (!copied) {
                    return std::unexpected(copied.error());
                }
                entry = copied->filename().generic_string();
            }
        }
        // EVERY `asset` in the document, wherever it sits. Enumerating the places a mesh can be
        // named was wrong three times running: `node.asset` was handled, `procedural.source.asset`
        // was not (the saucer), and `scatter[].asset` was not either -- eighteen references, every
        // tree, rock and plant in the Tree of Life. Each gap was found only by fixing the one above
        // it, which is the signature of a list that will go stale again the next time somebody adds
        // a place to name a mesh. So this walks the document instead: any string under a key called
        // `asset` is an asset, and a new home for one is covered the day it is invented.
        std::function<Result<void>(nlohmann::json&)> copyEveryAsset;
        copyEveryAsset = [&](nlohmann::json& value) -> Result<void> {
            if (value.is_array()) {
                for (auto& item : value) {
                    if (auto r = copyEveryAsset(item); !r) {
                        return r;
                    }
                }
                return {};
            }
            if (!value.is_object()) {
                return {};
            }
            for (auto& [key, child] : value.items()) {
                if (key == "asset" && child.is_string()) {
                    const auto source = resolveFrom(child.get<std::string>(), srcDir);
                    // A nested scene is bundled as a scene, not copied as an opaque file.
                    std::string ext = source.extension().string();
                    std::transform(ext.begin(), ext.end(), ext.begin(),
                                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                    Result<std::filesystem::path> copied =
                        ext == ".json" ? bundleScene(source, depth + 1) : copyFile(source);
                    if (!copied) {
                        return std::unexpected(copied.error());
                    }
                    child = copied->filename().generic_string();
                    continue;
                }
                if (auto r = copyEveryAsset(child); !r) {
                    return r;
                }
            }
            return {};
        };
        if (doc.contains("nodes")) {
            if (auto r = copyEveryAsset(doc["nodes"]); !r) {
                return std::unexpected(r.error());
            }
        }
        std::ofstream out(dest);
        if (!out) {
            return fail("cannot write '{}'", dest.string());
        }
        out << doc.dump(2) << '\n';
        return dest;
    };

    // Save the project first (captures the current state), then rewrite its references.
    if (auto r = saveProject(projectFile); !r) {
        return r;
    }
    std::ifstream in(projectFile);
    nlohmann::json doc = nlohmann::json::parse(in, nullptr, false);
    in.close();
    auto& assets = doc["assets"];
    if (assets.contains("audio")) {
        auto copied = copyFile(resolveFrom(assetRefPath(assets["audio"]), dir));
        if (!copied) {
            return std::unexpected(copied.error());
        }
        setAssetRefPath(assets["audio"], relativeTo(*copied, dir));
    }
    if (assets.contains("environment")) {
        auto copied = copyFile(resolveFrom(assetRefPath(assets["environment"]), dir));
        if (!copied) {
            return std::unexpected(copied.error());
        }
        setAssetRefPath(assets["environment"], relativeTo(*copied, dir));
    }
    if (assets.contains("scene") && assets["scene"].contains("path")) {
        const auto scenePath = resolveFrom(assetRefPath(assets["scene"]["path"]), dir);
        auto copied = assets["scene"]["kind"] == "composition" ? bundleScene(scenePath, 0) : copyFile(scenePath);
        if (!copied) {
            return std::unexpected(copied.error());
        }
        // A rewritten scene file has new content: refresh its identity.
        assets["scene"]["path"] = assetRefJson(*copied, dir);
    } else if (assets.contains("scene") && assets["scene"].contains("inline")) {
        // Inline compositions: write them out as a scene file in the bundle so nodes' assets can
        // be rewritten like any other scene file.
        const auto tmp = assetsDir / "composition.json";
        {
            std::ofstream out(tmp);
            // Node assets are relative to the composition's registry base; make them absolute.
            nlohmann::json inlineScene = assets["scene"]["inline"];
            for (auto& node : inlineScene["nodes"]) {
                if (node.contains("asset") && node["asset"].is_string()) {
                    node["asset"] = registry_.resolve(node["asset"].get<std::string>()).string();
                }
            }
            if (inlineScene.contains("environment") && inlineScene["environment"].contains("map")) {
                inlineScene["environment"]["map"] =
                    registry_.resolve(inlineScene["environment"]["map"].get<std::string>()).string();
            }
            out << inlineScene.dump(2) << '\n';
        }
        placed.erase(tmp);
        usedNames.erase("composition.json");
        auto bundled = bundleScene(tmp, 0);
        if (!bundled) {
            return std::unexpected(bundled.error());
        }
        assets["scene"] = {{"kind", "composition"}, {"path", relativeTo(*bundled, dir)}};
    }
    if (doc.contains("shaders")) {
        for (auto& entry : doc["shaders"]) {
            if (entry.is_object() && entry.contains("path") && entry["path"].is_string()) {
                auto copied = copyFile(resolveFrom(entry["path"].get<std::string>(), dir));
                if (!copied) {
                    return std::unexpected(copied.error());
                }
                entry["path"] = relativeTo(*copied, dir);
            }
        }
    }
    std::ofstream out(projectFile);
    if (!out) {
        return fail("cannot write '{}'", projectFile.string());
    }
    out << doc.dump(2) << '\n';
    log::info("bundle written to '{}': {} file(s) beside '{}'", assetsDir.string(), placed.size(),
              projectFile.filename().string());
    return {};
}

// Save As. The project and its own copies of everything it names, so opening it later cannot reach
// back into the folder it was saved from -- and, more to the point, so EDITING it later cannot write
// back over a scene another project is still using. That was the reported defect: a project saved to
// the Desktop kept pointing at `examples/world/...scene.json`, and saving the scene through it
// overwrote the shared original.
Result<void> Engine::saveProjectAsCopy(const std::filesystem::path& path) {
    auto file = path;
    if (file.extension().empty()) {
        file.replace_extension(".json");
    }
    // Per project rather than a shared `assets/`: two projects saved into one folder would otherwise
    // copy over each other's files by name, which is the same defect this exists to remove.
    const auto assetsDir = file.parent_path() / (file.stem().string() + "_assets");
    if (auto r = bundleInto(file, assetsDir); !r) {
        return r;
    }
    // The session now belongs to the copy. Without this the editor would still be holding the paths
    // it was opened with, and the next scene save would land on the original -- the copy would be a
    // snapshot rather than a move, and the reported defect would survive its own fix.
    return loadProject(file);
}

} // namespace avgen::app
