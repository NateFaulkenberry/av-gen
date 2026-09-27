// ADR-939: the aurora's "Audio response" is the master of everything the aurora does with the music,
// on the CPU side -- the packed record, the rows the Effects card draws and what they are called.
//
// The pixel half (the sky with music at "Audio response" 0 is the sky with none) is
// tests/rendering/test_aurora_audio_response_gpu.cpp. This file holds what a pixel cannot say: that
// the whole packed record at 0 is the silent record, lane for lane; that at 1 every bin passes through
// bit for bit (every aurora that never turned the master is unchanged); and that every audio-driven
// term has a control on the card, under a name for what it does, where an artist looks.

#include "ui/effects_panel_logic.hpp"
#include "world/atmospherics.hpp"
#include "world/effects/effect_instance.hpp"
#include "world/effects/effect_params.hpp"
#include "world/effects/effect_registry.hpp"
#include "params/modulation.hpp"
#include "params/parameter_set.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cstring>
#include <span>
#include <string>
#include <string_view>
#include <vector>

using namespace avgen;
using Catch::Matchers::WithinAbs;

namespace {

constexpr std::array<float, world::kAuroraBands> kShaped{0.05f, 0.95f, 0.05f, 0.95f, 0.05f, 0.95f, 0.05f, 0.95f,
                                                        0.05f, 0.95f, 0.30f, 0.70f, 0.20f, 0.80f, 0.10f, 0.90f};

world::AuroraGpu packed(float sensitivity, std::span<const float> spectrum, float glints = 1.0f) {
    world::EffectInstance e = world::glowmereAurora("sky");
    e.timing.fadeIn = 0.0;
    e.aurora.audio.sensitivity = sensitivity;
    e.aurora.audio.glints = glints;
    std::array<world::ResolvedAtmospheric, world::kMaxGpuComets> comets{};
    std::array<world::ResolvedAtmospheric, world::kMaxGpuAuroras> auroras{};
    const std::array<world::EffectInstance, 1> set{e};
    world::EffectContext ctx;
    ctx.seconds = 4.0;
    ctx.spectrum = spectrum;
    REQUIRE(world::resolveAtmosphericEffects(set, ctx, comets, auroras).auroras == 1);
    return world::packAurora(auroras[0], spectrum);
}

std::array<float, world::kAuroraBands> bins(const world::AuroraGpu& g) {
    return {g.band0.x, g.band0.y, g.band0.z, g.band0.w, g.band1.x, g.band1.y, g.band1.z, g.band1.w,
            g.band2.x, g.band2.y, g.band2.z, g.band2.w, g.band3.x, g.band3.y, g.band3.z, g.band3.w};
}

// The section heading a row of the Advanced list sits under, as the card draws it: a row with a
// `section` opens one (`ImGui::SeparatorText` in EffectsSection::drawField) and the rows after it
// stay under it until the next.
std::string advancedSectionOf(const ui::CardRows& rows, std::string_view leaf) {
    std::string section;
    for (const world::EffectField* f : rows.advanced) {
        if (f->section != nullptr && f->section[0] != '\0') {
            section = f->section;
        }
        if (leaf == f->leaf) {
            return section;
        }
    }
    return "<not on the Advanced list>";
}

const world::EffectField* row(const world::EffectSchema& s, std::string_view leaf) {
    for (const world::EffectField& f : s.fields) {
        if (leaf == f.leaf) {
            return &f;
        }
    }
    return nullptr;
}

} // namespace

TEST_CASE("the aurora's audio response at 0 packs exactly the aurora silence packs",
          "[world][atmospherics][aurora][adr939]") {
    // Every lane: the band depths, the beat, the glints' depth and the sixteen bins.
    const world::AuroraGpu music = packed(0.0f, kShaped);
    const world::AuroraGpu silence = packed(0.0f, {});
    CHECK(std::memcmp(&music, &silence, sizeof(world::AuroraGpu)) == 0);
    for (const float b : bins(music)) {
        CHECK(b == 0.5f); // the neutral value silence fills in
    }
    CHECK(music.audio3.x == 0.0f);
    CHECK(music.audio.w == 0.0f);

    // The control: at 1 the spectrum reaches the record, so the identity is not a record that could
    // never have held it.
    const world::AuroraGpu on = packed(1.0f, kShaped);
    const world::AuroraGpu onSilent = packed(1.0f, {});
    CHECK(std::memcmp(&on, &onSilent, sizeof(world::AuroraGpu)) != 0);
}

