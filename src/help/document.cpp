#include "help/document.hpp"

#include <algorithm>
#include <cctype>

namespace avgen::help {

std::string_view helpStatusName(HelpStatus status) {
    switch (status) {
    case HelpStatus::Stable:
        return "stable";
    case HelpStatus::Partial:
        return "partial";
    case HelpStatus::NotYetDocumented:
        return "not-yet-documented";
    }
    return "stable";
}

bool parseHelpStatus(std::string_view name, HelpStatus& out) {
    if (name == "stable") {
        out = HelpStatus::Stable;
        return true;
    }
    if (name == "partial") {
        out = HelpStatus::Partial;
        return true;
    }
    if (name == "not-yet-documented" || name == "gap") {
        out = HelpStatus::NotYetDocumented;
        return true;
    }
    return false;
}

std::string_view helpAudienceName(HelpAudience audience) {
    switch (audience) {
    case HelpAudience::Everyone:
        return "everyone";
    case HelpAudience::Beginner:
        return "beginner";
    case HelpAudience::Expert:
        return "expert";
    }
    return "everyone";
}

bool parseHelpAudience(std::string_view name, HelpAudience& out) {
    if (name == "everyone" || name == "all") {
        out = HelpAudience::Everyone;
        return true;
    }
    if (name == "beginner" || name == "new") {
        out = HelpAudience::Beginner;
        return true;
    }
    if (name == "expert" || name == "advanced" || name == "reference") {
        out = HelpAudience::Expert;
        return true;
    }
    return false;
}

std::string slugify(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    bool pendingHyphen = false;
    for (const char c : text) {
        const auto u = static_cast<unsigned char>(c);
        if (std::isalnum(u) != 0) {
            if (pendingHyphen && !out.empty()) {
                out.push_back('-');
            }
            pendingHyphen = false;
            out.push_back(static_cast<char>(std::tolower(u)));
        } else {
            pendingHyphen = true;
        }
    }
    return out;
}

std::string flattenSpans(const std::vector<InlineSpan>& spans) {
    std::string out;
    for (const InlineSpan& span : spans) {
        out += span.text;
    }
    return out;
}

} // namespace avgen::help
