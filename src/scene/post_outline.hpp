#pragma once

// ADR-1072: the screen-space outline (post/outline/*). One HDR pass after the exposure and before the
// defocus, so a line defocuses, smears and blooms with the surface it draws round. It reads the scene's
// depth, its normal target and its identifier target (ADR-035) and draws a line wherever one of three
// discontinuities is:
//
//   depth      the second difference of 1 / view depth across the pixel (zero on any plane, so a floor
//              seen at a grazing angle draws nothing), relative to the pixel's own: `depthThreshold`
//   normal     1 - cos of the angle between the pixel's normal and a neighbour's: `normalThreshold`
//   object     a neighbour belongs to a different object, or is the background
//
// `silhouette` 1 keeps only the depth and object edges (the outer outline, and where one object passes
// in front of another), dropping the creases inside a surface. The line is `width` pixels at 1080 lines
// (it scales with the output, so a 4K render matches its 1080p preview), its colour is `color` x
// `intensity` in exposed scene-linear light (above 1 blooms), and it fades out between `fadeStart` and
// `fadeEnd` metres from the camera (fadeEnd <= fadeStart: no fade).
//
// Off while `amount` is 0 (the default): the pass is not encoded and the frame is byte-identical. A pure
// function of the frame: seek equals play.

#include "params/parameter.hpp"

#include <glm/glm.hpp>
#include <nlohmann/json_fwd.hpp>

#include <string>
#include <vector>

namespace avgen::params {
class ParameterSet;
}

namespace avgen::scene {

struct PostOutlineSettings {
    float amount = 0.0f;            // 0..1: the line's opacity (0 = the pass is off)
    glm::vec3 color{0.0f};          // the line's colour (black ink by default)
    float intensity = 1.0f;         // multiplies the colour: scene-linear, so > 1 glows
    float width = 1.5f;             // pixels at 1080 lines
    float depthThreshold = 0.08f;   // relative second difference of 1 / depth that is an edge
    float normalThreshold = 0.35f;  // 1 - cos(angle) between normals that is an edge
    float silhouette = 0.0f;        // 1 = depth and object edges only (no creases)
    float objectEdges = 1.0f;       // 1 = a change of object is an edge
    float fadeStart = 0.0f;         // metres: full strength nearer than this
    float fadeEnd = 0.0f;           // metres: gone beyond this (<= fadeStart: no fade)

    [[nodiscard]] bool active() const { return amount > 0.0f; }
};

struct PostOutlineParameters {
    struct Row {
        params::Parameter<float>* param = nullptr;
        float PostOutlineSettings::*field = nullptr;
        const char* key = nullptr; // the scene file's `post` key ("outlineAmount")
    };
    std::vector<Row> rows;
    params::Parameter<glm::vec3>* color = nullptr; // post/outline/color, key "outlineColor"
};

[[nodiscard]] PostOutlineParameters registerPostOutlineParameters(params::ParameterSet& params,
                                                                  const PostOutlineSettings& defaults);
void applyPostOutlineParameters(const PostOutlineParameters& p, PostOutlineSettings& settings);
// A scene's `post` block key: true when it is one of these (and was applied). `error` is set when the
// key is ours but the value has the wrong shape.
bool applyPostOutlineJsonKey(const PostOutlineParameters& p, const std::string& key, const nlohmann::json& value,
                             std::string& error);

} // namespace avgen::scene
