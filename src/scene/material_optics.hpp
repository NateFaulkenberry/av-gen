#pragma once

// ADR-1143: a material's thin film and anisotropy -- their scene-file blocks and their parameters.
//
// Both blocks sit in a material wherever one is authored: a procedural node's `material`, an SDF
// object's `material`, a terrain or orb node's `material`. Every key is optional and defaults to the
// struct's value; an absent block is the default, and the default is off:
//
//   "thinFilm":   {"thickness": 60.0, "ior": 2.4}       // nm; 0 = off
//   "anisotropy": {"strength": 0.8, "rotation": 0.0}    // -1..1 (0 = off); radians
//
// The parameters sit under the owner's prefix, `<prefix>material/thinFilm/thickness`,
// `<prefix>material/thinFilm/ior`, `<prefix>material/anisotropy/strength` and
// `<prefix>material/anisotropy/rotation` (beside the owner's `material/roughness`), and they are
// registered whether or not the file authored a block, so either look can be switched on from the
// Parameters panel or driven by a route.

#include "core/error.hpp"
#include "scene/scene_types.hpp"

#include <nlohmann/json_fwd.hpp>

#include <string>
#include <vector>

namespace avgen::params {
class IParameter;
class ParameterSet;
template <typename T>
class Parameter;
} // namespace avgen::params

namespace avgen::scene {

// Read a `"thinFilm"` / `"anisotropy"` object into `out` (keys it does not name keep their value).
// An unknown key, a wrong type or a non-finite number is an error naming the key.
[[nodiscard]] Result<void> readThinFilm(const nlohmann::json& j, ThinFilm& out);
[[nodiscard]] Result<void> readAnisotropy(const nlohmann::json& j, Anisotropy& out);
// Every field, so a written block reads back exactly.
[[nodiscard]] nlohmann::json thinFilmToJson(const ThinFilm& t);
[[nodiscard]] nlohmann::json anisotropyToJson(const Anisotropy& a);
// True when the block is its struct's default field for field: such a material writes no block, so
// every scene that never authored one saves exactly the file it had.
[[nodiscard]] bool thinFilmIsDefault(const ThinFilm& t);
[[nodiscard]] bool anisotropyIsDefault(const Anisotropy& a);

// Reads both optional blocks of a material object `m` (a JSON object), when present. The error names
// the block and the key.
[[nodiscard]] Result<void> readMaterialOptics(const nlohmann::json& m, Material& out);
// Writes both blocks into the material object `m`, each only when it is not the default.
void writeMaterialOptics(const Material& material, nlohmann::json& m);

struct MaterialOpticsParameters {
    params::Parameter<float>* thinFilmThickness = nullptr;
    params::Parameter<float>* thinFilmIor = nullptr;
    params::Parameter<float>* anisotropyStrength = nullptr;
    params::Parameter<float>* anisotropyRotation = nullptr;
};

// Registers `<prefix>material/thinFilm/{thickness,ior}` and `<prefix>material/anisotropy/{strength,
// rotation}` with `rest` as the defaults, in `group`. Each parameter is also appended to `all` when
// it is not null (the owner's unregister list).
[[nodiscard]] MaterialOpticsParameters registerMaterialOpticsParameters(params::ParameterSet& params,
                                                                        const std::string& prefix,
                                                                        const std::string& group,
                                                                        const Material& rest,
                                                                        std::vector<params::IParameter*>* all);
// Copies the parameters' final values into `live`. Null handles leave their field alone.
void applyMaterialOpticsParameters(const MaterialOpticsParameters& p, Material& live);

// ---- the thin-film tint, on the CPU ----------------------------------------------------------------
//
// The same function the lit shader evaluates (`thinFilmTint` in shaders/lighting.wgsl), kept here so
// a test can pin the curve and a GPU test can compare a render against it. Airy reflectance of
// air | film | substrate at 16 wavelengths (380..680 nm), folded to XYZ with the Wyman, Sloan and
// Shirley (2013) fit of the CIE 1931 matching functions, divided by the bare substrate's reflectance
// and taken to linear Rec.709. A film of thickness 0 returns exactly (1, 1, 1).
//
// `cosView` is N.V; `substrateF0` is the luminance of the surface's f0 (its normal-incidence
// reflectance) and `metallic` picks the substrate's phase: a conductor reflects with a phase flip
// (amplitude -sqrt(f0)), a dielectric with the sign of (film ior - substrate ior).
[[nodiscard]] glm::vec3 thinFilmTint(float cosView, float thicknessNm, float filmIor, float substrateF0,
                                     float metallic);

} // namespace avgen::scene
