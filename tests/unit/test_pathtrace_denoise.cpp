// Denoising and the feature AOVs that feed it (ADR-353 Phase 4, spec sections 51, 61).
//
// These tests run in BOTH build configurations. Without `AVGEN_PATHTRACE_DENOISE` the denoiser must
// refuse with a message; with it, it must actually denoise. A test that only ran in one
// configuration would leave the other free to rot, and the default build is the one without.

#include "pathtrace/denoise.hpp"
#include "pathtrace/path_tracer.hpp"
#include "pathtrace/snapshot.hpp"
#include "scene/mesh_generators.hpp"
#include "scene/scene.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <vector>

using namespace avgen;
using Catch::Approx;

namespace {

float luminance(const glm::vec3& c) { return 0.2126f * c.x + 0.7152f * c.y + 0.0722f * c.z; }

scene::Scene noisyRoom() {
    scene::Scene s;
    s.environment.sky.enabled = false;
    s.environment.backgroundColor = glm::vec3(0.0f);
    s.camera.position = glm::vec3(0.0f, 1.5f, 3.4f);
    s.camera.target = glm::vec3(0.0f, 1.3f, 0.0f);
    s.camera.lens.useExplicitFov = true;
    s.camera.fovYRadians = 1.1f;

    const auto wall = [&](const char* name, glm::vec3 pos, glm::quat rot, glm::vec3 albedo) {
        const scene::MeshId id = s.addMesh(scene::makePlane(2.0f, 1));
        scene::Entity& e = s.addEntity(name, id);
        e.transform.position = pos;
        e.transform.rotation = rot;
        e.material.baseColor = albedo;
        e.material.roughness = 0.9f;
    };
    wall("floor", {0, 0, 0}, glm::quat(1, 0, 0, 0), {0.8f, 0.8f, 0.8f});
    wall("ceiling", {0, 4, 0}, glm::angleAxis(3.14159265f, glm::vec3(1, 0, 0)), {0.8f, 0.8f, 0.8f});
    wall("back", {0, 2, -2}, glm::angleAxis(1.5707963f, glm::vec3(1, 0, 0)), {0.8f, 0.8f, 0.8f});
    // Two coloured side walls, so the albedo AOV has something to be RIGHT about.
    wall("left", {-2, 2, 0}, glm::angleAxis(-1.5707963f, glm::vec3(0, 0, 1)), {0.75f, 0.12f, 0.12f});
    wall("right", {2, 2, 0}, glm::angleAxis(1.5707963f, glm::vec3(0, 0, 1)), {0.12f, 0.55f, 0.18f});

    const scene::MeshId emitId = s.addMesh(scene::makePlane(0.5f, 1));
    scene::Entity& emitter = s.addEntity("emitter", emitId);
    emitter.transform.position = glm::vec3(0.0f, 3.95f, 0.0f);
    emitter.transform.rotation = glm::angleAxis(3.14159265f, glm::vec3(1.0f, 0.0f, 0.0f));
    emitter.material.baseColor = glm::vec3(0.0f);
    emitter.material.emissiveColor = glm::vec3(1.0f);
    emitter.material.emissiveIntensity = 9.0f;
    return s;
}

pathtrace::Framebuffer render(const scene::Scene& sc, std::uint32_t spp, bool features) {
    pathtrace::TraceSettings t;
    t.width = 128;
    t.height = 96;
    t.samplesPerPixel = spp;
    t.maxDepth = 3;
    t.russianRouletteDepth = 2;
    t.captureFeatures = features;
    pathtrace::PathTracer tr;
    pathtrace::Framebuffer fb;
    REQUIRE(tr.render(pathtrace::buildSnapshot(sc), t, fb).has_value());
    return fb;
}

// Mean absolute difference between horizontal neighbours: how grainy the image is.
double roughness(const std::vector<glm::vec3>& img, std::uint32_t w, std::uint32_t h) {
    double sum = 0.0;
    int n = 0;
    for (std::uint32_t y = 0; y < h; ++y) {
        for (std::uint32_t x = 1; x < w; ++x) {
            sum += std::abs(luminance(img[y * w + x]) - luminance(img[y * w + x - 1]));
            ++n;
        }
    }
    return n > 0 ? sum / n : 0.0;
}

double meanLum(const std::vector<glm::vec3>& img) {
    double s = 0.0;
    for (const auto& c : img) s += luminance(c);
    return img.empty() ? 0.0 : s / img.size();
}

} // namespace

// ---- the feature AOVs, which are Phase 6's first two ---------------------------------------------

