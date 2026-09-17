// Lab cases: the reproduction format (ADR-261, spec §32).
//
// The format's only promise is that a number resolves to a configuration, so the tests that matter
// are the refusals. A format that accepts a case with no question, two cases with the same number,
// or a case filed under the wrong lab has not kept the promise; it has only stored something.

#include "labs/case.hpp"
#include "labs/lab.hpp"

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;
using namespace avgen;
using nlohmann::json;

namespace {

labs::LabCase goodCase() {
    labs::LabCase c;
    c.lab = "rendering";
    c.number = 37;
    c.title = "a case";
    c.question = "what is being measured";
    c.expectation = "it moves";
    c.fixture = "examples/quality/aliasing.json";
    c.timeSeconds = 1.25;
    c.width = 1280;
    c.height = 720;
    c.fps = 30.0;
    c.seed = 99;
    return c;
}

} // namespace

TEST_CASE("a lab case round-trips through JSON", "[labs][case]") {
    labs::LabCase c = goodCase();
    c.hasCamera = true;
    c.eye = glm::vec3(0.0f, 2.5f, 9.0f);
    c.aim = glm::vec3(0.0f, 1.0f, 0.0f);
    c.tier = "offline";
    c.disable = {"fxaa", "ao"};
    c.qualityArms = {"pcss"};
    c.aovs = {"velocity", "depth"};
    c.supersample = 2.0;
    c.notes = "why this exists";

    const auto back = labs::caseFromJson(labs::toJson(c));
    REQUIRE(back.has_value());
    CHECK(back->lab == c.lab);
    CHECK(back->number == c.number);
    CHECK(back->question == c.question);
    CHECK(back->expectation == c.expectation);
    CHECK(back->fixture == c.fixture);
    CHECK(back->timeSeconds == c.timeSeconds);
    CHECK(back->width == c.width);
    CHECK(back->height == c.height);
    CHECK(back->fps == c.fps);
    CHECK(back->seed == c.seed);
    CHECK(back->hasCamera);
    CHECK(back->eye == c.eye);
    CHECK(back->aim == c.aim);
    CHECK(back->tier == c.tier);
    CHECK(back->disable == c.disable);
    CHECK(back->qualityArms == c.qualityArms);
    CHECK(back->aovs == c.aovs);
    CHECK(back->supersample == c.supersample);
    CHECK(back->notes == c.notes);
}

TEST_CASE("a case with no camera stays without one", "[labs][case]") {
    // The default is "use the fixture's camera". A round-trip that invented an origin-to-origin
    // camera would silently repoint every case that did not ask for one.
    const auto back = labs::caseFromJson(labs::toJson(goodCase()));
    REQUIRE(back.has_value());
    CHECK_FALSE(back->hasCamera);
    CHECK(back->supersample == 1.0);
}

TEST_CASE("a case that cannot say what it tests is refused", "[labs][case]") {
    {
        labs::LabCase c = goodCase();
        c.question.clear();
        CHECK_FALSE(labs::caseFromJson(labs::toJson(c)).has_value());
    }
    {
        labs::LabCase c = goodCase();
        c.expectation.clear();
        CHECK_FALSE(labs::caseFromJson(labs::toJson(c)).has_value());
    }
    {
        labs::LabCase c = goodCase();
        c.fixture.clear();
        CHECK_FALSE(labs::caseFromJson(labs::toJson(c)).has_value());
    }
    {
        labs::LabCase c = goodCase();
        c.number = 0;
        CHECK_FALSE(labs::caseFromJson(labs::toJson(c)).has_value());
    }
    {
        labs::LabCase c = goodCase();
        c.lab = "shadows"; // the lab is "shadow"
        CHECK_FALSE(labs::caseFromJson(labs::toJson(c)).has_value());
    }
    {
        json j = labs::toJson(goodCase());
        j["size"] = json::array({0, 720});
        CHECK_FALSE(labs::caseFromJson(j).has_value());
    }
    // The control: the case these were derived from is accepted, so the refusals above are about
    // the field each one broke and not about the case being malformed to begin with.
    CHECK(labs::caseFromJson(labs::toJson(goodCase())).has_value());
}

