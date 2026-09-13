// Renderer forensics Phase 3.3 (CPU/WGSL layout audit) and Phase 9.3 (NaN/Inf guards), from
// outside and without a device.
//
// **Phase 3.3.** The renderer already carries `static_assert(sizeof(X) == N)` beside most of its
// uniform structures, and those asserts are worth exactly one thing: they notice when *this* side
// changes. They cannot notice when the *shader* side changes, and a second copy of the same number
// written in a test would not either. So this file does not restate any size. It parses the WGSL
// struct out of `shaders/*.wgsl`, parses the C++ struct out of the header that claims to mirror it,
// lays both out under their own language's rules, and compares the results byte for byte. The
// question it answers is the one the plan asks -- *do the two declarations describe the same
// bytes?* -- and the only way to make it pass is to keep them agreeing.
//
// The obvious objection is that a hand-written layout model is itself something that can be wrong.
// That is why the first test case exists: for every structure that lives in a GPU-free header the
// unit binary can actually include, the model's computed size and member offsets are checked
// against the compiler's own `sizeof` and `offsetof`. The model is only trusted for the structures
// it cannot reach because they sit behind `<webgpu/webgpu_cpp.h>`, and it is anchored by the ones
// it can.
//
// What is asserted versus assumed, stated plainly:
//   - *Asserted*: WGSL and C++ agree on total size, on every leaf scalar's offset and width, on the
//     scalar's type class (f32 / i32 / u32), on top-level field order and naming, on WGSL's
//     alignment rules being satisfied, and on dynamic-offset strides being 256-byte multiples no
//     smaller than the record they carry.
//   - *Assumed*: that Dawn's WGSL layout rules are the ones in the WGSL specification's
//     "Structure Member Layout" section, and that the host is the Itanium ABI with glm's default
//     (unaligned) vector types. Both hold for every platform this engine builds on, and the anchor
//     test below would fail loudly on one where the second did not.
//
// **Phase 9.3.** The existing finite guards cover the camera matrices, entity model matrices and
// skinning palettes (`finiteMatrix` in scene_renderer.cpp, `finitePalette` in skinning.cpp). The
// plan asks for the other doors -- bounds, materials, water state, packed GPU structures. A unit
// test cannot open the doors that need a device, so the ones opened here are the GPU-free
// functions the renderer calls on its way to the GPU: scene bounds, cull bounds, light packing,
// the cascade fit, material-program packing and field packing. Each is opened twice, once with
// finite input as a positive control and once poisoned, and the result is recorded as it is.
//
// These are *characterisation* tests. They assert what happens today, which in every case is that
// nothing names the entity, the property or the value -- and adding the guard the plan asks for
// will fail them. That is the point: they are the record that the door was open and the
// notification that it closed.
//
// The interesting part of the answer is that "non-finite data reaches the GPU" is only half of
// what is going on, and it is the less dangerous half. `glm::min` and `glm::max` are written
// `(y < x) ? y : x`, and a comparison against a NaN is false, so a NaN argument in the second
// position is *silently discarded* rather than propagated. Every bounds accumulator in this engine
// folds that way. So a poisoned vertex does not produce a NaN box that a finite guard could catch;
// it produces a perfectly finite box that is wrong, or -- when every corner is poisoned -- an
// untouched, inverted seed box. A guard written as `isfinite(box)` would pass on both.

#include "rendering/light_data.hpp"
#include "rendering/shadow_math.hpp"
#include "scene/material_program.hpp"
#include "scene/scene.hpp"
#include "scene/scene_types.hpp"
#include "spatial/field.hpp"

#include <catch2/catch_test_macros.hpp>

#include <regex>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

using namespace avgen;

namespace {

// The shaders are read from the source tree. `AVGEN_SHADER_SOURCE_DIR` is defined on avgen_gpu,
// which the unit binary does not link; `AVGEN_SOURCE_DIR` comes from avgen_core and does reach
// here, and the shaders live at a fixed place under it. Preferring the first keeps the test
// correct if the shader directory is ever moved out from under the source root.
#if defined(AVGEN_SHADER_SOURCE_DIR)
std::filesystem::path shaderDir() { return std::filesystem::path(AVGEN_SHADER_SOURCE_DIR); }
#else
std::filesystem::path shaderDir() { return std::filesystem::path(AVGEN_SOURCE_DIR) / "shaders"; }
#endif
std::filesystem::path sourceDir() { return std::filesystem::path(AVGEN_SOURCE_DIR) / "src"; }

// ---- source text --------------------------------------------------------------------------------

// Comments are stripped before anything else looks at the text, so a struct field mentioned inside
// a `//` explanation cannot be mistaken for a declaration. Both languages use the same two comment
// forms, so one function serves both.
std::string stripComments(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    for (std::size_t i = 0; i < text.size();) {
        if (text[i] == '/' && i + 1 < text.size() && text[i + 1] == '/') {
            while (i < text.size() && text[i] != '\n') {
                ++i;
            }
        } else if (text[i] == '/' && i + 1 < text.size() && text[i + 1] == '*') {
            i += 2;
            while (i + 1 < text.size() && !(text[i] == '*' && text[i + 1] == '/')) {
                ++i;
            }
            i = std::min(i + 2, text.size());
        } else {
            out.push_back(text[i]);
            ++i;
        }
    }
    return out;
}

std::optional<std::string> readFile(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return std::nullopt;
    }
    std::ostringstream buffer;
    buffer << in.rdbuf();
    return stripComments(buffer.str());
}

// The declarations a comparison needs are usually spread over more than one file -- `FrameUniforms`
// names `LightUniform` and `wind::WindUniforms`, `FieldBlock` names `spatial::FieldGpu`. A bundle
// is the concatenation of exactly the files one comparison needs, which also keeps the two
// unrelated structures both called `ObjectUniforms` (scene_renderer.hpp and reference_renderer.cpp)
// out of each other's way.
std::string bundle(const std::filesystem::path& root, const std::vector<std::string>& names) {
    std::string all;
    for (const std::string& name : names) {
        const std::filesystem::path path = root / name;
        const std::optional<std::string> text = readFile(path);
        INFO("reading " << path.string());
        REQUIRE(text.has_value()); // the audit is worthless if a file silently went missing
        all += *text;
        all += '\n';
    }
    return all;
}

// ---- a laid-out structure ------------------------------------------------------------------------

// One scalar as it finally sits in memory: where it starts, how wide it is, and which of WGSL's
// three scalar types it is. Comparing these is the strongest statement the audit can make, because
// it is invariant to how either side chose to *group* its bytes -- `wind::WindUniforms` is one
// member in C++ and four `vec4<f32>` in WGSL, and `float inv[3][4]` is one member in C++ and three
// `vec4<f32>` in WGSL, and both of those are correct.
struct Leaf {
    std::string path;    // "lights[2].cone.z", for the failure message
    std::size_t offset;
    std::size_t size;
    char kind;           // 'f' = f32, 'i' = i32, 'u' = u32
};

struct Member {
    std::string name;
    std::size_t offset;
    std::size_t size;
    std::size_t align;
};

struct Layout {
    std::size_t size = 0;
    std::size_t align = 1;
    std::vector<Member> members;
    std::vector<Leaf> leaves;
};

std::size_t roundUp(std::size_t align, std::size_t value) { return (value + align - 1) / align * align; }

// ---- WGSL ----------------------------------------------------------------------------------------

using StructBodies = std::unordered_map<std::string, std::vector<std::pair<std::string, std::string>>>;

std::string trimmed(std::string_view s) {
    std::size_t b = 0;
    std::size_t e = s.size();
    while (b < e && std::isspace(static_cast<unsigned char>(s[b])) != 0) {
        ++b;
    }
    while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1])) != 0) {
        --e;
    }
    return std::string(s.substr(b, e - b));
}

// Finds the matching `}` for the `{` at `open`, so a struct containing braces (none do today, but
// the parser should not depend on that) is still delimited correctly.
std::size_t matchBrace(const std::string& text, std::size_t open) {
    int depth = 1;
    std::size_t i = open + 1;
    while (i < text.size() && depth > 0) {
        if (text[i] == '{') {
            ++depth;
        } else if (text[i] == '}') {
            --depth;
        }
        ++i;
    }
    return i;
}

