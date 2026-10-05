// ADR-1110..1112: Phase 5 on the headless live-profile loop (docs/live-optimizer/03-phase5-plan.md).
//
// ORIGINAL and OPTIMIZED are rendered at the same piece times by the fixed-step clock (restarted, temporal history
// reset, ten settling frames, then a run of consecutive frames), written as PNGs, and handed to the Quality Lab's
// `avgen_quality ab` across a process boundary (ADR-250: the engine does not link the Lab). The self-difference floor
// is ORIGINAL rendered twice. Time is measured with the same counterbalanced A/B the profile's --verify-candidates
// uses (`compareArms`), on the pipelined frame cost max(GPU span, CPU work). The Critic, when asked, is asked last and
// its answer is stored beside the objective numbers; nothing here reads it.

#include "app/application.hpp"

#include "app/directing_evaluate.hpp"
#include "app/live_optimize.hpp"
#include "app/live_profile.hpp"
#include "assets/image.hpp"
#include "core/log.hpp"
#include "rendering/scene_renderer.hpp"

#include <fmt/format.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <unistd.h>

namespace avgen::app {

namespace fs = std::filesystem;
using nlohmann::json;

namespace {

constexpr int kSettleFrames = 10;
constexpr int kPairs = 2;

std::string joinedLevers(const std::vector<std::string>& v) {
    std::string out;
    for (const auto& s : v) {
        out += out.empty() ? s : "+" + s;
    }
    return out.empty() ? std::string("none") : out;
}

std::string slug(const std::string& s) {
    std::string out;
    for (const char c : s) {
        out += std::isalnum(static_cast<unsigned char>(c)) != 0 ? c : '-';
    }
    return out;
}

double medianOf(std::vector<double> v) {
    if (v.empty()) {
        return 0.0;
    }
    std::nth_element(v.begin(), v.begin() + static_cast<std::ptrdiff_t>(v.size() / 2), v.end());
    return v[v.size() / 2];
}

// The Quality Lab's CLI: AVGEN_QUALITY, else beside this build (build/<cfg>/src/avgen -> build/<cfg>/tools/quality-lab/
// avgen_quality), else on PATH.
fs::path qualityTool(const fs::path& executable) {
    if (const char* env = std::getenv("AVGEN_QUALITY"); env != nullptr && *env != '\0') {
        return env;
    }
    std::error_code ec;
    const fs::path exe = fs::weakly_canonical(executable, ec);
    for (const fs::path& candidate : {exe.parent_path().parent_path() / "tools" / "quality-lab" / "avgen_quality",
                                      exe.parent_path() / "avgen_quality"}) {
        if (fs::is_regular_file(candidate, ec)) {
            return candidate;
        }
    }
    return "avgen_quality";
}

} // namespace

int Application::runLivePhase5(LiveProfileRecord& record, const LiveAbDriver& d) {
    const LiveProfileOptions& o = options_.liveProfile;
    const fs::path root = o.abDir ? *o.abDir
                                  : fs::temp_directory_path() / fmt::format("avgen-ab-{}", static_cast<long long>(::getpid()));
    std::error_code ec;
    fs::create_directories(root, ec);
    const fs::path quality = qualityTool(executablePath_);
    const int frames = std::max(2, o.abFrames);
    const int blockFrames = std::clamp(static_cast<int>(std::lround(o.measureSeconds * o.targetFps / 2.0)), 30, 90);
    const bool gpuBound = record.gpuMs.valid() && record.gpuMs.p50 >= record.cpuWorkMs.p50;

    // Regions: the on-screen heroes' boxes (ADR-1108), for the Lab's region metrics.
    const fs::path regionsFile = root / "hero-regions.json";
    {
        json j;
        j["regions"] = json::array();
        for (const auto& b : record.contribution.heroRegions) {
            j["regions"].push_back({{"name", b.name}, {"x0", b.x0}, {"y0", b.y0}, {"x1", b.x1}, {"y1", b.y1}});
        }
        std::ofstream(regionsFile) << j.dump(2) << "\n";
    }

    const auto setQuality = [&](const rendering::QualitySettings& q) {
        renderer_->setQualitySettings(q);
        scene::DetailLimits limits = engine_->detailLimits();
        limits.distanceScale = q.drawDistanceScale; // ADR-1094's CPU half, as the live editor does
        engine_->setDetailLimits(limits);
    };
    const auto withLevers = [&](const std::vector<std::string>& levers) -> std::optional<rendering::QualitySettings> {
        LiveQualityRung c{};
        for (const auto& l : levers) {
            if (!applyLeverToCeiling(c, l)) {
                return std::nullopt;
            }
        }
        return applyCeiling(d.base, c);
    };

    // Renders one arm's run of frames into `dir`. False on a render or write failure.
    const auto capture = [&](const rendering::QualitySettings& q, const fs::path& dir) -> bool {
        fs::remove_all(dir, ec);
        fs::create_directories(dir, ec);
        setQuality(q);
        d.restart(d.startPiece);
        for (int i = 0; i < kSettleFrames + frames; ++i) {
            LiveProfileFrame f;
            std::optional<gpu::Image8> image;
            const bool keep = i >= kSettleFrames;
            if (!d.step(f, keep ? &image : nullptr)) {
                return false;
            }
            if (keep && image) {
                const fs::path file = dir / fmt::format("frame_{:06d}.png", i - kSettleFrames);
                if (auto r = assets::writePng(file, image->width, image->height, image->rgba); !r) {
                    log::error("ab: {}", r.error().message);
                    return false;
                }
            }
        }
        return true;
    };

    // The Lab's comparison of two runs.
    const auto compareDirs = [&](const fs::path& a, const fs::path& b, const std::string& name) -> AbVisual {
        const fs::path out = root / (name + ".abdiff.json");
        const std::vector<std::string> argv{quality.string(), "ab",        "--original", a.string(), "--optimized",
                                            b.string(),       "--regions", regionsFile.string(), "--out", out.string()};
        auto outcome = runProcess(argv, root, nullptr, 600.0);
        AbVisual v;
        if (!outcome) {
            v.unavailableReason = fmt::format("the Quality Lab ({}) did not start: {}", quality.string(),
                                              outcome.error().message);
            return v;
        }
        if (outcome->exitCode != 0) {
            v.unavailableReason = fmt::format("the Quality Lab exited {}: {}", outcome->exitCode, outcome->err);
            return v;
        }
        std::ifstream in(out);
        const json j = json::parse(in, nullptr, false);
        v = abVisualFromJson(j);
        return v;
    };

    // The counterbalanced time A/B (ADR-181), on the frame cost the budget is checked against.
    const auto timeArms = [&](const rendering::QualitySettings& baseQ, const rendering::QualitySettings& armQ) {
        std::vector<rendering::AbBlock> baseBlocks, armBlocks;
        std::vector<double> baseCost, armCost;
        for (int pair = 0; pair < kPairs; ++pair) {
            for (int half = 0; half < 2; ++half) {
                const bool isArm = (half == 0) == (pair % 2 == 1);
                setQuality(isArm ? armQ : baseQ);
                d.restart(d.startPiece);
                std::vector<double> cost, gpuMs;
                for (int i = 0; i < kSettleFrames + blockFrames; ++i) {
                    LiveProfileFrame f;
                    if (!d.step(f, nullptr)) {
                        return AbTiming{};
                    }
                    if (i >= kSettleFrames) {
                        cost.push_back(std::max(f.gpuMs, f.cpuWorkMs));
                        if (f.gpuMs >= 0.0) gpuMs.push_back(f.gpuMs);
                    }
                }
                (isArm ? armCost : baseCost).push_back(medianOf(cost));
                (isArm ? armBlocks : baseBlocks).push_back({rendering::describe(cost), rendering::describe(gpuMs)});
            }
        }
        const rendering::AbSummary ab = rendering::compareArms("phase5", baseBlocks, armBlocks);
        // The clock that bounds the frame decides: the GPU span on a GPU-bound scene (the tighter noise floor), else
        // the pipelined cost.
        const rendering::PairedDelta& delta = gpuBound ? ab.gpu : ab.wall;
        AbTiming t;
        t.measured = ab.blocks > 0;
        t.pairs = ab.blocks;
        t.baselineMs = medianOf(baseCost);
        t.optimizedMs = medianOf(armCost);
        t.savingMs = t.baselineMs - t.optimizedMs;
        t.savingPercent = t.baselineMs > 0.0 ? 100.0 * t.savingMs / t.baselineMs : 0.0;
        t.noiseFloorPercent = delta.noiseFloorPercent;
        t.baselineGpuMs = ab.gpu.baselineMs;
        t.optimizedGpuMs = ab.gpu.armMs;
        t.gpuSavingMs = ab.gpu.deltaMs;
        t.isResult = delta.isResult();
        t.voided = (gpuBound ? ab.gpuDrift : ab.wallDrift).voids(delta.deltaMs);
        t.verdict = !t.measured            ? "not measured"
                    : t.voided             ? "VOID: the machine drifted more than the effect"
                    : !t.isResult          ? "inside the noise: no measurable saving"
                    : delta.deltaMs > 0.0  ? "a saving, outside the noise"
                                           : "SLOWER, outside the noise";
        return t;
    };

    // The Critic, optional (ADR-1112): both runs submitted, then compared. Never read by the optimizer.
    const auto askCritic = [&](AbComparison& c, const fs::path& originalDir, const fs::path& optimizedDir) {
        c.critic.asked = true;
        const EvaluatorOptions eo = evaluatorOptionsFrom(options_.critic, options_.criticUrl);
        if (eo.critic.empty()) {
            c.critic.unavailableReason = "no Critic is configured (--critic <path to critic> or AVGEN_CRITIC)";
            return;
        }
        const auto submit = [&](const fs::path& dir, const std::string& label, std::string& job) -> bool {
            auto r = runProcess(criticSequenceCommand(eo.critic.string(), eo.criticUrl, dir.string(), o.targetFps, label,
                                                      std::string()),
                                root, nullptr, 900.0);
            if (!r) {
                c.critic.unavailableReason = "the Critic did not start: " + r.error().message;
                return false;
            }
            if (r->exitCode != 0 && r->exitCode != 5) {
                c.critic.unavailableReason =
                    r->exitCode == 4 ? fmt::format("the Critic is not running (exit 4; start it with `critic start "
                                                   "--daemon`): {}",
                                                   r->err)
                                     : fmt::format("the Critic exited {}: {}", r->exitCode, r->err);
                return false;
            }
            const json j = json::parse(r->out.substr(r->out.find('{') == std::string::npos ? 0 : r->out.find('{')),
                                       nullptr, false);
            job = j.is_object() ? j.value("job_id", std::string()) : std::string();
            if (job.empty()) {
                c.critic.unavailableReason = "the Critic printed no job id";
                return false;
            }
            return true;
        };
        if (!submit(originalDir, "original", c.critic.originalJob) ||
            !submit(optimizedDir, "optimized " + joinedLevers(c.levers), c.critic.optimizedJob)) {
            return;
        }
        auto r = runProcess(criticCompareCommand(eo.critic.string(), eo.criticUrl, c.critic.originalJob,
                                                 c.critic.optimizedJob),
                            root, nullptr, 600.0);
        if (!r || r->exitCode != 0) {
            c.critic.unavailableReason = r ? fmt::format("critic compare exited {}: {}", r->exitCode, r->err)
                                           : "critic compare did not start";
            return;
        }
        c.critic.comparison = json::parse(r->out, nullptr, false);
        c.critic.available = !c.critic.comparison.is_discarded();
        if (!c.critic.available) {
            c.critic.unavailableReason = "critic compare printed no JSON";
        }
    };

    // ---- ORIGINAL, and the floor ----
    const rendering::QualitySettings original =
        o.compare == "project" ? d.withoutCeilings : d.base;
    const fs::path originalDir = root / "original";
    const fs::path repeatDir = root / "original-repeat";
    log::info("phase 5: ORIGINAL (and again, for the self-difference floor), {} frames from {:.2f} s, into {}", frames,
              d.startPiece, root.string());
    if (!capture(original, originalDir) || !capture(original, repeatDir)) {
        return 2;
    }
    const AbVisual floor = compareDirs(originalDir, repeatDir, "floor");
    if (!floor.available) {
        record.notes.push_back("phase 5: the pictures could not be compared: " + floor.unavailableReason);
    }

    const auto measure = [&](const std::vector<std::string>& levers, const rendering::QualitySettings& optimized,
                             const rendering::QualitySettings& baseline) -> std::optional<AbComparison> {
        AbComparison c;
        c.levers = levers;
        c.label = "original vs optimized: " + joinedLevers(levers);
        const fs::path dir = root / ("optimized-" + slug(joinedLevers(levers)));
        if (!capture(optimized, dir)) {
            return std::nullopt;
        }
        c.framesDir = dir.string();
        c.visual = compareDirs(originalDir, dir, slug(joinedLevers(levers)));
        c.inFloor = withinFloor(c.visual, floor);
        c.visualVerdict = describeVisual(c.visual, floor, c.inFloor);
        c.timing = timeArms(baseline, optimized);
        log::info("phase 5: {}: {:.2f} -> {:.2f} ms ({}); {}", joinedLevers(levers), c.timing.baselineMs,
                  c.timing.optimizedMs, c.timing.verdict, c.visualVerdict);
        return c;
    };

    // ---- --compare ----
    if (!o.compare.empty()) {
        record.comparisonFloor = floor;
        std::vector<std::string> levers;
        rendering::QualitySettings optimized = d.base;
        if (o.compare == "project") {
            levers = {"(the project's live ceilings)"};
            if (!engine_->liveSettings().overrides) {
                record.notes.push_back("--compare project: the project has no live ceilings, so OPTIMIZED is ORIGINAL");
            }
        } else {
            std::stringstream ss(o.compare);
            for (std::string l; std::getline(ss, l, ',');) {
                if (!l.empty()) levers.push_back(l);
            }
            const auto q = withLevers(levers);
            if (!q) {
                log::error("--compare: '{}' names a lever with no form the project can keep (levers: volumequarter, "
                           "volumesteps, posttaps, nomotionblur, nodof, castercull, shadowatlas1k, lodbias2, drawdist75, "
                           "particlelod, scale85, scale71, pcss)",
                           o.compare);
                return 3;
            }
            optimized = *q;
        }
        auto c = measure(levers, optimized, original);
        if (!c) {
            return 2;
        }
        if (o.abCritic) {
            askCritic(*c, originalDir, c->framesDir);
        }
        record.comparisons.push_back(std::move(*c));
    }

    // ---- --optimize ----
    if (o.optimize) {
        OptimizationReport& opt = record.optimization;
        opt.ran = true;
        opt.targetFps = o.targetFps;
        opt.budgetMs = record.conditions.budgetMs;
        opt.margin = o.optimizeMargin;
        opt.targetMs = opt.budgetMs * opt.margin;
        opt.baselineMs = std::max(record.gpuMs.valid() ? record.gpuMs.p50 : 0.0, record.cpuWorkMs.p50);
        opt.alreadyUnder = opt.baselineMs <= opt.targetMs;
        opt.heroPolicy = heroPolicyFromToken(o.heroPolicy).value_or(HeroPolicy::Protect);
        opt.maxRisk = o.optimizeRisk;
        opt.floor = floor;
        // The rules' candidates, one per lever (two rules can name one lever), in the rules' order.
        int admitted = 0;
        for (const LiveProfileCandidate& k : record.candidates) {
            if (std::any_of(opt.candidates.begin(), opt.candidates.end(),
                            [&](const SearchCandidate& s) { return s.lever == k.lever; })) {
                continue;
            }
            SearchCandidate s;
            s.lever = k.lever;
            s.title = k.title;
            s.risk = k.risk;
            s.heroEffect = heroEffectOfLever(k.lever);
            s.estimatedLowMs = k.estimatedLowMs;
            s.estimatedHighMs = k.estimatedHighMs;
            s.admitted = leverAdmitted(k.lever, k.risk, opt.heroPolicy, opt.maxRisk, &s.refusal);
            if (s.admitted && admitted >= o.optimizeCandidates) {
                s.admitted = false;
                s.refusal = fmt::format("over --optimize-candidates {}", o.optimizeCandidates);
            }
            admitted += s.admitted ? 1 : 0;
            opt.candidates.push_back(std::move(s));
        }
        // The search's own extras (ADR-1111): the next render-scale step (high risk: half the pixels) and the
        // volume's other axis, which the rules do not offer while a cheaper form of the same lever is open.
        const auto addExtra = [&](const char* lever, const char* title, const char* risk, bool when) {
            if (!when || std::any_of(opt.candidates.begin(), opt.candidates.end(),
                                     [&](const SearchCandidate& s) { return s.lever == lever; })) {
                return;
            }
            SearchCandidate s;
            s.lever = lever;
            s.title = title;
            s.risk = risk;
            s.heroEffect = heroEffectOfLever(lever);
            s.admitted = leverAdmitted(lever, risk, opt.heroPolicy, opt.maxRisk, &s.refusal);
            if (s.admitted && admitted >= o.optimizeCandidates) {
                s.admitted = false;
                s.refusal = fmt::format("over --optimize-candidates {}", o.optimizeCandidates);
            }
            admitted += s.admitted ? 1 : 0;
            opt.candidates.push_back(std::move(s));
        };
        const bool hasVolume = std::any_of(record.gpu.begin(), record.gpu.end(), [](const GpuCategory& g) {
            return g.name == "volumetrics" && g.medianMs > 0.1;
        });
        addExtra("scale71", "Render resolution (half the pixels)", "high", d.base.renderScale > 0.72f);
        addExtra("volumesteps", "Volumetric march steps", "medium", hasVolume && d.base.volumeStepScale > 0.5f);
        if (opt.alreadyUnder) {
            opt.notes.push_back("the baseline is already under the target; singles are still measured so the low-risk "
                                "set is known");
        }
        for (SearchCandidate& s : opt.candidates) {
            if (!s.admitted) {
                continue;
            }
            auto c = measure({s.lever}, *withLevers({s.lever}), d.base);
            if (!c) {
                return 2;
            }
            s.single = std::move(*c);
            if (usableSingle(s)) {
                opt.measuredCombos.push_back(s.single); // a measured single is a candidate answer too
            }
        }
        opt.lowRisk = lowRiskSet(opt.candidates);
        if (!opt.alreadyUnder) {
            opt.plans = planCombinations(opt.candidates, opt.baselineMs, opt.targetMs, 3);
            // Round 1: the three best-ranked plans, MEASURED as combinations.
            for (std::size_t k = 0; k < std::min<std::size_t>(3, opt.plans.size()); ++k) {
                auto c = measure(opt.plans[k].levers, *withLevers(opt.plans[k].levers), d.base);
                if (!c) {
                    return 2;
                }
                opt.measuredCombos.push_back(std::move(*c));
            }
            chooseCombination(opt);
            // Round 2: nothing reached -- every usable single together (one render-scale lever, the lowest).
            if (!opt.reached) {
                std::vector<std::string> all;
                for (const SearchCandidate& s : opt.candidates) {
                    if (usableSingle(s)) all.push_back(s.lever);
                }
                if (std::count(all.begin(), all.end(), "scale85") > 0 && std::count(all.begin(), all.end(), "scale71") > 0) {
                    all.erase(std::find(all.begin(), all.end(), "scale85"));
                }
                if (all.size() > 3) {
                    opt.notes.push_back("round 2: no measured plan reached the target; every usable lever was measured "
                                        "together");
                    auto c = measure(all, *withLevers(all), d.base);
                    if (!c) {
                        return 2;
                    }
                    opt.measuredCombos.push_back(std::move(*c));
                }
            }
        }
        chooseCombination(opt);
        if (o.abCritic) {
            if (opt.chosenIndex >= 0) {
                AbComparison& chosen = opt.measuredCombos[static_cast<std::size_t>(opt.chosenIndex)];
                askCritic(chosen, originalDir, chosen.framesDir);
            } else {
                opt.notes.push_back("--ab-critic: nothing was chosen, so the Critic was not asked");
            }
        }
    }
    record.notes.push_back(fmt::format("phase 5: frames and the Lab's answers are in {}", root.string()));
    return 0;
}

} // namespace avgen::app
