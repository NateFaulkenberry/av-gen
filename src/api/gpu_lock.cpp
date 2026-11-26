#include "api/gpu_lock.hpp"

#include "core/log.hpp"

#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <system_error>

#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#if defined(__APPLE__)
#include <sys/sysctl.h>
#endif

namespace avgen::api {

namespace fs = std::filesystem;

const char* gpuLockAttemptName(GpuLockAttempt attempt) {
    switch (attempt) {
    case GpuLockAttempt::Acquired: return "acquired";
    case GpuLockAttempt::HeldByAncestor: return "held-by-ancestor";
    case GpuLockAttempt::Busy: return "busy";
    }
    return "busy";
}

long parentPid(long pid) {
    if (pid <= 0) {
        return 0;
    }
#if defined(__APPLE__)
    int mib[4] = {CTL_KERN, KERN_PROC, KERN_PROC_PID, static_cast<int>(pid)};
    struct kinfo_proc info {};
    std::size_t size = sizeof info;
    if (::sysctl(mib, 4, &info, &size, nullptr, 0) != 0 || size == 0) {
        return 0;
    }
    return static_cast<long>(info.kp_eproc.e_ppid);
#else
    std::ifstream stat("/proc/" + std::to_string(pid) + "/stat");
    std::string text((std::istreambuf_iterator<char>(stat)), std::istreambuf_iterator<char>());
    // "<pid> (<comm>) <state> <ppid> ...": comm may hold spaces and parentheses, so parse after the LAST ')'.
    const auto close = text.rfind(')');
    if (close == std::string::npos) {
        return 0;
    }
    std::istringstream rest(text.substr(close + 1));
    std::string state;
    long ppid = 0;
    rest >> state >> ppid;
    return ppid;
#endif
}

bool isSelfOrAncestor(long pid) {
    if (pid <= 0) {
        return false;
    }
    long p = static_cast<long>(::getpid());
    // launchd/init is everyone's ancestor and never a lock holder worth honouring; the walk stops above it.
    for (int depth = 0; depth < 64 && p > 1; ++depth) {
        if (p == pid) {
            return true;
        }
        p = parentPid(p);
    }
    return false;
}

bool processAlive(long pid) {
    if (pid <= 0) {
        return false;
    }
    if (::kill(static_cast<pid_t>(pid), 0) == 0) {
        return true;
    }
    return errno == EPERM;
}

std::optional<long> readLockPid(const fs::path& dir) {
    std::ifstream in(dir / "pid");
    if (!in) {
        return std::nullopt;
    }
    std::string text;
    std::getline(in, text);
    const auto first = text.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) {
        return std::nullopt;
    }
    char* end = nullptr;
    const long pid = std::strtol(text.c_str() + first, &end, 10);
    if (end == text.c_str() + first || pid <= 0) {
        return std::nullopt;
    }
    return pid;
}

namespace {

std::string readHolder(const fs::path& dir) {
    std::ifstream in(dir / "holder");
    std::string text;
    std::getline(in, text);
    return text;
}

// Seconds since the directory was last modified (the script's `stat -f %m`), or -1.
long ageSeconds(const fs::path& dir) {
    struct stat st {};
    if (::stat(dir.c_str(), &st) != 0) {
        return -1;
    }
    const auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    return static_cast<long>(now - st.st_mtime);
}

} // namespace

GpuLock::GpuLock(fs::path dir, std::string holderName) : dir_(std::move(dir)), holderName_(std::move(holderName)) {
    if (holderName_.empty()) {
        holderName_ = defaultHolder();
    }
}

GpuLock::~GpuLock() { release(); }

fs::path GpuLock::defaultPath() {
    const char* tmp = std::getenv("TMPDIR");
    const fs::path base = (tmp != nullptr && tmp[0] != '\0') ? fs::path(tmp) : fs::path("/tmp");
    return base / "avgen-gpu.lock";
}

