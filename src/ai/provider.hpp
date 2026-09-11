#pragma once

// The provider abstraction (ADR-094): normalized conversation types and one interface.
//
// This is the same decision ADR-065 made for image inference, for the same reason. `Provider` is
// an interface; OpenAI, Anthropic, Google and a local server are implementations chosen at
// configuration time, and **nothing above this line names a vendor**. `control_plane.cpp` builds
// the registry from a table of factories; the orchestrator, the tools, the panel and every test
// see only what is declared here.
//
// ## Why these types and not a vendor's
//
// Each provider's wire format is a different shape for the same four ideas: a conversation, a set
// of callable tools, the model asking for one, and the answer going back. Coupling the tool layer
// to any one of them would make the other three adapters translate *twice* -- once into the
// favoured vendor's types and once out -- and would make a fifth provider a refactor rather than a
// file. So the types here are AV Gen's, adapters translate at the edge, and the agent loop in
// `orchestrator.cpp` is written once.
//
// ## What is deliberately not here
//
// There is no model-facing tool type. A provider is handed `const ai::ToolDefinition&` -- the same
// object the registry holds -- and projects the three fields a model is allowed to see (name,
// description, inputSchema) out of it. A second, parallel "tool schema for the model" type is
// exactly the duplication §24 warns about, and it is the kind that drifts silently: the registry
// would say a tool is destructive and the copy sent to the model would not.
//
// ## Threading
//
// `complete()` blocks. It is called only from a `JobSystem` worker, never from the render, audio,
// analysis or UI thread -- which is enforced by shape: nothing on the main-thread path can reach a
// `Provider`, because the control plane hands one out only inside a job body. That is ADR-065's
// "no synchronous entry point" rule, kept.

#include "ai/tool_api.hpp"
#include "ai/tool_context.hpp"
#include "core/error.hpp"

