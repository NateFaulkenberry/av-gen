// The provider adapters (ADR-094). **This is the only translation unit in AV Gen that names a
// vendor.** Everything above `provider.hpp` -- the orchestrator, the tool registry, the panel, the
// tests -- is written against the interface, which is the property ADR-065 established for image
// inference and the reason a fifth provider is a class and a table row rather than a refactor.
//
// Each adapter does exactly one job: translate `CompletionRequest` into that vendor's JSON and
// their response back into `CompletionResponse`. They share `HttpProvider` for the transport, the
// error mapping and the tool-schema projection, because those are genuinely the same problem three
// times and the differences are only in the field names.
//
// ## What is normalized, and what is not
//
// Normalized, because the agent loop cannot work without it: the message transcript, tool
// definitions, tool calls, tool results, stop reasons and token usage. Those four ideas -- a
// conversation, callable tools, the model asking for one, the answer going back -- are the same
// everywhere and only the spelling differs.
//
// Left provider-specific, deliberately: model identifiers (an alias table would be stale within a
// month and would silently route a user to a model they did not choose), sampling parameters
// beyond temperature, reasoning and thinking configuration, caching directives, and anything
// beta-gated. A lowest common denominator that hid those would make the good providers worse.
//
// ## Streaming
//
// Not implemented in this pass, and `ProviderCapabilities::streaming` says so rather than
// pretending. `CompletionRequest::onTextDelta` is honoured -- it is called once with the complete
// text -- so a caller written against the callback works unchanged when SSE lands. The user-facing
// cost is nil today because §9 is explicit that progress should be a structured task/activity
// model rather than scraped model text, and that is what the panel shows.

#include "ai/http.hpp"
#include "ai/provider.hpp"
#include "core/log.hpp"

#include <algorithm>
#include <utility>

namespace avgen::ai {
namespace {

using nlohmann::json;

// ---- shared base ---------------------------------------------------------------------------------

// Turns an HTTP status into a message a person can act on. The generic "request failed" is the
// single most expensive error message a network feature can ship: 401 and 429 need completely
// different responses from the user and telling them apart costs one switch.
Error httpStatusError(std::string_view providerName, const HttpResponse& response) {
    std::string detail;
    if (const json parsed = json::parse(response.body, nullptr, false); !parsed.is_discarded()) {
        // Every one of the three nests the human-readable text differently; try each.
        if (parsed.contains("error")) {
            const json& e = parsed.at("error");
            if (e.is_object() && e.contains("message") && e.at("message").is_string()) {
                detail = e.at("message").get<std::string>();
            } else if (e.is_string()) {
                detail = e.get<std::string>();
            }
        }
    }
    if (detail.empty()) {
        detail = response.body.substr(0, 300);
    }
    switch (response.status) {
    case 401:
    case 403:
        return Error{fmt::format("{} rejected the credential (HTTP {}): {}", providerName,
                                 response.status, detail)};
    case 404:
        return Error{fmt::format("{}: not found (HTTP 404) -- check the model name and the "
                                 "endpoint: {}",
                                 providerName, detail)};
    case 429:
        return Error{fmt::format("{} rate limited this request (HTTP 429): {}", providerName,
                                 detail)};
    default:
        if (response.status >= 500) {
            return Error{fmt::format("{} had a server error (HTTP {}): {}", providerName,
                                     response.status, detail)};
        }
        return Error{
            fmt::format("{} refused the request (HTTP {}): {}", providerName, response.status,
                        detail)};
    }
}

// Trims a trailing slash so a user-entered endpoint with or without one behaves the same. The
// number of bug reports this one line prevents is not small.
std::string trimSlash(std::string url) {
    while (!url.empty() && url.back() == '/') {
        url.pop_back();
    }
    return url;
}

class HttpProvider : public Provider {
public:
    HttpProvider(ProviderConfig config, std::string secret, std::string endpoint)
        : config_(std::move(config)), secret_(std::move(secret)),
          endpoint_(trimSlash(std::move(endpoint))), http_(makeSystemHttpClient()) {}

