#include "scene/composition_data.hpp"

#include "spatial/detail.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>

namespace avgen::scene {

namespace {

using json = nlohmann::json;

void hashText(spatial::detail::Fnv& h, const std::string& text) {
    h.u32(static_cast<std::uint32_t>(text.size()));
    for (const char c : text) {
        h.u8(static_cast<std::uint8_t>(c));
    }
}

json vec3ToJson(const glm::vec3& v) {
    return json::array({v.x, v.y, v.z});
}
glm::vec3 vec3FromJson(const json& j, const glm::vec3& fallback) {
    if (!j.is_array() || j.size() != 3 || !j[0].is_number()) {
        return fallback;
    }
    return glm::vec3(j[0].get<float>(), j[1].get<float>(), j[2].get<float>());
}
float readFloat(const json& j, const char* key, float fallback) {
    return j.contains(key) && j[key].is_number() ? j[key].get<float>() : fallback;
}
std::string readString(const json& j, const char* key, const std::string& fallback = {}) {
    return j.contains(key) && j[key].is_string() ? j[key].get<std::string>() : fallback;
}

} // namespace

Result<void> CompositionData::validate() const {
    for (const FocalPoint& f : focalPoints) {
        if (f.name.empty()) {
            return fail("a focal point has no name");
        }
        if (f.radius <= 0.0f) {
            return fail("focal point '{}': radius must be positive", f.name);
        }
        if (f.clearance < 0.0f) {
            return fail("focal point '{}': clearance cannot be negative", f.name);
        }
    }
    float previousEnd = -std::numeric_limits<float>::max();
    for (const DepthLayer& l : layers) {
        if (l.end <= l.start) {
            return fail("depth layer '{}': end must be beyond start", l.name);
        }
        if (l.start < previousEnd - 1e-3f) {
            return fail("depth layer '{}': bands must be ordered near to far", l.name);
        }
        previousEnd = l.end;
    }
    for (const ExclusionRegion& e : exclusions) {
        if (e.shape == ExclusionRegion::Shape::Sphere && e.radius <= 0.0f) {
            return fail("exclusion '{}': radius must be positive", e.name);
        }
        if (e.softness < 0.0f) {
            return fail("exclusion '{}': softness cannot be negative", e.name);
        }
    }
    if (!cameraTarget.empty() && find(cameraTarget) == nullptr) {
        return fail("camera target '{}' is not a focal point", cameraTarget);
    }
    return {};
}

const FocalPoint* CompositionData::find(std::string_view name) const {
    for (const FocalPoint& f : focalPoints) {
        if (f.name == name) {
            return &f;
        }
    }
    return nullptr;
}

void CompositionData::appendFields(spatial::FieldSet& out) const {
    // Clearance: a scalar that falls to zero inside a focal point's clearance radius, so a
    // density filter multiplied by it empties the region the frame is about.
    for (const FocalPoint& f : focalPoints) {
        if (f.clearance <= 0.0f) {
            continue;
        }
        // Radial gives 1 at the centre falling to 0 at `radius`; inverted that is 0 inside the
        // clearance rising to 1 at its edge and staying there, which is what a density multiplier
        // wants. The falloff stays None so the field does not fade out again at a distance.
        spatial::FieldSpec field;
        field.name = "composition.clearance." + f.name;
        field.kind = spatial::FieldKind::Radial;
        field.position = f.position;
        field.radius = f.clearance;
        field.strength = 1.0f;
        field.invert = true;
        field.falloff.kind = spatial::FalloffKind::None;
        out.fields.push_back(std::move(field));
    }
    // Weight: a scalar that peaks at each focal point, for emission and scale effectors.
    for (const FocalPoint& f : focalPoints) {
        spatial::FieldSpec field;
        field.name = "composition.weight." + f.name;
        field.kind = spatial::FieldKind::Radial;
        field.position = f.position;
        field.radius = f.radius;
        field.strength = f.weight;
        field.falloff.kind = spatial::FalloffKind::Smooth;
        field.falloff.inner = 0.0f;
        field.falloff.outer = f.radius;
        out.fields.push_back(std::move(field));
    }
    // Exclusions: 0 inside the volume, 1 outside (or the reverse when inverted).
    for (const ExclusionRegion& e : exclusions) {
        spatial::FieldSpec field;
        field.name = "composition.exclusion." + e.name;
        field.position = e.position;
        field.strength = 1.0f;
        field.invert = !e.invert;
        if (e.shape == ExclusionRegion::Shape::Sphere) {
            field.kind = spatial::FieldKind::Sphere;
            field.radius = e.radius;
            field.softness = std::max(e.softness, 1e-3f);
        } else {
            field.kind = spatial::FieldKind::Box;
            field.size = e.size;
            field.softness = std::max(e.softness, 1e-3f);
        }
        field.falloff.kind = spatial::FalloffKind::None;
        out.fields.push_back(std::move(field));
    }
}

DepthLayer CompositionData::layerAt(float distance) const {
    if (layers.empty()) {
        return DepthLayer{};
    }
    for (const DepthLayer& l : layers) {
        if (distance < l.end) {
            return l;
        }
    }
    return layers.back();
}

std::uint64_t CompositionData::structuralHash() const {
    spatial::detail::Fnv h;
    for (const FocalPoint& f : focalPoints) {
        hashText(h, f.name);
        h.v3(f.position);
        h.f32(f.radius);
        h.f32(f.clearance);
        h.f32(f.weight);
        hashText(h, f.boundObject);
    }
    for (const DepthLayer& l : layers) {
        hashText(h, l.name);
        h.f32(l.start);
        h.f32(l.end);
        h.f32(l.density);
        h.f32(l.contrast);
        h.f32(l.saturation);
        h.f32(l.detail);
    }
    for (const ExclusionRegion& e : exclusions) {
        hashText(h, e.name);
        h.u32(static_cast<std::uint32_t>(e.shape));
        h.v3(e.position);
        h.v3(e.size);
        h.f32(e.radius);
        h.f32(e.softness);
        h.u32(e.invert ? 1u : 0u);
    }
    hashText(h, cameraTarget);
    h.v2(targetScreenPosition);
    h.f32(framingStrength);
    return h.value();
}

nlohmann::json CompositionData::toJson() const {
    json j = json::object();
    if (!focalPoints.empty()) {
        json arr = json::array();
        for (const FocalPoint& f : focalPoints) {
            json e;
            e["name"] = f.name;
            e["position"] = vec3ToJson(f.position);
            e["radius"] = f.radius;
            if (f.clearance > 0.0f) {
                e["clearance"] = f.clearance;
            }
            e["weight"] = f.weight;
            if (!f.boundObject.empty()) {
                e["object"] = f.boundObject;
            }
            arr.push_back(std::move(e));
        }
        j["focalPoints"] = std::move(arr);
    }
    if (!layers.empty()) {
        json arr = json::array();
        for (const DepthLayer& l : layers) {
            arr.push_back(json{{"name", l.name},
                               {"start", l.start},
                               {"end", l.end},
                               {"density", l.density},
                               {"contrast", l.contrast},
                               {"saturation", l.saturation},
                               {"detail", l.detail}});
        }
        j["layers"] = std::move(arr);
    }
    if (!exclusions.empty()) {
        json arr = json::array();
        for (const ExclusionRegion& e : exclusions) {
            json o;
            o["name"] = e.name;
            o["shape"] = e.shape == ExclusionRegion::Shape::Sphere ? "sphere" : "box";
            o["position"] = vec3ToJson(e.position);
            if (e.shape == ExclusionRegion::Shape::Sphere) {
                o["radius"] = e.radius;
            } else {
                o["size"] = vec3ToJson(e.size);
            }
            o["softness"] = e.softness;
            if (e.invert) {
                o["invert"] = true;
            }
            arr.push_back(std::move(o));
        }
        j["exclusions"] = std::move(arr);
    }
    if (!cameraTarget.empty()) {
        j["cameraTarget"] = cameraTarget;
        j["targetScreenPosition"] = json::array({targetScreenPosition.x, targetScreenPosition.y});
        j["framingStrength"] = framingStrength;
    }
    return j;
}

Result<CompositionData> CompositionData::fromJson(const nlohmann::json& j) {
    if (!j.is_object()) {
        return fail("'composition' must be an object");
    }
    CompositionData out;
    if (j.contains("focalPoints")) {
        if (!j["focalPoints"].is_array()) {
            return fail("'focalPoints' must be an array");
        }
        for (const json& e : j["focalPoints"]) {
            if (!e.is_object()) {
                return fail("focal point entries must be objects");
            }
            FocalPoint f;
            f.name = readString(e, "name", "focus");
            f.position = vec3FromJson(e.value("position", json()), f.position);
            f.radius = readFloat(e, "radius", f.radius);
            f.clearance = readFloat(e, "clearance", f.clearance);
            f.weight = readFloat(e, "weight", f.weight);
            f.boundObject = readString(e, "object");
            out.focalPoints.push_back(std::move(f));
        }
    }
    if (j.contains("layers")) {
        if (!j["layers"].is_array()) {
            return fail("'layers' must be an array");
        }
        for (const json& e : j["layers"]) {
            DepthLayer l;
            l.name = readString(e, "name", "layer");
            l.start = readFloat(e, "start", l.start);
            l.end = readFloat(e, "end", l.end);
            l.density = readFloat(e, "density", l.density);
            l.contrast = readFloat(e, "contrast", l.contrast);
            l.saturation = readFloat(e, "saturation", l.saturation);
            l.detail = readFloat(e, "detail", l.detail);
            out.layers.push_back(std::move(l));
        }
    }
    if (j.contains("exclusions")) {
        if (!j["exclusions"].is_array()) {
            return fail("'exclusions' must be an array");
        }
        for (const json& e : j["exclusions"]) {
            ExclusionRegion x;
            x.name = readString(e, "name", "clear");
            const std::string shape = readString(e, "shape", "sphere");
            if (shape == "box") {
                x.shape = ExclusionRegion::Shape::Box;
            } else if (shape != "sphere") {
                return fail("exclusion '{}': unknown shape '{}'", x.name, shape);
            }
            x.position = vec3FromJson(e.value("position", json()), x.position);
            x.size = vec3FromJson(e.value("size", json()), x.size);
            x.radius = readFloat(e, "radius", x.radius);
            x.softness = readFloat(e, "softness", x.softness);
            x.invert = e.contains("invert") && e["invert"].is_boolean() && e["invert"].get<bool>();
            out.exclusions.push_back(std::move(x));
        }
    }
    out.cameraTarget = readString(j, "cameraTarget");
    if (j.contains("targetScreenPosition") && j["targetScreenPosition"].is_array() &&
        j["targetScreenPosition"].size() == 2) {
        out.targetScreenPosition = glm::vec2(j["targetScreenPosition"][0].get<float>(),
                                             j["targetScreenPosition"][1].get<float>());
    }
    out.framingStrength = readFloat(j, "framingStrength", out.framingStrength);
    if (auto ok = out.validate(); !ok) {
        return std::unexpected(ok.error());
    }
    return out;
}

} // namespace avgen::scene
