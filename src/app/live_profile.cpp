#include "app/live_profile.hpp"

#include <fmt/format.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <numeric>

#if defined(__APPLE__)
#include <sys/sysctl.h>
#endif

namespace avgen::app {

using nlohmann::json;

// ---- the command line ----------------------------------------------------------------------------------------------

bool hasLiveProfileFlag(int argc, char** argv) {
    for (int i = 1; i < argc; ++i) {
        if (std::string_view(argv[i]) == "--live-profile") {
            return true;
        }
    }
    return false;
}

std::string liveProfileUsage() {
    return "  --live-profile      profile the scene for live performance (ADR-1090) and exit. Its own flags:\n"
           "                        --mode headless|live   headless: the fixed-step clock, fast and deterministic;\n"
           "                                               live: the real editor loop with present, Fifo and the\n"
           "                                               projection output (default headless)\n"
           "                        --target-fps <n>       the live target (default 60)\n"
           "                        --size WxH             the OUTPUT in pixels (default 1920x1080)\n"
           "                        --start <s>            where in the piece measurement starts (default 0)\n"
           "                        --warmup <s>           warm-up cap, seconds (default 3; 5 with --deep)\n"
           "                        --measure <s>          measured seconds (default 5; 20 with --deep)\n"
           "                        --deep                 the deep mode: longer, every diagnostic\n"
           "                        --quality <q>          auto | ultra..emergency | quality | balanced |\n"
           "                                               performance (default auto)\n"
           "                        --camera <name>        a named camera\n"
           "                        --no-audio             measure without the audio file playing\n"
           "                        --midi <filter>        a MIDI input to open while measuring\n"
           "                        --capture <png>        the last measured frame\n"
           "                        --json <file>          the record (avgen.liveprofile/1)\n"
           "                        --text / --no-text     the human report on stdout (default on)\n"
           "                        --verify-candidates <n> measure the top n candidates' savings (A/B)\n"
           "                        --no-prewarm           SDF variants compiled at first use on the main thread (the\n"
           "                                               behaviour before ADR-1102), for before/after measurements\n"
           "                      Phase 5, headless only (ADR-1108..1112):\n"
           "                        --compare <levers|project> ORIGINAL vs OPTIMIZED: the levers (comma separated A/B\n"
           "                                               names) applied as ceilings, or 'project' (the project's\n"
           "                                               own ceilings vs none); timed (A/B) and imaged (the Quality\n"
           "                                               Lab's pixel, SSIM, edge, luminance, temporal differences)\n"
           "                        --optimize             search lever combinations for the least measured visual\n"
           "                                               change that reaches the target (budget x margin)\n"
           "                        --hero-policy protect|strict  protect (default): no lever that degrades a hero;\n"
           "                                               strict: no image-wide lever either\n"
           "                        --optimize-risk low|medium|high  the riskiest lever tried (default medium)\n"
           "                        --optimize-margin <f>  the target as a fraction of the budget (default 0.9)\n"
           "                        --optimize-candidates <n> at most n single levers measured (default 8)\n"
           "                        --ab-frames <n>        consecutive frames captured per arm (default 12)\n"
           "                        --ab-dir <dir>         keep the ORIGINAL/OPTIMIZED frames there\n"
           "                        --ab-critic            also ask the Creative Critic (optional; --critic or\n"
           "                                               AVGEN_CRITIC names it)\n";
}

LiveProfileArgs parseLiveProfileArgs(const std::vector<std::string>& argv) {
    LiveProfileArgs out;
    LiveProfileOptions& o = out.options;
    bool warmupGiven = false;
    bool measureGiven = false;
    if (!argv.empty()) {
        out.rest.push_back(argv.front());
    }
    const auto number = [&](std::size_t& i, const char* flag, double& value) -> bool {
        if (i + 1 >= argv.size()) {
            out.error = fmt::format("{} requires a value", flag);
            return false;
        }
        char* end = nullptr;
        const std::string& v = argv[i + 1];
        value = std::strtod(v.c_str(), &end);
        if (end == v.c_str() || *end != '\0' || !std::isfinite(value)) {
            out.error = fmt::format("{} expects a number, got '{}'", flag, v);
            return false;
        }
        ++i;
        return true;
    };
    const auto text = [&](std::size_t& i, const char* flag, std::string& value) -> bool {
        if (i + 1 >= argv.size()) {
            out.error = fmt::format("{} requires a value", flag);
            return false;
        }
        value = argv[++i];
        return true;
    };
    for (std::size_t i = 1; i < argv.size(); ++i) {
        const std::string& a = argv[i];
        if (a == "--live-profile") {
            o.enabled = true;
        } else if (a == "--mode") {
            std::string m;
            if (!text(i, "--mode", m)) return out;
            if (m == "headless") {
                o.mode = LiveProfileMode::Headless;
            } else if (m == "live") {
                o.mode = LiveProfileMode::Live;
            } else {
                out.error = fmt::format("--mode expects headless or live, got '{}'", m);
                return out;
            }
        } else if (a == "--target-fps") {
            if (!number(i, "--target-fps", o.targetFps)) return out;
            if (o.targetFps < 24.0 || o.targetFps > 240.0) {
                out.error = fmt::format("--target-fps must be between 24 and 240, got {}", o.targetFps);
                return out;
            }
        } else if (a == "--size") {
            std::string s;
            if (!text(i, "--size", s)) return out;
            unsigned w = 0;
            unsigned h = 0;
            if (std::sscanf(s.c_str(), "%ux%u", &w, &h) != 2 || w < 16 || h < 16) {
                out.error = fmt::format("--size expects WxH in pixels, got '{}'", s);
                return out;
            }
            o.outputWidth = w;
            o.outputHeight = h;
        } else if (a == "--start") {
            if (!number(i, "--start", o.startSeconds)) return out;
            if (o.startSeconds < 0.0) {
                out.error = "--start must not be negative";
                return out;
            }
        } else if (a == "--warmup") {
            if (!number(i, "--warmup", o.warmupSeconds)) return out;
            warmupGiven = true;
        } else if (a == "--measure") {
            if (!number(i, "--measure", o.measureSeconds)) return out;
            if (o.measureSeconds <= 0.0) {
                out.error = "--measure must be positive";
                return out;
            }
            measureGiven = true;
        } else if (a == "--deep") {
            o.deep = true;
        } else if (a == "--quality") {
            if (!text(i, "--quality", o.quality)) return out;
        } else if (a == "--camera") {
            if (!text(i, "--camera", o.camera)) return out;
        } else if (a == "--no-audio") {
            o.audio = false;
        } else if (a == "--midi") {
            if (!text(i, "--midi", o.midi)) return out;
        } else if (a == "--capture") {
            std::string p;
            if (!text(i, "--capture", p)) return out;
            o.capture = p;
        } else if (a == "--json") {
            std::string p;
            if (!text(i, "--json", p)) return out;
            o.json = p;
        } else if (a == "--text") {
            o.text = true;
        } else if (a == "--no-text") {
            o.text = false;
        } else if (a == "--no-prewarm") {
            o.prewarm = false;
        } else if (a == "--compare") {
            if (!text(i, "--compare", o.compare)) return out;
        } else if (a == "--optimize") {
            o.optimize = true;
        } else if (a == "--hero-policy") {
            if (!text(i, "--hero-policy", o.heroPolicy)) return out;
            if (!heroPolicyFromToken(o.heroPolicy)) {
                out.error = fmt::format("--hero-policy expects protect or strict, got '{}'", o.heroPolicy);
                return out;
            }
        } else if (a == "--optimize-risk") {
            if (!text(i, "--optimize-risk", o.optimizeRisk)) return out;
            if (riskRank(o.optimizeRisk) > 2) {
                out.error = fmt::format("--optimize-risk expects low, medium or high, got '{}'", o.optimizeRisk);
                return out;
            }
        } else if (a == "--optimize-margin") {
            if (!number(i, "--optimize-margin", o.optimizeMargin)) return out;
            if (o.optimizeMargin < 0.5 || o.optimizeMargin > 1.0) {
                out.error = "--optimize-margin must be between 0.5 and 1";
                return out;
            }
        } else if (a == "--optimize-candidates") {
            double n = 0.0;
            if (!number(i, "--optimize-candidates", n)) return out;
            o.optimizeCandidates = std::clamp(static_cast<int>(n), 1, 16);
        } else if (a == "--ab-frames") {
            double n = 0.0;
            if (!number(i, "--ab-frames", n)) return out;
            o.abFrames = std::clamp(static_cast<int>(n), 2, 120);
        } else if (a == "--ab-dir") {
            std::string p;
            if (!text(i, "--ab-dir", p)) return out;
            o.abDir = p;
        } else if (a == "--ab-critic") {
            o.abCritic = true;
        } else if (a == "--verify-candidates") {
            double n = 0.0;
            if (!number(i, "--verify-candidates", n)) return out;
            o.verifyCandidates = std::clamp(static_cast<int>(n), 0, 8);
        } else {
            out.rest.push_back(a);
        }
    }
    if (o.deep) {
        if (!warmupGiven) o.warmupSeconds = 5.0;
        if (!measureGiven) o.measureSeconds = 20.0;
    }
    if (o.warmupSeconds < 0.0) {
        out.error = "--warmup must not be negative";
    }
    if ((o.optimize || !o.compare.empty()) && o.mode == LiveProfileMode::Live) {
        // ORIGINAL and OPTIMIZED must be the same moments of the piece, which only the fixed-step clock gives.
        out.error = "--compare and --optimize run headless only (the A/B images need the fixed-step clock)";
    }
    if (o.abCritic && o.compare.empty() && !o.optimize) {
        out.error = "--ab-critic needs --compare or --optimize";
    }
    return out;
}

// ---- steady state --------------------------------------------------------------------------------------------------

SteadyStateDetector::SteadyStateDetector(int window, double tolerance, int confirmations, int minFrames)
    : window_(std::max(window, 3)), tolerance_(tolerance), confirmations_(std::max(confirmations, 1)),
      minFrames_(std::max(minFrames, window)) {}

bool SteadyStateDetector::note(double ms) {
    samples_.push_back(ms);
    if (static_cast<int>(samples_.size()) < window_) {
        return steady_;
    }
    std::vector<double> last(samples_.end() - window_, samples_.end());
    std::nth_element(last.begin(), last.begin() + static_cast<std::ptrdiff_t>(last.size() / 2), last.end());
    lastMedian_ = last[last.size() / 2];
    // Compared once per window, not per frame: a median taken one frame later is almost the same set of frames, and
    // "it did not move" between two nearly identical sets says nothing.
    if (static_cast<int>(samples_.size()) % window_ == 0) {
        if (previousMedian_ > 0.0) {
            const double change = std::abs(lastMedian_ - previousMedian_) / previousMedian_;
            streak_ = change <= tolerance_ ? streak_ + 1 : 0;
        }
        previousMedian_ = lastMedian_;
        if (streak_ >= confirmations_ && static_cast<int>(samples_.size()) >= minFrames_) {
            steady_ = true;
        }
    }
    return steady_;
}

// ---- budget --------------------------------------------------------------------------------------------------------

BudgetStats budgetStats(const std::vector<double>& frameMs, double budgetMs, double refreshMs) {
    BudgetStats b;
    b.budgetMs = budgetMs;
    b.frames = frameMs.size();
    b.refreshMs = refreshMs;
    for (const double ms : frameMs) {
        if (ms > budgetMs) {
            ++b.overBudget;
        }
        if (refreshMs > 0.0) {
            // Vsyncs the frame occupied, against the vsyncs the target allows. Half a refresh of slack either side,
            // because a Fifo interval of 16.4 or 17.0 ms at 120 Hz is two vsyncs, not "over budget by 0.3 ms".
            const double allowed = std::max(1.0, std::round(budgetMs / refreshMs));
            const double took = std::max(1.0, std::round(ms / refreshMs));
            if (took > allowed) {
                ++b.deadlineMisses;
                b.vsyncsMissed += static_cast<std::size_t>(took - allowed);
            }
        }
    }
    if (b.frames > 0) {
        b.percentUnder = 100.0 * static_cast<double>(b.frames - b.overBudget) / static_cast<double>(b.frames);
        b.percentMissed = 100.0 * static_cast<double>(b.deadlineMisses) / static_cast<double>(b.frames);
    }
    return b;
}

std::vector<WorstFrame> worstFrames(const std::vector<LiveProfileFrame>& frames, std::size_t n) {
    std::vector<std::size_t> order(frames.size());
    std::iota(order.begin(), order.end(), std::size_t{0});
    std::stable_sort(order.begin(), order.end(),
                     [&](std::size_t a, std::size_t b) { return frames[a].frameMs > frames[b].frameMs; });
    std::vector<WorstFrame> out;
    for (std::size_t k = 0; k < std::min(n, order.size()); ++k) {
        const LiveProfileFrame& f = frames[order[k]];
        out.push_back({order[k], f.atSeconds, f.frameMs, f.gpuMs, f.cpuWorkMs, f.waitMs});
    }
    return out;
}

CriticalPath criticalPath(double frameMs, double gpuMs, double cpuWorkMs, double waitMs, double budgetMs, bool live) {
    CriticalPath c;
    c.frameMs = frameMs;
    c.gpuMs = gpuMs;
    c.cpuWorkMs = cpuWorkMs;
    c.waitMs = waitMs;
    if (frameMs <= 0.0) {
        c.verdict = "unknown";
        c.explanation = "no frames were measured";
        return c;
    }
    const double gpuShare = gpuMs > 0.0 ? gpuMs / frameMs : 0.0;
    const double cpuShare = cpuWorkMs / frameMs;
    if (!live) {
        // Headless serialises CPU and GPU (each frame waits for the queue), so the frame is roughly their sum and the
        // larger of the two is what a pipelined live frame would be held to.
        c.verdict = gpuMs >= cpuWorkMs ? "GPU" : "CPU";
        c.explanation = fmt::format("headless: GPU {:.2f} ms against CPU work {:.2f} ms; a pipelined live frame is "
                                    "held to the larger (headless has no present, so sync is not measured)",
                                    gpuMs, cpuWorkMs);
        return c;
    }
    if (gpuShare >= 0.85) {
        c.verdict = "GPU";
        c.explanation = fmt::format("the GPU span is {:.0f}% of the frame interval", 100.0 * gpuShare);
    } else if (cpuShare >= 0.85) {
        c.verdict = "CPU";
        c.explanation = fmt::format("main-thread work is {:.0f}% of the frame interval", 100.0 * cpuShare);
    } else if (frameMs > budgetMs) {
        c.verdict = "sync/present";
        c.explanation = fmt::format("over budget with the GPU at {:.0f}% and the CPU at {:.0f}% of the interval: the "
                                    "rest ({:.2f} ms) is waiting on the swapchain and the outputs",
                                    100.0 * gpuShare, 100.0 * cpuShare, waitMs);
    } else {
        c.verdict = gpuMs >= cpuWorkMs ? "GPU" : "CPU";
        c.explanation = fmt::format("within budget; the larger share is the {} ({:.0f}% GPU, {:.0f}% CPU), and the "
                                    "rest is the frame pacing to the display",
                                    c.verdict, 100.0 * gpuShare, 100.0 * cpuShare);
    }
    return c;
}

// ---- GPU categories ------------------------------------------------------------------------------------------------

const std::vector<std::string_view>& gpuCategoryOrder() {
    static const std::vector<std::string_view> kOrder{
        "geometry/opaque", "shadows",          "lighting",  "SDF",
        "particles/simulation", "volumetrics", "post: bloom", "post: depth of field",
        "post: motion blur", "post: other",    "temporal",  "composite/tonemap",
        "other"};
    return kOrder;
}

const std::vector<GpuCategoryRule>& gpuCategoryTable() {
    // Every label gpu::FrameTimeline carries (01-research.md, Phase 1), each in one category. Exact names first; a
    // pattern ending in '*' is a prefix, tried after every exact name, longest first. Anything else is "other": an
    // unknown label is reported, never dropped and never guessed into a neighbour.
    static const std::vector<GpuCategoryRule> kTable{
        {"scene", "geometry/opaque"},
        {"depth", "geometry/opaque"},
        {"cull", "geometry/opaque"},
        {"effectors", "geometry/opaque"},
        {"water", "geometry/opaque"},
        {"shadow", "shadows"},
        {"shadowmask", "shadows"},
        {"clusters", "lighting"},
        {"ao", "lighting"},
        {"sdf", "SDF"},
        {"particles", "particles/simulation"},
        {"sim", "particles/simulation"},
        {"volume.march", "volumetrics"},
        {"volume.composite", "volumetrics"},
        {"fogsky", "volumetrics"},
        {"post/bloom", "post: bloom"},
        {"post/halation", "post: bloom"},
        {"post/dof", "post: depth of field"},
        {"post/motionblur", "post: motion blur"},
        {"temporal", "temporal"},
        {"tonemap", "composite/tonemap"},
        {"composition", "composite/tonemap"},
        {"shaderlayer", "composite/tonemap"},
        {"volume.*", "volumetrics"},
        {"distort.*", "post: other"},
        {"post/*", "post: other"},
    };
    return kTable;
}

std::string_view gpuCategoryOf(std::string_view label) {
    const auto& table = gpuCategoryTable();
    for (const GpuCategoryRule& r : table) {
        if (!r.pattern.ends_with('*') && r.pattern == label) {
            return r.category;
        }
    }
    std::string_view best;
    std::size_t bestLength = 0;
    for (const GpuCategoryRule& r : table) {
        if (r.pattern.ends_with('*')) {
            const std::string_view prefix = r.pattern.substr(0, r.pattern.size() - 1);
            if (label.starts_with(prefix) && prefix.size() > bestLength) {
                best = r.category;
                bestLength = prefix.size();
            }
        }
    }
    return bestLength > 0 ? best : std::string_view("other");
}

std::vector<GpuCategory> groupGpuCategories(const std::vector<gpu::TimelineInterval>& passMedians) {
    std::vector<GpuCategory> out;
    for (const std::string_view name : gpuCategoryOrder()) {
        out.push_back({std::string(name), 0.0, {}});
    }
    for (const auto& p : passMedians) {
        const std::string_view cat = gpuCategoryOf(p.label);
        auto it = std::find_if(out.begin(), out.end(), [&](const GpuCategory& c) { return c.name == cat; });
        it->medianMs += p.ms;
        it->labels.push_back(p);
    }
    std::erase_if(out, [](const GpuCategory& c) { return c.labels.empty(); });
    return out;
}

// ---- CPU categories ------------------------------------------------------------------------------------------------

namespace {

double medianOf(std::vector<double> v) {
    if (v.empty()) {
        return -1.0;
    }
    std::nth_element(v.begin(), v.begin() + static_cast<std::ptrdiff_t>(v.size() / 2), v.end());
    return v[v.size() / 2];
}

template <typename F>
double medianField(const std::vector<LiveProfileFrame>& frames, F pick) {
    std::vector<double> v;
    v.reserve(frames.size());
    for (const auto& f : frames) {
        v.push_back(pick(f));
    }
    return medianOf(std::move(v));
}

double gpuShareOfPixels(std::string_view category) {
    // The resolution-dependent share of each category's cost, from the measured fits of ms = fixed + perMpx * Mpx over
    // six internal sizes (docs/live-quality/evidence-live-projection-2026-10-02.md §D, M2 Max). Where a category was
    // not fitted the value is a judgement and the candidate's basis says the range is wide.
    if (category == "SDF") return 1.0;
    if (category == "volumetrics") return 0.85;
    if (category == "post: motion blur" || category == "post: depth of field") return 1.0;
    if (category == "post: bloom") return 0.7;
    if (category == "post: other" || category == "composite/tonemap" || category == "temporal") return 0.9;
    if (category == "lighting") return 0.9;
    if (category == "geometry/opaque") return 0.6;
    if (category == "particles/simulation") return 0.3;
    if (category == "shadows") return 0.0;
    return 0.5;
}

double categoryMs(const std::vector<GpuCategory>& gpu, std::string_view name) {
    for (const auto& c : gpu) {
        if (c.name == name) return c.medianMs;
    }
    return 0.0;
}

double passMs(const std::vector<gpu::TimelineInterval>& passes, std::string_view label) {
    double ms = 0.0;
    for (const auto& p : passes) {
        if (p.label == label) ms += p.ms;
    }
    return ms;
}

} // namespace

std::vector<CpuCategory> cpuCategories(const std::vector<LiveProfileFrame>& frames, bool live) {
    std::vector<CpuCategory> out;
    const auto add = [&](std::string name, double ms, std::string basis, std::string covers) {
        out.push_back({std::move(name), ms, std::move(basis), std::move(covers)});
    };
    const auto m = [&](auto pick) { return medianField(frames, pick); };
    add("audio analysis", m([](const auto& f) { return f.analysisCatchupMs; }), "coarse",
        "the main thread catching the offline analysis up to the playhead; the live analysis thread is not timed");
    add("MIDI / OSC / control", m([](const auto& f) { return f.updControlMs; }), "measured",
        "the control hub's drain of MIDI, OSC and the bindings (engine.update: control)");
    add("signal bus", m([](const auto& f) { return f.updSignalsMs; }), "measured", "engine.update: signals");
    add("modulation and timeline", m([](const auto& f) { return f.updModulationMs; }), "coarse",
        "modulators, routes and timeline automation together (engine.update: modulation)");
    add("scene update (animation, entities, procedural, physics)", m([](const auto& f) { return f.updControllerMs; }),
        "coarse",
        "the scene controller's update: animation, entity behaviour, procedural regeneration and any flatten are one "
        "phase; they are not separately measurable");
    add("engine other", m([](const auto& f) { return f.updOtherMs; }), "measured", "engine.update: the remainder");
    add("resource uploads", m([](const auto& f) { return f.meshUploadMs + f.textureUploadMs + f.environmentMs; }),
        "measured", "mesh and texture uploads and environment builds");
    add("render submission: objects and culling", m([](const auto& f) { return f.render.objectsMs; }), "coarse",
        "entity traversal, object uniforms and shadow-caster selection (culling is inside it)");
    add("render submission: procedural", m([](const auto& f) { return f.render.proceduralMs; }), "measured",
        "procedural instance rebuilds, uploads and the cull encode");
    add("render submission: lights", m([](const auto& f) { return f.render.lightsMs; }), "measured",
        "frame uniforms, light packing, cascade fitting, the froxel encode");
    add("render submission: SDF", m([](const auto& f) { return f.render.sdfMs; }), "measured",
        "SDF node packing and mesh uploads");
    add("render submission: particles (encode)", m([](const auto& f) { return f.render.particlesMs; }), "measured",
        "the particle update encode; the simulation itself runs on the GPU");
    add("render submission: fields and materials", m([](const auto& f) { return f.render.fieldsMs; }), "measured",
        "fields, material programs, spline tables");
    add("render submission: pass encoding",
        m([](const auto& f) {
            const auto& r = f.render;
            return r.shadowEncodeMs + r.backgroundEncodeMs + r.depthEncodeMs + r.sceneEncodeMs + r.volumeEncodeMs +
                   r.postEncodeMs + r.tonemapEncodeMs + r.simulationMs;
        }),
        "measured", "recording every pass's commands");
    if (live) {
        add("UI build and record", m([](const auto& f) { return f.uiMs; }), "measured", "ui.build + imgui.record");
        add("synchronization (waits)", m([](const auto& f) { return f.waitMs; }), "measured",
            "the swapchain acquire and present waits: time blocked, not work");
        add("outputs (projection, sharing)", m([](const auto& f) { return f.outputsMs; }), "measured",
            "the projection window's present and any Syphon/NDI publish");
    } else {
        add("synchronization (queue wait)", m([](const auto& f) { return f.waitMs; }), "measured",
            "headless blocks on the queue each frame: GPU time seen from the CPU, not CPU work");
    }
    add("physics / simulation (separately)", -1.0, "not separately measurable",
        "inside the scene update above; no phase times it alone");
    return out;
}

// ---- candidates (1h) -----------------------------------------------------------------------------------------------

std::vector<LiveProfileCandidate> optimizationCandidates(const CandidateInputs& in) {
    std::vector<LiveProfileCandidate> out;
    const auto add = [&](LiveProfileCandidate c) {
        if (c.estimatedHighMs >= 0.1 && c.costMs > 0.0) {
            c.estimatedLowMs = std::max(0.0, std::min(c.estimatedLowMs, c.estimatedHighMs));
            c.heroEffect = heroEffectName(heroEffectOfLever(c.lever)); // ADR-1109
            out.push_back(std::move(c));
        }
    };
    const auto& gpu = in.gpu;
    // Volumetric fog / media.
    if (const double cost = categoryMs(gpu, "volumetrics"); cost > 0.0) {
        if (in.volumeResolutionScale > 0.26f) {
            const double s = 0.25 / static_cast<double>(in.volumeResolutionScale);
            const double base = cost * gpuShareOfPixels("volumetrics") * (1.0 - s * s);
            add({"volume-resolution", "Volumetric fog and media", cost,
                 fmt::format("quarter-resolution march (volumeResolutionScale {:.2f} -> 0.25)",
                             in.volumeResolutionScale),
                 "volumequarter", base * 0.7, base, "per-pixel share 0.85 of the volume passes (fitted, M2 Max)", "low"});
        } else if (in.volumeStepScale > 0.5f) {
            const double march = passMs(in.passes, "volume.march");
            add({"volume-steps", "Volumetric fog and media", cost, "half the march steps (volumeStepScale 0.5)",
                 "volumesteps", march * 0.25, march * 0.45,
                 "the march's steps are most of its cost but not all; range from ADR-141's split", "medium"});
        }
    }
    // Post effects: half the gather taps (Stage 2's lever), and switched off.
    for (const auto& [cat, id, title, enabled] :
         std::array<std::tuple<const char*, const char*, const char*, bool>, 2>{
             {{"post: motion blur", "motion-blur", "Motion blur", in.motionBlur},
              {"post: depth of field", "depth-of-field", "Depth of field", in.depthOfField}}}) {
        const double cost = categoryMs(gpu, cat);
        if (cost <= 0.0 || !enabled) {
            continue;
        }
        if (in.postEffectQuality > 0.51f) {
            const bool blur = std::string_view(id) == "motion-blur";
            add({std::string(id) + "-taps", title, cost,
                 "half the gather taps (postEffectQuality 0.5): motion blur's samples, depth of field's tap cap",
                 "posttaps", 0.0, blur ? cost * 0.1 : cost * 0.4,
                 blur ? "measured on Liminal at 1080p: half the samples saved 0.2 of 4.7 ms (inside the noise); the "
                        "pass is bound by its tile and bandwidth work, not its taps"
                      : "only the physical defocus scales its taps with the blur; a plain disc keeps 24",
                 "low"});
        }
        add({std::string(id) + "-off", title, cost, fmt::format("{} off at this level", title),
             std::string_view(id) == "motion-blur" ? "nomotionblur" : "nodof", cost * 0.85, cost,
             "the pass's whole measured cost", "medium"});
    }
    // Shadows.
    if (const double cost = categoryMs(gpu, "shadows"); cost > 0.0) {
        const double shadowPass = passMs(in.passes, "shadow");
        if (in.shadowCasterMinPixels <= 0.0f && in.resources.shadowCasters > 0.0) {
            add({"shadow-casters", "Background shadow casters", cost,
                 fmt::format("stop small and distant objects casting ({:.0f} casters now; a caster must cover 24 px)",
                             in.resources.shadowCasters),
                 "castercull", shadowPass * 0.15, shadowPass * 0.5,
                 "the depth passes' cost scales with casters drawn; how many are small is scene-dependent", "low"});
        }
        if (in.resources.shadowResolution > 1024.0) {
            add({"shadow-atlas", "Shadow map resolution", cost, "a 1024 shadow atlas (from 2048)", "shadowatlas1k",
                 cost * 0.05, cost * 0.3, "fewer texels to write and filter; vertex work is unchanged", "medium"});
        }
    }
    // Render scale: the general lever, offered when the frame is mostly per-pixel and not already scaled.
    if (in.renderScale > 0.86f) {
        double perPixel = 0.0;
        for (const auto& c : gpu) {
            perPixel += c.medianMs * gpuShareOfPixels(c.name);
        }
        const double base = perPixel * (1.0 - 0.85 * 0.85);
        add({"render-scale", "Render resolution", in.gpuMs, "render at 85% and upscale (renderScale 0.85)", "scale85",
             base * 0.75, base, "each category's per-pixel share times the pixels saved (28%)", "medium"});
    }
    // Geometry: LOD and draw distance.
    if (const double cost = categoryMs(gpu, "geometry/opaque"); cost > 0.0) {
        if (in.lodBias <= 1.01f && (in.resources.logicalTriangles > 2.0e6 || in.resources.entityLodDrawables > 0.0)) {
            add({"lod-bias", "Geometry level of detail", cost, "coarser LOD sooner (lodBias 2)", "lodbias2",
                 cost * 0.03, cost * 0.2,
                 "the vertex share of the scene pass; the fitted fixed part is 40-50% on vertex-heavy scenes", "low"});
        }
        if (in.drawDistanceScale >= 0.99f && in.resources.visibleInstances > 1000.0) {
            add({"draw-distance", "Draw distance", cost, "procedural and entity draw distance at 75%", "drawdist75",
                 cost * 0.02, cost * 0.15, "what is drawn beyond three quarters of the distance; scene-dependent",
                 "medium"});
        }
    }
    // Particles.
    if (const double cost = categoryMs(gpu, "particles/simulation"); cost > 0.0 && in.particleSpawnScale > 0.71f) {
        add({"particles", "Particles", cost, "70% spawn rate, and emitters beyond 60 m culled", "particlelod",
             cost * 0.15, cost * 0.35, "spawns scale the alive count; the fixed dispatch cost stays", "medium"});
    }
    // Procedural material programs (the Sonic Abstract art agent's finding): about 3 ms at full-frame coverage plus
    // about 0.09 ms per op, and "unlit" does not make them cheap. Their cost is inside the scene pass, so it is shown
    // as an ESTIMATE with its basis; `--verify-candidates` measures it with the noprograms arm.
    if (in.resources.materialPrograms > 0) {
        const double programs = static_cast<double>(in.resources.materialPrograms);
        const double avgOps = static_cast<double>(in.resources.materialProgramOps) / programs;
        const double fullFrame = 3.0 + 0.09 * avgOps; // what one program costs covering the whole frame
        const double scene = categoryMs(gpu, "geometry/opaque");
        add({"material-programs", "Procedural material programs", scene,
             fmt::format("{} program(s), {:.0f} ops each on average: fewer ops, or a plain material on what is small "
                         "on screen",
                         in.resources.materialPrograms, avgOps),
             "noprograms", std::min(scene, 0.1 * fullFrame), std::min(scene, fullFrame),
             "~3 ms + ~0.09 ms per op for a program covering the whole 1080p frame (the Sonic Abstract art pass, M2 "
             "Max); their screen coverage is not measured, so the range is 10% to 100% of the frame",
             "high"});
    }
    std::stable_sort(out.begin(), out.end(), [](const auto& a, const auto& b) {
        return a.estimatedHighMs + a.estimatedLowMs > b.estimatedHighMs + b.estimatedLowMs;
    });
    return out;
}

// ---- the builder ---------------------------------------------------------------------------------------------------

void buildLiveProfile(LiveProfileRecord& record, const std::vector<LiveProfileFrame>& frames, bool live) {
    std::vector<double> frame, gpuSamples, cpu, wait;
    for (const auto& f : frames) {
        frame.push_back(f.frameMs);
        if (f.gpuMs >= 0.0) gpuSamples.push_back(f.gpuMs);
        cpu.push_back(f.cpuWorkMs);
        wait.push_back(f.waitMs);
    }
    record.frameMs = rendering::describe(frame);
    record.gpuMs = rendering::describe(gpuSamples);
    record.cpuWorkMs = rendering::describe(cpu);
    record.waitMs = rendering::describe(wait);
    const double budget = record.conditions.budgetMs;
    const double refresh = live && record.conditions.displayRefreshHz > 0.0 ? 1000.0 / record.conditions.displayRefreshHz
                                                                            : 0.0;
    if (live) {
        record.frameBasis = "measured: the interval between presented frames (the real loop, present and Fifo)";
        record.budget = budgetStats(frame, budget, refresh);
    } else {
        // Headless serialises CPU and GPU, so the wall clock over-reads what a pipelined live frame costs. The budget is
        // checked against max(GPU, CPU work) per frame -- both measured, combined by the pipelining model -- and the
        // wall clock is still reported as itself.
        record.frameBasis = "headless: budget checked against max(GPU span, CPU work) per frame (each measured; their "
                            "max is the pipelined model). frameMs is the serial wall clock, a pessimistic bound";
        std::vector<double> pipelined;
        for (const auto& f : frames) {
            pipelined.push_back(std::max(f.gpuMs, f.cpuWorkMs));
        }
        record.budget = budgetStats(pipelined, budget, 0.0);
    }
    record.gpuBudget = budgetStats(gpuSamples, budget, 0.0);
    record.worst = worstFrames(frames, record.conditions.deep ? 10 : 5);
    // The GPU reading the controller uses (ADR-1085): the span, capped by the frame interval -- consecutive frames
    // overlap on the GPU, so a span can over-read.
    double gpuMedian = record.gpuMs.valid() ? record.gpuMs.p50 : 0.0;
    if (live && record.frameMs.valid() && gpuMedian > record.frameMs.mean) {
        gpuMedian = record.frameMs.mean;
    }
    record.critical = criticalPath(live ? record.frameMs.p50 : std::max(gpuMedian, record.cpuWorkMs.p50), gpuMedian,
                                   record.cpuWorkMs.p50, record.waitMs.p50, budget, live);
    record.cpu = cpuCategories(frames, live);
    {
        std::vector<std::pair<std::string, std::vector<double>>> byLabel;
        for (const auto& f : frames) {
            for (const auto& p : f.passes) {
                auto it = std::find_if(byLabel.begin(), byLabel.end(), [&](const auto& e) { return e.first == p.label; });
                if (it == byLabel.end()) {
                    byLabel.emplace_back(p.label, std::vector<double>{p.ms});
                } else {
                    it->second.push_back(p.ms);
                }
            }
        }
        record.passMedians.clear();
        for (auto& [label, v] : byLabel) {
            // A frame without the label paid 0 for it (the pass did not run): a median over only the frames that ran
            // it would report a pass the level switched off as if it still cost what it did before.
            v.resize(frames.size(), 0.0);
            record.passMedians.push_back({label, medianOf(v)});
        }
        std::stable_sort(record.passMedians.begin(), record.passMedians.end(),
                         [](const auto& a, const auto& b) { return a.ms > b.ms; });
    }
    record.gpu = groupGpuCategories(record.passMedians);
    // Status. "AT RISK": the median fits but more than 5% of frames do not.
    const double p50 = live ? record.frameMs.p50 : std::max(gpuMedian, record.cpuWorkMs.p50);
    if (record.budget.frames == 0) {
        record.status = "NO DATA";
    } else if (p50 > budget) {
        record.status = "OVER BUDGET";
    } else if (record.budget.percentUnder < 95.0 || (live && record.budget.percentMissed > 1.0)) {
        record.status = "AT RISK";
    } else {
        record.status = "TARGET ACHIEVED";
    }
    const double cost = live ? std::max(gpuMedian, record.cpuWorkMs.p50) : p50;
    record.headroom = record.status == "TARGET ACHIEVED" && cost < 0.8 * budget;
    record.limits = {
        "Results are specific to this machine (see conditions); another GPU, display or OS is another measurement.",
        "GPU pass times are intervals between pass-end timestamps on one timeline (gpu/frame_timeline.hpp); "
        "consecutive frames can overlap on the GPU, so a span can over-read (ADR-1085 caps it by the frame interval).",
        "Metal writes no timestamp for an empty render pass; that pass's cost folds into the next pass's interval.",
        "The ImGui UI and the output/projection copies are not GPU-timed; in live mode their cost is inside the frame "
        "interval and the waits.",
    };
    if (live && refresh > 0.0 && record.frameMs.valid() && record.frameMs.p50 < 0.75 * refresh) {
        record.limits.push_back(fmt::format(
            "The loop ran faster than the display refreshes (median interval {:.1f} ms, a refresh every {:.1f} ms): with "
            "the editor and the projection both presenting, intervals alternate short and long, and a frame over a "
            "refresh is a frame the display showed twice. Deadline misses count those long intervals.",
            record.frameMs.p50, refresh));
    }
    if (!live) {
        record.limits.push_back("Headless has no present, no Fifo and no UI: it cannot show deadline misses. Use "
                                "--mode live for a touring prediction.");
    }
}

// ---- writers -------------------------------------------------------------------------------------------------------

namespace {

json distJson(const rendering::Distribution& d) {
    if (!d.valid()) {
        return nullptr;
    }
    return json{{"count", d.count}, {"mean", d.mean},  {"p50", d.p50},       {"p90", d.p90},
                {"p95", d.p95},     {"p99", d.p99},    {"min", d.min},       {"max", d.max},
                {"low1Percent", d.low1Percent},         {"stddev", d.stddev}};
}

json budgetJson(const BudgetStats& b) {
    json j{{"budgetMs", b.budgetMs}, {"frames", b.frames}, {"overBudget", b.overBudget}, {"percentUnder", b.percentUnder}};
    if (b.refreshMs > 0.0) {
        j["refreshMs"] = b.refreshMs;
        j["deadlineMisses"] = b.deadlineMisses;
        j["vsyncsMissed"] = b.vsyncsMissed;
        j["percentMissed"] = b.percentMissed;
    } else {
        j["deadlineMisses"] = nullptr;
    }
    return j;
}

json definitions() {
    return json{
        {"measured", "timed on this run"},
        {"coarse", "timed on this run, but the number covers more than its name"},
        {"estimated", "computed from a model (the basis says which); never a measurement"},
        {"frameMs", "live: the interval between presented frames; headless: the wall clock of one serial frame"},
        {"budgetMs", "1000 / targetFps"},
        {"deadlineMisses", "live only: frames that took more display refreshes than the target allows"},
        {"gpuMs", "the GPU timeline's span per frame (first to last pass end)"},
        {"cpuWorkMs", "main-thread work per frame, waits excluded"},
        {"criticalPath", "GPU, CPU or sync/present: which one the frame waits on, from the measured medians"},
        {"candidates.estimated*", "ESTIMATED savings from the per-pass scaling model"},
        {"candidates.measured*", "MEASURED savings from an interleaved, counterbalanced A/B in this process"},
        {"candidates.heroEffect", "ADR-1109: exempt (heroes skipped by the engine), image-wide (every pixel, heroes "
                                  "included), degrades (a hero's own representation)"},
        {"contribution", "ADR-1108: screen coverage of bounding spheres over the lever weight; heroes are protected"},
        {"comparisons", "ADR-1110: ORIGINAL vs OPTIMIZED, MEASURED: timing by A/B, pictures by the Quality Lab"},
        {"optimization.plans", "ADR-1111: ESTIMATED from measured singles, assumed additive; never a measurement"},
        {"optimization.measuredCombos", "ADR-1111: MEASURED combinations"},
    };
}

} // namespace

json liveProfileJson(const LiveProfileRecord& r) {
    const auto& c = r.conditions;
    json j;
    j["schema"] = "avgen.liveprofile/1";
    j["definitions"] = definitions();
    j["conditions"] = {
        {"scene", c.scene},
        {"mode", c.mode},
        {"machine", c.machine},
        {"cpu", c.cpu},
        {"os", c.os},
        {"gpu", c.gpu},
        {"backend", c.backend},
        {"buildType", c.buildType},
        {"gitRevision", c.gitRevision},
        {"gitDirty", c.gitDirty},
        {"displayRefreshHz", c.displayRefreshHz > 0.0 ? json(c.displayRefreshHz) : json(nullptr)},
        {"window", c.window},
        {"output", {{"width", c.outputWidth}, {"height", c.outputHeight}}},
        {"internal", {{"width", c.internalWidth}, {"height", c.internalHeight}}},
        {"targetFps", c.targetFps},
        {"budgetMs", c.budgetMs},
        {"qualityBudgetMs", c.qualityBudgetMs},
        {"quality", {{"requested", c.qualityRequested}, {"level", c.liveLevel}, {"strategy", c.strategy},
                     {"profile", c.profile}, {"renderScale", c.renderScale}}},
        {"audio", c.audioState},
        {"midi", c.midi},
        {"seed", c.haveSeed ? json(c.seed) : json(nullptr)},
        {"camera", c.camera},
        {"startSeconds", c.startSeconds},
        {"warmupCapSeconds", c.warmupCapSeconds},
        {"measureSeconds", c.measureSeconds},
        {"deep", c.deep},
        {"startedAt", c.startedAt},
        {"sessionId", c.sessionId},
        {"hardwareSpecific", true},
    };
    j["cold"] = {{"loadMs", r.cold.loadMs},
                 {"prewarmMs", r.cold.prewarmMs >= 0.0 ? json(r.cold.prewarmMs) : json(nullptr)},
                 {"prewarmVariants", r.cold.prewarmVariants},
                 {"firstFrameMs", r.cold.firstFrameMs},
                 {"warmupFrames", r.cold.warmupFrames},
                 {"warmupSeconds", r.cold.warmupSeconds},
                 {"steadyReached", r.cold.steadyReached},
                 {"compilesDuringMeasure", r.cold.compilesDuringMeasure}};
    j["frame"] = {{"basis", r.frameBasis},
                  {"frameMs", distJson(r.frameMs)},
                  {"gpuMs", distJson(r.gpuMs)},
                  {"cpuWorkMs", distJson(r.cpuWorkMs)},
                  {"waitMs", distJson(r.waitMs)},
                  {"budget", budgetJson(r.budget)},
                  {"gpuBudget", budgetJson(r.gpuBudget)}};
    json worst = json::array();
    for (const auto& w : r.worst) {
        worst.push_back({{"index", w.index}, {"atSeconds", w.atSeconds}, {"frameMs", w.frameMs},
                         {"gpuMs", w.gpuMs >= 0.0 ? json(w.gpuMs) : json(nullptr)}, {"cpuWorkMs", w.cpuWorkMs},
                         {"waitMs", w.waitMs}});
    }
    j["worstFrames"] = worst;
    j["criticalPath"] = {{"verdict", r.critical.verdict}, {"frameMs", r.critical.frameMs}, {"gpuMs", r.critical.gpuMs},
                         {"cpuWorkMs", r.critical.cpuWorkMs}, {"waitMs", r.critical.waitMs},
                         {"explanation", r.critical.explanation}};
    json cpu = json::array();
    for (const auto& cc : r.cpu) {
        cpu.push_back({{"name", cc.name}, {"medianMs", cc.medianMs >= 0.0 ? json(cc.medianMs) : json(nullptr)},
                       {"basis", cc.basis}, {"covers", cc.covers}});
    }
    j["cpu"] = cpu;
    json gpu = json::array();
    for (const auto& g : r.gpu) {
        json labels = json::array();
        for (const auto& l : g.labels) {
            labels.push_back({{"label", l.label}, {"medianMs", l.ms}});
        }
        gpu.push_back({{"category", g.name}, {"medianMs", g.medianMs}, {"basis", "measured"}, {"labels", labels}});
    }
    j["gpu"] = gpu;
    const auto& s = r.resources;
    json largest = json::array();
    for (const auto& t : s.largestTextures) {
        largest.push_back({{"label", t.label}, {"bytes", t.bytes}, {"detail", t.detail}});
    }
    j["resources"] = {
        {"geometry",
         {{"draws", s.draws}, {"triangles", s.triangles}, {"logicalTriangles", s.logicalTriangles},
          {"visibleInstances", s.visibleInstances}, {"culledInstances", s.culledInstances}, {"entities", s.entities},
          {"proceduralLod", {s.lod[0], s.lod[1], s.lod[2], s.lod[3]}},
          {"entityLod", {{"drawables", s.entityLodDrawables}, {"demoted", s.entityLodDemoted}}}}},
        {"memory",
         s.haveMemory ? json{{"basis", "Dawn's own estimate (ComputeEstimatedMemoryUsageInfo), read once after measuring"},
                             {"textureBytes", s.textureBytes},
                             {"renderTargetBytes", s.renderTargetBytes},
                             {"depthStencilBytes", s.depthStencilBytes},
                             {"bufferBytes", s.bufferBytes},
                             {"totalBytes", s.totalBytes},
                             {"textures", s.textureCount},
                             {"buffers", s.bufferCount},
                             {"largestTextures", largest}}
                      : json(nullptr)},
        {"pipelines",
         {{"render", s.renderPipelines}, {"compute", s.computePipelines}, {"shaderModules", s.shaderModules},
          {"sdfVariants", s.sdfVariants}, {"materialPrograms", s.materialPrograms},
          {"materialProgramOps", s.materialProgramOps}}},
        {"shadows",
         {{"casters", s.shadowCasters}, {"views", s.shadowViews}, {"cascades", s.cascades}, {"spotMaps", s.spotMaps},
          {"pointMaps", s.pointMaps}, {"resolution", s.shadowResolution}, {"shadowDraws", s.shadowDraws},
          {"lightsCasting", s.shadowLights}, {"passSkippedNothingLit", s.shadowPassSkipped}}},
        {"lights", {{"shaded", s.lights}, {"directional", s.directionalLights}, {"clustered", s.clusteredLights}}},
        {"particles",
         {{"systems", s.particleSystems}, {"capacity", s.particleCapacity}, {"emittedPerFrame", s.particlesEmitted},
          {"dispatches", s.particleDispatches}, {"simulationStepsPerFrame", s.particleSimSteps},
          {"aliveNote", "alive counts live on the GPU and are not read back; capacity is the pools' sum"}}},
        {"post",
         {{"passes", s.postPasses}, {"bloomLevels", s.bloomLevels}, {"width", s.postWidth}, {"height", s.postHeight},
          {"volume", {{"width", s.volumeWidth}, {"height", s.volumeHeight}, {"steps", s.volumeSteps}}},
          {"ao", {{"width", s.aoWidth}, {"height", s.aoHeight}}}}},
        {"sdf", {{"raymarchObjects", s.sdfRaymarchObjects}, {"avgSteps", s.sdfAvgSteps}}},
        {"computeDispatches", s.computeDispatches},
        {"gpuPasses", s.gpuPasses},
        {"transientTextures", s.transientTextures},
    };
    json cands = json::array();
    for (const auto& k : r.candidates) {
        json cj{{"id", k.id},
                {"title", k.title},
                {"costMs", k.costMs},
                {"suggestion", k.suggestion},
                {"lever", k.lever},
                {"estimatedSavingMs", {{"low", k.estimatedLowMs}, {"high", k.estimatedHighMs}}},
                {"estimateBasis", k.estimateBasis},
                {"risk", k.risk},
                {"heroEffect", k.heroEffect},
                {"verified", k.verified}};
        if (k.verified) {
            cj["measuredSaving"] = {{"ms", k.measuredSavingMs},
                                    {"percent", k.measuredSavingPercent},
                                    {"noiseFloorPercent", k.noiseFloorPercent},
                                    {"isResult", k.measuredIsResult},
                                    {"void", k.measuredVoid},
                                    {"pairs", k.measuredPairs},
                                    {"verdict", k.measuredVerdict}};
        }
        cands.push_back(std::move(cj));
    }
    j["candidates"] = cands;
    j["verification"] = r.verificationMode;
    json ents = json::array();
    for (const auto& e : r.entities) {
        json ej{{"name", e.name},          {"projectedArea", e.projectedArea}, {"distance", e.distance},
                {"hero", e.hero},          {"importance", e.importance},       {"castsShadow", e.castsShadow},
                {"visible", e.visible},    {"radiusPx", e.radiusPx},           {"leverWeight", e.leverWeight},
                {"onScreen", e.onScreen},
                {"contribution", std::isfinite(e.contribution) ? json(e.contribution) : json("protected (hero)")}};
        if (e.haveBox) {
            ej["box"] = {e.x0, e.y0, e.x1, e.y1};
        }
        ents.push_back(std::move(ej));
    }
    j["entities"] = ents;
    j["contribution"] = contributionJson(r.contribution);
    if (!r.comparisons.empty()) {
        json comps = json::array();
        for (const auto& c : r.comparisons) {
            comps.push_back(comparisonJson(c));
        }
        j["comparisons"] = {{"floor", r.comparisonFloor.raw.is_null() ? json(nullptr) : r.comparisonFloor.raw},
                            {"runs", comps}};
    }
    if (r.optimization.ran) {
        j["optimization"] = optimizationJson(r.optimization);
    }
    j["status"] = r.status;
    j["headroom"] = r.headroom;
    j["limits"] = r.limits;
    j["notes"] = r.notes;
    return j;
}

std::string liveProfileJsonText(const LiveProfileRecord& record) { return liveProfileJson(record).dump(2) + "\n"; }

std::string liveProfileText(const LiveProfileRecord& r) {
    const auto& c = r.conditions;
    std::string t;
    const auto line = [&]<typename... A>(fmt::format_string<A...> f, A&&... args) {
        t += fmt::format(f, std::forward<A>(args)...);
        t += '\n';
    };
    const std::string rule(56, '-');
    line("AV GEN LIVE SCENE PROFILE ({})", c.mode);
    line("{}", rule);
    line("Scene:        {}", c.scene);
    line("Machine:      {}{}", c.machine.empty() ? "unknown" : c.machine, c.cpu.empty() ? "" : ", " + c.cpu);
    line("OS:           {}", c.os.empty() ? "unknown" : c.os);
    line("Renderer:     {} ({})", c.backend, c.gpu);
    line("Build:        {} {}{}", c.buildType, c.gitRevision, c.gitDirty ? " (dirty)" : "");
    line("Output:       {} x {} {}{}", c.outputWidth, c.outputHeight, c.window,
         c.displayRefreshHz > 0.0 ? fmt::format(", display {:.0f} Hz", c.displayRefreshHz) : std::string());
    line("Internal:     {} x {} (scale {:.2f})", c.internalWidth, c.internalHeight, c.renderScale);
    line("Quality:      {} -> {}{}{}", c.qualityRequested, c.liveLevel,
         c.strategy.empty() ? "" : ", strategy " + c.strategy, c.profile.empty() ? "" : ", profile " + c.profile);
    line("Target:       {:.0f} FPS   Budget: {:.2f} ms", c.targetFps, c.budgetMs);
    line("Audio/MIDI:   {} / {}", c.audioState, c.midi);
    line("Measured:     {:.1f} s from {:.1f} s ({} frames){}", c.measureSeconds, c.startSeconds, r.budget.frames,
         c.deep ? ", deep" : "");
    line("These numbers are specific to this machine.");
    line("{}", rule);
    line("COLD COSTS (not in the frame statistics)");
    line("  load {:.0f} ms   pre-warm {}   first frame {:.1f} ms", r.cold.loadMs,
         r.cold.prewarmMs >= 0.0 ? fmt::format("{:.0f} ms ({} built)", r.cold.prewarmMs, r.cold.prewarmVariants)
                                 : std::string("none"),
         r.cold.firstFrameMs);
    line("  warm-up {} frames / {:.1f} s, {}", r.cold.warmupFrames, r.cold.warmupSeconds,
         r.cold.steadyReached ? "steady state reached" : "CAP REACHED BEFORE THE FRAME SETTLED");
    if (r.cold.compilesDuringMeasure > 0) {
        line("  WARNING: {} pipeline/variant compile(s) happened while measuring", r.cold.compilesDuringMeasure);
    }
    line("{}", rule);
    line("FRAME ({})", r.frameBasis);
    if (r.frameMs.valid()) {
        const auto& d = r.frameMs;
        line("  Mean {:6.2f}  Median {:6.2f}  P90 {:6.2f}  P95 {:6.2f}  P99 {:6.2f}  Worst {:6.2f} ms", d.mean, d.p50,
             d.p90, d.p95, d.p99, d.max);
    }
    if (r.gpuMs.valid()) {
        line("  GPU  median {:6.2f}  P95 {:6.2f}  P99 {:6.2f} ms", r.gpuMs.p50, r.gpuMs.p95, r.gpuMs.p99);
    }
    line("  CPU work median {:6.2f}  P95 {:6.2f} ms   waits median {:.2f} ms", r.cpuWorkMs.p50, r.cpuWorkMs.p95,
         r.waitMs.p50);
    line("  under budget: {:.1f}%   over: {} of {}", r.budget.percentUnder, r.budget.overBudget, r.budget.frames);
    if (r.budget.refreshMs > 0.0) {
        line("  deadline misses: {} ({:.1f}%), {} vsync(s) lost at {:.2f} ms per refresh", r.budget.deadlineMisses,
             r.budget.percentMissed, r.budget.vsyncsMissed, r.budget.refreshMs);
    }
    line("  critical path: {} -- {}", r.critical.verdict, r.critical.explanation);
    if (!r.worst.empty()) {
        std::string w;
        for (const auto& f : r.worst) {
            w += fmt::format(" {:.1f}ms@{:.2f}s", f.frameMs, f.atSeconds);
        }
        line("  worst frames:{}", w);
    }
    line("{}", rule);
    line("STATUS: {}{}", r.status, r.headroom ? " (headroom available)" : "");
    line("{}", rule);
    line("GPU (medians, measured)");
    for (const auto& g : r.gpu) {
        std::string labels;
        for (const auto& l : g.labels) {
            labels += fmt::format(" {}={:.2f}", l.label, l.ms);
        }
        line("  {:<22} {:6.2f} ms  [{}]", g.name, g.medianMs, labels.empty() ? "" : labels.substr(1));
    }
    line("CPU (medians)");
    for (const auto& cc : r.cpu) {
        if (cc.medianMs < 0.0) {
            line("  {:<52}    --    {}", cc.name, cc.basis);
        } else {
            line("  {:<52} {:6.2f} ms {}", cc.name, cc.medianMs, cc.basis);
        }
    }
    const auto& s = r.resources;
    line("{}", rule);
    line("RESOURCES (medians over the window)");
    line("  geometry: {:.0f} draws, {:.0f} triangles ({:.0f} logical), {:.0f} entities, instances {:.0f} visible / "
         "{:.0f} culled",
         s.draws, s.triangles, s.logicalTriangles, s.entities, s.visibleInstances, s.culledInstances);
    if (s.haveMemory) {
        line("  memory (Dawn's estimate): textures {:.1f} MB (render targets {:.1f}, depth {:.1f}), buffers {:.1f} MB",
             s.textureBytes / 1.0e6, s.renderTargetBytes / 1.0e6, s.depthStencilBytes / 1.0e6, s.bufferBytes / 1.0e6);
        for (std::size_t k = 0; k < std::min<std::size_t>(5, s.largestTextures.size()); ++k) {
            line("    {:>7.1f} MB  {} {}", s.largestTextures[k].bytes / 1.0e6, s.largestTextures[k].label,
                 s.largestTextures[k].detail);
        }
    } else {
        line("  memory: not available on this backend");
    }
    line("  pipelines: {} render, {} compute, {} shader modules; SDF variants {}; material programs {} ({} ops)",
         s.renderPipelines, s.computePipelines, s.shaderModules, s.sdfVariants, s.materialPrograms,
         s.materialProgramOps);
    line("  shadows: {:.0f} casters, {:.0f} views ({:.0f} cascades, {:.0f} spot, {:.0f} point), {:.0f}^2, {:.0f} draws{}",
         s.shadowCasters, s.shadowViews, s.cascades, s.spotMaps, s.pointMaps, s.shadowResolution, s.shadowDraws,
         s.shadowPassSkipped ? "; pass skipped: nothing visible is lit" : "");
    line("  particles: {:.0f} systems, capacity {:.0f}, {:.0f} spawns/frame, {:.0f} sim steps/frame", s.particleSystems,
         s.particleCapacity, s.particlesEmitted, s.particleSimSteps);
    line("  post: {:.0f} passes at {}x{}, bloom {:.0f} levels; volume {}x{} x {:.0f} steps; AO {}x{}", s.postPasses,
         s.postWidth, s.postHeight, s.bloomLevels, s.volumeWidth, s.volumeHeight, s.volumeSteps, s.aoWidth, s.aoHeight);
    line("{}", rule);
    line("OPTIMIZATION CANDIDATES (savings ESTIMATED unless marked MEASURED)");
    int n = 0;
    for (const auto& k : r.candidates) {
        line("{:2}. {} -- cost {:.2f} ms", ++n, k.title, k.costMs);
        line("    change:    {}", k.suggestion);
        line("    estimated: {:.2f}-{:.2f} ms  ({})", k.estimatedLowMs, k.estimatedHighMs, k.estimateBasis);
        if (k.verified) {
            line("    MEASURED:  {:+.2f} ms GPU ({:+.1f}%), floor {:.1f}% over {} pair(s) -> {}", k.measuredSavingMs,
                 k.measuredSavingPercent, k.noiseFloorPercent, k.measuredPairs, k.measuredVerdict);
        }
        line("    risk:      {}{}   heroes: {}", k.risk, k.lever.empty() ? "" : "   (lever: " + k.lever + ")",
             k.heroEffect.empty() ? "-" : k.heroEffect);
    }
    if (r.candidates.empty()) {
        line("  none above 0.1 ms");
    }
    if (!r.verificationMode.empty()) {
        line("  verification: {}", r.verificationMode);
    }
    if (r.contribution.available) {
        line("{}", rule);
        t += contributionText(r.contribution);
    }
    for (const auto& c : r.comparisons) {
        line("{}", rule);
        t += comparisonText(c, r.comparisonFloor);
    }
    if (r.optimization.ran) {
        line("{}", rule);
        t += optimizationText(r.optimization);
    }
    line("{}", rule);
    line("LIMITS");
    for (const auto& l : r.limits) {
        line("  - {}", l);
    }
    for (const auto& note : r.notes) {
        line("  * {}", note);
    }
    return t;
}

// ---- host ----------------------------------------------------------------------------------------------------------

HostDescription describeHost() {
    HostDescription h;
#if defined(__APPLE__)
    const auto read = [](const char* name) -> std::string {
        std::size_t size = 0;
        if (::sysctlbyname(name, nullptr, &size, nullptr, 0) != 0 || size == 0) {
            return {};
        }
        std::string value(size, '\0');
        if (::sysctlbyname(name, value.data(), &size, nullptr, 0) != 0) {
            return {};
        }
        while (!value.empty() && value.back() == '\0') {
            value.pop_back();
        }
        return value;
    };
    h.machine = read("hw.model");
    h.cpu = read("machdep.cpu.brand_string");
    const std::string version = read("kern.osproductversion");
    h.os = version.empty() ? "macOS" : "macOS " + version;
#endif
    return h;
}

} // namespace avgen::app