    // Tests inject a scripted transport. Not a mock of an engine API (§54 forbids that); a mock of
    // the *provider's network*, which §52 explicitly asks for.
    void setHttpClient(std::shared_ptr<HttpClient> client) { http_ = std::move(client); }

    [[nodiscard]] std::string_view id() const override { return config_.id; }
    [[nodiscard]] std::string_view displayName() const override { return config_.displayName; }

protected:
    [[nodiscard]] Result<json> post(const std::string& url,
                                    std::vector<std::pair<std::string, std::string>> headers,
                                    const json& body, const CancelToken& cancel) {
        if (!http_ || !http_->available()) {
            return fail("no HTTP transport in this build");
        }
        HttpRequest request;
        request.url = url;
        request.method = "POST";
        request.headers = std::move(headers);
        request.headers.emplace_back("Content-Type", "application/json");
        request.body = body.dump();
        request.cancel = cancel;
        auto response = http_->send(request);
        if (!response) {
            return std::unexpected(response.error());
        }
        if (!response->ok()) {
            return std::unexpected(httpStatusError(config_.displayName, *response));
        }
        json parsed = json::parse(response->body, nullptr, false);
        if (parsed.is_discarded()) {
            return fail("{} returned a body that is not JSON", config_.displayName);
        }
        return parsed;
    }

    [[nodiscard]] Result<json> get(const std::string& url,
                                   std::vector<std::pair<std::string, std::string>> headers,
                                   const CancelToken& cancel) {
        if (!http_ || !http_->available()) {
            return fail("no HTTP transport in this build");
        }
        HttpRequest request;
        request.url = url;
        request.method = "GET";
        request.headers = std::move(headers);
        request.cancel = cancel;
        auto response = http_->send(request);
        if (!response) {
            return std::unexpected(response.error());
        }
        if (!response->ok()) {
            return std::unexpected(httpStatusError(config_.displayName, *response));
        }
        json parsed = json::parse(response->body, nullptr, false);
        if (parsed.is_discarded()) {
            return fail("{} returned a body that is not JSON", config_.displayName);
        }
        return parsed;
    }

    // The three fields a model is allowed to see. Called by every adapter so the projection has
    // one definition: the annotations -- destructive, expensive, which thread -- are the
    // orchestrator's business and are never sent.
    static void emitText(const CompletionRequest& request, const std::string& text) {
        if (request.onTextDelta && !text.empty()) {
            request.onTextDelta(text);
        }
    }

    ProviderConfig config_;
    std::string secret_;
    std::string endpoint_;
    std::shared_ptr<HttpClient> http_;
};

// Some servers validate tool schemas against OpenAPI 3.0, which has no `additionalProperties: false`
// and no `$schema`. Stripping them is lossless for our purposes -- the registry validates the
// arguments itself before a tool ever runs, so the schema sent to the model is a *hint*, and the
// enforcement is local.
json relaxSchema(json schema) {
    if (schema.is_object()) {
        schema.erase("additionalProperties");
        schema.erase("$schema");
        if (const auto props = schema.find("properties"); props != schema.end() && props->is_object()) {
            for (auto& [_, sub] : props->items()) {
                sub = relaxSchema(sub);
            }
        }
        if (const auto items = schema.find("items"); items != schema.end()) {
            *items = relaxSchema(*items);
        }
    }
    return schema;
}

// ================================================================================================
// Anthropic -- POST /v1/messages
// ================================================================================================

class AnthropicProvider final : public HttpProvider {
public:
    using HttpProvider::HttpProvider;

    [[nodiscard]] ProviderCapabilities capabilities() const override {
        ProviderCapabilities c;
        c.tools = true;
        c.streaming = false;
        c.systemPrompt = true;
        c.temperature = true;
        c.modelListing = true;
        c.reasoning = true;
        c.vision = true;
        c.maxContextTokens = 1000000;
        return c;
    }

    [[nodiscard]] std::string defaultModel() const override { return "claude-opus-5"; }

