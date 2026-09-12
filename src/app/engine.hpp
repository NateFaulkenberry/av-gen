#pragma once

// The audiovisual pipeline (architecture.md): AudioPlayer -> AnalysisRunner -> SignalBus ->
// Modulator -> ParameterSet -> OrbScene -> Scene. The renderer and UI sit beside it and only
// read the Scene / parameters. Two modes (ADR-012): Live (audio device is the clock, analysis
// runs on a thread) and Offline (fixed-step clock, analysis precomputed and indexed by time).

#include "analysis/analysis_runner.hpp"
#include "app/control_hub.hpp"
#include "app/music_runtime.hpp"
#include "app/render_settings.hpp"
#include "app/scene_states.hpp"
#include "app/transport.hpp"
#include "app/world_director.hpp"
#include "audio/audio_input.hpp"
#include "analysis/analysis_track.hpp"
#include "analysis/analyzer.hpp"
#include "audio/audio_file.hpp"
#include "audio/audio_player.hpp"
#include "comp/layer_stack.hpp"
#include "core/error.hpp"
#include "core/time.hpp"
#include "params/modulation.hpp"
#include "params/parameter_set.hpp"
#include "params/preset.hpp"
#include "params/timeline.hpp"
#include "assets/asset_registry.hpp"
#include "scene/composition.hpp"
#include "scene/gltf_scene.hpp"
#include "scene/orb_scene.hpp"
#include "scene/camera.hpp"
#include "scene/post_settings.hpp"
#include "scene/scene_controller.hpp"
#include "seq/director.hpp"
#include "seq/sequence.hpp"
#include "shaders/shader_layers.hpp"
#include "signals/audio_signals.hpp"
#include "signals/signal_bus.hpp"
#include "signals/source.hpp"

#include <nlohmann/json.hpp>

#include <filesystem>
#include <memory>
#include <optional>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace avgen::app {

enum class EngineMode { Live, Offline };

// Where the beat clock (beat.* signals, SourceContext tempo fields, the timeline's beat time)
// comes from: the audio analyser, or an incoming MIDI clock (ADR-021 follow-up). With MidiClock
// selected but no running clock the analyser is used; the analyser keeps running either way.
enum class TempoSource { Analysis, MidiClock };
[[nodiscard]] const char* tempoSourceName(TempoSource source);          // "analysis" | "midi"
[[nodiscard]] std::optional<TempoSource> tempoSourceFromName(std::string_view name);

struct EngineStats {
    double analysisHopMicros = 0.0;
    std::uint64_t analysisFrames = 0;
    double modulationMicros = 0.0;
    // Where the per-frame update's heap allocations come from, counted with the interposed
    // counters in core/phase_profiler.hpp. An idle editor that allocates hundreds of times a frame
    // is doing work it did not have to; knowing the total without knowing the source only tells
    // you that. Counts for the last frame, not an average -- an average of an allocation count
    // hides the frame that allocated ten thousand times.
    std::uint32_t allocsControl = 0;   // MIDI/OSC drain, control hub
    std::uint32_t allocsSignals = 0;   // time signals, music, timeline clock, cues, states
    std::uint32_t allocsModulation = 0; // parameter finals, timeline automation, routes
    std::uint32_t allocsController = 0; // the scene controller: composition update and rebuilds
    std::uint32_t allocsOther = 0;
};

class Engine {
public:
    explicit Engine(EngineMode mode);
    ~Engine();
    Engine(const Engine&) = delete;
    Engine& operator=(const Engine&) = delete;

    [[nodiscard]] EngineMode mode() const { return mode_; }

    // Decodes the file. Live: installs it in the player and starts the analysis thread.
    // Offline: precomputes the analysis track. Returns the decoded duration.
    [[nodiscard]] Result<double> loadAudio(const std::filesystem::path& path);

