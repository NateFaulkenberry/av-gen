#pragma once

// The audiovisual pipeline (architecture.md): AudioPlayer -> AnalysisRunner -> SignalBus ->
// Modulator -> ParameterSet -> OrbScene -> Scene. The renderer and UI sit beside it and only
// read the Scene / parameters. Two modes (ADR-012): Live (audio device is the clock, analysis
// runs on a thread) and Offline (fixed-step clock, analysis precomputed and indexed by time).

#include "analysis/analysis_runner.hpp"
#include "app/camera_director.hpp"
#include "app/control_hub.hpp"
#include "app/music_runtime.hpp"
#include "app/render_settings.hpp"
#include "app/scene_states.hpp"
#include "app/transport.hpp"
#include "scene/rebuild_deferral.hpp"
#include "app/world_director.hpp"
#include "audio/audio_input.hpp"
#include "analysis/analysis_track.hpp"
#include "analysis/analyzer.hpp"
#include "audio/audio_file.hpp"
#include "audio/arrangement.hpp"
#include "audio/audio_player.hpp"
#include "audio/tempo_metadata.hpp"
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
#include "world/atmospheric_params.hpp"
#include "world/effect_params.hpp"
#include "scene/scene_controller.hpp"
#include "seq/director.hpp"
#include "seq/sequence.hpp"
#include "shaders/shader_layers.hpp"
#include "signals/audio_signals.hpp"
#include "signals/signal_bus.hpp"
#include "signals/source.hpp"

#include <nlohmann/json.hpp>

#include <filesystem>
#include <functional>
#include <utility>
#include <memory>
#include <optional>
#include <array>
#include <cstdint>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace avgen::app {

enum class EngineMode { Live, Offline };

