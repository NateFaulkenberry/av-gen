#include "ai/control_plane.hpp"

#include "ai/engine_tools.hpp"
#include "app/engine.hpp"
#include "app/job_system.hpp"
#include "core/log.hpp"

#include <algorithm>

namespace avgen::ai {

// ---- settings ------------------------------------------------------------------------------------

nlohmann::json AiSettings::toJson() const {
    nlohmann::json j;
    j["activeProvider"] = activeProvider;
    nlohmann::json list = nlohmann::json::array();
    for (const ProviderConfig& config : providers) {
        list.push_back(config.toJson());
    }
    j["providers"] = std::move(list);
    j["limits"] = nlohmann::json{{"maxIterations", limits.maxIterations},
                                 {"maxToolCalls", limits.maxToolCalls},
                                 {"maxSeconds", limits.maxSeconds}};
    return j;
}

Result<AiSettings> AiSettings::fromJson(const nlohmann::json& j) {
    if (!j.is_object()) {
        return fail("'ai' settings must be an object");
    }
    AiSettings out;
    out.activeProvider = j.value("activeProvider", std::string{});
    if (const auto list = j.find("providers"); list != j.end()) {
        if (!list->is_array()) {
            return fail("'ai.providers' must be an array");
        }
        for (const nlohmann::json& entry : *list) {
            auto config = ProviderConfig::fromJson(entry);
            if (!config) {
                return std::unexpected(config.error());
            }
            out.providers.push_back(std::move(*config));
        }
    }
    if (const auto limits = j.find("limits"); limits != j.end() && limits->is_object()) {
        out.limits.maxIterations = std::clamp(limits->value("maxIterations", 12), 1, 100);
        out.limits.maxToolCalls = std::clamp(limits->value("maxToolCalls", 64), 1, 1000);
        out.limits.maxSeconds = std::clamp(limits->value("maxSeconds", 300.0), 5.0, 3600.0);
    }
    return out;
}

ProviderConfig* AiSettings::find(std::string_view id) {
    const auto it = std::find_if(providers.begin(), providers.end(),
                                 [&](const ProviderConfig& c) { return c.id == id; });
    return it == providers.end() ? nullptr : &*it;
}

const ProviderConfig* AiSettings::find(std::string_view id) const {
    const auto it = std::find_if(providers.begin(), providers.end(),
                                 [&](const ProviderConfig& c) { return c.id == id; });
    return it == providers.end() ? nullptr : &*it;
}

void AiSettings::reconcileWith(const ProviderRegistry& registry) {
    for (const ProviderRegistry::Entry& entry : registry.entries()) {
        if (find(entry.id) != nullptr) {
            continue;
        }
        ProviderConfig config;
        config.id = entry.id;
        config.displayName = entry.displayName;
        config.credentialAccount = entry.id;
        config.endpoint = entry.defaultEndpoint;
        config.model = entry.defaultModel;
        config.enabled = false;
        providers.push_back(std::move(config));
    }
}

// ---- the plane ------------------------------------------------------------------------------------

ControlPlane::ControlPlane(app::Engine& engine, app::JobSystem* jobs)
    : jobs_(jobs), credentials_(makeSystemCredentialStore()),
      orchestrator_(engine, tools_, queue_) {
    registerEngineTools(tools_);
    registerBuiltinProviders(providerRegistry_);
    settings_.reconcileWith(providerRegistry_);
    defaultSink_ = std::make_unique<SnapshotTransactionSink>(engine, snapshots_);
    activeSink_ = defaultSink_.get();
    orchestrator_.setTransactionSink(activeSink_);
    orchestrator_.setLimits(settings_.limits);
    log::info("ai: control plane ready -- {} tool(s) across {} domain(s), {} provider(s) available",
              tools_.size(), tools_.domains().size(), providerRegistry_.entries().size());
}

ControlPlane::~ControlPlane() { shutdown(); }

void ControlPlane::setCredentialStore(std::unique_ptr<CredentialStore> store) {
    if (store) {
        credentials_ = std::move(store);
    }
}

void ControlPlane::setTransactionSink(TransactionSink* sink) {
    activeSink_ = sink != nullptr ? sink : defaultSink_.get();
    orchestrator_.setTransactionSink(activeSink_);
}

void ControlPlane::setPerformanceSource(PerformanceSource source) {
    orchestrator_.setPerformanceSource(std::move(source));
}

void ControlPlane::setProvider(std::shared_ptr<Provider> provider) {
    unconfiguredReason_ = provider ? std::string{} : std::string("no provider configured");
    if (provider) {
        orchestrator_.setModel(provider->defaultModel());
    }
    orchestrator_.setProvider(std::move(provider));
}

Result<void> ControlPlane::applySettings() {
    orchestrator_.setLimits(settings_.limits);
    settings_.reconcileWith(providerRegistry_);

    if (settings_.activeProvider.empty()) {
        orchestrator_.setProvider(nullptr);
        unconfiguredReason_ = "no provider selected";
        return fail("{}", unconfiguredReason_);
    }
    const ProviderConfig* config = settings_.find(settings_.activeProvider);
    if (config == nullptr) {
        orchestrator_.setProvider(nullptr);
        unconfiguredReason_ =
            fmt::format("'{}' is not a provider this build knows about", settings_.activeProvider);
        return fail("{}", unconfiguredReason_);
    }
    if (!config->enabled) {
        orchestrator_.setProvider(nullptr);
        unconfiguredReason_ = fmt::format("{} is not enabled", config->displayName.empty()
                                                                   ? config->id
                                                                   : config->displayName);
        return fail("{}", unconfiguredReason_);
    }
    // The secret lives on this stack frame and inside the provider, and nowhere else. It is never
    // held on the ControlPlane, never copied into settings, never logged.
    std::string secret =
        credentials_->resolve(config->credentialAccount.empty() ? config->id
                                                                : config->credentialAccount)
            .value_or(std::string{});
    auto provider = providerRegistry_.create(*config, std::move(secret));
    if (!provider) {
        orchestrator_.setProvider(nullptr);
        unconfiguredReason_ = provider.error().message;
        return std::unexpected(provider.error());
    }
    orchestrator_.setModel(config->model.empty() ? (*provider)->defaultModel() : config->model);
    orchestrator_.setTemperature(config->temperature);
    orchestrator_.setProvider(std::shared_ptr<Provider>(std::move(*provider)));
    unconfiguredReason_.clear();
    log::info("ai: provider '{}' active", config->id);
    return {};
}

std::vector<ProviderStatus> ControlPlane::providerStatus() const {
    std::vector<ProviderStatus> out;
    for (const ProviderRegistry::Entry& entry : providerRegistry_.entries()) {
        ProviderStatus status;
        status.id = entry.id;
        status.displayName = entry.displayName;
        status.requiresCredential = entry.requiresCredential;
        status.requiresEndpoint = entry.requiresEndpoint;
        status.defaultModel = entry.defaultModel;
        status.credentialHint = entry.credentialHint;
        status.endpoint = entry.defaultEndpoint;
        if (const ProviderConfig* config = settings_.find(entry.id)) {
            status.enabled = config->enabled;
            if (!config->endpoint.empty()) {
                status.endpoint = config->endpoint;
            }
            status.model = config->model;
        }
        status.active = settings_.activeProvider == entry.id;
        // Status, never the value.
        status.credential = credentials_->status(entry.id);
        const auto test = std::find_if(testResults_.begin(), testResults_.end(),
                                       [&](const auto& e) { return e.first == entry.id; });
        if (test != testResults_.end()) {
            status.lastTestOk = test->second.first;
            status.lastTestResult = test->second.second;
        }
        out.push_back(std::move(status));
    }
    return out;
}

void ControlPlane::recordTestResult(std::string_view id, bool ok, std::string message) {
    const auto it = std::find_if(testResults_.begin(), testResults_.end(),
                                 [&](const auto& e) { return e.first == id; });
    if (it != testResults_.end()) {
        it->second = {ok, std::move(message)};
        return;
    }
    testResults_.emplace_back(std::string(id), std::make_pair(ok, std::move(message)));
}

Result<std::string> ControlPlane::testProvider(std::string_view id) {
    const ProviderConfig* config = settings_.find(id);
    if (config == nullptr) {
        return fail("no provider '{}'", id);
    }
    std::string secret =
        credentials_->resolve(config->credentialAccount.empty() ? config->id
                                                                : config->credentialAccount)
            .value_or(std::string{});
    auto provider = providerRegistry_.create(*config, std::move(secret));
    if (!provider) {
        return std::unexpected(provider.error());
    }
    return (*provider)->testConnection();
}

std::shared_ptr<AgentTask> ControlPlane::submit(std::string prompt) {
    if (jobs_ == nullptr) {
        log::warn("ai: no job system, so a task cannot be submitted from here");
        return nullptr;
    }
    {
        const std::lock_guard lock(taskMutex_);
        if (current_ && !current_->finished()) {
            return nullptr; // one task at a time; the panel disables Send while one runs
        }
    }
    auto task = std::make_shared<AgentTask>(std::move(prompt));
    {
        const std::lock_guard lock(taskMutex_);
        current_ = task;
        history_.push_back(task);
    }
    app::JobRequest request;
    request.type = "ai.task";
    request.name = task->prompt().substr(0, 60);
    request.cancellable = true;
    // The whole agent loop lives inside this body, on a worker. That is the structural reason
    // nothing in it can touch the render or audio thread: the only thing it can reach on the main
    // thread is `MainThreadQueue`, and that is a queue, not a call.
    request.body = [this, task](app::JobContext& jobCtx) -> Result<void> {
        jobCtx.setStages({"Preparing", "Thinking", "Working", "Validating"});
        jobCtx.beginStage(0);
        // The job system's own cancel and the task's token are two views of one intent.
        std::thread watcher;
        std::atomic<bool> watching{true};
        watcher = std::thread([&] {
            while (watching.load(std::memory_order_relaxed)) {
                if (jobCtx.shouldCancel()) {
                    task->requestCancel();
                    return;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds{50});
            }
        });
        runTaskBlocking(*task);
        watching.store(false, std::memory_order_relaxed);
        if (watcher.joinable()) {
            watcher.join();
        }
        const TaskOutcome outcome = task->outcome();
        jobCtx.log(outcome.success ? "completed" : ("failed: " + outcome.error));
        if (!outcome.success && !outcome.cancelled) {
            return fail("{}", outcome.error);
        }
        return {};
    };
    (void)jobs_->submit(std::move(request));
    return task;
}

void ControlPlane::runTaskBlocking(AgentTask& task) { orchestrator_.run(task); }

std::shared_ptr<AgentTask> ControlPlane::currentTask() const {
    const std::lock_guard lock(taskMutex_);
    return current_;
}

void ControlPlane::cancelCurrentTask() {
    const std::lock_guard lock(taskMutex_);
    if (current_ && !current_->finished()) {
        current_->requestCancel();
    }
}

void ControlPlane::clearHistory() {
    const std::lock_guard lock(taskMutex_);
    // The running task is kept: clearing a conversation must not orphan a worker that is still
    // mutating the project.
    history_.erase(std::remove_if(history_.begin(), history_.end(),
                                  [](const std::shared_ptr<AgentTask>& t) {
                                      return t && t->finished();
                                  }),
                   history_.end());
}

std::size_t ControlPlane::pump(double budgetMs) { return queue_.pump(budgetMs); }

void ControlPlane::shutdown() {
    cancelCurrentTask();
    // Release every worker waiting on the main thread before the engine goes away, or one will
    // wait for a pump that is never coming and take the shutdown with it.
    queue_.shutdown();
}

} // namespace avgen::ai
