// Phase C §44: motion style. "Motion matching should preserve stylistic identity ... Style metadata
// may eventually influence query cost." Here it does: a character has a style, a clip outside it
// pays `styleWeight` (a fraction of the database's cost spread), and the choice among motions that
// do what was asked leans to the character's own.
//
// Each case is paired with the arm that shows the term is what did it: style off, weight 0, and a
// style no clip carries all leave the choice exactly where it was.

#include "entity/entity.hpp"
#include "entity/match_motion_provider.hpp"
#include "scene/motion_database.hpp"
#include "scene/motion_match_explain.hpp"
#include "scene/motion_pack.hpp"
#include "support/golden_motion.hpp"

#include <catch2/catch_test_macros.hpp>

#include <fmt/format.h>

#include <filesystem>
#include <map>

using namespace avgen;

namespace {

// The golden corpus plus a second walk, a little faster (1.35 m/s against 1.2): the "strut".
// Without a style the plain walk is the better fit for a 1.2 m/s request; the strut is one
// plausible alternative, not a different motion. (A slower strut was tried first and won without
// any style: from the database's mean pose, a shorter stride is nearer, and continuity then holds
// it. The choice is path-dependent, so the variant has to be worse from every direction.)
scene::MotionPack styledPack() {
    scene::MotionPack pack = testsupport::goldenPack();
    testsupport::addGoldenClip(pack, {"Walk_strut",
                                      [](float t) { return glm::vec3(0.0f, 0.0f, 1.35f * t); },
                                      testsupport::noTurn});
    return pack;
}

struct Run {
    std::map<std::string, int> played; // second half of the run
    std::vector<std::uint32_t> selections;
};

Run drive(const scene::MotionDatabase& db, const std::vector<scene::AnimationClip>& clips, const std::string& style,
          float styleWeight, float speed) {
    entity::MatchMotionProvider matcher(&db, &clips, "styled");
    entity::MatchSettings settings;
    settings.styleWeight = styleWeight;
    matcher.setSettings(settings);
    matcher.setStyle(style, {{"strut", {"Walk_strut"}}, {"limp", {"Limp"}}});
    entity::MotionMemory memory;
    Run out;
    constexpr int kFrames = 120;
    for (int f = 0; f < kFrames; ++f) {
        entity::MotionRequest request;
        request.desiredVelocity = glm::vec3(0.0f, 0.0f, speed);
        request.bodyVelocity = request.desiredVelocity;
        request.bodyVelocityKnown = true;
        entity::MotionMemory next;
        const entity::MotionResult r = matcher.advance(request, memory, static_cast<double>(f) / 60.0, 1.0f / 60.0f, next);
        REQUIRE(r.ok());
        memory = next;
        out.selections.push_back(memory.selection);
        if (f >= kFrames / 2) {
            ++out.played[db.clipNames[db.sampleClip[memory.selection]]];
        }
    }
    return out;
}

int count(const Run& r, const std::string& clip) {
    const auto it = r.played.find(clip);
    return it == r.played.end() ? 0 : it->second;
}

} // namespace

TEST_CASE("§44 a character's style decides between motions that both do what was asked", "[style][motionmatching][phaseC]") {
    const scene::MotionPack pack = styledPack();
    auto db = scene::buildMotionDatabase(pack, testsupport::goldenOptions());
    REQUIRE(db.has_value());

    const Run plain = drive(*db, pack.animation, "", 0.05f, testsupport::kGoldenWalk);
    const Run strut = drive(*db, pack.animation, "strut", 0.05f, testsupport::kGoldenWalk);
    const Run weightless = drive(*db, pack.animation, "strut", 0.0f, testsupport::kGoldenWalk);
    const Run nobody = drive(*db, pack.animation, "limp", 0.05f, testsupport::kGoldenWalk);
    const auto list = [](const Run& r) {
        std::string s;
        for (const auto& [clip, n] : r.played) {
            s += fmt::format("{} {} ", clip, n);
        }
        return s;
    };
    WARN("plain: " + list(plain) + "\nstrut: " + list(strut) + "\nweight 0: " + list(weightless));

    // Without a style, the better fit: the plain walk.
    CHECK(count(plain, "Walk") == 60);
    // With the style, the strut, although it fits a little worse.
    CHECK(count(strut, "Walk_strut") == 60);
    // Control: the same style at weight 0 is the plain run exactly.
    CHECK(weightless.selections == plain.selections);
    // A style no clip carries costs every clip the same, which is no cost at all.
    CHECK(nobody.selections == plain.selections);
}

