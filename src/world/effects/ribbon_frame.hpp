#pragma once

// RIBBON: camera-facing strips, built on the CPU, drawn in pass 1's blended section (Effect Library
// Wave 1, docs/design/effect-library/shared-infrastructure.md "RIBBON").
//
// A strip is a point list -- a Trail's history samples today; a BOLT polyline or a two-anchor
// sweep later -- subdivided with Catmull-Rom and written as TWO vertices per point, one either side
// of the centre line. The vertex stage (shaders/ribbon.wgsl) does the camera-facing expansion,
// `normalize(cross(tangent, toCamera)) * halfWidth`, and clamps the width to a minimum on screen so a
// thin trail fades rather than aliasing away; the fragment stage shapes a hard CORE and/or a
// Gaussian GLOW across the strip. Every vertex is plain GPU-ready data, so the renderer uploads the
// block with one `WriteBuffer` and never evaluates an effect.
//
// **Budget.** One frame holds at most `kRibbonVertexBudget` vertices. A strip that does not fit is
// first drawn at reduced detail (fewer subdivisions, then every other point) and only then refused,
// and the builder says which: `Partial` with a reason for the first, `Dropped` with a reason for the
// second (ADR-703).
//
// **Gate.** No strip means no draw, no upload and no state change in the pass: the frame is
// byte-identical to one rendered by a build without this primitive (proved in
// tests/rendering/test_ribbon_gpu.cpp).

#include <glm/glm.hpp>

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace avgen::world {

struct EffectInstance;
struct EffectContext;
class HistoryBank;
enum class EffectKind : std::uint8_t;
enum class EffectStatus : std::uint8_t;

inline constexpr std::uint32_t kRibbonVertexBudget = 65536;
// A strip is one draw; a frame with more strips than this refuses the rest by name.
inline constexpr std::uint32_t kMaxRibbonStrips = 256;
// On screen, a strip is never narrower than this. Below it the width is held and the opacity is
// scaled by what was lost, so a trail at a kilometre is a faint thread rather than a dotted line.
inline constexpr float kRibbonMinPixelWidth = 1.5f;

enum class RibbonBlend : std::uint8_t { Additive, Alpha };

// One vertex, 64 bytes. Mirrors `RibbonIn` in shaders/ribbon.wgsl; the static_assert and the
// shader's four vec4 locations are the two halves of the contract.
struct RibbonVertex {
    glm::vec4 positionSide{0.0f}; // xyz = centre-line point (world), w = side, -1 or +1
    glm::vec4 tangentWidth{0.0f}; // xyz = tangent along the strip (world), w = half-width (m)
    glm::vec4 color{0.0f};        // rgb = HDR radiance, a = opacity (fade and envelope included)
    glm::vec4 profile{0.0f};      // x = core fraction of the half-width, y = glow amount,
                                  // z = core brightness boost, w = 1 for a soft edge ramp, 0 hard
};
static_assert(sizeof(RibbonVertex) == 64);

struct RibbonStrip {
    std::uint32_t firstVertex = 0;
    std::uint32_t vertexCount = 0; // a triangle strip: 2 per point
    RibbonBlend blend = RibbonBlend::Additive;
};

// The frame block (`scene::Scene::ribbons`). Plain data; the renderer reads it.
struct RibbonFrame {
    std::vector<RibbonVertex> vertices;
    std::vector<RibbonStrip> strips;
    std::uint32_t dropped = 0; // strips refused this frame for want of room
    [[nodiscard]] bool empty() const { return strips.empty(); }
};

// ---- building strips -----------------------------------------------------------------------------

// A point on a strip's centre line, head first.
struct RibbonPoint {
    glm::vec3 position{0.0f};
    float width = 0.0f;       // full width, metres
    glm::vec3 color{0.0f};    // HDR radiance at this point
    float opacity = 0.0f;
};

struct RibbonStyle {
    RibbonBlend blend = RibbonBlend::Additive;
    float coreFraction = 0.6f; // where the core ends, as a fraction of the half-width
    float glow = 0.0f;         // Gaussian glow across the whole width, 0 = none
    float coreBoost = 1.0f;    // the core's radiance multiplier (a Light Trail's white-hot centre)
    bool softEdge = true;      // ramp from the core to the rim rather than a hard anti-aliased edge
};

enum class RibbonFit : std::uint8_t {
    Written, // as asked
    Reduced, // written at lower detail to fit the budget
    NoFit,   // not written: even the coarsest version does not fit
    Nothing, // fewer than two distinct points: nothing to draw
};

// What a producer writes into. Owns no storage: it appends to the frame, keeping the budget.
class RibbonSink {
public:
    // `filtered` and `dense` are the caller's scratch, kept across frames so building allocates
    // nothing once they have grown.
    RibbonSink(RibbonFrame& frame, std::vector<RibbonPoint>& filtered, std::vector<RibbonPoint>& dense)
        : frame_(frame), filtered_(filtered), dense_(dense) {}
    [[nodiscard]] std::uint32_t remaining() const;
    // Appends one strip through `points` (head first), each segment Catmull-Rom subdivided into
    // `subdivisions` + 1 pieces. Reduces detail to fit before it refuses.
    RibbonFit appendStrip(std::span<const RibbonPoint> points, int subdivisions, const RibbonStyle& style);
    [[nodiscard]] static std::uint32_t verticesFor(std::size_t points, int subdivisions);

private:
    RibbonFit write(std::span<const RibbonPoint> points, int subdivisions, std::size_t stride,
                    const RibbonStyle& style);
    RibbonFrame& frame_;
    std::vector<RibbonPoint>& filtered_;
    std::vector<RibbonPoint>& dense_;
};

// ---- per-type producers --------------------------------------------------------------------------
//
// The builder is type-agnostic: a type in the `Ribbon` bucket registers how it turns one instance
// into strips, from its own file, when its schema is built -- so this file names no type. `emit`
// returns the status the instance gets (`Drawn`, `Dormant`, `Partial`, `Dropped`) and may write a
// reason. `historySeconds` is how much of its owner's transform history the instance reads, which
// is what subscribes the owner to HIST.
struct RibbonProducer {
    EffectStatus (*emit)(const EffectInstance&, const EffectContext&, const HistoryBank&, RibbonSink&,
                         std::string& reason) = nullptr;
    float (*historySeconds)(const EffectInstance&) = nullptr;
};
void registerRibbonProducer(EffectKind kind, RibbonProducer producer);
[[nodiscard]] const RibbonProducer* ribbonProducer(EffectKind kind);

// The Ribbon stage's builder (ADR-703's convention). Clears `out`, walks `order`, and writes the
// status (and the reason, for Dropped and Partial) of every instance whose type is in the `Ribbon`
// bucket. Allocation-free once `out` has grown to its budget.
void buildRibbonFrame(std::span<const EffectInstance> effects, const EffectContext& ctx,
                      const HistoryBank& history, RibbonFrame& out, std::span<const std::uint32_t> order,
                      std::span<EffectStatus> status, std::span<std::string> reasons);

// How far back `effect` reads its owner's history, in seconds; 0 when its type reads none. What the
// engine's subscription pass asks of every instance.
[[nodiscard]] float ribbonHistorySeconds(const EffectInstance& effect);

} // namespace avgen::world
