#include "comp/font.hpp"

#include <nlohmann/json.hpp>

#include <fmt/format.h>

namespace avgen::comp {

std::string FontDesc::key() const {
    return fmt::format("{}|{}|{:.3f}|{}", family, postScriptName, weight, italic ? 1 : 0);
}

nlohmann::json FontDesc::toJson() const {
    nlohmann::json j = nlohmann::json::object();
    j["family"] = family;
    if (!postScriptName.empty()) {
        j["postScript"] = postScriptName;
    }
    if (weight != 0.0f) {
        j["weight"] = weight;
    }
    if (italic) {
        j["italic"] = true;
    }
    if (!fallback.empty()) {
        j["fallback"] = fallback;
    }
    return j;
}

Result<FontDesc> FontDesc::fromJson(const nlohmann::json& j) {
    FontDesc desc;
    if (j.is_string()) { // shorthand: just a family name
        desc.family = j.get<std::string>();
        return desc;
    }
    if (!j.is_object()) {
        return fail("font must be an object or a family name");
    }
    desc.family = j.value("family", desc.family);
    desc.postScriptName = j.value("postScript", std::string());
    desc.weight = j.value("weight", 0.0f);
    desc.italic = j.value("italic", false);
    desc.fallback = j.value("fallback", desc.fallback);
    return desc;
}

} // namespace avgen::comp
