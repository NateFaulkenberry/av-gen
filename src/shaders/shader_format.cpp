#include "shaders/shader_format.hpp"

#include "core/log.hpp"

#include <fmt/format.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <limits>
#include <nlohmann/json.hpp>
#include <optional>
#include <string_view>
#include <utility>

// Implementation notes (the contract itself is documented in shader_format.hpp):
//
// Header parsing
//   * Leading whitespace and `//` comment lines are skipped; the header must then start with the
//     exact sequence `/*{`. The header ends at the first occurrence of `}*/` (JSON strings may not
//     contain that sequence; braces inside strings are fine because the JSON parser, not a brace
//     counter, delimits the object).
//   * The body is everything after `}*/`, verbatim (usually starting with the newline that follows
//     the header). `bodyLineOffset` is the 1-based source line on which the body starts, i.e. the
//     line that holds `}*/` when the body begins right after it.
//   * ISF semantics: the final pass renders to the output. When PASSES is empty, or when the last
//     pass names a TARGET (the author forgot the output pass), a default output pass is appended
//     rather than failing. An output pass (no TARGET) anywhere but last is rejected because it
//     would be overwritten silently.
//   * DEFAULT/MIN/MAX accept a scalar (broadcast to every component) or an array whose length must
//     equal the component count. Missing values fall back to the type defaults: DEFAULT is zeros
//     (bool false, color opaque white), MIN is zeros, MAX is 1 (long 10). bool and event ignore
//     MIN/MAX and are stored as [0, 1] so every input carries a full description.
//
// Generated module
//   Every generated declaration precedes the body and the prologue ends with the marker line
//   `// ---- user body ----`. The first generated line states the prologue length, so a Dawn line
//   number L maps to body line L - prologueLines and to source line
//   bodyLineOffset + (L - prologueLines) - 1.

