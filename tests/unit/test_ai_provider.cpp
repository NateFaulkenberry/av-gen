// The provider abstraction (ADR-094): wire-format translation, credential storage and settings.
//
// §52 permits exactly one mock -- the provider's network -- and this is where it is used. Each
// adapter is driven against a recorded response body and asserted in both directions: that the
// request it *sent* has the shape the vendor documents, and that the response it parsed came back
// as AV Gen's normalized types. No network, no key, no cloud model.
//
// The credential tests are the ones that matter most. A regression that let a secret into a
// settings file would be invisible until somebody's key appeared in a git diff.

#include "ai/credentials.hpp"
#include "ai/http.hpp"
#include "ai/provider.hpp"
#include "ai/scripted_provider.hpp"
#include "ai/tool_api.hpp"

#include <catch2/catch_test_macros.hpp>

#include <memory>

using namespace avgen;
using nlohmann::json;

namespace {

ai::ToolDefinition sampleTool() {
    ai::ToolDefinition tool;
    tool.name = "parameter.set";
    tool.title = "Set a parameter";
    tool.description = "Set a parameter's base value.";
    tool.inputSchema = ai::schema::object(
        {{"path", ai::schema::string("Parameter path")}, {"value", ai::schema::number("Value")}},
        {"path", "value"});
    // Metadata the model must never see.
    tool.annotations.mutatesProject = true;
    tool.annotations.destructive = true;
    tool.annotations.expensive = true;
    return tool;
}

struct Harness {
    ai::ProviderRegistry registry;
    std::shared_ptr<ai::ScriptedHttpClient> http = std::make_shared<ai::ScriptedHttpClient>();
    ai::ToolDefinition tool = sampleTool();

    Harness() { ai::registerBuiltinProviders(registry); }

    std::unique_ptr<ai::Provider> build(const std::string& id, std::string endpoint = {}) {
        ai::ProviderConfig config;
        config.id = id;
        config.endpoint = std::move(endpoint);
        auto provider = registry.create(config, "test-secret");
        REQUIRE(provider.has_value());
        REQUIRE(ai::setProviderTransportForTesting(**provider, http));
        return std::move(*provider);
    }

    ai::CompletionRequest request() {
        ai::CompletionRequest r;
        r.system = "You are operating AV Gen.";
        r.messages.push_back(ai::Message::user("Make the orb brighter."));
        r.tools.push_back(&tool);
        r.maxOutputTokens = 4096;
        r.temperature = 0.3;
        return r;
    }

    void reply(std::string body, int status = 200) {
        http->push(ai::ScriptedHttpClient::Exchange{{}, ai::HttpResponse{status, std::move(body)}});
    }
};

} // namespace

TEST_CASE("Every built-in provider is reachable by id and needs no code to add another",
          "[ai][provider]") {
    ai::ProviderRegistry registry;
    ai::registerBuiltinProviders(registry);
    const auto ids = registry.ids();
    for (const char* expected : {"anthropic", "openai", "gemini", "openai-compatible", "local"}) {
        INFO(expected);
        CHECK(std::find(ids.begin(), ids.end(), expected) != ids.end());
    }
    for (const ai::ProviderRegistry::Entry& entry : registry.entries()) {
        INFO(entry.id);
        CHECK(!entry.displayName.empty());
        CHECK(static_cast<bool>(entry.create));
        // A hint about where a key comes from -- never a key.
        if (entry.requiresCredential) {
            CHECK(!entry.credentialHint.empty());
        }
    }
    SECTION("a provider needing a credential refuses to build without one") {
        ai::ProviderConfig config;
        config.id = "anthropic";
        const auto result = registry.create(config, "");
        REQUIRE_FALSE(result.has_value());
        CHECK(result.error().message.find("Settings") != std::string::npos);
    }
    SECTION("a provider needing an endpoint refuses to build without one") {
        ai::ProviderConfig config;
        config.id = "openai-compatible";
        const auto result = registry.create(config, "");
        REQUIRE_FALSE(result.has_value());
        CHECK(result.error().message.find("endpoint") != std::string::npos);
    }
    SECTION("local defaults to a loopback endpoint and needs no credential") {
        const ai::ProviderRegistry::Entry* local = registry.find("local");
        REQUIRE(local != nullptr);
        CHECK(local->defaultEndpoint.find("127.0.0.1") != std::string::npos);
        CHECK_FALSE(local->requiresCredential);
        // The security note the llama.cpp research turned up belongs where a user will read it.
        CHECK(local->credentialHint.find("--tools") != std::string::npos);
    }
}

