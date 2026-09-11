#pragma once

// The AI control plane (ADR-094): the one object the application owns, and the seam every other
// part of AV Gen sees.
//
// It holds the tool registry, the provider registry, the credential store, the snapshot store, the
// transaction sink, the main-thread queue and the current task -- and it makes the two guarantees
// the brief cares most about:
//
//   - **AI is optional.** Construct it with no provider configured and everything works: the panel
//     says "AI assistant is not configured", the tool registry is fully populated and testable, a
//     scripted provider or another application can drive it, and nothing else in AV Gen changes
//     behaviour. ADR-065's rule, kept.
//
//   - **Nothing blocks a real-time thread.** The only method the frame loop calls is `pump()`,
//     which runs queued tool bodies under a time budget. Everything else -- context, network, the
//     agent loop -- happens on a `JobSystem` worker, and there is no way to reach a `Provider`
//     from the main thread because nothing on the main-thread path holds one.
//
// It is *not* a manager class in the sense the brief warns against. It owns components and wires
// them; it contains no engine logic, no UI logic and no vendor logic.

#include "ai/credentials.hpp"
#include "ai/main_thread_queue.hpp"
#include "ai/orchestrator.hpp"
#include "ai/provider.hpp"
#include "ai/tool_api.hpp"
#include "ai/transaction.hpp"

#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace avgen::app {
class Engine;
class JobSystem;
} // namespace avgen::app

namespace avgen::ai {

// The non-secret AI configuration. Lives in the application's settings, never in a project
// (§35: conversation history belongs to a session, credentials to global settings, and these two
// storage domains are never mixed).
struct AiSettings {
    std::string activeProvider;          // id; empty means "not configured"
    std::vector<ProviderConfig> providers;
    TaskLimits limits;

    [[nodiscard]] nlohmann::json toJson() const;
    [[nodiscard]] static Result<AiSettings> fromJson(const nlohmann::json& j);
    [[nodiscard]] ProviderConfig* find(std::string_view id);
    [[nodiscard]] const ProviderConfig* find(std::string_view id) const;
    // Ensures a config row exists for every provider the build knows about, so Settings can show
    // them all without the settings file having to have mentioned them.
    void reconcileWith(const ProviderRegistry& registry);
};

// What the panel shows about the provider, without ever touching a secret.
struct ProviderStatus {
    std::string id;
    std::string displayName;
    bool enabled = false;
    bool active = false;
    bool requiresCredential = true;
    bool requiresEndpoint = false;
    CredentialStatus credential;
    std::string endpoint;
    std::string model;
    std::string defaultModel;
    std::string credentialHint;
    std::string lastTestResult; // "" until Test connection has been pressed
    bool lastTestOk = false;
};

class ControlPlane {
public:
    // `jobs` may be null (a headless session or a test), in which case `submit()` refuses and a
    // caller drives `runTaskBlocking` itself.
    ControlPlane(app::Engine& engine, app::JobSystem* jobs);
    ~ControlPlane();
    ControlPlane(const ControlPlane&) = delete;
    ControlPlane& operator=(const ControlPlane&) = delete;

    // ---- configuration -------------------------------------------------------------------------
    [[nodiscard]] AiSettings& settings() { return settings_; }
    [[nodiscard]] const AiSettings& settings() const { return settings_; }
    [[nodiscard]] const ProviderRegistry& providers() const { return providerRegistry_; }
    [[nodiscard]] CredentialStore& credentials() { return *credentials_; }
    // Tests replace the store so nothing writes to a developer's real keychain.
    void setCredentialStore(std::unique_ptr<CredentialStore> store);

    // Rebuilds the active provider from settings + the stored credential. Safe to call whenever
    // settings change; a failure leaves the plane unconfigured rather than half-configured.
    [[nodiscard]] Result<void> applySettings();
    [[nodiscard]] bool configured() const { return orchestrator_.provider() != nullptr; }
    // Why not, in a sentence a person can act on. Empty when configured.
    [[nodiscard]] const std::string& unconfiguredReason() const { return unconfiguredReason_; }
    [[nodiscard]] std::vector<ProviderStatus> providerStatus() const;
    [[nodiscard]] Result<std::string> testProvider(std::string_view id);
    void recordTestResult(std::string_view id, bool ok, std::string message);

    // Installs a provider directly, bypassing settings and credentials. For tests and for
    // `--ai-script`; the panel never calls it.
    void setProvider(std::shared_ptr<Provider> provider);

    // ---- the tool surface ----------------------------------------------------------------------
    [[nodiscard]] const ToolRegistry& tools() const { return tools_; }
    [[nodiscard]] ToolRegistry& tools() { return tools_; }
    [[nodiscard]] SnapshotStore& snapshots() { return snapshots_; }
    // The undo seam. Replacing the default snapshot sink with one backed by the editor's undo
    // stack is the whole of the reconciliation §26 asks for.
    void setTransactionSink(TransactionSink* sink);
    [[nodiscard]] TransactionSink& transactionSink() const { return *activeSink_; }

    void setPerformanceSource(PerformanceSource source);
    // Where projects live, for the project life-cycle tools.
    void setProjectsRoot(std::filesystem::path root);
    // Directories the assistant may read content from.
    void setContentRoots(std::vector<std::filesystem::path> roots);

    // ---- running a task -------------------------------------------------------------------------
    // Submits to the job system and returns immediately. Null when a task is already running or
    // there is no job system.
    [[nodiscard]] std::shared_ptr<AgentTask> submit(std::string prompt);
    // Runs on the calling thread. **Never call this from the main thread of a live session**: it
    // blocks on the network and on the main-thread queue, which the main thread is what services.
    void runTaskBlocking(AgentTask& task);

    [[nodiscard]] std::shared_ptr<AgentTask> currentTask() const;
    [[nodiscard]] const std::vector<std::shared_ptr<AgentTask>>& history() const { return history_; }
    void cancelCurrentTask();
    void clearHistory();

    // ---- the frame loop's only obligation --------------------------------------------------------
    // Runs queued tool bodies on the calling thread under a time budget. Call once per frame.
    std::size_t pump(double budgetMs = MainThreadQueue::kDefaultBudgetMs);
    void shutdown();

private:
    app::JobSystem* jobs_;
    ToolRegistry tools_;
    ProviderRegistry providerRegistry_;
    std::unique_ptr<CredentialStore> credentials_;
    SnapshotStore snapshots_;
    std::unique_ptr<SnapshotTransactionSink> defaultSink_;
    TransactionSink* activeSink_ = nullptr;
    MainThreadQueue queue_;
    Orchestrator orchestrator_;
    AiSettings settings_;
    std::string unconfiguredReason_ = "no provider configured";
    std::vector<std::pair<std::string, std::pair<bool, std::string>>> testResults_;

    mutable std::mutex taskMutex_;
    std::shared_ptr<AgentTask> current_;
    std::vector<std::shared_ptr<AgentTask>> history_;
};

} // namespace avgen::ai