TEST_CASE("§44 style is a preference, not a filter: a request the style cannot serve still gets the right motion",
          "[style][motionmatching][phaseC]") {
    const scene::MotionPack pack = styledPack();
    auto db = scene::buildMotionDatabase(pack, testsupport::goldenOptions());
    REQUIRE(db.has_value());
    // Standing still and running: there is no strutting idle or strutting run, and a strutting walk
    // would be the wrong motion.
    const Run idle = drive(*db, pack.animation, "strut", 0.05f, 0.0f);
    const Run run = drive(*db, pack.animation, "strut", 0.05f, testsupport::kGoldenRun);
    WARN(fmt::format("idle: Idle {} Walk_strut {}; run: Run {} Walk_strut {}", count(idle, "Idle"),
                     count(idle, "Walk_strut"), count(run, "Run"), count(run, "Walk_strut")));
    CHECK(count(idle, "Walk_strut") == 0);
    CHECK(count(run, "Run") == 60);
    // And an unbounded weight does turn it into a filter, which is why the weight is bounded by the
    // spread rather than left to swamp the cost: the arm that shows the default is not that.
    const Run filtered = drive(*db, pack.animation, "strut", 1000.0f, testsupport::kGoldenRun);
    CHECK(count(filtered, "Walk_strut") == 60);
}

TEST_CASE("§44 the style term is in the breakdown, and the explainer prices it to the bit", "[style][motionmatching][phaseC]") {
    const scene::MotionPack pack = styledPack();
    auto db = scene::buildMotionDatabase(pack, testsupport::goldenOptions());
    REQUIRE(db.has_value());
    entity::MatchMotionProvider matcher(&db.value(), &pack.animation, "styled");
    matcher.setStyle("strut", {{"strut", {"Walk_strut"}}});
    REQUIRE(matcher.clipsInStyle() == 1u);
    entity::MotionRequest request;
    request.desiredVelocity = glm::vec3(0.0f, 0.0f, testsupport::kGoldenWalk);
    request.bodyVelocity = request.desiredVelocity;
    request.bodyVelocityKnown = true;
    const auto query = matcher.queryFor(request, entity::MotionMemory{});
    REQUIRE(query.has_value());
    REQUIRE(query->clipCost.size() == db->clipNames.size());
    const scene::MotionMatch match = scene::searchMotion(*db, *query, matcher.settings().weights);
    REQUIRE(match.found());
    CHECK(db->clipNames[db->sampleClip[match.sample]] == "Walk_strut");
    CHECK(match.breakdown.style == 0.0f);
    // An out-of-style candidate carries the penalty, in the search, the staged scorer and the
    // explainer alike.
    std::uint32_t walk = 0;
    while (db->clipNames[db->sampleClip[walk]] != "Walk") {
        ++walk;
    }
    const std::vector<std::uint32_t> one{walk};
    const scene::MotionMatch scored = scene::scoreMotionCandidates(*db, *query, matcher.settings().weights, one);
    const scene::MotionCandidateCost explained = scene::motionCandidateCost(*db, *query, matcher.settings().weights, walk);
    CHECK(explained.breakdown.style == matcher.settings().styleWeight * db->stats.costSpread);
    CHECK(explained.total == scored.cost);
}

TEST_CASE("§44 the style keys round-trip, and a style nothing defines is refused", "[style][motionmatching][phaseC]") {
    const nlohmann::json j = {{"name", "a"},
                              {"motionMatching",
                               {{"joints", {"foot.l"}},
                                {"style", "strut"},
                                {"styles", {{"strut", {"Strutting"}}, {"march", {"March", "Marching"}}}},
                                {"weights", {{"version", 1}, {"style", 0.5}}}}}};
    auto desc = entity::entityFromJson(j, {}, nullptr);
    REQUIRE(desc.has_value());
    CHECK(desc->motionMatching.style == "strut");
    REQUIRE(desc->motionMatching.styles.size() == 2u);
    CHECK(desc->motionMatching.styleWeight == 0.5f);
    auto again = entity::entityFromJson(entity::entitiesToJson({*desc}).at(0), {}, nullptr);
    REQUIRE(again.has_value());
    CHECK(again->motionMatching == desc->motionMatching);

    // A weights block without a style weight writes none back (older scenes round-trip unchanged).
    auto older = entity::entityFromJson(
        nlohmann::json{{"name", "b"}, {"motionMatching", {{"joints", {"foot.l"}}, {"weights", {{"version", 1}}}}}}, {}, nullptr);
    REQUIRE(older.has_value());
    CHECK_FALSE(entity::entitiesToJson({*older}).at(0)["motionMatching"]["weights"].contains("style"));

    CHECK_FALSE(entity::entityFromJson(
                    nlohmann::json{{"name", "c"}, {"motionMatching", {{"joints", {"foot.l"}}, {"style", "strut"}}}}, {}, nullptr)
                    .has_value());
    CHECK_FALSE(entity::entityFromJson(nlohmann::json{{"name", "d"},
                                                      {"motionMatching", {{"joints", {"foot.l"}}, {"styles", {{"strut", "S"}}}}}},
                                       {}, nullptr)
                    .has_value());
}

