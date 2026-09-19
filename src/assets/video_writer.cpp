// Backend selection and the external-ffmpeg backend (ADR-020, research §8.1). The native backend
// lives in video_writer_apple.mm; on other platforms video_writer_native_stub.cpp reports it absent.
//
// ffmpeg is never linked and never shipped: it is a separate process started with posix_spawn (no
// shell, so paths need no quoting), fed raw RGBA frames on stdin, its stderr captured to a
// temporary log whose tail is quoted in error messages.

#include "assets/video_writer.hpp"

#include "assets/video_writer_native.hpp"
#include "core/log.hpp"

#include <fmt/format.h>
#include <fmt/ranges.h>

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <mutex>
#include <optional>
#include <string_view>
#include <system_error>
#include <utility>

#ifndef _WIN32
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

extern char** environ; // NOLINT(readability-redundant-declaration): not declared by <unistd.h> on macOS
#endif

namespace avgen::assets {

namespace {

#ifdef _WIN32
constexpr const char* kFfmpegExe = "ffmpeg.exe";
constexpr char kPathSeparator = ';';
#else
constexpr const char* kFfmpegExe = "ffmpeg";
constexpr char kPathSeparator = ':';
#endif

std::string lowerExtension(const std::filesystem::path& path) {
    std::string ext = path.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return ext;
}

bool isExecutableFile(const std::filesystem::path& path) {
    std::error_code ec;
    if (!std::filesystem::is_regular_file(path, ec)) {
        return false;
    }
#ifdef _WIN32
    return true;
#else
    return ::access(path.c_str(), X_OK) == 0;
#endif
}

// A directory hint means "look for the executable in here".
std::filesystem::path ffmpegCandidate(const std::filesystem::path& hint) {
    std::error_code ec;
    if (std::filesystem::is_directory(hint, ec)) {
        return hint / kFfmpegExe;
    }
    return hint;
}

bool isNativeCodec(const std::string& codec) {
    const auto codecs = nativeCodecs();
    return std::find(codecs.begin(), codecs.end(), codec) != codecs.end();
}

bool isMp4Family(const std::string& ext) {
    return ext == ".mp4" || ext == ".mov" || ext == ".m4v";
}

// ---- ffmpeg command line -------------------------------------------------------------------------

struct FfmpegCodecPlan {
    std::string encoder;              // -c:v
    std::vector<std::string> quality; // encoder-specific quality flags
    std::string pixelFormat;          // -pix_fmt; empty = encoder default
    bool evenWidth = false;           // chroma subsampling constraints
    bool evenHeight = false;
};

// The native codec ids map onto ffmpeg's usual encoders so "auto" can fall back seamlessly; any
// other name is passed through as an encoder name.
FfmpegCodecPlan planFfmpegCodec(const std::string& codec, int quality) {
    const double q = static_cast<double>(std::clamp(quality, 0, 100)) / 100.0;
    const bool wants4444 = codec.find("4444") != std::string::npos;
    FfmpegCodecPlan plan;
    if (codec == "h264") {
        plan.encoder = "libx264";
    } else if (codec == "hevc") {
        plan.encoder = "libx265";
    } else if (codec == "prores4444" || codec == "prores422") {
        plan.encoder = "prores_ks";
    } else {
        plan.encoder = codec;
    }

    const auto crf = [q](double worst, double span) {
        return fmt::format("{}", std::lround(worst - q * span));
    };
    if (plan.encoder == "libx264" || plan.encoder == "libx265") {
        plan.quality = {"-crf", crf(51.0, 40.0)}; // q=80 -> 19, q=100 -> 11
        plan.pixelFormat = "yuv420p";
        plan.evenWidth = plan.evenHeight = true;
    } else if (plan.encoder == "libvpx-vp9" || plan.encoder == "libaom-av1") {
        plan.quality = {"-crf", crf(63.0, 55.0), "-b:v", "0"}; // constant-quality mode
        plan.pixelFormat = "yuv420p";
        plan.evenWidth = plan.evenHeight = true;
    } else if (plan.encoder == "libsvtav1") {
        plan.quality = {"-crf", crf(63.0, 55.0)};
        plan.pixelFormat = "yuv420p";
        plan.evenWidth = plan.evenHeight = true;
    } else if (plan.encoder == "prores_ks") {
        const bool p4444 = wants4444;
        plan.quality = {"-profile:v", p4444 ? "4" : "3"};
        plan.pixelFormat = p4444 ? "yuva444p10le" : "yuv422p10le";
        plan.evenWidth = !p4444;
    } else if (plan.encoder == "h264_videotoolbox" || plan.encoder == "hevc_videotoolbox") {
        plan.quality = {"-q:v", fmt::format("{}", std::max(1, std::clamp(quality, 0, 100)))};
        plan.pixelFormat = "yuv420p";
        plan.evenWidth = plan.evenHeight = true;
    }
    return plan;
}

std::string audioEncoderFor(const std::string& ext, const std::string& videoEncoder) {
    if (videoEncoder.rfind("prores", 0) == 0) {
        return "pcm_s16le";
    }
    if (ext == ".webm") {
        return "libopus";
    }
    if (isMp4Family(ext) || ext == ".mkv") {
        return "aac";
    }
    return {};
}

std::vector<std::string> ffmpegArguments(const std::filesystem::path& file, const VideoSettings& settings,
                                         const FfmpegCodecPlan& plan) {
    const std::string ext = lowerExtension(file);
    std::vector<std::string> args = {"-hide_banner", "-loglevel", "error", "-y",
                                     // input 0: raw frames on stdin
                                     "-f", "rawvideo", "-pix_fmt", "rgba", "-s",
                                     fmt::format("{}x{}", settings.width, settings.height), "-r",
                                     fmt::format("{}", settings.fps), "-i", "-"};
    const bool audio = !settings.audio.empty();
    if (audio) {
        // input 1: the audio file, positioned so that frame 0 lies at audioOffsetSeconds
        if (settings.audioOffsetSeconds > 0.0) {
            args.insert(args.end(), {"-ss", fmt::format("{}", settings.audioOffsetSeconds)});
        } else if (settings.audioOffsetSeconds < 0.0) {
            args.insert(args.end(), {"-itsoffset", fmt::format("{}", -settings.audioOffsetSeconds)});
        }
        args.insert(args.end(), {"-i", settings.audio.string()});
    }
    args.insert(args.end(), {"-map", "0:v:0"});
    if (audio) {
        args.insert(args.end(), {"-map", "1:a:0", "-shortest"});
    }
    args.insert(args.end(), {"-c:v", plan.encoder});
    args.insert(args.end(), plan.quality.begin(), plan.quality.end());
    if (!plan.pixelFormat.empty()) {
        args.insert(args.end(), {"-pix_fmt", plan.pixelFormat});
    }
    // ADR-365: the same three tags the native backend writes, so the two backends produce files a
    // player reads the same way. The input is 8-bit RGBA that the tone map has already
    // sRGB-encoded, and BT.709 is what a video deliverable of that is. Without these, ffmpeg writes
    // the file untagged and every downstream tool guesses -- usually correctly, which is exactly
    // what makes it worth fixing before anything depends on the guess.
    args.insert(args.end(), {"-color_primaries", "bt709", "-color_trc", "bt709",
                             "-colorspace", "bt709"});
    if (audio) {
        if (const std::string aenc = audioEncoderFor(ext, plan.encoder); !aenc.empty()) {
            args.insert(args.end(), {"-c:a", aenc});
        }
    }
    if (isMp4Family(ext)) {
        args.insert(args.end(), {"-movflags", "+faststart"});
    }
    args.push_back(file.string());
    return args;
}

#ifndef _WIN32

// ---- ffmpeg backend (POSIX) -----------------------------------------------------------------------

class FfmpegWriter final : public VideoWriter {
public:
    static Result<std::unique_ptr<VideoWriter>>
    open(const std::filesystem::path& file, const VideoSettings& settings, const std::filesystem::path& exe) {
        const FfmpegCodecPlan plan = planFfmpegCodec(settings.codec, settings.quality);
        if ((plan.evenWidth && settings.width % 2 != 0) || (plan.evenHeight && settings.height % 2 != 0)) {
            return fail("video: {} ({}) needs even dimensions, got {}x{}", plan.encoder, plan.pixelFormat,
                        settings.width, settings.height);
        }
        std::error_code ec;
        if (file.has_parent_path()) {
            std::filesystem::create_directories(file.parent_path(), ec);
        }
        auto writer = std::unique_ptr<FfmpegWriter>(new FfmpegWriter(file, settings));
        if (auto spawned = writer->spawn(exe, ffmpegArguments(file, settings, plan)); !spawned) {
            return std::unexpected(spawned.error());
        }
        log::info("video: ffmpeg backend ({}), {} {}x{} @ {} fps -> {}", exe.string(), plan.encoder,
                  settings.width, settings.height, settings.fps, file.string());
        return std::unique_ptr<VideoWriter>(std::move(writer));
    }

