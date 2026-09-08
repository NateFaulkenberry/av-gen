#include "app/engine.hpp"

#include "assets/image.hpp"
#include "core/log.hpp"
#include "params/serialization.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>

namespace avgen::app {

Engine::Engine(EngineMode mode) : mode_(mode) {
    audioSignals_ = signals::AudioSignals::declare(bus_);
    timeSignals_.seconds = bus_.declare("time.seconds", 0.0f, 3600.0f);
    timeSignals_.progress = bus_.declare("time.progress");
    timeSignals_.playing = bus_.declare("time.playing");
    timeSignals_.beatPhase = bus_.declare("beat.phase");
    timeSignals_.beatPulse = bus_.declare("beat.pulse", 0.0f, 1.0f, true);
    timeSignals_.beatCount = bus_.declare("beat.count", 0.0f, 100000.0f);
    timeSignals_.bpm = bus_.declare("beat.bpm", 0.0f, 300.0f);
    timeSignals_.barPhase = bus_.declare("beat.bar");
    sources_.attach(bus_, params_);
    installController(std::make_unique<scene::OrbScene>(params_, modulator_));
    if (mode_ == EngineMode::Live) {
        player_ = std::make_unique<audio::AudioPlayer>();
    }
}

void Engine::installController(std::unique_ptr<scene::SceneController> controller) {
    controller_ = std::move(controller);
    // Scene swaps clear the parameter set, so sources must re-register their parameters.
    sources_.attach(bus_, params_);
    rebind();
    modulator_.resetState();
}

void Engine::rebind() {
    if (auto r = modulator_.bind(bus_, params_); !r) {
        log::warn("modulation bind: {}", r.error().message);
    }
}

signals::Source& Engine::addSource(const std::string& kind, const std::string& baseName) {
    std::string name = baseName;
    for (int i = 2; sources_.find(kind, name) != nullptr; ++i) {
        name = baseName + std::to_string(i);
    }
    auto source = signals::SourceRack::create(kind, name);
    if (!source) {
        log::error("unknown source kind '{}'", kind);
        source = signals::SourceRack::create("lfo", name);
    }
    auto& ref = sources_.add(std::move(source));
    rebind();
    return ref;
}

void Engine::removeSource(const std::string& kind, const std::string& name) {
    if (sources_.remove(kind, name)) {
        rebind();
    }
}

params::Preset& Engine::storePreset(const std::string& name) {
    return presets_.add(params::capturePreset(params_, name));
}

bool Engine::recallPreset(const std::string& name) {
    const auto* preset = presets_.find(name);
    if (preset == nullptr) {
        return false;
    }
    params::applyPreset(params_, *preset);
    return true;
}

void Engine::morphPresets(const std::string& a, const std::string& b, float t) {
    const auto* pa = presets_.find(a);
    const auto* pb = presets_.find(b);
    if (pa == nullptr || pb == nullptr) {
        return;
    }
    params::applyPresetBlend(params_, *pa, *pb, t);
}

Result<void> Engine::saveProject(const std::filesystem::path& path) const {
    return params::saveProjectFile(path, params_, modulator_, &sources_, &presets_);
}

Result<void> Engine::loadProject(const std::filesystem::path& path) {
    if (auto r = params::loadProjectFile(path, params_, modulator_, &sources_, &presets_); !r) {
        return r;
    }
    sources_.attach(bus_, params_);
    // Parameter values for sources arrive in the same document; apply them again now that the
    // sources' parameters exist (unknown-at-first-pass paths were skipped).
    if (auto r = params::loadProjectFile(path, params_, modulator_, nullptr, nullptr); !r) {
        return r;
    }
    rebind();
    modulator_.resetState();
    projectPath_ = path;
    log::info("project '{}' loaded: {} parameters, {} routes, {} sources, {} presets", path.filename().string(),
              params_.size(), modulator_.routes().size(), sources_.sources().size(), presets_.presets().size());
    return {};
}

Result<void> Engine::loadScene(const std::filesystem::path& path) {
    // Import first so a failed load leaves the current scene, parameters and routes untouched.
    auto ctrl = scene::GltfScene::load(path);
    if (!ctrl) {
        return std::unexpected(ctrl.error());
    }
    const float masterGain = modulator_.masterGain;
    params_.clear();
    modulator_.clearRoutes();
    modulator_.masterGain = masterGain;
    (*ctrl)->attach(params_, modulator_);
    installController(std::move(*ctrl));
    return reapplyEnvironment();
}

void Engine::loadOrbScene() {
    params_.clear();
    modulator_.clearRoutes();
    installController(std::make_unique<scene::OrbScene>(params_, modulator_));
    if (auto r = reapplyEnvironment(); !r) {
        log::warn("environment: {}", r.error().message);
    }
}

Result<void> Engine::reapplyEnvironment() {
    if (environmentPath_.empty()) {
        return {};
    }
    auto image = assets::loadImage(environmentPath_, false);
    if (!image) {
        return std::unexpected(image.error());
    }
    if (!image->isHdr()) {
        return fail("'{}' is not an HDR image", environmentPath_.string());
    }
    auto& sc = controller_->scene();
    sc.environment.environmentMap = sc.addTexture(std::move(*image));
    return {};
}

Result<void> Engine::loadEnvironment(const std::filesystem::path& path) {
    auto image = assets::loadImage(path, false);
    if (!image) {
        return std::unexpected(image.error());
    }
    if (!image->isHdr()) {
        return fail("'{}' is not an HDR (Radiance .hdr) image", path.string());
    }
    auto& sc = controller_->scene();
    sc.environment.environmentMap = sc.addTexture(std::move(*image));
    environmentPath_ = path;
    log::info("environment map '{}' installed", path.filename().string());
    return {};
}

Result<void> Engine::loadFile(const std::filesystem::path& path) {
    std::string ext = path.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (ext == ".gltf" || ext == ".glb") {
        return loadScene(path);
    }
    if (ext == ".hdr") {
        return loadEnvironment(path);
    }
    if (ext == ".json") {
        return loadProject(path);
    }
    auto duration = loadAudio(path);
    if (!duration) {
        return std::unexpected(duration.error());
    }
    return {};
}

Engine::~Engine() {
    runner_.reset();
    player_.reset();
}

Result<double> Engine::loadAudio(const std::filesystem::path& path) {
    auto file = audio::AudioFile::load(path);
    if (!file) {
        return std::unexpected(file.error());
    }
    auto shared = std::make_shared<const audio::AudioFile>(std::move(*file));
    analyzerConfig_.sampleRate = shared->sampleRate();

    if (mode_ == EngineMode::Live) {
        runner_.reset();
        if (auto r = player_->setSource(shared); !r) {
            return std::unexpected(r.error());
        }
        runner_ = std::make_unique<analysis::AnalysisRunner>(analyzerConfig_, player_->analysisStream());
        runner_->start();
    } else {
        track_ = std::make_unique<analysis::AnalysisTrack>(analysis::AnalysisTrack::analyze(*shared, analyzerConfig_));
        offlineFrameCursor_ = 0;
        log::info("offline analysis: {} frames", track_->frames().size());
    }
    audioFile_ = shared;
    audioPath_ = path;
    modulator_.resetState();
    hasFrame_ = false;
    return shared->durationSeconds();
}

Result<void> Engine::play() {
    if (!player_ || !player_->hasSource()) {
        return fail("no audio loaded");
    }
    return player_->play();
}

void Engine::pause() {
    if (player_) {
        player_->pause();
    }
}

void Engine::togglePlay() {
    if (isPlaying()) {
        pause();
    } else if (auto r = play(); !r) {
        log::warn("play: {}", r.error().message);
    }
}

void Engine::stop() {
    if (player_) {
        player_->stop();
        modulator_.resetState();
        sources_.reset();
        beatClockPhase_ = 0.0;
    }
}

void Engine::seekSeconds(double seconds) {
    if (player_) {
        player_->seekSeconds(seconds);
    } else if (track_) {
        offlineFrameCursor_ = 0;
    }
    modulator_.resetState();
    sources_.reset();
    beatClockPhase_ = 0.0;
    lastAnalysisBeatCount_ = 0;
}

bool Engine::isPlaying() const { return player_ && player_->isPlaying(); }

double Engine::positionSeconds() const {
    if (player_) {
        return player_->positionSeconds();
    }
    return lastRenderTime_;
}

double Engine::durationSeconds() const { return audioFile_ ? audioFile_->durationSeconds() : 0.0; }

void Engine::setVolume(float volume) {
    if (player_) {
        player_->setVolume(volume);
    }
}

float Engine::volume() const { return player_ ? player_->volume() : 1.0f; }

FrameTime Engine::tick(FrameClock& clock) {
    FrameTime time = clock.tick();
    if (mode_ == EngineMode::Live && isPlaying()) {
        // Audio is the master clock while playing (ADR-012); dt stays wall-derived for smooth
        // integration because the play-head advances in device-period steps.
        time.renderTime = player_->positionSeconds();
        clock.seek(time.renderTime);
    }
    lastRenderTime_ = time.renderTime;
    return time;
}

void Engine::publishFrame(const analysis::AnalysisFrame& frame) {
    latest_ = frame;
    hasFrame_ = true;
    audioSignals_.publish(bus_, frame);
}

void Engine::updateTimeSignals(const FrameTime& time, bool newAnalysisFrame) {
    const double bpm = hasFrame_ ? static_cast<double>(latest_.tempoBpm) : 0.0;
    bool pulse = false;
    if (bpm > 0.0) {
        // Advance the per-frame beat clock; re-sync to the analyser whenever it reports a beat.
        beatClockPhase_ += time.deltaTime * bpm / 60.0;
        if (newAnalysisFrame && latest_.beatCount != lastAnalysisBeatCount_) {
            beatClockPhase_ = static_cast<double>(latest_.beatPhase);
            beatClockCount_ = latest_.beatCount;
            lastAnalysisBeatCount_ = latest_.beatCount;
            pulse = true;
        } else if (beatClockPhase_ >= 1.0) {
            beatClockPhase_ -= 1.0;
            ++beatClockCount_;
            pulse = true;
        }
    } else {
        beatClockPhase_ = 0.0;
    }
    const double duration = durationSeconds();
    bus_.set(timeSignals_.seconds, static_cast<float>(time.renderTime));
    bus_.set(timeSignals_.progress, duration > 0.0 ? static_cast<float>(std::clamp(positionSeconds() / duration, 0.0, 1.0)) : 0.0f);
    bus_.set(timeSignals_.playing, isPlaying() ? 1.0f : 0.0f);
    bus_.set(timeSignals_.beatPhase, static_cast<float>(beatClockPhase_));
    bus_.setEvent(timeSignals_.beatPulse, pulse, 1.0f);
    bus_.set(timeSignals_.beatCount, static_cast<float>(beatClockCount_));
    bus_.set(timeSignals_.bpm, static_cast<float>(bpm));
    bus_.set(timeSignals_.barPhase, static_cast<float>((beatClockCount_ % 4 + beatClockPhase_) / 4.0));

    sourceContext_.time = time;
    sourceContext_.audioPosition = positionSeconds();
    sourceContext_.audioDuration = duration;
    sourceContext_.playing = isPlaying();
    sourceContext_.beatPhase = static_cast<float>(beatClockPhase_);
    sourceContext_.beatCount = beatClockCount_;
    sourceContext_.tempoBpm = static_cast<float>(bpm);
    sourceContext_.beatEvent = pulse;
}

void Engine::update(const FrameTime& time) {
    const auto start = std::chrono::steady_clock::now();
    bool newFrame = false;

    if (mode_ == EngineMode::Live) {
        if (runner_ && runner_->acquire()) {
            newFrame = true;
            publishFrame(runner_->latest());
            stats_.analysisHopMicros = runner_->averageHopMicros();
            stats_.analysisFrames = runner_->framesProduced();
        } else if (hasFrame_) {
            // No new analysis this render frame: keep continuous values, drop the event pulse.
            bus_.setEvent(audioSignals_.onset, false);
        } else {
            audioSignals_.publishSilence(bus_);
        }
    } else if (track_ && !track_->empty()) {
        // Consume every analysis frame whose centre lies at or before renderTime so onsets that
        // fall between two render frames are not lost at low frame rates.
        const auto& frames = track_->frames();
        bool onset = false;
        float onsetStrength = 0.0f;
        std::size_t cursor = offlineFrameCursor_;
        while (cursor < frames.size() && frames[cursor].timeSeconds <= time.renderTime) {
            if (frames[cursor].onset) {
                onset = true;
                onsetStrength = std::max(onsetStrength, frames[cursor].onsetStrength);
            }
            ++cursor;
        }
        if (cursor > offlineFrameCursor_) {
            analysis::AnalysisFrame frame = frames[cursor - 1];
            frame.onset = onset;
            if (onset) {
                frame.onsetStrength = onsetStrength;
            }
            publishFrame(frame);
            newFrame = true;
            offlineFrameCursor_ = cursor;
            stats_.analysisFrames = cursor;
        } else if (hasFrame_) {
            bus_.setEvent(audioSignals_.onset, false);
        } else {
            audioSignals_.publishSilence(bus_);
        }
    } else {
        audioSignals_.publishSilence(bus_);
    }

    updateTimeSignals(time, newFrame);
    sources_.update(bus_, sourceContext_);
    modulator_.evaluate(bus_, params_, time.deltaTime);
    controller_->update(time);
    bus_.clearEvents();

    const auto end = std::chrono::steady_clock::now();
    const double micros = std::chrono::duration<double, std::micro>(end - start).count();
    stats_.modulationMicros = stats_.modulationMicros * 0.9 + micros * 0.1;
}

params::ModRoute* Engine::routeForTarget(const std::string& path) {
    for (auto& route : modulator_.routes()) {
        if (route.target == path) {
            return &route;
        }
    }
    return nullptr;
}

} // namespace avgen::app
