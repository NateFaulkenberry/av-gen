#include "app/engine.hpp"

#include "core/log.hpp"

#include <algorithm>
#include <chrono>

namespace avgen::app {

Engine::Engine(EngineMode mode) : mode_(mode), orbScene_(params_, modulator_) {
    audioSignals_ = signals::AudioSignals::declare(bus_);
    if (auto r = modulator_.bind(bus_, params_); !r) {
        log::error("modulation bind: {}", r.error().message);
    }
    if (mode_ == EngineMode::Live) {
        player_ = std::make_unique<audio::AudioPlayer>();
    }
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
    }
}

void Engine::seekSeconds(double seconds) {
    if (player_) {
        player_->seekSeconds(seconds);
        modulator_.resetState();
    } else if (track_) {
        offlineFrameCursor_ = 0;
        modulator_.resetState();
    }
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

void Engine::update(const FrameTime& time) {
    const auto start = std::chrono::steady_clock::now();

    if (mode_ == EngineMode::Live) {
        if (runner_ && runner_->acquire()) {
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

    modulator_.evaluate(bus_, params_, time.deltaTime);
    orbScene_.update(time);
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