    ~FfmpegWriter() override {
        if (!finished_) {
            abandon();
        }
    }
    FfmpegWriter(const FfmpegWriter&) = delete;
    FfmpegWriter& operator=(const FfmpegWriter&) = delete;

    Result<void> writeFrame(std::span<const std::uint8_t> rgba) override {
        if (error_) {
            return std::unexpected(*error_);
        }
        if (finished_) {
            return fail("video: writeFrame() after finish() for '{}'", path_.string());
        }
        if (rgba.size() != frameBytes_) {
            return setError(fmt::format("video: frame {} has {} bytes, expected {} ({}x{} RGBA8)", frames_,
                                        rgba.size(), frameBytes_, settings_.width, settings_.height));
        }
        if (auto written = writeAll(rgba.data(), rgba.size()); !written) {
            // ffmpeg most likely exited: reap it so its complaint can be quoted.
            const int status = reap();
            return setError(written.error().message + describeExit(status) + logTail());
        }
        ++frames_;
        return {};
    }

    Result<void> finish() override {
        if (finished_) {
            return fail("video: finish() called twice for '{}'", path_.string());
        }
        finished_ = true;
        if (error_) {
            abandon();
            return std::unexpected(*error_);
        }
        closeStdin();
        const int status = reap();
        const std::string tail = logTail();
        std::error_code ec;
        std::filesystem::remove(logPath_, ec);
        if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
            return fail("video: ffmpeg failed writing '{}'{}{}", path_.string(), describeExit(status), tail);
        }
        log::info("video: wrote {} frames to {}", frames_, path_.string());
        return {};
    }