TEST_CASE("The Anthropic adapter speaks /v1/messages", "[ai][provider]") {
    Harness h;
    auto provider = h.build("anthropic");
    h.reply(R"({
      "model": "claude-opus-5",
      "stop_reason": "tool_use",
      "content": [
        {"type": "text", "text": "Raising the emission."},
        {"type": "tool_use", "id": "toolu_1", "name": "parameter.set",
         "input": {"path": "orb/emissive", "value": 4.0}}
      ],
      "usage": {"input_tokens": 120, "output_tokens": 42, "cache_read_input_tokens": 90}
    })");

    const auto response = provider->complete(h.request());
    REQUIRE(response.has_value());
    CHECK(response->text == "Raising the emission.");
    CHECK(response->stopReason == ai::StopReason::ToolUse);
    REQUIRE(response->toolCalls.size() == 1);
    CHECK(response->toolCalls[0].id == "toolu_1");
    CHECK(response->toolCalls[0].name == "parameter.set");
    CHECK(response->toolCalls[0].arguments.at("path") == "orb/emissive");
    CHECK(response->usage.inputTokens == 120);
    CHECK(response->usage.cachedInputTokens == 90);

    SECTION("and sends the shape Anthropic documents") {
        REQUIRE(h.http->sent().size() == 1);
        const ai::HttpRequest& sent = h.http->sent().front();
        CHECK(sent.url.find("/v1/messages") != std::string::npos);
        const json body = json::parse(sent.body);
        CHECK(body.at("system") == "You are operating AV Gen.");
        CHECK(body.at("max_tokens") == 4096);
        REQUIRE(body.at("tools").size() == 1);
        CHECK(body.at("tools").at(0).at("name") == "parameter.set");
        CHECK(body.at("tools").at(0).contains("input_schema"));
        CHECK(body.at("messages").at(0).at("role") == "user");
    }
    SECTION("the credential travels in a header and never in the body or the URL") {
        const ai::HttpRequest& sent = h.http->sent().front();
        CHECK(sent.body.find("test-secret") == std::string::npos);
        CHECK(sent.url.find("test-secret") == std::string::npos);
        bool sawHeader = false;
        for (const auto& [name, value] : sent.headers) {
            if (name == "x-api-key") {
                sawHeader = true;
                CHECK(value == "test-secret");
            }
        }
        CHECK(sawHeader);
    }
    SECTION("tool annotations are never sent to the model") {
        const json body = json::parse(h.http->sent().front().body);
        const std::string dumped = body.dump();
        CHECK(dumped.find("destructive") == std::string::npos);
        CHECK(dumped.find("mutatesProject") == std::string::npos);
        CHECK(dumped.find("requiresMainThread") == std::string::npos);
    }
}

TEST_CASE("The Anthropic adapter sends tool results back as tool_result blocks", "[ai][provider]") {
    Harness h;
    auto provider = h.build("anthropic");
    h.reply(R"({"model":"claude-opus-5","stop_reason":"end_turn",
                "content":[{"type":"text","text":"Done."}],
                "usage":{"input_tokens":10,"output_tokens":5}})");

    ai::CompletionRequest request = h.request();
    ai::ToolCall call{"toolu_1", "parameter.set", json{{"path", "orb/emissive"}, {"value", 4.0}}};
    request.messages.push_back(ai::Message::assistant("Setting it.", {call}));
    request.messages.push_back(
        ai::Message::results({ai::ToolCallResult{"toolu_1", "parameter.set",
                                                 json{{"success", true}}, false}}));

    REQUIRE(provider->complete(request).has_value());
    const json body = json::parse(h.http->sent().front().body);
    REQUIRE(body.at("messages").size() == 3);
    CHECK(body.at("messages").at(1).at("role") == "assistant");
    CHECK(body.at("messages").at(1).at("content").at(1).at("type") == "tool_use");
    CHECK(body.at("messages").at(2).at("role") == "user");
    CHECK(body.at("messages").at(2).at("content").at(0).at("type") == "tool_result");
    CHECK(body.at("messages").at(2).at("content").at(0).at("tool_use_id") == "toolu_1");
}