StructBodies parseWgslStructs(const std::string& text) {
    StructBodies out;
    for (std::size_t pos = text.find("struct "); pos != std::string::npos; pos = text.find("struct ", pos + 1)) {
        // `struct` must start a declaration, not end an identifier.
        if (pos != 0 && (std::isalnum(static_cast<unsigned char>(text[pos - 1])) != 0 || text[pos - 1] == '_')) {
            continue;
        }
        const std::size_t nameStart = pos + 7;
        std::size_t brace = text.find('{', nameStart);
        if (brace == std::string::npos) {
            continue;
        }
        const std::string name = trimmed(text.substr(nameStart, brace - nameStart));
        if (name.empty() || name.find_first_of(" \t\n") != std::string::npos) {
            continue;
        }
        const std::size_t end = matchBrace(text, brace);
        const std::string body = text.substr(brace + 1, end - brace - 2);

        // Fields are separated by commas at angle-bracket depth zero: `array<Light, 8>` carries one
        // of its own, which is exactly the case a naive split gets wrong.
        std::vector<std::pair<std::string, std::string>> fields;
        int depth = 0;
        std::string current;
        const auto flush = [&] {
            const std::string field = trimmed(current);
            current.clear();
            const std::size_t colon = field.find(':');
            if (colon == std::string::npos) {
                return;
            }
            std::string fieldName = trimmed(field.substr(0, colon));
            // Attributes (`@location(0)`, `@builtin(position)`) precede the name on pipeline I/O
            // structures; host-shareable ones have none, and dropping them costs nothing.
            const std::size_t lastSpace = fieldName.find_last_of(" \t\n)");
            if (lastSpace != std::string::npos) {
                fieldName = fieldName.substr(lastSpace + 1);
            }
            fields.emplace_back(fieldName, trimmed(field.substr(colon + 1)));
        };
        for (const char c : body) {
            if (c == '<' || c == '(' || c == '[') {
                ++depth;
            } else if (c == '>' || c == ')' || c == ']') {
                --depth;
            }
            if (c == ',' && depth == 0) {
                flush();
            } else {
                current.push_back(c);
            }
        }
        flush();
        out.emplace(name, std::move(fields));
    }
    return out;
}

struct SizeAlign {
    std::size_t size;
    std::size_t align;
};

SizeAlign wgslSizeAlign(const std::string& type, const StructBodies& structs);
Layout wgslLayout(const std::string& name, const StructBodies& structs);

// Expands one WGSL type into its leaf scalars at absolute offsets.
void wgslLeaves(const std::string& type, const StructBodies& structs, std::size_t base, const std::string& path,
                std::vector<Leaf>& out) {
    const auto scalar = [&](char kind) { out.push_back({path, base, 4, kind}); };
    if (type == "f32") {
        scalar('f');
        return;
    }
    if (type == "i32") {
        scalar('i');
        return;
    }
    if (type == "u32") {
        scalar('u');
        return;
    }
    if (type.rfind("vec", 0) == 0) {
        const int n = type[3] - '0';
        const std::string element = trimmed(type.substr(type.find('<') + 1, type.rfind('>') - type.find('<') - 1));
        static const char* kLanes = "xyzw";
        for (int i = 0; i < n; ++i) {
            wgslLeaves(element, structs, base + static_cast<std::size_t>(i) * 4,
                       path + "." + kLanes[i], out);
        }
        return;
    }
    if (type.rfind("mat", 0) == 0) {
        const int columns = type[3] - '0';
        const int rows = type[5] - '0';
        const std::size_t columnStride = roundUp(rows == 3 ? 16 : static_cast<std::size_t>(rows) * 4,
                                                 static_cast<std::size_t>(rows) * 4);
        for (int c = 0; c < columns; ++c) {
            for (int r = 0; r < rows; ++r) {
                out.push_back({path + "[" + std::to_string(c) + "][" + std::to_string(r) + "]",
                               base + static_cast<std::size_t>(c) * columnStride + static_cast<std::size_t>(r) * 4,
                               4, 'f'});
            }
        }
        return;
    }
    if (type.rfind("array<", 0) == 0) {
        const std::size_t comma = type.rfind(',');
        const std::string element = trimmed(type.substr(6, comma - 6));
        std::string countText = trimmed(type.substr(comma + 1, type.rfind('>') - comma - 1));
        if (!countText.empty() && countText.back() == 'u') {
            countText.pop_back();
        }
        const auto count = static_cast<std::size_t>(std::stoul(countText));
        const SizeAlign element_ = wgslSizeAlign(element, structs);
        // A uniform-address-space array element aligns to a multiple of 16, which is what makes an
        // `array<Light, 8>` 512 bytes rather than however tightly `Light` happens to pack.
        const std::size_t stride = roundUp(std::max<std::size_t>(element_.align, 16), element_.size);
        for (std::size_t i = 0; i < count; ++i) {
            wgslLeaves(element, structs, base + i * stride, path + "[" + std::to_string(i) + "]", out);
        }
        return;
    }
    const auto it = structs.find(type);
    REQUIRE(it != structs.end()); // an unresolved type would silently contribute no leaves at all
    for (const Leaf& leaf : wgslLayout(type, structs).leaves) {
        out.push_back({path + "." + leaf.path, base + leaf.offset, leaf.size, leaf.kind});
    }
}

SizeAlign wgslSizeAlign(const std::string& type, const StructBodies& structs) {
    if (type == "f32" || type == "i32" || type == "u32") {
        return {4, 4};
    }
    if (type.rfind("vec", 0) == 0) {
        switch (type[3]) {
        case '2':
            return {8, 8};
        case '3':
            return {12, 16};
        default:
            return {16, 16};
        }
    }
    if (type.rfind("mat", 0) == 0) {
        const auto columns = static_cast<std::size_t>(type[3] - '0');
        const auto rows = static_cast<std::size_t>(type[5] - '0');
        const std::size_t columnAlign = rows == 2 ? 8 : 16;
        return {roundUp(columnAlign, rows * 4) * columns, columnAlign};
    }
    if (type.rfind("array<", 0) == 0) {
        const std::size_t comma = type.rfind(',');
        const std::string element = trimmed(type.substr(6, comma - 6));
        std::string countText = trimmed(type.substr(comma + 1, type.rfind('>') - comma - 1));
        if (!countText.empty() && countText.back() == 'u') {
            countText.pop_back();
        }
        const SizeAlign element_ = wgslSizeAlign(element, structs);
        const std::size_t align = std::max<std::size_t>(element_.align, 16);
        return {roundUp(align, element_.size) * static_cast<std::size_t>(std::stoul(countText)), align};
    }
    const Layout nested = wgslLayout(type, structs);
    return {nested.size, nested.align};
}

Layout wgslLayout(const std::string& name, const StructBodies& structs) {
    const auto it = structs.find(name);
    REQUIRE(it != structs.end());
    Layout layout;
    std::size_t offset = 0;
    for (const auto& [fieldName, fieldType] : it->second) {
        const SizeAlign sa = wgslSizeAlign(fieldType, structs);
        offset = roundUp(sa.align, offset);
        layout.members.push_back({fieldName, offset, sa.size, sa.align});
        wgslLeaves(fieldType, structs, offset, fieldName, layout.leaves);
        offset += sa.size;
        layout.align = std::max(layout.align, sa.align);
    }
    // Every structure compared here is host-shareable in the uniform or storage address space,
    // where WGSL rounds a struct's alignment up to 16. That rounding is the whole reason a
    // 272-byte `ObjectUniforms` is 272 and not 268.
    layout.align = std::max<std::size_t>(layout.align, 16);
    layout.size = roundUp(layout.align, offset);
    return layout;
}

// ---- C++ -------------------------------------------------------------------------------------

// A C++ member as declared: the type text, the name, and any array extents (outermost first).
struct CppField {
    std::string type;
    std::string name;
    std::vector<std::string> extents;
};

struct CppStruct {
    std::size_t forcedAlign = 0; // from `alignas(N)`
    std::vector<CppField> fields;
};

using CppStructs = std::unordered_map<std::string, CppStruct>;
using Constants = std::unordered_map<std::string, long long>;

