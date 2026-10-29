// ADR-1819: a glTF glass imports as screen-space refraction. KHR_materials_transmission, _ior and _volume map onto
// Material::refraction; a model without them, or the importer flag off, imports exactly as before.
//
// The fixture (glass.glb, plain.glb, glass-blend.glb, glass-thin.glb) is built by tools/make_glass_test_glb.py into the build tree; nothing downloaded.

#include "assets/gltf_loader.hpp"
#include "scene/scene.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <filesystem>
#include <string>

using namespace avgen;
namespace fs = std::filesystem;

namespace {

// The fixture's node order: 0 = the glass sphere, 1 = the first (red) stripe.
const scene::Material* materialOf(const scene::Scene& s, const std::string& name) {
    const std::size_t index = name == "glass" ? 0u : 1u;
    return index < s.entities.size() ? &s.entities[index].material : nullptr;
}

} // namespace

TEST_CASE("a glTF glass imports as screen-space refraction, and only a glass does", "[assets][gltf][adr1819]") {
    const fs::path dir(AVGEN_GLASS_FIXTURE_DIR);
    if (!fs::exists(dir / "glass.glb")) {
        SKIP("the glass fixture was not generated (no Python 3 at configure time)");
    }

    scene::Scene glass;
    REQUIRE(assets::loadGltf(dir / "glass.glb", glass).has_value());
    const scene::Material* g = materialOf(glass, "glass");
    REQUIRE(g != nullptr);
    CHECK(g->refraction.enabled());
    CHECK(g->refraction.transmission == 1.0f);
    CHECK(g->refraction.ior == Catch::Approx(1.5f));
    CHECK(g->refraction.strength == 0.0f); // a glass follows glTF's ior and volume, not ADR-1603's offset
    CHECK(g->refraction.opacity == 0.0f);
    CHECK(g->refraction.thickness == Catch::Approx(1.0f));
    CHECK(g->refraction.absorption.x == Catch::Approx(-std::log(0.55f))); // attenuation over 1 m
    CHECK(g->refraction.absorption.y == Catch::Approx(-std::log(0.85f)));
    CHECK(g->refraction.absorption.z == Catch::Approx(-std::log(0.8f)));
    // The stripes are ordinary materials.
    REQUIRE(materialOf(glass, "red") != nullptr);
    CHECK_FALSE(materialOf(glass, "red")->refraction.enabled());

    // The importer flag off: the glass is the opaque material it was before ADR-1819.
    scene::Scene off;
    assets::GltfLoadOptions noTransmission;
    noTransmission.importTransmission = false;
    REQUIRE(assets::loadGltf(dir / "glass.glb", off, noTransmission).has_value());
    REQUIRE(materialOf(off, "glass") != nullptr);
    CHECK_FALSE(materialOf(off, "glass")->refraction.enabled());

    // A model without the extensions: no refraction, whatever the flag says.
    scene::Scene plain;
    REQUIRE(assets::loadGltf(dir / "plain.glb", plain).has_value());
    REQUIRE(materialOf(plain, "glass") != nullptr);
    CHECK_FALSE(materialOf(plain, "glass")->refraction.enabled());
    CHECK(materialOf(plain, "glass")->refraction.transmission == 0.0f);

    // Without KHR_materials_volume: thin-walled (thickness 0), still a glass.
    scene::Scene thin;
    REQUIRE(assets::loadGltf(dir / "glass-thin.glb", thin).has_value());
    REQUIRE(materialOf(thin, "glass") != nullptr);
    CHECK(materialOf(thin, "glass")->refraction.enabled());
    CHECK(materialOf(thin, "glass")->refraction.thickness == 0.0f);
    CHECK(materialOf(thin, "glass")->refraction.absorption == glm::vec3(0.0f));

    // An exporter's BLEND fallback on a transmissive material: a glass that keeps its exported blend, so the renderer
    // can fall back to it where the ladder has no refraction; with the flag off, the blended fallback alone.
    scene::Scene blend;
    REQUIRE(assets::loadGltf(dir / "glass-blend.glb", blend).has_value());
    REQUIRE(materialOf(blend, "glass") != nullptr);
    CHECK(materialOf(blend, "glass")->refraction.enabled());
    CHECK(materialOf(blend, "glass")->alphaMode == scene::AlphaMode::Blend);
    CHECK(materialOf(blend, "glass")->opacity == 0.25f);
    scene::Scene blendOff;
    REQUIRE(assets::loadGltf(dir / "glass-blend.glb", blendOff, noTransmission).has_value());
    CHECK(materialOf(blendOff, "glass")->alphaMode == scene::AlphaMode::Blend);
    CHECK(materialOf(blendOff, "glass")->opacity == 0.25f);
}
