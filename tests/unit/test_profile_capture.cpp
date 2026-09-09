// Profiling capture (ADR-031): summaries, the bounded ring and the JSON report.

#include "app/profile_capture.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>

using namespace avgen;
using Catch::Matchers::WithinAbs;

TEST_CASE("Profile summaries are percentiles over the recorded column", "[profile]") {
    const auto s = app::summarise({1.0, 2.0, 3.0, 4.0, 5.0});
    CHECK_THAT(s.min, WithinAbs(1.0, 1e-9));
    CHECK_THAT(s.max, WithinAbs(5.0, 1e-9));
    CHECK_THAT(s.mean, WithinAbs(3.0, 1e-9));
    CHECK_THAT(s.p50, WithinAbs(3.0, 1e-9));
    CHECK_THAT(s.p95, WithinAbs(4.8, 1e-9));
    const auto empty = app::summarise({});
    CHECK_THAT(empty.mean, WithinAbs(0.0, 1e-9));
    const auto one = app::summarise({7.0});
    CHECK_THAT(one.p99, WithinAbs(7.0, 1e-9));
}

TEST_CASE("The capture records only while running and keeps a bounded ring", "[profile]") {
    app::ProfileCapture capture;
    app::ProfileSample sample;
    sample.cpuFrameMs = 4.0;
    capture.add(sample); // not recording
    CHECK(capture.size() == 0);
    capture.start("test");
    CHECK(capture.recording());
    capture.maxSamples = 4;
    for (int i = 0; i < 10; ++i) {
        sample.frame = static_cast<std::uint64_t>(i);
        sample.renderTime = i * 0.1;
        sample.cpuFrameMs = 1.0 + i;
        capture.add(sample);
    }
    CHECK(capture.size() == 4);
    CHECK(capture.samples().front().frame == 6); // oldest dropped
    CHECK(capture.samples().back().frame == 9);
    capture.stop();
    capture.add(sample);
    CHECK(capture.size() == 4);
}

TEST_CASE("The report carries summaries and every sample", "[profile]") {
    app::ProfileCapture capture;
    capture.start("session");
    for (int i = 0; i < 5; ++i) {
        app::ProfileSample s;
        s.frame = static_cast<std::uint64_t>(i);
        s.renderTime = i * 0.5;
        s.cpuFrameMs = 2.0 + i;
        s.gpuFrameMs = i == 0 ? -1.0 : 1.0 + i; // unavailable frames are excluded
        s.instances = 1000u * static_cast<std::uint64_t>(i);
        s.drawCalls = 10;
        capture.add(s);
    }
    const nlohmann::json j = capture.toJson();
    CHECK(j.at("label") == "session");
    CHECK(j.at("frames") == 5);
    CHECK_THAT(j.at("duration").get<double>(), WithinAbs(2.0, 1e-9));
    CHECK_THAT(j.at("summary").at("cpuFrameMs").at("mean").get<double>(), WithinAbs(4.0, 1e-9));
    CHECK_THAT(j.at("summary").at("gpuFrameMs").at("min").get<double>(), WithinAbs(2.0, 1e-9));
    CHECK_THAT(j.at("summary").at("drawCalls").at("max").get<double>(), WithinAbs(10.0, 1e-9));
    REQUIRE(j.at("samples").is_array());
    CHECK(j.at("samples").size() == 5);
    CHECK(j.at("samples")[4].at("instances") == 4000);

    const auto path = std::filesystem::temp_directory_path() / "avgen_profile_report.json";
    REQUIRE(capture.writeFile(path).has_value());
    std::ifstream in(path);
    REQUIRE(in.good());
    const nlohmann::json loaded = nlohmann::json::parse(in);
    CHECK(loaded.at("frames") == 5);
    std::filesystem::remove(path);
}