bool identifierChar(char c) { return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_' || c == ':'; }

// Scrapes `constexpr <type> <name> = <integer>;` so array extents written as
// `scene::kMaxFieldForces * 2` resolve to the number the header actually says, rather than to a
// number copied into this test that could drift away from it.
Constants parseConstants(const std::string& text) {
    Constants out;
    for (std::size_t pos = text.find("constexpr"); pos != std::string::npos; pos = text.find("constexpr", pos + 1)) {
        const std::size_t equals = text.find('=', pos);
        const std::size_t semi = text.find(';', pos);
        if (equals == std::string::npos || semi == std::string::npos || equals > semi) {
            continue;
        }
        const std::string declaration = trimmed(text.substr(pos + 9, equals - pos - 9));
        const std::size_t lastSpace = declaration.find_last_of(" \t\n");
        if (lastSpace == std::string::npos) {
            continue;
        }
        const std::string name = declaration.substr(lastSpace + 1);
        const std::string value = trimmed(text.substr(equals + 1, semi - equals - 1));
        if (name.empty() || value.empty() ||
            value.find_first_not_of("0123456789") != std::string::npos) {
            continue;
        }
        out.emplace(name, std::stoll(value));
    }
    return out;
}

// Array extents are small products and sums of scraped constants (`kMaxFieldForces * 2`), never
// anything that needs precedence: evaluating strictly left to right is enough and is checked by
// the anchor test, where a wrong extent would show up as a wrong `sizeof`.
long long evaluateExtent(const std::string& expression, const Constants& constants) {
    std::vector<std::string> tokens;
    std::string current;
    for (const char c : expression) {
        if (identifierChar(c) || std::isdigit(static_cast<unsigned char>(c)) != 0) {
            current.push_back(c);
        } else {
            if (!current.empty()) {
                tokens.push_back(current);
                current.clear();
            }
            if (c == '*' || c == '+') {
                tokens.emplace_back(1, c);
            }
        }
    }
    if (!current.empty()) {
        tokens.push_back(current);
    }
    REQUIRE_FALSE(tokens.empty());
    const auto value = [&](const std::string& token) -> long long {
        if (std::isdigit(static_cast<unsigned char>(token.front())) != 0) {
            return std::stoll(token);
        }
        const std::size_t colon = token.rfind(':');
        const std::string bare = colon == std::string::npos ? token : token.substr(colon + 1);
        const auto it = constants.find(bare);
        REQUIRE(it != constants.end()); // an unresolved extent would give a plausible wrong answer
        return it->second;
    };
    long long result = value(tokens[0]);
    for (std::size_t i = 1; i + 1 < tokens.size(); i += 2) {
        result = tokens[i] == "*" ? result * value(tokens[i + 1]) : result + value(tokens[i + 1]);
    }
    return result;
}

CppStructs parseCppStructs(const std::string& text) {
    CppStructs out;
    for (std::size_t pos = text.find("struct "); pos != std::string::npos; pos = text.find("struct ", pos + 1)) {
        if (pos != 0 && identifierChar(text[pos - 1])) {
            continue;
        }
        const std::size_t brace = text.find('{', pos);
        const std::size_t semi = text.find(';', pos);
        if (brace == std::string::npos || (semi != std::string::npos && semi < brace)) {
            continue; // a forward declaration, or `struct X;`
        }
        std::string head = trimmed(text.substr(pos + 7, brace - pos - 7));
        std::size_t forced = 0;
        if (head.rfind("alignas(", 0) == 0) {
            const std::size_t close = head.find(')');
            forced = static_cast<std::size_t>(std::stoul(head.substr(8, close - 8)));
            head = trimmed(head.substr(close + 1));
        }
        const std::size_t nameEnd = head.find_first_of(" \t\n:");
        const std::string name = nameEnd == std::string::npos ? head : head.substr(0, nameEnd);
        if (name.empty()) {
            continue;
        }
        const std::size_t end = matchBrace(text, brace);
        const std::string body = text.substr(brace + 1, end - brace - 2);

        CppStruct parsed;
        parsed.forcedAlign = forced;
        // Statements end at a `;` outside braces and angle brackets, so a default member
        // initialiser (`glm::vec4 dir{1.0f, 0.0f, 0.0f, 0.0f}`) and a `std::array<T, N>` both stay
        // in one piece.
        int depth = 0;
        std::string statement;
        const auto flush = [&] {
            std::string s = trimmed(statement);
            statement.clear();
            if (s.empty() || s.rfind("static", 0) == 0 || s.rfind("using", 0) == 0 ||
                s.rfind("enum", 0) == 0 || s.rfind("struct", 0) == 0 || s.rfind("[[", 0) == 0) {
                return;
            }
            const std::size_t equals = s.find('=');
            if (equals != std::string::npos) {
                s = trimmed(s.substr(0, equals));
            }
            if (const std::size_t init = s.find('{'); init != std::string::npos) {
                s = trimmed(s.substr(0, init));
            }
            // A member function declaration: parentheses that are not part of a template argument.
            if (s.find('(') != std::string::npos) {
                return;
            }
            std::vector<std::string> extents;
            while (!s.empty() && s.back() == ']') {
                const std::size_t open = s.rfind('[');
                if (open == std::string::npos) {
                    break;
                }
                extents.insert(extents.begin(), s.substr(open + 1, s.size() - open - 2));
                s = trimmed(s.substr(0, open));
            }
            const std::size_t lastSpace = s.find_last_of(" \t\n");
            if (lastSpace == std::string::npos) {
                return;
            }
            std::string type = trimmed(s.substr(0, lastSpace));
            // `std::array<T, N>` has exactly the layout of `T[N]`, so it is rewritten as one here
            // and the array rule below is written once instead of twice.
            if (type.rfind("std::array<", 0) == 0) {
                const std::size_t comma = type.rfind(',');
                extents.insert(extents.begin(), trimmed(type.substr(comma + 1, type.rfind('>') - comma - 1)));
                type = trimmed(type.substr(11, comma - 11));
            }
            parsed.fields.push_back({type, trimmed(s.substr(lastSpace + 1)), std::move(extents)});
        };
        for (const char c : body) {
            if (c == '{' || c == '<' || c == '(') {
                ++depth;
            } else if (c == '}' || c == '>' || c == ')') {
                --depth;
            }
            if (c == ';' && depth == 0) {
                flush();
            } else {
                statement.push_back(c);
            }
        }
        out.emplace(name, std::move(parsed));
    }
    return out;
}

SizeAlign cppSizeAlign(const CppField& field, const CppStructs& structs, const Constants& constants);
Layout cppLayout(const std::string& name, const CppStructs& structs, const Constants& constants);

// The base type's own size, alignment and leaf pattern, before array extents multiply it.
struct CppBase {
    SizeAlign sa;
    char kind = 'f';  // meaningful only for scalars
    int lanes = 1;    // 1 scalar, 2-4 vector, 16 for mat4
    std::string nested; // non-empty when the base is another parsed struct
};

CppBase cppBase(const std::string& rawType, const CppStructs& structs, const Constants& constants) {
    std::string type = trimmed(rawType);
    if (type.rfind("const ", 0) == 0) {
        type = trimmed(type.substr(6));
    }
    static const std::unordered_map<std::string, std::tuple<std::size_t, std::size_t, char, int>> kScalars = {
        {"float", {4, 4, 'f', 1}},          {"std::uint32_t", {4, 4, 'u', 1}},
        {"uint32_t", {4, 4, 'u', 1}},       {"std::int32_t", {4, 4, 'i', 1}},
        {"int32_t", {4, 4, 'i', 1}},        {"int", {4, 4, 'i', 1}},
        {"glm::vec2", {8, 4, 'f', 2}},      {"glm::vec3", {12, 4, 'f', 3}},
        // glm's default vector types are *not* over-aligned (GLM_FORCE_ALIGNED_GENTYPES is not
        // set), so a `glm::vec4` aligns to 4, not to the 16 its WGSL counterpart demands. Modelling
        // that honestly is what makes the pairing test able to catch a stray leading scalar: with a
        // pretend 16 the C++ side would silently re-pad itself into agreement with the shader.
        {"glm::vec4", {16, 4, 'f', 4}},     {"glm::uvec4", {16, 4, 'u', 4}},
        {"glm::ivec4", {16, 4, 'i', 4}},    {"glm::mat4", {64, 4, 'f', 16}},
    };
    if (const auto it = kScalars.find(type); it != kScalars.end()) {
        const auto& [size, align, kind, lanes] = it->second;
        return {{size, align}, kind, lanes, {}};
    }
    // Namespace-qualified struct references (`wind::WindUniforms`, `spatial::FieldGpu`) resolve on
    // the unqualified name, which is unambiguous inside one bundle by construction.
    const std::size_t colon = type.rfind(':');
    const std::string bare = colon == std::string::npos ? type : type.substr(colon + 1);
    const auto it = structs.find(bare);
    REQUIRE(it != structs.end()); // an unknown type must stop the audit, not be silently skipped
    const Layout nested = cppLayout(bare, structs, constants);
    return {{nested.size, nested.align}, 'f', 1, bare};
}

SizeAlign cppSizeAlign(const CppField& field, const CppStructs& structs, const Constants& constants) {
    CppBase base = cppBase(field.type, structs, constants);
    for (const std::string& extent : field.extents) {
        base.sa.size *= static_cast<std::size_t>(evaluateExtent(extent, constants));
    }
    return base.sa;
}

void cppLeaves(const CppField& field, const CppStructs& structs, const Constants& constants, std::size_t offset,
               std::vector<Leaf>& out) {
    const CppBase base = cppBase(field.type, structs, constants);
    std::size_t count = 1;
    for (const std::string& extent : field.extents) {
        count *= static_cast<std::size_t>(evaluateExtent(extent, constants));
    }
    for (std::size_t i = 0; i < count; ++i) {
        const std::size_t at = offset + i * base.sa.size;
        const std::string path = count == 1 ? field.name : field.name + "[" + std::to_string(i) + "]";
        if (base.nested.empty()) {
            for (int lane = 0; lane < base.lanes; ++lane) {
                out.push_back({path + "." + std::to_string(lane), at + static_cast<std::size_t>(lane) * 4, 4,
                               base.kind});
            }
        } else {
            for (const Leaf& leaf : cppLayout(base.nested, structs, constants).leaves) {
                out.push_back({path + "." + leaf.path, at + leaf.offset, leaf.size, leaf.kind});
            }
        }
    }
}

Layout cppLayout(const std::string& name, const CppStructs& structs, const Constants& constants) {
    const auto it = structs.find(name);
    REQUIRE(it != structs.end());
    Layout layout;
    std::size_t offset = 0;
    for (const CppField& field : it->second.fields) {
        const SizeAlign sa = cppSizeAlign(field, structs, constants);
        offset = roundUp(sa.align, offset);
        layout.members.push_back({field.name, offset, sa.size, sa.align});
        cppLeaves(field, structs, constants, offset, layout.leaves);
        offset += sa.size;
        layout.align = std::max(layout.align, sa.align);
    }
    layout.align = std::max(layout.align, it->second.forcedAlign);
    layout.size = roundUp(layout.align, offset);
    return layout;
}

// ---- the comparison ------------------------------------------------------------------------------

// What one side calls a member the other may call something else, or may group differently. Every
// entry here is a real, deliberate divergence between the two declarations and is listed so that an
// *undeliberate* one cannot hide behind it: the member-name check below is exact once the table has
// been applied.
struct Alias {
    std::string cppName;
    std::vector<std::string> wgslNames;
};

void comparePairing(const std::string& label, const Layout& wgsl, const Layout& cpp,
                    const std::vector<Alias>& aliases) {
    INFO("structure " << label);
    REQUIRE_FALSE(wgsl.members.empty());
    REQUIRE_FALSE(cpp.members.empty());

    CHECK(wgsl.size == cpp.size);
    // WGSL's uniform address space requires a host-shareable struct to align to 16, and every
    // structure here is uploaded either at offset 0 or at a dynamic offset that is a multiple of
    // 256, so its size must also be a multiple of 16 for an array of them to stay aligned.
    CHECK(wgsl.size % 16 == 0);
    CHECK(wgsl.align % 16 == 0);

    // The byte-level claim: the same scalars of the same width and the same type class in the same
    // places. This is what a shader actually reads, and it is invariant to grouping.
    REQUIRE(wgsl.leaves.size() == cpp.leaves.size());
    for (std::size_t i = 0; i < wgsl.leaves.size(); ++i) {
        const Leaf& w = wgsl.leaves[i];
        const Leaf& c = cpp.leaves[i];
        if (w.offset != c.offset || w.size != c.size || w.kind != c.kind) {
            INFO("leaf " << i << ": wgsl " << w.path << " @" << w.offset << " +" << w.size << " '" << w.kind
                         << "' vs cpp " << c.path << " @" << c.offset << " +" << c.size << " '" << c.kind << "'");
            CHECK(false);
        }
    }

    // The byte-level claim above cannot see two same-typed members swapped -- exchanging `material`
    // and `flags` moves no bytes and changes no widths, and turns every roughness into an alpha
    // mode. Member names in order are what catches that, which is why the alias table is explicit
    // rather than a fuzzy match.
    std::vector<std::string> expected;
    for (const Member& member : cpp.members) {
        const auto alias = std::find_if(aliases.begin(), aliases.end(),
                                        [&](const Alias& a) { return a.cppName == member.name; });
        if (alias == aliases.end()) {
            expected.push_back(member.name);
        } else {
            expected.insert(expected.end(), alias->wgslNames.begin(), alias->wgslNames.end());
        }
    }
    REQUIRE(expected.size() == wgsl.members.size());
    for (std::size_t i = 0; i < expected.size(); ++i) {
        INFO("member " << i);
        CHECK(expected[i] == wgsl.members[i].name);
    }
}

// A pairing the audit performs: one WGSL struct, one C++ struct, the files each needs, the aliases.
struct Pairing {
    std::string label;
    std::vector<std::string> shaders;
    std::string wgslName;
    std::vector<std::string> headers;
    std::string cppName;
    std::vector<Alias> aliases;
};

const std::vector<Pairing>& pairings() {
    static const std::vector<Pairing> kPairings = {
        {"FrameUniforms",
         {"common.wgsl"},
         "FrameUniforms",
         {"rendering/scene_renderer.hpp", "core/wind.hpp"},
         "FrameUniforms",
         // The wind field is one nested struct on the C++ side and four loose vec4s in the shader.
         // That is deliberate: `wind::packWind` builds the block once and the shaders read it
         // through `shaders/wind.wgsl`, which wants the four names.
         {{"wind", {"windDir", "windRegion", "windGust", "windTurb"}}}},
        {"ObjectUniforms", {"common.wgsl"}, "ObjectUniforms", {"rendering/scene_renderer.hpp"}, "ObjectUniforms", {}},
        {"LightUniform", {"common.wgsl"}, "Light", {"rendering/scene_renderer.hpp"}, "LightUniform", {}},
        {"TonemapUniforms", {"tonemap.wgsl"}, "TonemapUniforms", {"rendering/scene_renderer.hpp"},
         "TonemapUniforms", {}},
        {"WaterUniforms", {"water.wgsl"}, "WaterUniforms", {"rendering/water_renderer.hpp"}, "WaterUniforms", {}},
        // scene/particles.hpp is in the bundle for `kMaxFieldForces` and `kMaxCurveKeys`: the array
        // extents are written as expressions over them, and resolving those against the header
        // rather than against a number typed here is the point of scraping constants at all.
        {"ParticleUniforms", {"particles.wgsl"}, "Params",
         {"rendering/particle_renderer.hpp", "scene/particles.hpp"}, "ParticleUniforms", {}},
        {"FieldGpu",
         {"fields.wgsl"},
         "FieldGpu",
         {"spatial/field.hpp"},
         "FieldGpu",
         // `type` is `fieldType` in WGSL because `type` is a reserved word there.
         {{"type", {"fieldType"}}}},
        {"FieldBlock",
         {"fields.wgsl"},
         "FieldBlock",
         {"rendering/field_uniforms.hpp", "spatial/field.hpp"},
         "FieldBlock",
         {{"pad", {"pad0", "pad1", "pad2"}}}},
        {"SdfObjectUniforms", {"sdf_raymarch.wgsl"}, "SdfObjectUniforms", {"rendering/sdf_renderer.hpp"},
         "SdfObjectUniforms", {}},
        {"ShadowViewGpu", {"shadows.wgsl"}, "ShadowViewGpu", {"rendering/shadow_math.hpp"}, "ShadowViewGpu", {}},
        {"ShadowUniforms", {"shadows.wgsl"}, "ShadowUniforms", {"rendering/shadow_math.hpp"}, "ShadowUniforms", {}},
        {"GpuLight", {"shadows.wgsl"}, "GpuLight", {"rendering/light_data.hpp"}, "GpuLight", {}},
        {"VolumeUniforms", {"volume.wgsl"}, "VolumeUniforms", {"rendering/volume_renderer.hpp"},
         "VolumeUniforms", {}},
        {"AoUniforms", {"gtao.wgsl"}, "AoUniforms", {"rendering/ao_renderer.hpp"}, "AoUniforms", {}},
        {"ShadowMaskUniforms", {"shadow_mask.wgsl"}, "ShadowMaskUniforms",
         {"rendering/shadow_mask_renderer.hpp"}, "ShadowMaskUniforms", {}},
        {"SimUniforms", {"simulate.wgsl"}, "SimUniforms", {"rendering/simulation.hpp"}, "SimUniforms", {}},
        {"OutputMapUniforms",
         {"output_map.wgsl"},
         "OutputMapUniforms",
         {"rendering/output_mapper.hpp"},
         "OutputMapUniforms",
         // The homography is a 3x4 float array in C++ and three vec4s in the shader.
         {{"inv", {"inv0", "inv1", "inv2"}}}},
        {"MaterialOpGpu", {"material.wgsl"}, "MaterialOpGpu", {"scene/material_program.hpp"}, "MaterialOpGpu", {}},
        {"MaterialLayerGpu", {"material.wgsl"}, "MaterialLayerGpu", {"scene/material_program.hpp"},
         "MaterialLayerGpu", {}},
        {"MaterialProgramGpu", {"material.wgsl"}, "MaterialProgramGpu", {"scene/material_program.hpp"},
         "MaterialProgramGpu", {}},
        // The reference renderer (forensics Phase 2.2) has its own `ObjectUniforms` in a .cpp file
        // and its own three-field shader. It is in the audit because "the simplest thing that can
        // put geometry in the right pixels" is only a usable control if its uniforms are right.
        {"reference ObjectUniforms", {"reference.wgsl"}, "Object", {"rendering/reference_renderer.cpp"},
         "ObjectUniforms", {}},
    };
    return kPairings;
}

} // namespace

