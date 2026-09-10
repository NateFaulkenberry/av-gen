#include "world/world_recipe.hpp"

#include <nlohmann/json.hpp>

#include <cmath>
#include <fstream>

namespace avgen::world {
namespace {
using nlohmann::json;

// Every weight in a recipe is a 0..1 proportion, so reading one is the same four lines every time
// and the range check belongs here rather than in twenty places.
Result<void> readWeight(const json& j, const char* key, float& target, const char* where) {
    if (!j.contains(key)) {
        return {};
    }
    const json& v = j.at(key);
    if (!v.is_number()) {
        return fail("{}.{} must be a number", where, key);
    }
    const auto f = v.get<float>();
    if (!std::isfinite(f) || f < 0.0f || f > 1.0f) {
        return fail("{}.{} is {}, which is outside 0..1", where, key, f);
    }
    target = f;
    return {};
}

struct WeightField {
    const char* key;
    float* target;
};

Result<void> readGroup(const json& parent, const char* group, std::initializer_list<WeightField> fields) {
    if (!parent.contains(group)) {
        return {};
    }
    const json& j = parent.at(group);
    if (!j.is_object()) {
        return fail("'{}' must be an object", group);
    }
    for (const auto& f : fields) {
        if (auto r = readWeight(j, f.key, *f.target, group); !r) {
            return r;
        }
    }
    return {};
}
} // namespace

Result<void> WorldRecipe::validate() const {
    if (world.empty()) {
        return fail("a world recipe needs a non-empty 'world' name");
    }
    if (!(extent > 0.0f) || !std::isfinite(extent)) {
        return fail("world '{}': extent must be a finite value > 0", world);
    }
    for (const auto& [name, value] : weights()) {
        if (!std::isfinite(value) || value < 0.0f || value > 1.0f) {
            return fail("world '{}': {} is {}, which is outside 0..1", world, name, value);
        }
    }
    // A world with nothing in it is almost always a mistyped recipe rather than an intention, and
    // it composes to an empty scene that looks like a rendering failure.
    const float life = ecology.flora + ecology.fungi + ecology.rock + ecology.crystal +
                       ecology.creature + ecology.structure;
    if (life <= 0.0f) {
        return fail("world '{}': every ecology weight is zero, so nothing would be placed", world);
    }
    if (!(art.contrast > 0.0f) || !std::isfinite(art.contrast)) {
        return fail("world '{}': art.contrast must be a finite value > 0", world);
    }
    if (!(art.saturation >= 0.0f) || !std::isfinite(art.saturation)) {
        return fail("world '{}': art.saturation must be a finite value >= 0", world);
    }
    return {};
}

std::vector<std::pair<std::string, float>> WorldRecipe::weights() const {
    return {
        {"composition.foreground", composition.foreground},
        {"composition.midground", composition.midground},
        {"composition.background", composition.background},
        {"composition.negativeSpace", composition.negativeSpace},
        {"composition.focalStrength", composition.focalStrength},
        {"ecology.flora", ecology.flora},
        {"ecology.fungi", ecology.fungi},
        {"ecology.rock", ecology.rock},
        {"ecology.crystal", ecology.crystal},
        {"ecology.creature", ecology.creature},
        {"ecology.structure", ecology.structure},
        {"atmosphere.fog", atmosphere.fog},
        {"atmosphere.spores", atmosphere.spores},
        {"atmosphere.floating", atmosphere.floating},
        {"atmosphere.depthHaze", atmosphere.depthHaze},
        {"lighting.key", lighting.key},
        {"lighting.bioluminescence", lighting.bioluminescence},
        {"lighting.volumetric", lighting.volumetric},
        {"lighting.bounce", lighting.bounce},
        {"art.organicMotion", art.organicMotion},
        {"art.chaos", art.chaos},
    };
}

Result<WorldRecipe> WorldRecipe::fromJson(const json& j) {
    if (!j.is_object()) {
        return fail("a world recipe must be a JSON object");
    }
    WorldRecipe r;
    if (j.contains("world") && j.at("world").is_string()) {
        r.world = j.at("world").get<std::string>();
    }
    if (j.contains("description") && j.at("description").is_string()) {
        r.description = j.at("description").get<std::string>();
    }
    if (j.contains("seed") && j.at("seed").is_number_unsigned()) {
        r.seed = j.at("seed").get<std::uint32_t>();
    }
    if (j.contains("extent") && j.at("extent").is_number()) {
        r.extent = j.at("extent").get<float>();
    }
    if (j.contains("assetLibrary") && j.at("assetLibrary").is_string()) {
        r.assetLibrary = j.at("assetLibrary").get<std::string>();
    }

    if (auto e = readGroup(j, "composition",
                           {{"foreground", &r.composition.foreground},
                            {"midground", &r.composition.midground},
                            {"background", &r.composition.background},
                            {"negative_space", &r.composition.negativeSpace},
                            {"negativeSpace", &r.composition.negativeSpace},
                            {"focalStrength", &r.composition.focalStrength}});
        !e) {
        return std::unexpected(e.error());
    }
    if (auto e = readGroup(j, "ecology",
                           {{"flora", &r.ecology.flora},
                            {"fungi", &r.ecology.fungi},
                            {"rock", &r.ecology.rock},
                            {"rocks", &r.ecology.rock},
                            {"crystal", &r.ecology.crystal},
                            {"crystals", &r.ecology.crystal},
                            {"creature", &r.ecology.creature},
                            {"creatures", &r.ecology.creature},
                            {"structure", &r.ecology.structure}});
        !e) {
        return std::unexpected(e.error());
    }
    if (auto e = readGroup(j, "atmosphere",
                           {{"fog", &r.atmosphere.fog},
                            {"spores", &r.atmosphere.spores},
                            {"floating", &r.atmosphere.floating},
                            {"floating_elements", &r.atmosphere.floating},
                            {"depthHaze", &r.atmosphere.depthHaze}});
        !e) {
        return std::unexpected(e.error());
    }
    if (auto e = readGroup(j, "lighting",
                           {{"key", &r.lighting.key},
                            {"moon", &r.lighting.key},
                            {"bioluminescence", &r.lighting.bioluminescence},
                            {"volumetric", &r.lighting.volumetric},
                            {"bounce", &r.lighting.bounce}});
        !e) {
        return std::unexpected(e.error());
    }
    if (j.contains("art")) {
        const json& a = j.at("art");
        if (!a.is_object()) {
            return fail("'art' must be an object");
        }
        if (a.contains("name") && a.at("name").is_string()) {
            r.art.name = a.at("name").get<std::string>();
        }
        if (a.contains("palette")) {
            if (!a.at("palette").is_array()) {
                return fail("art.palette must be an array of colour names");
            }
            for (const auto& c : a.at("palette")) {
                if (!c.is_string()) {
                    return fail("art.palette entries must be strings");
                }
                r.art.palette.push_back(c.get<std::string>());
            }
        }
        if (a.contains("contrast") && a.at("contrast").is_number()) {
            r.art.contrast = a.at("contrast").get<float>();
        }
        if (a.contains("saturation") && a.at("saturation").is_number()) {
            r.art.saturation = a.at("saturation").get<float>();
        }
        if (auto e = readWeight(a, "organicMotion", r.art.organicMotion, "art"); !e) {
            return std::unexpected(e.error());
        }
        if (auto e = readWeight(a, "chaos", r.art.chaos, "art"); !e) {
            return std::unexpected(e.error());
        }
    }
    if (auto ok = r.validate(); !ok) {
        return std::unexpected(ok.error());
    }
    return r;
}

Result<WorldRecipe> WorldRecipe::loadFile(const std::filesystem::path& path) {
    std::ifstream in(path);
    if (!in) {
        return fail("world recipe '{}': cannot open", path.string());
    }
    json doc;
    try {
        in >> doc;
    } catch (const json::exception& e) {
        return fail("world recipe '{}': {}", path.string(), e.what());
    }
    auto r = fromJson(doc);
    if (!r) {
        return fail("world recipe '{}': {}", path.string(), r.error().message);
    }
    // A relative library is relative to the recipe, so a recipe and its manifest travel together.
    if (!r->assetLibrary.empty() && r->assetLibrary.is_relative()) {
        r->assetLibrary = (path.parent_path() / r->assetLibrary).lexically_normal();
    }
    return r;
}

json WorldRecipe::toJson() const {
    json j = json::object();
    j["world"] = world;
    if (!description.empty()) {
        j["description"] = description;
    }
    j["seed"] = seed;
    j["extent"] = extent;
    if (!assetLibrary.empty()) {
        j["assetLibrary"] = assetLibrary.generic_string();
    }
    j["composition"] = json{{"foreground", composition.foreground},
                            {"midground", composition.midground},
                            {"background", composition.background},
                            {"negativeSpace", composition.negativeSpace},
                            {"focalStrength", composition.focalStrength}};
    j["ecology"] = json{{"flora", ecology.flora},   {"fungi", ecology.fungi},
                        {"rock", ecology.rock},     {"crystal", ecology.crystal},
                        {"creature", ecology.creature}, {"structure", ecology.structure}};
    j["atmosphere"] = json{{"fog", atmosphere.fog},
                           {"spores", atmosphere.spores},
                           {"floating", atmosphere.floating},
                           {"depthHaze", atmosphere.depthHaze}};
    j["lighting"] = json{{"key", lighting.key},
                         {"bioluminescence", lighting.bioluminescence},
                         {"volumetric", lighting.volumetric},
                         {"bounce", lighting.bounce}};
    json a = json::object();
    if (!art.name.empty()) {
        a["name"] = art.name;
    }
    if (!art.palette.empty()) {
        a["palette"] = art.palette;
    }
    a["contrast"] = art.contrast;
    a["saturation"] = art.saturation;
    a["organicMotion"] = art.organicMotion;
    a["chaos"] = art.chaos;
    j["art"] = std::move(a);
    return j;
}

} // namespace avgen::world