namespace avgen::shaders {

using nlohmann::json;

namespace {

constexpr std::string_view kHeaderStart = "/*{";
constexpr std::string_view kHeaderEnd = "}*/";
constexpr std::string_view kBodyMarker = "// ---- user body ----";

// WGSL keywords, reserved words that matter in practice, predeclared type names, and the
// identifiers the generated prologue and entry points introduce.
// clang-format off
constexpr auto kReservedIdentifiers = std::to_array<std::string_view>({
    // generated declarations
    "Std", "Inputs", "VsOut", "sys", "std", "inputs", "linearSampler", "inputImage", "audioSpectrum",
    "mainImage", "vs_main", "fs_main",
    // keywords
    "alias", "break", "case", "const", "const_assert", "continue", "continuing", "default",
    "diagnostic", "discard", "else", "enable", "false", "fn", "for", "if", "let", "loop", "override",
    "requires", "return", "struct", "switch", "true", "var", "while",
    // reserved words and attribute / address-space / access-mode names
    "binding", "bitcast", "compute", "do", "enum", "fragment", "function", "group", "import", "in",
    "location", "macro", "module", "namespace", "new", "null", "operator", "private", "ptr", "read",
    "read_write", "ref", "static", "storage", "typedef", "uniform", "using", "vertex", "virtual",
    "workgroup", "write", "yield",
    // predeclared types
    "array", "atomic", "bool", "f16", "f32", "i32", "u32", "sampler", "sampler_comparison",
    "mat2x2", "mat2x3", "mat2x4", "mat3x2", "mat3x3", "mat3x4", "mat4x2", "mat4x3", "mat4x4",
    "vec2", "vec2f", "vec2i", "vec2u", "vec3", "vec3f", "vec3i", "vec3u", "vec4", "vec4f", "vec4i",
    "vec4u", "texture_1d", "texture_2d", "texture_2d_array", "texture_3d", "texture_cube",
    "texture_cube_array", "texture_depth_2d", "texture_depth_2d_array", "texture_depth_cube",
    "texture_depth_cube_array", "texture_depth_multisampled_2d", "texture_external",
    "texture_multisampled_2d", "texture_storage_1d", "texture_storage_2d",
    "texture_storage_2d_array", "texture_storage_3d",
});
// clang-format on

bool isReservedIdentifier(std::string_view name) {
    return std::find(kReservedIdentifiers.begin(), kReservedIdentifiers.end(), name) !=
           kReservedIdentifiers.end();
}

bool isIdentifierStart(char c) {
    return std::isalpha(static_cast<unsigned char>(c)) != 0 || c == '_';
}

bool isIdentifierChar(char c) {
    return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_';
}

// Letters, digits and underscores; must not start with a digit; must not be a lone underscore;
// must not collide with WGSL keywords or generated declarations.
Result<void> validateIdentifier(const std::string& name, std::string_view what,
                                const std::string& shaderName) {
    if (name.empty()) {
        return fail("shader '{}': {} name is empty", shaderName, what);
    }
    if (!isIdentifierStart(name.front()) || name == "_") {
        return fail("shader '{}': {} name '{}' is not a valid identifier", shaderName, what, name);
    }
    if (!std::all_of(name.begin(), name.end(), isIdentifierChar)) {
        return fail("shader '{}': {} name '{}' is not a valid identifier", shaderName, what, name);
    }
    if (name.size() >= 2 && name[0] == '_' && name[1] == '_') {
        return fail("shader '{}': {} name '{}' may not start with two underscores", shaderName, what, name);
    }
    if (isReservedIdentifier(name)) {
        return fail("shader '{}': {} name '{}' is a WGSL keyword or a reserved engine identifier", shaderName,
                    what, name);
    }
    return {};
}

std::vector<float> filled(std::size_t count, float value) {
    return std::vector<float>(count, value);
}

std::vector<float> typeDefaultValue(InputType type) {
    switch (type) {
    case InputType::Color:
        return {1.0f, 1.0f, 1.0f, 1.0f};
    case InputType::Point2D:
        return filled(2, 0.0f);
    case InputType::Float:
    case InputType::Long:
    case InputType::Bool:
    case InputType::Event:
        return {0.0f};
    }
    return {0.0f};
}

std::vector<float> typeMinValue(InputType type) {
    return filled(InputDesc{.type = type}.componentCount(), 0.0f);
}

std::vector<float> typeMaxValue(InputType type) {
    switch (type) {
    case InputType::Long:
        return {10.0f};
    case InputType::Color:
        return filled(4, 1.0f);
    case InputType::Point2D:
        return filled(2, 1.0f);
    case InputType::Float:
    case InputType::Bool:
    case InputType::Event:
        return {1.0f};
    }
    return {1.0f};
}

// Reads a scalar (number or bool) or an array of numbers/bools into `count` components.
Result<std::vector<float>> readComponents(const json& value, std::size_t count, std::string_view key,
                                          const std::string& inputName, const std::string& shaderName) {
    auto scalar = [&](const json& v) -> std::optional<float> {
        if (v.is_number()) {
            return v.get<float>();
        }
        if (v.is_boolean()) {
            return v.get<bool>() ? 1.0f : 0.0f;
        }
        return std::nullopt;
    };
    if (const auto s = scalar(value)) {
        return filled(count, *s);
    }
    if (value.is_array()) {
        if (value.size() != count) {
            return fail("shader '{}': input '{}' {} has {} components, expected {}", shaderName, inputName,
                        key, value.size(), count);
        }
        std::vector<float> out;
        out.reserve(count);
        for (const json& element : value) {
            const auto s = scalar(element);
            if (!s) {
                return fail("shader '{}': input '{}' {} must contain only numbers", shaderName, inputName,
                            key);
            }
            out.push_back(*s);
        }
        return out;
    }
    return fail("shader '{}': input '{}' {} must be a number or an array of numbers", shaderName, inputName,
                key);
}

Result<InputDesc> parseInput(const json& entry, const std::string& shaderName) {
    if (!entry.is_object()) {
        return fail("shader '{}': every INPUTS entry must be an object", shaderName);
    }
    InputDesc input;
    const auto nameIt = entry.find("NAME");
    if (nameIt == entry.end() || !nameIt->is_string()) {
        return fail("shader '{}': every INPUTS entry needs a string NAME", shaderName);
    }
    input.name = nameIt->get<std::string>();
    if (auto ok = validateIdentifier(input.name, "input", shaderName); !ok) {
        return std::unexpected(ok.error());
    }
    const auto typeIt = entry.find("TYPE");
    if (typeIt == entry.end() || !typeIt->is_string()) {
        return fail("shader '{}': input '{}' needs a string TYPE", shaderName, input.name);
    }
    const auto type = inputTypeFromName(typeIt->get<std::string>());
    if (!type) {
        return fail("shader '{}': input '{}': {}", shaderName, input.name, type.error().message);
    }
    input.type = *type;

    input.label = input.name;
    if (const auto labelIt = entry.find("LABEL"); labelIt != entry.end() && labelIt->is_string()) {
        input.label = labelIt->get<std::string>();
    }

    const std::size_t count = input.componentCount();
    input.defaultValue = typeDefaultValue(input.type);
    input.minValue = typeMinValue(input.type);
    input.maxValue = typeMaxValue(input.type);

    // Events carry no user-facing range or default; bools have a fixed [0, 1] range.
    const bool hasRange = input.type != InputType::Event && input.type != InputType::Bool;
    const bool hasDefault = input.type != InputType::Event;
    struct Slot {
        const char* key;
        std::vector<float>* target;
        bool enabled;
    };
    const std::array<Slot, 3> slots = {{{"DEFAULT", &input.defaultValue, hasDefault},
                                        {"MIN", &input.minValue, hasRange},
                                        {"MAX", &input.maxValue, hasRange}}};
    for (const Slot& slot : slots) {
        const auto it = entry.find(slot.key);
        if (it == entry.end() || it->is_null()) {
            continue;
        }
        if (!slot.enabled) {
            log::debug("shader '{}': input '{}' ({}) ignores {}", shaderName, input.name,
                       inputTypeName(input.type), slot.key);
            continue;
        }
        auto components = readComponents(*it, count, slot.key, input.name, shaderName);
        if (!components) {
            return std::unexpected(components.error());
        }
        *slot.target = std::move(*components);
    }
    if (input.type == InputType::Bool) {
        input.defaultValue[0] = input.defaultValue[0] >= 0.5f ? 1.0f : 0.0f;
    }
    if (input.type == InputType::Long) {
        for (auto* values : {&input.defaultValue, &input.minValue, &input.maxValue}) {
            (*values)[0] = std::round((*values)[0]);
        }
    }
    return input;
}

Result<std::string> readSizeExpression(const json& entry, const char* key, const std::string& fallback,
                                       const std::string& shaderName) {
    const auto it = entry.find(key);
    if (it == entry.end() || it->is_null()) {
        return fallback;
    }
    std::string expr;
    if (it->is_string()) {
        expr = it->get<std::string>();
    } else if (it->is_number_integer()) {
        expr = std::to_string(it->get<long long>());
    } else if (it->is_number()) {
        expr = fmt::format("{}", it->get<double>());
    } else {
        return fail("shader '{}': pass {} must be a string or a number", shaderName, key);
    }
    // Validate the syntax now so a malformed expression fails at load time, not at render time.
    if (auto value = evaluateSizeExpression(expr, 1920, 1080); !value) {
        return fail("shader '{}': pass {}: {}", shaderName, key, value.error().message);
    }
    return expr;
}

Result<PassDesc> parsePass(const json& entry, const std::string& shaderName) {
    if (!entry.is_object()) {
        return fail("shader '{}': every PASSES entry must be an object", shaderName);
    }
    PassDesc pass;
    if (const auto it = entry.find("TARGET"); it != entry.end() && !it->is_null()) {
        if (!it->is_string()) {
            return fail("shader '{}': pass TARGET must be a string", shaderName);
        }
        pass.target = it->get<std::string>();
        if (!pass.target.empty()) {
            if (auto ok = validateIdentifier(pass.target, "pass target", shaderName); !ok) {
                return std::unexpected(ok.error());
            }
        }
    }
    for (const auto& [key, flag] :
         {std::pair{"PERSISTENT", &pass.persistent}, std::pair{"FLOAT", &pass.floatFormat}}) {
        const auto it = entry.find(key);
        if (it == entry.end() || it->is_null()) {
            continue;
        }
        if (it->is_boolean()) {
            *flag = it->get<bool>();
        } else if (it->is_number()) {
            *flag = it->get<double>() != 0.0; // ISF files commonly write 1/0
        } else {
            return fail("shader '{}': pass {} must be a boolean", shaderName, key);
        }
    }
    auto width = readSizeExpression(entry, "WIDTH", pass.widthExpr, shaderName);
    if (!width) {
        return std::unexpected(width.error());
    }
    auto height = readSizeExpression(entry, "HEIGHT", pass.heightExpr, shaderName);
    if (!height) {
        return std::unexpected(height.error());
    }
    pass.widthExpr = std::move(*width);
    pass.heightExpr = std::move(*height);
    return pass;
}

Result<void> readStringArray(const json& header, const char* key, std::vector<std::string>& out,
                             const std::string& shaderName) {
    const auto it = header.find(key);
    if (it == header.end() || it->is_null()) {
        return {};
    }
    if (!it->is_array()) {
        return fail("shader '{}': {} must be an array of strings", shaderName, key);
    }
    for (const json& element : *it) {
        if (!element.is_string()) {
            return fail("shader '{}': {} must be an array of strings", shaderName, key);
        }
        out.push_back(element.get<std::string>());
    }
    return {};
}

Result<void> readOptionalString(const json& header, const char* key, std::string& out,
                                const std::string& shaderName) {
    const auto it = header.find(key);
    if (it == header.end() || it->is_null()) {
        return {};
    }
    if (!it->is_string()) {
        return fail("shader '{}': {} must be a string", shaderName, key);
    }
    out = it->get<std::string>();
    return {};
}

Result<ShaderDescription> parseHeader(const std::string& headerText, const std::string& name) {
    const json header = json::parse(headerText, nullptr, false);
    if (header.is_discarded()) {
        return fail("shader '{}': invalid JSON header", name);
    }
    if (!header.is_object()) {
        return fail("shader '{}': invalid JSON header (expected an object)", name);
    }

    ShaderDescription description;
    description.name = name;
    if (auto ok = readOptionalString(header, "NAME", description.name, name); !ok) {
        return std::unexpected(ok.error());
    }
    if (description.name.empty()) {
        description.name = name;
    }
    if (auto ok = readOptionalString(header, "DESCRIPTION", description.description, name); !ok) {
        return std::unexpected(ok.error());
    }
    if (auto ok = readOptionalString(header, "CREDIT", description.credit, name); !ok) {
        return std::unexpected(ok.error());
    }
    if (auto ok = readStringArray(header, "CATEGORIES", description.categories, name); !ok) {
        return std::unexpected(ok.error());
    }

    if (const auto it = header.find("INPUTS"); it != header.end() && !it->is_null()) {
        if (!it->is_array()) {
            return fail("shader '{}': INPUTS must be an array", name);
        }
        for (const json& entry : *it) {
            auto input = parseInput(entry, name);
            if (!input) {
                return std::unexpected(input.error());
            }
            const bool duplicate = std::any_of(description.inputs.begin(), description.inputs.end(),
                                               [&](const InputDesc& d) { return d.name == input->name; });
            if (duplicate) {
                return fail("shader '{}': duplicate input name '{}'", name, input->name);
            }
            description.inputs.push_back(std::move(*input));
        }
    }

    if (const auto it = header.find("PASSES"); it != header.end() && !it->is_null()) {
        if (!it->is_array()) {
            return fail("shader '{}': PASSES must be an array", name);
        }
        for (const json& entry : *it) {
            auto pass = parsePass(entry, name);
            if (!pass) {
                return std::unexpected(pass.error());
            }
            if (!pass->target.empty()) {
                const bool duplicate =
                    std::any_of(description.passes.begin(), description.passes.end(),
                                [&](const PassDesc& p) { return p.target == pass->target; });
                if (duplicate) {
                    return fail("shader '{}': duplicate pass target '{}'", name, pass->target);
                }
            }
            description.passes.push_back(std::move(*pass));
        }
    }
    // Only the final pass may render to the output; an earlier output pass would be overwritten.
    for (std::size_t i = 0; i + 1 < description.passes.size(); ++i) {
        if (description.passes[i].target.empty()) {
            return fail("shader '{}': pass {} has no TARGET but is not the last pass", name, i);
        }
    }
    if (description.passes.empty()) {
        description.passes.push_back(PassDesc{});
    } else if (!description.passes.back().target.empty()) {
        log::warn("shader '{}': last pass renders to target '{}'; appending an output pass", name,
                  description.passes.back().target);
        description.passes.push_back(PassDesc{});
    }
    return description;
}

bool bodyDefinesMainImage(const std::string& body) {
    std::size_t pos = 0;
    while ((pos = body.find("fn", pos)) != std::string::npos) {
        const bool startOk = pos == 0 || !isIdentifierChar(body[pos - 1]);
        std::size_t p = pos + 2;
        if (startOk && p < body.size() && std::isspace(static_cast<unsigned char>(body[p])) != 0) {
            while (p < body.size() && std::isspace(static_cast<unsigned char>(body[p])) != 0) {
                ++p;
            }
            constexpr std::string_view kName = "mainImage";
            if (body.compare(p, kName.size(), kName) == 0) {
                const std::size_t end = p + kName.size();
                if (end >= body.size() || !isIdentifierChar(body[end])) {
                    return true;
                }
            }
        }
        pos += 2;
    }
    return false;
}

// Skips whitespace and `//` comment lines; returns the offset of the first significant character.
std::size_t skipLeadingTrivia(const std::string& source) {
    std::size_t pos = 0;
    while (pos < source.size()) {
        if (std::isspace(static_cast<unsigned char>(source[pos])) != 0) {
            ++pos;
        } else if (source.compare(pos, 2, "//") == 0) {
            const std::size_t eol = source.find('\n', pos);
            pos = eol == std::string::npos ? source.size() : eol + 1;
        } else {
            break;
        }
    }
    return pos;
}

std::size_t countLines(const std::string& text) {
    return 1 + static_cast<std::size_t>(std::count(text.begin(), text.end(), '\n'));
}

// ---- size expressions ----------------------------------------------------------------------

std::string_view trim(std::string_view s) {
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front())) != 0) {
        s.remove_prefix(1);
    }
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back())) != 0) {
        s.remove_suffix(1);
    }
    return s;
}