// ---- Phase 3.3: the model is anchored to the compiler -------------------------------------------

// Nothing below this line is worth anything if the layout model is wrong, and the model is a
// hand-written reading of two languages' rules. Every structure the unit binary can include is
// therefore laid out twice -- once by the parser, once by the compiler -- and the two are compared.
// The structures that need `<webgpu/webgpu_cpp.h>` cannot be included here (avgen_tests links
// avgen_core, not avgen_gpu), so they are the ones the model has to be trusted for; these are what
// earn that trust.
TEST_CASE("the C++ layout model reproduces the compiler's own sizeof and offsetof",
          "[unit][renderer][forensics][layout]") {
    const std::string text = bundle(sourceDir(), {"spatial/field.hpp", "core/wind.hpp", "rendering/shadow_math.hpp",
                                                  "rendering/light_data.hpp", "scene/material_program.hpp",
                                                  "scene/scene_types.hpp"});
    const CppStructs structs = parseCppStructs(text);
    const Constants constants = parseConstants(text);

    const auto layoutOf = [&](const std::string& name) {
        REQUIRE(structs.count(name) == 1); // a renamed struct must fail here, not vanish from the audit
        return cppLayout(name, structs, constants);
    };
    const auto offsetOfMember = [](const Layout& layout, const std::string& name) {
        const auto it = std::find_if(layout.members.begin(), layout.members.end(),
                                     [&](const Member& m) { return m.name == name; });
        REQUIRE(it != layout.members.end());
        return it->offset;
    };

    SECTION("spatial::FieldGpu") {
        const Layout parsed = layoutOf("FieldGpu");
        CHECK(parsed.size == sizeof(spatial::FieldGpu));
        CHECK(parsed.align == alignof(spatial::FieldGpu));
        // The four leading u32s then a mat4 is the one place in this struct where a wrong
        // alignment rule would show, so it is the offset worth naming.
        CHECK(offsetOfMember(parsed, "worldToLocal") == offsetof(spatial::FieldGpu, worldToLocal));
        CHECK(offsetOfMember(parsed, "children") == offsetof(spatial::FieldGpu, children));
        CHECK(offsetOfMember(parsed, "gridRes") == offsetof(spatial::FieldGpu, gridRes));
    }
    SECTION("wind::WindUniforms") {
        const Layout parsed = layoutOf("WindUniforms");
        CHECK(parsed.size == sizeof(wind::WindUniforms));
        CHECK(parsed.align == alignof(wind::WindUniforms));
        CHECK(offsetOfMember(parsed, "turbulence") == offsetof(wind::WindUniforms, turbulence));
    }
    SECTION("rendering::ShadowUniforms") {
        const Layout parsed = layoutOf("ShadowUniforms");
        CHECK(parsed.size == sizeof(rendering::ShadowUniforms));
        CHECK(parsed.align == alignof(rendering::ShadowUniforms));
        // An array of a nested struct: the extent comes from a scraped `kMaxShadowViews`, so this
        // also proves the constant scraper found the right number.
        CHECK(offsetOfMember(parsed, "info") == offsetof(rendering::ShadowUniforms, info));
        CHECK(offsetOfMember(parsed, "splits") == offsetof(rendering::ShadowUniforms, splits));
    }
    SECTION("rendering::GpuLight") {
        const Layout parsed = layoutOf("GpuLight");
        CHECK(parsed.size == sizeof(rendering::GpuLight));
        CHECK(offsetOfMember(parsed, "extra") == offsetof(rendering::GpuLight, extra));
    }
    SECTION("scene::MaterialProgramGpu") {
        // The largest packed structure the engine has, and the one where an extent expression
        // (`std::array<MaterialOpGpu, kMaxMaterialOps>`) does the most work.
        const Layout parsed = layoutOf("MaterialProgramGpu");
        CHECK(parsed.size == sizeof(scene::MaterialProgramGpu));
        CHECK(parsed.align == alignof(scene::MaterialProgramGpu));
        CHECK(offsetOfMember(parsed, "layers") == offsetof(scene::MaterialProgramGpu, layers));
        CHECK(offsetOfMember(parsed, "ops") == offsetof(scene::MaterialProgramGpu, ops));
    }
    SECTION("scene::Vertex") {
        // The vertex buffer's element, whose `arrayStride` the pipelines take from `sizeof`. Three
        // `glm::vec3`s and a `glm::vec2` -- the one struct here that is *not* 16-byte aligned, and
        // the check that the model is not simply rounding everything up to 16.
        const Layout parsed = layoutOf("Vertex");
        CHECK(parsed.size == sizeof(scene::Vertex));
        CHECK(parsed.align == alignof(scene::Vertex));
        CHECK(offsetOfMember(parsed, "uv") == offsetof(scene::Vertex, uv));
    }
}

