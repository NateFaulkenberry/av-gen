// ADR-2159 (Engine API stage S6): the job table's state machine, the EngineHost's job queue driven through a fake
// GPU backend, and the machine GPU lock's protocol -- including interop with tools/gpu-lock.sh itself.
//
// Everything here runs in a temporary lock directory, never against the real ${TMPDIR}/avgen-gpu.lock: the CPU suite
// sets TMPDIR per process (support/per_process_tmpdir.cpp) and every lock below names its own folder besides.

#include "api/engine_host.hpp"
#include "api/gpu_lock.hpp"
#include "api/jobs.hpp"
#include "api/session.hpp"
#include "app/engine.hpp"
#include "core/hash.hpp"
#include "support/temp_dir.hpp"

#include <catch2/catch_test_macros.hpp>
#include <fmt/format.h>
#include <nlohmann/json.hpp>

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <thread>

#include <sys/time.h>
#include <sys/wait.h>
#include <unistd.h>

using namespace avgen;
using nlohmann::json;
namespace fs = std::filesystem;

namespace {

fs::path freshDir(const std::string& name) {
    const fs::path d = testsupport::processTempDir() / ("api_jobs_" + name);
    std::error_code ec;
    fs::remove_all(d, ec);
    fs::create_directories(d);
    return fs::weakly_canonical(d);
}

// A child that lives until killed: a lock holder that is not us and not our ancestor.
struct Child {
    pid_t pid = 0;
    Child() {
        pid = ::fork();
        if (pid == 0) {
            ::execl("/bin/sleep", "sleep", "60", static_cast<char*>(nullptr));
            ::_exit(127);
        }
    }
    void kill() {
        if (pid > 0) {
            ::kill(pid, SIGKILL);
            int status = 0;
            ::waitpid(pid, &status, 0);
            pid = 0;
        }
    }
    ~Child() { kill(); }
};

// A lock directory made the way gpu-lock.sh makes it.
void scriptStyleLock(const fs::path& dir, long pid, const std::string& holder) {
    fs::create_directory(dir);
    std::ofstream(dir / "pid") << pid << "\n";
    std::ofstream(dir / "holder") << holder << "\n";
}

// ---- a fake GPU half ------------------------------------------------------------------------------

struct FakeStats {
    std::atomic<int> created{0};
    std::atomic<int> devices{0};
    std::atomic<int> started{0};
    int frameSleepMs = 0;
};

class FakeJob final : public api::GpuRenderJob {
public:
    FakeJob(FakeStats& stats, api::RenderRequest q, std::string params)
        : stats_(stats), q_(std::move(q)), params_(std::move(params)) {
        total_ = static_cast<std::uint64_t>(std::max(1.0, std::ceil((q_.endSeconds - q_.startSeconds) * q_.fps - 1e-9)));
    }
    Result<void> start(const api::CompileWaitFn& onWait) override {
        ++stats_.started;
        if (onWait) {
            onWait(api::CompileProgress{2, 1, "fake-pipeline"});
            onWait(api::CompileProgress{2, 2, {}});
        }
        std::error_code ec;
        fs::create_directories(q_.outputDir, ec);
        return {};
    }
    bool step(double /*budget*/) override {
        if (cancelled_ || done_ >= total_) {
            return true;
        }
        if (stats_.frameSleepMs > 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(stats_.frameSleepMs));
        }
        // The frame is a pure function of the snapshot's parameters and its time: what the real one promises.
        const std::uint64_t index = done_;
        const double t = q_.startSeconds + static_cast<double>(index) / q_.fps;
        const std::string pixels = params_ + "@" + std::to_string(t);
        hashes_.push_back(fnv1a64(pixels));
        std::ofstream(q_.outputDir / fmt::format("frame_{:06d}.png", index)) << pixels;
        ++done_;
        return cancelled_ || done_ >= total_;
    }
    void cancel() override { cancelled_ = true; }
    api::GpuJobProgress progress() const override {
        api::GpuJobProgress g;
        g.framesDone = done_;
        g.framesTotal = total_;
        g.framesWritten = done_;
        return g;
    }
    api::GpuJobOutcome outcome() const override {
        api::GpuJobOutcome o;
        o.cancelled = cancelled_;
        o.frameHashes = hashes_;
        std::uint64_t seq = 14695981039346656037ull;
        for (const auto h : hashes_) {
            seq = (seq ^ h) * 1099511628211ull;
        }
        o.sequenceHash = seq;
        o.framesWritten = done_;
        return o;
    }

private:
    static std::uint64_t fnv1a64(const std::string& s) {
        std::uint64_t h = 14695981039346656037ull;
        for (const unsigned char c : s) {
            h = (h ^ c) * 1099511628211ull;
        }
        return h;
    }
    FakeStats& stats_;
    api::RenderRequest q_;
    std::string params_;
    std::uint64_t total_ = 1;
    std::uint64_t done_ = 0;
    bool cancelled_ = false;
    std::vector<std::uint64_t> hashes_;
};