    // Replaces the scene controller with a glTF scene (parameters and routes are rebuilt).
    [[nodiscard]] Result<void> loadScene(const std::filesystem::path& path);
    // Restores the built-in orb scene.
    void loadOrbScene();
    // Scene composition (milestone 0.7): a new empty composition, a composition from a scene
    // file, saving the current composition, and node editing.
    void newComposition();
    [[nodiscard]] Result<void> loadComposition(const std::filesystem::path& path);
    // The same install from a document already in memory. Exists for the assistant's transaction:
    // rolling back a created node means putting the previous composition back, and going through a
    // temporary file to do it would make a rollback depend on the disk.
    [[nodiscard]] Result<void> setCompositionJson(const nlohmann::json& document);
    [[nodiscard]] Result<void> saveComposition(const std::filesystem::path& path);
    [[nodiscard]] scene::Composition* composition() { return dynamic_cast<scene::Composition*>(controller_.get()); }
    // Adds a node to the current composition (converting the orb/glTF controller into one first).
    [[nodiscard]] Result<scene::CompositionNode*> addNode(scene::CompositionNode node);
    void removeNode(const std::string& name);
    [[nodiscard]] assets::AssetRegistry& assets() { return registry_; }
    [[nodiscard]] const std::filesystem::path& compositionPath() const { return compositionPath_; }
    // Loads an equirectangular HDR and installs it as the current scene's environment map.
    [[nodiscard]] Result<void> loadEnvironment(const std::filesystem::path& path);
    [[nodiscard]] const std::filesystem::path& environmentPath() const { return environmentPath_; }
    // Routes any supported file by extension: audio, .gltf/.glb, .hdr, .json (project).
    [[nodiscard]] Result<void> loadFile(const std::filesystem::path& path);

    // ---- project (milestone 0.9, ADR-019): parameters, routes, sources, presets, shaders,
    // timeline, plus the asset references (audio, scene, environment) that make a project a
    // complete session. Paths are written relative to the project file. ----
    static constexpr const char* kAppVersion = "0.1.0";
    [[nodiscard]] Result<void> saveProject(const std::filesystem::path& path);
    // Restores the assets first (a missing one is a warning, see projectWarnings()), then the
    // rest. Fails only when the document itself is invalid.
    [[nodiscard]] Result<void> loadProject(const std::filesystem::path& path);
    [[nodiscard]] const std::filesystem::path& projectPath() const { return projectPath_; }
    [[nodiscard]] const std::vector<std::string>& projectWarnings() const { return projectWarnings_; }
    // Parameter paths that a timeline cue's preset will overwrite, measured at load against the
    // values the scene file and the project put in effect (ADR-018). Sorted, deduplicated across
    // cues, empty when nothing is contested. Not a load fault -- a cue is *meant* to take a value
    // over -- but the thing an author needs told when a scene-file edit appears to do nothing.
    [[nodiscard]] const std::vector<std::string>& cuePresetOverrides() const { return cuePresetOverrides_; }
    void clearProjectPath() { projectPath_.clear(); }
    // Resets everything but the audio: orb scene, no sources/presets/timeline/shaders/environment,
    // default post settings, no project path.
    void newProject();
    // Every file the current session references (audio, environment, scene files and their
    // assets recursively, shader layers), absolute, without duplicates.
    [[nodiscard]] std::vector<std::filesystem::path> referencedFiles() const;
    // Copies every referenced file into <dir>/assets (scene files rewritten with relative
    // references) and writes <dir>/project.json pointing at the copies.
    [[nodiscard]] Result<void> exportBundle(const std::filesystem::path& dir);

    // ---- the 2D composition (ADR-083) ----
    // The layer stack drawn over the finished 3D frame. Every layer property is a parameter in
    // params(), registered before the timeline binds, so layers keyframe on the *same* timeline as
    // the scene and the camera and modulate from the *same* signal routes. Saved with the project
    // under "composition"; a project written before this existed simply has none.
    [[nodiscard]] comp::LayerStack& layers() { return layers_; }
    [[nodiscard]] const comp::LayerStack& layers() const { return layers_; }
    // Adding and removing go through the engine rather than the stack because a layer's parameters
    // have to enter and leave the parameter set with it, and the timeline has to re-bind.
    comp::TextLayer& addTextLayer(std::string text, double startSeconds = 0.0, double endSeconds = 0.0);
    comp::ShapeLayer& addShapeLayer(comp::ShapeKind shape);
    bool removeLayer(std::uint32_t id);
    comp::Layer* duplicateLayer(std::uint32_t id);
    // Re-registers every layer's parameters (after an edit that changed which exist) and re-binds.
    void refreshLayerParameters();

