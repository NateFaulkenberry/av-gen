#pragma once

// A provider whose turns come from a script (ADR-094).
//
// §52 asks for exactly one mock: the provider. Deterministic engine tests must not require a cloud
// model or a network, and the thing under test in an agent-loop test is the *loop* -- does it
// execute the tools, feed the results back, open and close the transaction, stop at the budget,
// roll back on failure, stop when cancelled -- none of which is a question about a language model.
//
// It is emphatically **not** a mock engine API. Every tool it calls is the real registry running
// against a real `app::Engine`; only the thing choosing which tool to call is scripted. §54 forbids
// the other kind, and this is the line between them.
//
// It is also the `--ai-script` demo path: a scripted run drives the whole control plane end to end
// -- context, loop, main-thread dispatch, transaction, activity events, summary -- and proves every
// part of it except the model's judgement, which is the one part no test can pin anyway.

#include "ai/provider.hpp"

#include <filesystem>
#include <vector>

namespace avgen::ai {

// One scripted assistant turn.
struct ScriptedTurn {
    std::string text;
    std::vector<ToolCall> toolCalls; // empty means the turn ends the task
    StopReason stopReason = StopReason::EndTurn;
    // Returned instead of a turn, so failure handling can be tested too.
    std::string error;
};

class ScriptedProvider final : public Provider {
public:
    explicit ScriptedProvider(std::vector<ScriptedTurn> turns = {}) : turns_(std::move(turns)) {}

    // JSON: { "turns": [ { "text": "...", "toolCalls": [ { "name": "...", "arguments": {...} } ],
    //                      "error": "..." } ] }
    [[nodiscard]] static Result<std::vector<ScriptedTurn>> loadScript(
        const std::filesystem::path& path);
    [[nodiscard]] static Result<std::vector<ScriptedTurn>> parseScript(const nlohmann::json& doc);

    [[nodiscard]] std::string_view id() const override { return "scripted"; }
    [[nodiscard]] std::string_view displayName() const override { return "Scripted (test)"; }
    [[nodiscard]] ProviderCapabilities capabilities() const override;
    [[nodiscard]] std::string defaultModel() const override { return "scripted"; }
    [[nodiscard]] Result<CompletionResponse> complete(const CompletionRequest& request) override;
    [[nodiscard]] Result<std::string> testConnection() override { return std::string("scripted"); }

    // The requests the loop made, so a test can assert that tool results actually went back and
    // that the system prompt carried what it should.
    [[nodiscard]] const std::vector<CompletionRequest>& received() const { return received_; }
    [[nodiscard]] std::size_t turnsUsed() const { return cursor_; }

private:
    std::vector<ScriptedTurn> turns_;
    std::vector<CompletionRequest> received_;
    std::size_t cursor_ = 0;
};

} // namespace avgen::ai
