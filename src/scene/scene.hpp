#pragma once

// The scene data model (ADR-004): value types in scene_types.hpp; this header adds the
// particle, post and procedural components and the Scene container.

#include "scene/animation.hpp"
#include "scene/particles.hpp"
#include "scene/post_settings.hpp"
#include "scene/composition_data.hpp"
#include "scene/light_rig.hpp"
#include "scene/procedural.hpp"
#include "scene/material_program.hpp"
#include "scene/sdf_object.hpp"
#include "scene/water_surface.hpp"
#include "spatial/field.hpp"
#include "spatial/spline.hpp"
#include "scene/scene_types.hpp"

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace avgen::scene {

// ---- scene ---------------------------------------------------------------------------------

struct Scene {
    Camera camera;                      // the active camera
    std::vector<Camera> cameras;        // imported cameras (the first becomes active on import)
    std::vector<PunctualLight> lights;
    Environment environment;
    std::vector<MeshData> meshes;
    std::vector<TextureData> textures;
    std::vector<Entity> entities;
    std::vector<ParticleSystem> particles;
    std::vector<ProceduralGeometry> procedurals; // ADR-023; evaluated by rendering::ProceduralRenderer
    spatial::FieldSet fields;                    // ADR-025; sampled by effectors, deformers, particles, materials
    CompositionData composition;                 // ADR-038; what the frame is about
    spatial::SplineSet splines;                  // ADR-026; distributions, path deformers, camera, emitters
    std::vector<SdfObject> sdfs;                 // ADR-027; rendering::SdfRenderer / meshed entities
    std::vector<MaterialProgram> materialPrograms; // ADR-030; referenced by Material::program
    // ADR-099: the water surfaces of this scene, one per distinct look. An entity drawn with
    // MeshStyle::Water finds its settings by the material-program name it carries, which is how a
    // water surface reaches rendering::WaterRenderer without an index on every Entity in a world.
    std::vector<WaterSurface> waters;
    // ADR-086: the skinned characters. An Entity names one through Entity::rig; scene::updateRigs
    // advances them from the controller's update, never from the renderer.
    std::vector<SkinnedRig> rigs;
    PostSettings post;                // built-in post-processing (copied in by the Engine)
    std::uint64_t meshVersion = 0;    // incremented when meshes change (renderer re-uploads)
    std::uint64_t textureVersion = 0; // incremented when textures change

    MeshId addMesh(MeshData mesh);
    // A mesh's world-space-agnostic bounds, computed once per `meshVersion`. MeshData::bounds()
    // scans every vertex, and bounds() below is called every frame by the renderer to size the
    // shadow cascades; before terrain that was a handful of meshes and now it is a world's worth,
    // so the answer is cached against the version that already says when meshes changed.
    [[nodiscard]] const std::pair<glm::vec3, glm::vec3>& meshBounds(MeshId mesh) const;
    TextureId addTexture(TextureData texture);
    Entity& addEntity(std::string name, MeshId mesh);
    PunctualLight& addLight(PunctualLight light);
    // World-space bounds over visible lit entities (their mesh bounds transformed by their matrix).
    [[nodiscard]] std::pair<glm::vec3, glm::vec3> bounds() const;
    void clear();

private:
    mutable std::vector<std::pair<glm::vec3, glm::vec3>> meshBoundsCache_;
    mutable std::uint64_t meshBoundsVersion_ = ~0ULL;
};

// ---- culling bounds (ADR-046 culling, renderer forensics Phase 5.1) ----------------------------
//
// The world-space box the camera cull tests an entity against. Extracted from `Composition` because
// it was written out twice there -- once for EntityWorld-driven characters and once for authored
// mesh nodes -- and because the property it has to satisfy is not one a composition test can reach:
// *a posed skeleton may reach outside its bind-pose bounds, and the box must still contain it.*
//
// A bind pose is usually the widest a character gets (a T-pose has the arms out), so for most rigs
// bind-pose bounds are conservative and the distinction is invisible. It is a reach, a jump or a
// swing -- a pose that extends past bind -- where using the wrong one makes a limb disappear at a
// frustum edge, and that is what `tests/unit/test_skeleton.cpp` exercises.
//
// `padFraction` and `padAbsolute` are the conservative residual: interpolation between palette
// updates and numerical edge cases can put a vertex slightly outside the box computed from the
// palette this frame.
struct CullBounds {
    glm::vec3 min{0.0f};
    glm::vec3 max{0.0f};
    bool posed = false; // the palette was used; false means the mesh's own bind-pose bounds
};

[[nodiscard]] CullBounds entityCullBounds(const Scene& scene, const Entity& entity,
                                          float padFraction = 0.25f, float padAbsolute = 0.25f);

} // namespace avgen::scene
