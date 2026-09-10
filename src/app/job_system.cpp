#include "app/job_system.hpp"

#include "core/log.hpp"

#include <algorithm>

namespace avgen::app {
namespace {
using Clock = std::chrono::steady_clock;

double secondsSince(Clock::time_point t) {
    return std::chrono::duration<double>(Clock::now() - t).count();
}
} // namespace

const char* jobStateName(JobState s) {
    switch (s) {
    case JobState::Queued:
        return "queued";
    case JobState::Preparing:
        return "preparing";
    case JobState::Running:
        return "running";
    case JobState::Paused:
        return "paused";
    case JobState::Completed:
        return "completed";
    case JobState::Cancelled:
        return "cancelled";
    case JobState::Failed:
        return "failed";
    }
    return "queued";
}

bool jobStateIsTerminal(JobState s) {
    return s == JobState::Completed || s == JobState::Cancelled || s == JobState::Failed;
}

struct JobSystem::Job {
    JobId id = 0;
    std::string type;
    std::string name;
    JobBody body;
    bool cancellable = true;

    mutable std::mutex mutex;          // guards everything below
    std::condition_variable resumed;
    JobState state = JobState::Queued;
    std::vector<std::string> stages;
    int stageIndex = 0;
    float stageProgress = 0.0f;
    bool stageProgressKnown = false;
    std::string operation;
    std::string error;
    std::vector<std::string> logs;
    Clock::time_point queuedAt = Clock::now();
    Clock::time_point startedAt{};
    double completedElapsed = 0.0;
    std::atomic<bool> cancelRequested{false};
    std::atomic<bool> pauseRequested{false};

    [[nodiscard]] JobStatus snapshot() const {
        std::lock_guard<std::mutex> lock(mutex);
        JobStatus s;
        s.id = id;
        s.type = type;
        s.name = name;
        s.state = state;
        s.stageCount = stages.empty() ? 1 : static_cast<int>(stages.size());
        s.stageIndex = stageIndex;
        s.stageName = stages.empty() ? std::string() : stages[static_cast<std::size_t>(stageIndex)];
        s.stageProgress = stageProgress;
        s.currentOperation = operation;
        s.error = error;
        s.cancellable = cancellable;
        s.logs = logs;
        s.elapsedSeconds = jobStateIsTerminal(state)
                               ? completedElapsed
                               : (state == JobState::Queued ? 0.0 : secondsSince(startedAt));

        if (state == JobState::Completed) {
            s.progress = 1.0f;
            s.progressKnown = true;
            s.estimatedRemainingSeconds = 0.0;
            return s;
        }
        if (jobStateIsTerminal(state)) {
            s.progressKnown = false;
            return s;
        }
        // Overall progress is only meaningful when the running stage can measure itself. A stage
        // that never reports leaves the whole job unknown rather than contributing a guess: a
        // fabricated bar is indistinguishable from a real one, which is exactly why it must not
        // exist.
        if (stageProgressKnown && !stages.empty()) {
            const float per = 1.0f / static_cast<float>(stages.size());
            s.progress = std::clamp(static_cast<float>(stageIndex) * per + stageProgress * per,
                                    0.0f, 1.0f);
            s.progressKnown = true;
            // Extrapolate from elapsed time, and only once there is enough of it to mean anything.
            if (s.progress > 0.02f && s.elapsedSeconds > 0.5) {
                const double total = s.elapsedSeconds / static_cast<double>(s.progress);
                s.estimatedRemainingSeconds = std::max(total - s.elapsedSeconds, 0.0);
            }
        }
        return s;
    }
};

// The worker's view of its own job.
class JobSystem::Context final : public JobContext {
public:
    explicit Context(Job& job) : job_(job) {}