TEST_CASE("the albedo AOV records material colour, not lighting", "[unit][pathtrace][aov]") {
    // The whole point of an albedo buffer is that it is FLAT where the beauty pass is shaded. If it
    // carried lighting it would be no use to a denoiser and no use to a compositor either.
    const scene::Scene sc = noisyRoom();
    const auto fb = render(sc, 16, true);
    REQUIRE(fb.albedo.size() == static_cast<std::size_t>(fb.width) * fb.height);

    const auto albedo = fb.resolvedAlbedo();
    const auto beauty = fb.resolvedRadiance();
    const auto at = [&](std::uint32_t x, std::uint32_t y) { return albedo[y * fb.width + x]; };

    // The left wall is red and the right wall is green, in the albedo, regardless of how they are lit.
    const glm::vec3 left = at(4, 48);
    const glm::vec3 right = at(fb.width - 5, 48);
    INFO("left " << left.x << "," << left.y << "," << left.z);
    INFO("right " << right.x << "," << right.y << "," << right.z);
    REQUIRE(left.x > left.y * 3.0f);
    REQUIRE(right.y > right.x * 3.0f);

    // And the albedo must be FLATTER than the beauty pass: same geometry, no shading, no noise.
    const double rAlbedo = roughness(albedo, fb.width, fb.height);
    const double rBeauty = roughness(beauty, fb.width, fb.height);
    INFO("albedo roughness " << rAlbedo << " beauty roughness " << rBeauty);
    REQUIRE(rBeauty > 0.0);           // live arm
    REQUIRE(rAlbedo < rBeauty * 0.5);
}

TEST_CASE("the normal AOV is unit length and points the way the surfaces face",
          "[unit][pathtrace][aov]") {
    const scene::Scene sc = noisyRoom();
    const auto fb = render(sc, 8, true);
    const auto normals = fb.resolvedNormal();
    const auto at = [&](std::uint32_t x, std::uint32_t y) { return normals[y * fb.width + x]; };

    int unit = 0;
    for (const auto& n : normals) {
        const float len = glm::length(n);
        if (len > 1e-6f) {
            REQUIRE(len == Approx(1.0f).margin(1e-4));
            ++unit;
        }
    }
    REQUIRE(unit > static_cast<int>(normals.size()) / 2); // live arm

    // The floor faces up, the left wall faces right (+X), the right wall faces left (-X).
    REQUIRE(at(64, fb.height - 4).y > 0.9f);
    REQUIRE(at(3, 48).x > 0.7f);
    REQUIRE(at(fb.width - 4, 48).x < -0.7f);
}

TEST_CASE("feature capture is opt-in and does not change the beauty pass",
          "[unit][pathtrace][aov]") {
    // Capturing AOVs must not perturb the image. Same seed, same sampler consumption, same pixels --
    // if the feature path consumed a random number the beauty path did not, this diverges.
    const scene::Scene sc = noisyRoom();
    const auto without = render(sc, 8, false);
    const auto with = render(sc, 8, true);
    REQUIRE(without.albedo.empty());
    REQUIRE_FALSE(with.albedo.empty());
    REQUIRE(without.radiance == with.radiance); // bit-identical
}

// ---- the denoiser --------------------------------------------------------------------------------

TEST_CASE("the denoiser reports whether it exists, and says so either way",
          "[unit][pathtrace][denoise]") {
    // Spec section 55: the capability must be reportable before a render, not discovered during one.
    const std::string version = pathtrace::denoiseVersion();
    REQUIRE_FALSE(version.empty());
    if (pathtrace::denoiseAvailable()) {
        REQUIRE(version.find("Open Image Denoise") != std::string::npos);
    } else {
        REQUIRE(version == "unavailable");
    }
}

TEST_CASE("a build without a denoiser refuses rather than passing the image through",
          "[unit][pathtrace][denoise]") {
    if (pathtrace::denoiseAvailable()) {
        SUCCEED("this build has OIDN; the refusal path is covered by the other configuration");
        return;
    }
    // Section 54. A denoise that silently returned its input is indistinguishable from one that ran
    // and achieved nothing, which is a far worse thing to debug than a refusal.
    std::vector<glm::vec3> color(16, glm::vec3(0.5f));
    std::vector<glm::vec3> out;
    pathtrace::DenoiseInput in;
    in.width = 4;
    in.height = 4;
    in.color = &color;
    const auto r = pathtrace::denoise(in, out);
    REQUIRE_FALSE(r.has_value());
    REQUIRE(r.error().message.find("AVGEN_PATHTRACE_DENOISE") != std::string::npos);
    REQUIRE(out.empty());
}

TEST_CASE("the denoiser rejects mismatched buffers", "[unit][pathtrace][denoise]") {
    std::vector<glm::vec3> color(16, glm::vec3(0.5f));
    std::vector<glm::vec3> out;

    pathtrace::DenoiseInput noColor;
    noColor.width = 4;
    noColor.height = 4;
    REQUIRE_FALSE(pathtrace::denoise(noColor, out).has_value());

    pathtrace::DenoiseInput empty;
    empty.color = &color;
    REQUIRE_FALSE(pathtrace::denoise(empty, out).has_value());

    if (!pathtrace::denoiseAvailable()) return; // the rest is covered by the refusal above

    std::vector<glm::vec3> wrongSize(9, glm::vec3(0.0f));
    pathtrace::DenoiseInput mismatch;
    mismatch.width = 4;
    mismatch.height = 4;
    mismatch.color = &color;
    mismatch.albedo = &wrongSize;
    REQUIRE_FALSE(pathtrace::denoise(mismatch, out).has_value());
}