    Result<CompletionResponse> complete(const CompletionRequest& request) override {
        json body;
        body["model"] = request.model.empty() ? defaultModel() : request.model;
        body["max_tokens"] = request.maxOutputTokens;
        if (!request.system.empty()) {
            body["system"] = request.system;
        }
        json tools = json::array();
        for (const ToolDefinition* tool : request.tools) {
            tools.push_back(json{{"name", tool->name},
                                 {"description", tool->description},
                                 {"input_schema", tool->inputSchema}});
        }
        if (!tools.empty()) {
            body["tools"] = std::move(tools);
        }
        json messages = json::array();
        for (const Message& message : request.messages) {
            json content = json::array();
            if (!message.text.empty()) {
                content.push_back(json{{"type", "text"}, {"text", message.text}});
            }
            for (const ToolCall& call : message.toolCalls) {
                content.push_back(json{{"type", "tool_use"},
                                       {"id", call.id},
                                       {"name", call.name},
                                       {"input", call.arguments}});
            }
            for (const ToolCallResult& result : message.toolResults) {
                json block{{"type", "tool_result"},
                           {"tool_use_id", result.id},
                           {"content", result.content.dump()}};
                if (result.isError) {
                    block["is_error"] = true;
                }
                content.push_back(std::move(block));
            }
            if (content.empty()) {
                continue;
            }
            messages.push_back(
                json{{"role", message.role == Role::Assistant ? "assistant" : "user"},
                     {"content", std::move(content)}});
        }
        body["messages"] = std::move(messages);

        auto parsed = post(endpoint_ + "/v1/messages",
                           {{"x-api-key", secret_}, {"anthropic-version", "2023-06-01"}}, body,
                           request.cancel);
        if (!parsed) {
            return std::unexpected(parsed.error());
        }

        CompletionResponse out;
        out.model = parsed->value("model", std::string{});
        for (const json& block : parsed->value("content", json::array())) {
            const std::string type = block.value("type", std::string{});
            if (type == "text") {
                out.text += block.value("text", std::string{});
            } else if (type == "tool_use") {
                ToolCall call;
                call.id = block.value("id", std::string{});
                call.name = block.value("name", std::string{});
                call.arguments = block.value("input", json::object());
                out.toolCalls.push_back(std::move(call));
            }
        }
        const std::string stop = parsed->value("stop_reason", std::string("end_turn"));
        out.stopReason = stop == "tool_use"    ? StopReason::ToolUse
                         : stop == "max_tokens" ? StopReason::MaxTokens
                         : stop == "refusal"    ? StopReason::Refusal
                         : stop == "end_turn"   ? StopReason::EndTurn
                                                : StopReason::Other;
        if (const auto usage = parsed->find("usage"); usage != parsed->end()) {
            out.usage.inputTokens = usage->value("input_tokens", 0ULL);
            out.usage.outputTokens = usage->value("output_tokens", 0ULL);
            out.usage.cachedInputTokens = usage->value("cache_read_input_tokens", 0ULL);
        }
        emitText(request, out.text);
        return out;
    }

    Result<std::vector<ProviderModel>> listModels() override {
        auto parsed = get(endpoint_ + "/v1/models",
                          {{"x-api-key", secret_}, {"anthropic-version", "2023-06-01"}},
                          CancelToken{});
        if (!parsed) {
            return std::unexpected(parsed.error());
        }
        std::vector<ProviderModel> models;
        for (const json& entry : parsed->value("data", json::array())) {
            models.push_back(ProviderModel{entry.value("id", std::string{}),
                                           entry.value("display_name", std::string{})});
        }
        return models;
    }
};

// ================================================================================================
// OpenAI Chat Completions -- also every OpenAI-compatible server (llama-server, LM Studio,
// Ollama, vLLM), which is why local inference costs no additional adapter.
// ================================================================================================

class OpenAiProvider final : public HttpProvider {
public:
    OpenAiProvider(ProviderConfig config, std::string secret, std::string endpoint,
                   std::string defaultModel, bool localish)
        : HttpProvider(std::move(config), std::move(secret), std::move(endpoint)),
          defaultModel_(std::move(defaultModel)), local_(localish) {}

    [[nodiscard]] ProviderCapabilities capabilities() const override {
        ProviderCapabilities c;
        c.tools = true;
        c.streaming = false;
        c.systemPrompt = true;
        c.temperature = true;
        c.modelListing = true;
        c.reasoning = !local_;
        c.vision = !local_;
        return c;
    }

