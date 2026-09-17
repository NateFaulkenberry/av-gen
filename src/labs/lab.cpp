#include "labs/lab.hpp"

#include <array>
#include <string>

namespace avgen::labs {
namespace {

// Every `decides` entry below was read out of the code, not inferred from a directory name. Where
// the spec's expected answer and the engine's actual one disagree, the engine wins and the
// disagreement is written down here rather than smoothed over -- that is §37 applied to the
// ownership map itself, and three of the fourteen entries below are corrections:
//
//   * There is no occlusion culling anywhere in the engine, so the Visibility Lab cannot own an
//     occlusion decision and `OCCLUSION_CULLED` is not in the reason vocabulary.
//   * There is no TAA. `shaders/post.wgsl` antialiases with FXAA and says so. The Temporal Lab's
//     subjects are the four things that genuinely carry state across frames: GTAO's history, the
//     LOD ladder's hysteresis and spread, the async cull readback, and the exposure meter.
//   * Entity LOD is not live. `rendering::RepresentationSelector` exists, is tested, and is called
//     by nothing in `scene_renderer.cpp`; the ladder that runs is the scatter one in `cull.wgsl`
//     and the terrain one in `world::chunkLod`.
constexpr std::array<LabDescriptor, 14> kLabs{{
    {LabId::Animation, "animation", "Animation Lab", LabStatus::InProgress,
     "Is this pose the one the clip asked for at this time?",
     "clip sampling, blending and the joint palette a frame is skinned with",
     "where the character stands while it plays -- that is the Character Lab",
     "src/scene/animation.cpp", "docs/engineering-labs.md", "examples/characters/alien.json", ""},

    {LabId::Character, "character", "Character Runtime Lab", LabStatus::InProgress,
     "Why is this character here, facing this way, with its feet at this height?",
     "root motion, grounding, steering and the navigation decisions behind them",
     "what the joints do once the entity is placed -- that is the Animation Lab",
     "src/entity/entity.cpp", "docs/engineering-labs.md",
     "examples/characters/alien-wander.json", ""},

    {LabId::Visibility, "visibility", "Visibility Lab", LabStatus::InProgress,
     "Why was this object not submitted?",
     "whether an object reaches a draw call at all: the entity cull, the GPU instance cull, the "
     "terrain chunk cull and the separate shadow-caster cull",
     "which representation a submitted object is drawn with -- that is the LOD Lab",
     "src/scene/composition.cpp:cullEntityNodes", "docs/engineering-labs.md",
     "examples/qa/renderer-qa.scene.json", ""},

    {LabId::Lod, "lod", "LOD / Geometry Lab", LabStatus::Planned,
     "Which representation was chosen, and does the choice hold still?",
     "the scatter ladder's level selection, its spread and hysteresis, and the terrain chunk ladder",
     "whether the object was culled at all -- that is the Visibility Lab",
     "shaders/cull.wgsl:cs_cull_classify", "docs/engineering-labs.md",
     "examples/stress/stress.json", ""},

    {LabId::Camera, "camera", "Camera / Framing Lab", LabStatus::Planned,
     "Is the camera where the shot says, and can it see what it is framing?",
     "camera evaluation, the frustum the cull is given, terrain clearance and line of sight to the "
     "subject",
     "which objects survive that frustum -- that is the Visibility Lab",
     "src/world/camera_clearance.cpp", "docs/engineering-labs.md",
     "examples/camera/behaviors.json", ""},

    {LabId::Shadow, "shadow", "Shadow Lab", LabStatus::Planned,
     "Why is this surface lit when it should be shadowed, or shadowed when it should be lit?",
     "cascade fitting and splits, the caster list, the atlas, the half-resolution mask and the "
     "contact march",
     "the light's own position and intensity -- that is the Lighting Lab",
     "src/rendering/shadow_math.cpp:fitDirectionalCascade", "docs/engineering-labs.md",
     "examples/qa/renderer-qa.scene.json", ""},

    {LabId::Lighting, "lighting", "Lighting Lab", LabStatus::Planned,
     "Which lights reached this pixel, and with how much?",
     "light packing, the froxel cluster assignment, the area-light LTC path and the IBL",
     "whether a light is occluded -- that is the Shadow Lab",
     "src/rendering/light_data.cpp:assignClusters", "docs/engineering-labs.md",
     "examples/lightrigs", ""},

    {LabId::Hdr, "hdr", "HDR / Exposure / Bloom Lab", LabStatus::Planned,
     "Why is this frame this bright, and where did the glow come from?",
     "metering, the exposure state, bloom, halation and the tonemap operator",
     "the radiance that entered the pipe -- that is the Lighting Lab",
     "src/rendering/post_processor.cpp:PostProcessor::run", "docs/engineering-labs.md",
     "examples/looks", ""},

    {LabId::Volumetric, "volumetric", "Volumetric / Atmosphere Lab", LabStatus::Planned,
     "What is the air doing between the camera and the subject?",
     "the volumetric march, its resolution and step scaling, the depth-aware composite, and the "
     "atmospheric effects",
     "the bloom that the in-scatter later feeds -- that is the HDR Lab",
     "src/rendering/volume_renderer.cpp", "docs/engineering-labs.md",
     "examples/world/glowmere-atmospherics.json", ""},

    {LabId::Particle, "particle", "Particle / VFX Lab", LabStatus::Planned,
     "How many particles are alive, where, and what are they writing?",
     "emission, simulation, compaction and the indirect draw -- and what particles write into the "
     "velocity target",
     "the world-effect fields that push them -- those are authored scene data",
     "src/rendering/particle_renderer.cpp", "docs/engineering-labs.md",
     "examples/qa/renderer-qa.scene.json", ""},

    {LabId::Temporal, "temporal", "Temporal Stability Lab", LabStatus::Planned,
     "Why does this pixel change when nothing moved?",
     "every piece of state that crosses a frame boundary: GTAO's history, LOD hysteresis and "
     "spread, the one-to-three-frame cull readback lag, and the exposure meter",
     "how loudly the instability reads to a person -- that is the Rendering Lab's measurement",
     "src/rendering/ao_renderer.cpp", "docs/engineering-labs.md",
     "examples/quality/aliasing-dolly.json", ""},

    {LabId::Aov, "aov", "AOV / Render Diagnostics Lab", LabStatus::Planned,
     "Does the auxiliary view agree with the beauty frame it was taken from?",
     "the five scene targets, the linear depth pass, the `--aov` export and the `--debug-target` "
     "views",
     "what the numbers in those files mean about quality -- that is the Rendering Lab",
     "src/app/render_job.cpp", "docs/engineering-labs.md",
     "examples/quality/aliasing.json", ""},

    {LabId::Rendering, "rendering", "Rendering Lab", LabStatus::Built,
     "Is this frame better or worse than that one, and by how much?",
     "measuring a finished frame or sequence: the quality vector, the distortion ladder, the "
     "artifact detectors and the report",
     "why the renderer produced the frame -- every other lab owns a piece of that",
     "tools/quality-lab/report/vector.cpp", "docs/quality-lab/README.md",
     "examples/quality/aliasing.json", "examples/labs/rendering/cases.json"},

    {LabId::Integration, "integration", "Integration / Stress Lab", LabStatus::Planned,
     "Does the whole thing still hold up at production scale?",
     "the scenes where every subsystem runs at once, and the counters they are certified against",
     "any single subsystem's behaviour -- isolate it in that subsystem's lab first",
     "tests/rendering/test_phase_g_certification.cpp", "docs/engineering-labs.md",
     "examples/stress/stress.json", ""},
}};

} // namespace

std::span<const LabDescriptor> labs() { return kLabs; }

const LabDescriptor& lab(LabId id) { return kLabs[static_cast<std::size_t>(id)]; }

std::optional<LabId> findLab(std::string_view key) {
    for (const LabDescriptor& d : kLabs) {
        if (d.key == key) {
            return d.id;
        }
    }
    return std::nullopt;
}

std::string_view labKeys() {
    // Built once from the table rather than typed a second time: a list of names that is maintained
    // beside the names is a list that eventually disagrees with them, and the disagreement shows up
    // as an error message offering a lab that does not exist.
    static const std::string text = [] {
        std::string out;
        for (const LabDescriptor& d : kLabs) {
            if (!out.empty()) {
                out += ", ";
            }
            out += d.key;
        }
        return out;
    }();
    return text;
}

std::string_view statusName(LabStatus status) {
    switch (status) {
    case LabStatus::Built: return "built";
    case LabStatus::InProgress: return "in progress";
    case LabStatus::Planned: return "planned";
    }
    return "planned";
}

} // namespace avgen::labs