class FakeBackend final : public api::RenderBackend {
public:
    explicit FakeBackend(FakeStats& stats) : stats_(stats) {}
    Result<void> ensureDevice() override {
        if (!device_) {
            device_ = true;
            ++stats_.devices;
        }
        return {};
    }
    bool deviceCreated() const override { return device_; }
    Result<std::unique_ptr<api::GpuRenderJob>> create(const api::RenderRequest& q) override {
        ++stats_.created;
        std::ifstream in(q.project);
        const json doc = json::parse(in, nullptr, false);
        if (doc.is_discarded()) {
            return fail("the snapshot '{}' is not JSON", q.project.string());
        }
        return std::unique_ptr<api::GpuRenderJob>(
            std::make_unique<FakeJob>(stats_, q, doc.value("parameters", json::object()).dump()));
    }

private:
    FakeStats& stats_;
    bool device_ = false;
};

struct Rig {
    FakeStats stats;
    fs::path root;
    fs::path lockDir;
    std::unique_ptr<api::EngineHost> host;
    std::unique_ptr<api::Session> session;

    explicit Rig(const std::string& name, bool gpu = true) {
        root = freshDir(name);
        lockDir = root / "lock" / "avgen-gpu.lock";
        fs::create_directories(lockDir.parent_path());
        api::EngineHostOptions o;
        if (gpu) {
            o.renderBackend = [this] { return std::make_unique<FakeBackend>(stats); };
        }
        o.gpuLockPath = lockDir;
        o.gpuLockTimeoutSeconds = 30.0;
        host = std::make_unique<api::EngineHost>(std::move(o));
        host->bindHostThread();
        api::SessionOptions so;
        so.client = api::ClientInfo{api::ClientKind::Test, "jobs", "1"};
        so.permissions.readRoots = {root};
        so.permissions.writeRoots = {root};
        auto s = api::Session::create(std::move(so), *host);
        REQUIRE(s.has_value());
        session = std::move(*s);
        const api::CallResult made =
            session->call("project.create", json{{"path", "proj/proj.json"}, {"scene", "empty"}});
        INFO(made.toWire().dump());
        REQUIRE(made.success);
    }
    ~Rig() {
        if (session) {
            session->close();
        }
    }
    api::CallResult call(const std::string& tool, const json& args = json::object()) {
        return session->call(tool, args);
    }
    std::string start(const json& args) {
        const api::CallResult r = call("render.start", args);
        INFO(r.toWire().dump());
        REQUIRE(r.success);
        REQUIRE(r.job.has_value());
        return *r.job;
    }
    json wait(const std::string& id, int timeoutMs = 10'000) {
        const api::CallResult r = call("job.wait", json{{"id", id}, {"timeoutMs", timeoutMs}});
        INFO(r.toWire().dump());
        REQUIRE(r.success);
        return r.result;
    }
};

} // namespace

// ---- the GPU lock ------------------------------------------------------------------------------------