    std::size_t framesWritten() const override { return frames_; }
    const std::filesystem::path& path() const override { return path_; }
    std::string backendName() const override { return "ffmpeg"; }

private:
    FfmpegWriter(std::filesystem::path file, VideoSettings settings)
        : path_(std::move(file))
        , settings_(std::move(settings))
        , frameBytes_(static_cast<std::size_t>(settings_.width) * settings_.height * 4) {}

    std::unexpected<Error> setError(std::string message) {
        error_ = Error{std::move(message)};
        return std::unexpected(*error_);
    }

    Result<void> spawn(const std::filesystem::path& exe, const std::vector<std::string>& args) {
        // stderr goes to a temp log so failures can be explained.
        std::string tmpl = (std::filesystem::temp_directory_path() / "avgen_ffmpeg_XXXXXX").string();
        std::vector<char> nameBuf(tmpl.begin(), tmpl.end());
        nameBuf.push_back('\0');
        const int logFd = ::mkstemp(nameBuf.data());
        if (logFd < 0) {
            return fail("video: cannot create a log file for ffmpeg: {}", std::strerror(errno));
        }
        logPath_ = nameBuf.data();

        // SIGPIPE has to be ignored process-wide, not merely blocked on whichever thread does the
        // writing. `writeAll` blocks it around its own writes, and that is enough only while the
        // writing thread is the only thread in the process with it unblocked. It stops being true
        // the moment anything else starts a thread: AVFoundation leaves CoreMedia worker threads
        // behind after a native render, and the signal is then delivered to one of those -- idle,
        // parked in a semaphore, with nothing to do with this pipe -- which kills the process
        // instead of returning EPIPE to the writer. That is a crash on the ordinary path where a
        // user's ffmpeg dies mid-render, and it was reaching the whole test binary too.
        //
        // Ignoring it makes write() report EPIPE on every thread, which is what the error path
        // below already expects. The per-thread block in `writeAll` stays: it costs nothing and it
        // keeps the guarantee local to the code that relies on it.
        static std::once_flag ignoreSigPipeOnce;
        std::call_once(ignoreSigPipeOnce, [] { ::signal(SIGPIPE, SIG_IGN); });

        int fds[2] = {-1, -1};
        if (::pipe(fds) != 0) {
            const int e = errno;
            ::close(logFd);
            return fail("video: cannot create a pipe for ffmpeg: {}", std::strerror(e));
        }
        ::fcntl(fds[0], F_SETFD, FD_CLOEXEC);
        ::fcntl(fds[1], F_SETFD, FD_CLOEXEC);

        posix_spawn_file_actions_t actions;
        posix_spawn_file_actions_init(&actions);
        posix_spawn_file_actions_adddup2(&actions, fds[0], STDIN_FILENO); // dup2 clears CLOEXEC
        posix_spawn_file_actions_addopen(&actions, STDOUT_FILENO, "/dev/null", O_WRONLY, 0);
        posix_spawn_file_actions_adddup2(&actions, logFd, STDERR_FILENO);

        std::vector<std::string> argvStore;
        argvStore.reserve(args.size() + 1);
        argvStore.push_back(exe.string());
        argvStore.insert(argvStore.end(), args.begin(), args.end());
        std::vector<char*> argv;
        argv.reserve(argvStore.size() + 1);
        for (auto& a : argvStore) {
            argv.push_back(a.data());
        }
        argv.push_back(nullptr);

        pid_t pid = -1;
        const int rc = ::posix_spawn(&pid, exe.c_str(), &actions, nullptr, argv.data(), environ);
        posix_spawn_file_actions_destroy(&actions);
        ::close(fds[0]);
        ::close(logFd);
        if (rc != 0) {
            ::close(fds[1]);
            std::error_code ec;
            std::filesystem::remove(logPath_, ec);
            return fail("video: cannot start '{}': {}", exe.string(), std::strerror(rc));
        }
        pid_ = pid;
        stdinFd_ = fds[1];
        log::debug("video: ffmpeg pid {} args: {}", static_cast<long>(pid), fmt::join(args, " "));
        return {};
    }