    [[nodiscard]] std::string defaultModel() const override { return defaultModel_; }

    Result<CompletionResponse> complete(const CompletionRequest& request) override {
        json body;
        body["model"] = request.model.empty() ? defaultModel_ : request.model;
        // `max_tokens` is deprecated in the current OpenAI schema and is rejected outright by the
        // reasoning models. `max_completion_tokens` is the field; compatible servers accept it.
        body["max_completion_tokens"] = request.maxOutputTokens;
        body["temperature"] = request.temperature;

        json messages = json::array();
        if (!request.system.empty()) {
            messages.push_back(json{{"role", "system"}, {"content", request.system}});
        }
        for (const Message& message : request.messages) {
            if (!message.toolResults.empty()) {
                // One message per result, each carrying its tool_call_id. Batching them into one
                // would drop the correlation the API requires.
                for (const ToolCallResult& result : message.toolResults) {
                    messages.push_back(json{{"role", "tool"},
                                            {"tool_call_id", result.id},
                                            {"content", result.content.dump()}});
                }
                continue;
            }
            json m;
            m["role"] = message.role == Role::Assistant ? "assistant" : "user";
            m["content"] = message.text.empty() ? json(nullptr) : json(message.text);
            if (!message.toolCalls.empty()) {
                json calls = json::array();
                for (const ToolCall& call : message.toolCalls) {
                    calls.push_back(json{{"id", call.id},
                                         {"type", "function"},
                                         {"function", json{{"name", call.name},
                                                           // arguments is a *string* here
                                                           {"arguments", call.arguments.dump()}}}});
                }
                m["tool_calls"] = std::move(calls);
            }
            messages.push_back(std::move(m));
        }
        body["messages"] = std::move(messages);

        json tools = json::array();
        for (const ToolDefinition* tool : request.tools) {
            tools.push_back(json{{"type", "function"},
                                 {"function", json{{"name", tool->name},
                                                   {"description", tool->description},
                                                   {"parameters", tool->inputSchema}}}});
        }
        if (!tools.empty()) {
            body["tools"] = std::move(tools);
            // tool_choice is deliberately omitted: Ollama documents that it does not support the
            // field, and "auto" is the default everywhere, so sending it buys nothing and breaks
            // one of the servers this adapter exists to reach.
        }

        std::vector<std::pair<std::string, std::string>> headers;
        if (!secret_.empty()) {
            headers.emplace_back("Authorization", "Bearer " + secret_);
        }
        auto parsed = post(endpoint_ + "/v1/chat/completions", std::move(headers), body,
                           request.cancel);
        if (!parsed) {
            return std::unexpected(parsed.error());
        }

        CompletionResponse out;
        out.model = parsed->value("model", std::string{});
        const json choices = parsed->value("choices", json::array());
        if (choices.empty()) {
            return fail("{} returned no choices", config_.displayName);
        }
        const json& choice = choices[0];
        const json message = choice.value("message", json::object());
        if (message.contains("content") && message.at("content").is_string()) {
            out.text = message.at("content").get<std::string>();
        }
        for (const json& call : message.value("tool_calls", json::array())) {
            ToolCall tc;
            tc.id = call.value("id", std::string{});
            const json function = call.value("function", json::object());
            tc.name = function.value("name", std::string{});
            // Documented as possibly not valid JSON. A malformed argument string is the model's
            // mistake and has to reach the model as a recoverable tool error, not crash the loop --
            // so it becomes an object carrying the raw text and the tool's schema validation
            // rejects it with a message the model can read.
            const std::string raw = function.value("arguments", std::string("{}"));
            json args = json::parse(raw, nullptr, false);
            if (args.is_discarded() || !args.is_object()) {
                log::warn("ai: {} produced unparseable tool arguments for '{}'",
                          config_.displayName, tc.name);
                args = json{{"__malformed_arguments", raw}};
            }
            tc.arguments = std::move(args);
            out.toolCalls.push_back(std::move(tc));
        }
        const std::string finish = choice.value("finish_reason", std::string("stop"));
        out.stopReason = finish == "tool_calls" ? StopReason::ToolUse
                         : finish == "length"   ? StopReason::MaxTokens
                         : finish == "content_filter" ? StopReason::Refusal
                                                      : StopReason::EndTurn;
        // A server that sets finish_reason "stop" while returning tool calls (several compatible
        // servers do) must still drive the loop forward.
        if (!out.toolCalls.empty()) {
            out.stopReason = StopReason::ToolUse;
        }
        if (const auto usage = parsed->find("usage"); usage != parsed->end()) {
            out.usage.inputTokens = usage->value("prompt_tokens", 0ULL);
            out.usage.outputTokens = usage->value("completion_tokens", 0ULL);
            if (const auto details = usage->find("prompt_tokens_details"); details != usage->end()) {
                out.usage.cachedInputTokens = details->value("cached_tokens", 0ULL);
            }
        }
        emitText(request, out.text);
        return out;
    }

