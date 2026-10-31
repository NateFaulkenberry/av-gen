#pragma once

// ADR-1873: CORRUPTION -- a world-space data mosh on real geometry, around one focus point (the Strange Attractor's
// portal). Not a screen effect: the target meshes' own vertices are displaced in the entity vertex stage, so what
// corrupts is lit, shadowed (every depth and shadow pass draws the same displacement) and seen in perspective.
//
// One scene-level FIELD drives everything: `amount` x a falloff from `focus` (full inside `inner` x `radius`, gone
// at `radius`) x how near the camera is (`fieldFar` .. `fieldNear`). Inside it:
//   * BLOCKS of the surface (cubes `block` m on a side, chosen by an integer hash of the cell and a per-cell epoch at
//     `rate` re-picks a second, a share `share` of them at full field) slip along the wall by up to `slip` m, push
//     out of it by up to `lift` m, and slide the strata coordinate (uv.x) by up to `slide` bands. Each slip eases in,
//     holds and eases out over its epoch, so nothing jumps: the triangles between a slipped block and its neighbours
//     stretch -- the smear -- and the bands inside it slide as if the motion vectors had gone wrong.
//   * STRIPS of the wall `strip` m tall (along the aperture's up) slide sideways past each other by up to `stripSlip`.
//   * a PULL toward the focus (`pull` m at full field), and a soft FOLD that ripples the wall in and out (`fold` m).
//   * FROZEN blocks: some cells keep only part of the pull and the strip slip (`lag`), so they lag behind their
//     neighbours like stale data.
//   * ECHOES: the renderer draws `echoes` extra copies of every target mesh near the focus; copy k shows only the
//     slipped blocks, at k / (echoes + 1) of the way back toward where they were -- stale copies trailing behind.
//   * the APERTURE, opening with the camera's approach (`openFar` .. `openNear` metres, eased, plus the `open`
//     parameter): a hairline seam of light `width` m long first (the first `seam` of the opening), then splitting
//     open to `height` -- the rock inside drawn back `depth` m into a dark throat, the lips curling out toward the
//     camera. Its edge is cut on a coarse block grid, torn rather than drawn.
//   * GLYPHS: world-anchored code cells on the rock, only in the scan bands near the focus and only on the targets
//     (never on water), sized by distance so a glyph is never under ~16 px, hard-capped by `glyph`.
//
// Every state is a pure function of the clock, the parameters and the camera's distance to the focus, so a seek lands
// on the same picture. `amount` 0 and the aperture shut is byte-identical to no block.
//
// File block (top level of a scene file):
//
//   "corruption": {"targets": ["attractor.terrain"], "focus": [x, y, z], "axis": [x, y, z], "amount": 1, ...}
//
// `axis` points INTO the rock (the direction the camera crosses the aperture). Every float key is a parameter
// `corruption/<key>`; `targets`, `focus`, `axis`, `echoes` and `glowColor` are structural.

#include "core/error.hpp"

#include <glm/glm.hpp>
#include <nlohmann/json_fwd.hpp>

#include <array>
#include <string>
#include <vector>

namespace avgen::params {
class IParameter;
class ParameterSet;
} // namespace avgen::params

namespace avgen::scene {

struct Corruption {
    std::vector<std::string> targets; // entity-name prefixes; empty = none
    glm::vec3 focus{0.0f};
    glm::vec3 axis{0.0f, 0.0f, -1.0f}; // into the rock
    float amount = 0.0f;   // master 0..1
    float radius = 400.0f; // the field's reach (m)
    float inner = 0.12f;   // full field inside inner x radius
    float fieldNear = 60.0f;   // camera distance at which the field is full
    float fieldFar = 1500.0f;  // ... and nothing
    float open = 0.0f;         // added to the approach-driven opening (0..1)
    float openNear = 30.0f;    // camera distance at which the aperture is fully open
    float openFar = 400.0f;    // ... and shut
    float width = 22.0f;   // aperture half-width (m), along the wall
    float height = 16.0f;  // aperture half-height (m)
    float depth = 30.0f;   // how far the throat is drawn back (m)
    float seam = 0.3f;     // the share of the opening spent as a growing seam
    float block = 6.0f;    // block size (m)
    float share = 0.35f;   // share of blocks corrupted at full field
    float slip = 4.0f;     // block slip (m)
    float rate = 0.6f;     // block re-picks per second
    float strip = 5.0f;    // strip height (m)
    float stripSlip = 2.5f;
    float slide = 0.5f;    // strata bands slid inside a slipped block
    float pull = 10.0f;    // pull toward the focus (m)
    float lift = 2.5f;     // block push out of the wall (m)
    float fold = 1.5f;     // fold amplitude (m)
    float lag = 0.65f;     // a frozen cell keeps 1 - lag of the pull and strip slip
    float seed = 0.0f;
    float glyph = 0.0f;      // glyph radiance (0 = none)
    float glyphSize = 1.6f;  // glyph cell (m) at close range
    float glow = 6.0f;       // the seam and rim radiance
    float echoOffset = 1.0f; // scales how far back the echoes trail (1 = all the way to the original place)
    float light = 0.0f;      // a point light in the opening (candela at full opening): its glow and the other world's
                             // light on the rock around it (0 = none)
    int echoes = 0;          // extra copies (0..3)
    glm::vec3 glowColor{1.0f, 0.55f, 0.18f};

    // Written each frame by the Composition (not authored): the field scale and the opening after the camera's
    // distance, and the previous frame's time (the velocity target displaces last frame's vertex at it).
    float liveAmount = 0.0f;
    float liveOpen = 0.0f;
    float prevTime = 0.0f;

    [[nodiscard]] bool active() const { return !targets.empty() && (liveAmount > 0.0f || liveOpen > 0.0f); }
};

inline constexpr int kMaxCorruptionEchoes = 3;

// True when `name` starts with one of `c.targets`.
[[nodiscard]] bool corruptionTargets(const Corruption& c, const std::string& name);

// The live field scale and opening for a camera at `cameraDistance` from the focus.
[[nodiscard]] float corruptionFieldScale(const Corruption& c, float cameraDistance);
[[nodiscard]] float corruptionOpening(const Corruption& c, float cameraDistance);

// The nine vec4s the frame block carries (shaders/corruption.wgsl reads them). All zero when inactive.
[[nodiscard]] std::array<glm::vec4, 9> packCorruption(const Corruption& c, bool active);

// The CPU twin of `crDisplace` in shaders/corruption.wgsl: where a target vertex at `p` (world) is drawn at `time`,
// and how far its strata coordinate slides. `echo` 0 is the surface itself; k >= 1 an echo copy (`keep` false where
// the copy shows nothing).
struct CorruptedPoint {
    glm::vec3 position{0.0f};
    float uvShift = 0.0f;
    bool keep = true;
};
[[nodiscard]] CorruptedPoint corruptPoint(const std::array<glm::vec4, 9>& packed, glm::vec3 p, float time, int echo);

[[nodiscard]] Result<Corruption> corruptionFromJson(const nlohmann::json& j);
[[nodiscard]] nlohmann::json corruptionToJson(const Corruption& c);

struct CorruptionParameters {
    std::vector<params::IParameter*> all;
};
[[nodiscard]] CorruptionParameters registerCorruptionParameters(params::ParameterSet& params, const Corruption& rest,
                                                                const std::string& prefix);
void applyCorruptionParameters(const CorruptionParameters& p, const Corruption& rest, Corruption& live);

} // namespace avgen::scene