// ---- Phase 3.3: the pairing ---------------------------------------------------------------------

// These guard the C++ side of the structures the model cannot include, by pinning what the
// compiler already knows about the ones it can. The plan's open item is "alignment/padding", and
// alignment is the half that had no guard anywhere: a `glm::vec3` or a stray `float` at the head of
// any of these would keep the size assert passing and move every following field.
//
// **A finding, recorded here because it is the natural place to look for it.** Only the structures
// written `struct alignas(16)` actually align to 16. `glm::vec4` aligns to 4 in this build --
// GLM_FORCE_ALIGNED_GENTYPES is not set and nothing here asks for it -- so a struct made only of
// vec4s and mat4s reports `alignof == 4`. That is harmless today because every one of them is
// sized to a 16-byte multiple and uploaded at offset 0 or at a 256-byte dynamic offset, so no
// instance ever lands off a 16-byte boundary. It is not harmless in principle, and the two asserts
// that would have caught it are the two below: a *size* that stays a 16-byte multiple, and offsets
// that stay where the shader reads them.
static_assert(alignof(spatial::FieldGpu) == 16);
static_assert(alignof(scene::MaterialProgramGpu) == 16);
static_assert(alignof(scene::MaterialOpGpu) == 16);
static_assert(alignof(scene::MaterialLayerGpu) == 16);
static_assert(sizeof(wind::WindUniforms) % 16 == 0);
static_assert(sizeof(rendering::ShadowViewGpu) % 16 == 0);
static_assert(sizeof(spatial::FieldGpu) % 16 == 0);
static_assert(sizeof(rendering::ShadowUniforms) % 16 == 0);
static_assert(sizeof(rendering::GpuLight) % 16 == 0);
static_assert(sizeof(scene::MaterialProgramGpu) % 16 == 0);
// The offsets `shaders/shadows.wgsl` and `shaders/fields.wgsl` read these at. Sizes were already
// asserted; these are the field *positions*, which a reordering would move and a size assert would
// not notice.
static_assert(offsetof(rendering::ShadowUniforms, info) == 640);
static_assert(offsetof(rendering::ShadowUniforms, splits) == 656);
static_assert(offsetof(rendering::ShadowViewGpu, params) == 64);
static_assert(offsetof(rendering::GpuLight, colorIntensity) == 32);
static_assert(offsetof(rendering::GpuLight, extra) == 112);
static_assert(offsetof(spatial::FieldGpu, worldToLocal) == 16);
static_assert(offsetof(spatial::FieldGpu, gridRes) == 352);
static_assert(offsetof(scene::MaterialProgramGpu, layers) == 80);
static_assert(offsetof(scene::MaterialProgramGpu, ops) == 336);
static_assert(offsetof(scene::MaterialOpGpu, constant) == 48);