TEST_CASE("The OpenAI adapter speaks chat completions", "[ai][provider]") {
    Harness h;
    auto provider = h.build("openai");
    h.reply(R"({
      "model": "gpt-5",
      "choices": [{
        "finish_reason": "tool_calls",
        "message": {
          "role": "assistant",
          "content": null,
          "tool_calls": [{"id": "call_1", "type": "function",
                          "function": {"name": "parameter.set",
                                       "arguments": "{\"path\":\"orb/emissive\",\"value\":4}"}}]
        }
      }],
      "usage": {"prompt_tokens": 90, "completion_tokens": 12,
                "prompt_tokens_details": {"cached_tokens": 64}}
    })");

    const auto response = provider->complete(h.request());
    REQUIRE(response.has_value());
    CHECK(response->stopReason == ai::StopReason::ToolUse);
    REQUIRE(response->toolCalls.size() == 1);
    // arguments arrive as a JSON *string* and are parsed into an object.
    CHECK(response->toolCalls[0].arguments.at("path") == "orb/emissive");
    CHECK(response->usage.inputTokens == 90);
    CHECK(response->usage.cachedInputTokens == 64);

    SECTION("the system prompt is the first message and max_completion_tokens is used") {
        const json body = json::parse(h.http->sent().front().body);
        CHECK(body.at("messages").at(0).at("role") == "system");
        // `max_tokens` is deprecated in the current schema and rejected by reasoning models.
        CHECK(body.contains("max_completion_tokens"));
        CHECK_FALSE(body.contains("max_tokens"));
        // tool_choice is omitted on purpose: Ollama documents that it does not support the field.
        CHECK_FALSE(body.contains("tool_choice"));
        CHECK(body.at("tools").at(0).at("function").at("name") == "parameter.set");
    }
    SECTION("the credential is a bearer header") {
        bool sawHeader = false;
        for (const auto& [name, value] : h.http->sent().front().headers) {
            if (name == "Authorization") {
                sawHeader = true;
                CHECK(value == "Bearer test-secret");
            }
        }
        CHECK(sawHeader);
    }
}

TEST_CASE("Unparseable tool arguments become a recoverable error, not a crash", "[ai][provider]") {
    // The OpenAI schema documents that `arguments` may not be valid JSON. A loop that threw here
    // would die on the model's mistake instead of letting the model correct it.
    Harness h;
    auto provider = h.build("openai");
    h.reply(R"({"model":"gpt-5","choices":[{"finish_reason":"tool_calls","message":{
        "tool_calls":[{"id":"call_1","type":"function",
                       "function":{"name":"parameter.set","arguments":"{not json"}}]}}],
        "usage":{"prompt_tokens":1,"completion_tokens":1}})");

    const auto response = provider->complete(h.request());
    REQUIRE(response.has_value());
    REQUIRE(response->toolCalls.size() == 1);
    CHECK(response->toolCalls[0].arguments.contains("__malformed_arguments"));
}

TEST_CASE("A compatible server that reports 'stop' while returning tool calls still loops",
          "[ai][provider][regression]") {
    // Several OpenAI-compatible servers do this. Trusting finish_reason alone would end the task
    // with the tools unrun and the model waiting.
    Harness h;
    auto provider = h.build("openai-compatible", "http://127.0.0.1:9999");
    h.reply(R"({"model":"local","choices":[{"finish_reason":"stop","message":{
        "tool_calls":[{"id":"c1","type":"function",
                       "function":{"name":"parameter.set","arguments":"{}"}}]}}],
        "usage":{}})");
    const auto response = provider->complete(h.request());
    REQUIRE(response.has_value());
    CHECK(response->stopReason == ai::StopReason::ToolUse);
}

TEST_CASE("The Gemini adapter speaks generateContent", "[ai][provider]") {
    Harness h;
    auto provider = h.build("gemini");
    h.reply(R"({
      "candidates": [{
        "finishReason": "STOP",
        "content": {"role": "model", "parts": [
          {"text": "Raising it."},
          {"functionCall": {"name": "parameter.set",
                            "args": {"path": "orb/emissive", "value": 4.0}}}
        ]}
      }],
      "usageMetadata": {"promptTokenCount": 77, "candidatesTokenCount": 9}
    })");

    const auto response = provider->complete(h.request());
    REQUIRE(response.has_value());
    CHECK(response->text == "Raising it.");
    REQUIRE(response->toolCalls.size() == 1);
    // Gemini's functionCall carries no id in this shape; the adapter synthesises a stable one so
    // the loop can correlate the result.
    CHECK(!response->toolCalls[0].id.empty());
    CHECK(response->toolCalls[0].arguments.at("value") == 4.0);
    CHECK(response->stopReason == ai::StopReason::ToolUse);
    CHECK(response->usage.inputTokens == 77);

    SECTION("the system instruction, roles and declarations are Gemini's spelling") {
        const ai::HttpRequest& sent = h.http->sent().front();
        CHECK(sent.url.find(":generateContent") != std::string::npos);
        const json body = json::parse(sent.body);
        CHECK(body.at("systemInstruction").at("parts").at(0).at("text") ==
              "You are operating AV Gen.");
        CHECK(body.at("contents").at(0).at("role") == "user");
        CHECK(body.at("tools").at(0).at("functionDeclarations").at(0).at("name") ==
              "parameter.set");
        // OpenAPI 3.03 Schema has no additionalProperties; sending it is rejected by some
        // deployments. Stripping it is safe because the registry validates arguments locally.
        CHECK_FALSE(body.at("tools").at(0).at("functionDeclarations").at(0).at("parameters")
                        .contains("additionalProperties"));
    }
    SECTION("the key goes in a header, not the query string") {
        const ai::HttpRequest& sent = h.http->sent().front();
        CHECK(sent.url.find("key=") == std::string::npos);
        bool sawHeader = false;
        for (const auto& [name, value] : sent.headers) {
            if (name == "x-goog-api-key") {
                sawHeader = true;
                CHECK(value == "test-secret");
            }
        }
        CHECK(sawHeader);
    }
}