// Where the beat clock (beat.* signals, SourceContext tempo fields, the timeline's beat time)
// comes from: the audio analyzer, or an incoming MIDI clock (ADR-021 follow-up). With MidiClock
// selected but no running clock the analyzer is used; the analyzer keeps running either way.
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
    // The same, with a project's record of the objects its session added to and removed from that
    // scene (ADR-330) spliced into the document before it is parsed. Only `loadProject` has such a
    // record; everything else opens a scene file as the file says it is.
    [[nodiscard]] Result<void> loadComposition(const std::filesystem::path& path, const nlohmann::json& nodeEdits);
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
    // The document `saveProject` would write for `path`, without writing it. Split out so the
    // unsaved-changes comparison below is the *same function* the save is -- a second serialiser
    // written "to match" is the shape of defect this codebase keeps finding (ADR-440).
    [[nodiscard]] nlohmann::json projectDocument(const std::filesystem::path& path);
    // Restores the assets first (a missing one is a warning, see projectWarnings()), then the
    // rest. Fails only when the document itself is invalid.
    [[nodiscard]] Result<void> loadProject(const std::filesystem::path& path);
    [[nodiscard]] const std::filesystem::path& projectPath() const { return projectPath_; }
    [[nodiscard]] const std::vector<std::string>& projectWarnings() const { return projectWarnings_; }

    // ---- unsaved changes (ADR-440) --------------------------------------------------------------
    //
    // "Is there work in this session that closing it would lose?" -- computed by serialising the
    // project and comparing it against the serialisation taken when it was last opened or saved,
    // never by a flag that every mutation site has to remember to set. The long comment above
    // `Engine::sampleProjectDirty` carries the measurement that rules out the two obvious
    // alternatives (comparing against the file; a hand-maintained list of noisy keys).
    //
    // `touchedSinceLastSample` is the host's answer to "did anything happen to this application
    // since you last asked me" -- a pointer, a key, a menu, a drop. It is not a dirty flag: it is
    // never consulted to decide that something *did* change, only to decide whether a change that
    // has already been measured could possibly be the user's. On a sample where nothing touched the
    // application, every difference is the engine writing its own state (entity positions, a world
    // effect's parameter writeback) and the baseline moves to absorb it.
    //
    // Costs a full project serialisation: 31 ms on `glowmere-valley-2-multicam.json` (675 KB, 5,502
    // parameters), under a millisecond on everything small. Call it when closing something, and
    // otherwise only on idle frames -- `Application` throttles it against its own measured cost.
    void sampleProjectDirty(bool touchedSinceLastSample);
    // Samples and answers. Monotone: once dirty, dirty until a save or a load.
    [[nodiscard]] bool projectDirty(bool touchedSinceLastSample = true);
    // The last answer, with no serialisation. For a per-frame reader such as the window title.
    [[nodiscard]] bool projectDirtyCached() const { return projectDirty_; }
    // "What is in memory is what is stored." Called by `saveProject` and at the end of a load; also
    // the hook for anything else that makes the two agree.
    void markProjectSaved();

    // ---- what a load is doing while it does it (the brief's section 5) -------------------------
    //
    // Opening a project is one synchronous call that can hold the main thread for seconds: the
    // scene's first flatten alone is about 620 ms on a real world (docs/application-performance.md
    // section 14), and the audio is decoded and mixed on the same thread before it. During that the
    // window does not redraw, which is indistinguishable from a crash.
    //
    // It is not moved to a worker, and that is deliberate rather than unfinished. The composition
    // is not thread-safe, the environment map's prefilter is GPU work and all GPU work in this
    // application is the main thread's, and the parameter set is being torn down and rebuilt
    // underneath everything that reads it. ADR-084 already rejected threading a much smaller piece
    // of this for the same reasons.
    //
    // What can be fixed without any of that risk is the *silence*. The loader says which stage it
    // is in as it enters it, and the editor shows that; the freeze is the same length and it stops
    // being a mystery. `index`/`count` are a position in a list of stages that is known up front --
    // a countable fact, not an estimate of time remaining, which ADR-064 is clear nobody should
    // invent.
    struct LoadStage {
        std::string_view name;
        int index = 0;
        int count = 1;
    };
    using LoadReporter = std::function<void(const LoadStage&)>;
    // Set by the live editor. Never set offline: `runHeadless` has no window to report to, and a
    // reporter that logged would put a wall clock into a deterministic path's output.
    void setLoadReporter(LoadReporter reporter) { loadReporter_ = std::move(reporter); }
    // Per-stage wall-clock of the last `loadProject`, in the order the stages ran. For the
    // measurement in docs/application-performance.md; empty until a project has been opened.
    [[nodiscard]] const std::vector<std::pair<std::string, double>>& lastLoadTimings() const {
        return loadTimings_;
    }
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

    // ---- multiple cameras (ADR-245) --------------------------------------------------------------
    //
    // The camera collection lives on the `scene::Composition` (it is world content, and a shot names
    // a camera by an id that only means something inside the scene it was composed for). These two
    // calls are the engine's part: re-registering a changed collection's parameters so the timeline
    // can bind onto them, and publishing which camera is live.
    //
    // **`activeCamera()` is the one published answer to "which camera is on screen".** Nothing else
    // in the engine decides it and nothing else should be asked. The camera's *pose* is, as it has
    // always been, `Scene::camera`; this says which camera that pose belongs to and why, which is
    // what an overlay, a sequencer lane or an output preview needs and what the pose cannot answer.
    // A session with no composition reports the main camera, default reason -- which is the truth.
    [[nodiscard]] scene::ActiveCameraState activeCamera() const;
    // Replaces the composition's camera collection, re-registers `cameras/<slug>/*` and re-binds the
    // timeline. Refuses an invalid collection whole and changes nothing on a refusal.
    [[nodiscard]] Result<void> setCameraDirection(scene::CameraDirection direction);

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
    // Is there a sequence worth writing to the project file?
    //
    // **This decides whether the whole `sequence` block is saved, so anything missing from it is
    // data a save silently destroys.** It tested shots, actors and overlays, which was already
    // incomplete and became load-bearing the day Song Mode stopped writing shots: a piece with ten
    // sections and seven performer rules but no shots answered *false*, and one Save later the
    // sections, the rules, the shot language and the analyzed structure were gone from the file.
    //
    // So the question is asked the other way round -- not "does it have the three things somebody
    // listed once", but "is it still the empty sequence a new project starts with". Everything a
    // `seq::Sequence` can carry is named here, and a field added to that struct without being added
    // here is a field that will not survive a round trip.
    [[nodiscard]] bool hasSequence() const {
        return !sequence_.shots.empty() || !sequence_.actors.empty() ||
               !sequence_.overlays.empty() || !sequence_.scenes.empty() ||
               !sequence_.markers.empty() || !sequence_.events.empty() ||
               !sequence_.tracks.empty() || !sequence_.sectionTimeline.sections.empty() ||
               !sequence_.sectionPerformance.entries.empty() ||
               !sequence_.structure.sections.empty() ||
               sequence_.shotLanguage.customized();
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

    // ---- tempo and where it came from (ADR-394) ----
    //
    // One resolution, used by the transport readout AND by the beat clock, so the number an artist
    // reads can never disagree with the number driving the picture. Precedence, highest first:
    //
    //   UserOverride     the artist typed it -- never silently replaced by anything below
    //   ExternalClock    a running MIDI clock, because selecting it is itself a user act
    //   EmbeddedMetadata the BPM written into the imported file
    //   Detected         the analyzer's estimate
    //   None             nothing knows
    //
    // Note what this does NOT do: an embedded BPM outranks the analyzer for the tempo *number*
    // only. Beat phase, the beat count and the whole beat grid still come from analysis, because a
    // BPM tag does not contain them. Metadata supplies the scalar; analysis supplies the grid.
    [[nodiscard]] audio::AudioTempo tempo() const;
    // The resolved tempo's bpm and provenance without the diagnostic strings. Identical precedence
    // -- it IS the precedence, which `tempo()` then decorates -- and it allocates nothing, which
    // matters because the beat clock asks every frame and a metadata format string is past SSO.
    [[nodiscard]] std::pair<audio::TempoProvenance, double> resolvedTempo() const;
    // The tempo embedded in the loaded arrangement's audio, whether or not it is the one in force.
    // Kept separately so the UI can say "the file says 128, you have set 130".
    [[nodiscard]] const audio::AudioTempo& embeddedTempo() const { return embeddedTempo_; }
    // The artist's number. Setting it makes it win over everything below; clearing returns the
    // project to whatever the file and the analyzer say.
    void setTempoOverride(double bpm);
    void clearTempoOverride();
    [[nodiscard]] const audio::AudioTempo& tempoOverride() const { return tempoOverride_; }

    // ---- outputs (milestone 1.2): the application owns the windows; the engine only carries the
    // project's "outputs" block so it saves and loads with everything else ----
    [[nodiscard]] const nlohmann::json& outputsJson() const { return outputs_; }
    void setOutputsJson(nlohmann::json outputs) { outputs_ = std::move(outputs); }

    // ---- offline render settings (milestone 1.0), saved in the project under "render" ----
    [[nodiscard]] RenderSettings& renderSettings() { return render_; }
    [[nodiscard]] const RenderSettings& renderSettings() const { return render_; }
    // The path tracer's authored settings (ADR-366). A peer of the render settings, not a member
    // of them: the two renderers share a resolution and nothing else.
    [[nodiscard]] PathTraceSettings& pathTraceSettings() { return pathTrace_; }
    [[nodiscard]] const PathTraceSettings& pathTraceSettings() const { return pathTrace_; }

    // ---- timeline (milestone 0.8) ----
    // ---- world effects (ADR-207) ----------------------------------------------------------------
    //
    // The *live* set: the composition's authored effects with this frame's modulation applied. The
    // panel edits these; `Composition::worldEffects()` is what a save writes.
    [[nodiscard]] std::vector<world::WorldEffect>& worldEffects() { return worldEffects_; }
    [[nodiscard]] const std::vector<world::WorldEffect>& worldEffects() const { return worldEffects_; }
    [[nodiscard]] const world::WorldEffectParameters& worldEffectParameters() const { return worldEffectParams_; }
    // Replaces the effect set: re-registers `worldfx/...` parameters and writes the set back to the
    // composition so a save carries it. Refuses the whole set the way `Composition::setWorldEffects`
    // does, and leaves everything as it was on a refusal.
    [[nodiscard]] Result<void> setWorldEffects(std::vector<world::WorldEffect> effects);

    // ---- atmospheric effects (ADR-230) -----------------------------------------------------------
    //
    // The same three accessors on the same terms, for the sky family. Separate from the world
    // effects all the way down -- separate list, separate parameter group, separate GPU block,
    // separate draw -- because the only thing the two share is a lifecycle.
    [[nodiscard]] std::vector<world::AtmosphericEffect>& atmosphericEffects() { return atmosphericEffects_; }
    [[nodiscard]] const std::vector<world::AtmosphericEffect>& atmosphericEffects() const {
        return atmosphericEffects_;
    }
    [[nodiscard]] const world::AtmosphericParameters& atmosphericParameters() const { return atmosphericParams_; }
    // §68. The fields this scene publishes, as of the last `update()`. Read by the World Effects
    // panel so the subscription combo offers names that exist rather than a free-text box in which
    // a typo is indistinguishable from a field somebody has not made yet.
    [[nodiscard]] const world::fields::FieldBus& fieldBus() const { return fieldBus_; }
    [[nodiscard]] Result<void> setAtmosphericEffects(std::vector<world::AtmosphericEffect> effects);

    // ADR-392. Attaches an effect's default audio routes and returns how many were added.
    //
    // Called when somebody **adds** an effect, which is a gesture, not a load. It deliberately is
    // not inside `setAtmosphericEffects`: that call also runs when a project is opened, and a
    // project whose author deleted every route must not grow them back each time it is loaded.
    // Same shape as `addDefaultPostRoutes` -- and, like it, a no-op when something already
    // automates this effect, so pressing the button twice does not stack two sets of routes.
    //
    // A route whose target does not resolve is skipped rather than written. The conformance test
    // guarantees that never happens for a kind that is wired; this is the belt for the one that is
    // not yet, because a dead route in somebody's saved project is the failure ADR-387 spent a day
    // on and it must not be introduced by a button.
    std::size_t addDefaultAtmosphericRoutes(std::string_view effectName);

    // The director's cut, flattened to what an effect needs for time gating (ADR-207). Installed by
    // `app::installSequence` and cleared by `releaseDirectedCamera`; empty means nothing is directing
    // the camera, in which case `CameraTravel` and `HeroFocus` effects simply never activate.
    void setShotSpans(std::vector<world::ShotSpan> spans) { shotSpans_ = std::move(spans); }
    [[nodiscard]] std::span<const world::ShotSpan> shotSpans() const { return shotSpans_; }

    // ADR-225: the Auto-director's controls, saved with the project.
    //
    // Here rather than in `DirectorState` -- which is a member of the running `Application` and of
    // nothing that is written anywhere -- because the project file is what `saveProject` writes and
    // what an offline render loads. `Application` mirrors the panel's live copy into this one, and
    // takes it back after a load; nothing in the engine reads it, exactly as nothing in the engine
    // reads `shotSpans_` except to hand it on.
    [[nodiscard]] AutoDirectorSettings& autoDirector() { return autoDirector_; }
    [[nodiscard]] const AutoDirectorSettings& autoDirector() const { return autoDirector_; }

    // ADR-249: the authored song plan -- what each section of the piece asks the camera to do.
    //
    // Here for the same reasons the settings above are: the project file is what `saveProject`
    // writes and what an offline render loads, and a Song Mode render that does not carry the plan
    // is a Song Mode render of nothing. Nothing in the engine reads it; the Auto-director does.
    //
    // **This is the plan the director is directed to, not a copy of anybody's song structure.** An
    // empty plan is the normal state: Song Mode then derives one from the analyzed structure
    // (`songPlanForEngine`), which is what makes the beginner path work before anybody has authored
    // a single intent.
    [[nodiscard]] SongPlan& songPlan() { return songPlan_; }
    [[nodiscard]] const SongPlan& songPlan() const { return songPlan_; }

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

    // ---- the audio arrangement (ADR-103) ---------------------------------------------------------
    //
    // A piece can be made of several files: a stem set, a song with a spoken outro, two cues with a
    // gap. The clips are mixed down to one buffer and that buffer is installed exactly as a loaded
    // file is, so the player, the analyzer, the waveform and the transport are unchanged -- a
    // one-clip arrangement is bit-identical to the file it names, which is what makes routing
    // `loadAudio` through here safe.
    [[nodiscard]] const std::vector<audio::AudioClip>& audioClips() const { return audioClips_; }
    [[nodiscard]] Result<void> setAudioClips(std::vector<audio::AudioClip> clips);
    // Re-mixes the current clips and installs the result. Called after an edit to one of them.
    [[nodiscard]] Result<void> rebuildAudio();
    // What the last mix did, for the sequencer's lane labels and the warnings list.
    [[nodiscard]] const audio::MixReport& audioMix() const { return audioMix_; }
    // Changes whenever the installed audio changes -- a load, a re-mix after a clip edit, or the
    // audio going away. Anything that caches something derived from it (a waveform summary, a beat
    // grid) keys on this.
    //
    // Not on the `AudioFile*`, which is what the sequencer used to do. The file is freed and a new
    // one allocated on every re-mix, and an allocator may hand back the same address -- so a cache
    // comparing pointers concluded "same file" about a different mix and kept drawing the old
    // waveform under the new clips. That was rare when audio changed only on an explicit load and
    // became likely the moment a clip drag started re-mixing.
    [[nodiscard]] std::uint64_t audioRevision() const { return audioRevision_; }
    // The decoded source behind a clip, for drawing its waveform. Null while it is missing.
    [[nodiscard]] std::shared_ptr<const audio::AudioFile> clipSource(const std::filesystem::path& p) const {
        return clipSources_.find(p);
    }
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

    // ---- the interactive seek (ADR-084's policy, applied to the playhead) ---------------------
    //
    // `seekSeconds` is the whole of it: the transport, the audio player, the modulator, the music
    // classifier, the director, the camera state, **the entity world re-simulated from
    // `target - 90 s` at a fixed 1/60 s step**, every rig reseeded, the event scheduler rebased.
    // On `glowmere-valley-2-multicam` that last item is 99.999% of the call and runs to seconds.
    //
    // A panel calling it from inside its own draw therefore holds the whole application for the
    // length of the re-simulation, and a drag asks for one per frame of the gesture -- of which
    // every one but the last is superseded before it finishes.
    //
    // `requestSeek` splits the two halves that were never actually one. The *cheap* half -- the
    // transport position and the timeline clock, which is what draws the playhead and what every
    // readout in the application means by "where are we" -- is applied immediately, on the calling
    // frame. The *expensive* half is recorded as outstanding and performed by
    // `serviceSeekRequest`, once, in a known place in the frame loop, at most once per frame and
    // deferred while the gesture is still moving.
    //
    // **What the UI reads while the work is outstanding**: the transport, exactly as it does now.
    // There is no second copy of the position, no snapshot, and nothing to keep in step -- the
    // requested position was already the authoritative answer to "where is the playhead" and the
    // re-simulation was always downstream of it. `seekPending()` says whether the world has caught
    // up, so an indicator can be honest about it rather than the editor pretending it has.
    //
    // No thread. The composition is not thread-safe, all GPU work in this application is the main
    // thread's, and ADR-084 §2 rejected threading a far smaller piece of this for those reasons.
    // Deferring work is not the same thing as moving it, and only one of the two needs a lock.
    void requestSeek(double seconds, bool gestureHeld);
    // Honours at most one outstanding request. `elapsedMs` is the frame's wall time, passed in
    // rather than read here so the policy stays a pure function of its inputs. Returns true when a
    // seek was performed.
    bool serviceSeekRequest(double elapsedMs);
    // True while a request has been taken and not yet evaluated: the playhead has moved and the
    // world has not. ADR-231's canvas indicator is the place this belongs.
    [[nodiscard]] bool seekPending() const { return seekPending_; }
    // Milliseconds. Zero means evaluate on the frame the request arrives, which is what this did
    // before and is what every non-live mode keeps. Set only by `installController` in
    // `EngineMode::Live`, exactly as `setInteractiveRebuildBudget` is -- a wall clock has no
    // business deciding what a deterministic render contains.
    void setInteractiveSeekBudget(double ms) { interactiveSeekBudgetMs_ = ms; }

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
    // The next/previous beat boundary from the analyzed beat grid, or from the tempo when there is
    // no grid. Returns the position unchanged when there is neither.
    [[nodiscard]] double beatBoundary(double fromSeconds, int direction) const;
    // The next or previous marker on the sequence, skipping the `Beat` markers -- those are the beat
    // grid drawn on the strip, there are thousands of them, and "jump to the next marker" means the
    // next *place*, not the next beat. Returns the position unchanged when there is none that way.
    //
    // `sectionsOnly` narrows it to the `Section` markers: the structural landmarks, which is what
    // Shift+arrow means in Markers snap mode (ADR-357). A marker list is mostly cues, and skipping
    // to the next *section* is the coarser step in the same sense a bar is coarser than a beat.
    [[nodiscard]] double markerBoundary(double fromSeconds, int direction,
                                        bool sectionsOnly = false) const;
    // Falls back to every marker when `sectionsOnly` is asked for and the piece has no sections, so
    // the coarse key is never a dead one.
    void stepMarkers(int direction, bool sectionsOnly = false);
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

    // ---- distance detail policy (ADR-186) ------------------------------------------------------
    //
    // What the procedural cull ladder, the rig pose rate and the entity world are allowed to drop
    // because it is far away. The default honours everything the scene authored, which is live
    // playback; an offline render lifts some or all of it. Written into the controller's scene at
    // the top of every `update`, so it survives a scene the controller rebuilds under it.
    void setDetailLimits(const scene::DetailLimits& limits) { detailLimits_ = limits; }
    [[nodiscard]] const scene::DetailLimits& detailLimits() const { return detailLimits_; }
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
    // The same track, shareable. A background job that reads it (the song-structure analysis,
    // ADR-216) has to survive the audio being replaced under it, and a raw pointer into something
    // `loadAudio` resets is a use-after-free waiting for a person to open two files quickly. Held
    // as a `shared_ptr` rather than copied into the job because an analyzed four-minute track is
    // tens of megabytes of spectra.
    [[nodiscard]] std::shared_ptr<const analysis::AnalysisTrack> trackShared() const { return track_; }
    [[nodiscard]] const analysis::AnalysisFrame& latestFrame() const { return latest_; }
    [[nodiscard]] bool hasFrame() const { return hasFrame_; }
    [[nodiscard]] const EngineStats& stats() const { return stats_; }

    // ADR-410. The renderer publishes what the temporal ring actually holds; the Engine carries it
    // so the World Effects panel can say "settling -- 3 of 8 frames" without `ui/` reaching into a
    // renderer header. Set once a frame by whoever owns the SceneRenderer; default-constructed
    // (and therefore "complete", because nothing is needed) when nobody does.
    void setTemporalHistoryReport(const scene::TemporalHistoryReport& r) { temporalReport_ = r; }
    [[nodiscard]] const scene::TemporalHistoryReport& temporalHistoryReport() const { return temporalReport_; }
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
    scene::DetailLimits detailLimits_{}; // ADR-186; all limits honoured == live playback
    params::ParameterSet params_;
    signals::SignalBus bus_;
    signals::AudioSignals audioSignals_;
    MusicRuntime music_;
    params::Modulator modulator_;
    signals::SourceRack sources_;
    params::PresetBank presets_;
    params::Timeline timeline_;
    RenderSettings render_;
    PathTraceSettings pathTrace_;
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
    // ADR-216: hands this frame's EntityAction firings to the action system. Not a decision about
    // what a verb means -- `seq::actionFromEvent` owns that -- only about who to ask.
    void applySectionActions();
    // Event ids already complained about, so a table row that names a missing entity says so once
    // rather than sixty times a second.
    std::set<std::string> sectionActionProblems_;
    void removeLayerParameters(); // drops "layers/*" from params_ (before a reload or a delete)
    scene::PostSettings post_;
    // ADR-207. `worldEffects_` is the live set (authored + modulated); `worldEffectParams_` owns the
    // `worldfx/...` parameters; `shotSpans_` is the director's cut, for time gating.
    std::vector<world::WorldEffect> worldEffects_;
    world::WorldEffectParameters worldEffectParams_;
    // ADR-230, on the same terms: the live set and the `atmos/...` parameters that own it.
    std::vector<world::AtmosphericEffect> atmosphericEffects_;
    // ADR-562: the last reported dropped-media count, so the warning is once per change rather than
    // once per frame.
    std::uint32_t lastMediaDropped_ = 0;
    world::AtmosphericParameters atmosphericParams_;
    // The aurora's spectrum, resolved each frame from the analysis frame every other consumer
    // reads. `kAuroraBands` entries; see `world/atmospherics.hpp` for why this one vector is not a
    // modulation route.
    std::array<float, world::kAuroraBands> auroraSpectrum_{};
    // §68. The scene's published spatial fields, rebuilt from scratch each frame by
    // `publishFields()` immediately before the atmospheric resolve reads it. A member rather than a
    // local so its storage is reused, and cleared-then-filled rather than updated in place so that
    // a field whose publisher went away this frame cannot linger as a name that still resolves.
    world::fields::FieldBus fieldBus_;
    // Names already reported as naming a field nobody publishes, so the log says it once per name
    // instead of sixty times a second. Cleared whenever the effect list changes.
    std::vector<std::string> reportedDeadFields_;
    std::vector<world::ShotSpan> shotSpans_;
    AutoDirectorSettings autoDirector_; // ADR-225: saved with the project, read by the host
    SongPlan songPlan_;                 // ADR-249: the same, for Song Mode's authored intents
    std::uint32_t lastWorldEffectCount_ = 0;
    std::uint32_t lastAtmosphericCount_ = 0;
    void updateWorldEffects();
    void updateAtmosphericEffects();
    // §68. Fills `fieldBus_` with everything this scene publishes: the world's wind, and one field
    // per live vortex effect. Called from `updateAtmosphericEffects` before the resolve.
    void publishFields();
    void updateAuroraSpectrum();
    [[nodiscard]] glm::vec3 cameraVelocityOnTimeline() const;
    scene::PostParameters postParams_;
    // ADR-410. Registered beside the post parameters and applied beside them, because the failure
    // this repo keeps paying for is a system that is built, tested and unreachable: a parameter
    // nothing registers is a parameter no panel can draw and no project can keep.
    scene::TemporalSettings temporal_;
    scene::TemporalParameters temporalParams_;
    scene::TemporalHistoryReport temporalReport_;
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
    // ADR-440. `projectBaseline_` is the document a save would have written at the moment this
    // project last agreed with the disk; `projectBaselineValid_` is false only before the first
    // load or save, when there is nothing to compare against and nothing to lose.
    nlohmann::json projectBaseline_;
    bool projectBaselineValid_ = false;
    bool projectDirty_ = false;
    // Appends to projectWarnings_ if it is not already there. Unresolved bindings are re-reported
    // on every rebind, and a warning list that grew a duplicate per scene swap would stop being
    // read.
    void noteBindingProblem(std::string message);
    std::vector<std::string> projectWarnings_;
    LoadReporter loadReporter_;
    std::vector<std::pair<std::string, double>> loadTimings_;
    // Beat clock extrapolated per render frame from the analysis tempo (ADR-012).
    double beatClockPhase_ = 0.0;
    std::uint32_t beatClockCount_ = 0;
    std::uint32_t lastAnalysisBeatCount_ = 0;

    // Installs a decoded buffer as *the* audio: the player's source, the analysis runner, the
    // offline analysis track and `audioFile_`. The one place that does it, so `loadAudio` and a
    // re-mix of the arrangement cannot drift apart. A buffer with no frames means "no audio".
    [[nodiscard]] Result<void> installAudio(std::shared_ptr<const audio::AudioFile> file);

    std::vector<audio::AudioClip> audioClips_;
    std::uint64_t audioRevision_ = 1;
    audio::ClipSources clipSources_;
    audio::MixReport audioMix_;
    // Embedded Tempo, captured when the arrangement's sources were loaded. Not derived from
    // `audioFile_`: that is a mixdown with no container to read (ADR-394).
    audio::AudioTempo embeddedTempo_;
    // The artist's tempo. Persisted; `available` false means "not overridden", which is not the
    // same as 0 bpm.
    audio::AudioTempo tempoOverride_;
    Transport transport_;
    // The interactive seek's outstanding request. See `requestSeek`.
    double seekRequestSeconds_ = 0.0;
    double seekEvaluatedSeconds_ = 0.0;
    bool seekPending_ = false;
    bool seekGestureHeld_ = false;
    double interactiveSeekBudgetMs_ = 0.0;
    // The ceiling on one seek's re-simulation, in body-steps (`entity::SeekBudget`). 0 = none,
    // which is what every non-live mode keeps.
    std::uint64_t seekBodyStepBudget_ = 0;
    scene::RebuildDeferral seekDeferral_;
    // What the audio player was last told to do, so the engine can tell whether the device needs
    // starting, stopping or seeking this frame without asking it every frame.
    bool audioFollowing_ = false;
    std::shared_ptr<const audio::AudioFile> audioFile_;
    std::filesystem::path audioPath_;
    analysis::AnalyzerConfig analyzerConfig_;
    std::unique_ptr<audio::AudioPlayer> player_;
    std::unique_ptr<analysis::AnalysisRunner> runner_;
    std::shared_ptr<analysis::AnalysisTrack> track_;
    std::size_t offlineFrameCursor_ = 0;

    analysis::AnalysisFrame latest_;
    bool hasFrame_ = false;
    double lastRenderTime_ = 0.0;
    EngineStats stats_;
};

} // namespace avgen::app