TEST_CASE("at audio response 1 the aurora packs exactly as it did before ADR-939",
          "[world][atmospherics][aurora][adr939]") {
    // Every tracked scene's aurora runs at 1, and so does every aurora a factory makes. `std::lerp`
    // is exact at its ends, so the bins are the clamped spectrum bit for bit -- ==, not "nearly".
    const world::AuroraGpu g = packed(1.0f, kShaped);
    const auto b = bins(g);
    for (std::size_t i = 0; i < b.size(); ++i) {
        INFO("bin " << i);
        CHECK(b[i] == kShaped[i]);
    }
    // The glints' depth is the shader's old constant's share: 1 x the sensitivity.
    CHECK(g.audio3.x == 1.0f);
    CHECK(g.audio3.y == 0.0f);
    CHECK(g.audio3.z == 0.0f);
    CHECK(g.audio3.w == 0.0f);
    // And a default aurora has never been told otherwise.
    CHECK(world::glowmereAurora("x").aurora.audio.glints == 1.0f);
    CHECK(world::AuroraAudio{}.glints == 1.0f);
}

TEST_CASE("audio response scales every bin's distance from silence, and the glints' depth",
          "[world][atmospherics][aurora][adr939]") {
    const auto half = bins(packed(0.5f, kShaped));
    const auto twice = bins(packed(2.0f, kShaped));
    for (std::size_t i = 0; i < half.size(); ++i) {
        INFO("bin " << i);
        CHECK_THAT(half[i], WithinAbs(0.5f + 0.5f * (kShaped[i] - 0.5f), 1e-6f));
        CHECK_THAT(twice[i], WithinAbs(std::clamp(0.5f + 2.0f * (kShaped[i] - 0.5f), 0.0f, 1.0f), 1e-6f));
    }
    CHECK_THAT(packed(0.5f, kShaped, 1.0f).audio3.x, WithinAbs(0.5f, 1e-6f));
    CHECK_THAT(packed(2.0f, kShaped, 0.75f).audio3.x, WithinAbs(1.5f, 1e-6f));
    CHECK(packed(1.0f, kShaped, 0.0f).audio3.x == 0.0f);
    // The five band depths were already under it, and still are.
    const world::AuroraGpu h = packed(0.5f, kShaped);
    const world::AuroraAudio defaults = world::glowmereAurora("x").aurora.audio;
    CHECK_THAT(h.audio.x, WithinAbs(defaults.bass * 0.5f, 1e-6f));
    CHECK_THAT(h.audio2.x, WithinAbs(defaults.beat * 0.5f, 1e-6f));
    // A negative glints depth is refused by the file, like every other audio depth.
    world::AuroraAudio bad;
    bad.glints = -0.1f;
    CHECK_FALSE(bad.validate().has_value());
}