TEST_CASE("Gemini tool results go back as a user turn carrying functionResponse",
          "[ai][provider]") {
    Harness h;
    auto provider = h.build("gemini");
    h.reply(R"({"candidates":[{"finishReason":"STOP","content":{"role":"model",
               "parts":[{"text":"Done."}]}}],"usageMetadata":{}})");

    ai::CompletionRequest request = h.request();
    request.messages.push_back(
        ai::Message::results({ai::ToolCallResult{"c1", "parameter.set", json{{"ok", true}}, false}}));
    REQUIRE(provider->complete(request).has_value());

    const json body = json::parse(h.http->sent().front().body);
    const json& last = body.at("contents").back();
    // Gemini has no tool role at all: "user" or "model" are the only legal values.
    CHECK(last.at("role") == "user");
    CHECK(last.at("parts").at(0).at("functionResponse").at("name") == "parameter.set");
    CHECK(last.at("parts").at(0).at("functionResponse").at("response").at("ok") == true);
}

TEST_CASE("HTTP failures are translated into messages a person can act on", "[ai][provider]") {
    SECTION("401 names the credential") {
        Harness h;
        auto provider = h.build("anthropic");
        h.reply(R"({"error":{"message":"invalid x-api-key"}})", 401);
        const auto response = provider->complete(h.request());
        REQUIRE_FALSE(response.has_value());
        CHECK(response.error().message.find("credential") != std::string::npos);
    }
    SECTION("429 says it was rate limited") {
        Harness h;
        auto provider = h.build("openai");
        h.reply(R"({"error":{"message":"slow down"}})", 429);
        const auto response = provider->complete(h.request());
        REQUIRE_FALSE(response.has_value());
        CHECK(response.error().message.find("rate limited") != std::string::npos);
    }
    SECTION("404 points at the model name and the endpoint") {
        Harness h;
        auto provider = h.build("openai");
        h.reply(R"({"error":{"message":"no such model"}})", 404);
        const auto response = provider->complete(h.request());
        REQUIRE_FALSE(response.has_value());
        CHECK(response.error().message.find("model name") != std::string::npos);
    }
    SECTION("a body that is not JSON is an error, not a parse crash") {
        Harness h;
        auto provider = h.build("openai");
        h.reply("<html>gateway error</html>");
        const auto response = provider->complete(h.request());
        REQUIRE_FALSE(response.has_value());
        CHECK(response.error().message.find("not JSON") != std::string::npos);
    }
}

// ---- credentials ------------------------------------------------------------------------------

TEST_CASE("A provider's configuration cannot carry a secret", "[ai][credentials]") {
    ai::ProviderConfig config;
    config.id = "anthropic";
    config.displayName = "Anthropic";
    config.endpoint = "https://api.anthropic.com";
    config.model = "claude-opus-5";
    config.enabled = true;

    const json serialised = config.toJson();
    const std::string dumped = serialised.dump();
    for (const char* forbidden : {"apiKey", "api_key", "secret", "token", "credential"}) {
        INFO(forbidden);
        CHECK(dumped.find(forbidden) == std::string::npos);
    }

    SECTION("and a file that carries one is refused rather than read") {
        // Hand-edited, or written by a future version that got this wrong. Either way the key must
        // not be silently loaded and used, and whoever put it there has to find out.
        json bad = serialised;
        bad["apiKey"] = "sk-should-not-be-here";
        const auto parsed = ai::ProviderConfig::fromJson(bad);
        REQUIRE_FALSE(parsed.has_value());
        CHECK(parsed.error().message.find("keystore") != std::string::npos);
    }
    SECTION("a round trip preserves the non-secret configuration") {
        const auto parsed = ai::ProviderConfig::fromJson(serialised);
        REQUIRE(parsed.has_value());
        CHECK(parsed->id == "anthropic");
        CHECK(parsed->model == "claude-opus-5");
        CHECK(parsed->enabled);
    }
}

