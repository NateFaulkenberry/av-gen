#pragma once

// ADR-1051: the room / spatial composition validator.
//
// "Is this spatial arrangement actually valid?" -- answered from the semantic scene, not from pixels, and
// before anything is rendered. It reads a composition scene file (`avgen-scene` JSON) and checks the
// relationships between the things in it: what intersects what, what floats, what faces what, whether a
// door can be walked through, whether a lyric sits on a clear stretch of wall, whether the mannequin is in
// its chair, whether the camera's path stays inside the rooms, whether an entity is missing a part.
//
// Semantics are DATA, in two places:
//   * the scene: any SDF tree node (or composition node) may carry an `entity` annotation,
//       "entity": {"id": "Chair_03", "category": "chair", "room": "kitchen", "anchor": "Table_01",
//                  "pose": "sit", "interior": [[x0,x1],[y0,y1],[z0,z1]], "hip": [x,y,z], "normal": [x,y,z],
//                  "t0": s, "t1": s}
//     and any SDF node inside an entity may name a component with `"part": "head"`. The engine ignores
//     both keys, so annotated scenes render unchanged. The entity's frame is the frame its node sits in:
//     its geometry is the annotated subtree (plus the cuts, when it is a difference's first child); a prop's
//     front is its local +Z and its up is +Y (the kit's convention: base on y = 0, front facing +Z).
//   * the rules: a category table (group, rests/mounts/embeds, faces, mayIntersect, keepClear, requires,
//     seat heights, expectations) and a pose table, with tolerances. Built-in defaults
//     (`defaultSpaceRules()`); a rules file is deep-merged over them.
//
// What coexists is decided by the journey's chapters (ADR-1042): entities are checked against each other
// only when their objects are shown together. Mesh text (ADR-1046) is measured from its real glyph mesh.
//
// Pure CPU, deterministic, no GPU and no engine: SDF distances come from the same CPU evaluator the GPU
// parity tests use (`spatial::SdfTree::evaluate`).

#include "core/error.hpp"

#include <nlohmann/json.hpp>

#include <map>
#include <string>
#include <vector>

namespace avgen::scene {

struct SpaceValidateOptions {
    std::string title = "ROOM VALIDATION";
    std::string source;          // the file the scene came from (reported only)
    double lyricMargin = -1.0;   // < 0: the rules' `lyric.margin`
    double eyeHeight = -1.0;     // < 0: the rules' `camera.eyeHeight`
    bool cameraPath = true;      // sample every chapter's path against the geometry
    bool text = true;            // build glyph meshes for lyric bounds (needs the font backend)
    // The global journey distances the film visits (the project's camera/journey/distance keys). When set, each
    // chapter's path is checked over the span the keys visit inside it, and unvisited chapters are skipped;
    // otherwise the whole authored path (first to last control point) is checked.
    std::vector<double> journeyDistances;
    // The same keys with their times, (seconds, metres), sorted by time. When set, each camera-path sample
    // is given the time the film reaches it, and is checked only against what is present then (entities with
    // `t0`/`t1` spans; an object whose entities all have spans is present only inside them).
    std::vector<std::pair<double, double>> journeyKeys;
    // ADR-1056: when an object is shown, from the project's `nodes/<n>/visible` (or `sdf/<n>/visible`) step tracks:
    // object name -> [from, to) spans. An object with no entry is shown throughout.
    std::map<std::string, std::vector<std::pair<double, double>>> visibleSpans;
};

// The built-in rules: categories, poses, tolerances.
[[nodiscard]] nlohmann::json defaultSpaceRules();
// `overrides` deep-merged over the defaults (objects merge key by key; anything else replaces).
[[nodiscard]] nlohmann::json mergeSpaceRules(const nlohmann::json& overrides);

// Validates a composition scene document. The report:
//   {"title", "source", "summary": {"errors", "warnings", "infos", "pass": {...}},
//    "entities": [...], "groups": [...], "violations": [{severity, rule, entities, group, relationship,
//    measured, message, expected, suggestion, fix}]}
// Fails only when the document is not a scene (no `nodes` array) or the rules are malformed.
[[nodiscard]] Result<nlohmann::json> validateSpace(const nlohmann::json& scene, const nlohmann::json& rules,
                                                   const SpaceValidateOptions& options = {});

// ADR-1057: merges a film pass (`app::validateFilm`) into a static report: its violations join the list (re-sorted
// by severity, the summary recounted) and its camera/motion statistics go under "film".
void mergeFilmReport(nlohmann::json& report, const nlohmann::json& film);

// ADR-1056: the owner's "Scene Validation Report": Markdown, Critical / Warnings / Informational, counts by rule.
[[nodiscard]] std::string formatSceneValidationMarkdown(const nlohmann::json& report);

// The human-readable report (the addendum's section 14 form).
[[nodiscard]] std::string formatSpaceReport(const nlohmann::json& report);

} // namespace avgen::scene