TEST_CASE("§44 on 100STYLE: a style is kept across twelve, and the locomotion still answers the request",
          "[style][motionmatching][100style][phaseC]") {
    const std::filesystem::path dir = std::filesystem::path(AVGEN_SOURCE_DIR) / "assets" / "100style-mixed-pack";
    if (!std::filesystem::exists(dir / "pack.json")) {
        SKIP("the 100STYLE MIXED pack is not present (see assets/100STYLE-ATTRIBUTION.md)");
    }
    auto pack = scene::readMotionPack(dir);
    REQUIRE(pack.has_value());
    scene::MotionDatabaseOptions options;
    options.config = scene::defaultBipedConfig("LeftAnkle", "RightAnkle", "Head");
    options.config.trajectoryTimes = {0.2f, 0.4f, 0.6f};
    // The request-led weights §65 found (at the engine defaults the pose half outweighs the request
    // here too: a forward request held `DuckFoot_TR1` for two seconds on its pose alone, which would
    // make the direction column below a measure of the weights, not of style).
    options.config.jointPositionWeight = 0.3f;
    options.config.trajectoryPositionWeight = 6.0f;
    options.config.rootVelocityWeight = 3.0f;
    auto db = scene::buildMotionDatabase(*pack, options);
    REQUIRE(db.has_value());

    std::vector<std::string> styles;
    std::vector<entity::MatchMotionProvider::StyleRule> rules;
    for (const scene::PackClip& c : pack->clips) {
        const std::string style = c.name.substr(0, c.name.find('_'));
        if (styles.empty() || styles.back() != style) {
            styles.push_back(style);
            rules.push_back({style, {style + "_"}});
        }
    }
    REQUIRE(styles.size() >= 10u);

    // Each style's own forward walk, as requests: what it moved like, frame by frame, at 30 Hz for
    // two seconds. The question is which style's clips answer, with the character's style and without.
    std::string report = "style               in style (off -> on)   forward (off -> on)\n";
    double inOff = 0.0;
    double inOn = 0.0;
    double fwOff = 0.0;
    double fwOn = 0.0;
    for (const std::string& style : styles) {
        std::size_t gt = pack->clips.size();
        for (std::size_t c = 0; c < pack->clips.size(); ++c) {
            if (pack->clips[c].name == style + "_FW") {
                gt = c;
            }
        }
        if (gt == pack->clips.size()) {
            continue;
        }
        const float speed = pack->clips[gt].groundSpeed;
        double in[2] = {0.0, 0.0};
        std::map<std::string, int> top[2];
        double fw[2] = {0.0, 0.0};
        for (int arm = 0; arm < 2; ++arm) {
            entity::MatchMotionProvider matcher(&db.value(), &pack->animation, "100style");
            matcher.setStyle(arm == 0 ? std::string() : style, rules);
            entity::MotionMemory memory;
            int frames = 0;
            for (int f = 0; f < 60; ++f) {
                entity::MotionRequest request;
                request.desiredVelocity = glm::vec3(0.0f, 0.0f, speed);
                request.bodyVelocity = request.desiredVelocity;
                request.bodyVelocityKnown = true;
                entity::MotionMemory next;
                if (!matcher.advance(request, memory, static_cast<double>(f) / 30.0, 1.0f / 30.0f, next).ok()) {
                    continue;
                }
                memory = next;
                const std::string& chosen = db->clipNames[db->sampleClip[memory.selection]];
                ++frames;
                ++top[arm][chosen];
                in[arm] += chosen.rfind(style + "_", 0) == 0 ? 1.0 : 0.0;
                fw[arm] += chosen.find("_FW") != std::string::npos || chosen.find("_FR") != std::string::npos ? 1.0 : 0.0;
            }
            REQUIRE(frames > 0);
            in[arm] /= frames;
            fw[arm] /= frames;
        }
        const auto most = [](const std::map<std::string, int>& m) {
            std::string best;
            int n = -1;
            for (const auto& [clip, c] : m) {
                if (c > n) {
                    n = c;
                    best = clip;
                }
            }
            return best;
        };
        report += fmt::format("{:<18}  {:5.2f} -> {:5.2f}          {:5.2f} -> {:5.2f}    mostly {} -> {}\n", style, in[0],
                              in[1], fw[0], fw[1], most(top[0]), most(top[1]));
        inOff += in[0] / static_cast<double>(styles.size());
        inOn += in[1] / static_cast<double>(styles.size());
        fwOff += fw[0] / static_cast<double>(styles.size());
        fwOn += fw[1] / static_cast<double>(styles.size());
    }
    report += fmt::format("mean                {:5.2f} -> {:5.2f}          {:5.2f} -> {:5.2f}\n", inOff, inOn, fwOff, fwOn);
    WARN(report);
    // The style is kept: most frames come from the character's own style once it has one.
    CHECK(inOn > 0.8);
    CHECK(inOn > inOff + 0.3);
    // And it still walks forward: the style did not buy its identity by doing the wrong motion.
    CHECK(fwOn >= fwOff - 0.1);
}
