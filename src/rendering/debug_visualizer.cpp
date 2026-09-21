#include "rendering/debug_visualizer.hpp"

#include "core/wind.hpp"

#include "rendering/procedural_renderer.hpp"

#include "rendering/light_data.hpp"
#include "rendering/visibility.hpp"
#include "spatial/field.hpp"

#include <array>

#include <algorithm>
#include <vector>
#include <cmath>
#include <limits>

namespace avgen::rendering {

namespace {

constexpr glm::vec4 kBoundsColour{0.35f, 0.75f, 1.0f, 0.7f};
constexpr glm::vec4 kNormalColour{0.4f, 1.0f, 0.55f, 0.8f};
constexpr glm::vec4 kSplineColour{1.0f, 0.65f, 0.85f, 0.9f};
constexpr glm::vec4 kFieldColour{1.0f, 0.55f, 0.3f, 0.75f};
constexpr glm::vec4 kSdfColour{0.5f, 1.0f, 0.9f, 0.7f};
constexpr glm::vec4 kOriginColour{1.0f, 1.0f, 1.0f, 0.9f};
constexpr glm::vec4 kTrailColour{1.0f, 0.45f, 0.15f, 0.9f};
constexpr glm::vec4 kFrustumColour{0.85f, 0.85f, 0.4f, 0.7f};
constexpr glm::vec4 kJointColour{1.0f, 0.85f, 0.3f, 0.95f};
constexpr glm::vec4 kBoneColour{0.3f, 0.9f, 1.0f, 0.85f};
// ADR-262: the emitter disc and the column's own axis.
constexpr glm::vec4 kBeamColour{0.25f, 0.95f, 1.0f, 0.95f};
// Where the column ends, dimmer -- "it stops here" is a softer fact than "it starts here", because
// the particles fade over their life rather than switching off.
constexpr glm::vec4 kBeamEndColour{0.25f, 0.95f, 1.0f, 0.35f};

// One colour per cascade, in the order the shader selects them: near to far. Four, because
// `kMaxCascades` is four; a spot or a cube face past that takes the last one and is distinguished
// by not having a camera slice.
constexpr glm::vec4 kCascadeColour[4] = {{1.00f, 0.35f, 0.30f, 0.80f},
                                         {0.45f, 1.00f, 0.40f, 0.80f},
                                         {0.35f, 0.60f, 1.00f, 0.80f},
                                         {1.00f, 0.85f, 0.30f, 0.80f}};
// Caster state, coloured. Green casts, amber casts from off screen, red does not cast at all --
// and red is one colour rather than four because the reason is a string the panel prints, not
// something a line can carry.
constexpr glm::vec4 kCasterColour{0.35f, 1.00f, 0.45f, 0.85f};
constexpr glm::vec4 kCasterOffScreenColour{1.00f, 0.70f, 0.20f, 0.85f};
constexpr glm::vec4 kNotCasterColour{1.00f, 0.30f, 0.30f, 0.55f};

// Lighting Lab. A light is drawn in its own colour so overlapping lights can be told apart, and
// these are the two fixed parts: the influence sphere is dimmer than the emitter because it is a
// consequence rather than an authored thing, and a disabled light is drawn at all rather than
// omitted -- "there is no light here" and "the light here is switched off" are different answers.
constexpr float kEmitterAlpha = 0.95f;
constexpr float kReachAlpha = 0.18f;
// `DebugVertex::size` is a **world-space diameter** (shaders/debug.wgsl), not a pixel size, and
// `DebugViewOptions::pointSize` defaults to 3. A light marker drawn at `pointSize` is a 3 m ball
// across the middle of the frame; this is the same 0.12 scale `worldAxes` uses for the origin.
constexpr float kMarkerScale = 0.12f;
constexpr glm::vec4 kLightOffColour{0.45f, 0.45f, 0.5f, 0.35f};

// Blue to red across a normalised value.
glm::vec4 heat(float t, float alpha) {
    t = std::clamp(t, 0.0f, 1.0f);
    return glm::vec4(t, 0.35f * (1.0f - std::abs(t - 0.5f) * 2.0f) + 0.2f, 1.0f - t, alpha);
}

// The eight corners of a camera's frustum, through the inverse of the matrix the camera would draw
// with. NDC z runs 0..1 here because that is what the projection produces (WebGPU/Metal); using the
// OpenGL -1..1 cube would put the near plane halfway down the volume and the box would be a
// plausible, wrong shape.
void drawFrustum(DebugDraw& draw, const scene::Camera& camera, float aspect) {
    const glm::mat4 inverse = glm::inverse(camera.projection(std::max(1e-3f, aspect)) * camera.view());
    std::array<glm::vec3, 8> corner{};
    for (int i = 0; i < 8; ++i) {
        const glm::vec4 ndc((i & 1) ? 1.0f : -1.0f, (i & 2) ? 1.0f : -1.0f, (i & 4) ? 1.0f : 0.0f, 1.0f);
        const glm::vec4 world = inverse * ndc;
        if (std::abs(world.w) < 1e-8f || !std::isfinite(world.w)) {
            return; // a degenerate projection draws nothing rather than a box at infinity
        }
        corner[static_cast<std::size_t>(i)] = glm::vec3(world) / world.w;
    }
    // Near face 0,1,3,2 -- the bit pattern makes 0-1 and 2-3 the horizontal edges.
    const int nearLoop[4] = {0, 1, 3, 2};
    const int farLoop[4] = {4, 5, 7, 6};
    for (int i = 0; i < 4; ++i) {
        draw.line(corner[static_cast<std::size_t>(nearLoop[i])], corner[static_cast<std::size_t>(nearLoop[(i + 1) % 4])],
                  kFrustumColour);
        draw.line(corner[static_cast<std::size_t>(farLoop[i])], corner[static_cast<std::size_t>(farLoop[(i + 1) % 4])],
                  kFrustumColour);
        draw.line(corner[static_cast<std::size_t>(nearLoop[i])], corner[static_cast<std::size_t>(farLoop[i])],
                  kFrustumColour);
    }
    // The basis, so "which way is the camera facing" is answerable without reading the box.
    const glm::mat4 view = camera.view();
    const glm::mat3 basis = glm::transpose(glm::mat3(view));
    glm::mat4 frame(basis);
    frame[3] = glm::vec4(camera.position, 1.0f);
    draw.axis(frame, glm::length(corner[0] - camera.position) * 0.5f);
}


// The twelve edges of a box given as eight corners in the bit order (x, y, z) = (1, 2, 4), which is
// the order `frustumCorners` and the NDC loops above both produce.
void drawCorners(DebugDraw& draw, const std::array<glm::vec3, 8>& c, const glm::vec4& colour) {
    static constexpr int kNear[4] = {0, 1, 3, 2};
    static constexpr int kFar[4] = {4, 5, 7, 6};
    for (int i = 0; i < 4; ++i) {
        draw.line(c[static_cast<std::size_t>(kNear[i])], c[static_cast<std::size_t>(kNear[(i + 1) % 4])], colour);
        draw.line(c[static_cast<std::size_t>(kFar[i])], c[static_cast<std::size_t>(kFar[(i + 1) % 4])], colour);
        draw.line(c[static_cast<std::size_t>(kNear[i])], c[static_cast<std::size_t>(kFar[i])], colour);
    }
}

// The part of the camera frustum between two view depths: the volume whose pixels select this
// cascade. `frustumCorners` gives the whole thing; a view depth is linear along each edge between
// the near and far faces, which is the same interpolation `fitDirectionalCascade` does -- and it is
// the same call, so the slice drawn is the slice fitted.
std::array<glm::vec3, 8> cameraSlice(const glm::mat4& invViewProj, float cameraNear, float cameraFar,
                                     float nearDepth, float farDepth) {
    const std::array<glm::vec3, 8> corners = frustumCorners(invViewProj);
    const float span = std::max(cameraFar - cameraNear, 1e-4f);
    const float t0 = std::clamp((nearDepth - cameraNear) / span, 0.0f, 1.0f);
    const float t1 = std::clamp((farDepth - cameraNear) / span, 0.0f, 1.0f);
    std::array<glm::vec3, 8> slice{};
    for (std::size_t i = 0; i < 4; ++i) {
        slice[i] = corners[i] + (corners[i + 4] - corners[i]) * t0;
        slice[i + 4] = corners[i] + (corners[i + 4] - corners[i]) * t1;
    }
    return slice;
}

glm::vec4 idColour(int id, float alpha) {
    const auto h = static_cast<std::uint32_t>(id) * 2654435761u;
    return glm::vec4(static_cast<float>((h >> 16) & 0xFF) / 255.0f, static_cast<float>((h >> 8) & 0xFF) / 255.0f,
                     static_cast<float>(h & 0xFF) / 255.0f, alpha);
}

} // namespace

// The rung colours. Deliberately not a heat ramp over a normalised value like `heat()`: a rung is
// one of four named things, not a position on a scale, and four fixed colours can be read off a
// still frame without a legend. Grey is "no decision available", which is a different statement
// from rung 0 and has to look like one.
constexpr std::array<glm::vec4, 4> kLodColours{{
    {0.30f, 1.00f, 0.45f, 0.95f}, // rung 0, the full mesh
    {1.00f, 0.90f, 0.25f, 0.95f}, // rung 1
    {1.00f, 0.50f, 0.15f, 0.95f}, // rung 2
    {1.00f, 0.25f, 0.30f, 0.95f}, // rung 3
}};
constexpr glm::vec4 kLodCulledColour{0.45f, 0.10f, 0.55f, 0.9f}; // rejected by the cull
constexpr glm::vec4 kLodUnknownColour{0.55f, 0.55f, 0.55f, 0.6f}; // the pass did not run for it

ProceduralLodLevels readProceduralLodLevels(ProceduralRenderer& procedurals,
                                            const scene::Scene& scene,
                                            const DebugViewOptions& options) {
    ProceduralLodLevels out;
    if (!options.lod) {
        return out;
    }
    for (const scene::ProceduralGeometry& pg : scene.procedurals) {
        if (!pg.visible || pg.instances.empty()) {
            continue;
        }
        // ADR-108: a material part shares its lead's decision and owns no buffer of its own, so the
        // lead is asked and the part is coloured from the same answer.
        const std::string& owner = pg.partOf.empty() ? pg.name : pg.partOf;
        auto levels = procedurals.readLodLevels(owner);
        // Not `fresh` means the cull dispatches were not encoded this frame, so the buffer holds
        // the last frame that ran them. Dropped rather than drawn: an overlay that shows history
        // while claiming to show this frame is worse than one that shows nothing (spec 37).
        if (!levels || !levels->fresh || levels->level.size() != pg.instances.size()) {
            continue;
        }
        out.emplace(pg.name, std::move(levels->level));
    }
    return out;
}

// A light's own colour, normalised so a 40,000 candela practical and a 0.2 fill draw at the same
// brightness: the overlay is about *where* the light is, and an intensity-scaled colour would make
// the dim ones invisible, which is the opposite of what someone looking for a missing light wants.
glm::vec4 lightColour(const scene::PunctualLight& light, float alpha) {
    const glm::vec3 c = light.color * scene::colorTemperatureToRgb(light.temperature, light.tint);
    const float peak = std::max({c.r, c.g, c.b, 1e-4f});
    return glm::vec4(glm::clamp(c / peak, glm::vec3(0.05f), glm::vec3(1.0f)), alpha);
}

// Three great circles, which reads as a sphere from any angle and costs 96 lines rather than a
// tessellated ball.
void drawSphere(DebugDraw& draw, const glm::vec3& centre, float radius, const glm::vec4& colour,
                int segments = 32) {
    if (!(radius > 0.0f) || !std::isfinite(radius)) {
        return;
    }
    draw.circle(centre, glm::vec3(0.0f, 1.0f, 0.0f), radius, colour, segments);
    draw.circle(centre, glm::vec3(1.0f, 0.0f, 0.0f), radius, colour, segments);
    draw.circle(centre, glm::vec3(0.0f, 0.0f, 1.0f), radius, colour, segments);
}

// The emitter the shading pass actually integrates, per kind. Not a generic marker: the difference
// between a Rect and a Disk of the same nominal size is a factor of pi/4 in the light it throws,
// and a lab that draws both as a dot cannot show that.
void drawEmitter(DebugDraw& draw, const scene::PunctualLight& light, const glm::vec4& colour,
                 float reach) {
    using Type = scene::PunctualLight::Type;
    const glm::vec3 dir = glm::dot(light.direction, light.direction) > 1e-12f ? glm::normalize(light.direction)
                                                                 : glm::vec3(0.0f, -1.0f, 0.0f);
    switch (light.type) {
    case Type::Directional:
        // No position and no extent. Drawn as an arrow through the camera's own target, because a
        // directional light's only authored quantity is the direction it travels and the only
        // honest place to draw it is where the frame is looking.
        break;
    case Type::Point:
        draw.arrow(light.position, dir, std::min(reach, 2.0f), colour);
        break;
    case Type::Spot: {
        // The outer cone, out to the reach, with the inner cone inside it: the two angles are what
        // `packLight` turns into the spot term, and a cone drawn at one angle hides the falloff.
        const float len = std::max(reach, 0.01f);
        for (const float angle : {light.outerConeAngle, light.innerConeAngle}) {
            if (!(angle > 0.0f)) {
                continue;
            }
            const float r = len * std::tan(std::clamp(angle, 0.0f, 1.55f));
            const glm::vec3 rim = light.position + dir * len;
            draw.circle(rim, dir, r, colour);
            glm::vec3 right = glm::cross(dir, glm::vec3(0.0f, 1.0f, 0.0f));
            if (glm::dot(right, right) < 1e-6f) {
                right = glm::vec3(1.0f, 0.0f, 0.0f);
            }
            right = glm::normalize(right);
            const glm::vec3 up = glm::cross(right, dir);
            for (int i = 0; i < 4; ++i) {
                const float a = static_cast<float>(i) * 1.5707963f;
                draw.line(light.position, rim + (right * std::cos(a) + up * std::sin(a)) * r, colour);
            }
        }
        break;
    }
    case Type::Rect:
    case Type::Disk: {
        // Exactly the quad `rendering::areaLightCorners` hands the LTC integration -- a Disk's is
        // the square of equal area, which is what the shader uses and therefore what is true.
        glm::vec3 corners[4];
        rendering::areaLightCorners(light, corners);
        const std::array<glm::vec3, 4> loop{corners[0], corners[1], corners[2], corners[3]};
        draw.polyline(loop, colour, true);
        if (light.type == Type::Disk) {
            draw.circle(light.position, dir, std::max(light.radius, 1e-3f), colour);
        }
        draw.arrow(light.position, dir, std::max(light.radius, std::max(light.width, light.height)) * 0.75f,
                   colour);
        break;
    }
    case Type::Tube: {
        glm::vec3 right = glm::cross(dir, glm::vec3(0.0f, 1.0f, 0.0f));
        if (glm::dot(right, right) < 1e-6f) {
            right = glm::vec3(1.0f, 0.0f, 0.0f);
        }
        right = glm::normalize(right);
        const float half = std::max(light.width, 1e-3f) * 0.5f;
        const float r = std::max(light.radius, 1e-3f);
        const glm::vec3 a = light.position - right * half;
        const glm::vec3 b = light.position + right * half;
        draw.line(a, b, colour);
        draw.circle(a, right, r, colour, 16);
        draw.circle(b, right, r, colour, 16);
        break;
    }
    case Type::Sphere:
        drawSphere(draw, light.position, std::max(light.radius, 1e-3f), colour, 20);
        break;
    }
}

// ADR-382 §19. The wind field, drawn as it actually is rather than as it is described: an arrow per
// grid point from `wind::sampleWind` -- the same CPU function `shaders/wind.wgsl` transliterates,
// so a divergence between them shows up here as arrows that disagree with the foliage.
void buildWindDebug(DebugDraw& draw, const scene::Scene& scene, double time) {
    const wind::WindParams& w = scene.environment.wind;
    if (!w.active()) {
        // An honest nothing: a view that drew a default field for a scene with no wind would be the
        // "diagnostic that shows the same picture whatever the state" this file's header forbids.
        return;
    }
    const wind::WindUniforms u = wind::packWind(w);
    // Centred on the first declared wind body if there is one, because that is what anybody turning
    // this on is looking at; on the origin otherwise.
    glm::vec3 centre(0.0f);
    float span = 120.0f;
    for (const scene::Entity& e : scene.entities) {
        if (e.wind.active()) {
            centre = e.wind.origin;
            span = std::max(e.wind.radius * 3.0f, 40.0f);
            break;
        }
    }
    const int grid = 9;
    const float step = span * 2.0f / static_cast<float>(grid - 1);
    for (int z = 0; z < grid; ++z) {
        for (int x = 0; x < grid; ++x) {
            const glm::vec3 p(centre.x - span + step * static_cast<float>(x), centre.y,
                              centre.z - span + step * static_cast<float>(z));
            const wind::WindSample s = wind::sampleWind(u, p, static_cast<float>(time));
            const float strength = s.strength * (1.0f + s.gust);
            const glm::vec3 dir(s.direction.x, 0.0f, s.direction.y);
            const glm::vec3 tip = p + dir * (step * 0.45f * std::min(strength, 3.0f));
            // Hue by strength, so the regional variation and the travelling gust fronts are
            // readable as bands rather than having to be inferred from arrow lengths.
            const float t = std::clamp(strength / 3.0f, 0.0f, 1.0f);
            const glm::vec4 colour(0.2f + 0.8f * t, 0.9f - 0.5f * t, 1.0f - 0.8f * t, 0.9f);
            draw.line(p, tip, colour);
            draw.point(tip, step * 0.06f, colour);
        }
    }
    // Every body's frame: origin, height and radius are what the deformation is keyed on, and
    // getting them wrong is invisible in the picture until the tree bends about the wrong point.
    for (const scene::Entity& e : scene.entities) {
        if (!e.wind.active()) {
            continue;
        }
        const glm::vec4 c(1.0f, 0.75f, 0.25f, 0.9f);
        draw.line(e.wind.origin, e.wind.origin + glm::vec3(0.0f, e.wind.height, 0.0f), c);
        const int n = 32;
        for (int i = 0; i < n; ++i) {
            const float a0 = 6.2831853f * static_cast<float>(i) / n;
            const float a1 = 6.2831853f * static_cast<float>(i + 1) / n;
            const glm::vec3 p0 = e.wind.origin + glm::vec3(std::cos(a0), 0.0f, std::sin(a0)) * e.wind.radius;
            const glm::vec3 p1 = e.wind.origin + glm::vec3(std::cos(a1), 0.0f, std::sin(a1)) * e.wind.radius;
            draw.line(p0, p1, c);
        }
        break; // one is the point; a forest of them is a different view
    }
}

// ADR-382 §19. The vortex: mouth, throat, depth and which way it turns. This is the view that would
// have shown the funnel extending upward as a cylinder, and the camera being inside the mouth,
// without a single render of the beauty pass.
void buildVortexDebug(DebugDraw& draw, const scene::Scene& scene, double time) {
    // ADR-562: EVERY placed medium, not the first. This view exists to show what the march is
    // actually drawing, and while the march carried one medium "the vortex" and "what is drawn"
    // were the same thing. They are not any more -- which is precisely the defect ADR-560 measured
    // and this view could have shown, had it existed in a world with more than one slot.
    //
    // Read out of the packed lanes, and that makes this view MORE correct rather than less:
    // `packVortex` applies the clamps, so what is drawn here is what the shader marches rather than
    // what was authored. This file's own comment below asks for exactly that agreement -- "so this
    // view and the shader cannot disagree about the shape" -- and before the lanes it could not
    // have it, because an authored value outside a clamp would draw one shape and march another.
    //
    // Lane map beside each kind's `packMedium`; named here rather than indexed inline.
    for (std::uint32_t slot = 0; slot < scene.atmospherics.mediumCount; ++slot) {
        const world::MediumSlot& m = scene.atmospherics.media[slot];
        const glm::vec3 centre(m.lane[0]);
        const float radius = m.lane[0].w;
        if (radius <= 0.0f) {
            continue;
        }
        const float rotationSpeed = m.lane[1].z;
        const float funnelDepth = m.lane[4].x;
        const float throatFraction = m.lane[4].y;

        const glm::vec4 mouthColour(0.35f, 0.85f, 1.0f, 0.95f);
        const glm::vec4 throatColour(0.9f, 0.4f, 1.0f, 0.85f);
        auto ring = [&](float y, float r, const glm::vec4& c) {
            const int n = 64;
            for (int i = 0; i < n; ++i) {
                const float a0 = 6.2831853f * static_cast<float>(i) / n;
                const float a1 = 6.2831853f * static_cast<float>(i + 1) / n;
                draw.line(centre + glm::vec3(std::cos(a0) * r, y, std::sin(a0) * r),
                          centre + glm::vec3(std::cos(a1) * r, y, std::sin(a1) * r), c);
            }
        };
        ring(0.0f, radius, mouthColour);
        // The wall, sampled down the throat exactly as the shader narrows it, so this view and the
        // march cannot disagree about the shape.
        const float depth = std::max(funnelDepth, 0.0f);
        if (depth > 0.0f) {
            const int rungs = 6;
            for (int i = 1; i <= rungs; ++i) {
                const float yn = static_cast<float>(i) / rungs;
                const float mouth = glm::mix(1.0f, std::clamp(throatFraction, 0.02f, 1.0f), yn * yn);
                ring(-depth * yn, radius * mouth, glm::mix(mouthColour, throatColour, yn));
            }
            draw.line(centre, centre - glm::vec3(0.0f, depth, 0.0f), throatColour);
        }
        // Which way it turns, and how hard: tangents on the mouth ring, length by rotation speed.
        const int arrows = 12;
        for (int i = 0; i < arrows; ++i) {
            const float a = 6.2831853f * static_cast<float>(i) / arrows;
            const glm::vec3 p = centre + glm::vec3(std::cos(a), 0.0f, std::sin(a)) * radius;
            const glm::vec3 tangent(-std::sin(a), 0.0f, std::cos(a));
            draw.line(p, p + tangent * (rotationSpeed * 400.0f),
                      glm::vec4(1.0f, 0.9f, 0.4f, 0.9f));
        }
    }
    (void)time;
}

void buildDebugGeometry(DebugDraw& draw, const scene::Scene& scene, const DebugViewOptions& options, double time,
                        const TransformHistory* history, const ProceduralLodLevels* lodLevels,
                        std::span<const ShadowView> shadowViews) {
    int budget = std::max(0, options.maxPoints);

    if (options.worldAxes) {
        // Sized from the distance to the camera so it stays legible in a two-metre room and in a
        // kilometre of terrain, and floored so it never vanishes when the camera sits on the origin.
        const float span = std::max(1.0f, glm::length(scene.camera.position) * 0.25f);
        draw.axis(glm::mat4(1.0f), span);
        draw.point(glm::vec3(0.0f), options.pointSize * 0.12f, kOriginColour);
    }

    if (options.beams) {
        // The same integration `tests/unit/test_beam_lab.cpp` asserts against, spelled here so the
        // overlay and the test cannot disagree about where the column ends.
        const auto reach = [](const scene::ParticleSystem& ps) {
            const float dt = 1.0f / 120.0f;
            float v = ps.speedMin;
            float travelled = 0.0f;
            for (float t = 0.0f; t < ps.lifetimeMin; t += dt) {
                v += -ps.gravity.y * dt;
                v *= std::max(0.0f, 1.0f - ps.drag * dt);
                travelled += v * dt;
            }
            return travelled;
        };
        for (const scene::ParticleSystem& ps : scene.particles) {
            if (!ps.enabled) {
                continue;
            }
            const glm::vec3 axis = glm::normalize(ps.direction + glm::vec3(1e-5f, 0.0f, 0.0f));
            const glm::vec3 bottom = ps.position + axis * reach(ps);
            draw.circle(ps.position, axis, ps.extent.x, kBeamColour);
            draw.line(ps.position, bottom, kBeamColour);
            // Where it stops. Dimmer, because "the column ends here" is a softer fact than "the
            // column starts here" -- the particles fade over their life rather than switching off.
            draw.circle(bottom, axis, ps.extent.x, kBeamEndColour);
            draw.point(ps.position, options.pointSize * 0.12f, kBeamColour);
        }
    }

    if (options.frustum) {
        drawFrustum(draw, scene.camera, options.frustumAspect);
    }

    // ---- the cascades (Shadow Lab) ------------------------------------------------------------
    if ((options.shadowCascades || options.shadowCascadeSlices) && !shadowViews.empty()) {
        const glm::mat4 cameraViewProj =
            scene.camera.projection(std::max(1e-3f, options.frustumAspect)) * scene.camera.view();
        const glm::mat4 invCamera = glm::inverse(cameraViewProj);
        for (std::size_t v = 0; v < shadowViews.size(); ++v) {
            if (options.shadowCascade >= 0 && static_cast<std::size_t>(options.shadowCascade) != v) {
                continue;
            }
            const ShadowView& view = shadowViews[v];
            const glm::vec4 colour = kCascadeColour[v % 4];
            if (options.shadowCascades) {
                // Through the inverse of the matrix the depth pass rasterised with. A view whose
                // matrix is singular -- which the renderer's own finite check already refuses to
                // upload -- draws nothing rather than a box at infinity.
                const glm::mat4 inverse = glm::inverse(view.viewProj);
                if (std::isfinite(inverse[0][0])) {
                    drawCorners(draw, frustumCorners(inverse), colour);
                    // The centre the fit snapped to, so a cascade that is crawling can be watched
                    // crawl: this point moves in whole texels or not at all.
                    draw.point(view.center, options.pointSize * 0.6f, colour);
                }
            }
            if (options.shadowCascadeSlices && view.cascade) {
                drawCorners(draw,
                            cameraSlice(invCamera, scene.camera.nearPlane, scene.camera.farPlane,
                                        view.nearDistance, view.farDistance),
                            colour);
            }
        }
    }

    // ---- the lights, and the froxels they were given to (Lighting Lab) -----------------------
    if (options.lights) {
        for (const scene::PunctualLight& light : scene.lights) {
            const bool on = light.enabled;
            const glm::vec4 emitter = on ? lightColour(light, kEmitterAlpha) : kLightOffColour;
            const float reach = rendering::lightInfluenceRadius(light);
            if (light.type == scene::PunctualLight::Type::Directional) {
                // Its only authored quantity is a direction, so it is drawn where the frame is
                // looking rather than at an origin it does not have.
                const glm::vec3 dir = glm::dot(light.direction, light.direction) > 1e-12f
                                          ? glm::normalize(light.direction)
                                          : glm::vec3(0.0f, -1.0f, 0.0f);
                const float span = std::max(1.0f, glm::length(scene.camera.position - scene.camera.target) * 0.3f);
                draw.arrow(scene.camera.target - dir * span, dir, span, emitter);
                continue;
            }
            draw.point(light.position, options.pointSize * kMarkerScale, emitter);
            drawEmitter(draw, light, emitter, reach);
            // The hard edge. `lightInfluenceRadius` is what the froxel pass is handed, so this ring
            // is where the light stops being evaluated at all -- not where it becomes dim.
            if (on) {
                // The horizontal great circle first and brighter: it is the one that crosses the
                // ground, which is where a reach edge is actually seen.
                draw.circle(light.position, glm::vec3(0.0f, 1.0f, 0.0f), reach,
                            lightColour(light, kReachAlpha * 2.0f), 48);
                draw.circle(light.position, glm::vec3(1.0f, 0.0f, 0.0f), reach,
                            lightColour(light, kReachAlpha), 32);
                draw.circle(light.position, glm::vec3(0.0f, 0.0f, 1.0f), reach,
                            lightColour(light, kReachAlpha), 32);
            }
        }
    }

    if (options.lightClusters) {
        // The grid the renderer builds in `SceneRenderer::updateLights`, from the same camera.
        // `frustumAspect` is the viewport's, for the reason `frustum` takes it: a scene camera does
        // not carry one, and a grid drawn at the wrong aspect is froxels that do not line up with
        // the pixels that read them.
        ClusterGrid grid;
        grid.zNear = std::max(scene.camera.nearPlane, 0.01f);
        grid.zFar = std::max(scene.camera.farPlane, grid.zNear * 2.0f);
        grid.tanHalfFovY = std::tan(scene.camera.effectiveFovY() * 0.5f);
        grid.aspect = std::max(1e-3f, options.frustumAspect);
        const glm::mat4 view = scene.camera.view();
        const glm::mat4 inverseView = glm::inverse(view);
        std::vector<glm::vec3> viewPositions;
        std::vector<float> radii;
        for (const scene::PunctualLight& light : scene.lights) {
            if (!light.enabled || light.type == scene::PunctualLight::Type::Directional) {
                continue; // directional lights reach every fragment and never enter the grid
            }
            viewPositions.emplace_back(view * glm::vec4(light.position, 1.0f));
            radii.push_back(rendering::lightInfluenceRadius(light));
        }
        const ClusterOccupancy occupancy = clusterOccupancy(grid, viewPositions, radii, kMaxLightsPerCluster);
        const auto scale = static_cast<float>(std::max(occupancy.max, 1u));
        const auto lists = assignClusters(grid, viewPositions, radii);
        for (std::uint32_t k = 0; k < grid.z; ++k) {
            if (options.lightClusterSlice >= 0 && static_cast<std::uint32_t>(options.lightClusterSlice) != k) {
                continue;
            }
            for (std::uint32_t j = 0; j < grid.y; ++j) {
                for (std::uint32_t i = 0; i < grid.x; ++i) {
                    const std::size_t count = lists[grid.indexOf(i, j, k)].size();
                    if (count == 0) {
                        continue; // an empty froxel is the common case and drawing it hides the rest
                    }
                    glm::vec3 lo;
                    glm::vec3 hi;
                    grid.bounds(i, j, k, lo, hi);
                    // The near face only: a full box per froxel is twelve lines and the picture
                    // becomes a solid wall before the occupancy is readable.
                    const std::array<glm::vec3, 4> face{
                        glm::vec3(inverseView * glm::vec4(lo.x, lo.y, hi.z, 1.0f)),
                        glm::vec3(inverseView * glm::vec4(hi.x, lo.y, hi.z, 1.0f)),
                        glm::vec3(inverseView * glm::vec4(hi.x, hi.y, hi.z, 1.0f)),
                        glm::vec3(inverseView * glm::vec4(lo.x, hi.y, hi.z, 1.0f))};
                    // Blue is one light, red is the busiest froxel in the frame; a froxel at the
                    // 32-light cap is drawn white, because that is the one that is losing lights.
                    const bool capped = count >= kMaxLightsPerCluster;
                    draw.polyline(face,
                                  capped ? glm::vec4(1.0f, 1.0f, 1.0f, 0.9f)
                                         : heat(static_cast<float>(count) / scale, 0.55f),
                                  true);
                }
            }
        }
    }

    if (options.transformTrail && history != nullptr) {
        const std::vector<glm::vec3> points = history->path();
        if (points.size() >= 2) {
            draw.polyline(points, kTrailColour);
        }
        for (const glm::vec3& p : points) {
            draw.point(p, options.pointSize * 0.5f, kTrailColour);
        }
    }

    // ---- caster state (Shadow Lab) ------------------------------------------------------------
    // Its own loop, deliberately, rather than a colour inside the entity loop below: that loop
    // honours `submittedOnly`, which skips exactly the camera-culled entities this overlay exists
    // to show. An off-screen caster is the interesting case, and an overlay that hides it while
    // another switch is on would be worse than none.
    if (options.shadowCasters) {
        std::vector<std::array<glm::vec4, 6>> planes;
        planes.reserve(shadowViews.size());
        for (const ShadowView& view : shadowViews) {
            planes.push_back(frustumPlanes(view.viewProj));
        }
        for (const scene::Entity& entity : scene.entities) {
            if ((!options.selectedEntity.empty() && entity.name != options.selectedEntity) ||
                entity.mesh >= scene.meshes.size()) {
                continue;
            }
            // `scene.meshBounds`, not `MeshData::bounds()`: the latter scans every vertex and this
            // runs per entity per frame. See the note in scene::entityCullBounds -- the same
            // mistake, in the same shape, cost the Tree of Life 90 ms a frame in the cull and 87 ms
            // here in the editor's debug pass.
            const auto bounds = scene.meshBounds(entity.mesh);
            const CasterState state = casterState(entity, bounds, planes);
            const auto [lo, hi] = entityWorldBounds(entity, bounds);
            draw.box(lo, hi, state == CasterState::Caster            ? kCasterColour
                             : state == CasterState::CasterOffScreen ? kCasterOffScreenColour
                                                                     : kNotCasterColour);
        }
    }

    for (std::size_t entityIndex = 0; entityIndex < scene.entities.size(); ++entityIndex) {
        const scene::Entity& entity = scene.entities[entityIndex];
        if (!entity.visible || (!options.selectedEntity.empty() && entity.name != options.selectedEntity) ||
            entity.mesh >= scene.meshes.size()) {
            continue;
        }
        // What the frame submits is what survived the camera cull. This is the *set*, not the
        // triangles: the arm answers "was this object handed to the GPU", which is the question a
        // missing object raises, and it is a claim the visualiser can make honestly.
        if (options.submittedOnly && entity.cameraCulled) {
            continue;
        }
        // Cached, for the reason above, and it matters most here: this loop has no `options`
        // guard on its entry, so it pays for every visible entity on every frame of every session
        // whether or not a single overlay is switched on. The comment at the call site says "an
        // empty set costs nothing"; with the uncached scan an empty set cost 87 ms.
        const auto [meshLo, meshHi] = scene.meshBounds(entity.mesh);
        const glm::mat4 model = entity.transform.matrix();
        glm::vec3 worldLo(std::numeric_limits<float>::max());
        glm::vec3 worldHi(std::numeric_limits<float>::lowest());
        for (int corner = 0; corner < 8; ++corner) {
            const glm::vec3 local((corner & 1) ? meshHi.x : meshLo.x,
                                  (corner & 2) ? meshHi.y : meshLo.y,
                                  (corner & 4) ? meshHi.z : meshLo.z);
            const glm::vec3 world = glm::vec3(model * glm::vec4(local, 1.0f));
            worldLo = glm::min(worldLo, world);
            worldHi = glm::max(worldHi, world);
        }
        // The id the identifier target writes for this entity, so the overlay and the `Ids`
        // auxiliary view agree on which object is which. Colouring by loop index instead would be a
        // second numbering, and two numberings is how a pick used to select the wrong tree.
        const std::uint32_t pickId = scene::packPickId(scene::PickSpace::Entity, entityIndex);
        const glm::vec4 culledColour(1.0f, 0.2f, 0.1f, 0.9f);
        const glm::vec4 boundsColour = options.entityIds ? idColour(static_cast<int>(pickId), 0.9f)
                                       : entity.cameraCulled ? culledColour
                                                             : kBoundsColour;
        if (options.entityBounds || options.entityIds) {
            draw.box(worldLo, worldHi, boundsColour);
        }
        if (options.skeletons && entity.rig < scene.rigs.size()) {
            const scene::SkinnedRig& rig = scene.rigs[entity.rig];
            // The model-space matrices the pose produced. Empty until the rig has been evaluated,
            // which is the honest state to draw nothing for: a skeleton drawn from a rest pose that
            // the frame is not using would be a second answer to "where are the joints".
            const std::vector<glm::mat4>& jointModel = rig.scratchModel;
            const std::size_t joints = std::min(jointModel.size(), rig.skeleton.joints.size());
            for (std::size_t j = 0; j < joints; ++j) {
                const glm::vec3 here = glm::vec3(model * jointModel[j][3]);
                draw.point(here, options.pointSize * 0.6f, kJointColour);
                const int parent = rig.skeleton.joints[j].parent;
                if (parent >= 0 && static_cast<std::size_t>(parent) < joints) {
                    const glm::vec3 up = glm::vec3(model * jointModel[static_cast<std::size_t>(parent)][3]);
                    draw.line(up, here, kBoneColour);
                }
            }
        }
        // Phase B §50. One block, because every one of these reads the same two things: the rig's
        // model-space joints and the per-layer state `driveLayers` wrote this frame. `model` takes
        // entity-local to world, which is the conversion a layer target needs -- a layer's target
        // is in the rig's own frame (ADR-274), and drawing it in world space without this is the
        // classic diagnostic that is confidently wrong by a whole transform.
        const bool wantMotion = options.motionChains || options.motionTargets ||
                                options.motionContacts || options.motionVectors ||
                                options.motionCompensation;
        if (wantMotion && entity.rig < scene.rigs.size()) {
            const scene::SkinnedRig& rig = scene.rigs[entity.rig];
            const scene::PoseLayerStack& stack = rig.layers;
            const std::vector<glm::mat4>& jointModel = rig.scratchModel;
            const std::vector<scene::PoseLayer>& layers = stack.layers();
            const std::vector<glm::ivec3>& chains = stack.chains();
            const std::vector<scene::IkStatus>& statuses = stack.ikStatuses();
            const std::vector<scene::LayerResolution>& results = stack.results();

            const auto worldOf = [&](int joint) {
                return glm::vec3(model * jointModel[static_cast<std::size_t>(joint)][3]);
            };
            const auto inRange = [&](int joint) {
                return joint >= 0 && static_cast<std::size_t>(joint) < jointModel.size();
            };

            for (std::size_t i = 0; i < layers.size(); ++i) {
                const scene::PoseLayer& layer = layers[i];
                // The **realized** weight, not the requested one. A layer at requested 1.0 and
                // realized 0.02 is two frames into a blend, and an overlay that showed the request
                // would say it is fully on -- which is exactly the defect §46 found, drawn as
                // though it were not happening.
                const float weight = layer.effectiveWeight();
                const bool live = weight > 0.0f && i < results.size() &&
                                  results[i] != scene::LayerResolution::Inactive;

                if (options.motionChains && i < chains.size() && live) {
                    const glm::ivec3 ids = chains[i];
                    if (inRange(ids.x) && inRange(ids.y) && inRange(ids.z)) {
                        // The colour IS the diagnostic. A chain drawn the same whatever the solver
                        // said is a picture of a leg, and this phase already has one of those.
                        const scene::IkStatus status =
                            i < statuses.size() ? statuses[i] : scene::IkStatus::Solved;
                        glm::vec4 colour{0.2f, 1.0f, 0.4f, 0.95f}; // Solved
                        if (status == scene::IkStatus::Clamped) {
                            colour = glm::vec4(1.0f, 0.75f, 0.1f, 0.95f);
                        } else if (status != scene::IkStatus::Solved) {
                            colour = glm::vec4(1.0f, 0.2f, 0.2f, 0.95f); // degenerate, of any kind
                        }
                        colour.a *= std::clamp(weight, 0.15f, 1.0f);
                        draw.line(worldOf(ids.x), worldOf(ids.y), colour);
                        draw.line(worldOf(ids.y), worldOf(ids.z), colour);
                        draw.point(worldOf(ids.z), options.pointSize * 0.9f, colour);
                    }
                }

                if (options.motionTargets && layer.hasTarget && live) {
                    const glm::vec3 target = glm::vec3(model * glm::vec4(layer.target, 1.0f));
                    const glm::vec4 colour = layer.kind == scene::PoseLayerKind::Aim
                                                 ? glm::vec4(0.4f, 0.8f, 1.0f, 0.9f)
                                                 : glm::vec4(1.0f, 0.4f, 0.9f, 0.9f);
                    draw.point(target, options.pointSize * 1.2f, colour);
                    // A line from the thing that was asked to reach it, so "the hand is not on the
                    // target" and "the target is not where you think" are distinguishable.
                    if (i < chains.size() && inRange(chains[i].z)) {
                        draw.line(worldOf(chains[i].z), target, colour);
                    } else if (inRange(stack.bodyJoint())) {
                        draw.line(worldOf(stack.bodyJoint()), target, colour);
                    }
                }

                if (options.motionContacts && layer.hasGround && live) {
                    const glm::vec3 point = glm::vec3(model * glm::vec4(layer.groundPoint, 1.0f));
                    const glm::vec3 normal =
                        glm::normalize(glm::vec3(model * glm::vec4(layer.groundNormal, 0.0f)));
                    const glm::vec4 colour{0.6f, 1.0f, 0.6f, 0.85f};
                    draw.circle(point, normal, 0.12f, colour);
                    draw.arrow(point, normal, 0.25f, colour);
                    // The gap between the plane and the foot on it: this is contact error, and it
                    // is the quantity a viewer reads as "the feet are floating".
                    if (i < chains.size() && inRange(chains[i].z)) {
                        draw.line(point, worldOf(chains[i].z), glm::vec4(1.0f, 1.0f, 0.4f, 0.9f));
                    }
                }
            }

            if (options.motionVectors && !layers.empty()) {
                // Read off any live layer: `driveLayers` writes the same body state onto all of
                // them, so this is the frame's own numbers rather than a second derivation.
                const scene::PoseLayer& any = layers.front();
                const glm::vec3 origin = glm::vec3(model[3]);
                const auto dir = [&](const glm::vec3& local) {
                    return glm::vec3(model * glm::vec4(local, 0.0f));
                };
                if (glm::length(any.bodyVelocity) > 1e-4f) {
                    draw.arrow(origin, glm::normalize(dir(any.bodyVelocity)),
                               glm::length(any.bodyVelocity) * 0.5f,
                               glm::vec4(0.3f, 1.0f, 0.9f, 0.95f));
                }
                if (glm::length(any.bodyAcceleration) > 1e-4f) {
                    draw.arrow(origin, glm::normalize(dir(any.bodyAcceleration)),
                               glm::length(any.bodyAcceleration) * 0.12f,
                               glm::vec4(1.0f, 0.6f, 0.2f, 0.95f));
                }
                // Facing is the entity's own +Z through its world transform, which is the one
                // direction here that does NOT come off a layer.
                draw.arrow(origin, glm::normalize(dir(glm::vec3(0.0f, 0.0f, 1.0f))), 0.8f,
                           glm::vec4(1.0f, 1.0f, 1.0f, 0.8f));
            }

            if (options.motionCompensation && inRange(stack.bodyJoint())) {
                const scene::BodyCompensation& body = stack.bodyCompensation();
                if (glm::length(body.translation) > 1e-5f) {
                    const glm::vec3 to = worldOf(stack.bodyJoint());
                    const glm::vec3 from =
                        to - glm::vec3(model * glm::vec4(body.translation, 0.0f));
                    // Red when limbs still cannot reach after the correction: the correction ran
                    // and was not enough, which is a different thing from it not running.
                    const glm::vec4 colour = body.unreachableAfter > 0u
                                                 ? glm::vec4(1.0f, 0.3f, 0.3f, 0.95f)
                                                 : glm::vec4(0.9f, 0.5f, 1.0f, 0.95f);
                    draw.line(from, to, colour);
                    draw.point(to, options.pointSize * 1.1f, colour);
                }
            }
        }

        if (options.entityOrigins && budget > 0) {
            draw.point(glm::vec3(model[3]), options.pointSize * 0.08f,
                       entity.cameraCulled ? culledColour : glm::vec4(1.0f, 0.9f, 0.2f, 0.9f));
            draw.axis(model, std::max(0.25f, glm::length(worldHi - worldLo) * 0.2f));
            --budget;
        }
    }

    for (const scene::ProceduralGeometry& pg : scene.procedurals) {
        if (!pg.visible) {
            continue;
        }
        if (options.bounds) {
            draw.box(pg.boundsMin, pg.boundsMax, kBoundsColour);
        }
        const bool wantPoints = options.points || options.density || options.instanceIds ||
                                options.lod || !options.attribute.empty();
        if (!wantPoints && !options.normals) {
            continue;
        }
        // ADR-029's rungs, as the cull pass assigned them (ADR-225: this checkbox had been in the
        // World panel since the option struct was written and this file read the field nowhere, so
        // it drew nothing at all). An object the caller did not read levels for is drawn grey --
        // the overlay says "I do not know", never rung 0.
        const std::vector<int>* levels = nullptr;
        if (options.lod && lodLevels != nullptr) {
            const auto found = lodLevels->find(pg.name);
            if (found != lodLevels->end() && found->second.size() == pg.instances.size()) {
                levels = &found->second;
            }
        }
        // Attribute colouring needs the cloud; instance records carry the rest.
        const spatial::PointCloud& cloud = pg.cloud;
        const bool haveCloud = cloud.count() == pg.instances.size();
        const spatial::AttributeBuffer* attribute =
            !options.attribute.empty() && haveCloud ? cloud.attributes.find(options.attribute) : nullptr;
        spatial::AttributeStats stats;
        if (attribute != nullptr) {
            stats = spatial::attributeStats(*attribute);
        }
        for (std::size_t i = 0; i < pg.instances.size() && budget > 0; ++i) {
            const scene::InstanceRecord& record = pg.instances[i];
            const glm::vec3 position(record.position);
            glm::vec4 colour(0.85f, 0.9f, 1.0f, 0.9f);
            if (options.lod) {
                if (levels == nullptr) {
                    colour = kLodUnknownColour;
                } else {
                    const int level = (*levels)[i];
                    colour = level < 0 ? kLodCulledColour
                                       : kLodColours[static_cast<std::size_t>(
                                             std::clamp(level, 0, scene::kMaxLodLevels - 1))];
                }
            } else if (attribute != nullptr) {
                const glm::vec4 value = spatial::readAsVec4(*attribute, i);
                const float span = stats.max.x - stats.min.x;
                colour = heat(span > 1e-6f ? (value.x - stats.min.x) / span : 0.5f, 0.9f);
            } else if (options.density) {
                colour = heat(std::clamp(record.position.w, 0.0f, 1.0f), 0.9f);
            } else if (options.instanceIds) {
                colour = idColour(static_cast<int>(record.color.a), 0.9f);
            }
            if (wantPoints) {
                draw.point(position, options.pointSize * 0.05f, colour);
                --budget;
            }
            if (options.normals) {
                const glm::quat rotation(record.rotation.w, record.rotation.x, record.rotation.y, record.rotation.z);
                const float length = std::max(0.2f, glm::length(glm::vec3(record.scale)) * 0.5f);
                draw.line(position, position + rotation * glm::vec3(0.0f, 1.0f, 0.0f) * length, kNormalColour);
            }
        }
    }

    if (options.splines) {
        for (const spatial::Spline& spline : scene.splines.splines) {
            const auto samples = spline.samples(96);
            std::vector<glm::vec3> path;
            path.reserve(samples.size());
            for (const spatial::SplineSample& s : samples) {
                path.push_back(s.position);
            }
            draw.polyline(path, kSplineColour, spline.closed);
            // Frames every eighth sample so the twist is visible.
            for (std::size_t i = 0; i < samples.size(); i += 8) {
                const spatial::SplineSample& s = samples[i];
                const float length = std::max(0.2f, spline.length() * 0.02f);
                draw.line(s.position, s.position + s.normal * length, glm::vec4(0.4f, 1.0f, 0.6f, 0.8f));
                draw.line(s.position, s.position + s.binormal * length, glm::vec4(1.0f, 0.5f, 0.5f, 0.8f));
            }
        }
    }

    if (options.fields || options.fieldVectors) {
        for (const spatial::FieldSpec& field : scene.fields.fields) {
            if (!field.enabled) {
                continue;
            }
            const glm::mat4 toWorld = field.localToWorld();
            const glm::vec3 origin = glm::vec3(toWorld[3]);
            if (options.fields) {
                draw.axis(toWorld, std::max(1.0f, field.falloff.outer * 0.1f));
                if (field.falloff.outer > 0.0f) {
                    draw.circle(origin, glm::vec3(0.0f, 1.0f, 0.0f), field.falloff.outer, kFieldColour, 48);
                    draw.circle(origin, glm::vec3(1.0f, 0.0f, 0.0f), field.falloff.outer, kFieldColour, 48);
                }
                if (field.falloff.inner > 0.0f) {
                    draw.circle(origin, glm::vec3(0.0f, 1.0f, 0.0f), field.falloff.inner,
                                glm::vec4(kFieldColour.r, kFieldColour.g, kFieldColour.b, 0.35f), 32);
                }
            }
            if (!options.fieldVectors) {
                continue;
            }
            const int grid = std::clamp(options.fieldGrid, 2, 24);
            const float extent = field.falloff.outer > 0.0f ? field.falloff.outer : 10.0f;
            const float step = 2.0f * extent / static_cast<float>(grid - 1);
            for (int z = 0; z < grid && budget > 0; ++z) {
                for (int y = 0; y < grid && budget > 0; ++y) {
                    for (int x = 0; x < grid && budget > 0; ++x) {
                        const glm::vec3 p = origin + glm::vec3(-extent + step * static_cast<float>(x),
                                                               -extent + step * static_cast<float>(y),
                                                               -extent + step * static_cast<float>(z));
                        const glm::vec3 v = spatial::sampleVector(field, p, time, &scene.fields);
                        const float length = glm::length(v);
                        if (length < 1e-4f) {
                            continue;
                        }
                        draw.arrow(p, v, std::min(step * 0.8f, length * step * 0.4f),
                                   heat(std::clamp(length, 0.0f, 1.0f), 0.8f));
                        --budget;
                    }
                }
            }
        }
    }

    if (options.sdfSlice) {
        for (const scene::SdfObject& object : scene.sdfs) {
            if (!object.visible) {
                continue;
            }
            draw.box(object.transform.matrix(), (object.boundsMax - object.boundsMin) * 0.5f, kSdfColour);
            // Iso-lines on a horizontal slice: sample a grid and mark near-surface cells.
            const int grid = 48;
            const glm::vec3 size = object.boundsMax - object.boundsMin;
            const float cell = std::max(size.x, size.z) / static_cast<float>(grid);
            for (int z = 0; z < grid && budget > 0; ++z) {
                for (int x = 0; x < grid && budget > 0; ++x) {
                    const glm::vec3 local(object.boundsMin.x + size.x * (static_cast<float>(x) + 0.5f) / grid,
                                          options.sliceHeight,
                                          object.boundsMin.z + size.z * (static_cast<float>(z) + 0.5f) / grid);
                    const float d = object.tree.evaluate(local, time, &scene.fields);
                    if (std::abs(d) > cell) {
                        continue;
                    }
                    const glm::vec3 world = glm::vec3(object.transform.matrix() * glm::vec4(local, 1.0f));
                    draw.point(world, cell, glm::vec4(kSdfColour.r, kSdfColour.g, kSdfColour.b, 0.9f));
                    --budget;
                }
            }
        }
    }
    if (options.wind) {
        buildWindDebug(draw, scene, time);
    }
    if (options.vortex) {
        buildVortexDebug(draw, scene, time);
    }
}

} // namespace avgen::rendering
