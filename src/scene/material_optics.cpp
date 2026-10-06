#include "scene/material_optics.hpp"

#include "params/parameter_set.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cmath>

namespace avgen::scene {

namespace {

using json = nlohmann::json;

template <typename S>
struct FloatKey {
    const char* key;
    float S::*field;
    float lo, hi, softLo, softHi;
};

constexpr float kPi = 3.14159265358979f;

// One row per field: its key (the file's and the parameter's last segment), its field and its ranges.
constexpr std::array<FloatKey<ThinFilm>, 2> kThinFilm{{
    {"thickness", &ThinFilm::thickness, 0.0f, 2000.0f, 0.0f, 400.0f},
    {"ior", &ThinFilm::ior, 1.0f, 5.0f, 1.2f, 3.0f},
}};
constexpr std::array<FloatKey<Anisotropy>, 2> kAnisotropy{{
    {"strength", &Anisotropy::strength, -1.0f, 1.0f, -1.0f, 1.0f},
    {"rotation", &Anisotropy::rotation, -2.0f * kPi, 2.0f * kPi, -kPi, kPi},
}};

template <typename S, std::size_t N>
Result<void> readBlock(const json& j, const char* block, const std::array<FloatKey<S>, N>& keys, S& out) {
    if (!j.is_object()) {
        return fail("'{}' must be an object", block);
    }
    for (auto it = j.begin(); it != j.end(); ++it) {
        const std::string& key = it.key();
        bool known = false;
        for (const FloatKey<S>& f : keys) {
            if (key != f.key) {
                continue;
            }
            known = true;
            if (!it->is_number() || !std::isfinite(it->get<double>())) {
                return fail("{} '{}' must be a finite number", block, key);
            }
            out.*(f.field) = it->get<float>();
        }
        if (!known) {
            std::string names;
            for (const FloatKey<S>& f : keys) {
                names += names.empty() ? f.key : std::string(", ") + f.key;
            }
            return fail("{} has no key '{}' ({})", block, key, names);
        }
    }
    return {};
}

template <typename S, std::size_t N>
json blockToJson(const S& s, const std::array<FloatKey<S>, N>& keys) {
    json j = json::object();
    for (const FloatKey<S>& f : keys) {
        j[f.key] = s.*(f.field);
    }
    return j;
}

template <typename S, std::size_t N>
bool blockIsDefault(const S& s, const std::array<FloatKey<S>, N>& keys) {
    const S d{};
    for (const FloatKey<S>& f : keys) {
        if (s.*(f.field) != d.*(f.field)) {
            return false;
        }
    }
    return true;
}

// Wyman, Sloan and Shirley (2013), the multi-lobe fit of the CIE 1931 2-degree matching functions.
// The shader's `thinFilmCmf` is this function, line for line.
glm::vec3 cieXyz(float lambda) {
    const auto g = [](float x, float mu, float lo, float hi) {
        const float t = (x - mu) * (x < mu ? lo : hi);
        return std::exp(-0.5f * t * t);
    };
    const float x = 0.362f * g(lambda, 442.0f, 0.0624f, 0.0374f) + 1.056f * g(lambda, 599.8f, 0.0264f, 0.0323f) -
                    0.065f * g(lambda, 501.1f, 0.0490f, 0.0382f);
    const float y = 0.821f * g(lambda, 568.8f, 0.0213f, 0.0247f) + 0.286f * g(lambda, 530.9f, 0.0613f, 0.0322f);
    const float z = 1.217f * g(lambda, 437.0f, 0.0845f, 0.0278f) + 0.681f * g(lambda, 459.0f, 0.0385f, 0.0725f);
    return {x, y, z};
}

glm::vec3 xyzToLinearRec709(const glm::vec3& c) {
    return {3.2406f * c.x - 1.5372f * c.y - 0.4986f * c.z, -0.9689f * c.x + 1.8758f * c.y + 0.0415f * c.z,
            0.0557f * c.x - 0.2040f * c.y + 1.0570f * c.z};
}

} // namespace

Result<void> readThinFilm(const json& j, ThinFilm& out) { return readBlock(j, "thinFilm", kThinFilm, out); }
Result<void> readAnisotropy(const json& j, Anisotropy& out) {
    return readBlock(j, "anisotropy", kAnisotropy, out);
}
json thinFilmToJson(const ThinFilm& t) { return blockToJson(t, kThinFilm); }
json anisotropyToJson(const Anisotropy& a) { return blockToJson(a, kAnisotropy); }
bool thinFilmIsDefault(const ThinFilm& t) { return blockIsDefault(t, kThinFilm); }
bool anisotropyIsDefault(const Anisotropy& a) { return blockIsDefault(a, kAnisotropy); }

Result<void> readMaterialOptics(const json& m, Material& out) {
    if (m.contains("thinFilm")) {
        if (auto r = readThinFilm(m.at("thinFilm"), out.thinFilm); !r) {
            return r;
        }
    }
    if (m.contains("anisotropy")) {
        if (auto r = readAnisotropy(m.at("anisotropy"), out.anisotropy); !r) {
            return r;
        }
    }
    return {};
}

void writeMaterialOptics(const Material& material, json& m) {
    if (!thinFilmIsDefault(material.thinFilm)) {
        m["thinFilm"] = thinFilmToJson(material.thinFilm);
    }
    if (!anisotropyIsDefault(material.anisotropy)) {
        m["anisotropy"] = anisotropyToJson(material.anisotropy);
    }
}

MaterialOpticsParameters registerMaterialOpticsParameters(params::ParameterSet& params, const std::string& prefix,
                                                          const std::string& group, const Material& rest,
                                                          std::vector<params::IParameter*>* all) {
    const auto add = [&](const std::string& block, const char* key, float value, float lo, float hi, float softLo,
                         float softHi) {
        params::ParamDesc<float> d;
        d.path = prefix + "material/" + block + "/" + key;
        d.label = "material/" + block + "/" + key;
        d.group = group;
        d.defaultValue = value;
        d.hardMin = lo;
        d.hardMax = hi;
        d.softMin = softLo;
        d.softMax = softHi;
        auto& p = params.add(std::move(d));
        if (all != nullptr) {
            all->push_back(&p);
        }
        return &p;
    };
    const auto addThin = [&](const FloatKey<ThinFilm>& f) {
        return add("thinFilm", f.key, rest.thinFilm.*(f.field), f.lo, f.hi, f.softLo, f.softHi);
    };
    const auto addAniso = [&](const FloatKey<Anisotropy>& f) {
        return add("anisotropy", f.key, rest.anisotropy.*(f.field), f.lo, f.hi, f.softLo, f.softHi);
    };
    MaterialOpticsParameters out;
    out.thinFilmThickness = addThin(kThinFilm[0]);
    out.thinFilmIor = addThin(kThinFilm[1]);
    out.anisotropyStrength = addAniso(kAnisotropy[0]);
    out.anisotropyRotation = addAniso(kAnisotropy[1]);
    return out;
}

void applyMaterialOpticsParameters(const MaterialOpticsParameters& p, Material& live) {
    const auto copy = [](const params::Parameter<float>* param, float& target) {
        if (param != nullptr) {
            target = param->value();
        }
    };
    copy(p.thinFilmThickness, live.thinFilm.thickness);
    copy(p.thinFilmIor, live.thinFilm.ior);
    copy(p.anisotropyStrength, live.anisotropy.strength);
    copy(p.anisotropyRotation, live.anisotropy.rotation);
}

glm::vec3 thinFilmTint(float cosView, float thicknessNm, float filmIor, float substrateF0, float metallic) {
    if (!(thicknessNm > 0.0f)) {
        return glm::vec3(1.0f);
    }
    const float n = std::max(filmIor, 1.0f);
    const float cosI = std::clamp(cosView, 0.0f, 1.0f);
    const float sinT2 = (1.0f - cosI * cosI) / (n * n);
    const float cosT = std::sqrt(std::max(1.0f - sinT2, 0.0f));
    const float r1 = (1.0f - n) / (1.0f + n); // air -> film
    // film -> substrate, as one real amplitude. A conductor: -sqrt(f0), the phase flip of a metal. A
    // dielectric: its ior from f0, and the Fresnel amplitude from the film into it.
    const float a = std::sqrt(std::clamp(substrateF0, 1e-4f, 0.98f));
    const float nb = (1.0f + a) / (1.0f - a);
    const float r2 = metallic * (-a) + (1.0f - metallic) * ((n - nb) / (n + nb));
    const float r0 = (r1 + r2) * (r1 + r2) / ((1.0f + r1 * r2) * (1.0f + r1 * r2));
    glm::vec3 acc(0.0f);
    glm::vec3 norm(0.0f);
    for (int k = 0; k < 16; ++k) {
        const float lambda = 380.0f + 20.0f * static_cast<float>(k);
        const float delta = 4.0f * kPi * n * thicknessNm * cosT / lambda;
        const float c = std::cos(delta);
        const float r = (r1 * r1 + r2 * r2 + 2.0f * r1 * r2 * c) / (1.0f + r1 * r1 * r2 * r2 + 2.0f * r1 * r2 * c);
        const glm::vec3 w = cieXyz(lambda);
        acc += w * (r / std::max(r0, 1e-6f));
        norm += w;
    }
    const glm::vec3 white = xyzToLinearRec709(glm::vec3(1.0f));
    return glm::max(xyzToLinearRec709(acc / norm) / glm::max(white, glm::vec3(1e-3f)), glm::vec3(0.0f));
}

} // namespace avgen::scene
