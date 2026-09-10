#include "assets/asset_library.hpp"

#include "core/log.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <fstream>

namespace avgen::assets {
namespace {
using nlohmann::json;

constexpr std::array<std::pair<AssetCategory, const char*>, 14> kCategoryNames{{
    {AssetCategory::Flora, "flora"},
    {AssetCategory::Fungi, "fungi"},
    {AssetCategory::Rock, "rock"},
    {AssetCategory::Crystal, "crystal"},
    {AssetCategory::Creature, "creature"},
    {AssetCategory::Structure, "structure"},
    {AssetCategory::Particle, "particle"},
    {AssetCategory::Atmosphere, "atmosphere"},
    {AssetCategory::Terrain, "terrain"},
    {AssetCategory::Water, "water"},
    {AssetCategory::Floating, "floating"},
    {AssetCategory::Architectural, "architectural"},
    {AssetCategory::Organic, "organic"},
    {AssetCategory::Unknown, "unknown"},
}};

// The category names the existing manifest already uses for its groups. They predate this file and
// there is no reason to make somebody rename them: "midgroundPlants" is flora that happens to carry
// a hint about where it belongs, so it becomes flora plus a `midground` tag.
struct LegacyGroup {
    const char* group;
    AssetCategory category;
    const char* tag; // "" for none
};
constexpr std::array<LegacyGroup, 5> kLegacyGroups{{
    {"largeStructures", AssetCategory::Flora, "background"},
    {"midgroundPlants", AssetCategory::Flora, "midground"},
    {"smallDetail", AssetCategory::Flora, "foreground"},
    {"rocks", AssetCategory::Rock, ""},
    {"fungi", AssetCategory::Fungi, ""},
}};

std::optional<float> readFloat(const json& j, const char* key) {
    if (j.contains(key) && j.at(key).is_number()) {
        return j.at(key).get<float>();
    }
    return std::nullopt;
}

void assignFloat(const json& j, const char* key, float& target) {
    if (auto v = readFloat(j, key)) {
        target = *v;
    }
}

std::optional<glm::vec3> readVec3(const json& j, const char* key) {
    if (!j.contains(key)) {
        return std::nullopt;
    }
    const json& a = j.at(key);
    if (!a.is_array() || a.size() != 3 || !a[0].is_number()) {
        return std::nullopt;
    }
    return glm::vec3(a[0].get<float>(), a[1].get<float>(), a[2].get<float>());
}

json vec3ToJson(const glm::vec3& v) { return json::array({v.x, v.y, v.z}); }

MaterialProfile materialFromJson(const json& j) {
    MaterialProfile m;
    if (!j.is_object()) {
        return m;
    }
    assignFloat(j, "emissive", m.emissive);
    assignFloat(j, "translucency", m.translucency);
    assignFloat(j, "roughness", m.roughness);
    assignFloat(j, "metallic", m.metallic);
    if (auto t = readVec3(j, "tint")) {
        m.tint = *t;
    }
    return m;
}

AudioResponseProfile audioFromJson(const json& j) {
    AudioResponseProfile a;
    if (!j.is_object()) {
        return a;
    }
    assignFloat(j, "energy", a.energy);
    assignFloat(j, "impact", a.impact);
    assignFloat(j, "sway", a.sway);
    assignFloat(j, "bloom", a.bloom);
    return a;
}

VariationProfile variationFromJson(const json& j) {
    VariationProfile v;
    if (!j.is_object()) {
        return v;
    }
    assignFloat(j, "scale", v.scale);
    assignFloat(j, "hue", v.hue);
    assignFloat(j, "emissive", v.emissive);
    assignFloat(j, "yaw", v.yaw);
    assignFloat(j, "lean", v.lean);
    return v;
}

Result<AssetDescriptor> assetFromJson(const json& j, AssetCategory fallbackCategory,
                                      const char* fallbackTag) {
    if (!j.is_object()) {
        return fail("asset entry must be a JSON object");
    }
    if (!j.contains("name") || !j.at("name").is_string()) {
        return fail("asset entry needs a string 'name'");
    }
    AssetDescriptor a;
    a.name = j.at("name").get<std::string>();
    a.id = j.contains("id") && j.at("id").is_string() ? j.at("id").get<std::string>() : a.name;
    if (j.contains("file") && j.at("file").is_string()) {
        a.file = j.at("file").get<std::string>();
    }
    a.category = fallbackCategory;
    if (j.contains("category") && j.at("category").is_string()) {
        const auto name = j.at("category").get<std::string>();
        if (auto c = assetCategoryFromName(name)) {
            a.category = *c;
        } else {
            return fail("asset '{}': unknown category '{}'", a.name, name);
        }
    }
    if (j.contains("archetype") && j.at("archetype").is_string()) {
        a.archetype = j.at("archetype").get<std::string>();
    }
    if (fallbackTag != nullptr && *fallbackTag != '\0') {
        a.tags.emplace_back(fallbackTag);
    }
    if (j.contains("tags")) {
        if (!j.at("tags").is_array()) {
            return fail("asset '{}': 'tags' must be an array of strings", a.name);
        }
        for (const auto& t : j.at("tags")) {
            if (!t.is_string()) {
                return fail("asset '{}': every tag must be a string", a.name);
            }
            auto tag = t.get<std::string>();
            if (std::find(a.tags.begin(), a.tags.end(), tag) == a.tags.end()) {
                a.tags.push_back(std::move(tag));
            }
        }
    }
    if (auto size = readVec3(j, "naturalSize")) {
        a.naturalSize = *size;
    }
    if (j.contains("triangles") && j.at("triangles").is_number_integer()) {
        a.triangles = j.at("triangles").get<int>();
    }
    assignFloat(j, "visualImportance", a.visualImportance);
    assignFloat(j, "preferredScale", a.preferredScale);
    assignFloat(j, "preferredDensity", a.preferredDensity);
    if (j.contains("material")) {
        a.material = materialFromJson(j.at("material"));
    }
    if (j.contains("audioResponse")) {
        a.audioResponse = audioFromJson(j.at("audioResponse"));
    }
    if (j.contains("variation")) {
        a.variation = variationFromJson(j.at("variation"));
    }
    if (!(a.visualImportance >= 0.0f) || a.visualImportance > 1.0f) {
        return fail("asset '{}': visualImportance {} is outside 0..1", a.name, a.visualImportance);
    }
    if (a.preferredScale < 0.0f || !std::isfinite(a.preferredScale)) {
        return fail("asset '{}': preferredScale must be a finite value >= 0", a.name);
    }
    if (a.preferredDensity < 0.0f || !std::isfinite(a.preferredDensity)) {
        return fail("asset '{}': preferredDensity must be a finite value >= 0", a.name);
    }
    return a;
}
} // namespace

const char* assetCategoryName(AssetCategory c) {
    for (const auto& [kind, name] : kCategoryNames) {
        if (kind == c) {
            return name;
        }
    }
    return "unknown";
}

std::optional<AssetCategory> assetCategoryFromName(std::string_view name) {
    for (const auto& [kind, text] : kCategoryNames) {
        if (name == text) {
            return kind;
        }
    }
    return std::nullopt;
}

std::vector<AssetCategory> allAssetCategories() {
    std::vector<AssetCategory> out;
    out.reserve(kCategoryNames.size());
    for (const auto& [kind, name] : kCategoryNames) {
        out.push_back(kind);
    }
    return out;
}

bool AssetDescriptor::hasTag(std::string_view tag) const {
    return std::find(tags.begin(), tags.end(), tag) != tags.end();
}

float AssetDescriptor::effectiveHeight() const {
    return preferredScale > 0.0f ? preferredScale : naturalSize.y;
}

Result<AssetLibrary> AssetLibrary::fromJson(const json& j, const std::filesystem::path& baseDirectory) {
    if (!j.is_object()) {
        return fail("asset manifest must be a JSON object");
    }
    AssetLibrary lib;
    lib.baseDirectory_ = baseDirectory;
    if (j.contains("source") && j.at("source").is_string()) {
        lib.source_ = j.at("source").get<std::string>();
    }
    if (j.contains("license") && j.at("license").is_string()) {
        lib.license_ = j.at("license").get<std::string>();
    }

    // Two shapes are accepted. `assets` is a flat array and is what a manifest written for this
    // system looks like. `categories` is the grouped object the repository's manifest already uses,
    // and it keeps working: the group supplies the category and a placement tag, and any entry may
    // still name its own.
    const auto addOne = [&lib](const json& entry, AssetCategory cat, const char* tag) -> Result<void> {
        auto asset = assetFromJson(entry, cat, tag);
        if (!asset) {
            return std::unexpected(asset.error());
        }
        if (lib.find(asset->id) != nullptr) {
            return fail("asset id '{}' appears twice in the manifest", asset->id);
        }
        lib.assets_.push_back(std::move(*asset));
        return {};
    };

    if (j.contains("assets")) {
        if (!j.at("assets").is_array()) {
            return fail("'assets' must be an array");
        }
        for (const auto& entry : j.at("assets")) {
            if (auto r = addOne(entry, AssetCategory::Unknown, ""); !r) {
                return std::unexpected(r.error());
            }
        }
    }
    if (j.contains("categories")) {
        if (!j.at("categories").is_object()) {
            return fail("'categories' must be an object of arrays");
        }
        for (const auto& [group, entries] : j.at("categories").items()) {
            if (!entries.is_array()) {
                return fail("category '{}' must be an array", group);
            }
            AssetCategory cat = AssetCategory::Unknown;
            const char* tag = "";
            for (const auto& legacy : kLegacyGroups) {
                if (group == legacy.group) {
                    cat = legacy.category;
                    tag = legacy.tag;
                    break;
                }
            }
            // A group named after a category is that category; that is how a new manifest is
            // expected to be written, and it costs nothing to accept.
            if (cat == AssetCategory::Unknown) {
                if (auto c = assetCategoryFromName(group)) {
                    cat = *c;
                }
            }
            for (const auto& entry : entries) {
                if (auto r = addOne(entry, cat, tag); !r) {
                    return std::unexpected(r.error());
                }
            }
        }
    }
    if (lib.assets_.empty()) {
        return fail("asset manifest lists no assets");
    }
    return lib;
}

Result<AssetLibrary> AssetLibrary::loadFile(const std::filesystem::path& manifest) {
    std::ifstream in(manifest);
    if (!in) {
        return fail("asset manifest '{}': cannot open", manifest.string());
    }
    json doc;
    try {
        in >> doc;
    } catch (const json::exception& e) {
        return fail("asset manifest '{}': {}", manifest.string(), e.what());
    }
    auto lib = fromJson(doc, manifest.parent_path());
    if (!lib) {
        return fail("asset manifest '{}': {}", manifest.string(), lib.error().message);
    }
    return lib;
}

const AssetDescriptor* AssetLibrary::find(std::string_view id) const {
    for (const auto& a : assets_) {
        if (a.id == id) {
            return &a;
        }
    }
    return nullptr;
}

std::vector<const AssetDescriptor*> AssetLibrary::select(const AssetQuery& query) const {
    std::vector<const AssetDescriptor*> out;
    for (const auto& a : assets_) {
        if (query.category && a.category != *query.category) {
            continue;
        }
        if (!query.anyTags.empty()) {
            const bool any = std::any_of(query.anyTags.begin(), query.anyTags.end(),
                                         [&a](const std::string& t) { return a.hasTag(t); });
            if (!any) {
                continue;
            }
        }
        const bool all = std::all_of(query.allTags.begin(), query.allTags.end(),
                                     [&a](const std::string& t) { return a.hasTag(t); });
        if (!all) {
            continue;
        }
        if (query.minImportance && a.visualImportance < *query.minImportance) {
            continue;
        }
        if (query.maxImportance && a.visualImportance > *query.maxImportance) {
            continue;
        }
        const float h = a.effectiveHeight();
        if (query.minHeight && h < *query.minHeight) {
            continue;
        }
        if (query.maxHeight && h > *query.maxHeight) {
            continue;
        }
        out.push_back(&a);
    }
    return out;
}

std::filesystem::path AssetLibrary::resolve(const AssetDescriptor& asset) const {
    if (asset.file.empty()) {
        return {};
    }
    if (asset.file.is_absolute()) {
        return asset.file.lexically_normal();
    }
    return (baseDirectory_ / asset.file).lexically_normal();
}

json AssetLibrary::toJson() const {
    // Written flat, whatever shape it was read in: a round trip is for machines, and the grouped
    // form exists only so hand-written manifests stay readable.
    json out = json::object();
    if (!source_.empty()) {
        out["source"] = source_;
    }
    if (!license_.empty()) {
        out["license"] = license_;
    }
    json arr = json::array();
    const AssetDescriptor def;
    for (const auto& a : assets_) {
        json e = json::object();
        e["name"] = a.name;
        if (a.id != a.name) {
            e["id"] = a.id;
        }
        if (!a.file.empty()) {
            e["file"] = a.file.generic_string();
        }
        e["category"] = assetCategoryName(a.category);
        if (!a.archetype.empty()) {
            e["archetype"] = a.archetype;
        }
        if (!a.tags.empty()) {
            e["tags"] = a.tags;
        }
        if (a.naturalSize != def.naturalSize) {
            e["naturalSize"] = vec3ToJson(a.naturalSize);
        }
        if (a.triangles != def.triangles) {
            e["triangles"] = a.triangles;
        }
        if (a.visualImportance != def.visualImportance) {
            e["visualImportance"] = a.visualImportance;
        }
        if (a.preferredScale != def.preferredScale) {
            e["preferredScale"] = a.preferredScale;
        }
        if (a.preferredDensity != def.preferredDensity) {
            e["preferredDensity"] = a.preferredDensity;
        }
        const MaterialProfile md;
        if (a.material.emissive != md.emissive || a.material.translucency != md.translucency ||
            a.material.roughness != md.roughness || a.material.metallic != md.metallic ||
            a.material.tint != md.tint) {
            e["material"] = json{{"emissive", a.material.emissive},
                                 {"translucency", a.material.translucency},
                                 {"roughness", a.material.roughness},
                                 {"metallic", a.material.metallic},
                                 {"tint", vec3ToJson(a.material.tint)}};
        }
        const AudioResponseProfile ad;
        if (a.audioResponse.energy != ad.energy || a.audioResponse.impact != ad.impact ||
            a.audioResponse.sway != ad.sway || a.audioResponse.bloom != ad.bloom) {
            e["audioResponse"] = json{{"energy", a.audioResponse.energy},
                                      {"impact", a.audioResponse.impact},
                                      {"sway", a.audioResponse.sway},
                                      {"bloom", a.audioResponse.bloom}};
        }
        const VariationProfile vd;
        if (a.variation.scale != vd.scale || a.variation.hue != vd.hue ||
            a.variation.emissive != vd.emissive || a.variation.yaw != vd.yaw ||
            a.variation.lean != vd.lean) {
            e["variation"] = json{{"scale", a.variation.scale},
                                  {"hue", a.variation.hue},
                                  {"emissive", a.variation.emissive},
                                  {"yaw", a.variation.yaw},
                                  {"lean", a.variation.lean}};
        }
        arr.push_back(std::move(e));
    }
    out["assets"] = std::move(arr);
    return out;
}

} // namespace avgen::assets