// Accepts `$WIDTH`, `$HEIGHT` or an unsigned decimal literal (digits with an optional fraction).
std::optional<double> parseOperand(std::string_view text, std::uint32_t width, std::uint32_t height) {
    text = trim(text);
    if (text.empty()) {
        return std::nullopt;
    }
    if (text == "$WIDTH") {
        return static_cast<double>(width);
    }
    if (text == "$HEIGHT") {
        return static_cast<double>(height);
    }
    std::size_t digits = 0;
    std::size_t dots = 0;
    for (const char c : text) {
        if (std::isdigit(static_cast<unsigned char>(c)) != 0) {
            ++digits;
        } else if (c == '.') {
            ++dots;
        } else {
            return std::nullopt;
        }
    }
    if (digits == 0 || dots > 1) {
        return std::nullopt;
    }
    const std::string literal(text);
    return std::strtod(literal.c_str(), nullptr);
}

} // namespace

// ---- InputDesc -------------------------------------------------------------------------------

std::size_t InputDesc::componentCount() const {
    switch (type) {
    case InputType::Color:
        return 4;
    case InputType::Point2D:
        return 2;
    case InputType::Float:
    case InputType::Long:
    case InputType::Bool:
    case InputType::Event:
        return 1;
    }
    return 1;
}