TEST_CASE("a case file refuses duplicate numbers and foreign labs", "[labs][case]") {
    const fs::path dir = fs::temp_directory_path() / "avgen-lab-cases-test";
    fs::create_directories(dir);
    const fs::path file = dir / "cases.json";

    labs::LabCase a = goodCase();
    labs::LabCase b = goodCase();
    b.title = "another";
    REQUIRE(labs::saveCases(file, "rendering", {a, b}).has_value());
    // Both are numbered 37. "Reproduce case 37" has two answers, which is no answer.
    CHECK_FALSE(labs::loadCases(file).has_value());

    b.number = 38;
    REQUIRE(labs::saveCases(file, "rendering", {a, b}).has_value());
    const auto loaded = labs::loadCases(file);
    REQUIRE(loaded.has_value());
    CHECK(loaded->size() == 2);

    // A case filed under the wrong lab: the file says rendering, the case says visibility.
    b.lab = "visibility";
    {
        json doc;
        doc["format"] = "avgen-lab-cases";
        doc["version"] = 1;
        doc["lab"] = "rendering";
        doc["cases"] = json::array({labs::toJson(a), labs::toJson(b)});
        std::ofstream out(file);
        out << doc.dump(1, ' ');
    }
    CHECK_FALSE(labs::loadCases(file).has_value());

    fs::remove_all(dir);
}

TEST_CASE("finding a case names the numbers that exist", "[labs][case]") {
    labs::LabCase a = goodCase();
    a.number = 1;
    labs::LabCase b = goodCase();
    b.number = 6;
    const std::vector<labs::LabCase> cases{a, b};
    REQUIRE(labs::findCase(cases, 6).has_value());
    const auto missing = labs::findCase(cases, 37);
    REQUIRE_FALSE(missing.has_value());
    // "no case 37" is half an answer when the file holds 1 and 6.
    CHECK(missing.error().message.find("1, 6") != std::string::npos);
}

TEST_CASE("the reproduce command carries every arm the case set", "[labs][case]") {
    labs::LabCase c = goodCase();
    c.tier = "offline";
    c.disable = {"fxaa", "ao"};
    c.qualityArms = {"pcss"};
    c.aovs = {"velocity", "depth"};
    c.supersample = 2.0;
    const std::string cmd = labs::reproduceCommand(c);
    INFO(cmd);
    CHECK(cmd.find("examples/quality/aliasing.json") != std::string::npos);
    CHECK(cmd.find("--size 1280x720") != std::string::npos);
    CHECK(cmd.find("--tier offline") != std::string::npos);
    CHECK(cmd.find("--disable fxaa,ao") != std::string::npos);
    CHECK(cmd.find("--quality-arm pcss") != std::string::npos);
    CHECK(cmd.find("--aov velocity,depth") != std::string::npos);
    CHECK(cmd.find("--supersample 2") != std::string::npos);
    CHECK(cmd.find("--range 1.25:1.25") != std::string::npos);

    // The control: a case with no arms prints no arm flags. A command line that always carries
    // `--disable` would reproduce something the case never asked for.
    const std::string bare = labs::reproduceCommand(goodCase());
    INFO(bare);
    CHECK(bare.find("--disable") == std::string::npos);
    CHECK(bare.find("--quality-arm") == std::string::npos);
    CHECK(bare.find("--aov") == std::string::npos);
    CHECK(bare.find("--supersample") == std::string::npos);
    CHECK(bare.find("--tier") == std::string::npos);
}

TEST_CASE("the Rendering Lab's shipped cases load and point at real fixtures", "[labs][case]") {
    const fs::path root(AVGEN_SOURCE_DIR);
    const fs::path file = root / labs::caseFilePath("rendering");
    REQUIRE(fs::is_regular_file(file));
    const auto cases = labs::loadCases(file);
    INFO((cases ? std::string() : cases.error().message));
    REQUIRE(cases.has_value());
    CHECK(cases->size() >= 4);
    for (const labs::LabCase& c : *cases) {
        INFO("case " << c.number << ": " << c.title);
        CHECK(fs::is_regular_file(root / c.fixture));
    }
    // Case 1 and case 2 are a pair: the same fixture at the same second, differing only in the
    // supersample factor. If they ever stop being that, the report comparing them is comparing two
    // things rather than one change.
    const auto candidate = labs::findCase(*cases, 1);
    const auto reference = labs::findCase(*cases, 2);
    REQUIRE(candidate.has_value());
    REQUIRE(reference.has_value());
    CHECK(candidate->fixture == reference->fixture);
    CHECK(candidate->timeSeconds == reference->timeSeconds);
    CHECK(candidate->width == reference->width);
    CHECK(candidate->height == reference->height);
    CHECK(candidate->supersample == 1.0);
    CHECK(reference->supersample == 2.0);
}