TEST_CASE("The credential store reports status without revealing the value", "[ai][credentials]") {
    ai::MemoryCredentialStore store;
    CHECK_FALSE(store.status("openai").configured);

    REQUIRE(store.store("openai", "sk-secret-value").has_value());
    const ai::CredentialStatus status = store.status("openai");
    CHECK(status.configured);
    CHECK(status.source == ai::CredentialSource::Keystore);
    // The status text is what a UI renders. It must never contain the value.
    CHECK(status.detail.find("sk-secret-value") == std::string::npos);

    CHECK(store.has("openai"));
    CHECK(store.load("openai").value() == "sk-secret-value");
    REQUIRE(store.erase("openai").has_value());
    CHECK_FALSE(store.has("openai"));
}

TEST_CASE("The environment variable name is derived predictably", "[ai][credentials]") {
    CHECK(ai::CredentialStore::environmentVariableFor("openai") == "AVGEN_AI_OPENAI_KEY");
    CHECK(ai::CredentialStore::environmentVariableFor("openai-compatible") ==
          "AVGEN_AI_OPENAI_COMPATIBLE_KEY");
    CHECK(ai::CredentialStore::environmentVariableFor("gemini") == "AVGEN_AI_GEMINI_KEY");
}

TEST_CASE("The environment takes precedence, and the status says so", "[ai][credentials]") {
    // A headless or SSH session cannot always read the keystore, and an operator overriding a key
    // for one run must not have to write it into stored state. Because the environment wins, the
    // status has to say which source is in effect -- a UI claiming "stored in keychain" while a
    // stale variable was actually in use would be lying in the one place people look.
    ai::MemoryCredentialStore store;
    REQUIRE(store.store("openai", "from-keystore").has_value());
    CHECK(store.resolve("openai").value() == "from-keystore");

    const std::string variable = ai::CredentialStore::environmentVariableFor("openai");
    ::setenv(variable.c_str(), "from-environment", 1);
    CHECK(store.resolve("openai").value() == "from-environment");
    const ai::CredentialStatus status = store.status("openai");
    CHECK(status.source == ai::CredentialSource::Environment);
    CHECK(status.detail.find(variable) != std::string::npos);
    CHECK(status.detail.find("from-environment") == std::string::npos);
    ::unsetenv(variable.c_str());
}

// ---- the scripted provider ----------------------------------------------------------------------

TEST_CASE("The scripted provider replays a script and refuses to run past its end",
          "[ai][provider]") {
    std::vector<ai::ScriptedTurn> turns;
    turns.push_back(ai::ScriptedTurn{
        "Looking.", {ai::ToolCall{"c1", "project.get_state", json::object()}}, {}, {}});
    turns.push_back(ai::ScriptedTurn{"Done.", {}, ai::StopReason::EndTurn, {}});
    ai::ScriptedProvider provider(turns);

    ai::CompletionRequest request;
    const auto first = provider.complete(request);
    REQUIRE(first.has_value());
    CHECK(first->stopReason == ai::StopReason::ToolUse);
    const auto second = provider.complete(request);
    REQUIRE(second.has_value());
    CHECK(second->stopReason == ai::StopReason::EndTurn);
    // Running off the end is a scripting mistake; an endless "I am done" would hide it.
    CHECK_FALSE(provider.complete(request).has_value());
}

TEST_CASE("A script parses from JSON", "[ai][provider]") {
    const json doc = json::parse(R"({"turns": [
        {"text": "Inspecting.", "toolCalls": [{"name": "parameter.search",
                                               "arguments": {"query": "emissive"}}]},
        {"text": "Finished."}
    ]})");
    const auto turns = ai::ScriptedProvider::parseScript(doc);
    REQUIRE(turns.has_value());
    REQUIRE(turns->size() == 2);
    CHECK((*turns)[0].toolCalls.at(0).name == "parameter.search");
    CHECK(!(*turns)[0].toolCalls.at(0).id.empty());

    CHECK_FALSE(ai::ScriptedProvider::parseScript(json::object()).has_value());
    CHECK_FALSE(ai::ScriptedProvider::parseScript(json::parse(R"({"turns":[]})")).has_value());
}