    void setStages(std::vector<std::string> stages) override {
        std::lock_guard<std::mutex> lock(job_.mutex);
        job_.stages = std::move(stages);
        job_.stageIndex = 0;
        job_.stageProgress = 0.0f;
        job_.stageProgressKnown = false;
    }
    void beginStage(int index) override {
        std::lock_guard<std::mutex> lock(job_.mutex);
        const int last = job_.stages.empty() ? 0 : static_cast<int>(job_.stages.size()) - 1;
        job_.stageIndex = std::clamp(index, 0, last);
        job_.stageProgress = 0.0f;
        job_.stageProgressKnown = false;
    }
    void setStageProgress(float fraction) override {
        std::lock_guard<std::mutex> lock(job_.mutex);
        job_.stageProgress = std::clamp(fraction, 0.0f, 1.0f);
        job_.stageProgressKnown = true;
    }
    void setOperation(std::string text) override {
        std::lock_guard<std::mutex> lock(job_.mutex);
        job_.operation = std::move(text);
    }
    void log(std::string line) override {
        std::lock_guard<std::mutex> lock(job_.mutex);
        // Bounded: a job that logs per frame for an hour must not become the reason memory ran out.
        if (job_.logs.size() >= 512) {
            job_.logs.erase(job_.logs.begin());
        }
        job_.logs.push_back(std::move(line));
    }
    [[nodiscard]] bool shouldCancel() const override { return job_.cancelRequested.load(); }

    [[nodiscard]] bool waitWhilePaused() override {
        if (!job_.pauseRequested.load()) {
            return !job_.cancelRequested.load();
        }
        std::unique_lock<std::mutex> lock(job_.mutex);
        const JobState previous = job_.state;
        job_.state = JobState::Paused;
        job_.resumed.wait(lock, [this] {
            return !job_.pauseRequested.load() || job_.cancelRequested.load();
        });
        if (job_.state == JobState::Paused) {
            job_.state = previous;
        }
        return !job_.cancelRequested.load();
    }

private:
    Job& job_;
};

JobSystem::JobSystem(unsigned workers) {
    const unsigned count = std::max(workers, 1u);
    workers_.reserve(count);
    for (unsigned i = 0; i < count; ++i) {
        workers_.emplace_back([this] { workerLoop(); });
    }
}

JobSystem::~JobSystem() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stopping_ = true;
        // Everything still queued or running is asked to stop, so a destructor cannot hang on a job
        // that would otherwise have run for another ten minutes.
        for (auto& job : all_) {
            job->cancelRequested.store(true);
            job->pauseRequested.store(false);
            job->resumed.notify_all();
        }
    }
    work_.notify_all();
    for (auto& t : workers_) {
        if (t.joinable()) {
            t.join();
        }
    }
}

JobId JobSystem::submit(JobRequest request) {
    auto job = std::make_shared<Job>();
    {
        std::lock_guard<std::mutex> lock(mutex_);
        job->id = nextId_++;
        job->type = std::move(request.type);
        job->name = std::move(request.name);
        job->body = std::move(request.body);
        job->cancellable = request.cancellable;
        all_.push_back(job);
        queue_.push_back(job);
    }
    work_.notify_one();
    return job->id;
}

std::shared_ptr<JobSystem::Job> JobSystem::findLocked(JobId id) const {
    for (const auto& job : all_) {
        if (job->id == id) {
            return job;
        }
    }
    return nullptr;
}

bool JobSystem::cancel(JobId id) {
    std::shared_ptr<Job> job;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        job = findLocked(id);
        if (!job) {
            return false;
        }
        if (!job->cancellable) {
            return false;
        }
        job->cancelRequested.store(true);
        job->pauseRequested.store(false);
        // A job that has not started yet never will: mark it now rather than making a worker pick
        // it up only to drop it.
        std::lock_guard<std::mutex> jobLock(job->mutex);
        if (job->state == JobState::Queued) {
            job->state = JobState::Cancelled;
            job->completedElapsed = 0.0;
            queue_.erase(std::remove_if(queue_.begin(), queue_.end(),
                                        [id](const std::shared_ptr<Job>& q) { return q->id == id; }),
                         queue_.end());
        }
    }
    job->resumed.notify_all();
    done_.notify_all();
    return true;
}

bool JobSystem::pause(JobId id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto job = findLocked(id);
    if (!job) {
        return false;
    }
    job->pauseRequested.store(true);
    return true;
}

bool JobSystem::resume(JobId id) {
    std::shared_ptr<Job> job;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        job = findLocked(id);
        if (!job) {
            return false;
        }
        job->pauseRequested.store(false);
    }
    job->resumed.notify_all();
    return true;
}

