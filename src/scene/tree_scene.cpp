#include "scene/mesh_generators.hpp"
#include "scene/tree_mesh.hpp"
#include "scene/tree_scene.hpp"

#include "core/noise.hpp"

#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <array>
#include <cmath>

namespace avgen::scene {
namespace {

MeshData makeGroundDisc(float radius, int segments) {
    MeshData mesh;
    mesh.name = "tree.ground";
    mesh.vertices.push_back(Vertex{{0.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, {0.5f, 0.5f}});
    for (int i = 0; i <= segments; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(segments) * glm::two_pi<float>();
        const float c = std::cos(t);
        const float s = std::sin(t);
        mesh.vertices.push_back(
            Vertex{{c * radius, 0.0f, s * radius}, {0.0f, 1.0f, 0.0f}, {0.5f + c * 0.5f, 0.5f + s * 0.5f}});
    }
    for (int i = 0; i < segments; ++i) {
        mesh.indices.insert(mesh.indices.end(),
                            {0u, static_cast<std::uint32_t>(i + 2), static_cast<std::uint32_t>(i + 1)});
    }
    return mesh;
}

// A distant tree: a tapered trunk and two overlapping blobs. Forty triangles, seen at sixty metres
// through mist, and it only has to read as "a tree of ordinary size".
MeshData makeDistantTree(float height, float lean, std::uint32_t seed) {
    MeshData mesh;
    const float trunkH = height * 0.42f;
    const float r = height * 0.022f;
    constexpr int kSides = 5;
    for (int ring = 0; ring < 2; ++ring) {
        const float y = ring == 0 ? 0.0f : trunkH;
        const float rr = ring == 0 ? r : r * 0.55f;
        const float x = ring == 0 ? 0.0f : lean;
        for (int i = 0; i < kSides; ++i) {
            const float a = static_cast<float>(i) / kSides * glm::two_pi<float>();
            const glm::vec3 n(std::cos(a), 0.0f, std::sin(a));
            mesh.vertices.push_back(Vertex{glm::vec3(x, y, 0.0f) + n * rr, n,
                                           {static_cast<float>(i) / kSides, static_cast<float>(ring)}});
        }
    }
    for (int i = 0; i < kSides; ++i) {
        const auto a = static_cast<std::uint32_t>(i);
        const auto b = static_cast<std::uint32_t>((i + 1) % kSides);
        mesh.indices.insert(mesh.indices.end(), {a, b, b + kSides, a, b + kSides, a + kSides});
    }
    for (int blob = 0; blob < 2; ++blob) {
        const float br = height * (blob == 0 ? 0.26f : 0.19f);
        const glm::vec3 c(lean * (blob == 0 ? 1.0f : 1.6f), trunkH + height * (blob == 0 ? 0.22f : 0.40f),
                          height * 0.05f * (blob == 0 ? 1.0f : -1.0f) *
                              (noise::hashIndex(seed, static_cast<std::uint32_t>(blob), 7u) * 2.0f - 1.0f));
        MeshData sphere = makeIcosphere(br, 1);
        for (Vertex& v : sphere.vertices) {
            v.position = c + v.position * glm::vec3(1.0f, 0.78f, 1.0f);
        }
        appendMesh(mesh, sphere);
    }
    return mesh;
}

void addDistantTrees(Scene& scene, const TreeLook& look, std::uint32_t seed) {
    if (look.distantTrees <= 0) {
        return;
    }
    MeshData all;
    all.name = "tree.distant";
    for (int i = 0; i < look.distantTrees; ++i) {
        const auto index = static_cast<std::uint32_t>(i);
        // Stratified in angle so they ring the hero instead of clumping, then jittered so the ring
        // is not a ring.
        float angle = (static_cast<float>(i) + noise::hashIndex(seed, index, 11u)) /
                      static_cast<float>(look.distantTrees) * glm::two_pi<float>();
        // Keep them out of the wedge directly behind the hero, where they would read as growing out
        // of its crown.
        const float toCamera = glm::half_pi<float>();
        if (std::abs(std::remainder(angle - toCamera, glm::two_pi<float>())) < look.distantClearAngle) {
            angle += look.distantClearAngle * 2.0f;
        }
        const float t = noise::hashIndex(seed, index, 12u);
        const float distance = look.distantNear + t * t * (look.distantFar - look.distantNear);
        const float height = look.distantHeightMin +
                             noise::hashIndex(seed, index, 13u) * (look.distantHeightMax - look.distantHeightMin);
        const float lean = (noise::hashIndex(seed, index, 14u) * 2.0f - 1.0f) * height * 0.06f;

        MeshData one = makeDistantTree(height, lean, seed ^ (index * 977u));
        const glm::mat4 xform =
            glm::translate(glm::mat4(1.0f), glm::vec3(std::cos(angle) * distance, 0.0f, std::sin(angle) * distance)) *
            glm::rotate(glm::mat4(1.0f), noise::hashIndex(seed, index, 15u) * glm::two_pi<float>(),
                        glm::vec3(0.0f, 1.0f, 0.0f));
        for (Vertex& v : one.vertices) {
            v.position = glm::vec3(xform * glm::vec4(v.position, 1.0f));
            v.normal = glm::normalize(glm::vec3(xform * glm::vec4(v.normal, 0.0f)));
        }
        appendMesh(all, one);
    }
    Material m;
    m.baseColor = look.barkColor * 0.6f;
    m.roughness = 0.95f;
    const MeshId id = scene.addMesh(std::move(all));
    Entity& e = scene.addEntity("tree.distant", id);
    e.material = m;
    e.materialName = "tree.distant";
    // They must not cast: the hero owns the one cascaded shadow light, and a ring of distant trees
    // in the cascade fit pushes its far plane out and coarsens every shadow on the hero itself.
    e.castsShadow = false;
}

void addPart(Scene& scene, const MeshData& mesh, const std::string& name, const Material& material) {
    if (mesh.vertices.empty()) {
        return;
    }
    MeshData copy = mesh;
    copy.name = name;
    const MeshId id = scene.addMesh(std::move(copy));
    Entity& entity = scene.addEntity(name, id);
    entity.material = material;
    entity.materialName = name;
    entity.style = MeshStyle::Lit;
}

} // namespace

Result<Scene> buildTreeScene(const TreeGraph& graph, const TreeMeshes& meshes, const TreeCameraView& camera,
                             const TreeLook& look) {
    if (graph.nodes.empty()) {
        return fail("tree scene: the graph has no nodes");
    }
    Scene scene;

    // ---- camera. The same one the candidate search evaluated with, or the tree was chosen for a
    // shot nobody takes. Static: no orbit, no rail, no shake.
    scene.camera.name = "tree";
    scene.camera.position = camera.eye;
    scene.camera.target = camera.target;
    scene.camera.up = camera.up;
    scene.camera.fovYRadians = camera.fovYRadians;
    scene.camera.lens.useExplicitFov = true;

    // ---- materials, one per tier.
    // The vein program, shared by every branch tier: they differ in gain, not in pattern.
    if (look.veinsEnabled) {
        VeinSettings veins = look.veins;
        veins.seed = graph.params.seed;
        veins.color = look.veinColor;
        scene.materialPrograms.push_back(makeVeinProgram("tree.veins", veins));
    }
    const auto branchMaterial = [&look](float gain) {
        Material m;
        m.baseColor = look.barkColor;
        m.roughness = look.barkRoughness;
        m.metallic = 0.0f;
        if (look.veinsEnabled) {
            m.program = "tree.veins";
            // ZERO, deliberately. The program asserts emission, so anything written here is
            // discarded by the shader -- and a value that looks authored but is never read is how
            // an art-direction rule passes review and then does nothing.
            m.emissiveIntensity = 0.0f;
            m.emissiveColor = look.veinColor;
        } else {
            m.emissiveColor = look.veinColor;
            m.emissiveIntensity = gain;
        }
        return m;
    };
    addPart(scene, meshes.roots, "tree.roots", [&look] {
        Material m;
        m.baseColor = look.rootColor;
        m.roughness = 0.85f;
        m.emissiveColor = look.veinColor;
        m.emissiveIntensity = look.tertiaryEmissive * 0.5f;
        return m;
    }());
    addPart(scene, meshes.trunk, "tree.trunk", branchMaterial(look.trunkEmissive));
    addPart(scene, meshes.primary, "tree.primary", branchMaterial(look.primaryEmissive));
    addPart(scene, meshes.secondary, "tree.secondary", branchMaterial(look.secondaryEmissive));
    addPart(scene, meshes.tertiary, "tree.tertiary", branchMaterial(look.tertiaryEmissive));
    for (int i = 0; i < kFoliageTints; ++i) {
        const auto t = static_cast<std::size_t>(i);
        Material m;
        m.baseColor = look.foliageColor[t];
        m.roughness = look.foliageRoughness;
        m.emissiveColor = look.foliageEmissiveTint[t];
        m.emissiveIntensity = look.foliageEmissiveIntensity * look.foliageEmissiveScale[t];
        // Two-sided, because a leaf card seen from behind is still a leaf. Without this half the
        // canopy is missing from any given angle and the crown reads as hollow.
        m.doubleSided = true;
        addPart(scene, meshes.foliage[t], "tree.foliage" + std::to_string(i), m);
    }

    addDistantTrees(scene, look, graph.params.seed ^ 0xD157u);

    if (look.includeGround) {
        Material ground;
        ground.baseColor = look.groundColor;
        ground.roughness = 0.92f;
        addPart(scene, makeGroundDisc(look.groundRadius, 96), "tree.ground", ground);
    }

    // ---- lighting. Rim-led: the brief asks for atmospheric separation and for the tree to stay
    // readable with emission off, which a key-led rig does not give against a dark sky.
    PunctualLight key;
    key.name = "moon";
    key.type = PunctualLight::Type::Directional;
    key.role = PunctualLight::Role::Key;
    key.direction = glm::normalize(glm::vec3(-0.42f, -0.68f, -0.60f));
    key.intensity = look.keyIntensity;
    key.temperature = 8200.0f;
    // Exactly one shadow caster. A second cascaded directional light is not rendered at all, and
    // finding that out from a black frame is a bad afternoon.
    key.castsShadow = true;
    key.shadowStrength = 0.82f;
    key.softness = 2.4f;
    key.volumetricStrength = 0.40f;
    scene.addLight(key);

    PunctualLight rim;
    rim.name = "rim";
    rim.type = PunctualLight::Type::Directional;
    rim.role = PunctualLight::Role::Rim;
    rim.direction = glm::normalize(glm::vec3(0.55f, -0.22f, 0.78f));
    rim.color = glm::vec3(0.72f, 0.58f, 1.0f);
    rim.intensity = look.rimIntensity;
    rim.temperature = 7400.0f;
    rim.castsShadow = false;
    // Off, deliberately: contact shadows default on for every light, and a marching pass per light
    // was measured at 19% of the scene pass. A light with no shadow map has nothing to contact.
    rim.contactShadow = false;
    rim.volumetricStrength = 0.0f;
    scene.addLight(rim);

    PunctualLight fill;
    fill.name = "skyfill";
    fill.type = PunctualLight::Type::Directional;
    fill.role = PunctualLight::Role::Fill;
    fill.direction = glm::normalize(glm::vec3(0.15f, -1.0f, 0.1f));
    fill.intensity = look.fillIntensity;
    fill.temperature = 11000.0f;
    fill.castsShadow = false;
    fill.contactShadow = false;
    fill.volumetricStrength = 0.0f;
    scene.addLight(fill);

    // ---- environment. The fog colour is DARKER than the sky's horizon on purpose: distance fog
    // pulls a surface toward `fogColor`, so a fog brighter than the background makes far things
    // brighter with distance -- the opposite of a silhouette, and a mistake this project has
    // already made once and written down.
    scene.environment.sky.enabled = true;
    scene.environment.sky.zenithColor = look.zenith;
    scene.environment.sky.horizonColor = look.horizon;
    scene.environment.sky.showBackground = true;
    scene.environment.sky.sunIntensity = 0.0f; // no sun disc: this is a night shot
    scene.environment.fogColor = look.fogColor;
    scene.environment.fogDensity = look.fogDensity;
    scene.environment.volumeDensity = look.volumeDensity;
    scene.environment.fogHeight = look.fogHeight;
    scene.environment.fogHeightFalloff = look.fogHeightFalloff;
    // Nearly isotropic, following Glowmere. A high anisotropy with a bright source near the view
    // axis turns the whole shot into glare, and this scene has a glowing canopy in the middle of it.
    scene.environment.volumeAnisotropy = 0.12f;
    scene.environment.volumeSteps = 24;
    scene.environment.volumeMaxDistance = 180.0f;
    scene.environment.environmentIntensity = 0.30f;
    scene.environment.skyIntensity = 0.55f;

    // ---- post. Bloom at Glowmere's shipped settings, so only the foliage -- the one thing above
    // the ladder's gap -- crosses the threshold. Anamorphic stays OFF: it is currently a tap comb
    // on compact bright features, which is exactly what a canopy of glowing cards is.
    scene.post.tonemap = TonemapOperator::AgX;
    scene.post.bloomEnabled = true;
    scene.post.bloomIntensity = 0.20f;
    scene.post.bloomThreshold = 1.0f;
    scene.post.bloomKnee = 0.5f;
    scene.post.bloomRadius = 1.15f;
    scene.post.bloomEmissionWeight = 0.75f;
    scene.post.antialias = 0.75f;
    scene.post.vignette = 0.22f;
    return scene;
}

Result<Scene> buildTreeScene(const TreeParams& params, const TreeCameraView& camera, const TreeLook& look,
                             const TreeMeshSettings& mesh) {
    auto graph = generateTree(params);
    if (!graph) {
        return std::unexpected(graph.error());
    }
    auto meshes = buildTreeMeshes(*graph, mesh);
    if (!meshes) {
        return std::unexpected(meshes.error());
    }
    return buildTreeScene(*graph, *meshes, camera, look);
}


Result<TreeSceneBuild> buildAnimatedTree(const TreeParams& params, const TreeCameraView& camera,
                                         const TreeLook& look, const TreeMeshSettings& mesh,
                                         const TreeRigSettings& rigSettings) {
    TreeSceneBuild out;
    auto graph = generateTree(params);
    if (!graph) {
        return std::unexpected(graph.error());
    }
    out.graph = std::move(*graph);
    auto meshes = buildTreeMeshes(out.graph, mesh);
    if (!meshes) {
        return std::unexpected(meshes.error());
    }
    out.meshes = std::move(*meshes);
    auto rig = buildTreeRig(out.graph, rigSettings);
    if (!rig) {
        return std::unexpected(rig.error());
    }
    out.rig = std::move(*rig);
    // Skinning happens BEFORE the scene is assembled, because `addPart` copies each mesh into the
    // scene and a skin attached afterwards would land on the copy nobody draws.
    if (auto ok = skinTreeMeshes(out.graph, out.rig, out.meshes); !ok) {
        return std::unexpected(ok.error());
    }
    auto scene = buildTreeScene(out.graph, out.meshes, camera, look);
    if (!scene) {
        return std::unexpected(scene.error());
    }
    out.scene = std::move(*scene);
    out.scene.rigs.push_back(makeSkinnedRig(out.rig));
    for (Entity& entity : out.scene.entities) {
        // The condition is whether the MESH carries skin data, not what the entity is called. The
        // name test this replaced ("anything but the ground") bound the distant trees to the hero's
        // rig the moment they were added -- they share the `tree.` prefix, they are environment, and
        // an entity naming a rig its mesh has no weights for is undefined at best.
        entity.rig = out.scene.meshes[entity.mesh].skinned() ? 0 : kInvalidRig;
    }
    out.triangles = out.meshes.triangles;
    return out;
}

} // namespace avgen::scene