TEST_CASE("every shared uniform structure has the layout its WGSL twin declares",
          "[unit][renderer][forensics][layout]") {
    for (const Pairing& pairing : pairings()) {
        DYNAMIC_SECTION(pairing.label) {
            const std::string wgslText = bundle(shaderDir(), pairing.shaders);
            const std::string cppText = bundle(sourceDir(), pairing.headers);
            const StructBodies wgslStructs = parseWgslStructs(wgslText);
            const CppStructs cppStructs = parseCppStructs(cppText);
            // Both declarations have to be *found* before anything is claimed about them; a
            // renamed or deleted struct must break the audit rather than quietly shrink it.
            REQUIRE(wgslStructs.count(pairing.wgslName) == 1);
            REQUIRE(cppStructs.count(pairing.cppName) == 1);

            comparePairing(pairing.label, wgslLayout(pairing.wgslName, wgslStructs),
                           cppLayout(pairing.cppName, cppStructs, parseConstants(cppText)), pairing.aliases);
        }
    }
}

TEST_CASE("no shared structure holds a member that straddles a 16-byte row",
          "[unit][renderer][forensics][layout]") {
    // WGSL lays a uniform structure out in 16-byte rows and will not let a member cross one; C++
    // will, because glm's vectors are only 4-aligned here. So the failure this catches is the
    // stray leading scalar: put a `float` at the head of `ObjectUniforms` and the C++ `model`
    // matrix moves to offset 4 while the shader's stays at 16, and every subsequent member is read
    // four bytes early. The pairing test above catches it too, by disagreeing with the shader; this
    // one catches it without needing the shader, and says *which* member went wrong.
    //
    // Members wider than a row (matrices, arrays, nested structs) are excluded because crossing is
    // what they do; their own leaves are covered by the recursion that produced them.
    for (const Pairing& pairing : pairings()) {
        DYNAMIC_SECTION(pairing.label) {
            const std::string cppText = bundle(sourceDir(), pairing.headers);
            const CppStructs cppStructs = parseCppStructs(cppText);
            REQUIRE(cppStructs.count(pairing.cppName) == 1);
            const CppStruct& declared = cppStructs.at(pairing.cppName);
            REQUIRE_FALSE(declared.fields.empty());

            // A `glm::vec3` is the other way in: WGSL pads it to 16 bytes and glm leaves it at 12,
            // so the two sides disagree about every byte after it while both still look reasonable.
            // None of the shared structures has one, and this is what keeps it that way.
            for (const CppField& field : declared.fields) {
                INFO("member " << field.name << " of type " << field.type);
                CHECK(field.type != "glm::vec3");
            }

            const Layout layout = cppLayout(pairing.cppName, cppStructs, parseConstants(cppText));
            REQUIRE_FALSE(layout.members.empty());
            for (const Member& member : layout.members) {
                if (member.size > 16) {
                    continue;
                }
                INFO("member " << member.name << " at " << member.offset << " is " << member.size << " bytes");
                CHECK(member.offset / 16 == (member.offset + member.size - 1) / 16);
            }
        }
    }
}

TEST_CASE("every dynamic-offset stride is a 256-byte multiple large enough for its record",
          "[unit][renderer][forensics][layout]") {
    // WebGPU's `minUniformBufferOffsetAlignment` is 256 on every backend this engine runs on, and a
    // slot that is not a multiple of it is a validation error at bind time rather than a wrong
    // pixel. The second half of the claim -- that the slot is no smaller than what is written into
    // it -- is the one that would corrupt the *next* object's uniforms rather than fail, and it is
    // checked against the size the audit computed rather than against a number retyped here.
    struct StrideCase {
        std::string label;
        std::string header;   // where the constant is declared
        std::string constant;
        std::string record;   // the struct it carries, empty when the header holds no single one
    };
    const std::vector<StrideCase> cases = {
        {"SceneRenderer object slot", "rendering/scene_renderer.hpp", "kObjectStride", "ObjectUniforms"},
        {"SdfRenderer object slot", "rendering/sdf_renderer.hpp", "kObjectStride", "SdfObjectUniforms"},
        {"WaterRenderer material slot", "rendering/water_renderer.hpp", "kStride", "WaterUniforms"},
        {"ReferenceRenderer object slot", "rendering/reference_renderer.cpp", "kStride", "ObjectUniforms"},
        {"Simulation uniform slot", "rendering/simulation.hpp", "kUniformStride", "SimUniforms"},
        {"OutputMapper slot", "rendering/output_mapper.hpp", "kSlotStride", "OutputMapUniforms"},
        {"PostProcessor slot", "rendering/post_processor.hpp", "kSlotStride", "Uniforms"},
        {"MaterialSelect slot", "rendering/material_programs.hpp", "kSelectStride", "MaterialSelect"},
        {"Environment uniform slot", "rendering/environment.hpp", "kUniformStride", "EnvUniforms"},
    };
    for (const StrideCase& c : cases) {
        DYNAMIC_SECTION(c.label) {
            const std::string text = bundle(sourceDir(), {c.header});
            const Constants constants = parseConstants(text);
            const auto stride = constants.find(c.constant);
            REQUIRE(stride != constants.end()); // a renamed constant must fail, not skip the check
            CHECK(stride->second % 256 == 0);

            const CppStructs structs = parseCppStructs(text);
            REQUIRE(structs.count(c.record) == 1);
            const std::size_t size = cppLayout(c.record, structs, constants).size;
            INFO(c.record << " is " << size << " bytes in a " << stride->second << "-byte slot");
            CHECK(static_cast<long long>(size) <= stride->second);
        }
    }
}

// ---- Phase 9.3: which doors are open -------------------------------------------------------------

namespace {

constexpr float kNaN = std::numeric_limits<float>::quiet_NaN();
constexpr float kInf = std::numeric_limits<float>::infinity();

bool finite(const glm::vec3& v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }
bool finite(const glm::vec4& v) {
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z) && std::isfinite(v.w);
}
bool finite(const glm::mat4& m) {
    for (int column = 0; column < 4; ++column) {
        if (!finite(m[column])) {
            return false;
        }
    }
    return true;
}

