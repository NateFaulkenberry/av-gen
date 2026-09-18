#include "labs/case.hpp"

#include "labs/lab.hpp"

#include <fmt/format.h>
#include <nlohmann/json.hpp>

#include <fstream>
#include <stdexcept>
#include <algorithm>

namespace avgen::labs {
namespace {

using nlohmann::json;

std::vector<std::string> readStrings(const json& j, const char* key) {
    std::vector<std::string> out;
    if (auto it = j.find(key); it != j.end() && it->is_array()) {
        for (const json& e : *it) {
            if (e.is_string()) {
                out.push_back(e.get<std::string>());
            }
        }
    }
    return out;
}

void writeStrings(json& j, const char* key, const std::vector<std::string>& values) {
    // Omitted when empty rather than written as `[]`. A case file is read by people, and an empty
    // array of arms reads as "these were considered and none applied" when the truth is that the
    // case never had any.
    if (!values.empty()) {
        j[key] = values;
    }
}

std::string joinList(const std::vector<std::string>& values) {
    std::string out;
    for (const std::string& v : values) {
        if (!out.empty()) {
            out += ',';
        }
        out += v;
    }
    return out;
}

} // namespace

std::filesystem::path caseFilePath(std::string_view labKey) {
    return std::filesystem::path("examples") / "labs" / std::string(labKey) / "cases.json";
}

json toJson(const LabCase& c) {
    json j;
    j["lab"] = c.lab;
    j["number"] = c.number;
    j["title"] = c.title;
    j["question"] = c.question;
    j["expectation"] = c.expectation;
    j["fixture"] = c.fixture;
    j["timeSeconds"] = c.timeSeconds;
    j["size"] = json::array({c.width, c.height});
    j["fps"] = c.fps;
    j["seed"] = c.seed;
    if (c.hasCamera) {
        j["camera"] = json{{"eye", json::array({c.eye.x, c.eye.y, c.eye.z})},
                           {"aim", json::array({c.aim.x, c.aim.y, c.aim.z})}};
    }
    if (c.supersample != 1.0) {
        j["supersample"] = c.supersample;
    }
    if (!c.tier.empty()) {
        j["tier"] = c.tier;
    }
    writeStrings(j, "disable", c.disable);
    writeStrings(j, "qualityArms", c.qualityArms);
    writeStrings(j, "aovs", c.aovs);
    if (!c.notes.empty()) {
        j["notes"] = c.notes;
    }
    if (!c.blockedBy.empty()) {
        j["blockedBy"] = c.blockedBy;
    }
    return j;
}

Result<LabCase> caseFromJson(const json& doc) {
    if (!doc.is_object()) {
        return fail("a lab case must be an object");
    }
    LabCase c;
    c.lab = doc.value("lab", std::string());
    if (c.lab.empty()) {
        return fail("a lab case must name its lab");
    }
    if (!findLab(c.lab)) {
        return fail("'{}' is not a lab ({})", c.lab, labKeys());
    }
    c.number = doc.value("number", 0);
    if (c.number <= 0) {
        return fail("lab case in '{}' has no positive number", c.lab);
    }
    c.title = doc.value("title", std::string());
    c.question = doc.value("question", std::string());
    c.expectation = doc.value("expectation", std::string());
    c.fixture = doc.value("fixture", std::string());
    if (c.fixture.empty()) {
        return fail("{} case {} names no fixture", c.lab, c.number);
    }
    // §5 says a lab must be able to answer "what am I testing" and "what should happen". A case
    // that cannot is not a case, and refusing it here is cheaper than discovering six months later
    // that nobody remembers what case 37 was for.
    if (c.question.empty() || c.expectation.empty()) {
        return fail("{} case {} must carry both a question and an expectation", c.lab, c.number);
    }
    c.timeSeconds = doc.value("timeSeconds", 0.0);
    if (auto it = doc.find("size"); it != doc.end() && it->is_array() && it->size() == 2) {
        c.width = (*it)[0].get<std::uint32_t>();
        c.height = (*it)[1].get<std::uint32_t>();
    }
    if (c.width == 0 || c.height == 0) {
        return fail("{} case {} has a zero viewport dimension", c.lab, c.number);
    }
    c.fps = doc.value("fps", 60.0);
    c.seed = doc.value("seed", std::uint64_t{0});
    if (auto it = doc.find("camera"); it != doc.end() && it->is_object()) {
        const auto read = [](const json& j, const char* key, glm::vec3& out) {
            if (auto a = j.find(key); a != j.end() && a->is_array() && a->size() == 3) {
                out = glm::vec3((*a)[0].get<float>(), (*a)[1].get<float>(), (*a)[2].get<float>());
                return true;
            }
            return false;
        };
        const bool eye = read(*it, "eye", c.eye);
        const bool aim = read(*it, "aim", c.aim);
        if (!eye || !aim) {
            return fail("{} case {} has a camera with no eye and aim", c.lab, c.number);
        }
        c.hasCamera = true;
    }
    c.supersample = doc.value("supersample", 1.0);
    if (c.supersample <= 0.0) {
        return fail("{} case {} has a non-positive supersample factor", c.lab, c.number);
    }
    c.tier = doc.value("tier", std::string());
    c.disable = readStrings(doc, "disable");
    c.qualityArms = readStrings(doc, "qualityArms");
    c.aovs = readStrings(doc, "aovs");
    c.notes = doc.value("notes", std::string());
    // ADR-275. A case may declare the unit it is waiting for; an empty string is the ordinary state
    // and means it can be run now.
    c.blockedBy = doc.value("blockedBy", std::string());
    return c;
}

Result<std::vector<LabCase>> loadCases(const std::filesystem::path& file) {
    std::ifstream in(file);
    if (!in) {
        return fail("cannot open lab case file '{}'", file.string());
    }
    json doc = json::parse(in, nullptr, false);
    if (doc.is_discarded()) {
        return fail("'{}' is not valid JSON", file.string());
    }
    if (doc.value("format", std::string()) != "avgen-lab-cases") {
        return fail("'{}' is not an avgen-lab-cases file", file.string());
    }
    const std::string labKey = doc.value("lab", std::string());
    if (!findLab(labKey)) {
        return fail("'{}' names lab '{}', which is not one of {}", file.string(), labKey, labKeys());
    }
    auto it = doc.find("cases");
    if (it == doc.end() || !it->is_array()) {
        return fail("'{}' has no cases array", file.string());
    }
    std::vector<LabCase> cases;
    for (const json& e : *it) {
        auto c = caseFromJson(e);
        if (!c) {
            return std::unexpected(c.error());
        }
        if (c->lab != labKey) {
            return fail("'{}' is {}'s case file but case {} names lab '{}'", file.string(), labKey,
                        c->number, c->lab);
        }
        for (const LabCase& seen : cases) {
            // Two cases numbered the same is the one defect this format cannot tolerate: §32's
            // whole promise is that a number identifies a configuration, and a duplicate turns
            // "reproduce case 37" back into a conversation.
            if (seen.number == c->number) {
                return fail("'{}' has two cases numbered {}", file.string(), c->number);
            }
        }
        cases.push_back(*c);
    }
    return cases;
}

Result<void> saveCases(const std::filesystem::path& file, std::string_view labKey,
                       const std::vector<LabCase>& cases) {
    if (!findLab(labKey)) {
        return fail("'{}' is not a lab ({})", labKey, labKeys());
    }
    json doc;
    doc["format"] = "avgen-lab-cases";
    doc["version"] = 1;
    doc["lab"] = std::string(labKey);
    json array = json::array();
    for (const LabCase& c : cases) {
        array.push_back(toJson(c));
    }
    doc["cases"] = std::move(array);
    std::error_code ec;
    std::filesystem::create_directories(file.parent_path(), ec);
    std::ofstream out(file);
    if (!out) {
        return fail("cannot write '{}'", file.string());
    }
    out << doc.dump(1, ' ') << '\n';
    return {};
}

Result<LabCase> findCase(const std::vector<LabCase>& cases, int number) {
    for (const LabCase& c : cases) {
        if (c.number == number) {
            return c;
        }
    }
    std::vector<int> numbers;
    numbers.reserve(cases.size());
    for (const LabCase& c : cases) {
        numbers.push_back(c.number);
    }
    std::sort(numbers.begin(), numbers.end());
    std::string have;
    for (int n : numbers) {
        have += (have.empty() ? "" : ", ") + std::to_string(n);
    }
    return fail("no case {} (this file has {})", number, have.empty() ? "none" : have);
}

std::filesystem::path repositoryRoot() {
    std::error_code ec;
    const std::filesystem::path here = std::filesystem::current_path(ec);
    if (!ec && std::filesystem::is_directory(here / "examples" / "labs", ec)) {
        return here;
    }
    return std::filesystem::path(AVGEN_SOURCE_DIR);
}

Result<LabCase> resolveCaseSpec(std::string_view spec, const std::filesystem::path& root) {
    const std::size_t colon = spec.find(':');
    if (colon == std::string_view::npos) {
        return fail("'{}' is not a lab case; write <lab>:<number>, e.g. rendering:3", spec);
    }
    const std::string key(spec.substr(0, colon));
    const std::string numberText(spec.substr(colon + 1));
    if (!findLab(key)) {
        return fail("'{}' is not a lab ({})", key, labKeys());
    }
    int number = 0;
    try {
        std::size_t consumed = 0;
        number = std::stoi(numberText, &consumed);
        if (consumed != numberText.size()) {
            throw std::invalid_argument("trailing");
        }
    } catch (const std::exception&) {
        return fail("'{}' is not a case number", numberText);
    }
    const std::filesystem::path file = root / caseFilePath(key);
    if (!std::filesystem::is_regular_file(file)) {
        // The lab exists and has no cases yet. Say which, because "file not found" sends somebody
        // looking for a typo in a path that is correct.
        return fail("the {} lab has no case file yet (expected {})", key, file.string());
    }
    auto cases = loadCases(file);
    if (!cases) {
        return std::unexpected(cases.error());
    }
    return findCase(*cases, number);
}

std::string reproduceCommand(const LabCase& c) {
    // Deliberately built from the case's own fields and not from a stored string: a command line
    // saved beside a configuration is a second copy of it, and the two diverge the first time
    // somebody edits one.
    // The flag the application would have used for this fixture, not a flag that happens to be
    // right for most of them: `Application` routes a `.scene.json` to `--composition` and anything
    // else to `--project`, and the printed command has to be the one that works. Found by the
    // Shadow Lab, whose fixture is the first composition a case has named -- `--project` on a
    // composition is a command that reproduces nothing.
    const bool composition = c.fixture.size() > 11 && c.fixture.compare(c.fixture.size() - 11, 11, ".scene.json") == 0;
    std::string cmd =
        fmt::format("avgen --headless {} {}", composition ? "--composition" : "--project", c.fixture);
    cmd += fmt::format(" --size {}x{} --fps {:g}", c.width, c.height, c.fps);
    if (c.supersample != 1.0) {
        cmd += fmt::format(" --supersample {:g}", c.supersample);
    }
    if (!c.tier.empty()) {
        cmd += fmt::format(" --tier {}", c.tier);
    }
    if (!c.disable.empty()) {
        cmd += fmt::format(" --disable {}", joinList(c.disable));
    }
    if (!c.qualityArms.empty()) {
        cmd += fmt::format(" --quality-arm {}", joinList(c.qualityArms));
    }
    if (!c.aovs.empty()) {
        cmd += fmt::format(" --aov {}", joinList(c.aovs));
    }
    // The time is a range of one frame, which is what `--range` means and what makes this a
    // reproduction of the case rather than of the whole fixture.
    cmd += fmt::format(" --range {:g}:{:g}", c.timeSeconds, c.timeSeconds);
    return cmd;
}

} // namespace avgen::labs
