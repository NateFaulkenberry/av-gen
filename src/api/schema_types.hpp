#pragma once

// ADR-2152: the typing tables, the one authored half of the document schemas.
//
// A schema has two halves. **Completeness** -- which keys exist -- is the parsers' own key lists
// (`app::Engine::projectFileKeys`, `scene::sceneFileKeys` and its siblings, the rig, route and chain
// lists, `scene::postFileKeys`), so a key a parser reads is named because the parser names it.
// **Typing** -- what each key holds -- cannot be derived: the parsers read a key wherever they need it.
// So it is written here, once per key, beside nothing else, and the tests make every gap visible:
//
//   - D2 (tests/unit/test_api_schemas.cpp): every listed key has an entry, and every entry under a
//     listed object names a listed key. Adding a key to `kSceneKeys` without an entry fails, naming
//     the key and the table file to edit.
//   - A block the PoC does not need is typed `{"x-avgen-untyped": true}` explicitly, so "not typed"
//     is a published fact rather than an absence.
//   - D3: every shipped example validates, so an entry stricter than the parser is a failing test.
//   - D4: `writtenBack: false` entries are exactly ADR-2151's documented normalisations.
//
// The enums inside the fragments are never written here: they come from the code that reads them
// (`scene::allNodeKinds`, `params::modOpNames`, ...).

#include <nlohmann/json.hpp>

#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace avgen::api {

// One key's type. `pointer` is a JSON pointer into the document, with `*` standing for every element
// of an array ("/lights/*/castsShadow"). `writtenBack` is false where a save does not write the key
// back as it was read (ADR-2151's normalisations: moved, omitted when empty, dropped); `note` says how.
struct TypingEntry {
    std::string pointer;
    nlohmann::json fragment;
    bool writtenBack = true;
    std::string note;
};

// A parser's key list and the object it lists. The generator builds that object's `properties` from
// the entries directly under `pointer`. `defName` is where the object lives in `$defs` (empty for
// the document root); `shell` is the object schema the properties go into.
struct KeyList {
    std::string pointer;
    std::string defName;
    std::vector<std::string> keys;
    std::string source; // the list's home, for the D2 message: "kSceneKeys in src/scene/composition.cpp"
    nlohmann::json shell;
};

struct DocumentTyping {
    std::string tableFile; // the file to edit, for the D2 message
    std::vector<KeyList> keyLists;
    std::vector<TypingEntry> entries;
    nlohmann::json defs = nlohmann::json::object(); // whole `$defs` no key list fills (assetRef, node, ...)
};

[[nodiscard]] DocumentTyping projectTyping(); // src/api/schema_types_project.cpp
[[nodiscard]] DocumentTyping sceneTyping();   // src/api/schema_types_scene.cpp

// The scene node and its procedural block, which a project's `sceneNodes.added` carries too.
[[nodiscard]] nlohmann::json sceneNodeDefs();

// Small fragment builders, so the tables read as types rather than as JSON.
namespace typing {

using nlohmann::json;

// How an object treats a key its reader does not know (`x-avgen-unknownKeys`):
//   keep       kept verbatim and written back (a project's top level, ADR-2132)
//   warn       warned about at load, kept and written back (a scene's top level and `post`, ADR-2151)
//   warn-drop  warned about at load, not written back
//   drop       ignored, not written back
inline json object(json properties = json::object(), std::string_view unknownKeys = "drop",
                   std::vector<std::string> required = {}) {
    json j{{"type", "object"}, {"additionalProperties", true}, {"x-avgen-unknownKeys", unknownKeys}};
    if (!properties.empty()) {
        j["properties"] = std::move(properties);
    }
    if (!required.empty()) {
        j["required"] = std::move(required);
    }
    return j;
}
inline json untyped(std::string_view description = {}) {
    json j{{"x-avgen-untyped", true}};
    if (!description.empty()) {
        j["description"] = description;
    }
    return j;
}
inline json described(json j, std::string_view description) {
    if (!description.empty()) {
        j["description"] = description;
    }
    return j;
}
inline json string(std::string_view description = {}) { return described({{"type", "string"}}, description); }
inline json number(std::string_view description = {}) { return described({{"type", "number"}}, description); }
inline json integer(std::string_view description = {}) { return described({{"type", "integer"}}, description); }
inline json boolean(std::string_view description = {}) { return described({{"type", "boolean"}}, description); }
inline json range(json j, double lo, double hi) {
    j["minimum"] = lo;
    j["maximum"] = hi;
    return j;
}
inline json atLeast(json j, double lo) {
    j["minimum"] = lo;
    return j;
}
inline json numbers(int n, std::string_view description = {}) {
    return described({{"type", "array"}, {"items", {{"type", "number"}}}, {"minItems", n}, {"maxItems", n}},
                     description);
}
inline json vec3(std::string_view description = {}) { return numbers(3, description); }
inline json arrayOf(json items, std::string_view description = {}) {
    return described({{"type", "array"}, {"items", std::move(items)}}, description);
}
inline json ref(std::string_view def) { return {{"$ref", "#/$defs/" + std::string(def)}}; }
inline json constant(json value) { return {{"const", std::move(value)}}; }
inline json anyOf(std::vector<json> forms, std::string_view description = {}) {
    return described({{"anyOf", std::move(forms)}}, description);
}
template <typename Names>
json oneOfNames(const Names& names, std::string_view description = {}) {
    json values = json::array();
    for (const auto& n : names) {
        values.push_back(std::string(n));
    }
    return described({{"type", "string"}, {"enum", std::move(values)}}, description);
}

} // namespace typing

} // namespace avgen::api