TEST_CASE("gpu lock: acquire writes pid and holder the script's way, release removes only our own", "[api][jobs][gpulock]") {
    const fs::path dir = freshDir("lock_acquire") / "avgen-gpu.lock";
    {
        api::GpuLock lock(dir, "tester");
        REQUIRE(lock.tryAcquire() == api::GpuLockAttempt::Acquired);
        CHECK(lock.ownsDirectory());
        CHECK(fs::is_directory(dir));
        CHECK(api::readLockPid(dir) == static_cast<long>(::getpid()));
        std::ifstream h(dir / "holder");
        std::string holder;
        std::getline(h, holder);
        CHECK(holder == "tester");
        CHECK_FALSE(fs::exists(dir / fmt::format("pid.{}", ::getpid()))); // renamed into place, not left beside it
        lock.release();
        CHECK_FALSE(fs::exists(dir));
    }
    // A holder that lost its lock (someone reclaimed it) does not remove the new owner's.
    {
        api::GpuLock lock(dir, "tester");
        REQUIRE(lock.tryAcquire() == api::GpuLockAttempt::Acquired);
        Child other;
        std::ofstream(dir / "pid") << other.pid << "\n";
        lock.release();
        CHECK(fs::is_directory(dir));
        fs::remove_all(dir);
    }
    // The default path is the script's, from the environment.
    const char* tmp = std::getenv("TMPDIR");
    CHECK(api::GpuLock::defaultPath() == fs::path(tmp != nullptr && tmp[0] ? tmp : "/tmp") / "avgen-gpu.lock");
}

TEST_CASE("gpu lock: a live foreign holder is waited for; a dead one is reclaimed", "[api][jobs][gpulock]") {
    const fs::path dir = freshDir("lock_stale") / "avgen-gpu.lock";
    Child holder;
    scriptStyleLock(dir, holder.pid, "another-agent");
    api::GpuLock lock(dir, "tester");
    CHECK(lock.tryAcquire() == api::GpuLockAttempt::Busy);
    REQUIRE(lock.lastHolder().has_value());
    CHECK(lock.lastHolder()->pid == holder.pid);
    CHECK(lock.lastHolder()->holder == "another-agent");
    CHECK(fs::is_directory(dir)); // untouched
    holder.kill();
    CHECK(lock.tryAcquire() == api::GpuLockAttempt::Acquired);
    CHECK(api::readLockPid(dir) == static_cast<long>(::getpid()));
}

TEST_CASE("gpu lock: a pid-less lock is reclaimed only once it is older than the grace", "[api][jobs][gpulock]") {
    const fs::path dir = freshDir("lock_unowned") / "avgen-gpu.lock";
    fs::create_directory(dir); // a winner between its mkdir and its pid
    api::GpuLock lock(dir, "tester");
    CHECK(lock.tryAcquire() == api::GpuLockAttempt::Busy);
    // Backdate it past the 30 s grace, as an ownerless lock from a crashed writer would be.
    struct timeval times[2];
    ::gettimeofday(&times[0], nullptr);
    times[0].tv_sec -= api::GpuLock::kUnownedGraceSeconds + 5;
    times[1] = times[0];
    REQUIRE(::utimes(dir.c_str(), times) == 0);
    CHECK(lock.tryAcquire() == api::GpuLockAttempt::Acquired);
}

TEST_CASE("gpu lock: a lock held by this process or an ancestor counts as ours, and is left alone",
          "[api][jobs][gpulock]") {
    // The suite's own situation: gpu-lock.sh holds the lock in the wrapper shell, our parent or further up.
    CHECK(api::isSelfOrAncestor(static_cast<long>(::getpid())));
    CHECK(api::isSelfOrAncestor(static_cast<long>(::getppid())));
    CHECK(api::parentPid(static_cast<long>(::getpid())) == static_cast<long>(::getppid()));
    Child child;
    CHECK_FALSE(api::isSelfOrAncestor(child.pid)); // a child is not an ancestor
    for (const long pid : {static_cast<long>(::getppid()), static_cast<long>(::getpid())}) {
        const fs::path dir = freshDir(fmt::format("lock_ancestor_{}", pid)) / "avgen-gpu.lock";
        scriptStyleLock(dir, pid, "gpu-lock.sh");
        {
            api::GpuLock lock(dir, "tester");
            CHECK(lock.tryAcquire() == api::GpuLockAttempt::HeldByAncestor);
            CHECK(lock.held());
            CHECK_FALSE(lock.ownsDirectory());
            lock.release();
        }
        CHECK(fs::is_directory(dir)); // the wrapper's lock is the wrapper's to remove
        CHECK(api::readLockPid(dir) == pid);
    }
}