TEST_CASE("the denoiser removes noise, keeps the picture, and keeps the energy",
          "[unit][pathtrace][denoise]") {
    if (!pathtrace::denoiseAvailable()) {
        SUCCEED("no denoiser in this build; run the AVGEN_PATHTRACE_DENOISE configuration");
        return;
    }
    const scene::Scene sc = noisyRoom();
    const auto noisy = render(sc, 4, true);   // deliberately few samples: it must be grainy
    const auto clean = render(sc, 512, true); // the converged reference

    const auto noisyColor = noisy.resolvedRadiance();
    const auto albedo = noisy.resolvedAlbedo();
    const auto normal = noisy.resolvedNormal();
    std::vector<glm::vec3> out;

    pathtrace::DenoiseInput in;
    in.width = noisy.width;
    in.height = noisy.height;
    in.color = &noisyColor;
    in.albedo = &albedo;
    in.normal = &normal;
    REQUIRE(pathtrace::denoise(in, out).has_value());
    REQUIRE(out.size() == noisyColor.size());

    const double rNoisy = roughness(noisyColor, noisy.width, noisy.height);
    const double rOut = roughness(out, noisy.width, noisy.height);
    INFO("roughness " << rNoisy << " -> " << rOut);
    REQUIRE(rNoisy > 0.0);              // live arm: the input really is noisy
    REQUIRE(rOut < rNoisy * 0.5);       // and the denoiser really smooths it

    // ENERGY: the denoiser must not darken or brighten the image. A filter that simply scaled
    // everything down would also reduce roughness, and this is the arm that separates the two.
    REQUIRE(meanLum(out) == Approx(meanLum(noisyColor)).epsilon(0.10));

    // PICTURE: the denoised 4-sample image must be CLOSER to the converged reference than the noisy
    // one was. This is the claim that matters and the one a roughness test alone cannot make --
    // a uniform grey image has zero roughness and is not a denoise.
    const auto cleanColor = clean.resolvedRadiance();
    const auto rmse = [&](const std::vector<glm::vec3>& a) {
        double s = 0.0;
        for (std::size_t i = 0; i < a.size(); ++i) {
            const double d = luminance(a[i]) - luminance(cleanColor[i]);
            s += d * d;
        }
        return std::sqrt(s / a.size());
    };
    const double eNoisy = rmse(noisyColor);
    const double eOut = rmse(out);
    INFO("RMSE vs converged: noisy " << eNoisy << " denoised " << eOut);
    REQUIRE(eOut < eNoisy * 0.7);

    // And the coloured walls must still be coloured -- the denoiser must not wash the scene grey.
    const auto at = [&](std::uint32_t x, std::uint32_t y) { return out[y * noisy.width + x]; };
    REQUIRE(at(4, 48).x > at(4, 48).y * 1.5f);
    REQUIRE(at(noisy.width - 5, 48).y > at(noisy.width - 5, 48).x * 1.5f);
}

TEST_CASE("preview: denoise before and after", "[.pathtrace-denoise-preview]") {
    if (!pathtrace::denoiseAvailable()) {
        WARN("no denoiser in this build");
        return;
    }
    const scene::Scene sc = noisyRoom();
    pathtrace::TraceSettings t;
    t.width = 400;
    t.height = 300;
    t.samplesPerPixel = 8;
    t.maxDepth = 3;
    t.russianRouletteDepth = 2;
    t.captureFeatures = true;
    pathtrace::PathTracer tr;
    pathtrace::Framebuffer fb;
    REQUIRE(tr.render(pathtrace::buildSnapshot(sc), t, fb).has_value());

    const auto color = fb.resolvedRadiance();
    const auto albedo = fb.resolvedAlbedo();
    const auto normal = fb.resolvedNormal();
    std::vector<glm::vec3> out;
    pathtrace::DenoiseInput in;
    in.width = fb.width;
    in.height = fb.height;
    in.color = &color;
    in.albedo = &albedo;
    in.normal = &normal;
    REQUIRE(pathtrace::denoise(in, out).has_value());

    const auto write = [&](const std::vector<glm::vec3>& img, const char* name) {
        const auto path = std::filesystem::temp_directory_path() / name;
        FILE* f = std::fopen(path.c_str(), "wb");
        REQUIRE(f != nullptr);
        std::fprintf(f, "P6\n%u %u\n255\n", fb.width, fb.height);
        for (const auto& c : img) {
            for (int k = 0; k < 3; ++k) {
                const float v = std::pow(std::clamp(c[k], 0.0f, 1.0f), 1.0f / 2.2f);
                const auto b = static_cast<unsigned char>(v * 255.0f + 0.5f);
                std::fwrite(&b, 1, 1, f);
            }
        }
        std::fclose(f);
    };
    write(color, "avgen_dn_before.ppm");
    write(out, "avgen_dn_after.ppm");
    write(albedo, "avgen_dn_albedo.ppm");
    WARN("wrote before/after/albedo; mean " << meanLum(color) << " -> " << meanLum(out));
}