    // ---- the cinematic sequence (ADR-089) ----
    // The timed performance: shots, scene slots, actors, overlay cues, markers. Held by value
    // because it is a value; installing it is what turns it into timeline tracks and layers.
    //
    // Nothing here runs per frame except `applyAnimation` inside `update()`, which is a handful of
    // string compares per actor. A sequence that is installed costs what its tracks cost.
    [[nodiscard]] const seq::Sequence& sequence() const { return sequence_; }
    // Non-const for editing. Edit, then call `installSequence()`; the two are separate because a
    // bake is a moment and an editor drags a shot handle sixty times a second.
    [[nodiscard]] seq::Sequence& sequence() { return sequence_; }
    [[nodiscard]] bool hasSequence() const {
        return !sequence_.shots.empty() || !sequence_.actors.empty() || !sequence_.overlays.empty();
    }
    // Replaces the sequence and installs it.
    [[nodiscard]] Result<seq::InstallReport> setSequence(seq::Sequence sequence);
    // Bakes the current sequence onto the timeline, realises its overlay cues as layers and binds.
    // Idempotent: every track and layer the previous install owned is replaced, never stacked.
    [[nodiscard]] Result<seq::InstallReport> installSequence();
    // Removes every track and layer the sequence owns, and forgets the sequence.
    void clearSequence();
    // What the last install did, for the editor to show.
    [[nodiscard]] const seq::InstallReport& sequenceReport() const { return sequenceReport_; }
    // Parameter paths the installed sequence owns. Anything else on the timeline is the author's.
    [[nodiscard]] const std::vector<std::string>& sequenceTargets() const { return sequenceTargets_; }

    // ---- cinematic events (ADR-098) ----
    //
    // Most of a sequence's events are not here: they stopped being events at bake and are now keys
    // on the timeline above. What is here is the two tiers a track cannot carry -- an imperative
    // action at a known time, and a trigger only a running world can supply.
    //
    // The engine advances the scheduled tier from the timeline clock once per frame and drains
    // whatever fired into `firedEvents()`, which is therefore this frame's list and no longer. It
    // does not *apply* them: an EntityAction belongs to the action system and a Notify belongs to
    // the host, and the engine inventing an interpretation for either would be the second event
    // system this design exists to avoid.
    [[nodiscard]] seq::EventDispatcher& sequenceEvents() { return sequenceEvents_; }
    [[nodiscard]] const seq::EventDispatcher& sequenceEvents() const { return sequenceEvents_; }
    // What fired during the most recent `update()`, in (time, priority, declaration) order.
    [[nodiscard]] std::span<const seq::FiredEvent> firedEvents() const { return firedEvents_; }

    // ---- built-in post-processing ----
    [[nodiscard]] scene::PostSettings& post() { return post_; }
    [[nodiscard]] const scene::PostSettings& post() const { return post_; }

    // ---- physical camera (ADR-037): lens, exposure and focus tracking ----
    // Registered as "camera/lens/*", "camera/exposure/*" and "camera/focus/*"; applied to
    // Scene::camera after the controller has placed it, so any scene picks them up. The lens only
    // drives the field of view when `camera/lens/useExplicitFov` is turned off, so every scene
    // authored before ADR-037 renders exactly as it did.
    [[nodiscard]] scene::LensSettings& lens() { return lens_; }
    [[nodiscard]] const scene::LensSettings& lens() const { return lens_; }
    [[nodiscard]] scene::ExposureSettings& exposure() { return exposure_; }
    [[nodiscard]] const scene::ExposureSettings& exposure() const { return exposure_; }
    [[nodiscard]] scene::FocusSettings& focus() { return focus_; }
    [[nodiscard]] const scene::FocusSettings& focus() const { return focus_; }
    // The focus distance the tracker has reached this frame (metres).
    [[nodiscard]] float trackedFocusDistance() const { return focusState_.distance; }
    // Re-seeds focus tracking and the auto-exposure meter. Call on a scene swap or a seek so an
    // offline render starts from the same state a live one does.
    void resetCameraState();

    // ---- user shader layers ----
    [[nodiscard]] shaders::ShaderLayerSet& shaderLayers() { return shaderLayers_; }
    [[nodiscard]] const shaders::ShaderLayerSet& shaderLayers() const { return shaderLayers_; }
    [[nodiscard]] Result<std::uint32_t> addShaderLayer(const std::filesystem::path& path, shaders::LayerStage stage);
    void removeShaderLayer(std::uint32_t id);

