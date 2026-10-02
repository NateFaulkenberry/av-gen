#pragma once

// ADR-1057: the film pass of the spatial validator (`avgen --validate-space <project> --film`).
//
// ADR-1051 validates the scene as authored, at rest. What the owner sees is the scene as PLAYED: the camera
// where the journey, the look-at, the breath and the guard actually put it, and every SDF node where its
// keys and routes actually put it. This plays the project offline (no window, no GPU) through the same
// Engine the renderer uses, as `--trace-jumps` does (ADR-1053), and after each frame checks:
//
//   camera   inside solid geometry (the live SDF's sign at the eye, clipped to each object's march bounds);
//            near-plane clipping (a frustum ray hits geometry nearer than the near plane);
//            close geometry that fills the frame (the wide-angle distortion of a wall a hand's width away);
//            the path crossing a wall BETWEEN two frames (a segment test, so a 1-frame pass is caught);
//            entering a room before it has opened (rooms annotated `"opensAt": s`);
//   motion   build-lock: a structural transform (an architectural or furniture entity's SDF node, an
//            object's transform) that keeps changing after its construction keys end -- continuous
//            modulation on a structural transform (the owner's PART 15, "everything is slightly twitching").
//
// Violations use the static report's schema and severities and are merged into it by the CLI.

#include "core/error.hpp"

#include <nlohmann/json.hpp>

#include <string>

namespace avgen::app {

struct FilmValidateOptions {
    double fps = 30.0;  // sampling rate; 0 = the project's render rate
    double from = 0.0;  // seconds
    double to = -1.0;   // < 0: the whole project
    bool camera = true;
    bool motion = true;
    std::string cameraTrace; // a CSV of every sample: time, frame, eye, target, fov, near, clearance, nearest object
    std::string motionTrace; // "<path substring>=<file.csv>": every sample of the matching watched transforms
};

// `project`: the project file. `scene`: its scene document (for entity annotations). `staticReport`: the
// ADR-1051 report of the same scene (entities and their boxes, to name what the camera is inside).
// `rules`: the merged space rules (their "film" and "motion" objects). Returns
//   {"violations": [...], "camera": {"samples", "minClearance", "minClearanceAt"}, "motion": {"watched", ...}}
[[nodiscard]] Result<nlohmann::json> validateFilm(const std::string& project, const nlohmann::json& scene,
                                                  const nlohmann::json& staticReport, const nlohmann::json& rules,
                                                  const FilmValidateOptions& options = {});

} // namespace avgen::app