    Result<std::vector<ProviderModel>> listModels() override {
        std::vector<std::pair<std::string, std::string>> headers;
        if (!secret_.empty()) {
            headers.emplace_back("Authorization", "Bearer " + secret_);
        }
        auto parsed = get(endpoint_ + "/v1/models", std::move(headers), CancelToken{});
        if (!parsed) {
            return std::unexpected(parsed.error());
        }
        std::vector<ProviderModel> models;
        for (const json& entry : parsed->value("data", json::array())) {
            const std::string id = entry.value("id", std::string{});
            models.push_back(ProviderModel{id, id});
        }
        return models;
    }

private:
    std::string defaultModel_;
    bool local_ = false;
};

// ================================================================================================
// Google Gemini -- POST /v1beta/models/{model}:generateContent
// ================================================================================================

class GeminiProvider final : public HttpProvider {
public:
    using HttpProvider::HttpProvider;

    [[nodiscard]] ProviderCapabilities capabilities() const override {
        ProviderCapabilities c;
        c.tools = true;
        c.streaming = false;
        c.systemPrompt = true;
        c.temperature = true;
        c.modelListing = true;
        c.vision = true;
        c.reasoning = true;
        return c;
    }

    [[nodiscard]] std::string defaultModel() const override { return "gemini-2.5-pro"; }