TEST_CASE("UI reach: every audio-driven part of the aurora has a control on its card, named for what it does",
          "[world][atmospherics][aurora][ui][adr939]") {
    const world::EffectSchema* schema = world::effectSchema(world::EffectKind::Aurora);
    REQUIRE(schema != nullptr);
    world::EffectInstance e = world::glowmereAurora("sky");
    const ui::CardRows rows = ui::effectCardRows(*schema, e);

    // The master and the spectrum's shape: on the card's first page, each with a tooltip that says
    // what it covers -- the master says it stops everything and that routes are not in it.
    for (const char* leaf : {"audioSensitivity", "spectrumShape"}) {
        INFO(leaf);
        const world::EffectField* f = row(*schema, leaf);
        REQUIRE(f != nullptr);
        CHECK(std::find(rows.main.begin(), rows.main.end(), f) != rows.main.end());
        REQUIRE(f->tip != nullptr);
        CHECK(std::string_view(f->tip).size() > 40);
    }
    CHECK(std::string_view(row(*schema, "audioSensitivity")->label) == "Audio response");
    const std::string_view masterTip = row(*schema, "audioSensitivity")->tip;
    CHECK(masterTip.find("no music") != std::string_view::npos);
    CHECK(masterTip.find("Routes") != std::string_view::npos);
    CHECK(masterTip.find("glints") != std::string_view::npos);

    // Each band's depth, the glints' among them, under Advanced > "Audio response".
    struct Depth {
        const char* leaf;
        const char* label;
    };
    for (const Depth& d : {Depth{"audioBass", "Bass -> height"}, Depth{"audioLowMid", "Low-mid -> waves"},
                           Depth{"audioMid", "Mid -> folds"}, Depth{"audioHigh", "High -> filaments"},
                           Depth{"audioGlints", "High -> glints"}, Depth{"audioBeat", "Beat -> pulse"}}) {
        INFO(d.leaf);
        const world::EffectField* f = row(*schema, d.leaf);
        REQUIRE(f != nullptr);
        CHECK(std::string_view(f->label) == d.label);
        CHECK(advancedSectionOf(rows, d.leaf) == "Audio response");
    }
    // The glints themselves, under the viewer's word, in Appearance.
    const world::EffectField* glints = row(*schema, "sparkle");
    REQUIRE(glints != nullptr);
    CHECK(std::string_view(glints->label) == "Glints");
    CHECK(advancedSectionOf(rows, "sparkle") == "Appearance");

    // Each is a registered parameter a slider, a key or a route reaches...
    params::ParameterSet params;
    const std::array<world::EffectInstance, 1> set{e};
    world::EffectParameters registered = world::registerEffectParameters(params, set);
    const std::string prefix = world::effectParameterPrefix(e.id);
    for (const char* leaf : {"audioSensitivity", "spectrumShape", "audioGlints", "sparkle"}) {
        INFO(leaf);
        params::IParameter* p = params.find(prefix + leaf);
        REQUIRE(p != nullptr);
        CHECK(p->flags().exposed);
        CHECK(p->flags().serialized);
    }
    CHECK(params.find(prefix + "audioGlints")->baseComponent(0) == 1.0f);
    world::unregisterEffectParameters(params, registered);

    // ...and the new depth survives the file under `audio/glints`, while a file that never wrote it
    // reads back the depth that keeps its aurora as it was.
    e.aurora.audio.glints = 0.25f;
    const nlohmann::json j = e.toJson();
    auto back = world::EffectInstance::fromJson(j);
    REQUIRE(back.has_value());
    CHECK(back->aurora.audio.glints == 0.25f);
    REQUIRE(j.at("parameters").at("audio").contains("glints"));
    CHECK(j["parameters"]["audio"]["glints"].get<float>() == 0.25f);
    nlohmann::json old = j;
    old["parameters"]["audio"].erase("glints");
    auto legacy = world::EffectInstance::fromJson(old);
    REQUIRE(legacy.has_value());
    CHECK(legacy->aurora.audio.glints == 1.0f);
}

TEST_CASE("UI reach: an effect's card names the routes that move it, which its audio response does not gate",
          "[world][atmospherics][aurora][ui][adr939]") {
    // "+ Add aurora" installs six audio routes onto the new aurora's parameters (ADR-230's defaults).
    // They are the project's, so "Audio response" at 0 leaves them moving it -- which is right (GV3
    // answers the lead slowly through a route with audio response 0) and was invisible from the aurora:
    // nothing on its card said anything but "Beat response" was driven. The card now lists them, each
    // as its source and the row it moves.
    const world::EffectSchema* schema = world::effectSchema(world::EffectKind::Aurora);
    REQUIRE(schema != nullptr);
    const world::EffectInstance e = world::glowmereAurora("sky");
    std::vector<params::ModRoute> routes = world::defaultEffectRoutes(e.id, e.kind);
    REQUIRE(routes.size() == 6);
    params::ModRoute other; // a route onto another effect whose id begins the same way
    other.source = "audio.rms";
    other.target = "fx/skyline/intensity";
    routes.push_back(other);
    params::ModRoute planned;
    planned.source = "lead.aurora";
    planned.target = world::effectParameterPrefix(e.id) + "intensity";
    planned.planItem = "gv3-look/lead.aurora";
    planned.enabled = false;
    routes.push_back(planned);

    const std::vector<ui::EffectRouteLine> lines = ui::effectRoutesOn(routes, e, *schema);
    std::vector<std::string> texts;
    for (const ui::EffectRouteLine& l : lines) {
        texts.push_back(l.text);
    }
    const std::vector<std::string> expected{
        "audio.bass -> Height",        "audio.rms -> Brightness", "audio.lowMid -> Wave amount",
        "audio.mid -> Turbulence",     "audio.treble -> Filaments", "beat.pulse -> Edge brightness",
        "lead.aurora -> Brightness"};
    CHECK(texts == expected);
    REQUIRE(lines.size() == 7);
    CHECK(lines.back().planned);
    CHECK_FALSE(lines.back().enabled);
    CHECK(lines.front().enabled);
    // The control: an effect nothing routes to lists nothing.
    CHECK(ui::effectRoutesOn(std::span<const params::ModRoute>(routes).subspan(6, 1), e, *schema).empty());
}