void JobSystem::cancelAll() {
    std::vector<JobId> ids;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (const auto& job : all_) {
            ids.push_back(job->id);
        }
    }
    for (const JobId id : ids) {
        cancel(id);
    }
}

std::vector<JobStatus> JobSystem::statuses() const {
    std::vector<std::shared_ptr<Job>> jobs;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        jobs = all_;
    }
    std::vector<JobStatus> out;
    out.reserve(jobs.size());
    for (const auto& job : jobs) {
        out.push_back(job->snapshot());
    }
    return out;
}

bool JobSystem::status(JobId id, JobStatus& out) const {
    std::shared_ptr<Job> job;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        job = findLocked(id);
    }
    if (!job) {
        return false;
    }
    out = job->snapshot();
    return true;
}

std::size_t JobSystem::pending() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return queue_.size();
}

bool JobSystem::idle() const {
    std::vector<std::shared_ptr<Job>> jobs;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!queue_.empty()) {
            return false;
        }
        jobs = all_;
    }
    for (const auto& job : jobs) {
        std::lock_guard<std::mutex> lock(job->mutex);
        if (!jobStateIsTerminal(job->state)) {
            return false;
        }
    }
    return true;
}

bool JobSystem::waitFor(JobId id, std::chrono::milliseconds timeout) {
    std::shared_ptr<Job> job;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        job = findLocked(id);
    }
    if (!job) {
        return false;
    }
    std::unique_lock<std::mutex> lock(job->mutex);
    return job->resumed.wait_for(lock, timeout, [&job] { return jobStateIsTerminal(job->state); });
}

void JobSystem::waitAll(std::chrono::milliseconds timeout) {
    const auto deadline = Clock::now() + timeout;
    while (Clock::now() < deadline) {
        if (idle()) {
            return;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds{2});
    }
}

void JobSystem::clearFinished() {
    std::lock_guard<std::mutex> lock(mutex_);
    all_.erase(std::remove_if(all_.begin(), all_.end(),
                              [](const std::shared_ptr<Job>& job) {
                                  std::lock_guard<std::mutex> jobLock(job->mutex);
                                  return jobStateIsTerminal(job->state);
                              }),
               all_.end());
}

void JobSystem::workerLoop() {
    for (;;) {
        std::shared_ptr<Job> job;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            work_.wait(lock, [this] { return stopping_ || !queue_.empty(); });
            if (stopping_ && queue_.empty()) {
                return;
            }
            if (queue_.empty()) {
                continue;
            }
            job = queue_.front();
            queue_.pop_front();
        }

        if (job->cancelRequested.load()) {
            std::lock_guard<std::mutex> lock(job->mutex);
            if (!jobStateIsTerminal(job->state)) {
                job->state = JobState::Cancelled;
            }
            job->resumed.notify_all();
            done_.notify_all();
            continue;
        }

        {
            std::lock_guard<std::mutex> lock(job->mutex);
            job->state = JobState::Preparing;
            job->startedAt = Clock::now();
        }
        Context ctx(*job);
        Result<void> result{};
        {
            std::lock_guard<std::mutex> lock(job->mutex);
            job->state = JobState::Running;
        }
        // A job body that throws would otherwise take a worker thread with it and the queue would
        // quietly stop draining. Anything escaping becomes an ordinary failure.
        try {
            result = job->body ? job->body(ctx) : Result<void>{};
        } catch (const std::exception& e) {
            result = fail("job threw: {}", e.what());
        } catch (...) {
            result = fail("job threw an unknown exception");
        }

        {
            std::lock_guard<std::mutex> lock(job->mutex);
            job->completedElapsed = secondsSince(job->startedAt);
            if (job->cancelRequested.load()) {
                job->state = JobState::Cancelled;
            } else if (!result) {
                job->state = JobState::Failed;
                job->error = result.error().message;
                // Logged, not thrown: an optional job's failure is not the caller's failure, and
                // what it means is the caller's decision.
                log::warn("job {} '{}' failed: {}", job->id, job->name, job->error);
            } else {
                job->state = JobState::Completed;
                job->stageProgress = 1.0f;
                job->stageProgressKnown = true;
            }
        }
        job->resumed.notify_all();
        done_.notify_all();
    }
}

} // namespace avgen::app