    // Writes everything or fails. SIGPIPE (ffmpeg gone) is blocked on this thread for the duration
    // and consumed if it fired, so the error surfaces as EPIPE instead of killing the process.
    Result<void> writeAll(const std::uint8_t* data, std::size_t size) {
        sigset_t pipeSet;
        sigset_t oldSet;
        sigemptyset(&pipeSet);
        sigaddset(&pipeSet, SIGPIPE);
        pthread_sigmask(SIG_BLOCK, &pipeSet, &oldSet);
        std::optional<Error> error;
        while (size > 0) {
            const ssize_t n = ::write(stdinFd_, data, size);
            if (n < 0) {
                if (errno == EINTR) {
                    continue;
                }
                const int e = errno;
                if (e == EPIPE) {
                    sigset_t pending;
                    sigemptyset(&pending);
                    if (sigpending(&pending) == 0 && sigismember(&pending, SIGPIPE)) {
                        int sig = 0;
                        sigwait(&pipeSet, &sig);
                    }
                }
                error = Error{
                    fmt::format("video: writing frame {} to ffmpeg failed: {}", frames_, std::strerror(e))};
                break;
            }
            data += n;
            size -= static_cast<std::size_t>(n);
        }
        pthread_sigmask(SIG_SETMASK, &oldSet, nullptr);
        if (error) {
            return std::unexpected(*error);
        }
        return {};
    }

    void closeStdin() {
        if (stdinFd_ >= 0) {
            ::close(stdinFd_);
            stdinFd_ = -1;
        }
    }

    // Waits for the child and returns its wait status (-1 when already reaped or never started).
    int reap() {
        if (pid_ <= 0) {
            return -1;
        }
        int status = 0;
        while (::waitpid(pid_, &status, 0) < 0) {
            if (errno != EINTR) {
                status = -1;
                break;
            }
        }
        pid_ = -1;
        return status;
    }

    static std::string describeExit(int status) {
        if (status < 0) {
            return "";
        }
        if (WIFEXITED(status)) {
            return fmt::format(" (exit status {})", WEXITSTATUS(status));
        }
        if (WIFSIGNALED(status)) {
            return fmt::format(" (killed by signal {})", WTERMSIG(status));
        }
        return "";
    }

