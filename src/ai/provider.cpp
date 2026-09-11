#include "ai/provider.hpp"

#include <algorithm>

namespace avgen::ai {

Message Message::user(std::string text) {
    Message m;
    m.role = Role::User;
    m.text = std::move(text);
    return m;
}

Message Message::assistant(std::string text, std::vector<ToolCall> calls) {
    Message m;
    m.role = Role::Assistant;
    m.text = std::move(text);
    m.toolCalls = std::move(calls);
    return m;
}

Message Message::results(std::vector<ToolCallResult> results) {
    Message m;
    m.role = Role::User; // every provider carries tool results on a user turn, by whatever name
    m.toolResults = std::move(results);
    return m;
}

const char* stopReasonName(StopReason reason) {
    switch (reason) {
    case StopReason::EndTurn: return "end_turn";
    case StopReason::ToolUse: return "tool_use";
    case StopReason::MaxTokens: return "max_tokens";
    case StopReason::Refusal: return "refusal";
    case StopReason::Other: return "other";
    }
    return "other";
}

Usage& Usage::operator+=(const Usage& other) {
    inputTokens += other.inputTokens;
    outputTokens += other.outputTokens;
    cachedInputTokens += other.cachedInputTokens;
    return *this;
}

nlohmann::json Usage::toJson() const {
    nlohmann::json j;
    j["inputTokens"] = inputTokens;
    j["outputTokens"] = outputTokens;
    if (cachedInputTokens > 0) {
        j["cachedInputTokens"] = cachedInputTokens;
    }
    return j;
}

nlohmann::json ProviderCapabilities::toJson() const {
    nlohmann::json j;
    j["tools"] = tools;
    j["streaming"] = streaming;
    j["systemPrompt"] = systemPrompt;
    j["temperature"] = temperature;
    j["modelListing"] = modelListing;
    j["vision"] = vision;
    j["reasoning"] = reasoning;
    if (maxContextTokens > 0) {
        j["maxContextTokens"] = maxContextTokens;
    }
    return j;
}

nlohmann::json ProviderConfig::toJson() const {
    // Note what is absent: there is no secret field, and there is no place to put one. A
    // credential cannot be written to a project or a settings file by accident because this
    // function is the only way configuration is serialised and it has nothing to write.
    nlohmann::json j;
    j["id"] = id;
    if (!displayName.empty()) {
        j["displayName"] = displayName;
    }
    if (!endpoint.empty()) {
        j["endpoint"] = endpoint;
    }
    if (!model.empty()) {
        j["model"] = model;
    }
    if (!credentialAccount.empty() && credentialAccount != id) {
        j["credentialAccount"] = credentialAccount;
    }
    j["enabled"] = enabled;
    j["temperature"] = temperature;
    j["maxOutputTokens"] = maxOutputTokens;
    return j;
}

Result<ProviderConfig> ProviderConfig::fromJson(const nlohmann::json& j) {
    if (!j.is_object()) {
        return fail("provider config must be an object");
    }
    if (!j.contains("id") || !j.at("id").is_string()) {
        return fail("provider config needs a string 'id'");
    }
    ProviderConfig c;
    c.id = j.at("id").get<std::string>();
    c.displayName = j.value("displayName", std::string{});
    c.endpoint = j.value("endpoint", std::string{});
    c.model = j.value("model", std::string{});
    c.credentialAccount = j.value("credentialAccount", c.id);
    c.enabled = j.value("enabled", false);
    c.temperature = std::clamp(j.value("temperature", 0.4), 0.0, 2.0);
    c.maxOutputTokens = std::clamp(j.value("maxOutputTokens", 8192u), 256u, 200000u);
    // A settings file that somehow carries a key -- hand-edited, or written by a future version
    // that got this wrong -- must not have it read back into memory and must not be trusted. This
    // is the last line of the "credentials never live in files" rule, and it is a hard error
    // rather than a silent drop so that whoever put it there finds out.
    for (const char* forbidden : {"apiKey", "api_key", "key", "secret", "token", "credential"}) {
        if (j.contains(forbidden)) {
            return fail("provider '{}': settings carry a '{}' field; credentials belong in the "
                        "system keystore and must never be written to a file",
                        c.id, forbidden);
        }
    }
    return c;
}

Result<std::string> Provider::testConnection() {
    CompletionRequest request;
    request.model = defaultModel();
    request.system = "Reply with the single word: ok";
    request.messages.push_back(Message::user("ok"));
    request.maxOutputTokens = 16;
    request.temperature = 0.0;
    auto response = complete(request);
    if (!response) {
        return std::unexpected(response.error());
    }
    return fmt::format("{} responded ({} in, {} out)", response->model.empty() ? request.model
                                                                              : response->model,
                       response->usage.inputTokens, response->usage.outputTokens);
}

Result<std::vector<ProviderModel>> Provider::listModels() {
    return std::vector<ProviderModel>{};
}

void ProviderRegistry::add(Entry entry) {
    const auto it = std::find_if(entries_.begin(), entries_.end(),
                                 [&](const Entry& e) { return e.id == entry.id; });
    if (it != entries_.end()) {
        *it = std::move(entry);
        return;
    }
    entries_.push_back(std::move(entry));
}

const ProviderRegistry::Entry* ProviderRegistry::find(std::string_view id) const {
    const auto it =
        std::find_if(entries_.begin(), entries_.end(), [&](const Entry& e) { return e.id == id; });
    return it == entries_.end() ? nullptr : &*it;
}

std::vector<std::string> ProviderRegistry::ids() const {
    std::vector<std::string> out;
    out.reserve(entries_.size());
    for (const Entry& e : entries_) {
        out.push_back(e.id);
    }
    return out;
}

Result<std::unique_ptr<Provider>> ProviderRegistry::create(const ProviderConfig& config,
                                                           std::string secret) const {
    const Entry* entry = find(config.id);
    if (entry == nullptr) {
        return fail("no provider '{}' in this build", config.id);
    }
    if (entry->requiresCredential && secret.empty()) {
        return fail("{} has no credential stored; add one in Settings -> AI", entry->displayName);
    }
    const std::string endpoint = config.endpoint.empty() ? entry->defaultEndpoint : config.endpoint;
    if (entry->requiresEndpoint && endpoint.empty()) {
        return fail("{} needs an endpoint URL", entry->displayName);
    }
    return entry->create(config, std::move(secret));
}

} // namespace avgen::ai
