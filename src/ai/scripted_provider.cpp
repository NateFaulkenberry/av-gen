#include "ai/scripted_provider.hpp"

#include <fstream>

namespace avgen::ai {

ProviderCapabilities ScriptedProvider::capabilities() const {
    ProviderCapabilities c;
    c.tools = true;
    c.streaming = false;
    c.systemPrompt = true;
    c.temperature = false;
    c.modelListing = false;
    return c;
}

Result<CompletionResponse> ScriptedProvider::complete(const CompletionRequest& request) {
    received_.push_back(request);
    if (request.cancel.cancelled()) {
        return fail("cancelled");
    }
    if (cursor_ >= turns_.size()) {
        // Running off the end is a scripting mistake, and an infinite "I am done" would hide it.
        return fail("scripted provider: the script has only {} turn(s) and the loop asked for {}",
                    turns_.size(), cursor_ + 1);
    }
    const ScriptedTurn& turn = turns_[cursor_++];
    if (!turn.error.empty()) {
        return fail("{}", turn.error);
    }
    CompletionResponse out;
    out.model = "scripted";
    out.text = turn.text;
    out.toolCalls = turn.toolCalls;
    out.stopReason = turn.toolCalls.empty() ? turn.stopReason : StopReason::ToolUse;
    out.usage.inputTokens = 0;
    out.usage.outputTokens = 0;
    if (request.onTextDelta && !out.text.empty()) {
        request.onTextDelta(out.text);
    }
    return out;
}

Result<std::vector<ScriptedTurn>> ScriptedProvider::parseScript(const nlohmann::json& doc) {
    if (!doc.is_object() || !doc.contains("turns") || !doc.at("turns").is_array()) {
        return fail("an AI script must be an object with a 'turns' array");
    }
    std::vector<ScriptedTurn> turns;
    int index = 0;
    for (const nlohmann::json& entry : doc.at("turns")) {
        if (!entry.is_object()) {
            return fail("turn {} is not an object", index);
        }
        ScriptedTurn turn;
        turn.text = entry.value("text", std::string{});
        turn.error = entry.value("error", std::string{});
        if (const auto calls = entry.find("toolCalls"); calls != entry.end()) {
            if (!calls->is_array()) {
                return fail("turn {}: 'toolCalls' must be an array", index);
            }
            int callIndex = 0;
            for (const nlohmann::json& call : *calls) {
                if (!call.is_object() || !call.contains("name")) {
                    return fail("turn {}: tool call {} needs a 'name'", index, callIndex);
                }
                ToolCall tc;
                tc.name = call.at("name").get<std::string>();
                tc.id = call.value("id", fmt::format("scripted-{}-{}", index, callIndex));
                tc.arguments = call.value("arguments", nlohmann::json::object());
                turn.toolCalls.push_back(std::move(tc));
                ++callIndex;
            }
        }
        turns.push_back(std::move(turn));
        ++index;
    }
    if (turns.empty()) {
        return fail("an AI script needs at least one turn");
    }
    return turns;
}

Result<std::vector<ScriptedTurn>> ScriptedProvider::loadScript(const std::filesystem::path& path) {
    std::ifstream in(path);
    if (!in) {
        return fail("cannot open '{}'", path.string());
    }
    nlohmann::json doc = nlohmann::json::parse(in, nullptr, false);
    if (doc.is_discarded()) {
        return fail("'{}' is not valid JSON", path.string());
    }
    return parseScript(doc);
}

} // namespace avgen::ai
