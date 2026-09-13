#include "rendering/renderer_snapshot.hpp"

#include <fmt/format.h>
#include <nlohmann/json.hpp>

#include <cmath>
#include <fstream>
#include <unordered_map>

namespace avgen::rendering {
namespace {

using nlohmann::json;

json vec3(const glm::vec3& v) { return json::array({v.x, v.y, v.z}); }

json mat4(const glm::mat4& m) {
    json out = json::array();
    for (int c = 0; c < 4; ++c) {
        for (int r = 0; r < 4; ++r) {
            out.push_back(m[c][r]);
        }
    }
    return out;
}

Result<glm::vec3> readVec3(const json& j, const char* key) {
    if (!j.contains(key) || !j.at(key).is_array() || j.at(key).size() != 3) {
        return fail("'{}' must be three numbers", key);
    }
    return glm::vec3(j.at(key)[0].get<float>(), j.at(key)[1].get<float>(), j.at(key)[2].get<float>());
}

Result<glm::mat4> readMat4(const json& j, const char* key) {
    if (!j.contains(key) || !j.at(key).is_array() || j.at(key).size() != 16) {
        return fail("'{}' must be sixteen numbers", key);
    }
    glm::mat4 m(1.0f);
    for (int c = 0; c < 4; ++c) {
        for (int r = 0; r < 4; ++r) {
            m[c][r] = j.at(key)[static_cast<std::size_t>(c * 4 + r)].get<float>();
        }
    }
    return m;
}

bool near(float a, float b, float epsilon) { return std::fabs(a - b) <= epsilon; }

bool near(const glm::vec3& a, const glm::vec3& b, float epsilon) {
    return near(a.x, b.x, epsilon) && near(a.y, b.y, epsilon) && near(a.z, b.z, epsilon);
}

bool near(const glm::mat4& a, const glm::mat4& b, float epsilon) {
    for (int c = 0; c < 4; ++c) {
        for (int r = 0; r < 4; ++r) {
            if (!near(a[c][r], b[c][r], epsilon)) {
                return false;
            }
        }
    }
    return true;
}

} // namespace

json snapshotToJson(const FrameSnapshot& s) {
    json objects = json::array();
    for (const RenderObjectDiagnostic& o : s.frame.objects) {
        json margins = json::array();
        for (const float m : o.frustumMargins) {
            margins.push_back(m);
        }
        objects.push_back(json{{"name", o.name},
                               {"entityIndex", o.entityIndex},
                               {"objectSlot", o.objectSlot},
                               {"mesh", o.mesh},
                               {"materialHash", o.materialHash},
                               {"worldPosition", vec3(o.worldPosition)},
                               {"worldMatrix", mat4(o.worldMatrix)},
                               {"boundsMin", vec3(o.worldBoundsMin)},
                               {"boundsMax", vec3(o.worldBoundsMax)},
                               {"frustumMargins", std::move(margins)},
                               {"rigIndex", o.rigIndex},
                               {"jointCount", o.jointCount},
                               {"paletteVersion", o.paletteVersion},
                               {"paletteTime", o.paletteTime},
                               {"cullReason", o.cullReason},
                               {"visible", o.visible},
                               {"cameraCulled", o.cameraCulled},
                               {"submitted", o.submitted},
                               {"finite", o.finite}});
    }
    return json{{"format", "avgen-frame-snapshot"},
                {"version", 1},
                {"scene", s.scene},
                {"note", s.note},
                {"toggles",
                 json{{"shadows", s.toggles.shadows},
                      {"ao", s.toggles.ao},
                      {"volume", s.toggles.volume},
                      {"post", s.toggles.post},
                      {"shadowMask", s.toggles.shadowMask},
                      {"culling", s.toggles.culling},
                      {"water", s.toggles.water},
                      {"transparency", s.toggles.transparency},
                      {"particles", s.toggles.particles},
                      {"animation", s.toggles.animation},
                      {"cameraMotion", s.toggles.cameraMotion}}},
                {"frameIndex", s.frame.frameIndex},
                {"stateHash", s.frame.stateHash},
                {"cameraPosition", vec3(s.frame.cameraPosition)},
                {"view", mat4(s.frame.view)},
                {"projection", mat4(s.frame.projection)},
                {"objects", std::move(objects)}};
}

Result<FrameSnapshot> snapshotFromJson(const json& doc) {
    if (!doc.is_object() || doc.value("format", std::string()) != "avgen-frame-snapshot") {
        return fail("not an avgen frame snapshot");
    }
    FrameSnapshot s;
    s.scene = doc.value("scene", std::string());
    s.note = doc.value("note", std::string());
    if (doc.contains("toggles") && doc.at("toggles").is_object()) {
        const json& t = doc.at("toggles");
        const auto flag = [&](const char* key, bool& out) { out = t.value(key, out); };
        flag("shadows", s.toggles.shadows);
        flag("ao", s.toggles.ao);
        flag("volume", s.toggles.volume);
        flag("post", s.toggles.post);
        flag("shadowMask", s.toggles.shadowMask);
        flag("culling", s.toggles.culling);
        flag("water", s.toggles.water);
        flag("transparency", s.toggles.transparency);
        flag("particles", s.toggles.particles);
        flag("animation", s.toggles.animation);
        flag("cameraMotion", s.toggles.cameraMotion);
    }
    s.frame.frameIndex = doc.value("frameIndex", std::uint64_t{0});
    s.frame.stateHash = doc.value("stateHash", std::uint64_t{0});
    auto camera = readVec3(doc, "cameraPosition");
    if (!camera) {
        return std::unexpected(camera.error());
    }
    s.frame.cameraPosition = *camera;
    auto view = readMat4(doc, "view");
    if (!view) {
        return std::unexpected(view.error());
    }
    s.frame.view = *view;
    auto projection = readMat4(doc, "projection");
    if (!projection) {
        return std::unexpected(projection.error());
    }
    s.frame.projection = *projection;
    s.frame.viewProjection = s.frame.projection * s.frame.view;
    if (!doc.contains("objects") || !doc.at("objects").is_array()) {
        return fail("a frame snapshot needs an 'objects' array");
    }
    for (const json& o : doc.at("objects")) {
        RenderObjectDiagnostic d;
        d.name = o.value("name", std::string());
        d.entityIndex = o.value("entityIndex", std::size_t{0});
        d.objectSlot = o.value("objectSlot", d.objectSlot);
        d.mesh = o.value("mesh", d.mesh);
        d.materialHash = o.value("materialHash", std::uint64_t{0});
        auto position = readVec3(o, "worldPosition");
        auto matrix = readMat4(o, "worldMatrix");
        auto lo = readVec3(o, "boundsMin");
        auto hi = readVec3(o, "boundsMax");
        if (!position || !matrix || !lo || !hi) {
            return fail("object '{}': incomplete state", d.name);
        }
        d.worldPosition = *position;
        d.worldMatrix = *matrix;
        d.worldBoundsMin = *lo;
        d.worldBoundsMax = *hi;
        if (o.contains("frustumMargins") && o.at("frustumMargins").is_array()) {
            for (std::size_t i = 0; i < d.frustumMargins.size() && i < o.at("frustumMargins").size(); ++i) {
                d.frustumMargins[i] = o.at("frustumMargins")[i].get<float>();
            }
        }
        d.rigIndex = o.value("rigIndex", d.rigIndex);
        d.jointCount = o.value("jointCount", 0u);
        d.paletteVersion = o.value("paletteVersion", std::uint64_t{0});
        d.paletteTime = o.value("paletteTime", -1.0);
        d.cullReason = o.value("cullReason", std::string());
        d.visible = o.value("visible", false);
        d.cameraCulled = o.value("cameraCulled", false);
        d.submitted = o.value("submitted", false);
        d.finite = o.value("finite", true);
        s.frame.objects.push_back(std::move(d));
    }
    return s;
}

Result<void> writeSnapshot(const FrameSnapshot& snapshot, const std::filesystem::path& path) {
    std::ofstream out(path);
    if (!out) {
        return fail("cannot write '{}'", path.string());
    }
    out << snapshotToJson(snapshot).dump(1);
    if (!out) {
        return fail("failed while writing '{}'", path.string());
    }
    return {};
}

Result<FrameSnapshot> readSnapshot(const std::filesystem::path& path) {
    std::ifstream in(path);
    if (!in) {
        return fail("cannot open '{}'", path.string());
    }
    json doc = json::parse(in, nullptr, false);
    if (doc.is_discarded()) {
        return fail("'{}' is not valid JSON", path.string());
    }
    return snapshotFromJson(doc);
}

std::vector<std::string> compareSnapshots(const FrameSnapshot& expected, const FrameSnapshot& actual,
                                          float epsilon, bool counters) {
    std::vector<std::string> out;
    // The arms first. A comparison across different arms is not a comparison, and saying so once is
    // worth more than reporting the thousand pixels it explains.
    const auto arm = [&](const char* name, bool a, bool b) {
        if (a != b) {
            out.push_back(fmt::format("isolation differs: {} was {} and is now {}", name,
                                      a ? "on" : "off", b ? "on" : "off"));
        }
    };
    arm("shadows", expected.toggles.shadows, actual.toggles.shadows);
    arm("ao", expected.toggles.ao, actual.toggles.ao);
    arm("volumetrics", expected.toggles.volume, actual.toggles.volume);
    arm("post", expected.toggles.post, actual.toggles.post);
    arm("shadow mask", expected.toggles.shadowMask, actual.toggles.shadowMask);
    arm("culling", expected.toggles.culling, actual.toggles.culling);
    arm("water", expected.toggles.water, actual.toggles.water);
    arm("transparency", expected.toggles.transparency, actual.toggles.transparency);
    arm("particles", expected.toggles.particles, actual.toggles.particles);
    arm("animation", expected.toggles.animation, actual.toggles.animation);
    arm("camera motion", expected.toggles.cameraMotion, actual.toggles.cameraMotion);

    if (!near(expected.frame.cameraPosition, actual.frame.cameraPosition, epsilon)) {
        out.push_back(fmt::format("camera moved: ({:.4f}, {:.4f}, {:.4f}) -> ({:.4f}, {:.4f}, {:.4f})",
                                  expected.frame.cameraPosition.x, expected.frame.cameraPosition.y,
                                  expected.frame.cameraPosition.z, actual.frame.cameraPosition.x,
                                  actual.frame.cameraPosition.y, actual.frame.cameraPosition.z));
    }
    if (!near(expected.frame.view, actual.frame.view, epsilon)) {
        out.push_back("the view matrix differs");
    }
    if (!near(expected.frame.projection, actual.frame.projection, epsilon)) {
        out.push_back("the projection differs");
    }

    // By name, so an added or removed object is reported as itself rather than as every object
    // after it having changed.
    std::unordered_map<std::string, const RenderObjectDiagnostic*> before;
    for (const RenderObjectDiagnostic& o : expected.frame.objects) {
        before.emplace(o.name, &o);
    }
    for (const RenderObjectDiagnostic& now : actual.frame.objects) {
        const auto it = before.find(now.name);
        if (it == before.end()) {
            out.push_back(fmt::format("'{}' is new", now.name));
            continue;
        }
        const RenderObjectDiagnostic& was = *it->second;
        if (!near(was.worldMatrix, now.worldMatrix, epsilon)) {
            out.push_back(fmt::format(
                "'{}' moved: ({:.4f}, {:.4f}, {:.4f}) -> ({:.4f}, {:.4f}, {:.4f})", now.name,
                was.worldPosition.x, was.worldPosition.y, was.worldPosition.z, now.worldPosition.x,
                now.worldPosition.y, now.worldPosition.z));
        }
        if (was.visible != now.visible) {
            out.push_back(fmt::format("'{}' visibility: {} -> {}", now.name, was.visible, now.visible));
        }
        if (was.cameraCulled != now.cameraCulled) {
            out.push_back(fmt::format("'{}' culling: {} -> {} ({})", now.name, was.cameraCulled,
                                      now.cameraCulled, now.cullReason));
        }
        if (was.submitted != now.submitted) {
            out.push_back(fmt::format("'{}' submission: {} -> {}", now.name, was.submitted, now.submitted));
        }
        if (was.mesh != now.mesh) {
            out.push_back(fmt::format("'{}' is drawing mesh {} instead of {}", now.name, now.mesh,
                                      was.mesh));
        }
        if (was.materialHash != now.materialHash) {
            // A wrong picture with every matrix identical looks like a renderer fault until this
            // line appears and says it is a different surface.
            out.push_back(fmt::format("'{}' has a different material", now.name));
        }
        if (was.objectSlot != now.objectSlot) {
            out.push_back(fmt::format("'{}' took GPU slot {} instead of {}", now.name, now.objectSlot,
                                      was.objectSlot));
        }
        if (was.jointCount != now.jointCount) {
            out.push_back(fmt::format("'{}' joint count: {} -> {}", now.name, was.jointCount,
                                      now.jointCount));
        }
        if (counters && was.paletteVersion != now.paletteVersion) {
            // Not a difference in the pose: the version is a monotonic counter, and two arrivals at
            // the same second legitimately differ. Reported because it explains a pixel difference,
            // and flagged as what it is so nobody chases it.
            out.push_back(fmt::format("'{}' palette version {} -> {} (a counter, not a pose)",
                                      now.name, was.paletteVersion, now.paletteVersion));
        }
        if (!now.finite) {
            out.push_back(fmt::format("'{}' has non-finite state", now.name));
        }
        before.erase(it);
    }
    for (const auto& [name, was] : before) {
        out.push_back(fmt::format("'{}' is gone", name));
    }
    return out;
}

} // namespace avgen::rendering