    Result<CompletionResponse> complete(const CompletionRequest& request) override {
        const std::string model = request.model.empty() ? defaultModel() : request.model;
        json body;
        if (!request.system.empty()) {
            body["systemInstruction"] = json{{"parts", json::array({json{{"text", request.system}}})}};
        }
        json contents = json::array();
        for (const Message& message : request.messages) {
            json parts = json::array();
            if (!message.text.empty()) {
                parts.push_back(json{{"text", message.text}});
            }
            for (const ToolCall& call : message.toolCalls) {
                parts.push_back(
                    json{{"functionCall", json{{"name", call.name}, {"args", call.arguments}}}});
            }
            for (const ToolCallResult& result : message.toolResults) {
                // Gemini has no tool role: a result is a user turn carrying functionResponse parts.
                parts.push_back(json{
                    {"functionResponse",
                     json{{"name", result.name}, {"response", wrapResponse(result.content)}}}});
            }
            if (parts.empty()) {
                continue;
            }
            // Only "user" and "model" are legal roles. A tool result is a user turn.
            const bool assistant = message.role == Role::Assistant && message.toolResults.empty();
            contents.push_back(
                json{{"role", assistant ? "model" : "user"}, {"parts", std::move(parts)}});
        }
        body["contents"] = std::move(contents);

        json declarations = json::array();
        for (const ToolDefinition* tool : request.tools) {
            declarations.push_back(json{{"name", tool->name},
                                        {"description", tool->description},
                                        {"parameters", relaxSchema(tool->inputSchema)}});
        }
        if (!declarations.empty()) {
            body["tools"] = json::array({json{{"functionDeclarations", std::move(declarations)}}});
        }
        body["generationConfig"] = json{{"temperature", request.temperature},
                                        {"maxOutputTokens", request.maxOutputTokens}};

        // The key goes in a header, never in the query string: a URL is the thing that ends up in
        // proxy logs, crash reports and shell history.
        auto parsed = post(fmt::format("{}/v1beta/models/{}:generateContent", endpoint_, model),
                           {{"x-goog-api-key", secret_}}, body, request.cancel);
        if (!parsed) {
            return std::unexpected(parsed.error());
        }

        CompletionResponse out;
        out.model = model;
        const json candidates = parsed->value("candidates", json::array());
        if (candidates.empty()) {
            const json feedback = parsed->value("promptFeedback", json::object());
            if (feedback.contains("blockReason")) {
                out.stopReason = StopReason::Refusal;
                out.text = fmt::format("blocked: {}",
                                       feedback.value("blockReason", std::string("unspecified")));
                return out;
            }
            return fail("Gemini returned no candidates");
        }
        int callIndex = 0;
        for (const json& part : candidates[0].value("content", json::object())
                                    .value("parts", json::array())) {
            if (part.contains("text") && part.at("text").is_string()) {
                out.text += part.at("text").get<std::string>();
            } else if (part.contains("functionCall")) {
                const json& call = part.at("functionCall");
                ToolCall tc;
                // Gemini's functionCall carries no id in every response shape, and the loop needs a
                // stable one to correlate the result. Synthesised, and used only internally.
                tc.id = call.value("id", fmt::format("gemini-call-{}", callIndex++));
                tc.name = call.value("name", std::string{});
                tc.arguments = call.value("args", json::object());
                out.toolCalls.push_back(std::move(tc));
            }
        }
        const std::string finish = candidates[0].value("finishReason", std::string("STOP"));
        out.stopReason = finish == "MAX_TOKENS" ? StopReason::MaxTokens
                         : (finish == "SAFETY" || finish == "PROHIBITED_CONTENT" ||
                            finish == "BLOCKLIST" || finish == "SPII")
                             ? StopReason::Refusal
                         : finish == "STOP" ? StopReason::EndTurn
                                            : StopReason::Other;
        if (!out.toolCalls.empty()) {
            out.stopReason = StopReason::ToolUse;
        }
        if (const auto usage = parsed->find("usageMetadata"); usage != parsed->end()) {
            out.usage.inputTokens = usage->value("promptTokenCount", 0ULL);
            out.usage.outputTokens = usage->value("candidatesTokenCount", 0ULL);
            out.usage.cachedInputTokens = usage->value("cachedContentTokenCount", 0ULL);
        }
        emitText(request, out.text);
        return out;
    }