std::size_t InputDesc::byteSize() const {
    return componentCount() * 4;
}

std::size_t InputDesc::alignment() const {
    switch (type) {
    case InputType::Color:
        return 16;
    case InputType::Point2D:
        return 8;
    case InputType::Float:
    case InputType::Long:
    case InputType::Bool:
    case InputType::Event:
        return 4;
    }
    return 4;
}

const char* InputDesc::wgslType() const {
    switch (type) {
    case InputType::Float:
    case InputType::Event:
        return "f32";
    case InputType::Long:
        return "i32";
    case InputType::Bool:
        return "u32";
    case InputType::Color:
        return "vec4<f32>";
    case InputType::Point2D:
        return "vec2<f32>";
    }
    return "f32";
}

// ---- parsing ---------------------------------------------------------------------------------

Result<ParsedShader> parseShaderSource(const std::string& source, const std::string& name) {
    ParsedShader parsed;
    const std::size_t start = skipLeadingTrivia(source);
    const bool hasHeader = source.compare(start, kHeaderStart.size(), kHeaderStart) == 0;

    if (hasHeader) {
        const std::size_t jsonStart = start + 2; // keep the '{'
        const std::size_t end = source.find(kHeaderEnd, jsonStart);
        if (end == std::string::npos) {
            return fail("shader '{}': unterminated header (missing '}}*/')", name);
        }
        parsed.header = source.substr(jsonStart, end + 1 - jsonStart); // include the '}'
        const std::size_t bodyStart = end + kHeaderEnd.size();
        parsed.body = source.substr(bodyStart);
        parsed.bodyLineOffset = countLines(source.substr(0, bodyStart));
        auto description = parseHeader(parsed.header, name);
        if (!description) {
            return std::unexpected(description.error());
        }
        parsed.description = std::move(*description);
    } else {
        parsed.body = source;
        parsed.bodyLineOffset = 1;
        parsed.description.name = name;
        parsed.description.passes.push_back(PassDesc{});
    }

    if (!bodyDefinesMainImage(parsed.body)) {
        return fail("shader '{}': body does not define 'fn mainImage(uv: vec2<f32>, fragCoord: vec2<f32>) "
                    "-> vec4<f32>' as the user shader contract requires",
                    name);
    }
    log::debug("shader '{}': {} inputs, {} passes, body at line {}", name, parsed.description.inputs.size(),
               parsed.description.passes.size(), parsed.bodyLineOffset);
    return parsed;
}

