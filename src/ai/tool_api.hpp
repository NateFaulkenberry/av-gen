#pragma once

// The AV Gen Tool API (ADR-094): the engine's semantic interface, expressed as named, schema'd,
// validated operations over real engine systems.
//
// This is the load-bearing layer, not the chat panel. The AI is one client of it. The test the
// brief sets for the architecture is: *if the AI disappeared tomorrow, could another application
// invoke these as legitimate engine operations?* Nothing in this file or in `engine_tools.*` names
// a model, a vendor, or a wire protocol, so the answer is yes.
//
// Three properties it enforces by shape rather than by documentation:
//
//   - **Arguments are validated before a tool runs.** `ToolRegistry::invoke` checks the call
//     against the tool's own JSON Schema and refuses it with a structured, recoverable error.
//     The tool body therefore never parses untrusted JSON defensively; it reads validated fields.
//     A model's output is untrusted input and this is the boundary that treats it that way.
//
//   - **Every tool carries machine-readable metadata** -- read-only, mutating, destructive,
//     idempotent, expensive, which thread it needs. The model is shown name, description and
//     input schema; the *orchestrator* is shown all of it, because that metadata is what makes
//     scheduling, confirmation, parallelisation and transaction handling decidable rather than
//     guessed. The vocabulary deliberately mirrors MCP's tool annotations so exposing this
//     registry over MCP later is a serialisation exercise, not a redesign.
//
//   - **Failure is structured and recoverable.** A tool returns `ToolResult`, never an exception
//     and never prose. `{ success: false, error: { code, message, recovery } }` is something an
//     agent can act on; "something went wrong" is something it can only apologise for.

#include "core/error.hpp"