    // ---- modulation sources and presets ----
    [[nodiscard]] signals::SourceRack& sources() { return sources_; }
    [[nodiscard]] params::PresetBank& presets() { return presets_; }
    // Adds a source of the given kind with a unique name derived from `baseName`; attaches and
    // re-binds. Returns the source.
    signals::Source& addSource(const std::string& kind, const std::string& baseName);
    void removeSource(const std::string& kind, const std::string& name);
    // Rebinds routes after routes/sources/parameters changed (UI edits).
    void rebind();
    // Preset helpers: capture the current base values, apply one, or morph between two.
    params::Preset& storePreset(const std::string& name);
    [[nodiscard]] bool recallPreset(const std::string& name);
    void morphPresets(const std::string& a, const std::string& b, float t);

    // ---- scene states and world macros (ADR-031) ----
    [[nodiscard]] StateMachine& states() { return states_; }
    [[nodiscard]] const StateMachine& states() const { return states_; }
    // Starts a transition to the named state (false when unknown). Instant skips the morph.
    bool goToState(const std::string& name, bool instant = false);
    // ---- art direction (ADR-041) ----
    // Installing a director replaces the world macros it owns with the knobs it declares; a look
    // is an ordinary parameter snapshot restricted to visual prefixes.
    [[nodiscard]] const WorldDirector& director() const { return director_; }
    void setDirector(WorldDirector director);
    void clearDirector();
    [[nodiscard]] const std::vector<LookPreset>& looks() const { return looks_; }
    void setLooks(std::vector<LookPreset> looks) { looks_ = std::move(looks); }
    LookApplyResult applyLookByName(const std::string& name);

    [[nodiscard]] std::vector<WorldMacro>& worldMacros() { return worldMacros_; }
    [[nodiscard]] const std::vector<WorldMacro>& worldMacros() const { return worldMacros_; }
    // Adds/replaces a world macro: ensures its knob exists on the macro source and regenerates
    // its routes. Removal drops the routes too.
    void setWorldMacro(WorldMacro macro);
    bool removeWorldMacro(const std::string& name);

    // ---- live control (milestone 1.1, ADR-021) ----
    [[nodiscard]] ControlHub& control() { return controlHub_; }
    [[nodiscard]] const ControlHub& control() const { return controlHub_; }
    // The always-present "control" source (control.<channel> signals).
    [[nodiscard]] signals::ControlSource& controlSource();
    // Live audio input instead of a file: opens the capture device (substring match, "" =
    // default) and runs the analysis on it. Live mode only. stopAudioInput() returns to the
    // player (silent until a file is loaded).
    [[nodiscard]] Result<void> useAudioInput(const std::string& deviceName = "");
    void stopAudioInput();
    [[nodiscard]] bool hasLiveInput() const { return input_ != nullptr; }
    [[nodiscard]] audio::AudioInput* audioInput() { return input_.get(); }
    // Tempo source (saved in the project's "control" block as "tempoSource").
    void setTempoSource(TempoSource source);
    [[nodiscard]] TempoSource tempoSource() const { return tempoSource_; }
    // True when this frame's beat clock came from the MIDI clock (source selected and running).
    [[nodiscard]] bool midiClockActive() const { return midiClockActive_; }

    // ---- outputs (milestone 1.2): the application owns the windows; the engine only carries the
    // project's "outputs" block so it saves and loads with everything else ----
    [[nodiscard]] const nlohmann::json& outputsJson() const { return outputs_; }
    void setOutputsJson(nlohmann::json outputs) { outputs_ = std::move(outputs); }

    // ---- offline render settings (milestone 1.0), saved in the project under "render" ----
    [[nodiscard]] RenderSettings& renderSettings() { return render_; }
    [[nodiscard]] const RenderSettings& renderSettings() const { return render_; }

    // ---- timeline (milestone 0.8) ----
    [[nodiscard]] params::Timeline& timeline() { return timeline_; }
    [[nodiscard]] const params::Timeline& timeline() const { return timeline_; }
    // The clock the timeline is evaluated against this frame: audio time (render time without
    // audio) and beats from the beat clock.
    [[nodiscard]] const params::TimelineClock& timelineClock() const { return timelineClock_; }
    // Records a key for `path` at the current timeline time with the parameter's base value.
    params::Track* recordKey(const std::string& path, int component = -1,
                             params::KeyInterp interp = params::KeyInterp::Linear,
                             params::TimeBase base = params::TimeBase::Seconds);
    // Index of the cue currently in effect (-1 = none) and its morph progress.
    [[nodiscard]] params::Timeline::CueState cueState() const { return cueState_; }