// A unit cube with one extra vertex on a spike above it, wired into a one-entity scene. The spike
// is the vertex the tests poison, because it is the only one whose loss the bounds can show:
// poisoning a corner of a symmetric box changes nothing that can be measured. Building the scene by
// hand rather than through a composition keeps the poison exactly where it was put -- a composition
// rebuilds its entities from parameters every update and would overwrite it.
scene::Scene cubeWithSpike(const glm::vec3& spike) {
    scene::Scene out;
    scene::MeshData mesh;
    mesh.name = "cube";
    for (int i = 0; i < 8; ++i) {
        const glm::vec3 corner((i & 1) != 0 ? 0.5f : -0.5f, (i & 2) != 0 ? 0.5f : -0.5f,
                               (i & 4) != 0 ? 0.5f : -0.5f);
        mesh.vertices.push_back({corner, glm::vec3(0.0f, 1.0f, 0.0f), glm::vec2(0.0f)});
    }
    mesh.vertices.push_back({spike, glm::vec3(0.0f, 1.0f, 0.0f), glm::vec2(0.0f)});
    mesh.indices = {0, 1, 2, 2, 1, 3, 4, 6, 5, 5, 6, 7, 0, 4, 1, 1, 4, 5,
                    2, 3, 6, 6, 3, 7, 0, 2, 4, 4, 2, 6, 1, 5, 3, 3, 5, 7, 3, 7, 8};
    const scene::MeshId id = out.addMesh(std::move(mesh));
    scene::Entity& entity = out.addEntity("cube", id);
    entity.transform.position = glm::vec3(0.0f, 1.0f, 0.0f);
    return out;
}

} // namespace

