#include "pathtrace/coverage.hpp"

#include <array>

namespace avgen::pathtrace {
namespace {

using D = Disposition;

// `scene::Scene`'s public data members, in declaration order. Unsupported and degraded rows are the ones
// `buildSnapshot` reports by name when the frame contains the feature.
constexpr std::array kScene{
    CoverageRow{"camera", D::Traced, "pinhole at the camera's pose and field of view; the near plane, depth of field "
                                     "and motion blur are not modelled (renderer caveat)"},
    CoverageRow{"cameras", D::NotApplicable, "imported alternatives; `camera` is the one in use"},
    CoverageRow{"lights", D::Traced, "directional, point, spot and area lights"},
    CoverageRow{"environment", D::Traced, "map / analytic sky / background (ADR-2240); its fog and volume terms are "
                                          "reported as volumetrics"},
    CoverageRow{"meshes", D::Traced, ""},
    CoverageRow{"textures", D::Traced, ""},
    CoverageRow{"entities", D::Traced, "skinned entities through CPU skinning"},
    CoverageRow{"particles", D::Unsupported, "GPU-only simulation"},
    CoverageRow{"procedurals", D::Traced, "Embree instances at LOD 0; generators unsupported; deformers and effectors "
                                          "degraded"},
    CoverageRow{"fields", D::Unsupported, "simulated grids are GPU state"},
    CoverageRow{"composition", D::NotApplicable, "what the frame is about; consumed by evaluation"},
    CoverageRow{"splines", D::NotApplicable, "consumed by evaluation (distributions, deformers, camera)"},
    CoverageRow{"sdfs", D::Traced, "Mesh mode traced; Raymarch mode unsupported"},
    CoverageRow{"ecosystem", D::Unsupported, "the first Environment: GPU emitters and splats"},
    CoverageRow{"astral", D::Unsupported, "the second Environment: GPU particles"},
    CoverageRow{"clouds", D::Unsupported, "Cloud Environment v2 is a GPU ray march with no CPU density"},
    CoverageRow{"attractorWorld", D::NotApplicable, "drawn as engine geometry (entities) since ADR-1857/1861"},
    CoverageRow{"meshWire", D::Unsupported, "a blended line pass"},
    CoverageRow{"corruption", D::Unsupported, "a vertex-stage and shading term of the raster"},
    CoverageRow{"portal", D::Unsupported, "another world drawn through an opening"},
    CoverageRow{"portalClip", D::Unsupported, "a clip plane of the raster's camera passes"},
    CoverageRow{"materialPrograms", D::Traced, "evaluated per hit in the lit shader's order (ADR-2246); inputs with no "
                                                 "ray-traced value are reported by name"},
    CoverageRow{"programTextures", D::Unsupported, "a program's texture ops read colour 1 (reported as 'program texture op')"},
    CoverageRow{"programTextureVersion", D::NotApplicable, "bookkeeping"},
    CoverageRow{"waters", D::Degraded, "a water surface is traced as its flat sheet"},
    CoverageRow{"terrainGround", D::NotApplicable, "a height field for the fog's ground following"},
    CoverageRow{"rigs", D::Traced, "posed through CPU skinning"},
    CoverageRow{"detailLimits", D::NotApplicable, "set to the offline limits by the trace job (ADR-2244)"},
    CoverageRow{"post", D::NotApplicable, "the tracer stops at scene-linear radiance; the look is downstream"},
    CoverageRow{"temporal", D::Unsupported, "frame echo and the temporal family are raster post"},
    CoverageRow{"waves", D::Unsupported, "a surface term of the lit pass"},
    CoverageRow{"atmospherics", D::Unsupported, "comets, auroras and placed media"},
    CoverageRow{"distortion", D::Unsupported, "screen-space"},
    CoverageRow{"pulses", D::Unsupported, "Shockwave fronts move vertices and light in the raster's shaders"},
    CoverageRow{"entityFx", D::Unsupported, "Glow, Pulse and Bloom Source lanes; its effect-light pool IS traced, as "
                                         "unshadowed point lights (ADR-2248)"},
    CoverageRow{"ribbons", D::Unsupported, "camera-facing strips"},
    CoverageRow{"stars", D::Unsupported, "the Stars effect's field is drawn by the background pass"},
    CoverageRow{"shells", D::Unsupported, "analytic proxy shells"},
    CoverageRow{"meshLods", D::NotApplicable, "the tracer always uses LOD 0"},
    CoverageRow{"meshVersion", D::NotApplicable, "bookkeeping"},
    CoverageRow{"textureVersion", D::NotApplicable, "bookkeeping"},
    CoverageRow{"identity", D::NotApplicable, "bookkeeping"},
};

// `scene::Material`'s fields, in declaration order.
constexpr std::array kMaterial{
    CoverageRow{"baseColor", D::Traced, ""},
    CoverageRow{"opacity", D::Traced, "cuts an alpha-masked surface (ADR-2251); alpha blend is unsupported and "
                                      "reported"},
    CoverageRow{"emissiveColor", D::Traced, ""},
    CoverageRow{"emissiveIntensity", D::Traced, ""},
    CoverageRow{"roughness", D::Traced, ""},
    CoverageRow{"metallic", D::Traced, ""},
    CoverageRow{"normalScale", D::Traced, "scales the normal map's xy, as the raster does"},
    CoverageRow{"occlusionStrength", D::NotApplicable, "a path tracer computes occlusion"},
    CoverageRow{"alphaMode", D::Traced, "Mask: rays pass through cut texels, shadow rays included (ADR-2251); "
                                        "Blend: unsupported, traced opaque, reported"},
    CoverageRow{"alphaCutoff", D::Traced, "the cutoff, without the raster's distance fade of it"},
    CoverageRow{"doubleSided", D::Traced, "camera rays pass through a one-sided back face as the raster culls it; "
                                        "scattered, shadow and light rays see both faces -- a deliberate difference "
                                        "(ADR-2249)"},
    CoverageRow{"unlit", D::Traced, ""},
    CoverageRow{"surfaceClass", D::NotApplicable, "an AOV manifest label"},
    CoverageRow{"program", D::Traced, "scene::evaluateMaterialProgram at the hit (ADR-2246)"},
    CoverageRow{"baseColorTexture", D::Traced, ""},
    CoverageRow{"metallicRoughnessTexture", D::Traced, ""},
    CoverageRow{"normalTexture", D::Traced, "the raster's cotangent frame from the triangle's uv gradient (ADR-2251)"},
    CoverageRow{"emissiveTexture", D::Traced, "at the hit and at a light sample alike (ADR-2247)"},
    CoverageRow{"occlusionTexture", D::NotApplicable, "a path tracer computes occlusion"},
    CoverageRow{"toon", D::Unsupported, "cel lighting is a raster shading model"},
    CoverageRow{"wire", D::Unsupported, "edge lines are a raster pass"},
    CoverageRow{"thinFilm", D::Degraded, "traced as the bare material"},
    CoverageRow{"anisotropy", D::Degraded, "the GGX lobe is isotropic"},
    CoverageRow{"refraction", D::Unsupported, "no transmission: traced opaque"},
    CoverageRow{"engraving", D::Degraded, "traced as the smooth surface"},
    CoverageRow{"regions", D::Degraded, "per-region temper and polish are not applied"},
};

} // namespace

std::span<const CoverageRow> sceneCoverage() { return kScene; }
std::span<const CoverageRow> materialCoverage() { return kMaterial; }

} // namespace avgen::pathtrace