    // Built-in signals published every frame: time.seconds, time.progress, time.playing,
    // beat.phase (per-frame extrapolated), beat.pulse (event), beat.count, beat.bpm, beat.bar.
    struct TimeSignals {
        signals::SignalId seconds = signals::kInvalidSignal;
        signals::SignalId progress = signals::kInvalidSignal;
        signals::SignalId playing = signals::kInvalidSignal;
        signals::SignalId beatPhase = signals::kInvalidSignal;
        signals::SignalId beatPulse = signals::kInvalidSignal;
        signals::SignalId beatCount = signals::kInvalidSignal;
        signals::SignalId bpm = signals::kInvalidSignal;
        signals::SignalId barPhase = signals::kInvalidSignal;
        // Musical structure above the bar (ADR-041): a phrase is `phraseBars` bars, a section is
        // `sectionPhrases` phrases. States and slow escalations key to these rather than to beats.
        signals::SignalId phrasePhase = signals::kInvalidSignal;  // 0..1 through the current phrase
        signals::SignalId phraseCount = signals::kInvalidSignal;  // phrases since the start
        signals::SignalId phrasePulse = signals::kInvalidSignal;  // event at each phrase boundary
        signals::SignalId sectionPhase = signals::kInvalidSignal; // 0..1 through the current section
        signals::SignalId sectionCount = signals::kInvalidSignal;
    };
    [[nodiscard]] const TimeSignals& timeSignals() const { return timeSignals_; }
    // Musical structure: bars per phrase (default 4) and phrases per section (default 4). Saved
    // with the project so a piece keeps its structure.
    [[nodiscard]] int phraseBars() const { return phraseBars_; }
    void setPhraseBars(int bars) { phraseBars_ = std::max(1, bars); }
    [[nodiscard]] int sectionPhrases() const { return sectionPhrases_; }
    void setSectionPhrases(int phrases) { sectionPhrases_ = std::max(1, phrases); }
    [[nodiscard]] const signals::SourceContext& sourceContext() const { return sourceContext_; }
    [[nodiscard]] bool hasAudio() const { return audioFile_ != nullptr; }
    [[nodiscard]] const std::filesystem::path& audioPath() const { return audioPath_; }
    [[nodiscard]] std::shared_ptr<const audio::AudioFile> audioFile() const { return audioFile_; }

    // ---- transport (ADR-102) --------------------------------------------------------------------
    //
    // These are the same five calls they have always been, and every caller -- the panels, the
    // keyboard, the OSC/MIDI control map, the AI tools, the UI script driver -- keeps working. What
    // changed is what is behind them: the application's `Transport`, rather than the audio device.
    //
    // Before this, "playing" meant "the audio device is running", so a project with no audio could
    // not be played, paused, seeked or stopped at all, and one with audio stopped at the end of the
    // wav however long the sequence was.
    //
    // `play()` still returns a Result because it can still fail -- an audio device that will not
    // start, say -- but no longer fails merely for want of a file.
    [[nodiscard]] Result<void> play();
    void pause();
    void togglePlay();
    void stop();
    void seekSeconds(double seconds);
    [[nodiscard]] bool isPlaying() const;
    [[nodiscard]] double positionSeconds() const;
    // The project's length: the longest of the audio, the baked sequence and the timeline. Not the
    // audio file's length -- `audioDurationSeconds()` is that, for the places that mean the file.
    [[nodiscard]] double durationSeconds() const;
    [[nodiscard]] double audioDurationSeconds() const;
    // The transport itself, for everything the five calls above do not cover: the loop, the rate,
    // frame stepping, the frame rate and the snapshot the UI draws from.
    [[nodiscard]] Transport& transport() { return transport_; }
    [[nodiscard]] const Transport& transport() const { return transport_; }
    // Re-reads the project's length and tempo into the transport. Called after anything that can
    // change either -- loading audio, baking a sequence, editing the timeline -- and once per
    // update, which is cheap and means nothing has to remember.
    void refreshTransport();
    // Frame stepping and beat stepping, here rather than on the transport because the beat grid is
    // the analysis's and the resynchronising a seek needs is the engine's.
    void stepFrames(std::int64_t frames);
    void stepBeats(int beats);
    // The next/previous beat boundary from the analysed beat grid, or from the tempo when there is
    // no grid. Returns the position unchanged when there is neither.
    [[nodiscard]] double beatBoundary(double fromSeconds, int direction) const;
    // The next or previous marker on the sequence, skipping the `Beat` markers -- those are the beat
    // grid drawn on the strip, there are thousands of them, and "jump to the next marker" means the
    // next *place*, not the next beat. Returns the position unchanged when there is none that way.
    [[nodiscard]] double markerBoundary(double fromSeconds, int direction) const;
    void stepMarkers(int direction);
    void setVolume(float volume);
    [[nodiscard]] float volume() const;