TEST_CASE("gpu lock: interoperates with tools/gpu-lock.sh in both directions", "[api][jobs][gpulock]") {
    const fs::path script = fs::path(AVGEN_SOURCE_DIR) / "tools" / "gpu-lock.sh";
    REQUIRE(fs::exists(script));
    const fs::path tmp = freshDir("lock_script");
    const fs::path dir = tmp / "avgen-gpu.lock";
    const auto runScript = [&](const std::string& command) {
        const std::string line = fmt::format("TMPDIR='{}' AVGEN_AGENT=script AVGEN_GPU_LOCK_TIMEOUT=0 "
                                             "AVGEN_GPU_LOCK_POLL=0.1 /bin/bash '{}' {} >/dev/null 2>&1",
                                             tmp.string(), script.string(), command);
        const int status = std::system(line.c_str());
        return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
    };
    // Ours held: the script times out (75) instead of sharing the GPU.
    {
        api::GpuLock lock(dir, "cpp");
        REQUIRE(lock.tryAcquire() == api::GpuLockAttempt::Acquired);
        CHECK(runScript("true") == 75);
        CHECK(api::readLockPid(dir) == static_cast<long>(::getpid())); // and did not reclaim a live lock
    }
    CHECK_FALSE(fs::exists(dir));
    CHECK(runScript("true") == 0); // free: it runs, and removes its own lock
    CHECK_FALSE(fs::exists(dir));
    // The script's held (backgrounded, so the holder is not our ancestor): we wait; when it ends, we take it.
    const std::string bg = fmt::format("TMPDIR='{}' AVGEN_AGENT=script /bin/bash '{}' /bin/sleep 2 >/dev/null 2>&1 &",
                                       tmp.string(), script.string());
    REQUIRE(std::system(bg.c_str()) == 0);
    api::GpuLock lock(dir, "cpp");
    const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    // The script writes `pid`, then `holder`: wait for both, or the holder can be read before it exists.
    while (!(fs::exists(dir / "pid") && fs::exists(dir / "holder") && fs::file_size(dir / "holder") > 0) &&
           std::chrono::steady_clock::now() < until) {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    CHECK(lock.tryAcquire() == api::GpuLockAttempt::Busy);
    REQUIRE(lock.lastHolder().has_value());
    CHECK(lock.lastHolder()->holder == "script");
    api::GpuLockAttempt a = api::GpuLockAttempt::Busy;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (a == api::GpuLockAttempt::Busy && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        a = lock.tryAcquire();
    }
    CHECK(a == api::GpuLockAttempt::Acquired);
}

// ---- the job table -------------------------------------------------------------------------------------

TEST_CASE("job table: states, cancel, wait and the listener", "[api][jobs]") {
    api::JobTable table;
    std::vector<json> events;
    table.setListener([&](const json& e) { events.push_back(e); });
    const std::string id = table.create("render.start", "sequence", json{{"start", 0}}, "/tmp/s.json");
    CHECK(id == "job-1");
    REQUIRE(table.get(id));
    CHECK(table.get(id)->state == api::JobState::Queued);
    CHECK(table.unfinished() == 1);
    CHECK_FALSE(table.get("job-9"));
    // A wait on an unfinished job times out with the record as it is.
    const auto t0 = std::chrono::steady_clock::now();
    const auto early = table.wait(id, std::chrono::milliseconds(50));
    CHECK(std::chrono::steady_clock::now() - t0 >= std::chrono::milliseconds(45));
    REQUIRE(early);
    CHECK(early->state == api::JobState::Queued);
    // A waiter on another thread is released by the update that finishes it.
    std::optional<api::JobRecord> waited;
    std::thread waiter([&] { waited = table.wait(id, std::chrono::seconds(10)); });
    table.update(id, [](api::JobRecord& r) { r.state = api::JobState::Running; r.phase = "rendering"; });
    CHECK(table.requestCancel(id));
    CHECK(table.cancelRequested(id));
    table.update(id, [](api::JobRecord& r) { r.state = api::JobState::Cancelled; r.phase = "cancelled"; });
    waiter.join();
    REQUIRE(waited);
    CHECK(waited->state == api::JobState::Cancelled);
    CHECK(waited->finished != std::chrono::system_clock::time_point{});
    CHECK_FALSE(table.requestCancel(id)); // finished: nothing to cancel
    CHECK(table.unfinished() == 0);
    // Every state change reached the listener.
    REQUIRE(events.size() >= 2);
    CHECK(events.front().at("state") == "running");
    CHECK(events.back().at("state") == "cancelled");
    const json j = table.get(id)->toJson();
    CHECK(j.at("state") == "cancelled");
    CHECK(j.at("error").is_null());
}

// ---- the EngineHost's job queue, through the tools ------------------------------------------------------

TEST_CASE("engine host jobs: a sequence runs queued -> preparing -> running -> succeeded with hashes and files",
          "[api][jobs]") {
    Rig rig("lifecycle");
    std::vector<std::string> states;
    rig.session->jobs().setListener([&](const json& e) {
        const std::string s = e.at("state").get<std::string>();
        if (states.empty() || states.back() != s) {
            states.push_back(s);
        }
    });
    CHECK(rig.host->canRender());
    CHECK_FALSE(rig.host->jobStats().deviceCreated);
    const std::string id = rig.start(json{{"range", "0:0.5"}, {"fps", 8}, {"width", 64}, {"height", 32},
                                          {"output", "png"}, {"path", "renders/seq"}});
    const json queued = rig.call("job.status", json{{"id", id}}).result;
    CHECK(queued.at("state") == "queued");
    CHECK(queued.at("framesTotal") == 4);
    const json done = rig.wait(id);
    CHECK(done.at("state") == "succeeded");
    CHECK(done.at("timedOut") == false);
    const json& result = done.at("result");
    CHECK(result.at("frameHashes").size() == 4);
    CHECK(result.at("sequenceHash").get<std::string>().size() == 16);
    REQUIRE(done.at("outputs").size() == 4);
    for (const json& o : done.at("outputs")) {
        const auto sha = sha256File(o.at("path").get<std::string>());
        REQUIRE(sha);
        CHECK(o.at("sha256") == *sha);
    }
    CHECK(fs::path(done.at("outputs")[0].at("path").get<std::string>()).parent_path() ==
          rig.root / "proj" / "renders" / "seq");
    CHECK(result.at("lock").at("waited") == false);
    CHECK(result.at("lock").at("path") == rig.lockDir.string());
    CHECK(result.at("timing").at("deviceCreated") == true);
    CHECK_FALSE(fs::exists(rig.lockDir)); // released between jobs: an idle host holds nothing
    CHECK(rig.stats.devices == 1);
    CHECK(states == std::vector<std::string>{"queued", "preparing", "running", "succeeded"});
    // The snapshot is in the session's scratch directory, not beside the project.
    const fs::path snapshot = done.at("snapshot").get<std::string>();
    CHECK(snapshot.parent_path() == *rig.session->scratchDir());
    CHECK_FALSE(api::pathInside(snapshot, rig.root));
    // A second job reuses the device.
    const std::string again = rig.start(json{{"range", "0:0.25"}, {"fps", 8}, {"output", "png"}, {"path", "renders/b"}});
    CHECK(rig.wait(again).at("result").at("timing").at("deviceCreated") == false);
    CHECK(rig.stats.devices == 1);
    // job.list has both, oldest first.
    const json list = rig.call("job.list").result.at("jobs");
    REQUIRE(list.size() == 2);
    CHECK(list[0].at("id") == id);
}

TEST_CASE("engine host jobs: a render right after parameter.set, unsaved, renders the change", "[api][jobs]") {
    Rig rig("unsaved");
    const auto capture = [&](const std::string& path) {
        const api::CallResult r = rig.call("render.capture", json{{"frame", 3}, {"fps", 24}, {"path", path}});
        INFO(r.toWire().dump());
        REQUIRE(r.success);
        const json done = rig.wait(*r.job);
        REQUIRE(done.at("state") == "succeeded");
        return done.at("result").at("frameHash").get<std::string>();
    };
    const std::string before = capture("a.png");
    CHECK(capture("a2.png") == before);
    const fs::path file = rig.root / "proj" / "proj.json";
    const std::string onDisk = *sha256File(file);
    REQUIRE(rig.call("parameter.set", json{{"path", "camera/fov"}, {"value", 71.0}}).success);
    const std::string after = capture("b.png");
    CHECK(after != before);
    CHECK(*sha256File(file) == onDisk); // nothing was saved
    CHECK(fs::exists(rig.root / "proj" / "b.png"));
}

TEST_CASE("engine host jobs: output outside the write roots is refused before any GPU work", "[api][jobs]") {
    Rig rig("outside");
    const fs::path outside = freshDir("outside_target");
    for (const json& args : {json{{"frame", 0}, {"path", (outside / "x.png").string()}},
                             json{{"frame", 0}, {"path", "../../../x.png"}}}) {
        const api::CallResult r = rig.call("render.capture", args);
        INFO(r.toWire().dump());
        REQUIRE_FALSE(r.success);
        CHECK(r.error->code == ai::ToolErrorCode::PermissionDenied);
    }
    const api::CallResult seq = rig.call("render.start", json{{"range", "0:1"}, {"output", "png"}, {"path", outside.string()}});
    CHECK(seq.error->code == ai::ToolErrorCode::PermissionDenied);
    CHECK(rig.stats.created == 0);
    CHECK(rig.stats.devices == 0);
    CHECK_FALSE(rig.host->jobStats().deviceCreated);
    CHECK(rig.call("job.list").result.at("jobs").empty());
}

TEST_CASE("engine host jobs: the FIFO holds four; cancel stops a queued job and a running one keeps its frames",
          "[api][jobs]") {
    Rig rig("queue");
    std::vector<std::string> ids;
    for (int i = 0; i < 4; ++i) {
        ids.push_back(rig.start(json{{"range", "0:1"}, {"fps", 10}, {"output", "png"}, {"path", fmt::format("q{}", i)}}));
    }
    const api::CallResult fifth = rig.call("render.start", json{{"range", "0:1"}, {"output", "png"}, {"path", "q5"}});
    REQUIRE_FALSE(fifth.success);
    CHECK(fifth.error->code == ai::ToolErrorCode::Busy);
    CHECK(rig.call("job.list").result.at("jobs").size() == 4); // a refused submission leaves no record
    // A queued job cancelled never starts.
    REQUIRE(rig.call("job.cancel", json{{"id", ids[3]}}).success);
    // The first runs a few slices, then is cancelled mid-sequence.
    for (int i = 0; i < 6; ++i) {
        (void)rig.host->stepJobs();
    }
    const json mid = rig.call("job.status", json{{"id", ids[0]}}).result;
    REQUIRE(mid.at("state") == "running");
    REQUIRE(rig.call("job.cancel", json{{"id", ids[0]}}).success);
    const json cancelled = rig.wait(ids[0]);
    CHECK(cancelled.at("state") == "cancelled");
    const std::size_t partial = cancelled.at("outputs").size();
    CHECK(partial > 0);
    CHECK(partial < 10);
    CHECK(cancelled.at("result").at("frameHashes").size() == partial);
    const json never = rig.wait(ids[3]);
    CHECK(never.at("state") == "cancelled");
    CHECK(never.at("outputs").empty());
    CHECK(rig.wait(ids[1]).at("state") == "succeeded");
    CHECK(rig.wait(ids[2]).at("state") == "succeeded");
    CHECK(rig.stats.started == 3); // the cancelled-in-queue job never reached the GPU
    CHECK(rig.call("job.status", json{{"id", "job-99"}}).error->code == ai::ToolErrorCode::NotFound);
}

TEST_CASE("engine host jobs: a job waits for the machine lock a live process holds, then runs", "[api][jobs][gpulock]") {
    Rig rig("lockwait");
    Child holder;
    scriptStyleLock(rig.lockDir, holder.pid, "other-agent");
    const std::string id = rig.start(json{{"range", "0:0.25"}, {"fps", 8}, {"output", "png"}, {"path", "w"}});
    const json waiting = rig.wait(id, 400);
    CHECK(waiting.at("timedOut") == true);
    CHECK(waiting.at("state") == "preparing");
    CHECK(waiting.at("phase").get<std::string>().find("waiting for the GPU lock") == 0);
    CHECK(waiting.at("phase").get<std::string>().find("other-agent") != std::string::npos);
    CHECK(rig.stats.devices == 0); // the device waits for the lock too
    holder.kill(); // the holder dies without releasing: reclaimed as stale
    const json done = rig.wait(id);
    CHECK(done.at("state") == "succeeded");
    CHECK(done.at("result").at("lock").at("waited") == true);
    CHECK(done.at("result").at("lock").at("waitedSeconds").get<double>() > 0.3);
}

TEST_CASE("engine host jobs: job.wait blocks the caller, never the host; calls stay answered while a job runs",
          "[api][jobs]") {
    Rig rig("offhost");
    rig.stats.frameSleepMs = 20; // 40 frames: about 0.8 s of "GPU"
    std::thread hostThread([&] { rig.host->run(*rig.session); });
    const api::CallResult started =
        rig.session->call("render.start", json{{"range", "0:1"}, {"fps", 40}, {"output", "png"}, {"path", "slow"}});
    REQUIRE(started.success);
    std::atomic<bool> waitDone{false};
    json waited;
    std::thread waiter([&] {
        waited = rig.session->call("job.wait", json{{"id", *started.job}, {"timeoutMs", 20'000}}).result;
        waitDone = true;
    });
    // While the waiter blocks and the job renders, a read is answered, promptly, by the host.
    int answeredWhileRunning = 0;
    for (int i = 0; i < 20 && !waitDone; ++i) {
        const auto t0 = std::chrono::steady_clock::now();
        const api::CallResult s = rig.session->call("job.status", json{{"id", *started.job}});
        const api::CallResult p = rig.session->call("parameter.get", json{{"paths", json::array({"camera/fov"})}});
        const auto ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        REQUIRE(s.success);
        REQUIRE(p.success);
        if (s.result.at("state") == "running") {
            ++answeredWhileRunning;
            CHECK(ms < 500.0);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    waiter.join();
    CHECK(answeredWhileRunning > 0);
    CHECK(waited.at("state") == "succeeded");
    rig.host->stop();
    hostThread.join();
    rig.host->bindHostThread();
}

TEST_CASE("engine host jobs: no GPU half, and the live policy, refuse render jobs", "[api][jobs]") {
    {
        Rig cpu("nogpu", false);
        CHECK_FALSE(cpu.host->canRender());
        const api::CallResult r = cpu.call("render.capture", json{{"frame", 0}, {"path", "a.png"}});
        REQUIRE_FALSE(r.success);
        CHECK(r.error->code == ai::ToolErrorCode::Unavailable);
        CHECK(cpu.call("job.list").success);
    }
    // Owner decision 3: while Studio runs a show, an external client's render is Busy (the job class).
    app::Engine engine(app::EngineMode::Offline);
    api::LocalHost host(engine);
    host.queue().bindToCurrentThread();
    host.setShowPredicate([] { return true; });
    api::SessionOptions so;
    so.client = api::ClientInfo{api::ClientKind::JsonRpc, "external", "1"};
    so.policy = api::HostPolicy::Live;
    auto session = api::Session::create(std::move(so), host);
    REQUIRE(session.has_value());
    for (const auto& [tool, args] : {std::pair{"render.capture", json{{"path", "x.png"}}},
                                     std::pair{"render.start", json{{"path", "x"}, {"range", "0:1"}, {"output", "png"}}}}) {
        const api::CallResult r = (*session)->call(tool, args);
        INFO(tool << " " << r.toWire().dump());
        REQUIRE_FALSE(r.success);
        CHECK(r.error->code == ai::ToolErrorCode::Busy);
    }
    CHECK((*session)->call("job.list").success); // reads stay available
}

TEST_CASE("engine host jobs: a cancelled sequence in a reused folder lists only its own frames", "[api][jobs]") {
    // UI-readiness audit (job and result lifecycle): finishActive listed every frame_%06d file in the range, so an
    // earlier render's frames were reported, with their sha256s, as a cancelled job's output.
    Rig rig("reuse");
    const json first = rig.wait(rig.start(json{{"range", "0:1"}, {"fps", 10}, {"output", "png"}, {"path", "renders/r"}}));
    REQUIRE(first.at("state") == "succeeded");
    REQUIRE(first.at("outputs").size() == 10);
    // The earlier render's files are old.
    for (const json& o : first.at("outputs")) {
        const fs::path f = o.at("path").get<std::string>();
        fs::last_write_time(f, fs::last_write_time(f) - std::chrono::hours(1));
    }
    const std::string again = rig.start(json{{"range", "0:1"}, {"fps", 10}, {"output", "png"}, {"path", "renders/r"}});
    for (int i = 0; i < 4; ++i) {
        (void)rig.host->stepJobs();
    }
    REQUIRE(rig.call("job.cancel", json{{"id", again}}).success);
    const json cancelled = rig.wait(again);
    REQUIRE(cancelled.at("state") == "cancelled");
    const std::size_t hashed = cancelled.at("result").at("frameHashes").size();
    CHECK(hashed < 10);
    CHECK(cancelled.at("outputs").size() <= hashed); // never the earlier render's leftovers
    for (const json& o : cancelled.at("outputs")) {
        CHECK(o.at("frame").get<std::uint64_t>() < hashed);
    }
}