TEST_CASE("a non-finite vertex is dropped from the bounds rather than reported",
          "[unit][renderer][forensics][layout][guards]") {
    // Recorded as found, and it is not what the plan's wording expects. `MeshData::bounds()` walks
    // the vertices through `glm::min`/`glm::max`, which are `(y < x) ? y : x` and `(x < y) ? y : x`
    // -- and every comparison against a NaN is false, so the accumulator is returned unchanged.
    //
    // So a NaN does *not* propagate into the box. It is silently *excluded* from it, and the box
    // that comes out is perfectly finite and too small. That is worse than propagation for the
    // purpose Phase 9.3 has in mind: a finite guard downstream can never fire, because there is
    // nothing non-finite left to find. The entity is drawn with a box that does not contain it, and
    // the cull, the shadow cascade sizing and the framing all believe it.
    SECTION("a finite spike is inside the world bounds") {
        const scene::Scene clean = cubeWithSpike(glm::vec3(0.0f, 50.0f, 0.0f));
        const auto [lo, hi] = clean.bounds();
        REQUIRE(finite(lo));
        REQUIRE(finite(hi));
        REQUIRE(hi.y > 50.0f); // the entity sits at y = 1, so the spike reaches 51
    }
    SECTION("a NaN spike leaves finite bounds that no longer contain the mesh") {
        const scene::Scene poisoned = cubeWithSpike(glm::vec3(0.0f, kNaN, 0.0f));
        const auto [lo, hi] = poisoned.bounds();
        CHECK(finite(lo));
        CHECK(finite(hi));
        // Exactly the cube, whose entity sits at y = 1: the spike vanished without a word. The
        // bracket is tight on purpose -- an implementation that let the NaN through would leave the
        // accumulator at its `lowest()` seed, which is also "less than the spike" and is not this.
        CHECK(lo.y > 0.4f);
        CHECK(lo.y < 0.6f);
        CHECK(hi.y > 1.4f);
        CHECK(hi.y < 1.6f);
    }
    SECTION("the cascade fit passes a non-finite caster distance straight into the shadow matrix") {
        // The other end of the same chain, checked separately because the bounds turned out not to
        // feed it. Whatever route a non-finite scene radius takes -- a camera the guard has not
        // reached yet, an authored far plane, an entity transform -- `fitDirectionalCascade` does
        // not test it, and `ShadowViewGpu` is filled from the result. `finiteMatrix` in
        // scene_renderer.cpp covers the camera and the entity matrices; nothing covers this one.
        const glm::mat4 viewProj =
            glm::perspective(1.0f, 16.0f / 9.0f, 0.1f, 200.0f) *
            glm::lookAt(glm::vec3(0.0f, 4.0f, 10.0f), glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
        const glm::mat4 invViewProj = glm::inverse(viewProj);
        const glm::vec3 sun(-0.4f, -1.0f, -0.35f);

        const rendering::ShadowView clean =
            rendering::fitDirectionalCascade(invViewProj, 0.1f, 200.0f, 0.1f, 30.0f, sun, 1024, 40.0f);
        REQUIRE(finite(clean.viewProj));

        const rendering::ShadowView poisoned =
            rendering::fitDirectionalCascade(invViewProj, 0.1f, 200.0f, 0.1f, 30.0f, sun, 1024, kNaN);
        CHECK_FALSE(finite(poisoned.viewProj));
        const bool scalesFinite = std::isfinite(poisoned.texelWorldSize) && std::isfinite(poisoned.depthRange);
        CHECK_FALSE(scalesFinite);
    }
}

TEST_CASE("a non-finite transform is refused rather than collapsing the cull box",
          "[unit][renderer][forensics][layout][guards]") {
    // `entityCullBounds` (ADR-046) is what decides whether an entity is offered to the camera pass
    // at all. It seeds its accumulators with `FLT_MAX` and `lowest()` and folds the eight
    // transformed corners in with `glm::min`/`glm::max`.
    //
    // An infinite scale makes `glm::scale` compute `0 * inf`, which is NaN, so every corner comes
    // out NaN in every lane -- and by the comparison rule above, *every* fold is refused. The box
    // that is returned is the untouched seed: min = FLT_MAX, max = lowest. It is finite, so no
    // finite guard can see it; it is inverted, so it intersects nothing and contains nothing; and
    // the entity's own name never appears anywhere. This is the shape of a "the object is just
    // gone" report, and it is the reason the guard Phase 9.3 asks for has to test for a *valid*
    // box rather than for a finite one.
    SECTION("a finite scene gives a box that contains its entity") {
        const scene::Scene clean = cubeWithSpike(glm::vec3(0.0f, 2.0f, 0.0f));
        const scene::CullBounds bounds = scene::entityCullBounds(clean, clean.entities.front());
        REQUIRE(finite(bounds.min));
        REQUIRE(finite(bounds.max));
        REQUIRE(bounds.min.y < bounds.max.y);
    }
    SECTION("an infinite scale is refused by name and drawn unculled") {
        // The paragraph above describes what this *used* to do, and the repair is the reason it is
        // written in the past tense. A box that contains nothing is now detected rather than
        // returned: the entity and its transform are named in a warning, and the fallback is a small
        // box at its unposed position, so the object draws in the wrong place instead of vanishing.
        // A thing in the wrong place can be seen and chased; a thing that is not there cannot.
        scene::Scene poisoned = cubeWithSpike(glm::vec3(0.0f, 2.0f, 0.0f));
        poisoned.entities.front().transform.position = glm::vec3(3.0f, 2.0f, -1.0f);
        poisoned.entities.front().transform.scale = glm::vec3(1.0f, kInf, 1.0f);
        const scene::CullBounds bounds = scene::entityCullBounds(poisoned, poisoned.entities.front());
        CHECK(finite(bounds.min));
        CHECK(finite(bounds.max));
        // Valid, and around where the entity actually is.
        CHECK(bounds.min.x <= bounds.max.x);
        CHECK(bounds.min.y <= bounds.max.y);
        CHECK(bounds.min.z <= bounds.max.z);
        CHECK(bounds.min.x <= 3.0f);
        CHECK(bounds.max.x >= 3.0f);
        CHECK(bounds.min.y <= 2.0f);
        CHECK(bounds.max.y >= 2.0f);
    }
}

TEST_CASE("a non-finite light is refused rather than packed into the GPU light record",
          "[unit][renderer][forensics][layout][guards]") {
    // `packLight` clamps and normalises -- `std::max(light.intensity, 0.0f)`, `safeNormalize`,
    // `std::clamp(shadowStrength, 0, 1)` -- and every one of those is a comparison that a NaN wins
    // by being incomparable. The result goes straight into the light storage buffer.
    scene::PunctualLight light;
    light.name = "key";
    light.type = scene::PunctualLight::Type::Point;
    light.position = glm::vec3(0.0f, 3.0f, 0.0f);

    SECTION("a well-formed light packs finite") {
        const rendering::GpuLight packed = rendering::packLight(light);
        REQUIRE(finite(packed.positionType));
        REQUIRE(finite(packed.colorIntensity));
        REQUIRE(finite(packed.cone));
    }
    // The three doors, now shut. Each was open when this test was written: the clamps are
    // comparisons and a NaN wins every one by being incomparable, so the value went into the light
    // storage buffer verbatim and a whole frame came out NaN wherever that light reached.
    //
    // Dropped rather than corrected. A light nobody can see is a missing light, which somebody can
    // look for; a light that quietly became a different light is a wrong picture nobody can explain.
    SECTION("a NaN intensity is refused") {
        light.intensity = kNaN;
        const rendering::GpuLight packed = rendering::packLight(light);
        CHECK(finite(packed.colorIntensity));
        CHECK(packed.colorIntensity.w == 0.0f);   // no influence radius: the shader skips it
    }
    SECTION("a NaN position is refused") {
        light.position.y = kNaN;
        const rendering::GpuLight packed = rendering::packLight(light);
        CHECK(finite(packed.positionType));
        CHECK(finite(packed.colorIntensity));
    }
    SECTION("an infinite range is refused") {
        light.range = kInf;
        const rendering::GpuLight packed = rendering::packLight(light);
        CHECK(finite(packed.directionRange));
        CHECK(finite(packed.colorIntensity));
        // `lightInfluenceRadius` is still infinite for such a light -- it is a pure function of the
        // authored range and is not the guard's business. What matters is that it no longer reaches
        // the packed record.
        CHECK_FALSE(std::isfinite(rendering::lightInfluenceRadius(light)));
    }
}

TEST_CASE("a non-finite material-program constant is packed into the storage buffer unchanged",
          "[unit][renderer][forensics][layout][guards]") {
    // The packed GPU structure the plan names explicitly. A material program's constants are
    // authored numbers that arrive from JSON, an AI edit or a parameter, and `packMaterialProgram`
    // copies them into `MaterialProgramGpu` without looking at them.
    scene::MaterialProgram program;
    program.name = "glow";
    scene::MaterialOp op;
    op.kind = scene::MaterialOpKind::Constant;
    op.constant = glm::vec4(0.5f, 0.5f, 0.5f, 1.0f);
    op.dst = 0;
    program.ops.push_back(op);
    program.baseColorRegister = 0;

    SECTION("a finite constant packs finite") {
        const scene::MaterialProgramGpu packed = scene::packMaterialProgramWithSlots(program, {});
        REQUIRE(finite(packed.ops[0].constant));
    }
    SECTION("a NaN constant is packed verbatim") {
        program.ops[0].constant.g = kNaN;
        const scene::MaterialProgramGpu packed = scene::packMaterialProgramWithSlots(program, {});
        CHECK_FALSE(finite(packed.ops[0].constant));
    }
}

TEST_CASE("a non-finite field strength is packed into the field block unchanged",
          "[unit][renderer][forensics][layout][guards]") {
    // The other packed GPU structure with an authored number in it. `packField` builds the record
    // the SDF, particle and material paths all sample; a NaN here propagates to every shader that
    // reads the field rather than to one object.
    spatial::FieldSpec field;
    field.name = "pulse";
    field.kind = spatial::FieldKind::Constant;

    SECTION("a finite strength packs finite") {
        const spatial::FieldGpu packed = spatial::packField(field, 0.0);
        REQUIRE(finite(packed.strengthInnerOuterTau));
    }
    SECTION("a NaN strength is packed verbatim") {
        field.strength = kNaN;
        const spatial::FieldGpu packed = spatial::packField(field, 0.0);
        CHECK_FALSE(finite(packed.strengthInnerOuterTau));
    }
}

// ---- shared scalar constants: the other half of the CPU/WGSL pairing -----------------------------
//
// The struct guards above check that two *layouts* describe the same bytes. This checks the other
// thing that is written down twice: a **constant** that C++ and WGSL both hardcode.
//
// The existing guard on `kMaxLodLevels` is
//     static_assert(scene::kMaxLodLevels == 4, "shaders/cull.wgsl hardcodes kMaxLodLevels ...")
// which fires when the C++ value changes and is silent when the *shader* changes -- and the shader
// is the copy a person is more likely to edit while looking at shader code. It is also the exact
// shape this repository's working rules warn against: a transcription of a constant into an
// assertion passes for as long as both copies are edited together, and fails only when someone
// edits one.
//
// So these read the number out of the WGSL and compare it to the C++ symbol. No third copy is
// written down here: if the shader says 4 and C++ says 4, the test passes without either number
// appearing in this file.
//
// Found during the renderer upgrade audit: `shaders/spline.wgsl` carries an **unguarded** second
// copy of `kMaxGpuSplines` as an array extent, with nothing to catch the two drifting apart.
// Glowmere already logs "22 splines; only the first 16 are available on the GPU", so the limit is
// user-visible today and a silent mismatch would be a wrong picture rather than a validation error.
namespace {

// `const NAME: u32 = 4u;` -> 4
std::optional<std::size_t> wgslConst(const std::string& text, const std::string& name) {
    const std::regex re("const\\s+" + name + R"(\s*:\s*u32\s*=\s*(\d+)u?\s*;)");
    std::smatch m;
    if (!std::regex_search(text, m, re)) {
        return std::nullopt;
    }
    return static_cast<std::size_t>(std::stoul(m[1].str()));
}

// `field: array<TYPE, 16>,` -> 16. The element type may itself be generic (`vec4<f32>`), so the
// nesting has to be tolerated -- a plain `[^,>]+` stops at the inner `>` and matches nothing, which
// is what the control section below caught on the first run.
std::optional<std::size_t> wgslArrayExtent(const std::string& text, const std::string& field) {
    const std::regex re(field + R"(\s*:\s*array\s*<(?:[^<>]|<[^<>]*>)*,\s*(\d+)\s*>)");
    std::smatch m;
    if (!std::regex_search(text, m, re)) {
        return std::nullopt;
    }
    return static_cast<std::size_t>(std::stoul(m[1].str()));
}

} // namespace

TEST_CASE("a constant written down in both C++ and WGSL agrees with itself",
          "[unit][renderer][forensics][layout]") {
    SECTION("kMaxLodLevels: cull.wgsl strides the shared indirect buffer by it") {
        const auto text = readFile(shaderDir() / "cull.wgsl");
        REQUIRE(text.has_value());
        const auto shader = wgslConst(stripComments(*text), "kMaxLodLevels");
        INFO("shaders/cull.wgsl must declare `const kMaxLodLevels: u32 = <n>u;`");
        REQUIRE(shader.has_value());
        CHECK(*shader == static_cast<std::size_t>(scene::kMaxLodLevels));
    }

    SECTION("kMaxGpuSplines: spline.wgsl sizes its table by it") {
        // This pairing had no guard at all until the upgrade audit found it.
        const auto text = readFile(shaderDir() / "spline.wgsl");
        REQUIRE(text.has_value());
        const std::string body = stripComments(*text);
        const auto info = wgslArrayExtent(body, "info");
        INFO("shaders/spline.wgsl must declare `info: array<vec4<f32>, <n>>` in SplineTable");
        REQUIRE(info.has_value());

        // The C++ side is *scraped from the header text*, not included: `spline_buffers.hpp` pulls
        // in <webgpu/webgpu_cpp.h>, which this deliberately GPU-free binary cannot link. Scraping is
        // also the more honest comparison -- two declarations read the same way, with no third copy
        // of the number written down in this file.
        const auto header = readFile(sourceDir() / "rendering" / "spline_buffers.hpp");
        REQUIRE(header.has_value());
        const Constants cpp = parseConstants(stripComments(*header));
        const auto it = cpp.find("kMaxGpuSplines");
        INFO("src/rendering/spline_buffers.hpp must declare kMaxGpuSplines");
        REQUIRE(it != cpp.end());
        CHECK(static_cast<long long>(*info) == it->second);
    }

    SECTION("the parsers actually parse, rather than returning nothing and passing") {
        // Without this, a regex that matched nothing would make both sections above vacuous -- the
        // REQUIREs would fire, but a future edit that renamed a field would look like a parser
        // problem rather than a drift. These are the controls for the two readers.
        const std::string sample = "const kThing: u32 = 7u;\nstruct S { info: array<vec4<f32>, 12>, };";
        CHECK(wgslConst(sample, "kThing") == std::optional<std::size_t>(7));
        CHECK(wgslArrayExtent(sample, "info") == std::optional<std::size_t>(12));
        CHECK_FALSE(wgslConst(sample, "kAbsent").has_value());
        CHECK_FALSE(wgslArrayExtent(sample, "absent").has_value());
    }
}