    // Produces this frame's time. Live: audio position while playing, free-running while paused.
    FrameTime tick(FrameClock& clock);

    // Evaluates the pipeline for one frame: signals -> modulation -> scene.
    void update(const FrameTime& time);

    // The size the next frames will be rendered at, forwarded to the composition every update.
    // Terrain LOD is a screen-space decision (ADR-046) and a composition is authored without
    // knowing its window, so whoever owns the render target tells it. Callers that never set it
    // keep the composition's default reference viewport.
    void setViewport(std::uint32_t width, std::uint32_t height);

    // ---- accessors for UI / renderer / tests ----
    [[nodiscard]] const scene::Scene& scene() const { return controller_->scene(); }
    [[nodiscard]] scene::SceneController& controller() { return *controller_; }
    // The orb preset when it is the active controller (nullptr otherwise).
    [[nodiscard]] scene::OrbScene* orbScene() { return dynamic_cast<scene::OrbScene*>(controller_.get()); }
    [[nodiscard]] scene::GltfScene* gltfScene() { return dynamic_cast<scene::GltfScene*>(controller_.get()); }
    [[nodiscard]] params::ParameterSet& params() { return params_; }
    [[nodiscard]] params::Modulator& modulator() { return modulator_; }
    [[nodiscard]] signals::SignalBus& signals() { return bus_; }
    [[nodiscard]] const signals::AudioSignals& audioSignals() const { return audioSignals_; }
    // The musical event signals (ADR-073): music.beat ... music.impact, and the classifier behind
    // them. Read it to ask *when* something fired; the bus clears event values at the end of every
    // update(), so polling the signals from outside the frame only ever sees zero.
    [[nodiscard]] const MusicRuntime& music() const { return music_; }
    [[nodiscard]] audio::AudioPlayer* player() { return player_.get(); }
    [[nodiscard]] analysis::AnalysisRunner* runner() { return runner_.get(); }
    [[nodiscard]] const analysis::AnalysisTrack* track() const { return track_.get(); }
    [[nodiscard]] const analysis::AnalysisFrame& latestFrame() const { return latest_; }
    [[nodiscard]] bool hasFrame() const { return hasFrame_; }
    [[nodiscard]] const EngineStats& stats() const { return stats_; }
    [[nodiscard]] const analysis::AnalyzerConfig& analyzerConfig() const { return analyzerConfig_; }

    // Per-route "response" convenience used by the UI: amount of the route targeting `path`.
    [[nodiscard]] params::ModRoute* routeForTarget(const std::string& path);

private:
    void publishFrame(const analysis::AnalysisFrame& frame);
    // Shared by loadComposition and setCompositionJson: detach, swap, attach, reapply.
    [[nodiscard]] Result<void> installComposition(std::unique_ptr<scene::Composition> composition);
    void installController(std::unique_ptr<scene::SceneController> controller);
    Result<void> reapplyEnvironment();
    void updateTimeSignals(const FrameTime& time, bool newAnalysisFrame);
    void addDefaultPostRoutes();
    void updateTimelineClock(const FrameTime& time);
    // Puts the audio device where the transport is. The one place that knows the rule: audio
    // follows, and it only follows at unit rate, because `AudioPlayer` has no rate control and a
    // silent device is honest where a resampled one would be a lie (see ADR-102).
    [[nodiscard]] Result<void> syncAudioToTransport(bool seekDevice);
    void applyCues();
    // Logs (and records in projectWarnings()) every value a cue preset will overwrite that the
    // scene file or the project set to something else. Presets are meant to win; they are not
    // meant to win silently. Called once at the end of loadProject().
    void reportCuePresetOverrides();
    void detachSceneParameters(); // before params_.clear(): composition, shader layers, timeline
    void ensureControlSource();   // the "control" source exists in the rack and its channels are declared

