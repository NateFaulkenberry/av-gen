#include "app/directing_record.hpp"

#include "app/directing_context.hpp"
#include "app/engine.hpp"
#include "app/render_source.hpp"
#include "core/log.hpp"
#include "core/time.hpp"
#include "directing/performance.hpp"
#include "entity/entity.hpp"
#include "scene/composition.hpp"

#include <fmt/format.h>
#include <fmt/ranges.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <map>
#include <numbers>
#include <system_error>
#include <utility>
#include <unistd.h>

namespace avgen::app {
namespace {

double since(std::chrono::steady_clock::time_point start) {
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
}

void frame(Engine& engine, std::uint64_t f) {
    engine.update(FrameTime{static_cast<double>(f) / 60.0, f == 0 ? 0.0 : 1.0 / 60.0, f});
}

// A scratch engine loaded from the recording's copy, set up the way a recording and its check must
// be: no live control, no audio, every body simulated. Worker-safe: it touches nothing but the file.
Result<std::unique_ptr<Engine>> loadCopy(const std::filesystem::path& copy) {
    auto engine = std::make_unique<Engine>(EngineMode::Offline);
    engine->setLiveControl(false);
    if (auto loaded = engine->loadProject(copy); !loaded) {
        return fail("record: the scratch copy does not load: {}", loaded.error().message);
    }
    scene::DetailLimits limits = engine->detailLimits();
    limits.entityDistanceCull = false;
    engine->setDetailLimits(limits);
    if (auto r = engine->setAudioClips({}); !r) {
        return fail("record: cannot silence the scratch copy: {}", r.error().message);
    }
    return engine;
}

struct Window {
    std::size_t performance = 0;
    std::string entity;
    std::string node;
    double from = 0.0;
    double until = 0.0;
    std::vector<std::string> events; // the plan events its beats emit
};

} // namespace

Result<std::filesystem::path> writeRecordingCopy(Engine& live, const std::filesystem::path& scratchDir) {
    static std::uint64_t serial = 0;
    const std::filesystem::path dir = scratchDir.empty() ? std::filesystem::temp_directory_path() : scratchDir;
    const RenderSource source = renderSourceFor(live.projectPath(), dir, "director_record", ++serial,
                                                static_cast<long long>(::getpid()));
    if (auto r = live.writeProjectCopy(source.scratch); !r) {
        return fail("record: cannot write the scratch copy: {}", r.error().message);
    }
    return source.scratch;
}

Result<RecordReport> recordLivePerformances(Engine& live, const directing::Compilation& compilation,
                                            const RecordOptions& options) {
    auto copy = writeRecordingCopy(live, options.scratchDir);
    if (!copy) {
        return std::unexpected(copy.error());
    }
    auto report = recordFromCopy(*copy, compilation, options);
    std::error_code ec;
    std::filesystem::remove(*copy, ec);
    return report;
}

Result<RecordReport> recordFromCopy(const std::filesystem::path& copy, const directing::Compilation& compilation,
                                    const RecordOptions& options, const RecordProgress& progress) {
    using namespace directing;
    RecordReport report;
    report.plan = compilation.plan;
    const auto say = [&](const std::string& phase) {
        if (progress) {
            progress(phase);
        }
    };
    const auto cancelled = [&] { return options.cancel != nullptr && options.cancel->load(); };

    // ---- what to record: facts from the copy, which is the project as it was when asked ------------
    say("loading a scratch copy of the project");
    auto scratch = loadCopy(copy);
    if (!scratch) {
        return std::unexpected(scratch.error());
    }
    std::vector<Window> windows;
    const SceneFacts facts = sceneFactsFor(**scratch);
    for (std::size_t i = 0; i < compilation.plan.performances.size(); ++i) {
        const PlanPerformance& p = compilation.plan.performances[i];
        if (p.mode != PerformanceMode::Goal || p.recording || compilation.validation.isBlocked(p.key)) {
            continue;
        }
        Window w;
        w.performance = i;
        w.entity = compilation.plan.subject(p.subject)->id;
        const CharacterMark* mark = facts.character(w.entity);
        w.node = mark != nullptr && mark->node != w.entity ? mark->node : std::string();
        double last = 0.0;
        for (std::size_t b = 0; b < p.beats.size(); ++b) {
            const double t = *goalBeatTime(compilation.plan, i, b, compilation.validation.times);
            w.from = b == 0 ? t : std::min(w.from, t);
            last = std::max(last, t);
            if (!p.beats[b].emits.empty()) {
                w.events.push_back(p.beats[b].emits);
            }
        }
        w.until = last + options.maxSeconds; // shortened below to the last event heard
        windows.push_back(std::move(w));
    }
    if (windows.empty()) {
        return fail("record: the plan has no live performance to record");
    }

    // ---- play it, from zero, and keep what happened -------------------------------------------------
    auto start = std::chrono::steady_clock::now();
    Engine& engine = **scratch;
    if (auto r = installCompilation(engine, compilation); !r) {
        return fail("record: the plan does not install on the scratch copy: {}", r.error().message);
    }
    struct Sample {
        double t;
        glm::vec3 position;
        float yawDegrees;
        scene::Composition::ClipReadout clip;
    };
    std::vector<std::vector<Sample>> samples(windows.size());
    std::vector<std::map<std::string, double>> heard(windows.size());
    double end = 0.0;
    for (const Window& w : windows) {
        end = std::max(end, w.until);
    }
    const auto lastFrame = static_cast<std::uint64_t>(std::ceil(end * 60.0));
    std::uint64_t recordedFrames = 0;
    for (std::uint64_t f = 0; f <= lastFrame; ++f) {
        if (cancelled()) {
            return fail("record: cancelled");
        }
        frame(engine, f);
        recordedFrames = f;
        const double t = static_cast<double>(f) / 60.0;
        if (f % 60 == 0) {
            say(fmt::format("recording: playing {:.0f} of {:.0f} s", t, end));
        }
        for (const seq::FiredEvent& fired : engine.firedEvents()) {
            const auto& events = engine.sequence().events;
            if (fired.eventIndex >= events.size() || events[fired.eventIndex].what.kind != seq::EventActionKind::Notify) {
                continue;
            }
            for (std::size_t k = 0; k < windows.size(); ++k) {
                const std::string& name = events[fired.eventIndex].what.target;
                if (std::find(windows[k].events.begin(), windows[k].events.end(), name) != windows[k].events.end() &&
                    !heard[k].contains(name)) {
                    heard[k][name] = fired.timeSeconds;
                }
            }
        }
        bool allDone = true;
        for (std::size_t k = 0; k < windows.size(); ++k) {
            Window& w = windows[k];
            if (heard[k].size() == w.events.size() && !w.events.empty()) {
                double lastHeard = 0.0;
                for (const auto& [name, at] : heard[k]) {
                    lastHeard = std::max(lastHeard, at);
                }
                w.until = std::min(w.until, lastHeard + options.tailSeconds);
            }
            if (t >= w.from - 1e-9 && t <= w.until + 1e-9) {
                const entity::Entity* body = engine.composition()->entityWorld().find(w.entity);
                if (body == nullptr) {
                    return fail("record: '{}' is not in the scratch copy", w.entity);
                }
                const std::string node = w.node.empty() ? w.entity : w.node;
                samples[k].push_back(Sample{t, body->state().position(),
                                            body->state().yaw * 180.0f / std::numbers::pi_v<float>,
                                            engine.composition()->clipReadout(node, t)});
            }
            allDone = allDone && t > w.until;
        }
        if (allDone) {
            break;
        }
    }
    report.recordMs = since(start);

    // ---- the recordings -----------------------------------------------------------------------------
    for (std::size_t k = 0; k < windows.size(); ++k) {
        const Window& w = windows[k];
        const std::vector<Sample>& s = samples[k];
        if (s.size() < 2) {
            return fail("record: '{}' produced no samples", w.entity);
        }
        seq::Actor actor;
        actor.id = w.entity;
        actor.node = w.node;
        const auto every = std::max<std::size_t>(1, static_cast<std::size_t>(std::llround(options.keyEverySeconds * 60.0)));
        float unwrapped = s.front().yawDegrees;
        float previous = s.front().yawDegrees;
        std::string clip;
        bool airborne = false;
        double airFrom = 0.0;
        for (std::size_t i = 0; i < s.size(); ++i) {
            // Yaw unwrapped, so a turn through 180 degrees is interpolated the short way.
            float d = s[i].yawDegrees - previous;
            d -= 360.0f * std::round(d / 360.0f);
            unwrapped += d;
            previous = s[i].yawDegrees;
            if (i % every == 0 || i + 1 == s.size()) {
                seq::ActorKey key;
                key.timeSeconds = s[i].t;
                key.position = s[i].position;
                key.rotationDegrees = glm::vec3(0.0f, unwrapped, 0.0f);
                key.interp = params::KeyInterp::Linear;
                actor.keys.push_back(key);
            }
            // The clip, as it changed: a cue per state change, starting the clip where it was.
            if (s[i].clip.found && s[i].clip.state != clip) {
                clip = s[i].clip.state;
                seq::ClipCue cue;
                cue.timeSeconds = s[i].t;
                cue.clip = clip;
                cue.speed = s[i].clip.speed;
                cue.offsetSeconds = s[i].clip.clipSeconds;
                cue.playback = s[i].clip.loops ? seq::ClipPlayback::Loop : seq::ClipPlayback::Once;
                actor.clips.push_back(cue);
            }
            // Off the ground, where the body was: kept as an airborne span so the performer holds its
            // height rather than the terrain's (ADR-822).
            const bool up = facts.groundAt && s[i].position.y - facts.groundAt(s[i].position.x, s[i].position.z) > 0.15f;
            if (up && !airborne) {
                airborne = true;
                airFrom = s[i].t;
            } else if (!up && airborne) {
                airborne = false;
                actor.airborne.emplace_back(airFrom, s[i].t);
            }
        }
        if (airborne) {
            actor.airborne.emplace_back(airFrom, s.back().t);
        }
        PlanPerformance& p = report.plan.performances[w.performance];
        PerformanceRecording recording;
        recording.actor = actorDocument(actor);
        recording.fromMode = performanceModeName(p.mode);
        for (const auto& [name, at] : heard[k]) {
            recording.events.emplace_back(name, at);
        }
        p.recording = std::move(recording);
        std::vector<std::string> missing;
        for (const std::string& e : w.events) {
            if (!heard[k].contains(e)) {
                missing.push_back(e);
            }
        }
        report.notes.push_back(fmt::format("{}: recorded {:.2f}-{:.2f}s, {} keys, {} clip cue(s), {} event(s){}", w.entity,
                                           s.front().t, s.back().t, actor.keys.size(), actor.clips.size(),
                                           heard[k].size(),
                                           missing.empty() ? std::string()
                                                           : fmt::format("; never heard: {}", fmt::join(missing, ", "))));
    }
    // Every live performance is recorded: nothing in the plan depends on live state any more.
    bool allRecorded = true;
    for (const PlanPerformance& p : report.plan.performances) {
        allRecorded = allRecorded && (p.mode == PerformanceMode::Scripted || p.recording.has_value());
    }
    if (allRecorded) {
        report.plan.tier = Tier::Baked;
    }

    // ---- the check: play the recording back, and scrub into it --------------------------------------
    start = std::chrono::steady_clock::now();
    say("checking: playing the recording back");
    auto check = loadCopy(copy);
    if (!check) {
        return std::unexpected(check.error());
    }
    Plan recorded = report.plan;
    recorded.produced.clear();
    const Compilation baked = compilePlan(recorded, sceneFactsFor(**check));
    if (baked.validation.hasErrors()) {
        return fail("record: the recorded plan does not validate: {}", baked.diffText());
    }
    if (auto r = installCompilation(**check, baked); !r) {
        return fail("record: the recorded plan does not install: {}", r.error().message);
    }
    std::vector<double> probes;
    for (std::size_t k = 0; k < windows.size(); ++k) {
        probes.push_back((samples[k].front().t + samples[k].back().t) * 0.5);
    }
    std::map<std::uint64_t, std::map<std::string, glm::vec3>> playedAt;
    const auto drawn = [](const Engine& e) {
        std::map<std::string, glm::vec3> out;
        for (const auto& body : e.composition()->entityWorld().entities()) {
            out[body->name()] = body->visualPosition();
        }
        return out;
    };
    for (std::uint64_t f = 0; f <= recordedFrames; ++f) {
        if (cancelled()) {
            return fail("record: cancelled");
        }
        frame(**check, f);
        const double t = static_cast<double>(f) / 60.0;
        for (std::size_t k = 0; k < windows.size(); ++k) {
            const auto& keys = recordedActor(*report.plan.performances[windows[k].performance].recording)->keys;
            const auto hit = std::find_if(keys.begin(), keys.end(), [&](const seq::ActorKey& key) {
                return std::abs(key.timeSeconds - t) < 1e-6;
            });
            if (hit != keys.end()) {
                const glm::vec3 body = (*check)->composition()->entityWorld().find(windows[k].entity)->state().position();
                const glm::vec3 d = body - hit->position;
                report.replayWorstMetres = std::max(report.replayWorstMetres,
                                                    static_cast<double>(glm::length(glm::vec2(d.x, d.z))));
            }
        }
        for (const double probe : probes) {
            if (f == static_cast<std::uint64_t>(std::llround(probe * 60.0)) + 1) {
                playedAt[f] = drawn(**check);
            }
        }
    }
    for (const auto& [f, played] : playedAt) {
        say("checking: a scrub into the recording");
        auto scrub = loadCopy(copy);
        if (!scrub) {
            return std::unexpected(scrub.error());
        }
        if (auto r = installCompilation(**scrub, baked); !r) {
            return fail("record: the recorded plan does not install for the scrub: {}", r.error().message);
        }
        frame(**scrub, 0);
        (*scrub)->seekSeconds(static_cast<double>(f - 1) / 60.0);
        frame(**scrub, f);
        for (const auto& [name, position] : drawn(**scrub)) {
            if (const auto it = played.find(name); it != played.end()) {
                report.scrubWorstMetres = std::max(report.scrubWorstMetres, static_cast<double>(glm::length(position - it->second)));
            }
        }
    }
    report.checkMs = since(start);
    say("done");
    for (std::size_t k = 0; k < windows.size(); ++k) {
        report.plan.performances[windows[k].performance].recording->replayWorstMetres = report.replayWorstMetres;
    }
    log::info("director record: {} performance(s) in {:.0f} ms; played back {:.4f} m from the recording, scrubbed "
              "{:.4f} m from the play ({:.0f} ms)",
              windows.size(), report.recordMs, report.replayWorstMetres, report.scrubWorstMetres, report.checkMs);
    return report;
}

RecordingJob::~RecordingJob() {
    cancel();
    finish();
}

void RecordingJob::finish() {
    if (thread_.joinable()) {
        thread_.join();
    }
    if (!copy_.empty()) {
        std::error_code ec;
        std::filesystem::remove(copy_, ec);
        copy_.clear();
    }
}

Result<void> RecordingJob::start(Engine& live, directing::Compilation compilation, RecordOptions options) {
    if (running()) {
        return fail("a recording is already running");
    }
    finish();
    auto copy = writeRecordingCopy(live, options.scratchDir);
    if (!copy) {
        return std::unexpected(copy.error());
    }
    copy_ = *copy;
    cancel_ = false;
    done_ = false;
    {
        std::lock_guard lock(mutex_);
        result_.reset();
        phase_ = "starting";
    }
    options.cancel = &cancel_;
    thread_ = std::thread([this, compilation = std::move(compilation), options = std::move(options)] {
        auto r = recordFromCopy(copy_, compilation, options, [this](const std::string& phase) {
            std::lock_guard lock(mutex_);
            phase_ = phase;
        });
        {
            std::lock_guard lock(mutex_);
            result_ = std::move(r);
        }
        done_ = true;
    });
    return {};
}

bool RecordingJob::running() const { return thread_.joinable() && !done_.load(); }

std::string RecordingJob::phase() const {
    std::lock_guard lock(mutex_);
    return phase_;
}

std::optional<Result<RecordReport>> RecordingJob::take() {
    if (!done_.load()) {
        return std::nullopt;
    }
    finish();
    std::lock_guard lock(mutex_);
    return std::exchange(result_, std::nullopt);
}

void RecordingJob::cancel() { cancel_ = true; }

namespace {

class JobHandle final : public ai::RecordingHandle {
public:
    [[nodiscard]] bool done() const override { return job.finished(); }
    [[nodiscard]] std::string phase() const override { return job.phase(); }
    [[nodiscard]] Result<directing::Plan> take() override {
        auto r = job.take();
        if (!r) {
            return fail("the recording has not finished");
        }
        if (!*r) {
            return std::unexpected(r->error());
        }
        return (*r)->plan;
    }
    void cancel() override { job.cancel(); }
    RecordingJob job;
};

} // namespace

ai::RecordingHook makeRecordingHook(RecordOptions options) {
    return [options](Engine& engine, const directing::Compilation& c) -> Result<std::shared_ptr<ai::RecordingHandle>> {
        auto handle = std::make_shared<JobHandle>();
        if (auto r = handle->job.start(engine, c, options); !r) {
            return std::unexpected(r.error());
        }
        return handle;
    };
}

} // namespace avgen::app
