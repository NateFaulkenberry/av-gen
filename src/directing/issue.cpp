#include "directing/issue.hpp"

#include <algorithm>
#include <array>
#include <utility>

namespace avgen::directing {
namespace {

constexpr std::array<std::pair<Severity, const char*>, 3> kSeverities{{
    {Severity::Info, "info"},
    {Severity::Warning, "warning"},
    {Severity::Error, "error"},
}};

constexpr std::array<std::pair<IssueCode, const char*>, 20> kCodes{{
    {IssueCode::SchemaInvalid, "SCHEMA_INVALID"},
    {IssueCode::SchemaUnknownField, "SCHEMA_UNKNOWN_FIELD"},
    {IssueCode::SchemaVersionUnsupported, "SCHEMA_VERSION_UNSUPPORTED"},
    {IssueCode::DuplicateKey, "DUPLICATE_KEY"},
    {IssueCode::UnknownSubject, "UNKNOWN_SUBJECT"},
    {IssueCode::AmbiguousReference, "AMBIGUOUS_REFERENCE"},
    {IssueCode::UndeclaredSubject, "UNDECLARED_SUBJECT"},
    {IssueCode::MalformedTime, "MALFORMED_TIME"},
    {IssueCode::UnresolvableTime, "UNRESOLVABLE_TIME"},
    {IssueCode::AmbiguousTime, "AMBIGUOUS_TIME"},
    {IssueCode::TimeOutOfRange, "TIME_OUT_OF_RANGE"},
    {IssueCode::CapabilityUnavailable, "CAPABILITY_UNAVAILABLE"},
    {IssueCode::Unsupported, "UNSUPPORTED"},
    {IssueCode::SpatialInfeasible, "SPATIAL_INFEASIBLE"},
    {IssueCode::TimingConflict, "TIMING_CONFLICT"},
    {IssueCode::CameraConflict, "CAMERA_CONFLICT"},
    {IssueCode::NonDeterministic, "NON_DETERMINISTIC"},
    {IssueCode::UnknownEvent, "UNKNOWN_EVENT"},
    {IssueCode::HandEdited, "HAND_EDITED"},
    {IssueCode::Blocked, "BLOCKED"},
}};
// Every code has a row: the table is indexed by nothing, so a code added without one would name
// itself SCHEMA_INVALID. The last enumerator is checked here, and the round-trip test walks them all.
static_assert(kCodes.back().first == kLastIssueCode);

template <typename E, std::size_t N>
const char* nameIn(const std::array<std::pair<E, const char*>, N>& table, E value) {
    for (const auto& [e, n] : table) {
        if (e == value) {
            return n;
        }
    }
    return table[0].second;
}

template <typename E, std::size_t N>
std::optional<E> valueIn(const std::array<std::pair<E, const char*>, N>& table, std::string_view name) {
    for (const auto& [e, n] : table) {
        if (name == n) {
            return e;
        }
    }
    return std::nullopt;
}

} // namespace

const char* severityName(Severity severity) { return nameIn(kSeverities, severity); }
std::optional<Severity> severityFromName(std::string_view name) { return valueIn(kSeverities, name); }
const char* issueCodeName(IssueCode code) { return nameIn(kCodes, code); }
std::optional<IssueCode> issueCodeFromName(std::string_view name) { return valueIn(kCodes, name); }

nlohmann::json Issue::toJson() const {
    nlohmann::json j{{"severity", severityName(severity)},
                     {"code", issueCodeName(code)},
                     {"message", message},
                     {"recoverable", recoverable}};
    if (!subject.empty()) {
        j["subject"] = subject;
    }
    if (!location.empty()) {
        j["location"] = location;
    }
    if (!item.empty()) {
        j["item"] = item;
    }
    if (!cause.empty()) {
        j["cause"] = cause;
    }
    if (!details.empty()) {
        j["details"] = details;
    }
    if (!suggestions.empty()) {
        j["suggestions"] = suggestions;
    }
    return j;
}

std::optional<Issue> Issue::fromJson(const nlohmann::json& j) {
    if (!j.is_object()) {
        return std::nullopt;
    }
    const auto severity = severityFromName(j.value("severity", std::string{}));
    const auto code = issueCodeFromName(j.value("code", std::string{}));
    if (!severity || !code) {
        return std::nullopt;
    }
    Issue out;
    out.severity = *severity;
    out.code = *code;
    out.message = j.value("message", std::string{});
    out.subject = j.value("subject", std::string{});
    out.location = j.value("location", std::string{});
    out.item = j.value("item", std::string{});
    out.cause = j.value("cause", std::string{});
    out.recoverable = j.value("recoverable", true);
    out.details = j.value("details", nlohmann::json::object());
    out.suggestions = j.value("suggestions", std::vector<std::string>{});
    return out;
}

bool hasErrors(const std::vector<Issue>& issues) {
    return std::any_of(issues.begin(), issues.end(),
                       [](const Issue& i) { return i.severity == Severity::Error; });
}

} // namespace avgen::directing