Result<ParsedShader> parseShaderFile(const std::filesystem::path& path) {
    std::error_code ec;
    if (!std::filesystem::is_regular_file(path, ec)) {
        return fail("shader file not found: {}", path.string());
    }
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return fail("cannot read shader file: {}", path.string());
    }
    std::string source((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    if (file.bad()) {
        return fail("cannot read shader file: {}", path.string());
    }
    return parseShaderSource(source, path.stem().string());
}

// ---- layout ----------------------------------------------------------------------------------

InputsLayout computeInputsLayout(const ShaderDescription& description) {
    InputsLayout layout;
    layout.offsets.reserve(description.inputs.size());
    std::size_t cursor = 0;
    for (const InputDesc& input : description.inputs) {
        const std::size_t align = input.alignment();
        cursor = (cursor + align - 1) / align * align;
        layout.offsets.push_back(cursor);
        cursor += input.byteSize();
    }
    layout.size = std::max<std::size_t>(16, (cursor + 15) / 16 * 16);
    return layout;
}

void packInputs(const ShaderDescription& description, const InputsLayout& layout,
                const std::vector<std::vector<float>>& values, std::vector<std::uint8_t>& out) {
    out.assign(layout.size, 0);
    const std::size_t count = std::min(description.inputs.size(), layout.offsets.size());
    for (std::size_t i = 0; i < count; ++i) {
        const InputDesc& input = description.inputs[i];
        const std::vector<float>* source = i < values.size() ? &values[i] : nullptr;
        const std::size_t components = input.componentCount();
        if (layout.offsets[i] + components * 4 > out.size()) {
            continue; // layout does not match the description; never write out of bounds
        }
        for (std::size_t c = 0; c < components; ++c) {
            const float value = (source != nullptr && c < source->size()) ? (*source)[c] : 0.0f;
            std::uint8_t* dst = out.data() + layout.offsets[i] + c * 4;
            switch (input.type) {
            case InputType::Bool: {
                const std::uint32_t u = value >= 0.5f ? 1u : 0u;
                std::memcpy(dst, &u, sizeof(u));
                break;
            }
            case InputType::Long: {
                const double rounded = std::round(static_cast<double>(value));
                const double clamped =
                    std::clamp(rounded, static_cast<double>(std::numeric_limits<std::int32_t>::min()),
                               static_cast<double>(std::numeric_limits<std::int32_t>::max()));
                const std::int32_t s = static_cast<std::int32_t>(clamped);
                std::memcpy(dst, &s, sizeof(s));
                break;
            }
            case InputType::Float:
            case InputType::Event:
            case InputType::Color:
            case InputType::Point2D:
                std::memcpy(dst, &value, sizeof(value));
                break;
            }
        }
    }
}

// ---- module generation -----------------------------------------------------------------------

std::string generateModuleSource(const ShaderDescription& description, const std::string& body) {
    std::string prologue;
    auto line = [&prologue](std::string_view text) {
        prologue.append(text);
        prologue.push_back('\n');
    };

    // Line 1 is patched below once the prologue length is known; keep it a single line.
    line("");
    line("struct Std {");
    line("    time: f32,");
    line("    timeDelta: f32,");
    line("    frameIndex: f32,");
    line("    passIndex: f32,");
    line("    renderSize: vec2<f32>,");
    line("    passSize: vec2<f32>,");
    line("    audio: vec4<f32>,");
    line("    audio2: vec4<f32>,");
    line("    beat: vec4<f32>,");
    line("    pad: vec4<f32>,");
    line("};");
    line("@group(0) @binding(0) var<uniform> sys: Std;");
    line("struct Inputs {");
    if (description.inputs.empty()) {
        line("    _pad0: vec4<f32>,");
    }
    for (const InputDesc& input : description.inputs) {
        line(fmt::format("    {}: {},", input.name, input.wgslType()));
    }
    line("};");
    line("@group(0) @binding(1) var<uniform> inputs: Inputs;");
    line("@group(0) @binding(2) var linearSampler: sampler;");
    line("@group(0) @binding(3) var inputImage: texture_2d<f32>;");
    line("@group(0) @binding(4) var audioSpectrum: texture_2d<f32>;");
    std::uint32_t binding = 5;
    for (const PassDesc& pass : description.passes) {
        if (pass.target.empty()) {
            continue;
        }
        line(fmt::format("@group(0) @binding({}) var {}: texture_2d<f32>;", binding, pass.target));
        ++binding;
    }
    line(kBodyMarker);

    const std::size_t prologueLines =
        static_cast<std::size_t>(std::count(prologue.begin(), prologue.end(), '\n'));
    prologue.replace(0, 0,
                     fmt::format("// avgen user shader '{}': {} prologue lines; body starts at line {}",
                                 description.name, prologueLines, prologueLines + 1));

    std::string module = std::move(prologue);
    module += body;
    if (module.empty() || module.back() != '\n') {
        module.push_back('\n');
    }
    module += "// ---- entry points ----\n"
              "struct VsOut {\n"
              "    @builtin(position) position: vec4<f32>,\n"
              "    @location(0) uv: vec2<f32>,\n"
              "};\n"
              "@vertex fn vs_main(@builtin(vertex_index) i: u32) -> VsOut {\n"
              "    var positions = array<vec2<f32>, 3>(vec2<f32>(-1.0, -1.0), vec2<f32>(3.0, -1.0), "
              "vec2<f32>(-1.0, 3.0));\n"
              "    let p = positions[i];\n"
              "    var out: VsOut;\n"
              "    out.position = vec4<f32>(p, 0.0, 1.0);\n"
              "    out.uv = vec2<f32>(p.x * 0.5 + 0.5, 1.0 - (p.y * 0.5 + 0.5));\n"
              "    return out;\n"
              "}\n"
              "@fragment fn fs_main(in: VsOut) -> @location(0) vec4<f32> {\n"
              "    return mainImage(in.uv, in.uv * sys.passSize);\n"
              "}\n";
    return module;
}

// ---- size expressions ------------------------------------------------------------------------

Result<std::uint32_t> evaluateSizeExpression(const std::string& expr, std::uint32_t width,
                                             std::uint32_t height) {
    const std::string_view text = trim(expr);
    if (text.empty()) {
        return fail("empty size expression");
    }
    double value = 0.0;
    const std::size_t op = text.find_first_of("/*");
    if (op == std::string_view::npos) {
        const auto operand = parseOperand(text, width, height);
        if (!operand) {
            return fail("malformed size expression '{}'", expr);
        }
        value = *operand;
    } else {
        const auto lhs = parseOperand(text.substr(0, op), width, height);
        const auto rhs = parseOperand(text.substr(op + 1), width, height);
        if (!lhs || !rhs) {
            return fail("malformed size expression '{}' (expected <operand> {} <number>)", expr, text[op]);
        }
        if (text[op] == '/') {
            if (*rhs == 0.0) {
                return fail("size expression '{}' divides by zero", expr);
            }
            value = *lhs / *rhs;
        } else {
            value = *lhs * *rhs;
        }
    }
    if (!std::isfinite(value)) {
        return fail("size expression '{}' does not evaluate to a finite number", expr);
    }
    const double rounded = std::clamp(std::round(value), 1.0, 16384.0);
    return static_cast<std::uint32_t>(rounded);
}

// ---- type names ------------------------------------------------------------------------------

const char* inputTypeName(InputType type) {
    switch (type) {
    case InputType::Float:
        return "float";
    case InputType::Long:
        return "long";
    case InputType::Bool:
        return "bool";
    case InputType::Color:
        return "color";
    case InputType::Point2D:
        return "point2D";
    case InputType::Event:
        return "event";
    }
    return "float";
}

Result<InputType> inputTypeFromName(const std::string& name) {
    if (name == "float") {
        return InputType::Float;
    }
    if (name == "long") {
        return InputType::Long;
    }
    if (name == "bool") {
        return InputType::Bool;
    }
    if (name == "color") {
        return InputType::Color;
    }
    if (name == "point2D" || name == "point2d") {
        return InputType::Point2D;
    }
    if (name == "event") {
        return InputType::Event;
    }
    return fail("unsupported input type '{}' (expected float, long, bool, color, point2D or event)", name);
}

} // namespace avgen::shaders
