#include "app/live_profile_hook.hpp"

#include "app/directing_evaluate.hpp"
#include "app/directing_record.hpp"
#include "app/engine.hpp"

#include <fmt/format.h>
#include <nlohmann/json.hpp>

#include <atomic>
#include <fstream>
#include <mutex>
#include <optional>
#include <sstream>
#include <thread>

namespace avgen::app {

namespace fs = std::filesystem;

std::vector<std::string> liveProfileCommand(const fs::path& executable, const fs::path& project, const fs::path& json,
                                            const ai::ProfileRequest& r) {
    std::vector<std::string> argv{executable.string(), "--live-profile", "--project", project.string(),
                                  "--mode", r.mode, "--target-fps", fmt::format("{}", r.targetFps),
                                  "--size", fmt::format("{}x{}", r.width, r.height),
                                  "--start", fmt::format("{}", r.start), "--quality", r.quality,
                                  "--json", json.string(), "--no-text"};
    if (r.deep) {
        argv.push_back("--deep");
    }
    if (r.verifyCandidates > 0) {
        argv.push_back("--verify-candidates");
        argv.push_back(std::to_string(r.verifyCandidates));
    }
    return argv;
}

namespace {

class ProfileHandle final : public ai::DeferredResult {
public:
    ~ProfileHandle() override {
        cancel_ = true;
        if (thread_.joinable()) {
            thread_.join();
        }
        std::error_code ec;
        fs::remove(copy_, ec);
        fs::remove(json_, ec);
    }
    Result<void> start(Engine& live, const ai::ProfileRequest& request, const fs::path& executable,
                       const fs::path& scratchDir) {
        std::error_code ec;
        fs::create_directories(scratchDir, ec);
        auto copy = writeRecordingCopy(live, scratchDir);
        if (!copy) {
            return std::unexpected(copy.error());
        }
        copy_ = *copy;
        json_ = copy_;
        json_.replace_extension(".liveprofile.json");
        const std::vector<std::string> argv = liveProfileCommand(executable, copy_, json_, request);
        thread_ = std::thread([this, argv, dir = scratchDir] {
            {
                std::lock_guard lock(mutex_);
                phase_ = "profiling";
            }
            auto outcome = runProcess(argv, dir, &cancel_, 1800.0);
            std::lock_guard lock(mutex_);
            if (!outcome) {
                error_ = outcome.error().message;
            } else if (outcome->cancelled) {
                error_ = "cancelled";
            } else if (outcome->exitCode != 0) {
                error_ = fmt::format("the profiler exited {}: {}", outcome->exitCode,
                                     outcome->err.size() > 2000 ? outcome->err.substr(outcome->err.size() - 2000)
                                                                : outcome->err);
            } else {
                std::ifstream in(json_);
                std::stringstream text;
                text << in.rdbuf();
                result_ = nlohmann::json::parse(text.str(), nullptr, false);
                if (result_.is_discarded()) {
                    error_ = "the profiler wrote no readable record";
                }
            }
            done_ = true;
        });
        return {};
    }
    [[nodiscard]] bool done() const override { return done_.load(); }
    [[nodiscard]] std::string phase() const override {
        std::lock_guard lock(mutex_);
        return phase_;
    }
    [[nodiscard]] Result<nlohmann::json> take() override {
        if (thread_.joinable()) {
            thread_.join();
        }
        std::lock_guard lock(mutex_);
        if (!error_.empty()) {
            return fail("{}", error_);
        }
        nlohmann::json out = result_;
        out["summary"] = fmt::format("{} ({} candidate(s))", result_.value("status", std::string("?")),
                                     result_.contains("candidates") ? result_["candidates"].size() : 0);
        out["contended"] = "the editor was rendering while this ran: the GPU was shared";
        return out;
    }
    void cancel() override { cancel_ = true; }

private:
    std::thread thread_;
    std::atomic<bool> cancel_{false};
    std::atomic<bool> done_{false};
    mutable std::mutex mutex_;
    std::string phase_ = "starting";
    std::string error_;
    nlohmann::json result_;
    fs::path copy_, json_;
};

} // namespace

ai::ProfileHook makeProfileHook(fs::path executable, fs::path scratchDir) {
    return [executable = std::move(executable), scratchDir = std::move(scratchDir)](
               Engine& engine, const ai::ProfileRequest& request) -> Result<std::shared_ptr<ai::DeferredResult>> {
        auto handle = std::make_shared<ProfileHandle>();
        if (auto r = handle->start(engine, request, executable, scratchDir); !r) {
            return std::unexpected(r.error());
        }
        return handle;
    };
}

} // namespace avgen::app
