#include "external/vmaf.hpp"

#include "assets/video_writer.hpp"

#include <fmt/format.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <limits>

namespace avgen::quality {
namespace {

// Single-quote for /bin/sh. Paths here come from a directory walk and from the command line, so
// they are not hostile, but a space in a path is ordinary and an unquoted one silently truncates
// the argument.
std::string shellQuote(const std::string& value) {
    std::string out = "'";
    for (const char c : value) {
        if (c == '\'') {
            out += "'\\''";
        } else {
            out += c;
        }
    }
    out += "'";
    return out;
}

// Runs a command, capturing stdout+stderr. Returns the exit status.
int runCapturing(const std::string& command, std::string& output) {
    output.clear();
    std::FILE* pipe = ::popen((command + " 2>&1").c_str(), "r");
    if (pipe == nullptr) {
        return -1;
    }
    std::array<char, 4096> buffer{};
    while (std::fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr) {
        output += buffer.data();
    }
    const int status = ::pclose(pipe);
    return status == -1 ? -1 : (status >> 8) & 0xFF;
}

VmafSeries summarise(std::vector<double> values, std::string label) {
    VmafSeries series;
    series.perFrame = values;
    std::vector<double> finite;
    finite.reserve(values.size());
    for (const double v : values) {
        if (std::isfinite(v)) {
            finite.push_back(v);
        }
    }
    if (finite.empty()) {
        series.available = false;
        series.reason = fmt::format(
            "{} reported no finite value on any frame -- libvmaf writes null where the metric is "
            "infinite, which is what an identical pair produces",
            label);
        return series;
    }
    series.available = true;
    double total = 0.0;
    for (const double v : finite) {
        total += v;
    }
    series.mean = total / static_cast<double>(finite.size());
    std::sort(finite.begin(), finite.end());
    series.min = finite.front();
    series.max = finite.back();
    const auto index =
        static_cast<std::size_t>(0.05 * static_cast<double>(finite.size() - 1) + 0.5);
    series.p5 = finite[std::min(index, finite.size() - 1)];
    return series;
}

// Links `frames` into `directory` as 000000.png, 000001.png, ... A symlink rather than a copy
// because a 300-frame 1080p pair is a gigabyte, and the link is read-only from ffmpeg's side.
Result<void> stage(const std::vector<std::filesystem::path>& frames,
                   const std::filesystem::path& directory) {
    std::error_code ec;
    std::filesystem::remove_all(directory, ec);
    std::filesystem::create_directories(directory, ec);
    if (ec) {
        return fail("quality: could not create '{}': {}", directory.string(), ec.message());
    }
    for (std::size_t i = 0; i < frames.size(); ++i) {
        const std::filesystem::path link = directory / fmt::format("{:06d}.png", i);
        std::filesystem::create_symlink(std::filesystem::absolute(frames[i]), link, ec);
        if (ec) {
            // A filesystem that refuses symlinks is not an error worth failing over.
            ec.clear();
            std::filesystem::copy_file(frames[i], link,
                                       std::filesystem::copy_options::overwrite_existing, ec);
            if (ec) {
                return fail("quality: could not stage '{}': {}", frames[i].string(), ec.message());
            }
        }
    }
    return {};
}

double readMetric(const nlohmann::json& metrics, const char* key) {
    if (!metrics.contains(key) || metrics[key].is_null()) {
        return std::numeric_limits<double>::quiet_NaN();
    }
    return metrics[key].get<double>();
}

} // namespace

ExternalTool findVmafTool() {
    ExternalTool tool;
    const std::filesystem::path ffmpeg = assets::findFfmpeg();
    if (ffmpeg.empty()) {
        tool.reason = "ffmpeg not found; set AVGEN_FFMPEG or install ffmpeg on PATH";
        return tool;
    }
    tool.path = ffmpeg;
    std::string output;
    if (runCapturing(shellQuote(ffmpeg.string()) + " -hide_banner -version", output) != 0) {
        tool.reason = fmt::format("'{}' did not run", ffmpeg.string());
        return tool;
    }
    const auto newline = output.find('\n');
    tool.version = output.substr(0, newline == std::string::npos ? output.size() : newline);

    // The probe that matters: an ffmpeg without libvmaf exists, is on PATH, runs, and fails at
    // filter-graph time. Asking the filter list is the difference between "unavailable, here is
    // why" and a wall of ffmpeg diagnostics in a quality report.
    if (runCapturing(shellQuote(ffmpeg.string()) + " -hide_banner -filters", output) != 0 ||
        output.find("libvmaf") == std::string::npos) {
        tool.reason = fmt::format("'{}' has no libvmaf filter (built without --enable-libvmaf)",
                                  ffmpeg.string());
        return tool;
    }
    tool.available = true;
    return tool;
}

Result<VmafResult> runVmaf(const ExternalTool& tool, const VmafRequest& request) {
    VmafResult result;
    result.model = request.model;
    if (!tool.available) {
        result.reason = tool.reason.empty() ? "ffmpeg with libvmaf is not available" : tool.reason;
        return result;
    }
    result.ffmpegVersion = tool.version;
    if (request.candidateFrames.empty() || request.referenceFrames.empty()) {
        return fail("quality: runVmaf needs both a candidate and a reference sequence");
    }
    if (request.candidateFrames.size() != request.referenceFrames.size()) {
        result.reason = fmt::format(
            "candidate has {} frames and reference has {}; libvmaf pairs frames by position and "
            "would silently compare the wrong ones",
            request.candidateFrames.size(), request.referenceFrames.size());
        return result;
    }
    if (request.workDirectory.empty()) {
        return fail("quality: runVmaf needs a work directory");
    }

    const std::filesystem::path candidateDir = request.workDirectory / "vmaf-candidate";
    const std::filesystem::path referenceDir = request.workDirectory / "vmaf-reference";
    const std::filesystem::path logPath = request.workDirectory / "vmaf.json";
    if (auto staged = stage(request.candidateFrames, candidateDir); !staged) {
        return std::unexpected(staged.error());
    }
    if (auto staged = stage(request.referenceFrames, referenceDir); !staged) {
        return std::unexpected(staged.error());
    }
    std::error_code ec;
    std::filesystem::remove(logPath, ec);

    std::string features;
    if (request.wantPsnrHvs) {
        features += "name=psnr_hvs";
    }
    if (request.wantCambi) {
        if (!features.empty()) {
            features += "|";
        }
        features += "name=cambi";
    }

    // yuv420p because that is the domain every VMAF model was trained in. It subsamples chroma
    // before the measurement, which is a limitation the report carries: a chroma-only defect is
    // half-invisible to every number produced here.
    std::string filter = "[0:v]format=yuv420p[dist];[1:v]format=yuv420p[ref];[dist][ref]libvmaf=";
    filter += "log_path=" + logPath.string() + ":log_fmt=json";
    filter += ":model=" + request.model;
    if (!features.empty()) {
        filter += ":feature=" + features;
    }

    const std::string command = fmt::format(
        "{} -nostdin -hide_banner -loglevel error -f image2 -framerate {} -i {} "
        "-f image2 -framerate {} -i {} -lavfi {} -f null -",
        shellQuote(tool.path.string()), request.fps,
        shellQuote((candidateDir / "%06d.png").string()), request.fps,
        shellQuote((referenceDir / "%06d.png").string()), shellQuote(filter));
    result.commandLine = command;

    std::string output;
    const int status = runCapturing(command, output);
    if (status != 0 || !std::filesystem::exists(logPath, ec)) {
        result.reason = fmt::format("ffmpeg exited {}: {}", status,
                                    output.substr(0, std::min<std::size_t>(output.size(), 600)));
        return result;
    }

    std::ifstream stream(logPath);
    nlohmann::json log;
    try {
        stream >> log;
    } catch (const std::exception& e) {
        result.reason = fmt::format("libvmaf's JSON log did not parse: {}", e.what());
        return result;
    }
    if (!log.contains("frames") || !log["frames"].is_array()) {
        result.reason = "libvmaf's JSON log has no frames array";
        return result;
    }
    std::vector<double> vmaf;
    std::vector<double> psnrHvs;
    std::vector<double> cambi;
    for (const auto& frame : log["frames"]) {
        if (!frame.contains("metrics")) {
            continue;
        }
        const auto& metrics = frame["metrics"];
        vmaf.push_back(readMetric(metrics, "vmaf"));
        if (request.wantPsnrHvs) {
            psnrHvs.push_back(readMetric(metrics, "psnr_hvs"));
        }
        if (request.wantCambi) {
            cambi.push_back(readMetric(metrics, "cambi"));
        }
    }
    result.vmaf = summarise(std::move(vmaf), "vmaf");
    if (request.wantPsnrHvs) {
        result.psnrHvs = summarise(std::move(psnrHvs), "psnr_hvs");
    }
    if (request.wantCambi) {
        result.cambi = summarise(std::move(cambi), "cambi");
    }
    result.available = result.vmaf.available;
    if (!result.available) {
        result.reason = result.vmaf.reason;
    }
    if (!request.keepLog) {
        std::filesystem::remove(logPath, ec);
    }
    std::filesystem::remove_all(candidateDir, ec);
    std::filesystem::remove_all(referenceDir, ec);
    return result;
}

} // namespace avgen::quality