std::string GpuLock::defaultHolder() {
    const char* agent = std::getenv("AVGEN_AGENT");
    if (agent != nullptr && agent[0] != '\0') {
        return agent;
    }
    return "avgen-" + std::to_string(static_cast<long long>(::getpid()));
}

fs::path GpuLock::target() const { return dir_.empty() ? defaultPath() : dir_; }

bool GpuLock::make(const fs::path& dir) {
    if (::mkdir(dir.c_str(), 0777) != 0) {
        return false;
    }
    // The pid arrives whole, by rename, as the script's `echo $$ > pid.$$ && mv pid.$$ pid` does.
    const std::string self = std::to_string(static_cast<long long>(::getpid()));
    const fs::path partial = dir / ("pid." + self);
    {
        std::ofstream out(partial);
        out << self << "\n";
    }
    std::error_code ec;
    fs::rename(partial, dir / "pid", ec);
    {
        std::ofstream out(dir / "holder");
        out << holderName_ << "\n";
    }
    owned_ = true;
    ancestor_ = false;
    lastHolder_.reset();
    return true;
}

GpuLockAttempt GpuLock::tryAcquire() {
    if (owned_) {
        return GpuLockAttempt::Acquired;
    }
    const fs::path dir = target();
    used_ = dir;
    // A few rounds: a reclaim is followed by a fresh mkdir, which another waiter may win.
    for (int round = 0; round < 4; ++round) {
        if (make(dir)) {
            return GpuLockAttempt::Acquired;
        }
        std::error_code ec;
        if (!fs::is_directory(dir, ec)) {
            if (fs::exists(dir, ec)) {
                // Something that is not a directory sits on the path: nobody can take the lock. Report it as held,
                // so the job waits (and times out with a message) rather than spinning.
                log::warn("gpu-lock: '{}' exists and is not a directory", dir.string());
                lastHolder_ = GpuLockHolder{0, "(not a directory)"};
                return GpuLockAttempt::Busy;
            }
            continue; // released between our mkdir and our look: try again
        }
        const std::optional<long> pid = readLockPid(dir);
        if (pid) {
            if (isSelfOrAncestor(*pid)) {
                ancestor_ = true;
                lastHolder_ = GpuLockHolder{*pid, readHolder(dir)};
                return GpuLockAttempt::HeldByAncestor;
            }
            if (!processAlive(*pid)) {
                // The holder died without releasing. Reclaim the lock THAT pid owns, not one another waiter
                // reclaimed since.
                if (readLockPid(dir) == pid) {
                    log::warn("gpu-lock: stale lock from pid {}, reclaiming", *pid);
                    fs::remove_all(dir, ec);
                }
                continue;
            }
            lastHolder_ = GpuLockHolder{*pid, readHolder(dir)};
            return GpuLockAttempt::Busy;
        }
        // No pid: a winner between its mkdir and its first write, or one that died there. Only time tells.
        const long age = ageSeconds(dir);
        if (age > kUnownedGraceSeconds) {
            log::warn("gpu-lock: a lock with no pid for {}s, reclaiming", age);
            fs::remove_all(dir, ec);
            continue;
        }
        lastHolder_ = GpuLockHolder{0, readHolder(dir)};
        return GpuLockAttempt::Busy;
    }
    return GpuLockAttempt::Busy;
}

void GpuLock::setHolderName(std::string name) {
    if (name.empty() || name == holderName_) {
        return;
    }
    holderName_ = std::move(name);
    if (owned_ && readLockPid(used_) == static_cast<long>(::getpid())) {
        std::ofstream out(used_ / "holder");
        out << holderName_ << "\n";
    }
}

void GpuLock::release() {
    ancestor_ = false;
    if (!owned_) {
        return;
    }
    owned_ = false;
    // Only while `pid` is still ours: a lock somebody reclaimed from us (we looked dead) is theirs now.
    if (readLockPid(used_) == static_cast<long>(::getpid())) {
        std::error_code ec;
        fs::remove_all(used_, ec);
    }
}

} // namespace avgen::api