#include <nlohmann/json.hpp>

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace avgen::ai {

// ---- errors (§25) -----------------------------------------------------------------------------

// A short, closed set. Closed because an agent branches on these, and a code it has never seen is
// worth no more to it than the message it came with.
enum class ToolErrorCode : std::uint8_t {
    InvalidArguments, // the call did not match the schema
    NotFound,         // a named parameter / node / track / signal does not exist
    OutOfRange,       // a value outside the parameter's hard range
    Unsupported,      // a valid request this build cannot serve
    Unavailable,      // the subsystem exists but is not present in this session (no renderer, no audio)
    Conflict,         // the change would be overwritten by automation or a route, or a name collides
    Cancelled,
    Internal,
};
[[nodiscard]] const char* toolErrorCodeName(ToolErrorCode code);
// One sentence per code: what it means and what a client should do about it. Published in the
// error catalogue (`avgen --describe errors`, docs/api/errors.md), so it is written for a client.
[[nodiscard]] const char* toolErrorCodeMeaning(ToolErrorCode code);
// Every code, in enum order. The error catalogue and its drift test walk this list; a code added
// to the enum and not here fails a static_assert in tool_api.cpp.
inline constexpr ToolErrorCode kAllToolErrorCodes[] = {
    ToolErrorCode::InvalidArguments, ToolErrorCode::NotFound,    ToolErrorCode::OutOfRange,
    ToolErrorCode::Unsupported,      ToolErrorCode::Unavailable, ToolErrorCode::Conflict,
    ToolErrorCode::Cancelled,        ToolErrorCode::Internal,
};

struct ToolError {
    ToolErrorCode code = ToolErrorCode::Internal;
    std::string message;
    // What the caller could do instead. Not decoration: this is the difference between an agent
    // that recovers and one that repeats the same failing call.
    std::string recovery;
};

struct ToolResult {
    bool success = true;
    nlohmann::json value = nlohmann::json::object(); // structured payload for the model
    std::string summary;                             // one line for the activity log and the UI
    std::optional<ToolError> error;

    [[nodiscard]] static ToolResult ok(nlohmann::json value, std::string summary = {});
    [[nodiscard]] static ToolResult failure(ToolErrorCode code, std::string message,
                                            std::string recovery = {});
    // The form the model sees. Compact on purpose (§39): no pretty printing, no echoed arguments.
    [[nodiscard]] nlohmann::json toJson() const;
};

// ---- metadata (addendum §5) -------------------------------------------------------------------

// How a call is scheduled, and what a live host lets through (ADR-2150). It is derived from the
// annotations below unless a tool states it: a read-only tool is `Read`, a project write is
// `Instant`, a session write is `Session`. The exceptions are stated where the tool is registered:
// `Rebind` (adds or removes a route, a key, a shot), `Structural` (rebuilds the composition:
// a node added, removed or re-parented) and `Job` (seconds to minutes: a child process, a render).
enum class ExecutionClass : std::uint8_t { Read, Instant, Rebind, Structural, Session, Job };
[[nodiscard]] const char* executionClassName(ExecutionClass c);

// What a session must allow before a tool may run (ADR-2150). A closed vocabulary, checked at
// registration: "read" (reads a file or folder the caller names, or lists a content root),
// "write" (writes files), "subprocess" (starts a child process), "gpu" (starts GPU work).
inline constexpr std::string_view kToolPermissions[] = {"read", "write", "subprocess", "gpu"};

struct ToolAnnotations {
    bool readOnly = false;
    bool mutatesProject = false;
    // Changes the running session without changing the project: the transport position, the
    // selection, a view setting. The distinction is not pedantic -- a session mutation is outside
    // the transaction's snapshot domain, so claiming `mutatesProject` for one would promise a
    // rollback that cannot happen, and claiming `readOnly` would be a plain lie.
    bool mutatesSession = false;
    bool destructive = false;
    bool idempotent = false;
    bool expensive = false;
    // Defaults to true because it is the safe answer: engine state is read by the main thread every
    // frame, so a tool that touches it runs on the main thread unless it has been shown not to.
    bool requiresMainThread = true;
    bool requiresRenderThread = false;
    bool requiresAudioThread = false;
    // Whether the change is captured by the transaction primitive in transaction.hpp. A tool that
    // mutates something outside the snapshot domain must say so, or a rollback would silently
    // leave it behind.
    bool undoable = false;
    bool supportsCancellation = false;
    bool supportsProgress = false;
    // The Director's safety model (spec §46-§47). `requiresApproval`: what the tool asks for changes
    // the project only after a person approves it -- the tool itself changes nothing, and the task
    // waits in `AwaitingApproval`. `deterministic`: the content it leads to renders the same way
    // twice (the baked tier, ADR-091).
    bool requiresApproval = false;
    bool deterministic = false;
    // Unset means derived; see ExecutionClass. Set only for the stated exceptions.
    std::optional<ExecutionClass> executionClass;
    std::vector<std::string> permissions; // from kToolPermissions; empty: none beyond calling

    [[nodiscard]] ExecutionClass effectiveExecutionClass() const;
    [[nodiscard]] nlohmann::json toJson() const;
};

struct ToolDefinition {
    std::string name;        // "parameter.set" -- the namespace vocabulary of §40
    std::string title;       // human-readable, for the UI
    std::string description; // what the model reads
    nlohmann::json inputSchema = nlohmann::json::object();
    nlohmann::json outputSchema;                          // null when unspecified
    ToolAnnotations annotations;
    // The API level at which this tool's current contract appeared (ADR-2150). A breaking change
    // to a tool bumps the API level (avgen::api::kApiLevel) and sets this to it.
    int since = 1;

    // The domain the name lives in ("parameter", "scene", "sequencer", ...).
    [[nodiscard]] std::string domain() const;
};

// ---- execution --------------------------------------------------------------------------------

class ToolContext;

using ToolFn = std::function<ToolResult(const nlohmann::json& args, ToolContext& ctx)>;

struct Tool {
    ToolDefinition definition;
    ToolFn execute;
};

class ToolRegistry {
public:
    // Adding a name twice is a programming error, not a runtime one: the tool table is built once
    // at start-up from code, so a duplicate is a mistake in that code and silently keeping one of
    // the two would hide it.
    void add(Tool tool);

    [[nodiscard]] const Tool* find(std::string_view name) const;
    [[nodiscard]] std::vector<const Tool*> all() const; // registration order
    [[nodiscard]] std::vector<std::string> names() const;
    [[nodiscard]] std::vector<std::string> domains() const; // sorted, deduplicated
    [[nodiscard]] std::size_t size() const { return tools_.size(); }
    [[nodiscard]] bool empty() const { return tools_.empty(); }

    // Validates `args` against the tool's input schema, then runs it. An unknown name, a schema
    // violation and a thrown exception all come back as a structured failure -- a tool body that
    // throws must not take the orchestrator's worker thread with it.
    [[nodiscard]] ToolResult invoke(std::string_view name, const nlohmann::json& args,
                                    ToolContext& ctx) const;

private:
    std::vector<Tool> tools_;
    std::unordered_map<std::string, std::size_t> index_;
};

// ---- schema validation (§25) --------------------------------------------------------------------

// A deliberately small JSON Schema subset: `type` (object/array/string/number/integer/boolean),
// `properties`, `required`, `additionalProperties: false`, `enum`, `minimum`/`maximum`,
// `minItems`/`maxItems`, `items`. That is the whole vocabulary the tool schemas use, and a general
// validator would be a dependency bought for no consumer -- the same call ADR-065 made about
// tensors.
//
// Returns an empty optional when the value conforms. `path` seeds the error's location text.
[[nodiscard]] std::optional<ToolError> validateAgainstSchema(const nlohmann::json& schema,
                                                             const nlohmann::json& value,
                                                             std::string_view path = "arguments");

// ---- schema construction helpers ----------------------------------------------------------------
// Tool definitions are written by hand, and hand-written JSON Schema in C++ is where typos live.
// These keep the shape uniform and the diff small.

namespace schema {
[[nodiscard]] nlohmann::json object(nlohmann::json properties, std::vector<std::string> required = {},
                                    bool additionalProperties = false);
[[nodiscard]] nlohmann::json string(std::string description, std::vector<std::string> enumValues = {});
[[nodiscard]] nlohmann::json number(std::string description, std::optional<double> min = std::nullopt,
                                    std::optional<double> max = std::nullopt);
[[nodiscard]] nlohmann::json integer(std::string description, std::optional<double> min = std::nullopt,
                                     std::optional<double> max = std::nullopt);
[[nodiscard]] nlohmann::json boolean(std::string description);
[[nodiscard]] nlohmann::json array(nlohmann::json items, std::string description,
                                   std::optional<int> minItems = std::nullopt,
                                   std::optional<int> maxItems = std::nullopt);
} // namespace schema

} // namespace avgen::ai
