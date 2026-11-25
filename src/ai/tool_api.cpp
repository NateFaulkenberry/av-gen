#include "ai/tool_api.hpp"

#include "core/log.hpp"

#include <algorithm>
#include <cmath>
#include <exception>
#include <iterator>
#include <stdexcept>

namespace avgen::ai {
namespace {

std::string joinPath(std::string_view path, std::string_view key) {
    std::string out(path);
    out += '.';
    out += key;
    return out;
}

bool isIntegral(const nlohmann::json& value) {
    if (value.is_number_integer()) {
        return true;
    }
    if (!value.is_number_float()) {
        return false;
    }
    const double d = value.get<double>();
    return std::isfinite(d) && std::floor(d) == d;
}

const char* jsonTypeName(const nlohmann::json& value) {
    switch (value.type()) {
    case nlohmann::json::value_t::null: return "null";
    case nlohmann::json::value_t::object: return "object";
    case nlohmann::json::value_t::array: return "array";
    case nlohmann::json::value_t::string: return "string";
    case nlohmann::json::value_t::boolean: return "boolean";
    case nlohmann::json::value_t::number_integer:
    case nlohmann::json::value_t::number_unsigned: return "integer";
    case nlohmann::json::value_t::number_float: return "number";
    default: return "unknown";
    }
}

std::string enumList(const nlohmann::json& values) {
    std::string out;
    for (const auto& v : values) {
        if (!out.empty()) {
            out += ", ";
        }
        out += v.is_string() ? v.get<std::string>() : v.dump();
    }
    return out;
}

} // namespace

const char* toolErrorCodeName(ToolErrorCode code) {
    switch (code) {
    case ToolErrorCode::InvalidArguments: return "INVALID_ARGUMENTS";
    case ToolErrorCode::NotFound: return "NOT_FOUND";
    case ToolErrorCode::OutOfRange: return "OUT_OF_RANGE";
    case ToolErrorCode::Unsupported: return "UNSUPPORTED";
    case ToolErrorCode::Unavailable: return "UNAVAILABLE";
    case ToolErrorCode::Conflict: return "CONFLICT";
    case ToolErrorCode::Cancelled: return "CANCELLED";
    case ToolErrorCode::Internal: return "INTERNAL";
    case ToolErrorCode::PermissionDenied: return "PERMISSION_DENIED";
    case ToolErrorCode::FailedPrecondition: return "FAILED_PRECONDITION";
    case ToolErrorCode::Busy: return "BUSY";
    }
    return "INTERNAL";
}

static_assert(std::size(kAllToolErrorCodes) == static_cast<std::size_t>(ToolErrorCode::Busy) + 1,
              "kAllToolErrorCodes must list every ToolErrorCode");

const char* toolErrorCodeMeaning(ToolErrorCode code) {
    switch (code) {
    case ToolErrorCode::InvalidArguments:
        return "The call did not match the tool's input schema, or named something in a form the tool cannot read. Fix the "
               "arguments; the message names the field.";
    case ToolErrorCode::NotFound:
        return "A named parameter, node, track, signal, file or tool does not exist. Look the name up (parameter.search, "
               "scene.find_nodes, signal.list, capability.list_tools) and retry.";
    case ToolErrorCode::OutOfRange:
        return "A value is outside its hard range. Clamp it into the range the schema or parameter.get reports.";
    case ToolErrorCode::Unsupported:
        return "A valid request this build cannot serve. Retrying will not help; the recovery text says what the build "
               "lacks.";
    case ToolErrorCode::Unavailable:
        return "The subsystem exists but is not present in this session (no renderer, no audio, no host hook). It may be "
               "available in another kind of session.";
    case ToolErrorCode::Conflict:
        return "The change would be overwritten by automation or a route, or a name collides. Resolve the conflict the "
               "message names.";
    case ToolErrorCode::Cancelled:
        return "The call was cancelled, or the host did not service it in time. Nothing further was applied.";
    case ToolErrorCode::Internal:
        return "An engine fault. Report it with the message; the project was not necessarily modified.";
    case ToolErrorCode::PermissionDenied:
        return "This session's permissions forbid the call: a path outside its read or write roots, a write into a "
               "project it may only read, or a child process it may not start. Retrying unchanged will not help; "
               "the recovery text names what to change, and details.path names the path when there is one.";
    case ToolErrorCode::FailedPrecondition:
        return "The session is not in the state the call needs: no project is open, there is no composition, or a "
               "transaction is already open (or not open). Put the session in that state first; the recovery text "
               "says how.";
    case ToolErrorCode::Busy:
        return "The call is valid but cannot run now: the host is running a live show, or the work it needs is "
               "taken. Retry later; details.retryAfterMs says when, if the host knows.";
    }
    return "";
}

const char* executionClassName(ExecutionClass c) {
    switch (c) {
    case ExecutionClass::Read: return "read";
    case ExecutionClass::Instant: return "instant";
    case ExecutionClass::Rebind: return "rebind";
    case ExecutionClass::Structural: return "structural";
    case ExecutionClass::Session: return "session";
    case ExecutionClass::Job: return "job";
    }
    return "read";
}

ExecutionClass ToolAnnotations::effectiveExecutionClass() const {
    if (executionClass) {
        return *executionClass;
    }
    if (readOnly) {
        return ExecutionClass::Read;
    }
    return mutatesProject ? ExecutionClass::Instant : ExecutionClass::Session;
}

ToolResult ToolResult::ok(nlohmann::json value, std::string summary) {
    ToolResult r;
    r.success = true;
    r.value = std::move(value);
    r.summary = std::move(summary);
    return r;
}

ToolResult ToolResult::failure(ToolErrorCode code, std::string message, std::string recovery,
                               nlohmann::json details) {
    return failure(ToolError{code, std::move(message), std::move(recovery), std::move(details)});
}

ToolResult ToolResult::failure(ToolError error) {
    ToolResult r;
    r.success = false;
    r.value = nlohmann::json::object();
    r.summary = error.message;
    r.error = std::move(error);
    return r;
}

nlohmann::json ToolResult::toJson() const {
    nlohmann::json j;
    j["success"] = success;
    if (success) {
        if (!value.is_null()) {
            j["result"] = value;
        }
    } else if (error) {
        nlohmann::json e;
        e["code"] = toolErrorCodeName(error->code);
        e["message"] = error->message;
        if (!error->recovery.empty()) {
            e["recovery"] = error->recovery;
        }
        if (error->details.is_object() && !error->details.empty()) {
            e["details"] = error->details;
        }
        j["error"] = std::move(e);
    }
    return j;
}

nlohmann::json ToolAnnotations::toJson() const {
    nlohmann::json j;
    j["readOnly"] = readOnly;
    j["mutatesProject"] = mutatesProject;
    j["mutatesSession"] = mutatesSession;
    j["destructive"] = destructive;
    j["idempotent"] = idempotent;
    j["expensive"] = expensive;
    j["requiresMainThread"] = requiresMainThread;
    j["requiresRenderThread"] = requiresRenderThread;
    j["requiresAudioThread"] = requiresAudioThread;
    j["undoable"] = undoable;
    j["supportsCancellation"] = supportsCancellation;
    j["supportsProgress"] = supportsProgress;
    j["requiresApproval"] = requiresApproval;
    j["deterministic"] = deterministic;
    j["executionClass"] = executionClassName(effectiveExecutionClass());
    j["permissions"] = permissions;
    j["requirements"] = requirements;
    return j;
}

std::string ToolDefinition::domain() const {
    const auto dot = name.find('.');
    return dot == std::string::npos ? name : name.substr(0, dot);
}

// ---- registry ---------------------------------------------------------------------------------

void ToolRegistry::add(Tool tool) {
    if (tool.definition.name.empty()) {
        throw std::logic_error("ToolRegistry::add: tool has no name");
    }
    if (!tool.execute) {
        throw std::logic_error("ToolRegistry::add: tool '" + tool.definition.name + "' has no body");
    }
    if (index_.contains(tool.definition.name)) {
        throw std::logic_error("ToolRegistry::add: duplicate tool '" + tool.definition.name + "'");
    }
    // MCP's tool annotations default pessimistically -- an unannotated tool reads as writing,
    // destructive and non-idempotent -- precisely so that forgetting to annotate cannot quietly
    // downgrade safety. These defaults are optimistic instead, because every tool here is authored
    // by hand in one file; so the guard against forgetting has to live somewhere, and it lives
    // here. A tool that is neither a read nor a write has not been classified at all.
    const ToolAnnotations& a = tool.definition.annotations;
    if (!a.readOnly && !a.mutatesProject && !a.mutatesSession) {
        throw std::logic_error("ToolRegistry::add: tool '" + tool.definition.name +
                               "' is annotated neither readOnly, mutatesProject nor mutatesSession");
    }
    if (a.readOnly && (a.mutatesProject || a.mutatesSession || a.destructive)) {
        throw std::logic_error("ToolRegistry::add: tool '" + tool.definition.name +
                               "' is readOnly and also claims to modify something");
    }
    // ADR-2150: a stated execution class must agree with what the tool says it touches -- a "read"
    // that writes, or a project write classed as a session change, would mis-schedule it in a
    // live host. `Job` is exempt: a job may be read-only (a render of a scratch copy) or not.
    if (a.executionClass) {
        const ExecutionClass c = *a.executionClass;
        const bool agrees = c == ExecutionClass::Job ||
                            (c == ExecutionClass::Read && a.readOnly) ||
                            (c == ExecutionClass::Session && a.mutatesSession) ||
                            ((c == ExecutionClass::Instant || c == ExecutionClass::Rebind ||
                              c == ExecutionClass::Structural) && a.mutatesProject);
        if (!agrees) {
            throw std::logic_error("ToolRegistry::add: tool '" + tool.definition.name + "' is classed '" +
                                   executionClassName(c) + "', which disagrees with its annotations");
        }
    }
    for (const std::string& p : a.permissions) {
        if (std::find(std::begin(kToolPermissions), std::end(kToolPermissions), p) == std::end(kToolPermissions)) {
            throw std::logic_error("ToolRegistry::add: tool '" + tool.definition.name + "' names unknown permission '" +
                                   p + "'");
        }
    }
    for (const std::string& r : a.requirements) {
        if (std::find(std::begin(kToolRequirements), std::end(kToolRequirements), r) == std::end(kToolRequirements)) {
            throw std::logic_error("ToolRegistry::add: tool '" + tool.definition.name + "' names unknown requirement '" +
                                   r + "'");
        }
    }
    if (tool.definition.since < 1) {
        throw std::logic_error("ToolRegistry::add: tool '" + tool.definition.name + "' has no API level ('since')");
    }
    index_.emplace(tool.definition.name, tools_.size());
    tools_.push_back(std::move(tool));
}

const Tool* ToolRegistry::find(std::string_view name) const {
    const auto it = index_.find(std::string(name));
    return it == index_.end() ? nullptr : &tools_[it->second];
}

std::vector<const Tool*> ToolRegistry::all() const {
    std::vector<const Tool*> out;
    out.reserve(tools_.size());
    for (const Tool& tool : tools_) {
        out.push_back(&tool);
    }
    return out;
}

std::vector<std::string> ToolRegistry::names() const {
    std::vector<std::string> out;
    out.reserve(tools_.size());
    for (const Tool& tool : tools_) {
        out.push_back(tool.definition.name);
    }
    return out;
}

std::vector<std::string> ToolRegistry::domains() const {
    std::vector<std::string> out;
    for (const Tool& tool : tools_) {
        std::string d = tool.definition.domain();
        if (std::find(out.begin(), out.end(), d) == out.end()) {
            out.push_back(std::move(d));
        }
    }
    std::sort(out.begin(), out.end());
    return out;
}

ToolError ToolRegistry::unknownTool(std::string_view name) {
    return ToolError{ToolErrorCode::NotFound, fmt::format("no tool named '{}'", name),
                     "call capability.list_tools for the available tool names", nullptr};
}

std::optional<ToolError> ToolRegistry::checkArguments(const Tool& tool, const nlohmann::json& args) {
    const nlohmann::json& effective = args.is_null() ? nlohmann::json::object() : args;
    if (!effective.is_object()) {
        return ToolError{ToolErrorCode::InvalidArguments, "arguments must be a JSON object",
                         "pass an object whose keys match the tool's input schema", nullptr};
    }
    return validateAgainstSchema(tool.definition.inputSchema, effective);
}

ToolResult ToolRegistry::run(const Tool& tool, const nlohmann::json& args, ToolContext& ctx) {
    const nlohmann::json& effective = args.is_null() ? nlohmann::json::object() : args;
    const std::string& name = tool.definition.name;
    // A tool body that throws must not take the orchestrator's worker with it. Engine code signals
    // programming errors with exceptions (core/error.hpp), and a model can reach a code path an
    // author never did.
    try {
        return tool.execute(effective, ctx);
    } catch (const std::exception& e) {
        log::error("ai: tool '{}' threw: {}", name, e.what());
        return ToolResult::failure(ToolErrorCode::Internal,
                                   fmt::format("tool '{}' failed: {}", name, e.what()),
                                   "report this; the project was not necessarily modified");
    } catch (...) {
        log::error("ai: tool '{}' threw a non-standard exception", name);
        return ToolResult::failure(ToolErrorCode::Internal,
                                   fmt::format("tool '{}' failed with an unknown exception", name));
    }
}

ToolResult ToolRegistry::invoke(std::string_view name, const nlohmann::json& args,
                                ToolContext& ctx) const {
    const Tool* tool = find(name);
    if (tool == nullptr) {
        return ToolResult::failure(unknownTool(name));
    }
    if (auto error = checkArguments(*tool, args)) {
        return ToolResult::failure(std::move(*error));
    }
    return run(*tool, args, ctx);
}

// ---- schema validation --------------------------------------------------------------------------

namespace {

// The validator proper. `root` is the schema a local `$ref` (`#/$defs/...`) resolves against: the
// document schemas (ADR-2152) name their shared shapes once, and a tool schema that has no `$defs`
// never meets one.
std::optional<ToolError> validateNode(const nlohmann::json& schema, const nlohmann::json& root,
                                      const nlohmann::json& value, std::string_view path, int depth) {
    const auto invalid = [&](std::string message, std::string recovery = {}) {
        return ToolError{ToolErrorCode::InvalidArguments, std::move(message), std::move(recovery)};
    };
    if (!schema.is_object()) {
        return std::nullopt; // no constraint declared
    }
    // A `$ref` that points at itself (directly or round a loop) would recurse for ever; no schema
    // this engine publishes nests anywhere near this deep.
    constexpr int kMaxDepth = 256;
    if (depth > kMaxDepth) {
        return ToolError{ToolErrorCode::Internal, fmt::format("{}: the schema nests deeper than {} levels", path, kMaxDepth),
                         "report this; the schema refers to itself"};
    }

    // ADR-2152: a local reference, applied beside the keywords next to it (JSON Schema 2020-12). Only
    // `#/...` is resolved; a schema that names another document is a schema bug, said so.
    if (const auto ref = schema.find("$ref"); ref != schema.end() && ref->is_string()) {
        const std::string target = ref->get<std::string>();
        const nlohmann::json* resolved = nullptr;
        if (target.starts_with("#")) {
            try {
                const nlohmann::json::json_pointer pointer(target.substr(1));
                if (root.contains(pointer)) {
                    resolved = &root.at(pointer);
                }
            } catch (const nlohmann::json::exception&) {
                resolved = nullptr;
            }
        }
        if (resolved == nullptr) {
            return ToolError{ToolErrorCode::Internal,
                             fmt::format("{}: the schema's $ref '{}' does not resolve", path, target),
                             "report this; the schema names a definition it does not contain"};
        }
        if (auto e = validateNode(*resolved, root, value, path, depth + 1)) {
            return e;
        }
    }

    // ADR-2152: `const`, one allowed value. Reported like a one-value `enum`.
    if (const auto c = schema.find("const"); c != schema.end()) {
        if (*c != value) {
            return invalid(fmt::format("{} must be {}", path, c->dump()), fmt::format("use {}", c->dump()));
        }
    }

    // ADR-2152: `anyOf`, at least one form must hold. When none does, every form's reason is listed,
    // so a client sees each way the value could have been right.
    if (const auto any = schema.find("anyOf"); any != schema.end() && any->is_array()) {
        std::vector<ToolError> reasons;
        bool matched = false;
        for (const nlohmann::json& branch : *any) {
            auto e = validateNode(branch, root, value, path, depth + 1);
            if (!e) {
                matched = true;
                break;
            }
            if (e->code == ToolErrorCode::Internal) {
                return e; // a broken schema is not a reason the value is wrong
            }
            reasons.push_back(std::move(*e));
        }
        if (!matched) {
            if (reasons.size() == 1) {
                return reasons.front();
            }
            std::string why;
            for (std::size_t i = 0; i < reasons.size(); ++i) {
                why += fmt::format("{}({}) {}", i == 0 ? "" : "; ", i + 1, reasons[i].message);
            }
            return invalid(fmt::format("{} matches none of the {} allowed forms: {}", path, reasons.size(), why),
                           "make the value match one of the forms");
        }
    }

    if (const auto type = schema.find("type"); type != schema.end() && type->is_string()) {
        const std::string want = type->get<std::string>();
        bool okType = true;
        if (want == "object") {
            okType = value.is_object();
        } else if (want == "array") {
            okType = value.is_array();
        } else if (want == "string") {
            okType = value.is_string();
        } else if (want == "boolean") {
            okType = value.is_boolean();
        } else if (want == "integer") {
            okType = isIntegral(value);
        } else if (want == "number") {
            okType = value.is_number();
        }
        if (!okType) {
            return invalid(fmt::format("{} must be a {} (got {})", path, want, jsonTypeName(value)),
                           fmt::format("pass a {} for {}", want, path));
        }
    }

    if (const auto en = schema.find("enum"); en != schema.end() && en->is_array()) {
        const bool found = std::any_of(en->begin(), en->end(),
                                       [&](const nlohmann::json& v) { return v == value; });
        if (!found) {
            return invalid(fmt::format("{} must be one of: {}", path, enumList(*en)),
                           fmt::format("choose one of: {}", enumList(*en)));
        }
    }

    if (value.is_number()) {
        const double d = value.get<double>();
        if (!std::isfinite(d)) {
            return invalid(fmt::format("{} must be a finite number", path));
        }
        if (const auto min = schema.find("minimum"); min != schema.end() && min->is_number()) {
            if (d < min->get<double>()) {
                return ToolError{ToolErrorCode::OutOfRange,
                                 fmt::format("{} must be >= {}", path, min->get<double>()),
                                 "clamp the value into the documented range"};
            }
        }
        if (const auto max = schema.find("maximum"); max != schema.end() && max->is_number()) {
            if (d > max->get<double>()) {
                return ToolError{ToolErrorCode::OutOfRange,
                                 fmt::format("{} must be <= {}", path, max->get<double>()),
                                 "clamp the value into the documented range"};
            }
        }
    }

    if (value.is_array()) {
        if (const auto mi = schema.find("minItems"); mi != schema.end() && mi->is_number()) {
            if (value.size() < mi->get<std::size_t>()) {
                return invalid(fmt::format("{} needs at least {} item(s)", path, mi->get<int>()));
            }
        }
        if (const auto ma = schema.find("maxItems"); ma != schema.end() && ma->is_number()) {
            if (value.size() > ma->get<std::size_t>()) {
                return invalid(fmt::format("{} accepts at most {} item(s)", path, ma->get<int>()));
            }
        }
        if (const auto items = schema.find("items"); items != schema.end() && items->is_object()) {
            for (std::size_t i = 0; i < value.size(); ++i) {
                if (auto e = validateNode(*items, root, value[i], fmt::format("{}[{}]", path, i), depth + 1)) {
                    return e;
                }
            }
        }
    }

    if (value.is_object()) {
        const auto props = schema.find("properties");
        if (const auto req = schema.find("required"); req != schema.end() && req->is_array()) {
            for (const auto& name : *req) {
                if (!name.is_string()) {
                    continue;
                }
                const std::string key = name.get<std::string>();
                if (!value.contains(key) || value.at(key).is_null()) {
                    return invalid(fmt::format("{} is missing required field '{}'", path, key),
                                   fmt::format("include '{}' in the arguments", key));
                }
            }
        }
        const bool additional = schema.value("additionalProperties", true);
        if (!additional && props != schema.end() && props->is_object()) {
            for (const auto& [key, _] : value.items()) {
                if (!props->contains(key)) {
                    std::string known;
                    for (const auto& [pk, __] : props->items()) {
                        if (!known.empty()) {
                            known += ", ";
                        }
                        known += pk;
                    }
                    return invalid(fmt::format("{} has an unknown field '{}'", path, key),
                                   known.empty() ? std::string{}
                                                 : fmt::format("known fields: {}", known));
                }
            }
        }
        if (props != schema.end() && props->is_object()) {
            for (const auto& [key, sub] : props->items()) {
                if (!value.contains(key) || value.at(key).is_null()) {
                    continue;
                }
                if (auto e = validateNode(sub, root, value.at(key), joinPath(path, key), depth + 1)) {
                    return e;
                }
            }
        }
    }

    return std::nullopt;
}

} // namespace

std::optional<ToolError> validateAgainstSchema(const nlohmann::json& schema,
                                               const nlohmann::json& value, std::string_view path) {
    return validateNode(schema, schema, value, path, 0);
}

// ---- schema helpers ------------------------------------------------------------------------------

namespace schema {

nlohmann::json object(nlohmann::json properties, std::vector<std::string> required,
                      bool additionalProperties) {
    nlohmann::json j;
    j["type"] = "object";
    j["properties"] = std::move(properties);
    if (!required.empty()) {
        j["required"] = std::move(required);
    }
    j["additionalProperties"] = additionalProperties;
    return j;
}

nlohmann::json string(std::string description, std::vector<std::string> enumValues) {
    nlohmann::json j;
    j["type"] = "string";
    j["description"] = std::move(description);
    if (!enumValues.empty()) {
        j["enum"] = std::move(enumValues);
    }
    return j;
}

nlohmann::json number(std::string description, std::optional<double> min, std::optional<double> max) {
    nlohmann::json j;
    j["type"] = "number";
    j["description"] = std::move(description);
    if (min) {
        j["minimum"] = *min;
    }
    if (max) {
        j["maximum"] = *max;
    }
    return j;
}

nlohmann::json integer(std::string description, std::optional<double> min, std::optional<double> max) {
    nlohmann::json j = number(std::move(description), min, max);
    j["type"] = "integer";
    return j;
}

nlohmann::json boolean(std::string description) {
    nlohmann::json j;
    j["type"] = "boolean";
    j["description"] = std::move(description);
    return j;
}

nlohmann::json array(nlohmann::json items, std::string description, std::optional<int> minItems,
                     std::optional<int> maxItems) {
    nlohmann::json j;
    j["type"] = "array";
    j["description"] = std::move(description);
    j["items"] = std::move(items);
    if (minItems) {
        j["minItems"] = *minItems;
    }
    if (maxItems) {
        j["maxItems"] = *maxItems;
    }
    return j;
}

} // namespace schema

} // namespace avgen::ai
