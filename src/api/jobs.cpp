#include "api/jobs.hpp"

#include "ai/tool_context.hpp"

#include <fmt/format.h>

#include <algorithm>
#include <ctime>

namespace avgen::api {

using nlohmann::json;

const char* jobStateName(JobState state) {
    switch (state) {
    case JobState::Queued: return "queued";
    case JobState::Preparing: return "preparing";
    case JobState::Running: return "running";
    case JobState::Succeeded: return "succeeded";
    case JobState::Failed: return "failed";
    case JobState::Cancelled: return "cancelled";
    }
    return "failed";
}

bool jobFinished(JobState state) {
    return state == JobState::Succeeded || state == JobState::Failed || state == JobState::Cancelled;
}

const char* renderFormatName(RenderFormat format) {
    switch (format) {
    case RenderFormat::Png: return "png";
    case RenderFormat::Exr: return "exr";
    case RenderFormat::Video: return "video";
    }
    return "png";
}

const char* renderHistoryName(RenderHistory history) { return history == RenderHistory::Play ? "play" : "seek"; }

std::string hashHex(std::uint64_t hash) { return fmt::format("{:016x}", hash); }

namespace {

std::string isoTime(std::chrono::system_clock::time_point t) {
    if (t == std::chrono::system_clock::time_point{}) {
        return {};
    }
    const std::time_t c = std::chrono::system_clock::to_time_t(t);
    std::tm tm{};
    gmtime_r(&c, &tm);
    char buf[32];
    std::strftime(buf, sizeof buf, "%Y-%m-%dT%H:%M:%SZ", &tm);
    return buf;
}

json eventOf(const JobRecord& r) {
    return json{{"job", r.id},
                {"tool", r.tool},
                {"state", jobStateName(r.state)},
                {"phase", r.phase},
                {"progress", r.progress},
                {"framesDone", r.framesDone},
                {"framesTotal", r.framesTotal}};
}

} // namespace

json JobRecord::toJson() const {
    json j{{"id", id},
           {"tool", tool},
           {"kind", kind},
           {"state", jobStateName(state)},
           {"phase", phase},
           {"progress", progress},
           {"framesDone", framesDone},
           {"framesTotal", framesTotal},
           {"request", request},
           {"snapshot", snapshot},
           {"outputs", outputs},
           {"result", result},
           {"cancelRequested", cancelRequested},
           {"created", isoTime(created)},
           {"finished", finished == std::chrono::system_clock::time_point{} ? json(nullptr) : json(isoTime(finished))}};
    if (error) {
        json e{{"code", ai::toolErrorCodeName(error->code)}, {"message", error->message}, {"recovery", error->recovery}};
        if (error->details.is_object() && !error->details.empty()) {
            e["details"] = error->details;
        }
        j["error"] = std::move(e);
    } else {
        j["error"] = nullptr;
    }
    return j;
}

std::string JobTable::create(std::string tool, std::string kind, json request, std::string snapshot) {
    JobRecord r;
    {
        const std::lock_guard lock(mutex_);
        r.id = fmt::format("job-{}", next_++);
        r.tool = std::move(tool);
        r.kind = std::move(kind);
        r.request = std::move(request);
        r.snapshot = std::move(snapshot);
        r.created = std::chrono::system_clock::now();
        jobs_[r.id] = r;
        order_.push_back(r.id);
    }
    changed_.notify_all();
    return r.id;
}

bool JobTable::update(const std::string& id, const std::function<void(JobRecord&)>& change) {
    json event;
    std::shared_ptr<Listener> listener;
    {
        const std::lock_guard lock(mutex_);
        const auto it = jobs_.find(id);
        if (it == jobs_.end()) {
            return false;
        }
        JobRecord& r = it->second;
        const JobState stateBefore = r.state;
        const std::string phaseBefore = r.phase;
        change(r);
        if (jobFinished(r.state) && r.finished == std::chrono::system_clock::time_point{}) {
            r.finished = std::chrono::system_clock::now();
        }
        const auto now = std::chrono::steady_clock::now();
        const bool milestone = r.state != stateBefore || r.phase != phaseBefore;
        auto& last = lastProgressEvent_[id];
        if (listener_ && (milestone || now - last >= kProgressPeriod)) {
            last = now;
            event = eventOf(r);
            listener = listener_;
        }
    }
    changed_.notify_all();
    if (listener && *listener) {
        (*listener)(event);
    }
    return true;
}

std::optional<JobRecord> JobTable::get(const std::string& id) const {
    const std::lock_guard lock(mutex_);
    const auto it = jobs_.find(id);
    if (it == jobs_.end()) {
        return std::nullopt;
    }
    return it->second;
}

std::vector<JobRecord> JobTable::list() const {
    const std::lock_guard lock(mutex_);
    std::vector<JobRecord> out;
    out.reserve(order_.size());
    for (const std::string& id : order_) {
        out.push_back(jobs_.at(id));
    }
    return out;
}

bool JobTable::requestCancel(const std::string& id) {
    {
        const std::lock_guard lock(mutex_);
        const auto it = jobs_.find(id);
        if (it == jobs_.end() || jobFinished(it->second.state)) {
            return false;
        }
        it->second.cancelRequested = true;
    }
    changed_.notify_all();
    return true;
}

bool JobTable::cancelRequested(const std::string& id) const {
    const std::lock_guard lock(mutex_);
    const auto it = jobs_.find(id);
    return it != jobs_.end() && it->second.cancelRequested;
}

void JobTable::discard(const std::string& id) {
    {
        const std::lock_guard lock(mutex_);
        jobs_.erase(id);
        lastProgressEvent_.erase(id);
        order_.erase(std::remove(order_.begin(), order_.end(), id), order_.end());
    }
    changed_.notify_all();
}

std::optional<JobRecord> JobTable::wait(const std::string& id, std::chrono::milliseconds timeout,
                                        const ai::CancelToken* cancel) const {
    std::unique_lock lock(mutex_);
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (true) {
        const auto it = jobs_.find(id);
        if (it == jobs_.end()) {
            return std::nullopt;
        }
        if (jobFinished(it->second.state) || std::chrono::steady_clock::now() >= deadline ||
            (cancel != nullptr && cancel->cancelled())) {
            return it->second;
        }
        // Woken by every update; the short period is only for a cancel token, which has no condition variable.
        changed_.wait_for(lock, std::min<std::chrono::steady_clock::duration>(
                                    deadline - std::chrono::steady_clock::now(), std::chrono::milliseconds{100}));
    }
}

std::size_t JobTable::unfinished() const {
    const std::lock_guard lock(mutex_);
    return static_cast<std::size_t>(
        std::count_if(jobs_.begin(), jobs_.end(), [](const auto& kv) { return !jobFinished(kv.second.state); }));
}

void JobTable::setListener(Listener listener) {
    const std::lock_guard lock(mutex_);
    listener_ = listener ? std::make_shared<Listener>(std::move(listener)) : nullptr;
}

} // namespace avgen::api