#include <nlohmann/json.hpp>

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace avgen::ai {

// ---- conversation ------------------------------------------------------------------------------

enum class Role : std::uint8_t { User, Assistant };

// The model asking for a tool.
struct ToolCall {
    std::string id;   // provider-assigned; echoed back with the result
    std::string name; // must match a ToolRegistry entry
    nlohmann::json arguments = nlohmann::json::object();
};

// The answer going back.
struct ToolCallResult {
    std::string id;
    std::string name;
    nlohmann::json content = nlohmann::json::object();
    bool isError = false;
};

// One turn. An assistant turn may carry text and tool calls together; a user turn carries text or
// tool results. Keeping both on one type rather than a variant is what lets the transcript be a
// plain vector that every adapter walks the same way.
struct Message {
    Role role = Role::User;
    std::string text;
    std::vector<ToolCall> toolCalls;
    std::vector<ToolCallResult> toolResults;

    [[nodiscard]] static Message user(std::string text);
    [[nodiscard]] static Message assistant(std::string text, std::vector<ToolCall> calls = {});
    [[nodiscard]] static Message results(std::vector<ToolCallResult> results);
};

enum class StopReason : std::uint8_t {
    EndTurn,   // the model finished
    ToolUse,   // it wants tools run and the loop to continue
    MaxTokens, // it was cut off
    Refusal,   // it declined
    Other,
};
[[nodiscard]] const char* stopReasonName(StopReason reason);

struct Usage {
    std::uint64_t inputTokens = 0;
    std::uint64_t outputTokens = 0;
    std::uint64_t cachedInputTokens = 0;

    Usage& operator+=(const Usage& other);
    [[nodiscard]] nlohmann::json toJson() const;
};

// ---- requests ----------------------------------------------------------------------------------

struct CompletionRequest {
    std::string model;
    std::string system;                        // the system prompt (context.hpp builds it)
    std::vector<Message> messages;
    std::vector<const ToolDefinition*> tools;  // borrowed from the registry; never null
    double temperature = 0.4;
    std::uint32_t maxOutputTokens = 8192;
    CancelToken cancel;

    // Incremental text, when the provider streams. A provider that does not stream calls this once
    // with the whole text before returning, so a caller written against the callback works either
    // way and does not have to ask which it got.
    std::function<void(std::string_view delta)> onTextDelta;
};

struct CompletionResponse {
    std::string text;
    std::vector<ToolCall> toolCalls;
    StopReason stopReason = StopReason::EndTurn;
    Usage usage;
    std::string model; // what actually served it, which is not always what was asked for
};

// ---- capability discovery (§4, §47) ----------------------------------------------------------------

struct ProviderCapabilities {
    bool tools = true;
    bool streaming = false;
    bool systemPrompt = true;
    bool temperature = true;
    bool modelListing = false;
    bool vision = false;      // the multimodal hook (addendum §20); nothing uses it yet
    bool reasoning = false;
    std::uint32_t maxContextTokens = 0; // 0 = not advertised

    [[nodiscard]] nlohmann::json toJson() const;
};

// ---- configuration (§6, §7) ---------------------------------------------------------------------

// Everything a provider needs **except the secret**. This struct is what gets written to settings
// files and read back; the credential is fetched from the platform keystore by
// `credentialAccount`, never stored here, never serialised, never logged.
struct ProviderConfig {
    std::string id;              // "anthropic", "openai", "gemini", "openai-compatible"
    std::string displayName;     // what Settings shows
    std::string endpoint;        // base URL; empty means the adapter's default
    std::string model;           // empty means the adapter's default
    std::string credentialAccount; // keystore account name; usually the id
    bool enabled = false;
    double temperature = 0.4;
    std::uint32_t maxOutputTokens = 8192;

    [[nodiscard]] nlohmann::json toJson() const;
    [[nodiscard]] static Result<ProviderConfig> fromJson(const nlohmann::json& j);
};

struct ProviderModel {
    std::string id;
    std::string displayName;
};

// ---- the interface ------------------------------------------------------------------------------

class Provider {
public:
    virtual ~Provider() = default;

    [[nodiscard]] virtual std::string_view id() const = 0;
    [[nodiscard]] virtual std::string_view displayName() const = 0;
    [[nodiscard]] virtual ProviderCapabilities capabilities() const = 0;
    [[nodiscard]] virtual std::string defaultModel() const = 0;

    // One turn. Blocking; worker threads only. Returns a structured error rather than throwing --
    // an unreachable provider is an ordinary state, not an exceptional one (ADR-065).
    [[nodiscard]] virtual Result<CompletionResponse> complete(const CompletionRequest& request) = 0;

    // A cheap round trip for the Settings "Test connection" button. The default asks the model for
    // one token, because that is the only check that proves the credential, the endpoint, the
    // model name and the network all work at once.
    [[nodiscard]] virtual Result<std::string> testConnection();

    // Where the provider advertises one. An empty list is not an error.
    [[nodiscard]] virtual Result<std::vector<ProviderModel>> listModels();
};

// ---- the registry --------------------------------------------------------------------------------

// A provider is a factory registered by id. Adding one is: write the class, add one line here.
// Nothing else in the codebase changes -- which is the property ADR-065 asked for and the reason
// `ProviderRegistry` exists rather than a switch somewhere in the panel.
class ProviderRegistry {
public:
    struct Entry {
        std::string id;
        std::string displayName;
        bool requiresCredential = true;
        bool requiresEndpoint = false;
        std::string defaultEndpoint;
        std::string defaultModel;
        std::string credentialHint; // where a user gets a key; never a key
        std::function<Result<std::unique_ptr<Provider>>(const ProviderConfig&, std::string secret)>
            create;
    };

    void add(Entry entry);
    [[nodiscard]] const Entry* find(std::string_view id) const;
    [[nodiscard]] const std::vector<Entry>& entries() const { return entries_; }
    [[nodiscard]] std::vector<std::string> ids() const;

    // Builds a provider. `secret` is passed by value and moved in: it exists on the stack for the
    // length of this call and inside the provider afterwards, and nowhere else.
    [[nodiscard]] Result<std::unique_ptr<Provider>> create(const ProviderConfig& config,
                                                           std::string secret) const;

private:
    std::vector<Entry> entries_;
};

// The providers this build ships. Declared here, defined in `providers.cpp`, which is the only
// translation unit that names a vendor.
void registerBuiltinProviders(ProviderRegistry& registry);

class HttpClient;

// Replaces a provider's transport. **Tests only** -- this is how the adapters are checked against
// recorded wire payloads with no network and no key (§52). It is the one mock the brief permits,
// and it is a mock of the *provider's network*, never of an engine API. A provider that is not
// HTTP-backed ignores it and returns false.
[[nodiscard]] bool setProviderTransportForTesting(Provider& provider,
                                                  std::shared_ptr<HttpClient> transport);

} // namespace avgen::ai