    Result<std::vector<ProviderModel>> listModels() override {
        auto parsed = get(endpoint_ + "/v1beta/models", {{"x-goog-api-key", secret_}}, CancelToken{});
        if (!parsed) {
            return std::unexpected(parsed.error());
        }
        std::vector<ProviderModel> models;
        for (const json& entry : parsed->value("models", json::array())) {
            std::string name = entry.value("name", std::string{});
            // "models/gemini-2.5-pro" -> "gemini-2.5-pro"
            if (const auto slash = name.rfind('/'); slash != std::string::npos) {
                name = name.substr(slash + 1);
            }
            models.push_back(ProviderModel{name, entry.value("displayName", name)});
        }
        return models;
    }

private:
    // `functionResponse.response` must be an object. A tool result that is an array or a scalar
    // gets wrapped rather than rejected.
    static json wrapResponse(const json& content) {
        return content.is_object() ? content : json{{"result", content}};
    }
};

} // namespace

bool setProviderTransportForTesting(Provider& provider, std::shared_ptr<HttpClient> transport) {
    auto* http = dynamic_cast<HttpProvider*>(&provider);
    if (http == nullptr) {
        return false;
    }
    http->setHttpClient(std::move(transport));
    return true;
}

// ================================================================================================

void registerBuiltinProviders(ProviderRegistry& registry) {
    registry.add(ProviderRegistry::Entry{
        .id = "anthropic",
        .displayName = "Anthropic",
        .requiresCredential = true,
        .requiresEndpoint = false,
        .defaultEndpoint = "https://api.anthropic.com",
        .defaultModel = "claude-opus-5",
        .credentialHint = "An API key from the Anthropic console.",
        .create = [](const ProviderConfig& config, std::string secret)
            -> Result<std::unique_ptr<Provider>> {
            ProviderConfig c = config;
            if (c.displayName.empty()) {
                c.displayName = "Anthropic";
            }
            return std::make_unique<AnthropicProvider>(
                std::move(c), std::move(secret),
                config.endpoint.empty() ? "https://api.anthropic.com" : config.endpoint);
        }});

    registry.add(ProviderRegistry::Entry{
        .id = "openai",
        .displayName = "OpenAI",
        .requiresCredential = true,
        .requiresEndpoint = false,
        .defaultEndpoint = "https://api.openai.com",
        .defaultModel = "gpt-5",
        .credentialHint = "An API key from the OpenAI platform dashboard.",
        .create = [](const ProviderConfig& config, std::string secret)
            -> Result<std::unique_ptr<Provider>> {
            ProviderConfig c = config;
            if (c.displayName.empty()) {
                c.displayName = "OpenAI";
            }
            return std::make_unique<OpenAiProvider>(
                std::move(c), std::move(secret),
                config.endpoint.empty() ? "https://api.openai.com" : config.endpoint,
                config.model.empty() ? "gpt-5" : config.model, false);
        }});

    registry.add(ProviderRegistry::Entry{
        .id = "gemini",
        .displayName = "Google Gemini",
        .requiresCredential = true,
        .requiresEndpoint = false,
        .defaultEndpoint = "https://generativelanguage.googleapis.com",
        .defaultModel = "gemini-2.5-pro",
        .credentialHint = "An API key from Google AI Studio.",
        .create = [](const ProviderConfig& config, std::string secret)
            -> Result<std::unique_ptr<Provider>> {
            ProviderConfig c = config;
            if (c.displayName.empty()) {
                c.displayName = "Google Gemini";
            }
            return std::make_unique<GeminiProvider>(
                std::move(c), std::move(secret),
                config.endpoint.empty() ? "https://generativelanguage.googleapis.com"
                                        : config.endpoint);
        }});

    // Any server speaking the OpenAI chat-completions shape: a self-hosted gateway, vLLM, or a
    // vendor not in the list above.
    registry.add(ProviderRegistry::Entry{
        .id = "openai-compatible",
        .displayName = "OpenAI-compatible endpoint",
        .requiresCredential = false,
        .requiresEndpoint = true,
        .defaultEndpoint = {},
        .defaultModel = {},
        .credentialHint = "Whatever the endpoint expects as a bearer token; leave empty if none.",
        .create = [](const ProviderConfig& config, std::string secret)
            -> Result<std::unique_ptr<Provider>> {
            ProviderConfig c = config;
            if (c.displayName.empty()) {
                c.displayName = "OpenAI-compatible endpoint";
            }
            return std::make_unique<OpenAiProvider>(std::move(c), std::move(secret), config.endpoint,
                                                    config.model, true);
        }});

    // Local inference. **The same adapter**, because the decision recorded in
    // docs/decisions/ADR-094 is that a local model is reached as a process rather than embedded --
    // so "local" is a different endpoint and a different default, not different code. It is a
    // separate registry entry rather than a note on the one above because §5 asks for a local
    // option a user can *choose*, and "OpenAI-compatible endpoint, and by the way type
    // 127.0.0.1 here" is not that.
    registry.add(ProviderRegistry::Entry{
        .id = "local",
        .displayName = "Local model (llama.cpp server)",
        .requiresCredential = false,
        .requiresEndpoint = true,
        .defaultEndpoint = "http://127.0.0.1:8080",
        .defaultModel = {},
        .credentialHint =
            "Run: llama-server -m <model>.gguf --port 8080 --api-key <a random string>, and put "
            "that same string here. Never start it with --tools, --agent or --mcp-servers: AV "
            "Gen's tool layer is the security boundary, and those flags give the server its own "
            "shell and filesystem access outside it.",
        .create = [](const ProviderConfig& config, std::string secret)
            -> Result<std::unique_ptr<Provider>> {
            ProviderConfig c = config;
            if (c.displayName.empty()) {
                c.displayName = "Local model";
            }
            return std::make_unique<OpenAiProvider>(
                std::move(c), std::move(secret),
                config.endpoint.empty() ? "http://127.0.0.1:8080" : config.endpoint, config.model,
                true);
        }});
}

} // namespace avgen::ai
