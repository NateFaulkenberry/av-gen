#include "scene/tree_scene.hpp"

#include <glm/gtc/constants.hpp>

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
    const auto branchMaterial = [&look](float emissive) {
        Material m;
        m.baseColor = look.barkColor;
        m.roughness = look.barkRoughness;
        m.metallic = 0.0f;
        m.emissiveColor = look.veinColor;
        m.emissiveIntensity = emissive;
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
    key.volumetricStrength = 0.10f;
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
    scene.environment.fogHeight = 6.0f;
    scene.environment.fogHeightFalloff = 0.09f;
    scene.environment.volumeAnisotropy = 0.14f;
    scene.environment.volumeSteps = 16;
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
        // The ground is not part of the tree and must not be skinned to it.
        if (entity.name != "tree.ground") {
            entity.rig = 0;
        }
    }
    out.triangles = out.meshes.triangles;
    return out;
}

} // namespace avgen::scene