    // Last ~2 KB of ffmpeg's stderr, formatted for inclusion in an error message.
    std::string logTail() const {
        std::ifstream in(logPath_, std::ios::binary);
        if (!in) {
            return "";
        }
        std::string all((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        while (!all.empty() && (all.back() == '\n' || all.back() == '\r' || all.back() == ' ')) {
            all.pop_back();
        }
        if (all.empty()) {
            return "";
        }
        constexpr std::size_t kTail = 2048;
        if (all.size() > kTail) {
            all = "..." + all.substr(all.size() - kTail);
        }
        return "\n--- ffmpeg stderr ---\n" + all;
    }

    // Kills the encoder and removes its partial output (the destructor without finish()).
    void abandon() {
        closeStdin();
        if (pid_ > 0) {
            ::kill(pid_, SIGKILL);
            reap();
        }
        std::error_code ec;
        std::filesystem::remove(logPath_, ec);
        std::filesystem::remove(path_, ec);
    }

    std::filesystem::path path_;
    VideoSettings settings_;
    std::filesystem::path logPath_;
    std::size_t frameBytes_ = 0;
    std::size_t frames_ = 0;
    int stdinFd_ = -1;
    pid_t pid_ = -1;
    bool finished_ = false;
    std::optional<Error> error_;
};

Result<std::unique_ptr<VideoWriter>> openFfmpegWriter(const std::filesystem::path& file,
                                                      const VideoSettings& settings,
                                                      const std::filesystem::path& exe) {
    return FfmpegWriter::open(file, settings, exe);
}

#else

Result<std::unique_ptr<VideoWriter>> openFfmpegWriter(const std::filesystem::path& file,
                                                      const VideoSettings& settings,
                                                      const std::filesystem::path& exe) {
    return fail("video: the ffmpeg backend is not implemented on this platform ('{}' for '{}', codec {})",
                exe.string(), file.string(), settings.codec);
}

#endif // _WIN32

} // namespace

std::filesystem::path findFfmpeg(const std::filesystem::path& hint) {
    std::vector<std::filesystem::path> candidates;
    if (!hint.empty()) {
        candidates.push_back(ffmpegCandidate(hint));
    }
    if (const char* env = std::getenv("AVGEN_FFMPEG"); env != nullptr && *env != '\0') {
        candidates.push_back(ffmpegCandidate(env));
    }
    if (const char* pathEnv = std::getenv("PATH"); pathEnv != nullptr && *pathEnv != '\0') {
        std::string_view rest(pathEnv);
        while (!rest.empty()) {
            const auto sep = rest.find(kPathSeparator);
            const auto dir = rest.substr(0, sep);
            if (!dir.empty()) {
                candidates.push_back(std::filesystem::path(dir) / kFfmpegExe);
            }
            if (sep == std::string_view::npos) {
                break;
            }
            rest.remove_prefix(sep + 1);
        }
    }
    candidates.emplace_back("/opt/homebrew/bin/ffmpeg");
    candidates.emplace_back("/usr/local/bin/ffmpeg");
    for (const auto& candidate : candidates) {
        if (isExecutableFile(candidate)) {
            return candidate;
        }
    }
    return {};
}

std::string describeVideoBackends() {
    std::string out = "native: ";
    if (hasNativeVideo()) {
        out += fmt::format("{}", fmt::join(nativeCodecs(), ", "));
    } else {
        out += "none";
    }
    const auto ffmpeg = findFfmpeg();
    out += "; ffmpeg: ";
    out += ffmpeg.empty() ? std::string("not found (set AVGEN_FFMPEG or install ffmpeg on PATH)")
                          : ffmpeg.string();
    return out;
}

Result<std::unique_ptr<VideoWriter>> openVideoWriter(const std::filesystem::path& file,
                                                     const VideoSettings& settings) {
    if (file.empty()) {
        return fail("video: no output file given");
    }
    if (settings.width == 0 || settings.height == 0) {
        return fail("video: width and height must be positive, got {}x{}", settings.width, settings.height);
    }
    if (!(settings.fps > 0.0) || !std::isfinite(settings.fps)) {
        return fail("video: frame rate must be positive, got {}", settings.fps);
    }
    if (settings.codec.empty()) {
        return fail("video: no codec given ({})", describeVideoBackends());
    }
    if (!settings.audio.empty()) {
        std::error_code ec;
        if (!std::filesystem::is_regular_file(settings.audio, ec)) {
            return fail("video: audio file not found: '{}'", settings.audio.string());
        }
    }

    const bool nativeCanEncode = hasNativeVideo() && isNativeCodec(settings.codec);
    if (settings.backend == "native") {
        if (!hasNativeVideo()) {
            return fail("video: the native backend is not available on this platform ({})",
                        describeVideoBackends());
        }
        if (!nativeCanEncode) {
            return fail("video: '{}' is not a native codec; native codecs: {}", settings.codec,
                        fmt::join(nativeCodecs(), ", "));
        }
        return detail::openNativeVideoWriter(file, settings);
    }
    if (settings.backend == "ffmpeg") {
        const auto exe = findFfmpeg(settings.ffmpegPath);
        if (exe.empty()) {
            return fail("video: ffmpeg not found (set VideoSettings::ffmpegPath or AVGEN_FFMPEG, or install "
                        "ffmpeg on PATH){}",
                        settings.ffmpegPath.empty()
                            ? std::string()
                            : fmt::format("; hint '{}' is not an executable", settings.ffmpegPath.string()));
        }
        return openFfmpegWriter(file, settings, exe);
    }
    if (settings.backend == "auto") {
        if (nativeCanEncode) {
            return detail::openNativeVideoWriter(file, settings);
        }
        if (const auto exe = findFfmpeg(settings.ffmpegPath); !exe.empty()) {
            return openFfmpegWriter(file, settings, exe);
        }
        return fail("video: no backend can encode '{}' ({})", settings.codec, describeVideoBackends());
    }
    return fail("video: unknown backend '{}' (expected \"auto\", \"native\" or \"ffmpeg\")",
                settings.backend);
}

} // namespace avgen::assets