    EngineMode mode_;
    params::ParameterSet params_;
    signals::SignalBus bus_;
    signals::AudioSignals audioSignals_;
    MusicRuntime music_;
    params::Modulator modulator_;
    signals::SourceRack sources_;
    params::PresetBank presets_;
    params::Timeline timeline_;
    RenderSettings render_;
    ControlHub controlHub_;
    nlohmann::json outputs_;
    TempoSource tempoSource_ = TempoSource::Analysis;
    bool midiClockActive_ = false;
    std::unique_ptr<audio::AudioInput> input_;
    params::Parameter<float>* inputGain_ = nullptr;
    params::TimelineClock timelineClock_;
    params::Timeline::CueState cueState_;
    std::vector<std::string> cuePresetOverrides_;
    std::uint32_t viewportWidth_ = 0;   // 0 = never set; the composition keeps its own default
    std::uint32_t viewportHeight_ = 0;
    params::Preset cueFrom_;      // base values captured when the current cue started (morphs)
    bool cueApplied_ = false;     // the current cue's preset has been applied at full weight
    StateMachine states_;
    std::vector<WorldMacro> worldMacros_;
    WorldDirector director_;
    std::vector<LookPreset> looks_;
    signals::SignalId stateProgressSignal_ = signals::kInvalidSignal; // "state.progress"
    signals::SignalId stateIndexSignal_ = signals::kInvalidSignal;    // "state.index"
    void applyWorldMacros();      // regenerates every world macro's routes (after load)
    void ensureMacroKnob(const std::string& knob, float defaultValue);
    shaders::ShaderLayerSet shaderLayers_;
    comp::LayerStack layers_;
    seq::Sequence sequence_;
    std::vector<std::string> sequenceTargets_;
    seq::InstallReport sequenceReport_;
    seq::EventDispatcher sequenceEvents_;
    std::vector<seq::FiredEvent> firedEvents_;
    void removeLayerParameters(); // drops "layers/*" from params_ (before a reload or a delete)
    scene::PostSettings post_;
    scene::PostParameters postParams_;
    scene::LensSettings lens_;
    scene::ExposureSettings exposure_;
    scene::FocusSettings focus_;
    scene::CameraParameters cameraParams_;
    scene::FocusState focusState_;
    bool cameraStateReset_ = true; // forwarded to the post chain as PostSettings::exposureReset
    TimeSignals timeSignals_;
    int phraseBars_ = 4;
    int sectionPhrases_ = 4;
    std::uint32_t lastPhraseIndex_ = 0;
    signals::SourceContext sourceContext_;
    assets::AssetRegistry registry_;
    std::unique_ptr<scene::SceneController> controller_;
    std::filesystem::path environmentPath_;
    std::filesystem::path compositionPath_;
    std::filesystem::path projectPath_;
    // Appends to projectWarnings_ if it is not already there. Unresolved bindings are re-reported
    // on every rebind, and a warning list that grew a duplicate per scene swap would stop being
    // read.
    void noteBindingProblem(std::string message);
    std::vector<std::string> projectWarnings_;
    // Beat clock extrapolated per render frame from the analysis tempo (ADR-012).
    double beatClockPhase_ = 0.0;
    std::uint32_t beatClockCount_ = 0;
    std::uint32_t lastAnalysisBeatCount_ = 0;

    Transport transport_;
    // What the audio player was last told to do, so the engine can tell whether the device needs
    // starting, stopping or seeking this frame without asking it every frame.
    bool audioFollowing_ = false;
    std::shared_ptr<const audio::AudioFile> audioFile_;
    std::filesystem::path audioPath_;
    analysis::AnalyzerConfig analyzerConfig_;
    std::unique_ptr<audio::AudioPlayer> player_;
    std::unique_ptr<analysis::AnalysisRunner> runner_;
    std::unique_ptr<analysis::AnalysisTrack> track_;
    std::size_t offlineFrameCursor_ = 0;

    analysis::AnalysisFrame latest_;
    bool hasFrame_ = false;
    double lastRenderTime_ = 0.0;
    EngineStats stats_;
};

} // namespace avgen::app
